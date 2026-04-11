# libaaruformat

C implementation of the [AaruFormat](https://github.com/aaru-dps/Aaru) disk image format for the
[Aaru Data Preservation Suite](https://github.com/aaru-dps/Aaru).

Written in C (C99 standard) with no external runtime dependencies. All third-party code is statically
linked from the `3rdparty/` directory.

## Features

### Core
- AaruFormat V1 images reading (writing will never be implemented)
- AaruFormat V2 images reading and writing
- Image identification and verification
- LRU block cache for read performance
- Large file (>2 GiB) I/O support
- Feature parity with the C# implementation

### Compression
- LZMA
- Zstandard (Zstd)
- FLAC (for CD audio tracks)
- Claunia Subchannel Transform (for CD subchannel data)

### Checksums & Hashing
- Hashing while writing: MD5, SHA1, SHA256, SpamSum, BLAKE3
- CRC64 with SIMD acceleration (CLMUL on x86, VMULL on ARM)
- ECC CD checksums

### Erasure Coding & Data Recovery
- Reed-Solomon error correction with configurable K,M parameters
- GF(2^8) arithmetic with 4-path SIMD dispatch (AVX2, SSSE3, ARM NEON, scalar fallback)
- Five protection groups: data blocks, DDT-primary, DDT-secondary, metadata, index
- Transparent recovery on read
- Recovery footer at EOF with backup header

### Console Disc Encryption/Decryption
- **PS3**: Per-sector AES-128-CBC encryption/decryption, encryption map support, IRD file parsing
- **Wii U**: Two-tier encryption (disc key + per-title keys), WUD/WUX format support
- **NGC/Wii**: Wii partition encryption, GameCube/Wii LFG PRNG junk detection and generation

### Media & Metadata
- Optical disc tracks
- Tape files and partitions
- Raw flux transition captures (Kryoflux, Pauline, Applesauce)
- Apple Lisa tag handling
- Dump hardware lists
- CHS geometry retrieval and setting
- XML metadata retrieval (writing will never be implemented)
- JSON metadata retrieval and writing
- Deduplication (xxHash-based with hash maps)
- Media type list in sync with Aaru

### CLI Tool (`aaruformattool`)

Optional command-line tool built with `-DBUILD_TOOL=ON`. Commands include:
- `identify` — Identify AaruFormat version
- `info` — Display detailed image information
- `read` / `read_long` — Read sectors
- `verify` / `verify_sectors` — Verify image and sector integrity
- `compare` — Compare images
- `convert` — Convert between image formats
- `convert-ps3` — Convert PS3 disc images
- `convert-wiiu` — Convert Wii U disc images (WUD/WUX)
- `convert-ngcw` — Convert GameCube/Wii disc images
- `inject-media-tag` — Add media tags to images

### Platform Support
- macOS (x86_64, arm64)
- Linux (x86_64, aarch64, armv7, mips, riscv64)
- Windows (MSVC, MinGW — x86, x64, ARM, ARM64)

### Developer
- Unit testing (Google Test)
- Automatic API documentation generation (Doxygen)
- Format specification in AsciiDoc (`docs/spec/`)

## Things still to be implemented

- Automatic media type generation from C# enumeration
- NuGet package for linking with Aaru
- Snapshots
- Parent images
- Data positioning measurements

## Building and Testing

### Standard Build

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

### Running Tests

```bash
cd build
ctest --verbose
```

### Building with Address Sanitizer

For debugging memory issues, you can build with Address Sanitizer enabled:

```bash
mkdir build-asan
cd build-asan
cmake -DUSE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug ..
cmake --build .
ctest --verbose
```

For detailed information on using Address Sanitizer to detect memory issues,
see [docs/ASAN_USAGE.md](docs/ASAN_USAGE.md).

### Build Options

- `-DBUILD_SHARED_LIBS=OFF` — Build as static library (default: shared)
- `-DBUILD_TOOL=ON` — Build the CLI tool (requires Argtable3, ICU, curses)
- `-DUSE_SLOG=ON` — Enable slog logging for debugging
- `-DUSE_ASAN=ON` — Enable Address Sanitizer for memory error detection

