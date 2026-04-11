# AGENTS.md - AI Assistant Guidelines for libaaruformat

## Project Overview

**libaaruformat** is a C implementation of the AaruFormat disk image format for the [Aaru Data Preservation Suite](https://github.com/aaru-dps/Aaru). The library handles reading and writing AaruFormat V1 and V2 disk images with support for compression, checksums, metadata, console disc encryption/decryption, and erasure coding for data recovery.

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
│   ├── internal.h     # Library-internal declarations
│   ├── erasure_internal.h  # Erasure coding internals
│   ├── log.h          # Logging macros (TRACE/FATAL)
│   └── aaruformat/    # Detailed headers
│       ├── structs/   # Packed struct definitions (header, data, ddt, index,
│       │              #   metadata, dump, checksum, optical, tape, flux,
│       │              #   lisa_tag, options, erasure)
│       ├── context.h  # aaruformatContext structure
│       ├── enums.h    # All enumerations
│       ├── consts.h   # Constants
│       ├── errors.h   # Error codes and error_string()
│       ├── decls.h    # Public function declarations
│       ├── endian.h   # Byte-order helpers
│       ├── simd.h     # SIMD feature detection
│       ├── crc64.h    # CRC64 API
│       ├── spamsum.h  # SpamSum API
│       ├── flac.h     # FLAC helpers
│       ├── lru.h      # LRU cache API
│       ├── hash_map.h # Hash map for deduplication
│       └── static_lru_hash_map.h
├── src/               # Source files
│   ├── blocks/        # Block-level read/write (data, metadata, optical,
│   │                  #   dump, checksum, tape, flux)
│   ├── checksum/      # Checksum implementations (MD5, SHA1, SHA256, SpamSum,
│   │                  #   ECC CD, SIMD detection)
│   ├── compression/   # Compression (LZMA, FLAC, Zstd, CST)
│   ├── crc64/         # CRC64 with SIMD (CLMUL on x86, VMULL on ARM)
│   ├── ddt/           # Deduplication table (V1/V2, hash_map, static_lru)
│   ├── index/         # Index structures (V1/V2/V3)
│   ├── lib/           # Shared utility libraries
│   │   ├── aes128.c/h     # AES-128 (shared by PS3 and Wii U crypto)
│   │   ├── gf256.c/h      # GF(2^8) arithmetic with SIMD (AVX2/SSSE3/NEON)
│   │   └── reed_solomon.c/h  # Reed-Solomon erasure codec
│   ├── ps3/           # PS3 disc encryption/decryption and encryption maps
│   ├── wiiu/          # Wii U disc encryption/decryption
│   ├── ngcw/          # NGC/Wii support (LFG PRNG junk, Wii crypto)
│   ├── erasure.c      # Erasure coding read/write integration
│   ├── open.c         # Image opening (V1/V2, backup header recovery)
│   ├── close.c        # Image closing and resource cleanup
│   ├── close_write.c  # Write finalization (DDT, metadata, index serialization)
│   ├── read.c         # Sector reading (with EC recovery and re-encryption)
│   ├── write.c        # Sector writing (block accumulation, compression, DDT)
│   ├── create.c       # Image creation
│   ├── verify.c       # Image verification
│   ├── identify.c     # Format identification
│   ├── helpers.c      # DataType <-> MediaTagType conversion
│   ├── lru.c          # LRU block cache
│   ├── lisa_tag.c     # Apple Lisa tag handling
│   ├── metadata.c     # Metadata reading
│   ├── metadata_write.c  # Metadata writing
│   ├── dump.c         # Dump hardware info
│   ├── options.c      # Image options
│   └── time.c         # Time helpers
├── tests/             # Unit tests using Google Test (C++)
├── tool/              # Command-line tool (aaruformattool)
│   ├── ps3/           # PS3 convert tool (IRD, SFO, ISO9660 parsers)
│   ├── wiiu/          # Wii U convert tool (WUD/WUX reader)
│   └── ngcw/          # NGC/Wii convert tool
├── 3rdparty/          # Third-party dependencies (BLAKE3, FLAC, LZMA, xxHash,
│                      #   Zstd, uthash, slog)
├── docs/              # Documentation and format specification
│   ├── spec/          # AsciiDoc format specification
│   └── ASAN_USAGE.md  # Address Sanitizer guide
├── cmake-modules/     # Custom CMake find modules (FindLibreSSL)
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
- `-DBUILD_SHARED_LIBS=OFF` - Build as static library (default: shared)
- `-DBUILD_TOOL=ON` - Build the CLI tool (requires Argtable3, ICU, curses)

### Running Tests
```bash
cd build
ctest --verbose
```

### Generating Documentation
```bash
cmake --build . --target doxygen
```

### Format Specification (PDF)
```bash
cmake --build . --target spec
```

## Code Style and Conventions

### General Guidelines
- Target C99 standard (C89 compatible where possible)
- Use POSIX-compatible code for portability
- Avoid external runtime dependencies (static linking of 3rd party libraries)
- Use `snake_case` for function and variable names
- Prefix public API functions with `aaruf_` or `aaru_`
- Use `TRACE`/`FATAL` macros from `log.h` for logging; never log key material

### Memory Management
- Always check memory allocations for NULL
- Free all allocated memory before returning from functions
- Use `memset(ptr, 0, size)` before `free()` for sensitive data (crypto keys)
- Use the LRU cache (`src/lru.c`) for block caching
- Consider Address Sanitizer for memory debugging (`-DUSE_ASAN=ON`)

### Error Handling
- Return error codes from `include/aaruformat/errors.h`
- Use `AARUF_STATUS_OK` (0) for success
- Negative values for fatal errors, positive for sector status
- Document error conditions in function headers

### Endianness
- Use helpers from `include/aaruformat/endian.h` for cross-platform byte ordering
- AaruFormat uses little-endian on disk

### Platform Support
- macOS (x86_64, arm64)
- Linux (x86_64, aarch64, armv7, mips, riscv64)
- Windows (MSVC, MinGW - x86, x64, ARM, ARM64)

## Key Components

### Public API (`include/aaru.h`, `include/aaruformat.h`)
The main entry points for opening, reading, writing, and closing AaruFormat images.

### Context Structure
The `aaruformatContext` structure (in `include/aaruformat/context.h`) holds all state for an open image. Always use the provided API functions to manipulate it. Not thread-safe — each context should be used from a single thread.

### Compression
- **LZMA**: General-purpose compression for data blocks
- **Zstd**: Alternative general-purpose compression
- **FLAC**: Lossless audio compression for CD audio tracks
- **CST**: Claunia Subchannel Transform for CD subchannel data

### Checksums
- MD5, SHA1, SHA256 for data integrity
- SpamSum for fuzzy hashing
- BLAKE3 for fast hashing during write operations
- CRC64 with SIMD acceleration (CLMUL on x86, VMULL on ARM)

### Deduplication
The DDT (Deduplication Table) uses xxHash and hash maps for block deduplication during write operations. Supports single-level and multi-level (primary/secondary) DDTs.

### Console Disc Encryption
- **PS3**: Per-sector AES-128-CBC; IV = sector number as 128-bit big-endian. Key derived via AES-128-CBC-Encrypt. IRD file parsing for key extraction.
- **Wii U**: Per-0x8000-byte-sector AES-128-CBC with IV=0. Two-tier keys: disc key for SI/UP/GI partitions, per-title keys for GM partitions. WUD/WUX format support.
- **NGC/Wii**: Wii partition crypto, LFG PRNG junk detection/generation.

### Erasure Coding
Reed-Solomon (RS) and XOR parity for data recovery. Five protection groups: data blocks (interleaved stripes), DDT-secondary (batch), DDT-primary (replicas), metadata (batch), index (replicas). GF(2^8) arithmetic with 4-path SIMD dispatch (AVX2, SSSE3, NEON, scalar). Parity computed on raw on-disk (compressed) bytes. Recovery is transparent in `aaruf_read_sector()`. Recovery footer at EOF with backup header.

## Testing

Tests are in the `tests/` directory using Google Test framework:
- `crc64.cpp` - CRC64 implementation tests
- `spamsum.cpp` - SpamSum hash tests
- `open_image.cpp` - Image opening tests
- `create_image.cpp` - Image creation tests
- `verify_image.cpp` - Image verification tests
- `identify.cpp` - Format identification tests
- Checksum tests: `md5.cpp`, `sha1.cpp`, `sha256.cpp`
- Compression tests: `flac.cpp`, `lzma.cpp`
- PS3 tests: `ps3_aes.cpp`, `ps3_crypto.cpp`, `ps3_encryption_map.cpp`, `ps3_ird.cpp`, `ps3_sfo.cpp`, `ps3_iso9660.cpp`
- NGC/Wii tests: `ngcw.cpp`
- Erasure coding tests: `reed_solomon.cpp`, `erasure_coding.cpp`
- `large_file_io.cpp` - Large file (>2 GiB) I/O tests
- `mode2_nocrc.cpp`, `mode2_errored.cpp` - CD Mode 2 edge cases

Test data files are in `tests/data/`.

## Documentation

- API documentation is generated with Doxygen
- Format specification is in `docs/spec/` (AsciiDoc, built with `asciidoctor-pdf`)
- Address Sanitizer usage guide: `docs/ASAN_USAGE.md`

## Important Notes for AI Agents

1. **No external runtime dependencies**: All third-party code is statically linked from `3rdparty/`.

2. **Format versions**: V1 is read-only; V2 supports full read/write. Never implement V1 write support.

3. **Thread safety**: The library is not thread-safe. Each context should be used from a single thread.

4. **Test before committing**: Always run `ctest --verbose` after making changes.

5. **Cross-platform**: Changes must work on all supported platforms. Avoid platform-specific code without appropriate guards.

6. **Memory**: Use Address Sanitizer (`-DUSE_ASAN=ON`) when debugging memory issues.

7. **NuGet package**: The library is distributed as a NuGet package with pre-built binaries in `runtimes/`. Update version in `libaaruformat.nuspec` when releasing.

8. **Crypto key handling**: Zero key material with `memset()` before `free()`. Never log key material.

9. **Erasure coding parity**: Must be computed on raw on-disk (compressed) bytes, not uncompressed data. A single bit flip in a compressed block causes decompression failure.

10. **Media tags**: Stored in uthash hash table (`ctx->mediaTags`). Bidirectional DataType <-> MediaTagType conversion in `src/helpers.c`.

11. **Write finalization order**: See `src/close_write.c` — header rewrite, close final block, DDTs, checksums, tracks, metadata blocks, index block, then header rewrite with final indexOffset.

12. **Spec documents**: AsciiDoc in `docs/spec/`. When adding/removing appendixes, search all `.adoc` files for `Annex [A-Z]` references and update cross-references.

## Related Projects

- [Aaru](https://github.com/aaru-dps/Aaru) - Main disk image management application (C#)
- AaruFormat specification - See `docs/spec/` for the format documentation

