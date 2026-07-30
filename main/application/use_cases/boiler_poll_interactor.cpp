#include "application/use_cases/boiler_poll_interactor.h"
#include "application/ports/driven/iboiler_hardware.h"
#include "application/ports/driven/iheating_state_store.h"
#include "application/ports/driven/ilogger.h"
#include "application/ports/driven/itime_source.h"
#include "application/services/dhw_hysteresis_service.h"
#include "domain/value_objects/fault_codes.h"
#include <cmath>

// OpenTherm data IDs (mirrored from opentherm.h to avoid dependency)
enum {
    OT_ID_STATUS           = 0,
    OT_ID_CH_SETPOINT      = 1,
    OT_ID_MASTER_CONFIG    = 2,
    OT_ID_SLAVE_CONFIG     = 3,
    OT_ID_ASF_FLAGS        = 5,
    OT_ID_CH2_SETPOINT     = 8,
    OT_ID_MODULATION       = 17,
    OT_ID_CH_TEMP          = 25,
    OT_ID_DHW_TEMP         = 26,
    OT_ID_RETURN_TEMP      = 28,
    OT_ID_DHW_BOUNDS       = 48,
    OT_ID_CH_BOUNDS        = 49,
    OT_ID_DHW_SETPOINT     = 56,
    OT_ID_MAX_CH_SETPOINT  = 57,
    OT_ID_OUTSIDE_TEMP     = 27,
    OT_ID_OEM_DIAGNOSTIC   = 115,
    OT_ID_BURNER_STARTS    = 116,
    OT_ID_CH_PUMP_STARTS   = 117,
    OT_ID_DHW_VALVE_STARTS = 118,
    OT_ID_DHW_BURNER_STARTS= 119,
    OT_ID_BURNER_HOURS     = 120,
    OT_ID_CH_PUMP_HOURS    = 121,
    OT_ID_DHW_VALVE_HOURS  = 122,
    OT_ID_DHW_BURNER_HOURS = 123,
    OT_ID_OT_VERSION_S     = 124,
    OT_ID_SLAVE_VERSION    = 125,
    OT_ID_MASTER_VERSION   = 126,
};

enum {
    MASTER_CH_ENABLE  = 1 << 0,
    MASTER_DHW_ENABLE = 1 << 1,
    MASTER_CH2_ENABLE = 1 << 4,
};

enum {
    SLAVE_FAULT      = 1 << 0,
    SLAVE_CH_ACTIVE  = 1 << 1,
    SLAVE_DHW_ACTIVE = 1 << 2,
    SLAVE_FLAME      = 1 << 3,
};

static constexpr uint32_t HANDSHAKE_INTERVAL_MS = 3600000;
static constexpr uint32_t HANDSHAKE_RETRY_MS    = 10000;   // retry failed handshake after 10s
static constexpr uint32_t CONNECTIVITY_TIMEOUT_MS = 10000;

BoilerPollInteractor::BoilerPollInteractor(IBoilerHardware& boiler, IHeatingStateStore& state, ILogger& log, ITimeSource& time)
    : boiler_(boiler), state_(state), log_(log), time_(time)
{
}

uint32_t BoilerPollInteractor::now_ms() const
{
    return static_cast<uint32_t>(time_.monotonic_ms());
}

float BoilerPollInteractor::f88_to_float(uint16_t v)
{
    return static_cast<float>(v) / 256.0f;
}

uint16_t BoilerPollInteractor::float_to_f88(float f)
{
    if (f < 0) return 0;
    if (f > 255.99f) return 0xFFFF;
    return static_cast<uint16_t>(f * 256.0f);
}

void BoilerPollInteractor::set_ch_enable(bool en)
{
    pending_ch_enable_ = true;
    pending_ch_enable_val_ = en;
}

void BoilerPollInteractor::set_dhw_enable(bool en)
{
    pending_dhw_enable_ = true;
    pending_dhw_enable_val_ = en;
}

void BoilerPollInteractor::set_ch_setpoint(float sp)
{
    pending_ch_sp_ = sp;
    pending_ch_sp_dirty_ = true;
}

void BoilerPollInteractor::set_dhw_setpoint(float sp)
{
    pending_dhw_sp_ = sp;
    pending_dhw_sp_dirty_ = true;
}

void BoilerPollInteractor::set_dhw_hysteresis(float hyst)
{
    state_.lock_exclusive();
    state_.set_dhw_hysteresis(hyst);
    state_.unlock_exclusive();
}

