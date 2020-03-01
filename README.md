# libaaruformat
C implementation of [Aaru](https://www.github.com/aaru-dps/Aaru) file format.

Work in progress, don't expect it to work yet.

The target is to be able with a normal C (C89 compliant) compiler, having no external dependencies.
This means any hash or compression algorithm must be statically linked inside the library.

cmake is not a hard dependency, it's merely for the ease of using IDEs (specifically CLion).

Things still to be implemented that are already in the C# version:
- Caching
- Tape file blocks
- Automatic media type generation from C# enumeration
- Nuget package for linking with Aaru
- Writing
- Hashing while writing (requires MD5, SHA1, SHA256 and SpamSum)
- Deduplication (requires SHA256)
- Compression (requires FLAC and LZMA)

Things to be implemented not in the C# version:
- Compile for Dreamcast (KallistiOS preferibly)
- Compile for PlayStation Portable
- Compile for Wii
- Compile for Wii U
- Compile for PlayStation 2
- Compile for PlayStation 3
- Unit testing