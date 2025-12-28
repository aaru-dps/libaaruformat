# Address Sanitizer (ASan) Usage Guide

## Overview

Address Sanitizer (ASan) is a fast memory error detector that helps identify:
- Buffer overflows (heap, stack, and global)
- Use-after-free bugs
- Use-after-return bugs
- Use-after-scope bugs
- Double-free bugs
- Memory leaks
- Invalid pointer operations

This guide explains how to build and test libaaruformat with Address Sanitizer enabled.

## Prerequisites

### Compiler Support

Address Sanitizer requires a modern compiler:
- **GCC**: Version 4.8 or later
- **Clang**: Version 3.1 or later
- **MSVC**: Version 16.9 or later (Visual Studio 2019 16.9+)

### Platform Support

ASan is supported on:
- Linux (x86_64, ARM, AArch64)
- macOS (x86_64, ARM64)
- Windows (x86_64, ARM64)

## Building with Address Sanitizer

### Option 1: Using CMake Command Line

Build the library and tests with ASan enabled:

```bash
# Create a separate build directory for ASan builds
mkdir build-asan
cd build-asan

# Configure with ASan enabled
cmake -DUSE_ASAN=ON ..

# Build the library and tests
cmake --build .
```

### Option 2: Using CMake with Build Type

For additional debug information (recommended):

```bash
mkdir build-asan
cd build-asan

# Configure with ASan and Debug build type
cmake -DUSE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug ..

# Build
cmake --build .
```

### Option 3: Cross-Platform Build Script

For convenience, use the provided build script:

```bash
# On Linux/macOS
./build.sh -DUSE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug

# On Windows (PowerShell)
cmake -DUSE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug -B build-asan -S .
cmake --build build-asan
```

## Running Tests with Address Sanitizer

### Basic Test Execution

Once built with ASan, run the tests normally:

```bash
cd build-asan
ctest --verbose
```

Or run the test executable directly:

```bash
cd build-asan/tests
./tests_run
```

### Understanding ASan Output

When ASan detects an error, it will print a detailed report including:

1. **Error type**: e.g., "heap-buffer-overflow", "use-after-free"
2. **Stack trace**: Shows where the error occurred
3. **Memory allocation context**: Shows where the problematic memory was allocated
4. **Shadow byte legend**: Explains the memory state

Example ASan error output:
```
=================================================================
==12345==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x602000000014 at pc 0x000000400d3c bp 0x7fff5fbff7f0 sp 0x7fff5fbff7e8
READ of size 4 at 0x602000000014 thread T0
    #0 0x400d3b in main example.c:5
    #1 0x7ffff7a05b44 in __libc_start_main (/lib/x86_64-linux-gnu/libc.so.6+0x21b44)
    #2 0x400c48 in _start (a.out+0x400c48)

0x602000000014 is located 0 bytes to the right of 4-byte region [0x602000000010,0x602000000014)
allocated by thread T0 here:
    #0 0x7ffff7aaecf8 in malloc (/usr/lib/x86_64-linux-gnu/libasan.so.4+0xd9cf8)
    #1 0x400d06 in main example.c:4
    #2 0x7ffff7a05b44 in __libc_start_main (/lib/x86_64-linux-gnu/libc.so.6+0x21b44)
=================================================================
```

### Environment Variables

ASan behavior can be customized using the `ASAN_OPTIONS` environment variable:

#### Detect Memory Leaks

```bash
# Enable leak detection (default on Linux, not on macOS by default)
export ASAN_OPTIONS=detect_leaks=1
./tests_run
```

#### Continue After First Error

```bash
# By default, ASan stops after the first error. To continue:
export ASAN_OPTIONS=halt_on_error=0
./tests_run
```

#### Save Error Reports to File

```bash
# Save ASan reports to files instead of stderr
export ASAN_OPTIONS=log_path=/tmp/asan.log
./tests_run
# Reports will be saved as /tmp/asan.log.12345 (with PID)
```

#### Combine Multiple Options

```bash
export ASAN_OPTIONS=detect_leaks=1:log_path=/tmp/asan.log:halt_on_error=0
./tests_run
```

#### Useful Options Summary

| Option | Description | Default |
|--------|-------------|---------|
| `detect_leaks=1` | Enable memory leak detection | 1 (Linux), 0 (macOS) |
| `halt_on_error=0` | Continue after first error | 1 |
| `log_path=path` | Save reports to files | stderr |
| `verbosity=1` | Increase verbosity level | 0 |
| `symbolize=1` | Symbolize stack traces | 1 |
| `abort_on_error=1` | Call abort() on error | 0 |
| `print_stats=1` | Print statistics at exit | 0 |

## Platform-Specific Notes

### Linux

ASan works out of the box on most Linux distributions. Make sure you have debug symbols installed for better stack traces:

```bash
# Ubuntu/Debian
sudo apt-get install binutils

# Fedora/RHEL
sudo dnf install binutils
```

### macOS

