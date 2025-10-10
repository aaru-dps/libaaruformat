# libaaruformat

C implementation of [Aaru](https://www.github.com/aaru-dps/Aaru) file format.

The target is to be able to compile it with a normal C (C89 compliant) compiler.

Currently depends on libicu being available thru vcpkg due to UTF-16 shenanigans.
Currently under debate on a breaking ABI change to remove this dependency.

cmake is not a hard dependency, it's merely for the ease of using IDEs (specifically CLion).

Currently supported features:
- AaruFormat V1 images reading (writing will never be implemented)
- AaruFormat V2 images reading and writing
- LZMA compression
- Claunia Subchannel Transform
- Optical disc tracks
- XML metadata retrieval (writing will never be implemented)
- JSON metadata retrieval and writing
- Hashing while writing (MD5, SHA1, SHA256, SpamSum and BLAKE3)
- Deduplication
- Tape file and partitions
- Dump hardware lists
- Currently on sync (as of October 2025) with Aaru's media type list
- CHS geometry retrieval and setting
- Metadata
- Unit testing
- Automatic generation of API documentation
- It is to all effects feature parity with C#

Things still to be implemented that are already in the C# version:

- Automatic media type generation from C# enumeration
- Nuget package for linking with Aaru

Things to be implemented not in the C# version (maybe):

- Compile for Dreamcast (KallistiOS preferibly)
- Compile for PlayStation Portable
- Compile for Wii
- Compile for Wii U
- Compile for PlayStation 2
- Compile for PlayStation 3
- Snapshots
- Parent images
- Data positioning measurements