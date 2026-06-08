#!/bin/bash
# Build and run all host tests for esp-ot-gateway.
# Requires: cmake, g++-13+, Catch2 v3 (system package: apt install catch2)
#
# Usage:
#   ./test/run_tests.sh              # build and run all tests
#   ./test/run_tests.sh --quick      # run without rebuilding
#   ./test/run_tests.sh "Kalman2D*"  # run only matching tests

set -e
cd "$(dirname "$0")"

BUILD_DIR="../build_test"

if [ "$1" != "--quick" ]; then
    echo "=== Configuring ==="
    mkdir -p "$BUILD_DIR"
    cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release

    echo "=== Building ==="
    cmake --build "$BUILD_DIR" -j$(nproc)
fi

echo "=== Running tests ==="
FILTER="${1:-}"
if [ -n "$FILTER" ] && [ "$FILTER" != "--quick" ]; then
    "$BUILD_DIR/run_tests" "$FILTER"
else
    "$BUILD_DIR/run_tests"
fi

echo ""
echo "Done."