**Important Note**: Leak detection (`detect_leaks=1`) is **not supported** on macOS. If you try to enable it, ASan will abort with an error: `AddressSanitizer: detect_leaks is not supported on this platform.`

On macOS, use ASan options without leak detection:

```bash
# Correct for macOS
export ASAN_OPTIONS="print_stats=1:color=always"

# WRONG - will cause abort on macOS
export ASAN_OPTIONS="detect_leaks=1"  # Don't use this!
```

For better symbolization on macOS:

```bash
# Install llvm-symbolizer if using Clang
brew install llvm
export PATH="/usr/local/opt/llvm/bin:$PATH"
```

System Integrity Protection (SIP) does not need to be disabled for basic ASan functionality.

### Windows (MSVC)

On Windows with MSVC, ASan requires:

1. Visual Studio 2019 version 16.9 or later
2. The "C++ AddressSanitizer" component installed

Additional MSVC-specific considerations:

```cmd
REM Run tests in Command Prompt or PowerShell
cd build-asan\tests
.\tests_run.exe

REM For better symbols, ensure PDB files are generated
cmake -DUSE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug ..
```

## Common Issues and Solutions

### Issue: False Positives

**Problem**: ASan reports errors in third-party libraries you can't fix.

**Solution**: Use suppressions file:

1. Create a file `asan_suppressions.txt`:
   ```
   # Suppress leak in third-party library
   leak:libthirdparty.so
   ```

2. Set the suppressions file:
   ```bash
   export ASAN_OPTIONS=suppressions=./asan_suppressions.txt
   ./tests_run
   ```

### Issue: Performance Impact

**Problem**: Tests run significantly slower with ASan.

**Solution**: This is expected. ASan typically adds 2x-3x overhead. Use ASan builds only for debugging, not production.

### Issue: Out of Memory

**Problem**: ASan uses significant additional memory (2x-3x normal).

**Solution**:
- Run fewer tests in parallel
- Increase system swap space if needed
- Test in smaller batches

### Issue: Missing Stack Traces

**Problem**: Stack traces show addresses but no symbols.

**Solution**:
- Build with debug symbols: `-DCMAKE_BUILD_TYPE=Debug`
- Install `llvm-symbolizer` and ensure it's in PATH
- On Linux: Install debug packages for system libraries

## Integration with CI/CD

### GitHub Actions Example

```yaml
name: ASan Tests

on: [push, pull_request]

jobs:
  asan:
    runs-on: ubuntu-latest
    steps:
    - uses: actions/checkout@v2

    - name: Install dependencies
      run: |
        sudo apt-get update
        sudo apt-get install -y cmake build-essential

    - name: Build with ASan
      run: |
        mkdir build-asan
        cd build-asan
        cmake -DUSE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug ..
        cmake --build .

    - name: Run tests with ASan
      run: |
        cd build-asan
        export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
        ctest --verbose
```

### GitLab CI Example

```yaml
asan-tests:
  stage: test
  script:
    - mkdir build-asan && cd build-asan
    - cmake -DUSE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug ..
    - cmake --build .
    - export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
    - ctest --verbose
  artifacts:
    when: on_failure
    paths:
      - build-asan/Testing/Temporary/LastTest.log
```

## Best Practices

1. **Regular Testing**: Run ASan builds regularly, not just when debugging
2. **Separate Build Directory**: Always use a separate build directory for ASan builds
3. **Debug Mode**: Combine ASan with Debug build type for maximum information
4. **Clean Environment**: Test with a clean environment to avoid interference
5. **Document Suppressions**: If you use suppressions, document why each one is needed
6. **CI Integration**: Add ASan tests to your CI pipeline for continuous validation

## Additional Resources

- [AddressSanitizer Documentation (Google)](https://github.com/google/sanitizers/wiki/AddressSanitizer)
- [Clang AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html)
- [GCC Instrumentation Options](https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html)
- [MSVC AddressSanitizer](https://docs.microsoft.com/en-us/cpp/sanitizers/asan)

## Quick Reference

### Build Commands

```bash
# Standard ASan build
cmake -DUSE_ASAN=ON -B build-asan -S .
cmake --build build-asan

# ASan build with debug symbols
cmake -DUSE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug -B build-asan -S .
cmake --build build-asan

# Run tests
cd build-asan && ctest --verbose
```

### Environment Variables

```bash
# Enable all leak detection
export ASAN_OPTIONS=detect_leaks=1

# Save reports to files
export ASAN_OPTIONS=log_path=/tmp/asan.log

# Multiple options
export ASAN_OPTIONS=detect_leaks=1:log_path=/tmp/asan.log:halt_on_error=0
```

## Troubleshooting

If you encounter issues:

1. Check compiler version supports ASan
2. Verify CMake found the correct compiler
3. Look for conflicting compiler flags
4. Check environment variables aren't interfering
5. Try with a minimal test case first
6. Review ASan documentation for your specific platform

For project-specific issues, please file a bug report with:
- Platform and compiler version
- Full CMake configuration output
- Complete ASan error report
- Steps to reproduce

