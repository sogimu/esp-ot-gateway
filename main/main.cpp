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

// ── OTA ──────────────────────────────────────────────────
#include "infrastructure/driving/ota_controller.h"

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
    
    // OTA-контроллер: владеет адаптерами валидности, загрузки, версий
    // и интерактором. Конструктор создаёт OtaValidityAdapter (нужен для
    // is_pending_global — NVS-заморозка D9/D10) и логирует состояние партиции.
    OtaController ota_ctrl;

    CrashDiagnosticsAdapter crash_diag(ca_log);
    crash_diag.start();

    // Связка: признак краха → OTA-валидатор (arm() немедленно откатит при краше)
    ota_ctrl.set_crash_flag(crash_diag.last_boot_had_crash());

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
    MainPollerInteractor main_poller;
    main_poller.add(&boiler_poll);
    main_poller.add(&sensors_poll);
    main_poller.add(&pid_poll);
    main_poller.add(&mod_stats);
    main_poller.add(&burn_cycle_service);
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
    HttpControllerAdapter http(ca_web, sys_cfg, sys_cfg, gas_corr, sys_cfg,
                               wifi, ca_time, mqtt);
    http.start();

    // ── OTA: старт подсистемы (адаптеры + интерактор + валидация) ──
    // start() создаёт FirmwareOtaInteractor, регистрирует flush-stats,
    // взводит валидацию и возвращает IOtaManager для HTTP-хендлеров.
    IOtaManager* ota_mgr = ota_ctrl.start({
        .heating_stats = &stores.heating_stats, .state = &ca_state, .burn_cycle_service = &burn_cycle_service,
        .mod_stats = &mod_stats, .gas_flow = &gas_flow, .gas_corr = &gas_corr,
        .time = &ca_time, .total_uptime_base_sec = &total_uptime_base_sec
    });
    http.set_ota(ota_mgr);

    // ── Idle: CPU stats every 60s, periodic NVS save every 10 min ──
    static const char* TAG = "main";
    int save_tick = 0;
    while (1) {
        // OTA: heartbeat (подтверждение/откат) + опрос прогресса загрузки.
        ota_ctrl.tick();

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

        ESP_LOGI(TAG, "Аптайм: %lld с, куча: своб=%" PRIu32 " крупн=%" PRIu32
                 " блоков: алл=%" PRIu32 " своб=%" PRIu32
                 " | CPU: core0=%d%% core1=%d%% total=%d%%",
                 ca_time.monotonic_us() / 1000000,
                 free_heap, largest_free,
                 (uint32_t)info.allocated_blocks, (uint32_t)info.free_blocks,
                 cpu0, cpu1, (cpu0 + cpu1) / 2);

        // Per-task CPU stats every 5 min (5 ticks)
        if (save_tick % 5 == 0) {
            static char stats_buf[2048];
            vTaskGetRunTimeStats(stats_buf);
            ESP_LOGI(TAG, "── Статистика задач (CPU) ──\n%s", stats_buf);
        }

        // AP watchdog: restart AP if WiFi died
        wifi.try_recover_ap();

        // ── Recovery ladder: prevent silent death from fragmentation ──
        static int recovery_level = 0;  // highest level reached
        if (largest_free < 4096 || free_heap < 8192) {
            // Level 4: last resort — reboot
            ESP_LOGE(TAG, "Куча исчерпана (своб=%" PRIu32 " крупн=%" PRIu32 ") — перезагрузка",
                     free_heap, largest_free);
            ca_log.event(ILogger::SYSTEM, "Перезагрузка: куча исчерпана (%" PRIu32 "/%" PRIu32 ")",
                         free_heap, largest_free);
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();
        } else if (largest_free < 6144 && recovery_level < 3) {
            recovery_level = 3;
            ESP_LOGW(TAG, "Recovery L3: перезапуск HTTP (своб=%" PRIu32 " крупн=%" PRIu32 ")",
                     free_heap, largest_free);
            ca_log.event(ILogger::SYSTEM, "Recovery L3: перезапуск HTTP");
            http.stop();
            vTaskDelay(pdMS_TO_TICKS(1000));
            http.start();
        } else if (largest_free < 12288 && recovery_level < 2) {
            recovery_level = 2;
            ESP_LOGW(TAG, "Recovery L2: фрагментация (своб=%" PRIu32 " крупн=%" PRIu32 ")",
                     free_heap, largest_free);
            ca_log.event(ILogger::SYSTEM, "Recovery L2: фрагментация кучи");
        } else if (largest_free < 16384 && recovery_level < 1) {
            recovery_level = 1;
            ESP_LOGW(TAG, "Recovery L1: предупреждение (своб=%" PRIu32 " крупн=%" PRIu32 ")",
                     free_heap, largest_free);
            ca_log.event(ILogger::SYSTEM, "Recovery L1: фрагментация растёт");
        }
        // Reset ladder when heap recovers
        if (largest_free >= 32768) recovery_level = 0;

        // Save all persistent state to NVS every 10 min (10 ticks).
        //
        // ЗАМОРОЗКА NVS (D9): во время PENDING_VERIFY (до mark_valid) блобы не
        // пишем. Дублирующая защита — основные блобы дополнительно блокируются в
        // самих save_* методах HeatingStatsNvsStore через OtaValidityAdapter::is_pending_global().
        // Если свежезалитая прошивка откатится, данные предыдущей валидной версии
        // останутся нетронутыми. save_tick НЕ сбрасываем во время заморозки, чтобы
        // первый после mark_valid save сработал ближайшей же итерацией (быстрое
        // возобновление персистентности). Штатно mark_valid≈90 c (первый healthy
        // тик), первый save≈150 c — не пересекаются; заморозка даёт гарантию на
        // нештатных путях (медленный HTTP, краш-диагностика).
        if (save_tick >= 10) {
            if (ota_ctrl.is_pending()) {
                ESP_LOGW(TAG, "Периодический NVS-save пропущен: образ на проверке (PENDING_VERIFY)");
            } else {
                save_tick = 0;
                uint32_t bs = burn_cycle_service.burner_seconds();
                burn_cycle_service.save_to_store();
                static NvsHistBlob hist_blob;
                mod_stats.fill_histogram(hist_blob);
                stores.heating_stats.save_stats(ca_state, bs, gas_flow.integral_m3(),
                               &hist_blob, nullptr, nullptr, nullptr);
                stores.heating_stats.save_total_uptime(total_uptime_base_sec +
                                      (uint32_t)(ca_time.monotonic_us() / 1000000));
                stores.gas.save_meter(ca_state, &gas_corr.meter_blob());
            }
        }
    }
}
