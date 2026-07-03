#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

#include "esp_system.h"
#include "esp_log.h"
#include "rom/ets_sys.h"

// ── Driven adapters ──────────────────────────────────────
#include "infrastructure/driven/event_log_adapter.h"
#include "infrastructure/driven/heating_state_adapter.h"
#include "infrastructure/driven/ot_hardware_adapter.h"
#include "infrastructure/driven/boiler_opentherm_adapter.h"
#include "infrastructure/driven/temperature_sensor_adapter.h"
#include "infrastructure/driven/web_presenter_adapter.h"
#include "infrastructure/driven/nvs_config_store.h"
#include "infrastructure/driven/sntp_time_adapter.h"
#include "infrastructure/driven/crash_diagnostics_adapter.h"

// ── Driving adapters ─────────────────────────────────────
#include "infrastructure/driving/main_poller_task_adapter.h"
#include "infrastructure/driving/http_controller_adapter.h"

// ── WiFi provisioning ────────────────────────────────────
#include "infrastructure/driven/wifi_nvs_store.h"
#include "infrastructure/driven/esp32_wifi_adapter.h"
#include "infrastructure/driving/wifi_apsta_adapter.h"

// ── MQTT ─────────────────────────────────────────────────
#include "infrastructure/freertos/mqtt_queue_adapter.h"
#include "infrastructure/driven/mqtt_socket_adapter.h"
#include "infrastructure/driven/mqtt_renderer_adapter.h"
#include "infrastructure/driven/mqtt_nvs_store.h"
#include "infrastructure/driving/mqtt_interactor.h"

// ── Use cases ────────────────────────────────────────────
#include "application/use_cases/main_poller_interactor.h"
#include "application/use_cases/boiler_poll_interactor.h"
#include "application/use_cases/sensors_poll_interactor.h"
#include "application/use_cases/pid_poll_interactor.h"
#include "application/use_cases/system_config_interactor.h"
#include "application/use_cases/gas_correction_interactor.h"

// ── Application services ─────────────────────────────────
#include "application/services/modulation_stats_service.h"
#include "application/services/burn_cycle_service.h"
#include "application/services/gas_flow_estimator.h"
#include "application/services/dhw_predict_service.h"

