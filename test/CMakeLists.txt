cmake_minimum_required(VERSION 3.16)
project(esp_ot_gateway_tests LANGUAGES CXX)

# ── Build type: Debug by default ──────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type (Debug, Release, etc.)" FORCE)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# ── Catch2 (system-installed) ─────────────────────────────
find_package(Catch2 3 REQUIRED)

# ── Test source directory ─────────────────────────────────
set(TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(MAIN_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/../main)

# ── Include paths (domain + application only, NO ESP-IDF) ─
set(INCLUDE_DIRS
    ${TEST_DIR}/stubs          # ESP-IDF stubs (must be first to shadow real includes)
    ${MAIN_DIR}
    ${MAIN_DIR}/domain
    ${MAIN_DIR}/domain/value_objects
    ${MAIN_DIR}/domain/entities
    ${MAIN_DIR}/domain/services
    ${MAIN_DIR}/application/ports/driving
    ${MAIN_DIR}/application/ports/driven
    ${MAIN_DIR}/application/services
    ${MAIN_DIR}/application/use_cases
    ${MAIN_DIR}/infrastructure/driving
    ${MAIN_DIR}/infrastructure/driven
    ${TEST_DIR}/fakes
)

# ── Test source files ─────────────────────────────────────
set(TEST_SRCS
    # Domain — pure logic, no dependencies beyond C++ stdlib
    test_kalman2d.cpp
    test_kalman1d.cpp
    test_pid_algorithm.cpp
    test_ch_schedule.cpp

    # Application services
    test_dhw_hysteresis.cpp
    test_schedule_service.cpp
    test_modulation_stats.cpp
    test_burn_cycle.cpp
    test_dhw_predict.cpp
    test_pid_quality_assessor.cpp
    test_fopdt_estimator.cpp

    # Use case interactors
    test_main_poller.cpp
    test_pid_poll_interactor.cpp
    test_reset_statistics.cpp

    # Bug-focused tests (calculation edge cases)
    test_burn_cycle_bugs.cpp
    test_mod_stats_bugs.cpp
    test_gas_flow_bugs.cpp
    test_pid_algorithm_bugs.cpp
    test_hysteresis_bugs.cpp

    # Burn cycle rounding
    test_burn_cycle_rounding.cpp

    # Config store persistence
    test_config_store_persistence.cpp

    # Timezone persistence
    test_timezone_persistence.cpp

    # JSON helpers (regression: missing key vs -1 value)
    test_json_parsing_missing_key.cpp

    # Gas meter persistence
    test_gas_meter_persistence.cpp

    # NVS nullptr safety regression
    test_nvs_nullptr_safety.cpp

    # Gas correction orchestration (read + save paths)
    test_gas_correction_orchestration.cpp

    # MQTT — ports, queue, client, interactor
    test_mqtt_ports_compile.cpp
    test_mqtt_nvs_config.cpp
    test_mqtt_queue.cpp
    test_mqtt_client_fake.cpp
    test_mqtt_interactor.cpp

    # WiFi provisioning
    test_wifi_validation.cpp
    test_manual_time_offset.cpp
    test_wifi_apsta_adapter.cpp

    # Gas correction metrics (domain pure function)
    test_gas_correction_metrics.cpp

    # Gas integration (JSON rendering, end-to-end)
    test_gas_integration.cpp

    # Boiler model config (blob sizes, defaults, validation)
    test_boiler_model_config.cpp

    # GasFlowService model configuration (DHW, efficiency, gas temp offset)
    test_gas_model_config.cpp

    # Fault codes (OEM + ASF flag decoding, JSON rendering)
    test_fault_codes.cpp
)

# ── Stubs (replace ESP-IDF-dependent implementations) ──────
set(STUB_SRCS
    ${TEST_DIR}/fakes/stub_boiler_poll_interactor.cpp
)

# ── Application .cpp files (compiled into test binary) ────
set(APP_SRCS
    ${MAIN_DIR}/application/services/dhw_hysteresis_service.cpp
    ${MAIN_DIR}/application/services/schedule_service.cpp
    ${MAIN_DIR}/application/services/modulation_stats_service.cpp
    ${MAIN_DIR}/application/services/burn_cycle_service.cpp
    ${MAIN_DIR}/application/services/dhw_predict_service.cpp
    ${MAIN_DIR}/application/services/gas_flow_estimator.cpp
    ${MAIN_DIR}/application/services/fopdt_estimator.cpp
    ${MAIN_DIR}/application/services/pid_quality_assessor.cpp
    ${MAIN_DIR}/application/use_cases/main_poller_interactor.cpp
    ${MAIN_DIR}/application/use_cases/system_config_interactor.cpp
    ${MAIN_DIR}/application/use_cases/gas_correction_interactor.cpp
    ${MAIN_DIR}/application/use_cases/pid_poll_interactor.cpp
    ${MAIN_DIR}/domain/value_objects/wifi_validation.cpp
    ${MAIN_DIR}/infrastructure/driving/wifi_apsta_adapter.cpp
    ${MAIN_DIR}/infrastructure/driven/dns_captive_server.cpp
    # MQTT interactor (testable on host — no ESP-IDF deps)
    ${MAIN_DIR}/infrastructure/driving/mqtt_interactor.cpp
    ${MAIN_DIR}/infrastructure/driven/web_presenter_adapter.cpp
)

# ── Test executable ───────────────────────────────────────
add_executable(run_tests ${TEST_SRCS} ${APP_SRCS} ${STUB_SRCS})
target_include_directories(run_tests PRIVATE ${INCLUDE_DIRS})

# Link Catch2
target_link_libraries(run_tests PRIVATE Catch2::Catch2WithMain)

# Compiler flags — C++17, warnings (RTTI enabled for Catch2, no exceptions)
target_compile_features(run_tests PRIVATE cxx_std_17)
target_compile_options(run_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)

# ── Firmware version (same logic as main/CMakeLists.txt) ──
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

# ── Debug-build hardening (ABI-compatible, safe with prebuilt Catch2) ──
target_compile_definitions(run_tests PRIVATE
    $<$<CONFIG:Debug>:
        _GLIBCXX_ASSERTIONS         # std::vector bounds, std::optional access (ABI-safe)
        DEBUG                       # application-level debug paths
    >
    FIRMWARE_VERSION="${FIRMWARE_VERSION}"
)
target_compile_options(run_tests PRIVATE
    $<$<CONFIG:Debug>:
        -g3                         # max debug info (includes macros)
        -O0                         # no optimisation — predictable debuggable code
    >
)

# ── Sanitizers ────────────────────────────────────────
option(ENABLE_SANITIZERS "Build with maximum sanitizers" ON)
if(ENABLE_SANITIZERS)
    set(SAN_COMPILE
        -fsanitize=address              # buffer overflow, use-after-free, double-free, leaks
        -fsanitize=undefined            # null deref, signed overflow, misaligned, div-by-zero
        -fsanitize=pointer-compare      # pointer comparison across allocations
        -fsanitize=pointer-subtract     # pointer subtraction across allocations
        -fno-omit-frame-pointer         # readable stack traces
        -fno-sanitize-recover=all       # abort on first error (fail fast in CI)
        -fstack-protector-strong        # stack smashing detection
        -fstack-clash-protection        # stack overflow detection
    )
    target_compile_options(run_tests PRIVATE ${SAN_COMPILE})
    target_link_options(run_tests PRIVATE ${SAN_COMPILE})
    target_compile_definitions(run_tests PRIVATE
        _GLIBCXX_ASSERTIONS             # std::vector bounds, std::optional access
        _FORTIFY_SOURCE=3               # buffer overflow in memcpy/sprintf (glibc >=2.34)
    )
endif()

# ── Convenience target: make test ─────────────────────────
enable_testing()
add_test(NAME esp_ot_gateway_tests COMMAND run_tests)

# ── Custom target to build and run ────────────────────────
add_custom_target(check
    COMMAND run_tests
    DEPENDS run_tests
    COMMENT "Running all tests..."
)

# ── Coverage ──────────────────────────────────────────────
option(ENABLE_COVERAGE "Build with code coverage instrumentation" OFF)
if(ENABLE_COVERAGE)
    target_compile_options(run_tests PRIVATE -fprofile-arcs -ftest-coverage)
    target_link_options(run_tests PRIVATE -fprofile-arcs -ftest-coverage)

    find_program(LCOV lcov)
    find_program(GENHTML genhtml)

    if(LCOV AND GENHTML)
        set(COVERAGE_DIR ${CMAKE_BINARY_DIR}/coverage)
        add_custom_target(coverage
            COMMAND ${CMAKE_COMMAND} -E remove_directory ${COVERAGE_DIR}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${COVERAGE_DIR}
            # Run tests (gcda files written on clean exit)
            COMMAND ./run_tests > /dev/null
            # Capture coverage data (disable branch coverage — GCC doesn't emit it)
            COMMAND ${LCOV} --capture --directory . --output-file ${COVERAGE_DIR}/coverage.info
                --rc branch_coverage=0 --ignore-errors unused
            # Exclude test files and system headers from report
            COMMAND ${LCOV} --remove ${COVERAGE_DIR}/coverage.info
                '*/test/*' '*/fakes/*' '/usr/*'
                --output-file ${COVERAGE_DIR}/coverage.info
                --rc branch_coverage=0 --ignore-errors unused
            # HTML report
            COMMAND ${GENHTML} ${COVERAGE_DIR}/coverage.info
                --output-directory ${COVERAGE_DIR}/html
                --title "esp-ot-gateway test coverage"
                --rc branch_coverage=0 --ignore-errors unused,empty,corrupt
            DEPENDS run_tests
            COMMENT "Generating coverage report..."
        )
        add_custom_target(coverage-summary
            COMMAND ${LCOV} --list ${COVERAGE_DIR}/coverage.info
            DEPENDS coverage
            COMMENT "Coverage summary:"
        )
    endif()
endif()