void BoilerPollInteractor::trigger_fault_reset()
{
    pending_fault_reset_ = true;
}

// ================================================================
// Main poll cycle
// ================================================================

void BoilerPollInteractor::execute()
{
    do_handshake();
    do_dhw_hysteresis();
    do_status();
    do_extra_step();
    check_connectivity();

    // Log fault state changes with code and ASF description
    state_.lock_shared();
    bool cur_fault = state_.has_fault();
    state_.unlock_shared();
    if (cur_fault != last_fault_) {
        if (cur_fault) {
            // Force-read ASF flags immediately to get OEM code and description
            uint8_t oem = 0, asf = 0;
            auto r = boiler_.read(OT_ID_ASF_FLAGS);
            if (r.success && !std::isnan(r.value_f88)) {
                uint16_t raw = float_to_f88(r.value_f88);
                asf = static_cast<uint8_t>((raw >> 8) & 0xFF);
                oem = static_cast<uint8_t>(raw & 0xFF);
                state_.lock_exclusive();
                state_.set_fault_codes(asf, oem, state_.get_oem_diagnostic());
                state_.unlock_exclusive();
            }
            char asf_buf[128];
            FaultCodes::asf_flags_text(asf, asf_buf, sizeof(asf_buf));
            if (oem != 0) {
                log_.event(ILogger::EQUIP, "АВАРИЯ: код %d, %s", oem, asf_buf);
            } else {
                log_.event(ILogger::EQUIP, "АВАРИЯ: %s", asf_buf);
            }
        } else {
            log_.event(ILogger::EQUIP, "Авария сброшена, ошибок нет");
        }
        last_fault_ = cur_fault;
    }


    // Dual-write bridge REMOVED — caused data race with HTTP task (no mutex on Model)
    // Web reads Model::to_json() from its own fields (updated by SystemConfigInteractor)
    // OT state is written to HeatingStateAdapter only
}

// ================================================================
// Handshake — exchange version/config info
// ================================================================

void BoilerPollInteractor::do_handshake()
{
    uint32_t now = now_ms();
    if (handshake_done_ && (now - last_handshake_ms_ <= HANDSHAKE_INTERVAL_MS))
        return;

    // Retry failed handshake after HANDSHAKE_RETRY_MS (10s), not 1 hour
    if (handshake_attempted_ && !handshake_done_ &&
        (now - last_handshake_ms_ <= HANDSHAKE_RETRY_MS))
        return;

    handshake_attempted_ = true;
    last_handshake_ms_ = now;
    bool any_success = false;

    auto r = boiler_.read(OT_ID_SLAVE_VERSION);
    if (r.success) {
        any_success = true;
        uint16_t v = float_to_f88(r.value_f88);
        uint8_t slave_type = static_cast<uint8_t>(v >> 8);
        uint8_t slave_ver  = static_cast<uint8_t>(v & 0xFF);
        state_.lock_exclusive();
        state_.set_version(slave_type, slave_ver, 0);
        state_.unlock_exclusive();
    }

    boiler_.write(OT_ID_MASTER_VERSION, 0x013F);

    auto cfg = boiler_.read(OT_ID_SLAVE_CONFIG);
    if (cfg.success) {
        any_success = true;
        uint16_t cv = float_to_f88(cfg.value_f88);
        boiler_.write(OT_ID_MASTER_CONFIG, cv);
    }

    auto otv = boiler_.read(OT_ID_OT_VERSION_S);
    if (otv.success) {
        any_success = true;
        state_.lock_exclusive();
        state_.set_version(state_.get_slave_type(), state_.get_slave_version(), otv.value_f88);
        state_.unlock_exclusive();
    }

    if (any_success) {
        handshake_done_ = true;
        log_.event(ILogger::SYSTEM, "OT рукопожатие завершено");
    }
    // If no success, handshake_done_ stays false → retry in 10s
}

// ================================================================
// DHW hysteresis — toggles DHW priority flag
// ================================================================

