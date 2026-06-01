#include "controller.h"

#include <ctime>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char* TAG = "controller";

Controller::Controller(Model& model,
                       Endpoints& endpoints,
                       LogService& log_service,
                       StatsService& stats_service)
    : model_(model), endpoints_(endpoints), log_service_(log_service)
    , stats_service_(stats_service)
    , last_schedule_hour_(-1)
{
}

void Controller::start()
{
    load_config_nvs();

    endpoints_.web_.set_model(&model_);
    endpoints_.ot_.subscribe(&ot_obs_);
    endpoints_.web_.subscribe(&web_obs_);
    endpoints_.sensors_.subscribe(&sens_obs_);

    ESP_LOGI(TAG, "Controller initialized");
}

void Controller::load_config_nvs()
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("config", NVS_READONLY, &h);
    if (err != ESP_OK) return;

    int32_t tz = 0;
    if (nvs_get_i32(h, "tz_offset", &tz) == ESP_OK) {
        model_.set_tz_offset((int)tz);
        endpoints_.sntp_.set_timezone((int)tz);
    }

    uint8_t u8;
    if (nvs_get_u8(h, "ch_en", &u8) == ESP_OK) {
        bool en = (u8 != 0);
        model_.set_ch_enable(en);
        endpoints_.ot_.set_ch_enable(en);
    }
    if (nvs_get_u8(h, "dhw_en", &u8) == ESP_OK) {
        bool en = (u8 != 0);
        model_.set_dhw_enable(en);
        endpoints_.ot_.set_dhw_enable(en);
    }

        if (nvs_get_u8(h, "dhw_hyst", &u8) == ESP_OK) {
        float hyst = (float)u8 / 10.0f;
        if (hyst >= 0.5f && hyst <= 10.0f) {
            model_.set_dhw_hysteresis(hyst);
            endpoints_.ot_.set_dhw_hysteresis(hyst);
        }
    }

    int16_t i16;
    if (nvs_get_i16(h, "ch_sp", &i16) == ESP_OK) {
        if (i16 >= 20 && i16 <= 80) {
            float sp = (float)i16;
            endpoints_.ot_.set_ch_setpoint(sp);
            model_.set_ch_setpoint(sp);
        }
    }
    if (nvs_get_i16(h, "dhw_sp", &i16) == ESP_OK) {
        if (i16 >= 35 && i16 <= 80) {
            float sp = (float)i16;
            endpoints_.ot_.set_dhw_setpoint(sp);
            model_.set_dhw_setpoint(sp);
        }
    }

    size_t sz = sizeof(CH_Schedule);
    CH_Schedule sched;
    if (nvs_get_blob(h, "schedule", &sched, &sz) == ESP_OK) {
        model_.set_schedule(sched);
        if (sched.enabled && sched.temps[0] >= 20.0f && sched.temps[0] <= 80.0f) {
            float sp = sched.temps[0];
            endpoints_.ot_.set_ch_setpoint(sp);
            model_.set_ch_setpoint(sp);
        }
    }

    char srv[64];
    bool srv0_loaded = false, srv1_loaded = false;
    sz = sizeof(srv);
    if (nvs_get_blob(h, "sntp_srv0", srv, &sz) == ESP_OK) {
        model_.set_sntp_server0(srv);
        srv0_loaded = true;
    }
    sz = sizeof(srv);
    if (nvs_get_blob(h, "sntp_srv1", srv, &sz) == ESP_OK) {
        model_.set_sntp_server1(srv);
        srv1_loaded = true;
    }
    if (srv0_loaded || srv1_loaded) {
        endpoints_.sntp_.set_servers(
            model_.get_sntp_server0(),
            model_.get_sntp_server1());
    }

    nvs_close(h);
}

