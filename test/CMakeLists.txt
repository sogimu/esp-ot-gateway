cmake_minimum_required(VERSION 3.16)
project(esp_ot_gateway_tests LANGUAGES CXX)

# ── Catch2 (system-installed) ─────────────────────────────
find_package(Catch2 3 REQUIRED)

# ── Test source directory ─────────────────────────────────
set(TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(MAIN_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/../main)

# ── Include paths (domain + application only, NO ESP-IDF) ─
set(INCLUDE_DIRS
    ${MAIN_DIR}
    ${MAIN_DIR}/domain
    ${MAIN_DIR}/domain/value_objects
    ${MAIN_DIR}/domain/entities
    ${MAIN_DIR}/domain/services
    ${MAIN_DIR}/application/ports/driving
    ${MAIN_DIR}/application/ports/driven
    ${MAIN_DIR}/application/services
    ${MAIN_DIR}/application/use_cases
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

    # Use case interactors
    test_main_poller.cpp

    # Bug-focused tests (calculation edge cases)
    test_burn_cycle_bugs.cpp
    test_mod_stats_bugs.cpp
    test_gas_flow_bugs.cpp
    test_pid_algorithm_bugs.cpp
    test_hysteresis_bugs.cpp
)

# ── Application .cpp files (compiled into test binary) ────
set(APP_SRCS
    ${MAIN_DIR}/application/services/dhw_hysteresis_service.cpp
    ${MAIN_DIR}/application/services/schedule_service.cpp
    ${MAIN_DIR}/application/services/modulation_stats_service.cpp
    ${MAIN_DIR}/application/services/burn_cycle_service.cpp
    ${MAIN_DIR}/application/services/dhw_predict_service.cpp
    ${MAIN_DIR}/application/services/gas_flow_estimator.cpp
    ${MAIN_DIR}/application/use_cases/main_poller_interactor.cpp
)

# ── Test executable ───────────────────────────────────────
add_executable(run_tests ${TEST_SRCS} ${APP_SRCS})
target_include_directories(run_tests PRIVATE ${INCLUDE_DIRS})

# Link Catch2
target_link_libraries(run_tests PRIVATE Catch2::Catch2WithMain)

# Compiler flags — C++17, warnings (RTTI enabled for Catch2, no exceptions)
target_compile_features(run_tests PRIVATE cxx_std_17)
target_compile_options(run_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)

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
