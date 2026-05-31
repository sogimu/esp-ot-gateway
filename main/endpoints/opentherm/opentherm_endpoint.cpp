#include "opentherm_endpoint.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstring>

static const char* TAG = "ot_endpoint";

OpenthermEndpoint::OpenthermEndpoint()
    : task_(nullptr), running_(false)
    , pending_ch_enable_(true), pending_ch_enable_val_(true)
    , pending_dhw_enable_(true), pending_dhw_enable_val_(true)
    , pending_fault_reset_(false)
    , pending_ch_sp_(30.0f), pending_ch_sp_dirty_(true)
    , pending_dhw_sp_(55.0f), pending_dhw_sp_dirty_(true)
    , poll_step_(0), handshake_done_(false)
    , last_handshake_ms_(0), last_response_ms_(0)
    , connected_(false)
    , dhw_priority_(false), dhw_session_start_ms_(0), dhw_session_min_temp_(0)
    , last_fault_(false), last_flame_(false)
    , last_ch_active_(false), last_dhw_active_(false)
{
    std::memset(&ot_, 0, sizeof(ot_));
    ot_.ch_setpoint = 30.0f;
    ot_.dhw_setpoint = 55.0f;
    ot_.ch_enable = true;
    ot_.dhw_enable = true;
    ot_.t1_temp = -127.0f;
    ot_.t2_temp = -127.0f;
}

OpenthermEndpoint::~OpenthermEndpoint()
{
    stop();
}

void OpenthermEndpoint::start()
{
    if (running_) return;
    OT_Init();
    running_ = true;
    xTaskCreate(task_wrapper, "ot_poll", 4096, this, 5, &task_);
}

void OpenthermEndpoint::stop()
{
    if (!running_) return;
    running_ = false;
    if (task_) {
        vTaskDelete(task_);
        task_ = nullptr;
    }
}

void OpenthermEndpoint::set_ch_enable(bool enable)
{
    pending_ch_enable_ = true;
    pending_ch_enable_val_ = enable;
}

void OpenthermEndpoint::set_dhw_enable(bool enable)
{
    pending_dhw_enable_ = true;
    pending_dhw_enable_val_ = enable;
}

void OpenthermEndpoint::set_ch_setpoint(float temp)
{
    pending_ch_sp_ = temp;
    pending_ch_sp_dirty_ = true;
}

void OpenthermEndpoint::set_dhw_setpoint(float temp)
{
    pending_dhw_sp_ = temp;
    pending_dhw_sp_dirty_ = true;
}

void OpenthermEndpoint::trigger_fault_reset()
{
    pending_fault_reset_ = true;
}

void OpenthermEndpoint::subscribe(IOpenthermObserver* obs)
{
    for (auto* o : observers_) {
        if (o == obs) return;
    }
    observers_.push_back(obs);
}

void OpenthermEndpoint::unsubscribe(IOpenthermObserver* obs)
{
    for (auto it = observers_.begin(); it != observers_.end(); ++it) {
        if (*it == obs) { observers_.erase(it); return; }
    }
}

void OpenthermEndpoint::task_wrapper(void* arg)
{
    auto* self = static_cast<OpenthermEndpoint*>(arg);
    self->task_loop();
}

void OpenthermEndpoint::task_loop()
{
    ESP_LOGI(TAG, "Opentherm poll task started");
    while (running_) {
        poll_cycle();
        vTaskDelay(pdMS_TO_TICKS(OT_POLL_INTERVAL_MS));
    }
    vTaskDelete(nullptr);
}

void OpenthermEndpoint::poll_cycle()
{
    do_handshake();
    do_dhw_hysteresis();
    do_status();
    do_extra_step();
    check_connectivity();

    if (dhw_priority_ && ot_.dhw_temp < dhw_session_min_temp_)
        dhw_session_min_temp_ = ot_.dhw_temp;
}

void OpenthermEndpoint::do_handshake()
{
    OT_Frame req = OT_Frame{}, rsp = OT_Frame{};
    bool ok;
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

    if (!handshake_done_ || (now_ms - last_handshake_ms_ > 3600000)) {
        req.msg_type = OT_MSG_READ_DATA;
        req.data_id  = OT_ID_SLAVE_VERSION;
        ok = OT_Transaction(&req, &rsp);
        if (ok) {
            ot_.slave_type    = (uint8_t)(rsp.data_value >> 8);
            ot_.slave_version = (uint8_t)(rsp.data_value & 0xFF);
        }

        req.msg_type = OT_MSG_WRITE_DATA;
        req.data_id  = OT_ID_MASTER_VERSION;
        req.data_value = 0x013F;
        OT_Transaction(&req, &rsp);

        req.msg_type = OT_MSG_READ_DATA;
        req.data_id  = OT_ID_SLAVE_CONFIG;
        req.data_value = 0;
        ok = OT_Transaction(&req, &rsp);
        if (ok) {
            uint8_t flags    = (uint8_t)(rsp.data_value >> 8);
            uint8_t member_id = (uint8_t)(rsp.data_value & 0xFF);
            req.msg_type = OT_MSG_WRITE_DATA;
            req.data_id  = OT_ID_MASTER_CONFIG;
            req.data_value = (uint16_t)((flags << 8) | member_id);
            OT_Transaction(&req, &rsp);
        }

        req.msg_type = OT_MSG_READ_DATA;
        req.data_id  = OT_ID_OT_VERSION_S;
        req.data_value = 0;
        ok = OT_Transaction(&req, &rsp);
        if (ok)
            ot_.ot_version = OT_f88_to_float(rsp.data_value);

        handshake_done_ = true;
        last_handshake_ms_ = now_ms;
    }
}