void Controller::save_config_nvs()
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("config", NVS_READWRITE, &h);
    if (err != ESP_OK) return;

    nvs_set_i32(h, "tz_offset", (int32_t)model_.get_tz_offset());
    nvs_set_u8(h, "ch_en", model_.is_ch_enabled() ? 1 : 0);
    nvs_set_u8(h, "dhw_en", model_.is_dhw_enabled() ? 1 : 0);
    nvs_set_i16(h, "ch_sp", (int16_t)model_.get_ch_setpoint());
    nvs_set_i16(h, "dhw_sp", (int16_t)model_.get_dhw_setpoint());
    nvs_set_blob(h, "schedule", (const void*)&model_.get_schedule(), sizeof(CH_Schedule));

    nvs_set_u8(h, "dhw_hyst", (uint8_t)(model_.get_dhw_hysteresis() * 10.0f + 0.5f));

    nvs_set_blob(h, "sntp_srv0", model_.get_sntp_server0(), strlen(model_.get_sntp_server0()) + 1);
    nvs_set_blob(h, "sntp_srv1", model_.get_sntp_server1(), strlen(model_.get_sntp_server1()) + 1);

    nvs_commit(h);
    nvs_close(h);
}

void Controller::apply_schedule()
{
    const auto& sched = model_.get_schedule();
    if (!sched.enabled) return;

    time_t now;
    struct tm ti;
    time(&now);
    localtime_r(&now, &ti);
    if (ti.tm_year < (2024 - 1900)) return;

    int hour = ti.tm_hour;
    if (hour < 0 || hour >= 24) return;
    if (hour == last_schedule_hour_) return;

    last_schedule_hour_ = hour;
    float sp = sched.temps[hour];
    if (sp >= 20.0f && sp <= 80.0f) {
        endpoints_.ot_.set_ch_setpoint(sp);
        model_.set_ch_setpoint(sp);
        ESP_LOGI(TAG, "Schedule: hour=%d setpoint=%.0f", hour, (double)sp);
    }
}

// ===== OpenthermObserver =====

void Controller::OpenthermObserver::on_connected()
{
    c_.model_.set_connected(true);
    c_.log_service_.event(LOG_CAT_SYSTEM, "Котёл подключён");
}

void Controller::OpenthermObserver::on_disconnected()
{
    c_.model_.set_connected(false);
    c_.log_service_.event(LOG_CAT_SYSTEM, "Котёл отключён");
}

void Controller::OpenthermObserver::on_status_changed(
    bool fault, bool flame, bool ch_active, bool dhw_active)
{
    c_.model_.set_fault(fault);
    c_.model_.set_flame(flame);
    c_.model_.set_ch_active(ch_active);
    c_.model_.set_dhw_active(dhw_active);

    static bool prev_fault = false;
    if (fault != prev_fault) {
        if (fault)
            c_.log_service_.event(LOG_CAT_EQUIP, "АВАРИЯ!");
        else
            c_.log_service_.event(LOG_CAT_EQUIP, "Авария снята");
        prev_fault = fault;
    }

    bool pump = ch_active || dhw_active;
    static bool prev_pump = false;
    if (pump != prev_pump) {
        c_.log_service_.event(LOG_CAT_EQUIP, pump ? "Насос: вкл" : "Насос: выкл");
        prev_pump = pump;
    }

    static bool prev_dhw = false;
    if (dhw_active != prev_dhw) {
        if (dhw_active)
            c_.log_service_.event(LOG_CAT_EQUIP, "Клапан → БКН");
        else
            c_.log_service_.event(LOG_CAT_EQUIP, "Клапан → CH");
        prev_dhw = dhw_active;
    }

    static bool prev_flame = false;
    if (flame != prev_flame) {
        c_.log_service_.event(LOG_CAT_MODE, flame ? "Горелка: вкл" : "Горелка: выкл");
        prev_flame = flame;
    }

    c_.apply_schedule();
}

void Controller::OpenthermObserver::on_ch_temp(float value)
{
    c_.model_.set_ch_temp(value);
}

void Controller::OpenthermObserver::on_dhw_temp(float value)
{
    c_.model_.set_dhw_temp(value);
}

void Controller::OpenthermObserver::on_return_temp(float value)
{
    c_.model_.set_return_temp(value);
}

void Controller::OpenthermObserver::on_outside_temp(float value)
{
    c_.model_.set_outside_temp(value);
}

void Controller::OpenthermObserver::on_modulation(float pct)
{
    c_.model_.set_modulation(pct);
}

void Controller::OpenthermObserver::on_ch_bounds(float min, float max)
{
    c_.model_.set_ch_sp_min(min);
    c_.model_.set_ch_sp_max(max);
}

void Controller::OpenthermObserver::on_dhw_bounds(float min, float max)
{
    c_.model_.set_dhw_sp_min(min);
    c_.model_.set_dhw_sp_max(max);
}