void BoilerPollInteractor::do_dhw_hysteresis()
{
    state_.lock_shared();
    bool  dhw_enabled = state_.is_dhw_enabled();
    float dhw_temp    = state_.get_dhw_temp();
    float dhw_setpoint = state_.get_dhw_setpoint();
    float hysteresis   = state_.get_dhw_hysteresis();
    state_.unlock_shared();

    if (dhw_enabled && dhw_temp > 5.0f) {
        float hyst = hysteresis;
        if (hyst < 0.5f) hyst = 2.0f;

        if (!dhw_priority_ && DHWHysteresisService::should_heat(dhw_temp, dhw_setpoint, hyst, dhw_enabled)) {
            dhw_priority_ = true;
            dhw_session_start_ms_ = now_ms();
            dhw_session_min_temp_ = dhw_temp;
            log_.event(ILogger::MODE, "ГВС нагрев начат с %.1f C", (double)dhw_temp);
        } else if (dhw_priority_ && DHWHysteresisService::should_stop(dhw_temp, dhw_setpoint)) {
            dhw_priority_ = false;
            uint32_t dur = now_ms() - dhw_session_start_ms_;
            state_.lock_exclusive();
            state_.set_dhw_session_finished(dur, dhw_session_min_temp_);
            state_.unlock_exclusive();
            log_.event(ILogger::MODE, "ГВС нагрев завершён: %lu с, Tмин %.1f C",
                       (unsigned long)(dur / 1000), (double)dhw_session_min_temp_);
            dhw_session_start_ms_ = 0;
        }
    } else {
        dhw_priority_ = false;
    }
}

// ================================================================
// Status — read/write boiler status flags
// ================================================================

uint8_t BoilerPollInteractor::build_master_byte()
{
    uint8_t m = 0;

    // Use pending value if set, otherwise read current state (e.g. from NVS load)
    bool ch_en = pending_ch_enable_ ? pending_ch_enable_val_ : state_.is_ch_enabled();
    bool dhw_en = pending_dhw_enable_ ? pending_dhw_enable_val_ : state_.is_dhw_enabled();

    if (ch_en) m |= MASTER_CH_ENABLE;
    if (dhw_en && dhw_priority_) {
        m |= MASTER_DHW_ENABLE;
        m |= MASTER_CH2_ENABLE;
    }
    return m;
}

void BoilerPollInteractor::do_status()
{
    uint8_t m = build_master_byte();
    bool fr = pending_fault_reset_;
    pending_fault_reset_ = false;

    auto r = boiler_.read_status(m, fr);

    if (r.success) {
        uint16_t raw = float_to_f88(r.value_f88);
        uint8_t sl = static_cast<uint8_t>(raw & 0xFF);

        bool fault      = (sl & SLAVE_FAULT)      != 0;
        bool ch_active  = (sl & SLAVE_CH_ACTIVE)  != 0;
        bool dhw_active = (sl & SLAVE_DHW_ACTIVE) != 0;
        bool flame      = (sl & SLAVE_FLAME)       != 0;

        state_.lock_exclusive();
        state_.set_fault(fault);
        state_.set_flame(flame);
        state_.set_ch_active(ch_active);
        state_.set_dhw_active(dhw_active);

        // Consume pending enable flags — write to state and clear
        if (pending_ch_enable_) {
            state_.set_ch_enable(pending_ch_enable_val_);
            pending_ch_enable_ = false;
        }
        if (pending_dhw_enable_) {
            state_.set_dhw_enable(pending_dhw_enable_val_);
            pending_dhw_enable_ = false;
        }
        state_.unlock_exclusive();

        last_response_ms_ = now_ms();

        if (!connected_) {
            connected_ = true;
            state_.lock_exclusive();
            state_.set_connected(true);
            state_.unlock_exclusive();
            log_.event(ILogger::SYSTEM, "Котёл подключён");
        }

        // Track state changes for connectivity monitoring and polling logic
        last_flame_ = flame;
        last_ch_active_ = ch_active;
        last_dhw_active_ = dhw_active;
    }
}

// ================================================================
// Extra step — 18-step round-robin for all data IDs
// ================================================================

