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
#include "infrastructure/driven/nvs_config_store.h"  // blob types (NvsHistBlob, etc.)
#include "infrastructure/driven/time_settings_nvs_store.h"
#include "infrastructure/driven/boiler_nvs_store.h"
#include "infrastructure/driven/gas_correction_nvs_store.h"
#include "infrastructure/driven/predict_nvs_store.h"
#include "infrastructure/driven/burn_stats_nvs_store.h"
#include "infrastructure/driven/heating_stats_nvs_store.h"
#include "infrastructure/driven/sntp_time_adapter.h"
#include "infrastructure/driven/crash_diagnostics_adapter.h"

// ── Driving adapters ─────────────────────────────────────
#include "infrastructure/driving/control_loop_task_adapter.h"
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

// ── OTA ──────────────────────────────────────────────────
#include "infrastructure/driven/ota_validity_adapter.h"
#include "infrastructure/driven/esp_ota_adapter.h"
#include "infrastructure/driven/ota_version_index_adapter.h"
#include "infrastructure/driving/ota_interactor.h"

// ── Use cases ────────────────────────────────────────────
#include "application/use_cases/control_loop_interactor.h"
#include "application/use_cases/boiler_poll_interactor.h"
#include "application/use_cases/sensors_poll_interactor.h"
#include "application/use_cases/pid_poll_interactor.h"
#include "application/use_cases/system_config_interactor.h"
#include "application/use_cases/gas_correction_interactor.h"
#include "application/use_cases/supervision_loop_interactor.h"
#include "application/use_cases/persistence_loop_interactor.h"

// ── Application services ─────────────────────────────────
#include "application/services/modulation_stats_service.h"
#include "application/services/burn_cycle_service.h"
#include "application/services/gas_flow_estimator.h"
#include "application/services/dhw_predict_service.h"