void OpenthermEndpoint::do_dhw_hysteresis()
{
    bool dhw_user_enabled = pending_dhw_enable_ ? pending_dhw_enable_val_ : false;
    if (dhw_user_enabled && ot_.dhw_temp > 5.0f) {
        if (!dhw_priority_ && ot_.dhw_temp < ot_.dhw_setpoint - DHW_HYST_ON) {
            dhw_priority_ = true;
            dhw_session_start_ms_ = (uint32_t)(esp_timer_get_time() / 1000);
            dhw_session_min_temp_ = ot_.dhw_temp;
        } else if (dhw_priority_ && ot_.dhw_temp >= ot_.dhw_setpoint) {
            dhw_priority_ = false;
            uint32_t dur = (uint32_t)(esp_timer_get_time() / 1000) - dhw_session_start_ms_;
            ot_.dhw_last_session_sec = (int)(dur / 1000);
            dhw_session_start_ms_ = 0;
            for (auto* o : observers_)
                o->on_dhw_session_finished(dur, dhw_session_min_temp_);
        }
    } else {
        dhw_priority_ = false;
    }
}

uint8_t OpenthermEndpoint::build_master_byte()
{
    uint8_t m = 0;
    bool ch_en = pending_ch_enable_ ? pending_ch_enable_val_ : false;
    bool dhw_en = pending_dhw_enable_ ? pending_dhw_enable_val_ : false;

    if (ch_en) m |= OT_MASTER_CH_ENABLE;
    if (dhw_en && dhw_priority_) m |= OT_MASTER_DHW_ENABLE;
    if (dhw_en && dhw_priority_) m |= OT_MASTER_CH2_ENABLE;
    return m;
}

void OpenthermEndpoint::do_status()
{
    OT_Frame req = OT_Frame{}, rsp = OT_Frame{};
    req.msg_type = OT_MSG_READ_DATA;
    req.data_id  = OT_ID_STATUS;

    uint8_t m = build_master_byte();
    uint8_t lb = pending_fault_reset_ ? 1 : 0;
    req.data_value = (uint16_t)((m << 8) | lb);

    bool ok = OT_Transaction(&req, &rsp);
    if (ok) {
        uint8_t sl = (uint8_t)(rsp.data_value & 0xFF);
        ot_.fault      = (sl & OT_SLAVE_FAULT)      != 0;
        ot_.ch_active  = (sl & OT_SLAVE_CH_ACTIVE)  != 0;
        ot_.dhw_active = (sl & OT_SLAVE_DHW_ACTIVE) != 0;
        ot_.flame      = (sl & OT_SLAVE_FLAME)       != 0;
        last_response_ms_ = (uint32_t)(esp_timer_get_time() / 1000);

        if (!connected_) {
            connected_ = true;
            for (auto* o : observers_) o->on_connected();
        }

        if (ot_.fault != last_fault_ || ot_.flame != last_flame_ ||
            ot_.ch_active != last_ch_active_ || ot_.dhw_active != last_dhw_active_) {
            notify_status(ot_.fault, ot_.flame, ot_.ch_active, ot_.dhw_active);
            last_fault_ = ot_.fault;
            last_flame_ = ot_.flame;
            last_ch_active_ = ot_.ch_active;
            last_dhw_active_ = ot_.dhw_active;
        }
    }
}

