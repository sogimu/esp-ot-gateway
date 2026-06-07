#include "pid/pid_service.h"
#include "esp_log.h"

#include <ctime>
#include <cstdio>

static const char *TAG = "pid";

static const int SENSOR_TIMEOUT_MS = 120000;
static const float MAX_SAFE_ROOM = 30.0f;
static const float FALLBACK_SETPOINT = 40.0f;
static const float MIN_SETPOINT = 25.0f;

PidService::PidService(Model& model, OpenthermEndpoint& ot, SensorsEndpoint& sensors)
    : model_(model), ot_(ot), sensors_(sensors)
{
    pid_cfg_ = PID_CONFIG_DEFAULT();
    pid_state_ = PID_STATE_DEFAULT();
}

PidService::~PidService()
{
    stop();
}

void PidService::start()
{
    stop();

    if (!mutex_) {
        mutex_ = xSemaphoreCreateMutex();
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);

    pid_cfg_ = PID_CONFIG_DEFAULT();
    pid_cfg_.kp = kp_;
    pid_cfg_.ki = ki_;
    pid_cfg_.kd = kd_;
    pid_state_ = PID_STATE_DEFAULT();
    active_ = false;
    output_ = 0;
    p_ = i_ = d_ = 0;
    prev_flame_ = false;
    dhw_active_ = false;
    cycle_locked_ = false;
    remaining_lockout_ = 0;
    lockout_logged_ = false;
    lockout_ended_ = false;
    timeout_logged_ = false;
    overheat_logged_ = false;
    hyst_block_logged_ = false;
    ch_enabled_by_pid_ = false;
    sensor_timeout_ = false;
    room_temp_ = -127.0f;
    last_room_temp_ms_ = 0;
    last_flame_off_ms_ = 0;
    last_compute_ms_ = 0;
    start_ms_ = (uint32_t)(esp_timer_get_time() / 1000);
    tick_count_ = 0;
    pid_cfg_.out_min = model_.get_ch_sp_min() >= 20.0f ? model_.get_ch_sp_min() : 25.0f;
    pid_cfg_.out_max = (model_.get_ch_sp_max() >= 20.0f && model_.get_ch_sp_max() <= 80.0f) ? model_.get_ch_sp_max() : 75.0f;

    xSemaphoreGive(mutex_);

    esp_timer_create_args_t timer_args = {};
    timer_args.callback = on_timer_static;
    timer_args.arg = this;
    timer_args.name = "pid_timer";
    esp_timer_create(&timer_args, &tick_timer_);
    esp_timer_start_periodic(tick_timer_, 1000000);

    ot_.subscribe(this);
    sensors_.subscribe(this);

    ESP_LOGI(TAG, "PidService started, enabled=%d", enabled_);
}

void PidService::stop()
{
    ot_.unsubscribe(this);
    sensors_.unsubscribe(this);

    if (tick_timer_) {
        esp_timer_stop(tick_timer_);
        esp_timer_delete(tick_timer_);
        tick_timer_ = nullptr;
    }

    if (mutex_) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

void PidService::set_enabled(bool en)
{
    xSemaphoreTake(mutex_, portMAX_DELAY);

    enabled_ = en;
    if (!enabled_) {
        active_ = false;
        pid_state_ = PID_STATE_DEFAULT();
    } else {
        pid_reset(&pid_state_, room_temp_ > -100.0f ? room_temp_ : 0);
        last_compute_ms_ = (uint32_t)(esp_timer_get_time() / 1000);
        tick_count_ = 0;
        ch_enabled_by_pid_ = true;
        hyst_block_logged_ = false;
    }
    push_to_model();

    xSemaphoreGive(mutex_);
}

void PidService::set_config(float kp, float ki, float kd, int dt_sec,
                            int room_sensor, float target_room, int lockout_sec)
{
    xSemaphoreTake(mutex_, portMAX_DELAY);

    kp_ = kp;
    ki_ = ki;
    kd_ = kd;
    dt_sec_ = dt_sec < 10 ? 10 : dt_sec;
    int new_sensor = (room_sensor == 0 || room_sensor == 1) ? room_sensor : 0;
    if (new_sensor != room_sensor_) {
        room_temp_ = -127.0f;
        last_room_temp_ms_ = 0;
        sensor_timeout_ = false;
        timeout_logged_ = false;
    }
    room_sensor_ = new_sensor;
    target_room_ = target_room < 10.0f ? 10.0f : (target_room > 35.0f ? 35.0f : target_room);
    lockout_sec_ = lockout_sec < 60 ? 60 : lockout_sec;

    pid_cfg_.kp = kp_;
    pid_cfg_.ki = ki_;
    pid_cfg_.kd = kd_;

    tick_count_ = dt_sec_;

    push_to_model();

    xSemaphoreGive(mutex_);
}

void PidService::set_hysteresis(float h)
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    hysteresis_ = h < 0.1f ? 0.1f : (h > 3.0f ? 3.0f : h);
    tick_count_ = dt_sec_;
    push_to_model();
    xSemaphoreGive(mutex_);
}