// ── OTA: сброс статистики перед перезагрузкой в новый слот ──────
static void ota_flush_and_reboot(BurnCycleService& burn_cycle_service,
                                  ModulationStatsService& mod_stats,
                                  HeatingStatsNvsStore& heating_stats,
                                  HeatingStateAdapter& ca_state,
                                  GasFlowService& gas_flow,
                                  GasCorrectionInteractor& gas_corr,
                                  uint32_t& total_uptime_base_sec,
                                  SntpTimeAdapter& ca_time)
{
    burn_cycle_service.save_to_store();
    NvsHistBlob hb; memset(&hb, 0, sizeof(hb));
    mod_stats.fill_histogram(hb);
    heating_stats.save_stats(ca_state, burn_cycle_service.burner_seconds(),
        gas_flow.integral_m3(), &hb, nullptr, nullptr, nullptr);
    heating_stats.save_total_uptime(total_uptime_base_sec +
        (uint32_t)(ca_time.monotonic_us() / 1000000));
    heating_stats.save_meter(ca_state, &gas_corr.meter_blob());
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

extern "C" void app_main(void)
{
    // ── Phase 0: Pre-scheduler boot ──────────────────────
    ets_printf("=== Gas boiler Baxi duo-tec compact ===\n");
    ets_printf("Clean Architecture\n");


    // NVS-хранилища сгруппированы в одной структуре.
    struct {
        TimeSettingsNvsStore  time;
        WifiNvsStore          wifi;
        MqttNvsStore          mqtt;
        BoilerNvsStore        boiler;
        GasCorrectionNvsStore gas;
        PredictNvsStore       predict;
        BurnStatsNvsStore     burn_stats;
        HeatingStatsNvsStore  heating_stats;
    } stores;
    stores.time.init();
    stores.gas.init(stores.boiler);
    stores.wifi.init();

    // ── Phase 1: Foundation ──────────────────────────────
    EventLogAdapter ca_log;
    OtaValidityAdapter ota_validity;
    

    CrashDiagnosticsAdapter crash_diag(ca_log);
    crash_diag.start();
    ota_validity.set_crash_flag(crash_diag.last_boot_had_crash());


    ca_log.event(ILogger::SYSTEM, "Система запущена");

    // ── Phase 2: Driven adapters ────────────────────────
    HeatingStateAdapter      ca_state(stores.time, stores.boiler);
    OtHardwareAdapter        ca_ot_hw;
    BoilerOpenThermAdapter   ca_boiler(ca_ot_hw);
    TemperatureSensorAdapter ca_sensors;

    ca_state.load_settings();

    // ── Phase 3: Network ─────────────────────────────────

    Esp32WifiAdapter   wifi_hw;
    WifiApStaAdapter   wifi(wifi_hw, stores.wifi);
    auto wifi_mode = wifi.boot();

    // SNTP + manual time
    SntpTimeAdapter ca_time(ca_state.get_tz_offset(),
                             wifi_mode == IWifiManager::Mode::STA, &ca_log);

    // ── Phase 4: Use cases ───────────────────────────────
    BoilerPollInteractor  boiler_poll(ca_boiler, ca_state, ca_log, ca_time);
    SensorsPollInteractor sensors_poll(ca_sensors, ca_state);
    PidPollInteractor     pid_poll(ca_state, ca_boiler, ca_time, ca_log);

    // ── Phase 5: Application services ────────────────────
    ModulationStatsService mod_stats(ca_state, stores.heating_stats);
    BurnCycleService       burn_cycle_service(ca_state, ca_time, stores.burn_stats);
    GasFlowService         gas_flow(ca_state, ca_time, stores.heating_stats);
    SystemConfigInteractor sys_cfg(ca_state, ca_boiler, stores.time, stores.boiler, ca_log, ca_time,
                                    &boiler_poll, &pid_poll,
                                    &burn_cycle_service, &mod_stats, &gas_flow);
    DHWPredictService      dhw_predict(ca_state, stores.predict, ca_time);
    dhw_predict.load_history();

    // Gas correction interactor — после gas_flow (нужен для integral_m3)
    GasCorrectionInteractor gas_corr(ca_state, stores.gas, ca_log,
                                      &ca_time, &gas_flow);
    gas_corr.init();

    // Restore saved burner stats from NVS
    burn_cycle_service.load_from_store();

    mod_stats.load_from_store();
    gas_flow.load_integral();

    // Web presenter — все зависимости готовы
    uint32_t total_uptime_base_sec = stores.heating_stats.restore_total_uptime();
    WebPresenterAdapter ca_web(ca_state, ca_log, ca_time,
                               mod_stats, burn_cycle_service,
                               gas_flow, gas_corr, total_uptime_base_sec);
    FreeRtosMqttQueue        ca_mqtt_queue;
    MqttSocketAdapter        ca_mqtt(ca_mqtt_queue, ca_time);
    MqttRendererAdapter ca_mqtt_renderer(ca_web);

    // ── MQTT interactor ──────────────────────────────────
    stores.mqtt.init();

    MqttInteractor mqtt(ca_mqtt, ca_mqtt_queue,
                        stores.mqtt,   // IMqttConfigStore
                        ca_state,
                        sys_cfg,       // IConfigureSystem
                        ca_log,
                        ca_time,
                        ca_mqtt_renderer,  // IMqttStateRenderer
                        &ca_log);          // IEventLogReader (journal callback)

    // MQTT инициализируется всегда (если enabled в NVS).
    // Время нужно только для таймстемпов в логах, MQTT работает без него.
    mqtt.init();

    if (!ca_time.has_valid_time()) {
        ESP_LOGW("main", "SNTP: время недостоверно, но MQTT запущен");
        ca_log.event(ILogger::SYSTEM, "SNTP: время недостоверно, MQTT запущен без времени");
    }

    // ── Phase 6: Main poller ─────────────────────────────
    ControlLoopInteractor control_loop;
    control_loop.add(&boiler_poll);
    control_loop.add(&sensors_poll);
    control_loop.add(&pid_poll);
    control_loop.add(&mod_stats);
    control_loop.add(&burn_cycle_service);
    control_loop.add(&gas_flow);
    control_loop.add(&dhw_predict);
    control_loop.add(&mqtt);       // MQTT: публикация после обновления состояния

    // ── Phase 7: Hardware init + OTA adapters ──────────────
    ca_sensors.init();
    ca_ot_hw.init();

    EspOtaAdapter          ota_downloader;
    OtaVersionIndexAdapter ota_versions;
    auto ota_now_ms = [&]() { return ca_time.monotonic_us() / 1000; };
    auto ota_spawn  = [](OtaInteractor* self) -> bool {
        return xTaskCreate([](void* arg) { static_cast<OtaInteractor*>(arg)->run_download(); },
                           "ota_dl", 12*1024, self, 3, nullptr) == pdPASS;
    };
    auto ota_reboot = [&]() { ota_flush_and_reboot(burn_cycle_service, mod_stats,
        stores.heating_stats, ca_state, gas_flow, gas_corr, total_uptime_base_sec, ca_time); };
    OtaInteractor ota(ota_validity, ota_downloader, ota_versions,
                      ota_now_ms, ota_spawn, ota_reboot);
    control_loop.add(&ota);        // OTA: синхронизация прогресса загрузки

    ControlLoopTaskAdapter poll_task(control_loop);
    poll_task.start();
    ESP_LOGI("main", "Задача опроса запущена (7 IControlTask)");

    // ── Phase 8: HTTP server ─────────────────────────────
    HttpControllerAdapter http(ca_web, sys_cfg, sys_cfg, gas_corr, sys_cfg,
                               wifi, ca_time, mqtt);

    // ── OTA: взведение валидации (HTTP поднят) ────────────
    ota_validity.set_http_server_up(true);
    ota_validity.arm();
    http.set_ota(&ota);
    http.start();

    // ── Supervision + Persistence ──────────────────────────
    SupervisionLoopInteractor  supervision(ota_validity, ca_log, http, wifi, ca_time);
    PersistenceLoopInteractor  persister(ota_validity, burn_cycle_service, mod_stats,
        gas_flow, gas_corr, stores.heating_stats, ca_state, ca_time, total_uptime_base_sec);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(15000));
        supervision.tick();
        persister.tick();
    }
}
