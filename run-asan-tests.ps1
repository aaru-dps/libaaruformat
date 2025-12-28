# Quick script to build and run tests with Address Sanitizer on Windows
# Usage: .\run-asan-tests.ps1 [asan_options]
#
# Example:
#   .\run-asan-tests.ps1                                # Run with default ASan options
#   .\run-asan-tests.ps1 "detect_leaks=1"               # Enable leak detection
#   .\run-asan-tests.ps1 "detect_leaks=1:halt_on_error=0"  # Multiple options

param(
    [string]$AsanOptions = ""
)

$ErrorActionPreference = "Stop"

# Build directory for ASan
$BuildDir = "build-asan"

Write-Host "========================================" -ForegroundColor Blue
Write-Host "Address Sanitizer Test Runner" -ForegroundColor Blue
Write-Host "========================================" -ForegroundColor Blue
Write-Host ""

# Check if build directory exists and is configured
if (-not (Test-Path $BuildDir) -or -not (Test-Path "$BuildDir/CMakeCache.txt")) {
    Write-Host "Build directory '$BuildDir' not configured. Setting up..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null

    Write-Host "Running CMake configuration..." -ForegroundColor Blue
    try {
        cmake -DUSE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug -B $BuildDir -S .
    }
    catch {
        Write-Host "CMake configuration failed!" -ForegroundColor Red
        exit 1
    }
}

# Build the project
Write-Host "Building with Address Sanitizer..." -ForegroundColor Blue
try {
    cmake --build $BuildDir --config Debug
}
catch {
    Write-Host "Build failed!" -ForegroundColor Red
    exit 1
}

Write-Host "Build completed successfully!" -ForegroundColor Green
Write-Host ""

# Set up ASan options
# Priority: 1) Command line parameter, 2) Existing ASAN_OPTIONS env var, 3) Default
if ($AsanOptions -ne "") {
    $env:ASAN_OPTIONS = $AsanOptions
    Write-Host "Using ASAN_OPTIONS from command line: $env:ASAN_OPTIONS" -ForegroundColor Yellow
}
elseif ($env:ASAN_OPTIONS) {
    Write-Host "Using ASAN_OPTIONS from environment: $env:ASAN_OPTIONS" -ForegroundColor Yellow
    # Add print_stats=1 if not already specified
    if ($env:ASAN_OPTIONS -notmatch "print_stats") {
        $env:ASAN_OPTIONS = "$env:ASAN_OPTIONS`:print_stats=1"
        Write-Host "Added print_stats=1 to your options" -ForegroundColor Yellow
    }
}
else {
    # Default options: print stats (leak detection often not supported on Windows)
    $env:ASAN_OPTIONS = "print_stats=1"
    Write-Host "Using default ASAN_OPTIONS: $env:ASAN_OPTIONS" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Running tests..." -ForegroundColor Blue
Write-Host "========================================" -ForegroundColor Blue
Write-Host ""

# Run tests
Push-Location "$BuildDir/tests"
try {
    ctest --verbose --output-on-failure -C Debug
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "All tests passed!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    Pop-Location
    exit 0
}
catch {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "Tests failed!" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Write-Host ""
    Write-Host "Check the output above for Address Sanitizer reports." -ForegroundColor Yellow
    Write-Host "See docs\ASAN_USAGE.md for help interpreting the results." -ForegroundColor Yellow
    Pop-Location
    exit 1
}