void Controller::OpenthermObserver::on_fault_codes(
    uint8_t asf, uint8_t oem_fault, uint16_t oem_diag)
{
    c_.model_.set_fault_codes(asf, oem_fault, oem_diag);
}

void Controller::OpenthermObserver::on_runtime_counters(
    uint16_t bs, uint16_t cps, uint16_t dvs, uint16_t dbs)
{
    c_.model_.set_runtime_counters(bs, cps, dvs, dbs);
}

void Controller::OpenthermObserver::on_runtime_hours(
    uint16_t bh, uint16_t cph, uint16_t dvh, uint16_t dbh)
{
    c_.model_.set_runtime_hours(bh, cph, dvh, dbh);
}

void Controller::OpenthermObserver::on_version(
    uint8_t st, uint8_t sv, float ov)
{
    c_.model_.set_version(st, sv, ov);
}

void Controller::OpenthermObserver::on_dhw_session_finished(
    uint32_t dur_ms, float min_temp)
{
    c_.model_.set_dhw_session_finished(dur_ms, min_temp);
}

void Controller::OpenthermObserver::on_ch_setpoint_confirmed(float value)
{
    if (value != c_.model_.get_ch_setpoint()) {
        c_.model_.set_ch_setpoint(value);
    }
}

void Controller::OpenthermObserver::on_dhw_setpoint_confirmed(float value)
{
    if (value != c_.model_.get_dhw_setpoint()) {
        c_.model_.set_dhw_setpoint(value);
    }
}

// ===== WebServerObserver =====

void Controller::WebServerObserver::on_cmd_set_ch_enable(bool enable)
{
    c_.endpoints_.ot_.set_ch_enable(enable);
    c_.model_.set_ch_enable(enable);
    c_.save_config_nvs();
    c_.log_service_.event(LOG_CAT_USER, "CH: %s", enable ? "вкл" : "выкл");
}

void Controller::WebServerObserver::on_cmd_set_dhw_enable(bool enable)
{
    c_.endpoints_.ot_.set_dhw_enable(enable);
    c_.model_.set_dhw_enable(enable);
    c_.save_config_nvs();
    c_.log_service_.event(LOG_CAT_USER, "DHW: %s", enable ? "вкл" : "выкл");
}

void Controller::WebServerObserver::on_cmd_set_ch_setpoint(float temp)
{
    c_.endpoints_.ot_.set_ch_setpoint(temp);
    c_.save_config_nvs();
    c_.log_service_.event(LOG_CAT_USER, "Уставка CH: %.0f°C", (double)temp);
}

void Controller::WebServerObserver::on_cmd_set_dhw_setpoint(float temp)
{
    c_.endpoints_.ot_.set_dhw_setpoint(temp);
    c_.save_config_nvs();
    c_.log_service_.event(LOG_CAT_USER, "Уставка DHW: %.0f°C", (double)temp);
}

void Controller::WebServerObserver::on_cmd_set_dhw_hysteresis(float value)
{
    if (value < 0.5f) value = 0.5f;
    if (value > 10.0f) value = 10.0f;
    c_.model_.set_dhw_hysteresis(value);
    c_.endpoints_.ot_.set_dhw_hysteresis(value);
    c_.save_config_nvs();
    c_.log_service_.event(LOG_CAT_USER, "Гистерезис БКН: %.1f°C", (double)value);
}

void Controller::WebServerObserver::on_cmd_set_sntp_servers(const char* srv0, const char* srv1)
{
    c_.model_.set_sntp_server0(srv0);
    c_.model_.set_sntp_server1(srv1);
    c_.endpoints_.sntp_.set_servers(srv0, srv1);
    c_.save_config_nvs();
    c_.log_service_.event(LOG_CAT_USER, "NTP серверы: %s, %s", srv0, srv1);
}

void Controller::WebServerObserver::on_cmd_fault_reset()
{
    c_.endpoints_.ot_.trigger_fault_reset();
    c_.log_service_.event(LOG_CAT_USER, "Сброс аварии");
}

void Controller::WebServerObserver::on_cmd_set_schedule(const CH_Schedule& schedule)
{
    c_.model_.set_schedule(schedule);
    c_.last_schedule_hour_ = -1;
    c_.save_config_nvs();
    c_.log_service_.event(LOG_CAT_USER, "Расписание: %s",
              schedule.enabled ? "вкл" : "выкл");
}

