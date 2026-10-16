# libaaruformat

**libaaruformat** is a C library for reading and writing
[AaruFormat](https://github.com/aaru-dps/Aaru) disk images — the native image format of the
[Aaru Data Preservation Suite](https://github.com/aaru-dps/Aaru).

## What is Aaru?

[Aaru](https://github.com/aaru-dps/Aaru) is an open-source application for managing and preserving
digital storage media. It can create, convert, compare, and analyze disk images from a wide variety
of physical formats — floppy disks, hard drives, optical discs, tapes, memory cards, and more.
Aaru is written in C# and runs on Windows, macOS, and Linux.

## What is AaruFormat?

AaruFormat is Aaru's own disk image file format, designed from the ground up for long-term archival
and data preservation. Unlike most existing image formats, which target a single media family or
prioritize emulation speed, AaruFormat aims to be a single, universal, extensible standard capable
of faithfully representing any kind of digital or analog storage media — from punch cards to Blu-ray
discs to magnetic flux captures.

Key design goals of the format include:

- **Universality**: one format for all media types, eliminating the need for a different format per
  storage technology.
- **Completeness**: stores not just raw sector data, but also per-sector metadata (subchannel data,
  error flags, tags), per-media metadata (TOCs, catalog numbers, serial numbers), dump hardware
  information, and CHS geometry.
- **Compression**: blocks of sectors are compressed individually (LZMA, Zstd, or FLAC for audio),
  allowing random access without decompressing the entire image.
- **Deduplication**: a Deduplication Table (DDT) maps each sector to a content-addressed block via
  xxHash, so identical sectors across the image are stored only once.
- **Integrity**: every block carries a CRC64 checksum. Hashes (MD5, SHA1, SHA256, SpamSum, BLAKE3)
  are computed during writing and stored in the image.
- **Resilience**: optional Reed-Solomon erasure coding protects data blocks, deduplication tables,
  metadata, and the index against corruption, with transparent recovery on read.
- **Openness**: the format specification is freely available in `docs/spec/` and is published under
  the same terms as the library.

## What is libaaruformat?

**libaaruformat** is the reference implementation of AaruFormat, written in C. Aaru itself uses
libaaruformat via P/Invoke, having replaced its earlier C# implementation. The library can also be
used independently by other projects — emulators, forensic tools, archival systems, or anything
written in C or a language with C FFI — to read and write AaruFormat images without depending on
the .NET runtime.

The library is written in C99, has no external runtime dependencies (all third-party code is
statically linked from the `3rdparty/` directory), and builds on macOS, Linux, and Windows. It is
licensed under the LGPL-2.1.

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

> [!CAUTION]
> **`aaruformattool` is a debugging tool for internal and development use only. It is NOT intended
> for end users.**
>
> - It exists solely to help develop, test and debug libaaruformat itself.
> - Using it **can break, damage and corrupt images in unrecoverable ways**.
> - Its commands, options and output may change or disappear at any time without notice, and no
>   support is provided for it.
> - Never run it on your only copy of an image. Never use it to create, convert or modify images you
>   intend to preserve.
>
> To work with AaruFormat images, use [Aaru](https://github.com/aaru-dps/Aaru).

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
- `-DBUILD_TOOL=ON` — Build the `aaruformattool` debugging tool (requires Argtable3, ICU, curses).
  For internal and development use only; it can irreversibly corrupt images and is not meant for end
  users (see [CLI Tool](#cli-tool-aaruformattool))
- `-DAARU_ENABLE_LOGGING=ON` — Enable TRACE/FATAL debug logging. Output goes to stderr; set
  `AARUFORMAT_LOG_FILE=<path>` to append to a file instead, and `AARUFORMAT_LOG_LEVEL=trace|fatal|none`
  to filter it at runtime
- `-DUSE_ASAN=ON` — Enable Address Sanitizer for memory error detection

