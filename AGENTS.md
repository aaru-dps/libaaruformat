# AGENTS.md - AI Assistant Guidelines for libaaruformat

## Project Overview

**libaaruformat** is a C implementation of the AaruFormat disk image format for the [Aaru Data Preservation Suite](https://github.com/aaru-dps/Aaru). The library handles reading and writing AaruFormat V1 and V2 disk images with support for compression, checksums, and metadata.

- **Language**: C (C99 standard)
- **License**: LGPL-2.1-only
- **Build System**: CMake (minimum 3.13)
- **Author**: Natalia Portillo (claunia)

## Repository Structure

```
libaaruformat/
├── include/           # Public and internal headers
│   ├── aaru.h         # Main public API header
│   ├── aaruformat.h   # Format-specific public header
│   └── aaruformat/    # Detailed headers (structs, enums, etc.)
├── src/               # Source files
│   ├── blocks/        # Block-level read/write operations
│   ├── checksum/      # Checksum implementations (MD5, SHA1, SHA256, SpamSum)
│   ├── compression/   # Compression (LZMA, FLAC, CST)
│   ├── crc64/         # CRC64 with SIMD optimizations
│   ├── ddt/           # Deduplication table handling
│   └── index/         # Index structures for V1/V2/V3
├── tests/             # Unit tests using Google Test
├── tool/              # Command-line tool (aaruformattool)
├── 3rdparty/          # Third-party dependencies (BLAKE3, FLAC, LZMA, xxHash, uthash)
├── docs/              # Documentation and format specification
└── runtimes/          # Pre-built binaries for NuGet package
```

## Building

### Standard Build
```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### Build Options
- `-DUSE_SLOG=ON` - Enable slog logging for debugging
- `-DUSE_ASAN=ON` - Enable Address Sanitizer for memory error detection

### Running Tests
```bash
cd build
ctest --verbose
```

### Generating Documentation
```bash
cmake --build . --target doxygen
```

## Code Style and Conventions

### General Guidelines
- Target C99 standard (C89 compatible where possible)
- Use POSIX-compatible code for portability
- Avoid external runtime dependencies (static linking of 3rd party libraries)
- Use `snake_case` for function and variable names
- Prefix public API functions with `aaru_` or `aaruformat_`

### Memory Management
- Always check memory allocations for NULL
- Free all allocated memory before returning from functions
- Use the LRU cache (`src/lru.c`) for block caching
- Consider Address Sanitizer for memory debugging (`-DUSE_ASAN=ON`)

### Error Handling
- Return error codes from `include/aaruformat/errors.h`
- Use `AARUF_STATUS_OK` (0) for success
- Document error conditions in function headers

### Endianness
- Use helpers from `include/aaruformat/endian.h` for cross-platform byte ordering
- AaruFormat uses little-endian on disk

### Platform Support
- macOS (x86_64, arm64)
- Linux (x86_64, aarch64, armv7, mips)
- Windows (MSVC, MinGW - x86, x64, ARM, ARM64)

## Key Components

### Public API (`include/aaru.h`, `include/aaruformat.h`)
The main entry points for opening, reading, writing, and closing AaruFormat images.

### Context Structure
The `aaruformatContext` structure holds all state for an open image. Always use the provided API functions to manipulate it.

### Compression
- **LZMA**: General-purpose compression for data blocks
- **FLAC**: Lossless audio compression for CD audio tracks
- **CST**: Claunia Subchannel Transform for CD subchannel data

### Checksums
- MD5, SHA1, SHA256 for data integrity
- SpamSum for fuzzy hashing
- BLAKE3 for fast hashing during write operations
- CRC64 with SIMD acceleration (CLMUL on x86, VMULL on ARM)

### Deduplication
The DDT (Deduplication Table) uses xxHash and hash maps for block deduplication during write operations.

## Testing

Tests are in the `tests/` directory using Google Test framework:
- `crc64.cpp` - CRC64 implementation tests
- `spamsum.cpp` - SpamSum hash tests
- `open_image.cpp` - Image opening tests
- `create_image.cpp` - Image creation tests
- Checksum tests: `md5.cpp`, `sha1.cpp`, `sha256.cpp`
- Compression tests: `flac.cpp`, `lzma.cpp`

Test data files are in `tests/data/`.

## Documentation

- API documentation is generated with Doxygen
- Format specification is in `docs/spec/`
- Address Sanitizer usage guide: `docs/ASAN_USAGE.md`

## Important Notes for AI Agents

1. **No external runtime dependencies**: All third-party code is statically linked from `3rdparty/`.

2. **Format versions**: V1 is read-only; V2 supports full read/write. Never implement V1 write support.

3. **Thread safety**: The library is not thread-safe. Each context should be used from a single thread.

4. **Test before committing**: Always run `ctest --verbose` after making changes.

5. **Cross-platform**: Changes must work on all supported platforms. Avoid platform-specific code without appropriate guards.

6. **Memory**: Use Address Sanitizer (`-DUSE_ASAN=ON`) when debugging memory issues.

7. **NuGet package**: The library is distributed as a NuGet package with pre-built binaries in `runtimes/`. Update version in `libaaruformat.nuspec` when releasing.

## Related Projects

- [Aaru](https://github.com/aaru-dps/Aaru) - Main disk image management application (C#)
- AaruFormat specification - See `docs/spec/` for the format documentation