extern "C" void app_main(void)
{
    // ── Phase 0: Pre-scheduler boot ──────────────────────
    ets_printf("=== Gas boiler Baxi duo-tec compact ===\n");
    ets_printf("Clean Architecture\n");

    // ── Phase 1: Foundation ──────────────────────────────
    EventLogAdapter ca_log;

    NvsConfigStore nvs;
    nvs.init();

    CrashDiagnosticsAdapter crash_diag(ca_log);
    crash_diag.start();

    if (crash_diag.last_boot_had_crash()) {
        ca_log.event(ILogger::SYSTEM, "Предыдущая загрузка: КРАШ");
    }

    ca_log.event(ILogger::SYSTEM, "Система запущена");

    // ── Phase 2: Driven adapters ────────────────────────
    HeatingStateAdapter      ca_state;
    SntpTimeAdapter          ca_time;
    ca_time.init();
    OtHardwareAdapter        ca_ot_hw;
    BoilerOpenThermAdapter   ca_boiler(ca_ot_hw);
    TemperatureSensorAdapter ca_sensors;
    WebPresenterAdapter      ca_web;

    FreeRtosMqttQueue        ca_mqtt_queue;
    MqttSocketAdapter        ca_mqtt(ca_mqtt_queue, ca_time);
    MqttRendererAdapter      ca_mqtt_renderer(ca_web);

    ca_log.set_time_source(&ca_time);

    ca_web.set_state(&ca_state);
    ca_web.set_log_reader(&ca_log);   // IEventLogReader (lock/unlock/to_json)
    ca_web.set_time_source(&ca_time);

    nvs.load_all(ca_state);  // restore persisted config into state
    nvs.load_meter(ca_state); // restore gas meter base reading

    // ── Phase 3: Network ─────────────────────────────────
    WifiNvsStore     wifi_nvs;
    wifi_nvs.init();

    Esp32WifiAdapter   wifi_hw;
    WifiApStaAdapter   wifi(wifi_hw, wifi_nvs);
    auto wifi_mode = wifi.boot();

    // SNTP + manual time
    ca_time.set_logger(&ca_log);
    if (wifi_mode == IWifiManager::Mode::STA) {
        // Full SNTP sync (STA mode has internet)
        ca_time.start();
        ca_time.set_timezone(ca_state.get_tz_offset());
        if (ca_time.is_synced()) {
            ca_time.save_time_offset();  // сохранить смещение в NVS для будущих загрузок
        }
    } else {
        // No internet — try restoring manual time offset from NVS
        if (ca_time.restore_time_offset()) {
            ESP_LOGI("main", "SNTP пропущен (нет STA) — время из NVS");
        } else {
            ESP_LOGW("main", "SNTP пропущен, ручное время не задано");
        }
    }

    // ── Phase 4: Use cases ───────────────────────────────
    BoilerPollInteractor  boiler_poll(ca_boiler, ca_state, ca_log, ca_time);
    SensorsPollInteractor sensors_poll(ca_sensors, ca_state);
    PidPollInteractor     pid_poll(ca_state, ca_boiler, ca_time, ca_log);

    SystemConfigInteractor sys_cfg(ca_state, ca_boiler, nvs, ca_log, ca_time);
    sys_cfg.set_boiler_poll(&boiler_poll);
    sys_cfg.set_pid_poll(&pid_poll);

    GasCorrectionInteractor gas_corr(ca_state, nvs, ca_log);

    // ── Phase 5: Application services ────────────────────
    ModulationStatsService mod_stats(ca_state);
    BurnCycleService       burn_cycles(ca_state, ca_time);
    GasFlowService         gas_flow(ca_state, ca_time);
    sys_cfg.set_burn_cycles(&burn_cycles);
    sys_cfg.set_mod_stats(&mod_stats);
    sys_cfg.set_gas_flow_reset(&gas_flow);
    DHWPredictService      dhw_predict(ca_state, nvs, ca_time);
    dhw_predict.load_history();

    // Wire gas correction interactor to gas flow service and restore correction log
    gas_corr.set_gas_flow(&gas_flow);
    gas_corr.set_time_source(&ca_time);
    gas_corr.init();

    // Restore saved burner stats from NVS
    {
        uint32_t bs = 0, tps = 0, cc = 0, ips = 0, ic = 0, mps = 0, mc = 0;
        if (nvs.load_burn_stats(bs, tps, cc, ips, ic, mps, mc)) {
            *burn_cycles.burner_sec_ptr()      = bs;
            *burn_cycles.total_pause_sec_ptr() = tps;
            *burn_cycles.cycle_cnt_ptr()       = cc;
            *burn_cycles.inter_pause_sec_ptr() = ips;
            *burn_cycles.inter_cnt_ptr()       = ic;
            *burn_cycles.mod_pause_sec_ptr()   = mps;
            *burn_cycles.mod_cnt_ptr()         = mc;
            ESP_LOGI("main", "NVS: восстановлена burn-статистика (burner_sec=%" PRIu32 ")", bs);
        }
    }

    // Restore modulation histogram from NVS
    {
        static NvsHistBlob hist_blob;
        memset(&hist_blob, 0, sizeof(hist_blob));
        float saved_integ_m3 = 0;
        uint32_t saved_burner_sec_hist = 0;
        if (nvs.load_stats(saved_burner_sec_hist, saved_integ_m3,
                           &hist_blob, nullptr, nullptr, nullptr)) {
            *mod_stats.samples_ptr() = hist_blob.samples;
            for (int i = 0; i < HIST_BINS; i++)
                mod_stats.hist_ptr()[i] = hist_blob.hist[i];
            gas_flow.set_integral(saved_integ_m3);
            ESP_LOGI("main", "NVS: восстановлена гистограмма (samples=%" PRIu32 ") и integral_m3=%.3f",
                     hist_blob.samples, (double)saved_integ_m3);
        }
    }

    // Restore total uptime (cumulative across reboots)
    uint32_t total_uptime_base_sec = 0;
    nvs.load_total_uptime(total_uptime_base_sec);
    ca_web.set_total_uptime_base(total_uptime_base_sec);

    ca_web.set_mod_stats(&mod_stats);
    ca_web.set_burn_cycles(&burn_cycles);
    ca_web.set_gas_flow(&gas_flow);
    ca_web.set_gas_correction(&gas_corr);

    // ── MQTT interactor ──────────────────────────────────
    MqttNvsStore       mqtt_nvs;
    mqtt_nvs.init();

    MqttInteractor mqtt(ca_mqtt, ca_mqtt_queue,
                        mqtt_nvs,      // IMqttConfigStore
                        ca_state,
                        sys_cfg,       // IConfigureSystem
                        ca_log,
                        ca_time,
                        ca_mqtt_renderer);  // IMqttStateRenderer

    // MQTT инициализируется всегда (если enabled в NVS).
    // Время нужно только для таймстемпов в логах, MQTT работает без него.
    mqtt.init();
    if (!ca_time.has_valid_time()) {
        ESP_LOGW("main", "SNTP: время недостоверно, но MQTT запущен");
        ca_log.event(ILogger::SYSTEM, "SNTP: время недостоверно, MQTT запущен без времени");
    }

    // ── Phase 6: Main poller ─────────────────────────────
    MainPollerInteractor main_poller;
    main_poller.add(&boiler_poll);
    main_poller.add(&sensors_poll);
    main_poller.add(&pid_poll);
    main_poller.add(&mod_stats);
    main_poller.add(&burn_cycles);
    main_poller.add(&gas_flow);
    main_poller.add(&dhw_predict);
    main_poller.add(&mqtt);       // MQTT: публикация после обновления состояния

    // ── Phase 7: Hardware init + start ───────────────────
    ca_sensors.init();
    ca_ot_hw.init();

    MainPollerTaskAdapter poll_task(main_poller);
    poll_task.start();
    ESP_LOGI("main", "Задача опроса запущена (7 IPollable)");

    // ── Phase 8: HTTP server ─────────────────────────────
    HttpControllerAdapter http;
    http.set_presenter(&ca_web);
    http.set_config(&sys_cfg);
    http.set_pid(&sys_cfg);
    http.set_fault(&sys_cfg);
    http.set_gas(&gas_corr);
    http.set_wifi(&wifi);
    http.set_time_adapter(&ca_time);
    http.set_mqtt(&mqtt);
    http.start();

    // ── Idle: CPU stats every 60s, periodic NVS save every 10 min ──
    static const char* TAG = "main";
    int save_tick = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(15000));  // keep 15s until fragmentation is fixed
        save_tick++;

        uint32_t idle0 = ulTaskGetIdleRunTimePercentForCore(0);
        uint32_t idle1 = ulTaskGetIdleRunTimePercentForCore(1);
        int cpu0 = 100 - (int)idle0;
        int cpu1 = 100 - (int)idle1;
        uint32_t free_heap = esp_get_free_heap_size();

        multi_heap_info_t info;
        heap_caps_get_info(&info, MALLOC_CAP_DEFAULT);
        uint32_t largest_free = info.largest_free_block;

        ESP_LOGI(TAG, "Аптайм: %lld с, сборка: %s %s, куча: всего %" PRIu32
                 " (крупн %" PRIu32 ") | CPU: core0=%d%% core1=%d%% total=%d%%",
                 ca_time.monotonic_us() / 1000000, __DATE__, __TIME__,
                 free_heap, largest_free,
                 cpu0, cpu1, (cpu0 + cpu1) / 2);

        // Per-task CPU stats every 5 min (5 ticks)
        if (save_tick % 5 == 0) {
            static char stats_buf[2048];
            vTaskGetRunTimeStats(stats_buf);
            ESP_LOGI(TAG, "── Статистика задач (CPU) ──\n%s", stats_buf);
        }

        // AP watchdog: restart AP if WiFi died
        wifi.try_recover_ap();

        if (largest_free < 8192) {
            ESP_LOGW(TAG, "Критическая фрагментация! крупн.блок=%" PRIu32 " всего=%" PRIu32,
                     largest_free, free_heap);
        }

        if (free_heap < 40 * 1024) {
            ca_log.event(ILogger::SYSTEM,
                "Мало свободной кучи: %" PRIu32 " байт", free_heap);
        }

        // Save all persistent state to NVS every 10 min (10 ticks)
        if (save_tick >= 10) {
            save_tick = 0;
            uint32_t bs = burn_cycles.burner_seconds();
            nvs.save_burn_stats(bs,
                                burn_cycles.total_pause_seconds(),
                                burn_cycles.cycle_count(),
                                burn_cycles.inter_session_pause_sec(),
                                burn_cycles.inter_session_cnt(),
                                burn_cycles.modulation_pause_sec(),
                                burn_cycles.modulation_cnt());
            static NvsHistBlob hist_blob;
            hist_blob.samples = mod_stats.samples();
            for (int i = 0; i < HIST_BINS; i++) {
                hist_blob.hist[i] = mod_stats.hist_ptr()[i];
            }
            nvs.save_stats(ca_state, bs, gas_flow.integral_m3(),
                           &hist_blob, nullptr, nullptr, nullptr);
            nvs.save_total_uptime(total_uptime_base_sec +
                                  (uint32_t)(ca_time.monotonic_us() / 1000000));
            nvs.save_meter(ca_state, &gas_corr.meter_blob());
        }
    }
}