void PidService::on_timer_static(void* arg)
{
    auto* self = static_cast<PidService*>(arg);
    self->on_tick();
}

void PidService::on_tick()
{
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

    xSemaphoreTake(mutex_, portMAX_DELAY);

    tick_count_++;
    uint32_t elapsed = (last_compute_ms_ > 0) ? (now_ms - last_compute_ms_) : 0;

    if (last_room_temp_ms_ > 0) {
        sensor_timeout_ = ((int)(now_ms - last_room_temp_ms_) > SENSOR_TIMEOUT_MS);
    } else {
        sensor_timeout_ = ((int)(now_ms - start_ms_) > SENSOR_TIMEOUT_MS);
    }

    bool was_locked = cycle_locked_;
    if (prev_flame_) {
        cycle_locked_ = false;
        remaining_lockout_ = 0;
    } else if (last_flame_off_ms_ > 0 && !prev_flame_) {
        int elapsed_since_off = (int)((now_ms - last_flame_off_ms_) / 1000);
        if (elapsed_since_off < lockout_sec_) {
            cycle_locked_ = true;
            remaining_lockout_ = lockout_sec_ - elapsed_since_off;
        } else {
            cycle_locked_ = false;
            remaining_lockout_ = 0;
        }
    }
    if (was_locked && !cycle_locked_) {
        lockout_ended_ = true;
        lockout_logged_ = false;
        char buf[48];
        snprintf(buf, sizeof(buf), "PID: блок снят, нагрев OK");
        model_.add_log_entry((uint32_t)time(nullptr), (uint8_t)LOG_CAT_MODE, buf);
    }

    float room = room_temp_;
    bool room_valid = (room > -100.0f && !sensor_timeout_);

    if (room_valid && room > MAX_SAFE_ROOM) {
        float safe_sp = MIN_SETPOINT;
        output_ = safe_sp;
        p_ = i_ = d_ = 0;
        active_ = true;
        push_to_model();
        xSemaphoreGive(mutex_);

        ot_.set_ch_setpoint(safe_sp);
        ESP_LOGW(TAG, "Overheat: room=%.1f > %.0f, forcing setpoint=%.0f",
                 (double)room, (double)MAX_SAFE_ROOM, (double)safe_sp);
        if (!overheat_logged_) {
            char buf[64];
            snprintf(buf, sizeof(buf), "PID: ПЕРЕГРЕВ %.1f>%.0f, SP %.0f",
                     (double)room, (double)MAX_SAFE_ROOM, (double)safe_sp);
            model_.add_log_entry((uint32_t)time(nullptr), (uint8_t)LOG_CAT_MODE, buf);
            overheat_logged_ = true;
        }
        timeout_logged_ = false;
        lockout_logged_ = false;
        return;
    }
    overheat_logged_ = false;

    if (!enabled_ || !room_valid || cycle_locked_) {
        if (!enabled_) {
            active_ = false;
        } else if (!room_valid) {
            if (sensor_timeout_) {
                float safe_sp = FALLBACK_SETPOINT;
                output_ = safe_sp;
                active_ = true;
                model_.set_ch_enable(true);
                push_to_model();
                xSemaphoreGive(mutex_);

                ot_.set_ch_setpoint(safe_sp);
                ot_.set_ch_enable(true);
                ESP_LOGW(TAG, "Sensor timeout, fallback setpoint=%.0f", (double)safe_sp);
                if (!timeout_logged_) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "PID: таймаут датч, SP %.0f",
                             (double)safe_sp);
                    model_.add_log_entry((uint32_t)time(nullptr), (uint8_t)LOG_CAT_MODE, buf);
                    timeout_logged_ = true;
                }
                lockout_logged_ = false;
                return;
            }
        } else if (cycle_locked_) {
            float safe_sp = MIN_SETPOINT;
            output_ = safe_sp;
            active_ = true;
            push_to_model();
            xSemaphoreGive(mutex_);

            ot_.set_ch_setpoint(safe_sp);
            if (!lockout_logged_) {
                char buf[64];
                snprintf(buf, sizeof(buf), "PID: блок цикла %dс, SP %.0f",
                         remaining_lockout_, (double)safe_sp);
                model_.add_log_entry((uint32_t)time(nullptr), (uint8_t)LOG_CAT_MODE, buf);
                lockout_logged_ = true;
            }
            timeout_logged_ = false;
            return;
        }
        push_to_model();
        xSemaphoreGive(mutex_);
        return;
    }

    if (tick_count_ < dt_sec_ && last_compute_ms_ > 0) {
        push_to_model();
        xSemaphoreGive(mutex_);
        return;
    }

    int effective_dt = tick_count_;
    if (elapsed > 0 && elapsed < (uint32_t)(dt_sec_ * 3 * 1000)) {
        effective_dt = (int)(elapsed / 1000);
        if (effective_dt < 1) effective_dt = 1;
    } else {
        effective_dt = dt_sec_;
    }

    tick_count_ = 0;
    last_compute_ms_ = now_ms;

    pid_cfg_.out_min = model_.get_ch_sp_min() >= 20.0f ? model_.get_ch_sp_min() : 25.0f;
    pid_cfg_.out_max = (model_.get_ch_sp_max() >= 20.0f && model_.get_ch_sp_max() <= 80.0f) ? model_.get_ch_sp_max() : 75.0f;

    float saved_ki = pid_cfg_.ki;
    if (dhw_active_) {
        pid_cfg_.ki = 0.0f;
    }

    float output = pid_step(&pid_state_, &pid_cfg_, target_room_, room, effective_dt);

    pid_cfg_.ki = saved_ki;

    p_ = pid_cfg_.kp * (target_room_ - room);
    i_ = pid_state_.integral;
    d_ = pid_state_.d_filt;

    active_ = true;
    output_ = output;

    lockout_logged_ = false;
    timeout_logged_ = false;
    overheat_logged_ = false;

    bool user_ch_on = model_.is_ch_enabled();
    bool was_ch_enabled = ch_enabled_by_pid_;
    if (!user_ch_on || room > target_room_ + hysteresis_) {
        ch_enabled_by_pid_ = false;
    } else if (room < target_room_ - hysteresis_) {
        ch_enabled_by_pid_ = true;
    }
    if (was_ch_enabled && !ch_enabled_by_pid_) {
        if (!hyst_block_logged_) {
            char buf[48];
            snprintf(buf, sizeof(buf), "PID: CH откл, комн %.1f>%.1f",
                     (double)room, (double)(target_room_ + hysteresis_));
            model_.add_log_entry((uint32_t)time(nullptr), (uint8_t)LOG_CAT_MODE, buf);
            hyst_block_logged_ = true;
        }
    } else if (!was_ch_enabled && ch_enabled_by_pid_) {
        char buf[48];
        snprintf(buf, sizeof(buf), "PID: CH вкл, комн %.1f<%.1f",
                 (double)room, (double)(target_room_ - hysteresis_));
        model_.add_log_entry((uint32_t)time(nullptr), (uint8_t)LOG_CAT_MODE, buf);
        hyst_block_logged_ = false;
    }

    model_.set_ch_enable(ch_enabled_by_pid_);

    push_to_model();
    xSemaphoreGive(mutex_);

    if (ch_enabled_by_pid_) {
        ot_.set_ch_setpoint(output);
        ot_.set_ch_enable(true);
    } else {
        ot_.set_ch_setpoint(pid_cfg_.out_min);
        ot_.set_ch_enable(false);
    }

    ESP_LOGD(TAG, "room=%.1f target=%.1f P=%.1f I=%.1f D=%.1f out=%.0f dt=%d",
             (double)room, (double)target_room_, (double)p_, (double)i_, (double)d_,
             (double)output, effective_dt);
    char buf[64];
    snprintf(buf, sizeof(buf), "PID: %.1f->%.1f вых %.0f, CH %s",
             (double)room, (double)target_room_, (double)output,
             ch_enabled_by_pid_ ? "вкл" : "откл");
    model_.add_log_entry((uint32_t)time(nullptr), (uint8_t)LOG_CAT_MODE, buf);
}

