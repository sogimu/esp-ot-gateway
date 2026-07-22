# ── Firmware version from git tags ──
execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    OUTPUT_VARIABLE FIRMWARE_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT FIRMWARE_VERSION)
    set(FIRMWARE_VERSION "unknown")
endif()
message(STATUS "Firmware version: ${FIRMWARE_VERSION}")

idf_component_register(
    SRCS
        "main.cpp"
        "c_legacy/opentherm.c"
        "c_legacy/sensors.c"
        "infrastructure/driving/http_controller_adapter.cpp"
        "infrastructure/driven/sntp_time_adapter.cpp"
        "infrastructure/driven/crash_diagnostics_adapter.cpp"
        "infrastructure/driven/event_log_adapter.cpp"
        "infrastructure/driven/heating_state_adapter.cpp"
        "infrastructure/driven/ot_hardware_adapter.cpp"
        "infrastructure/driven/boiler_opentherm_adapter.cpp"
        "infrastructure/driven/temperature_sensor_adapter.cpp"
        "infrastructure/driven/web_presenter_adapter.cpp"
        "infrastructure/driven/boiler_nvs_store.cpp"
        "infrastructure/driven/gas_correction_nvs_store.cpp"
        "infrastructure/driven/predict_nvs_store.cpp"
        "infrastructure/driven/esp_ota_adapter.cpp"
        "infrastructure/driven/ota_version_index_adapter.cpp"
        "infrastructure/driven/ota_validity_adapter.cpp"
        "infrastructure/driven/nvs_config_store.cpp"
        "infrastructure/driving/main_poller_task_adapter.cpp"
        "infrastructure/driving/ota_interactor.cpp"
        "infrastructure/driving/ota_controller.cpp"
        "infrastructure/freertos/shared_mutex.c"
        "application/use_cases/main_poller_interactor.cpp"
        "application/use_cases/boiler_poll_interactor.cpp"
        "application/use_cases/sensors_poll_interactor.cpp"
        "application/use_cases/pid_poll_interactor.cpp"
        "application/use_cases/system_config_interactor.cpp"
        "application/use_cases/gas_correction_interactor.cpp"
        "application/services/modulation_stats_service.cpp"
        "application/services/burn_cycle_service.cpp"
        "application/services/gas_flow_estimator.cpp"
        "application/services/dhw_hysteresis_service.cpp"
	    "application/services/dhw_predict_service.cpp"
        "application/services/pid_quality_assessor.cpp"
        "application/services/fopdt_estimator.cpp"
        "application/services/schedule_service.cpp"
        "domain/value_objects/wifi_validation.cpp"
        "infrastructure/driven/wifi_nvs_store.cpp"
        "infrastructure/driven/esp32_wifi_adapter.cpp"
        "infrastructure/driven/dns_captive_server.cpp"
        "infrastructure/driving/wifi_apsta_adapter.cpp"

        # MQTT
        "infrastructure/freertos/mqtt_queue.c"
        "infrastructure/driven/mqtt_client_adapter.cpp"
        "infrastructure/driven/mqtt_socket_adapter.cpp"
        "infrastructure/driven/mqtt_renderer_adapter.cpp"
        "infrastructure/driven/mqtt_nvs_store.cpp"
        "infrastructure/driving/mqtt_interactor.cpp"
    INCLUDE_DIRS
        "application/use_cases"
        "application/services"
        "infrastructure/driven"
        "infrastructure/driving"
        "."
        "domain"
        "domain/value_objects"
        "domain/entities"
        "domain/services"
        "application/ports/driving"
        "application/ports/driven"
        "infrastructure/freertos"
        "c_legacy"
    REQUIRES
        esp_wifi
        esp_http_server
        nvs_flash
        esp_netif
        driver
        esp_timer
        esp_event
        log
        espcoredump
        mqtt
        app_update
        esp_https_ota
        esp_http_client
    )

# ── Firmware version compile definition ──
idf_component_get_property(main_lib main COMPONENT_LIB)
target_compile_definitions(${main_lib} PRIVATE
    FIRMWARE_VERSION="${FIRMWARE_VERSION}"
)

# Override lwip PBUF_POOL_SIZE (default 16 → 32) — more pbufs for concurrent
# HTTP + MQTT traffic, preventing "tcp_write error, errno=No more processes".
idf_component_get_property(lwip_lib lwip COMPONENT_LIB)
target_compile_definitions(${lwip_lib} PRIVATE PBUF_POOL_SIZE=32)

# Ensure http_controller_adapter.cpp is recompiled when web_page_gz.inc changes
# by touching the source file, forcing the build system to detect the change.
set(WEB_PAGE_GZ_INC "${CMAKE_CURRENT_SOURCE_DIR}/infrastructure/driving/web_page_gz.inc")
set(WEB_PAGE_H     "${CMAKE_CURRENT_SOURCE_DIR}/infrastructure/driving/web_page.h")
set(HTTP_CTRL_CPP  "${CMAKE_CURRENT_SOURCE_DIR}/infrastructure/driving/http_controller_adapter.cpp")
set(COMPRESS_PY    "${CMAKE_CURRENT_SOURCE_DIR}/../compress_web_page.py")

add_custom_command(
    OUTPUT ${WEB_PAGE_GZ_INC}
    COMMAND python3 ${COMPRESS_PY} ${WEB_PAGE_H} ${WEB_PAGE_GZ_INC}
    COMMAND ${CMAKE_COMMAND} -E touch ${HTTP_CTRL_CPP}
    DEPENDS ${WEB_PAGE_H} ${COMPRESS_PY}
    COMMENT "Compressing web page (web_page.h → web_page_gz.inc) + marking .cpp for rebuild"
    VERBATIM
)

add_custom_target(web_page_gz DEPENDS ${WEB_PAGE_GZ_INC})
