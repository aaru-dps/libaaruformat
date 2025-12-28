#!/bin/bash

# Quick script to build and run tests with Address Sanitizer
# Usage: ./run-asan-tests.sh [asan_options]
#
# Example:
#   ./run-asan-tests.sh                           # Run with default ASan options
#   ./run-asan-tests.sh detect_leaks=1            # Enable leak detection
#   ./run-asan-tests.sh "detect_leaks=1:halt_on_error=0"  # Multiple options

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Build directory for ASan
BUILD_DIR="build-asan"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Address Sanitizer Test Runner${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Check if build directory exists and is configured
if [ ! -d "$BUILD_DIR" ] || [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo -e "${YELLOW}Build directory '$BUILD_DIR' not configured. Setting up...${NC}"
    mkdir -p "$BUILD_DIR"

    echo -e "${BLUE}Running CMake configuration...${NC}"
    cmake -DUSE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug -B "$BUILD_DIR" -S . || {
        echo -e "${RED}CMake configuration failed!${NC}"
        exit 1
    }
fi

# Build the project
echo -e "${BLUE}Building with Address Sanitizer...${NC}"
cmake --build "$BUILD_DIR" -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) || {
    echo -e "${RED}Build failed!${NC}"
    exit 1
}

echo -e "${GREEN}Build completed successfully!${NC}"
echo ""

# Set up ASan options
if [ -n "$1" ]; then
    export ASAN_OPTIONS="$1"
    echo -e "${YELLOW}Using custom ASAN_OPTIONS: $ASAN_OPTIONS${NC}"
else
    # Default options: print stats, use colors
    # Note: detect_leaks is not supported on macOS, so we don't enable it by default
    if [[ "$OSTYPE" == "darwin"* ]]; then
        export ASAN_OPTIONS="print_stats=1:color=always"
        echo -e "${YELLOW}Using default ASAN_OPTIONS (macOS): $ASAN_OPTIONS${NC}"
        echo -e "${YELLOW}Note: leak detection not supported on macOS${NC}"
    else
        export ASAN_OPTIONS="detect_leaks=1:print_stats=1:color=always"
        echo -e "${YELLOW}Using default ASAN_OPTIONS: $ASAN_OPTIONS${NC}"
    fi
fi

echo ""
echo -e "${BLUE}Running tests...${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Run tests
cd "$BUILD_DIR/tests"
if ctest --verbose --output-on-failure; then
    echo ""
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}All tests passed!${NC}"
    echo -e "${GREEN}========================================${NC}"
    exit 0
else
    echo ""
    echo -e "${RED}========================================${NC}"
    echo -e "${RED}Tests failed!${NC}"
    echo -e "${RED}========================================${NC}"
    echo ""
    echo -e "${YELLOW}Check the output above for Address Sanitizer reports.${NC}"
    echo -e "${YELLOW}See docs/ASAN_USAGE.md for help interpreting the results.${NC}"
    exit 1
fi

