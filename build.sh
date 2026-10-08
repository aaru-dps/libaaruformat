#!/bin/bash
set -e

usage() {
    echo "Usage: $0 [--mac-only]"
    echo "  -m, --mac-only  Build only the macOS targets"
    echo "  -h, --help      Show this help"
}

MAC_ONLY=0
for arg in "$@"; do
    case "$arg" in
        -m|--mac-only) MAC_ONLY=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $arg" >&2; usage >&2; exit 1 ;;
    esac
done

if [[ $MAC_ONLY == 1 && $(uname) != Darwin ]]; then
    echo "--mac-only requires running on macOS" >&2
    exit 1
fi

# Function to build for a target using dockcross
build_with_dockcross() {
    local docker_image="$1"
    local script_name="$2"
    local output_ext="$3"
    local runtime_dir="$4"
    echo "\n==== Building for $runtime_dir ===="
    rm -f CMakeCache.txt
    mkdir -p "$runtime_dir"
    docker run --rm "$docker_image" > "docker/$script_name"
    chmod +x "docker/$script_name"
    "docker/$script_name" cmake -DCMAKE_BUILD_TYPE=Release -DAARU_BUILD_PACKAGE=1 .
    "docker/$script_name" make
    mv "libaaruformat.$output_ext" "$runtime_dir/"
    rm -f "docker/$script_name"
}

if [[ $MAC_ONLY == 0 ]]; then
# Linux targets
build_with_dockcross dockcross/linux-armv7a-lts dockcross-linux-arm so runtimes/linux-arm/native
build_with_dockcross dockcross/linux-arm64-lts dockcross-linux-arm64 so runtimes/linux-arm64/native
build_with_dockcross dockcross/linux-mips dockcross-linux-mips64 so runtimes/linux-mips64/native
build_with_dockcross dockcross/linux-arm64-musl dockcross-linux-musl-arm64 so runtimes/linux-musl-arm64/native
build_with_dockcross dockcross/linux-s390x dockcross-linux-s390x so runtimes/linux-s390x/native
build_with_dockcross dockcross/linux-x64 dockcross-linux-x64 so runtimes/linux-x64/native
build_with_dockcross dockcross/linux-x86 dockcross-linux-x86 so runtimes/linux-x86/native
build_with_dockcross dockcross/linux-ppc64le dockcross-linux-ppc64le so runtimes/linux-ppc64le/native

# Windows targets
build_with_dockcross dockcross/windows-armv7 dockcross-win-arm dll runtimes/win-arm/native
build_with_dockcross dockcross/windows-arm64 dockcross-win-arm64 dll runtimes/win-arm64/native
build_with_dockcross dockcross/windows-shared-x64 dockcross-win-x64 dll runtimes/win-x64/native
build_with_dockcross dockcross/windows-shared-x86 dockcross-win-x86 dll runtimes/win-x86/native
fi

# Mac OS X targets
if [[ $(uname) == Darwin ]]; then
    build_macos() {
        local arch="$1"
        local runtime_dir="$2"
        echo "\n==== Building for $runtime_dir ===="
        rm -f CMakeCache.txt
        cmake -DCMAKE_BUILD_TYPE=Release -DAARU_BUILD_PACKAGE=1 -DAARU_MACOS_TARGET_ARCH="$arch" \
              -DCMAKE_SYSTEM_NAME=Darwin -DCMAKE_SYSTEM_PROCESSOR="$arch" .
        make
        mkdir -p "$runtime_dir"
        mv libaaruformat.dylib "$runtime_dir/"
    }
    build_macos x86_64 runtimes/osx-x64/native
    build_macos arm64 runtimes/osx-arm64/native
fi