void PidService::on_status_changed(bool fault, bool flame, bool ch_active, bool dhw_active)
{
    (void)fault;
    (void)ch_active;

    xSemaphoreTake(mutex_, portMAX_DELAY);

    dhw_active_ = dhw_active;

    if (flame != prev_flame_) {
        if (prev_flame_ && !flame) {
            last_flame_off_ms_ = (uint32_t)(esp_timer_get_time() / 1000);
            cycle_locked_ = true;
            remaining_lockout_ = lockout_sec_;
            lockout_logged_ = false;
        }
        prev_flame_ = flame;
    }

    xSemaphoreGive(mutex_);
}

void PidService::on_sensor_data(int sensor_id, float temperature)
{
    if (sensor_id == room_sensor_) {
        ESP_LOGI(TAG, "sensor[%d]=%.1f, room_sensor=%d, enabled=%d, active=%d",
                 sensor_id, (double)temperature, room_sensor_, enabled_, active_);
        xSemaphoreTake(mutex_, portMAX_DELAY);

        room_temp_ = temperature;
        last_room_temp_ms_ = (uint32_t)(esp_timer_get_time() / 1000);
        timeout_logged_ = false;

        if (!enabled_) {
            ESP_LOGW(TAG, "sensor data ignored: pid not enabled");
            xSemaphoreGive(mutex_);
            return;
        }

        if (!active_) {
            ESP_LOGI(TAG, "first sensor data, forcing PID compute");
            pid_reset(&pid_state_, room_temp_);
            last_compute_ms_ = (uint32_t)(esp_timer_get_time() / 1000);
            tick_count_ = dt_sec_;
        }

        xSemaphoreGive(mutex_);
    }
}

void PidService::push_to_model()
{
    model_.set_pid_state(
        enabled_, active_, output_,
        p_, i_, d_,
        room_temp_,
        target_room_,
        cycle_locked_,
        remaining_lockout_,
        ch_enabled_by_pid_
    );
    model_.set_pid_config(
        kp_, ki_, kd_,
        dt_sec_, room_sensor_,
        target_room_, lockout_sec_
    );
    model_.set_pid_hysteresis(hysteresis_);
}