void Controller::WebServerObserver::on_cmd_set_timezone(int offset)
{
    c_.endpoints_.sntp_.set_timezone(offset);
    c_.model_.set_tz_offset(offset);
    c_.save_config_nvs();
    c_.log_service_.event(LOG_CAT_USER, "Часовой пояс: UTC%+d", offset);
}

void Controller::WebServerObserver::on_cmd_set_k_calib(float value)
{
    if (value < 0.1f) value = 0.1f;
    if (value > 10.0f) value = 10.0f;
    c_.model_.set_k_calib(value);
    c_.log_service_.event(LOG_CAT_USER, "K_calib: %.3f", (double)value);
}


void Controller::WebServerObserver::on_cmd_set_gas_meter_base(float value)
{
    if (value < 0.0f) value = 0.0f;
    c_.model_.set_gas_meter_base(value);
    c_.model_.set_last_correction_refs(value, c_.model_.get_gas_data().integral_m3);
    c_.log_service_.event(LOG_CAT_USER, "Нач. показ. счётчика: %.3f", (double)value);
    c_.stats_service_.request_save_meter();
}

void Controller::WebServerObserver::on_cmd_add_gas_meter_correction(float reading)
{
    if (reading < c_.model_.get_gas_meter_base() && c_.model_.get_gas_meter_base() > 0) {
        c_.log_service_.event(LOG_CAT_USER, "Ошибка: показ. < нач. знач.");
        return;
    }

    float current_integral = c_.model_.get_gas_data().integral_m3;
    float base = c_.model_.get_gas_meter_base();
    float estimated_total = base + current_integral;
    float difference = reading - estimated_total;

    // Compute ratio-based correction coefficient
    float prev_k = c_.model_.get_k_calib();
    float new_k = prev_k;
    float prev_actual = c_.model_.get_last_correction_actual();
    float prev_integral = c_.model_.get_integral_at_last_correction();

    if (prev_actual > 0 && prev_integral >= 0) {
        float actual_delta = reading - prev_actual;
        float integral_delta = current_integral - prev_integral;
        if (actual_delta > 0.001f && integral_delta > 0.0001f) {
            float ratio = actual_delta / integral_delta;
            new_k = prev_k * ratio;
            if (new_k < 0.1f) new_k = 0.1f;
            if (new_k > 10.0f) new_k = 10.0f;
        }
    }

    c_.model_.set_k_calib(new_k);
    c_.model_.set_last_correction_refs(reading, current_integral);
    c_.stats_service_.request_save();

    CorrectionEntry entry;
    entry.timestamp = (uint32_t)time(nullptr);
    entry.actual_reading = reading;
    entry.estimated_total = estimated_total;
    entry.difference = difference;
    entry.prev_k_calib = prev_k;
    entry.new_k_calib = new_k;
    c_.model_.add_correction(entry);

    c_.log_service_.event(LOG_CAT_USER,
        "Сверка: показ. %.3f, расх. %+.4f, K %.4f→%.4f",
        (double)reading, (double)difference, (double)prev_k, (double)new_k);

    c_.stats_service_.request_save_meter();
}

void Controller::WebServerObserver::on_cmd_reset_modulation_stats()
{
    c_.stats_service_.reset_modulation_stats();
    c_.log_service_.event(LOG_CAT_USER, "Статистика модуляции сброшена");
}

void Controller::WebServerObserver::on_cmd_reset_cycle_stats()
{
    c_.stats_service_.reset_cycle_stats();
    c_.log_service_.event(LOG_CAT_USER, "Статистика циклов горения сброшена");
}

void Controller::WebServerObserver::on_cmd_reset_gas_stats()
{
    c_.stats_service_.reset_gas_stats();
    c_.log_service_.event(LOG_CAT_USER, "Статистика расхода газа сброшена");
}
// ===== SensorsObserver =====

void Controller::SensorsObserver::on_sensor_data(int sensor_id, float temperature)
{
    if (sensor_id == 0)
        c_.model_.set_t1_temp(temperature);
    else if (sensor_id == 1)
        c_.model_.set_t2_temp(temperature);
}