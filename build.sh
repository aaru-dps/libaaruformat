#!/bin/bash
## Linux (ARMv7-A)
# Detected system processor: arm
rm -f CMakeCache.txt
mkdir -p runtimes/linux-arm/native
docker run --rm dockcross/linux-armv7a-lts >docker/dockcross-linux-arm
chmod +x docker/dockcross-linux-arm
docker/dockcross-linux-arm cmake -DCMAKE_BUILD_TYPE=Release -DAARU_BUILD_PACKAGE=1 .
docker/dockcross-linux-arm make
mv libaaruformat.so runtimes/linux-arm/native/

## Linux (ARM64)
# Detected system processor: aarch64
rm -f CMakeCache.txt
mkdir -p runtimes/linux-arm64/native
docker run --rm dockcross/linux-arm64-lts >docker/dockcross-linux-arm64
chmod +x docker/dockcross-linux-arm64
docker/dockcross-linux-arm64 cmake -DCMAKE_BUILD_TYPE=Release -DAARU_BUILD_PACKAGE=1 .
docker/dockcross-linux-arm64 make
mv libaaruformat.so runtimes/linux-arm64/native/

## Linux (MIPS64)
# Detected system processor: mips
rm -f CMakeCache.txt
mkdir -p runtimes/linux-mips64/native
docker run --rm dockcross/linux-mips >docker/dockcross-linux-mips64
chmod +x docker/dockcross-linux-mips64
docker/dockcross-linux-mips64 cmake -DCMAKE_BUILD_TYPE=Release -DAARU_BUILD_PACKAGE=1 .
docker/dockcross-linux-mips64 make
mv libaaruformat.so runtimes/linux-mips64/native/

## Linux (ARM64), musl
# Detected system processor: aarch64
rm -f CMakeCache.txt
mkdir -p runtimes/linux-musl-arm64/native
docker run --rm dockcross/linux-arm64-musl >docker/dockcross-linux-musl-arm64
chmod +x docker/dockcross-linux-musl-arm64
docker/dockcross-linux-musl-arm64 cmake -DCMAKE_BUILD_TYPE=Release -DAARU_BUILD_PACKAGE=1 .
docker/dockcross-linux-musl-arm64 make
mv libaaruformat.so runtimes/linux-musl-arm64/native/

## Linux (s390x)
# Detected system processor: s390x
rm -f CMakeCache.txt
mkdir -p runtimes/linux-s390x/native
docker run --rm dockcross/linux-s390x >docker/dockcross-linux-s390x
chmod +x docker/dockcross-linux-s390x
docker/dockcross-linux-s390x cmake -DCMAKE_BUILD_TYPE=Release -DAARU_BUILD_PACKAGE=1 .
docker/dockcross-linux-s390x make
mv libaaruformat.so runtimes/linux-s390x/native/

## Linux (amd64)
# Detected system processor: x86_64
rm -f CMakeCache.txt
mkdir -p runtimes/linux-x64/native
docker run --rm dockcross/linux-x64 >docker/dockcross-linux-x64
chmod +x docker/dockcross-linux-x64
docker/dockcross-linux-x64 cmake -DCMAKE_BUILD_TYPE=Release -DAARU_BUILD_PACKAGE=1 .
docker/dockcross-linux-x64 make
mv libaaruformat.so runtimes/linux-x64/native/

## Linux (x86)
# Detected system processor: i686
rm -f CMakeCache.txt
mkdir -p runtimes/linux-x86/native
docker run --rm dockcross/linux-x86 > docker/dockcross-linux-x86
chmod +x docker/dockcross-linux-x86
docker/dockcross-linux-x86 cmake -DCMAKE_BUILD_TYPE=Release -DAARU_BUILD_PACKAGE=1 .
docker/dockcross-linux-x86 make
mv libaaruformat.so runtimes/linux-x86/native/

## Linux (ppc64le)
# Detected system processor: ppc64le
rm -f CMakeCache.txt
mkdir -p runtimes/linux-ppc64le/native
docker run --rm dockcross/linux-ppc64le > docker/dockcross-linux-ppc64le
chmod +x docker/dockcross-linux-ppc64le
docker/dockcross-linux-ppc64le cmake -DCMAKE_BUILD_TYPE=Release -DAARU_BUILD_PACKAGE=1 .
docker/dockcross-linux-ppc64le make
mv libaaruformat.so runtimes/linux-ppc64le/native/

## Windows (ARM)
# Detected system processor: arm
rm -f CMakeCache.txt
mkdir -p runtimes/win-arm/native
docker run --rm dockcross/windows-armv7 > docker/dockcross-win-arm
chmod +x docker/dockcross-win-arm
docker/dockcross-win-arm cmake -DCMAKE_BUILD_TYPE=Release -DAARU_BUILD_PACKAGE=1 .
docker/dockcross-win-arm make
mv libaaruformat.dll runtimes/win-arm/native/

## Windows (ARM64)
# Detected system processor: aarch64
rm -f CMakeCache.txt
mkdir -p runtimes/win-arm64/native
docker run --rm dockcross/windows-arm64 > docker/dockcross-win-arm64
chmod +x docker/dockcross-win-arm64
docker/dockcross-win-arm64 cmake -DCMAKE_BUILD_TYPE=Release -DAARU_BUILD_PACKAGE=1 .
docker/dockcross-win-arm64 make
mv libaaruformat.dll runtimes/win-arm64/native/

## Windows (AMD64)
# Detected system processor: x86_64
# TODO: Requires MSVCRT.DLL
rm -f CMakeCache.txt
mkdir -p runtimes/win-x64/native
docker run --rm dockcross/windows-shared-x64 >docker/dockcross-win-x64
chmod +x docker/dockcross-win-x64
docker/dockcross-win-x64 cmake -DCMAKE_BUILD_TYPE=Release -DAARU_BUILD_PACKAGE=1 .
docker/dockcross-win-x64 make
mv libaaruformat.dll runtimes/win-x64/native/

## Windows (x86)
# Detected system processor: i686
# TODO: Requires MSVCRT.DLL
rm -f CMakeCache.txt
mkdir -p runtimes/win-x86/native
docker run --rm dockcross/windows-shared-x86 > docker/dockcross-win-x86
chmod +x docker/dockcross-win-x86
docker/dockcross-win-x86 cmake -DCMAKE_BUILD_TYPE=Release -DAARU_BUILD_PACKAGE=1 .
docker/dockcross-win-x86 make
mv libaaruformat.dll runtimes/win-x86/native/

## Mac OS X (arm64 and x64)
if [[ ${OS_NAME} == Darwin ]]; then
    rm -f CMakeCache.txt
    cmake -DCMAKE_BUILD_TYPE=Release -DAARU_BUILD_PACKAGE=1 -DAARU_MACOS_TARGET_ARCH=x86_64 .
    make
    mkdir -p runtimes/osx-x64/native
    mv libaaruformat.dylib runtimes/osx-x64/native

    rm -f CMakeCache.txt
    cmake -DCMAKE_BUILD_TYPE=Release -DAARU_BUILD_PACKAGE=1 -DAARU_MACOS_TARGET_ARCH=arm64 .
    make
    mkdir -p runtimes/osx-arm64/native
    mv libaaruformat.dylib runtimes/osx-arm64/native
fi