void OpenthermEndpoint::do_extra_step()
{
    OT_Frame req = OT_Frame{}, rsp = OT_Frame{};
    bool ok;

    switch (poll_step_) {

    case 0:
    case 9:
        req.msg_type   = OT_MSG_WRITE_DATA;
        req.data_id    = OT_ID_MAX_CH_SETPOINT;
        req.data_value = OT_float_to_f88(80.0f);
        OT_Transaction(&req, &rsp);
        break;

    case 1:
    case 10: {
        float sp = pending_ch_sp_dirty_ ? pending_ch_sp_ : ot_.ch_setpoint;
        req.msg_type   = OT_MSG_WRITE_DATA;
        req.data_id    = OT_ID_CH_SETPOINT;
        req.data_value = OT_float_to_f88(sp);
        ok = OT_Transaction(&req, &rsp);
        if (ok) {
            pending_ch_sp_dirty_ = false;
            ot_.ch_setpoint = sp;
            for (auto* o : observers_)
                o->on_ch_setpoint_confirmed(sp);
        }
        break;
    }

    case 2:
    case 4:
    case 8:
    case 12:
        req.msg_type   = OT_MSG_READ_DATA;
        req.data_id    = OT_ID_DHW_TEMP;
        ok = OT_Transaction(&req, &rsp);
        if (ok && rsp.msg_type == OT_MSG_READ_ACK) {
            float t = OT_f88_to_float(rsp.data_value);
            if (t != ot_.dhw_temp) {
                ot_.dhw_temp = t;
                for (auto* o : observers_)
                    o->on_dhw_temp(t);
            }
        }
        break;

    case 3:
    case 6:
    case 11: {
        float sp = pending_dhw_sp_dirty_ ? pending_dhw_sp_ : ot_.dhw_setpoint;
        if (ot_.dhw_setpoint_max > 0 && sp > ot_.dhw_setpoint_max)
            sp = ot_.dhw_setpoint_max;
        req.msg_type   = OT_MSG_WRITE_DATA;
        req.data_id    = OT_ID_DHW_SETPOINT;
        req.data_value = OT_float_to_f88(sp);
        ok = OT_Transaction(&req, &rsp);
        if (ok) {
            pending_dhw_sp_dirty_ = false;
            ot_.dhw_setpoint = sp;
            for (auto* o : observers_)
                o->on_dhw_setpoint_confirmed(sp);
        }
        break;
    }

    case 5:
    case 14:
    case 17:
        req.msg_type   = OT_MSG_READ_DATA;
        req.data_id    = OT_ID_MODULATION;
        ok = OT_Transaction(&req, &rsp);
        if (ok && rsp.msg_type == OT_MSG_READ_ACK) {
            float m = OT_f88_to_float(rsp.data_value);
            if (m == 0.0f && ot_.flame) m = 0.3f;
            ot_.modulation = m;
            for (auto* o : observers_)
                o->on_modulation(m);
        }
        break;

    case 7:
        req.msg_type   = OT_MSG_READ_DATA;
        req.data_id    = OT_ID_CH_TEMP;
        ok = OT_Transaction(&req, &rsp);
        if (ok && rsp.msg_type == OT_MSG_READ_ACK) {
            float t = OT_f88_to_float(rsp.data_value);
            ot_.ch_temp = t;
            for (auto* o : observers_)
                o->on_ch_temp(t);
        }
        break;

    case 13:
        req.msg_type   = OT_MSG_READ_DATA;
        req.data_id    = OT_ID_RETURN_TEMP;
        ok = OT_Transaction(&req, &rsp);
        if (ok && rsp.msg_type == OT_MSG_READ_ACK) {
            float t = OT_f88_to_float(rsp.data_value);
            ot_.return_temp = t;
            for (auto* o : observers_)
                o->on_return_temp(t);
        }
        break;

    case 15:
        req.msg_type   = OT_MSG_READ_DATA;
        req.data_id    = OT_ID_DHW_BOUNDS;
        ok = OT_Transaction(&req, &rsp);
        if (ok && rsp.msg_type == OT_MSG_READ_ACK) {
            float hi = (float)((rsp.data_value >> 8) & 0xFF);
            float lo = (float)((rsp.data_value     ) & 0xFF);
            if (hi > lo && hi > 0) {
                ot_.dhw_setpoint_max = hi;
                ot_.dhw_setpoint_min = lo;
                for (auto* o : observers_)
                    o->on_dhw_bounds(lo, hi);
            }
        }
        break;

    case 16:
        req.msg_type   = OT_MSG_READ_DATA;
        req.data_id    = OT_ID_CH_BOUNDS;
        ok = OT_Transaction(&req, &rsp);
        if (ok && rsp.msg_type == OT_MSG_READ_ACK) {
            float hi = (float)((rsp.data_value >> 8) & 0xFF);
            float lo = (float)((rsp.data_value     ) & 0xFF);
            if (hi > lo && hi > 0) {
                ot_.ch_setpoint_max = hi;
                ot_.ch_setpoint_min = lo;
                for (auto* o : observers_)
                    o->on_ch_bounds(lo, hi);
            }
        }
        break;

    default:
        poll_step_ = 0;
        break;
    }

    poll_step_++;
    if (poll_step_ >= 18) poll_step_ = 0;
}

void OpenthermEndpoint::check_connectivity()
{
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    bool was_connected = connected_;
    if (now - last_response_ms_ > 10000 && connected_) {
        connected_ = false;
        ESP_LOGW(TAG, "Котёл не отвечает > 10 сек");
        notify_all();
    }
    if (connected_ != was_connected) {
        for (auto* o : observers_) {
            if (connected_) o->on_connected();
            else o->on_disconnected();
        }
    }
}

void OpenthermEndpoint::notify_all()
{
    notify_status(ot_.fault, ot_.flame, ot_.ch_active, ot_.dhw_active);
}

void OpenthermEndpoint::notify_status(bool fault, bool flame, bool ch_active, bool dhw_active)
{
    for (auto* o : observers_)
        o->on_status_changed(fault, flame, ch_active, dhw_active);
}
