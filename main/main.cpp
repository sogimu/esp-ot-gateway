#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"

// ── Driven adapters ──────────────────────────────────────
#include "infrastructure/driven/event_log_adapter.h"
#include "infrastructure/driven/heating_state_adapter.h"
#include "infrastructure/driven/esp_timer_adapter.h"
#include "infrastructure/driven/ot_hardware_adapter.h"
#include "infrastructure/driven/boiler_opentherm_adapter.h"
#include "infrastructure/driven/temperature_sensor_adapter.h"
#include "infrastructure/driven/web_presenter_adapter.h"
#include "infrastructure/driven/nvs_config_adapter.h"
#include "infrastructure/driven/sntp_time_adapter.h"
#include "infrastructure/driven/crash_diagnostics_adapter.h"

// ── Driving adapters ─────────────────────────────────────
#include "infrastructure/driving/wifi_init_adapter.h"
#include "infrastructure/driving/main_poller_task_adapter.h"
#include "infrastructure/driving/http_controller_adapter.h"

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

    NvsConfigAdapter nvs;
    nvs.init();

    CrashDiagnosticsAdapter crash_diag(ca_log);
    crash_diag.start();

    ca_log.event(ILogger::SYSTEM, "Система запущена");

    // ── Phase 2: Driven adapters ────────────────────────
    HeatingStateAdapter      ca_state;
    EspTimerAdapter          ca_time;
    OtHardwareAdapter        ca_ot_hw;
    BoilerOpenThermAdapter   ca_boiler(ca_ot_hw);
    TemperatureSensorAdapter ca_sensors;
    WebPresenterAdapter      ca_web;

    ca_web.set_state(&ca_state);
    ca_web.set_logger(&ca_log);

    nvs.load_all(ca_state);  // restore persisted config into state

    // ── Phase 3: Network ─────────────────────────────────
    WifiInitAdapter wifi;
    SntpTimeAdapter sntp;

    wifi.start();
    sntp.set_timezone(ca_state.get_tz_offset());
    sntp.start();

    // ── Phase 4: Use cases ───────────────────────────────
    BoilerPollInteractor  boiler_poll(ca_boiler, ca_state, ca_log, ca_time);
    SensorsPollInteractor sensors_poll(ca_sensors, ca_state);
    PidPollInteractor     pid_poll(ca_state, ca_boiler, ca_time, ca_log);

    SystemConfigInteractor sys_cfg(ca_state, ca_boiler, nvs, ca_log, sntp);
    sys_cfg.set_boiler_poll(&boiler_poll);

    GasCorrectionInteractor gas_corr(ca_state, nvs, ca_log);

    // ── Phase 5: Application services ────────────────────
    ModulationStatsService mod_stats(ca_state);
    BurnCycleService       burn_cycles(ca_state, ca_time);
    GasFlowService         gas_flow(ca_state, ca_time);
    DHWPredictService      dhw_predict(ca_state, nvs, ca_time);
    dhw_predict.load_history();

    // Restore saved burner stats from NVS (survives reboots)
    {
        uint32_t saved_bs = 0, saved_cc = 0;
        if (nvs.load_burner_sec(saved_bs, saved_cc)) {
            *burn_cycles.burner_sec_ptr() = saved_bs;
            *burn_cycles.cycle_cnt_ptr()  = saved_cc;
            ESP_LOGI("main", "NVS: восстановлено burner_sec=%" PRIu32 " cycle_cnt=%" PRIu32,
                     saved_bs, saved_cc);
        }
    }

    ca_web.set_mod_stats(&mod_stats);
    ca_web.set_burn_cycles(&burn_cycles);
    ca_web.set_gas_flow(&gas_flow);

    // ── Phase 6: Main poller ─────────────────────────────
    MainPollerInteractor main_poller;
    main_poller.add(&boiler_poll);
    main_poller.add(&sensors_poll);
    main_poller.add(&pid_poll);
    main_poller.add(&mod_stats);
    main_poller.add(&burn_cycles);
    main_poller.add(&gas_flow);
    main_poller.add(&dhw_predict);

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
    http.start();

    // ── Idle: CPU stats every 60s, periodic NVS save ─────
    static const char* TAG = "main";
    int save_tick = 0;
    uint32_t last_saved_burner_sec = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        save_tick++;

        int idle0 = (int)ulTaskGetIdleRunTimePercentForCore(0);
        int idle1 = (int)ulTaskGetIdleRunTimePercentForCore(1);

        ESP_LOGI(TAG, "Аптайм: %lld с, свободно: %" PRIu32
                 " | CPU: core0=%d%% core1=%d%% total=%d%%",
                 esp_timer_get_time() / 1000000,
                 esp_get_free_heap_size(),
                 100 - idle0, 100 - idle1, (200 - idle0 - idle1) / 2);

        // Save burner runtime to NVS every 10 min (10 ticks) if changed
        if (save_tick >= 10) {
            save_tick = 0;
            uint32_t bs = burn_cycles.burner_seconds();
            if (bs != last_saved_burner_sec) {
                nvs.save_burner_sec(bs, burn_cycles.cycle_count());
                last_saved_burner_sec = bs;
            }
        }
    }
}