void BoilerPollInteractor::do_extra_step()
{
    switch (poll_step_) {
    case 0:
        boiler_.write(OT_ID_MAX_CH_SETPOINT, float_to_f88(80.0f));
        break;
    case 9: {
        auto r = boiler_.read(OT_ID_ASF_FLAGS);
        if (r.success && !std::isnan(r.value_f88)) {
            uint16_t raw = float_to_f88(r.value_f88);
            uint8_t asf = static_cast<uint8_t>((raw >> 8) & 0xFF);
            uint8_t oem = static_cast<uint8_t>(raw & 0xFF);
            state_.lock_exclusive();
            state_.set_fault_codes(asf, oem, state_.get_oem_diagnostic());
            state_.unlock_exclusive();
        }
        break;
    }
    case 1: case 10: {
        float sp;
        if (pending_ch_sp_dirty_) {
            sp = pending_ch_sp_;
        } else {
            state_.lock_shared();
            sp = state_.get_ch_setpoint();
            state_.unlock_shared();
        }
        auto w = boiler_.write(OT_ID_CH_SETPOINT, float_to_f88(sp));
        if (w.success) {
            pending_ch_sp_dirty_ = false;
            state_.lock_exclusive();
            state_.set_ch_setpoint(sp);
            state_.unlock_exclusive();
        }
        break;
    }
    case 2: case 8: case 12: {
        auto r = boiler_.read(OT_ID_DHW_TEMP);
        if (r.success && !std::isnan(r.value_f88)) {
            state_.lock_exclusive();
            state_.set_dhw_temp(r.value_f88);
            state_.unlock_exclusive();
        }
        break;
    }
    case 4: {
        auto r = boiler_.read(OT_ID_OUTSIDE_TEMP);
        if (r.success && !std::isnan(r.value_f88)) {
            state_.lock_exclusive();
            state_.set_outside_temp(r.value_f88);
            state_.unlock_exclusive();
        }
        break;
    }
    case 3: case 11: {
        float sp, sp_max;
        if (pending_dhw_sp_dirty_) {
            sp = pending_dhw_sp_;
            state_.lock_shared();
            sp_max = state_.get_dhw_sp_max();
            state_.unlock_shared();
        } else {
            state_.lock_shared();
            sp = state_.get_dhw_setpoint();
            sp_max = state_.get_dhw_sp_max();
            state_.unlock_shared();
        }
        if (sp_max > 0 && sp > sp_max) sp = sp_max;
        auto w = boiler_.write(OT_ID_DHW_SETPOINT, float_to_f88(sp));
        if (w.success) {
            pending_dhw_sp_dirty_ = false;
            state_.lock_exclusive();
            state_.set_dhw_setpoint(sp);
            state_.unlock_exclusive();
        }
        break;
    }
    case 6: {
        auto r = boiler_.read(OT_ID_OEM_DIAGNOSTIC);
        if (r.success && !std::isnan(r.value_f88)) {
            uint16_t raw = float_to_f88(r.value_f88);
            state_.lock_exclusive();
            state_.set_fault_codes(state_.get_asf_flags(), state_.get_oem_fault_code(), raw);
            state_.unlock_exclusive();
        }
        break;
    }
    case 5: case 14: case 17: {
        auto r = boiler_.read(OT_ID_MODULATION);
        if (r.success && !std::isnan(r.value_f88)) {
            float m = r.value_f88;
            state_.lock_exclusive();
            state_.set_modulation(m);
            state_.unlock_exclusive();
        }
        break;
    }
    case 7: {
        auto r = boiler_.read(OT_ID_CH_TEMP);
        if (r.success && !std::isnan(r.value_f88)) {
            state_.lock_exclusive();
            state_.set_ch_temp(r.value_f88);
            state_.unlock_exclusive();
        }
        break;
    }
    case 13: {
        auto r = boiler_.read(OT_ID_RETURN_TEMP);
        if (r.success && !std::isnan(r.value_f88)) {
            state_.lock_exclusive();
            state_.set_return_temp(r.value_f88);
            state_.unlock_exclusive();
        }
        break;
    }
    case 15: {
        auto r = boiler_.read(OT_ID_DHW_BOUNDS);
        if (r.success && !std::isnan(r.value_f88)) {
            uint16_t raw = float_to_f88(r.value_f88);
            float hi = static_cast<float>((raw >> 8) & 0xFF);
            float lo = static_cast<float>(raw & 0xFF);
            if (hi > lo && hi > 0) {
                state_.lock_exclusive();
                state_.set_dhw_sp_min(lo);
                state_.set_dhw_sp_max(hi);
                state_.unlock_exclusive();
            }
        }
        break;
    }
    case 16: {
        auto r = boiler_.read(OT_ID_CH_BOUNDS);
        if (r.success && !std::isnan(r.value_f88)) {
            uint16_t raw = float_to_f88(r.value_f88);
            float hi = static_cast<float>((raw >> 8) & 0xFF);
            float lo = static_cast<float>(raw & 0xFF);
            if (hi > lo && hi > 0) {
                state_.lock_exclusive();
                state_.set_ch_sp_min(lo);
                state_.set_ch_sp_max(hi);
                state_.unlock_exclusive();
            }
        }
        break;
    }
    // Runtime counters — read once per 24-step cycle (~26s)
    case 18: {
        auto r = boiler_.read(OT_ID_BURNER_STARTS);
        if (r.success && !std::isnan(r.value_f88)) {
            uint16_t v = float_to_f88(r.value_f88);
            state_.lock_exclusive();
            state_.set_runtime_counters(v, state_.get_ch_pump_starts(),
                                         state_.get_dhw_valve_starts(), state_.get_dhw_burner_starts());
            state_.unlock_exclusive();
        }
        break;
    }
    case 19: {
        auto r = boiler_.read(OT_ID_CH_PUMP_STARTS);
        if (r.success && !std::isnan(r.value_f88)) {
            uint16_t v = float_to_f88(r.value_f88);
            state_.lock_exclusive();
            state_.set_runtime_counters(state_.get_burner_starts(), v,
                                         state_.get_dhw_valve_starts(), state_.get_dhw_burner_starts());
            state_.unlock_exclusive();
        }
        break;
    }
    case 20: {
        auto r = boiler_.read(OT_ID_DHW_VALVE_STARTS);
        if (r.success && !std::isnan(r.value_f88)) {
            uint16_t v = float_to_f88(r.value_f88);
            state_.lock_exclusive();
            state_.set_runtime_counters(state_.get_burner_starts(), state_.get_ch_pump_starts(),
                                         v, state_.get_dhw_burner_starts());
            state_.unlock_exclusive();
        }
        break;
    }
    case 21: {
        auto r = boiler_.read(OT_ID_DHW_BURNER_STARTS);
        if (r.success && !std::isnan(r.value_f88)) {
            uint16_t v = float_to_f88(r.value_f88);
            state_.lock_exclusive();
            state_.set_runtime_counters(state_.get_burner_starts(), state_.get_ch_pump_starts(),
                                         state_.get_dhw_valve_starts(), v);
            state_.unlock_exclusive();
        }
        break;
    }
    case 22: {
        auto r = boiler_.read(OT_ID_BURNER_HOURS);
        if (r.success && !std::isnan(r.value_f88)) {
            uint16_t v = float_to_f88(r.value_f88);
            state_.lock_exclusive();
            state_.set_runtime_hours(v, state_.get_ch_pump_hours(),
                                      state_.get_dhw_valve_hours(), state_.get_dhw_burner_hours());
            state_.unlock_exclusive();
        }
        break;
    }
    case 23: {
        auto r = boiler_.read(OT_ID_CH_PUMP_HOURS);
        if (r.success && !std::isnan(r.value_f88)) {
            uint16_t v = float_to_f88(r.value_f88);
            state_.lock_exclusive();
            state_.set_runtime_hours(state_.get_burner_hours(), v,
                                      state_.get_dhw_valve_hours(), state_.get_dhw_burner_hours());
            state_.unlock_exclusive();
        }
        break;
    }
    case 24: {
        auto r = boiler_.read(OT_ID_DHW_VALVE_HOURS);
        if (r.success && !std::isnan(r.value_f88)) {
            uint16_t v = float_to_f88(r.value_f88);
            state_.lock_exclusive();
            state_.set_runtime_hours(state_.get_burner_hours(), state_.get_ch_pump_hours(),
                                      v, state_.get_dhw_burner_hours());
            state_.unlock_exclusive();
        }
        break;
    }
    case 25: {
        auto r = boiler_.read(OT_ID_DHW_BURNER_HOURS);
        if (r.success && !std::isnan(r.value_f88)) {
            uint16_t v = float_to_f88(r.value_f88);
            state_.lock_exclusive();
            state_.set_runtime_hours(state_.get_burner_hours(), state_.get_ch_pump_hours(),
                                      state_.get_dhw_valve_hours(), v);
            state_.unlock_exclusive();
        }
        break;
    }
    default:
        poll_step_ = 0;
        break;
    }

    poll_step_++;
    if (poll_step_ >= 26) poll_step_ = 0;
}

// ================================================================
// Connectivity check — detect OT link loss
// ================================================================

void BoilerPollInteractor::check_connectivity()
{
    if (now_ms() - last_response_ms_ > CONNECTIVITY_TIMEOUT_MS && connected_) {
        connected_ = false;
        log_.event(ILogger::EQUIP, "Котёл отключён (>10с)");
        state_.lock_exclusive();
        state_.set_connected(false);
        state_.unlock_exclusive();
    }
}
