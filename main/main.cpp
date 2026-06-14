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
    SntpTimeAdapter          ca_time;
    OtHardwareAdapter        ca_ot_hw;
    BoilerOpenThermAdapter   ca_boiler(ca_ot_hw);
    TemperatureSensorAdapter ca_sensors;
    WebPresenterAdapter      ca_web;

    ca_log.set_time_source(&ca_time);

    ca_web.set_state(&ca_state);
    ca_web.set_logger(&ca_log);
    ca_web.set_time_source(&ca_time);

    nvs.load_all(ca_state);  // restore persisted config into state
    nvs.load_meter(ca_state); // restore gas meter base reading

    // ── Phase 3: Network ─────────────────────────────────
    WifiInitAdapter wifi;

    wifi.start();

    // Sync SNTP with UTC+0 first, then apply user timezone.
    // This prevents a wrong TZ from NVS from corrupting the system clock.
    ca_time.set_logger(&ca_log);
    ca_time.start();   // sets TZ=UTC+0, starts SNTP, waits for first sync
    ca_time.set_timezone(ca_state.get_tz_offset());

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

    // Restore modulation histogram from NVS (NvsHistBlob)
    {
        NvsHistBlob hist_blob;
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

    // ── Idle: CPU stats every 60s, periodic NVS save every 10 min ──
    static const char* TAG = "main";
    int save_tick = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        save_tick++;

        int idle0 = (int)ulTaskGetIdleRunTimePercentForCore(0);
        int idle1 = (int)ulTaskGetIdleRunTimePercentForCore(1);

        ESP_LOGI(TAG, "Аптайм: %lld с, свободно: %" PRIu32
                 " | CPU: core0=%d%% core1=%d%% total=%d%%",
                 ca_time.monotonic_us() / 1000000,
                 esp_get_free_heap_size(),
                 100 - idle0, 100 - idle1, (200 - idle0 - idle1) / 2);

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
            // Save modulation histogram
            NvsHistBlob hist_blob;
            hist_blob.samples = mod_stats.samples();
            for (int i = 0; i < HIST_BINS; i++) {
                uint32_t v = mod_stats.hist_ptr()[i];
                hist_blob.hist[i] = v > 65535 ? 65535 : (uint16_t)v;
            }
            nvs.save_stats(ca_state, bs, gas_flow.integral_m3(),
                           &hist_blob, nullptr, nullptr, nullptr);
            // Save total uptime
            nvs.save_total_uptime(total_uptime_base_sec +
                                  (uint32_t)(ca_time.monotonic_us() / 1000000));
            // Save gas meter blob (includes correction log)
            nvs.save_meter(ca_state, &gas_corr.meter_blob());
        }
    }
}
