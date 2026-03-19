cmake_minimum_required(VERSION 3.13)

# This file is part of the Aaru Data Preservation Suite.
# Copyright (c) 2019-2026 Natalia Portillo.
#
# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as
# published by the Free Software Foundation; either version 2.1 of the
# License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, see <http://www.gnu.org/licenses/>.

if(APPLE)
  # Too early cmake has not yet set it
  if((NOT DEFINED CMAKE_SYSTEM_PROCESSOR) AND (NOT DEFINED AARU_MACOS_TARGET_ARCH))
    execute_process(COMMAND uname -m OUTPUT_VARIABLE AARU_MACOS_TARGET_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
  endif()

  if(NOT DEFINED AARU_MACOS_TARGET_ARCH)
    if(${CMAKE_SYSTEM_PROCESSOR} MATCHES "x86_64")
      set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "Build architectures for macOS" FORCE)
    elseif(${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm64")
      set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Build architectures for macOS" FORCE)
    else()
      message(FATAL_ERROR "Unknown system processor ${CMAKE_SYSTEM_PROCESSOR} for macOS")
    endif()
  elseif(AARU_MACOS_TARGET_ARCH STREQUAL "x86_64")
    set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "Build architectures for macOS" FORCE)
  elseif(AARU_MACOS_TARGET_ARCH STREQUAL "arm64")
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Build architectures for macOS" FORCE)
  else()
    message(FATAL_ERROR "Unknown Aaru target architecture ${AARU_MACOS_TARGET_ARCH} for macOS")
  endif()

  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum OS X deployment version")
endif(APPLE)

# Integrate vcpkg toolchain if available but not explicitly provided.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE
        "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
    message(STATUS "vcpkg toolchain configured: $ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
  endif()
elseif(DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
  message(STATUS "vcpkg toolchain already set: ${CMAKE_TOOLCHAIN_FILE}")
endif()

project(libaaruformat C)

# Option to enable slog logging (disabled by default)
option(USE_SLOG "Enable slog logging" OFF)

# Option to enable address sanitizer (disabled by default)
option(USE_ASAN "Enable address sanitizer" OFF)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Enable position independent code by default
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# MSVC ARM specific override
if("${CMAKE_C_COMPILER_ID}" MATCHES "MSVC" AND "${CMAKE_C_COMPILER_ARCHITECTURE_ID}" MATCHES "ARMV7")
  set(CMAKE_C_STANDARD 11)
endif()

# Global compile definitions
add_compile_definitions(__STDC_FORMAT_MACROS=1)

# POSIX functions (strdup, fseeko, ftello, etc.) require feature test macros
# when building with C_EXTENSIONS OFF. Define on non-Windows platforms.
if(NOT WIN32)
  add_compile_definitions(_POSIX_C_SOURCE=200809L)
endif()

# MinGW specific configuration
if("${CMAKE_C_PLATFORM_ID}" MATCHES "MinGW")
  if("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "aarch64" OR "${CMAKE_SYSTEM_PROCESSOR}" MATCHES "arm")
    set(WIN32 TRUE)
  endif()
  add_link_options(-static-libgcc)
endif()

message(STATUS "Detected system processor: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "Detected vs platform name: ${CMAKE_C_COMPILER_ARCHITECTURE_ID}")
message(STATUS "Detected compiler: ${CMAKE_C_COMPILER_ID}")
message(STATUS "Detected build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Detected platform: ${CMAKE_C_PLATFORM_ID}")
message(STATUS "Size of (void*): ${CMAKE_SIZEOF_VOID_P}")

# Check if target is 64-bit
if("${CMAKE_SIZEOF_VOID_P}" MATCHES "8"
   OR "${CMAKE_C_COMPILER_ARCHITECTURE_ID}" MATCHES "x64"
   OR "${CMAKE_SYSTEM_PROCESSOR}" MATCHES "x86_64"
   OR "${CMAKE_SYSTEM_PROCESSOR}" MATCHES "AMD64"
   OR "${CMAKE_SYSTEM_PROCESSOR}" MATCHES "aarch64"
   OR "${CMAKE_SYSTEM_PROCESSOR}" MATCHES "riscv64")
  set(ARCHITECTURE_IS_64BIT TRUE)
endif()

if("${CMAKE_BUILD_TYPE}" MATCHES "Release")
  add_compile_definitions(NDEBUG)

  if("${CMAKE_C_COMPILER_ID}" MATCHES "MSVC")
    add_compile_options("/O2" "/fp:fast")
    if(${CMAKE_C_COMPILER_ARCHITECTURE_ID} MATCHES "X86" OR ${CMAKE_C_COMPILER_ARCHITECTURE_ID} MATCHES "x64")
      add_compile_options("/arch:SSE2")
    endif()
  else()
    add_compile_options(-ffast-math -O3)

    # Architecture-specific flags are only set for native builds.
    # Cross-compilation toolchain files provide their own -march/-mtune/-mfpu.
    if(NOT CMAKE_CROSSCOMPILING)
      if(${CMAKE_SYSTEM_PROCESSOR} MATCHES "x86_64"
         OR ${CMAKE_SYSTEM_PROCESSOR} MATCHES "i686"
         OR ${CMAKE_SYSTEM_PROCESSOR} MATCHES "AMD64")
        if(NOT "${CMAKE_C_COMPILER_ID}" MATCHES "AppleClang")
          add_compile_options(-march=core2 -mtune=westmere -mfpmath=sse)
        endif()

        add_compile_options(-msse3)

        if(NOT "${CMAKE_C_PLATFORM_ID}" MATCHES "MinGW")
          add_compile_options(-flto)
        endif()
      elseif(${CMAKE_SYSTEM_PROCESSOR} MATCHES "aarch64")
        if(NOT "${CMAKE_C_COMPILER_ID}" MATCHES "AppleClang")
          add_compile_options(-march=armv8-a)
        endif()

        if(NOT "${CMAKE_C_PLATFORM_ID}" MATCHES "MinGW")
          add_compile_options(-flto)
        endif()
      elseif(${CMAKE_SYSTEM_PROCESSOR} MATCHES "armv7l" OR ${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm")
        if(NOT "${CMAKE_C_COMPILER_ID}" MATCHES "AppleClang")
          add_compile_options(-march=armv7+fp -mfpu=vfpv3-d16)
        endif()
      elseif(${CMAKE_SYSTEM_PROCESSOR} MATCHES "mips")
        if(NOT "${CMAKE_C_PLATFORM_ID}" MATCHES "MinGW")
          add_compile_options(-flto)
        endif()
      endif()
    endif()
  endif()
endif()

# Address sanitizer configuration
if(USE_ASAN)
  message(STATUS "Address sanitizer enabled")

  if("${CMAKE_C_COMPILER_ID}" MATCHES "MSVC")
    # MSVC address sanitizer flags
    add_compile_options(/fsanitize=address)
    add_link_options(/fsanitize=address)
  else()
    # GCC/Clang address sanitizer flags
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
  endif()
else()
  message(STATUS "Address sanitizer disabled (enable with -DUSE_ASAN=ON)")
endif()

# Library type controlled by BUILD_SHARED_LIBS (CMake standard).
# -DBUILD_SHARED_LIBS=ON  → shared library (default, backwards compatible)
# -DBUILD_SHARED_LIBS=OFF → static library
if(NOT DEFINED BUILD_SHARED_LIBS)
  set(BUILD_SHARED_LIBS ON)
endif()

if(BUILD_SHARED_LIBS)
  message(STATUS "Building shared library (use -DBUILD_SHARED_LIBS=OFF for static)")
else()
  message(STATUS "Building static library")
endif()

add_library(aaruformat
            include/aaruformat/consts.h
            include/aaruformat/enums.h
            include/aaru.h
            include/aaruformat.h
            include/aaruformat/decls.h
            include/aaruformat/structs.h
            src/identify.c
            src/open.c
            include/aaruformat/context.h
            src/close.c
            include/aaruformat/errors.h
            src/read.c
            include/aaruformat/crc64.h
            src/compression/cst.c
            src/checksum/ecc_cd.c
            src/helpers.c
            src/checksum/simd.c
            include/aaruformat/simd.h
            src/crc64/crc64.c
            src/crc64/crc64_clmul.c
            src/crc64/crc64_vmull.c
            src/crc64/arm_vmull.c
            src/crc64/arm_vmull.h
            src/checksum/spamsum.c
            include/aaruformat/spamsum.h
            include/aaruformat/flac.h
            src/compression/flac.c
            src/compression/lzma.c
            src/lru.c
            include/aaruformat/lru.h
            include/aaruformat/endian.h
            src/verify.c
            include/aaruformat/structs/header.h
            include/aaruformat/structs/ddt.h
            include/aaruformat/structs/index.h
            include/aaruformat/structs/data.h
            include/aaruformat/structs/metadata.h
            include/aaruformat/structs/dump.h
            include/aaruformat/structs/checksum.h
            include/aaruformat/structs/optical.h
            src/index/index_v1.c
            include/internal.h
            src/index/index_v2.c
            src/blocks/data.c
            src/ddt/ddt_v1.c
            src/blocks/metadata.c
            src/blocks/optical.c
            src/blocks/dump.c
            src/blocks/checksum.c
            src/index/index_v3.c
            src/ddt/ddt_v2.c
            include/aaruformat/structs/options.h
            src/options.c
            src/create.c
            src/time.c
            src/write.c
            include/log.h
            src/ddt/hash_map.c
            include/aaruformat/hash_map.h
            src/ddt/static_lru_hash_map.c
            include/aaruformat/static_lru_hash_map.h
            src/checksum/md5.c
            include/md5.h
            src/checksum/sha1.c
            include/sha1.h
            src/checksum/sha256.c
            include/sha256.h
            src/lisa_tag.c
            include/aaruformat/structs/lisa_tag.h
            src/metadata.c
            src/dump.c
            include/aaruformat/structs/tape.h
            src/blocks/tape.c
            src/blocks/flux.c
            src/lib/aes128.c
            src/lib/aes128.h
            src/ps3/ps3_crypto.c
            src/ps3/ps3_crypto.h
            src/ps3/ps3_encryption_map.c
            src/ps3/ps3_encryption_map.h
            src/wiiu/wiiu_crypto.c
            src/wiiu/wiiu_crypto.h
            src/ngcw/lfg.c
            src/ngcw/lfg.h
            src/ngcw/ngcw_junk.c
            src/ngcw/ngcw_junk.h
            src/ngcw/wii_crypto.c
            src/ngcw/wii_crypto.h)

# Set up include directories for the target
target_include_directories(aaruformat
                           PUBLIC
                           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/aaruformat>
                           $<INSTALL_INTERFACE:include>
                           ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/uthash/include
                           ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/uthash/src
)

# Load third-party dependencies
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake-modules")

include(3rdparty/flac.cmake)
include(3rdparty/lzma.cmake)
include(3rdparty/xxhash.cmake)
include(3rdparty/blake3.cmake)

if(TARGET blake3)
  target_link_libraries(aaruformat blake3)
endif()

macro(TARGET_LINK_LIBRARIES_WHOLE_ARCHIVE target)
  if(MSVC)
    foreach(lib IN LISTS ARGN)
      set_target_properties(${target} PROPERTIES LINK_FLAGS "/WHOLEARCHIVE:${lib}")
    endforeach()
  elseif(APPLE)
    foreach(lib IN LISTS ARGN)
      target_link_libraries(${target} -Wl,-force_load ${lib})
    endforeach()
  elseif(UNIX)
    foreach(lib IN LISTS ARGN)
      target_link_libraries(${target} -Wl,--whole-archive ${lib} -Wl,--no-whole-archive)
    endforeach()
  endif()
endmacro()

# MinGW/ARM specific: disable PIC
if(NOT "${CMAKE_C_PLATFORM_ID}" MATCHES "MinGW"
   OR (NOT ${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm"
   AND NOT ${CMAKE_SYSTEM_PROCESSOR} MATCHES "aarch64"))
  set_property(TARGET aaruformat PROPERTY POSITION_INDEPENDENT_CODE TRUE)
else()
  set_property(TARGET aaruformat PROPERTY POSITION_INDEPENDENT_CODE FALSE)
endif()

# Add slog submodule
if(POLICY CMP0000)
  cmake_policy(VERSION 3.5)
endif()

# Conditionally enable slog logging
if(USE_SLOG)
  add_subdirectory(3rdparty/slog)

  # Include slog headers
  target_include_directories(aaruformat PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/slog/src)

  # Enable TRACE and slog output
  target_compile_definitions(aaruformat PRIVATE ENABLE_TRACE ENABLE_FATAL USE_SLOG)

  # Link slog
  target_link_libraries(aaruformat slog)

  message(STATUS "slog logging enabled")
else()
  message(STATUS "slog logging disabled (enable with -DUSE_SLOG=ON)")
endif()

# Check for math library
include(CheckLibraryExists)

check_library_exists(m cos "" HAVE_LIB_M)
if(HAVE_LIB_M)
  target_link_libraries(aaruformat m)
endif()

# Find Doxygen for documentation generation
find_package(Doxygen)

if(DOXYGEN_FOUND)
  # Set Doxygen configuration variables
  set(DOXYGEN_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/docs")
  set(PROJECT_VERSION "1.0")

  # Configure the Doxyfile
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in
                 ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)

  # Add custom target to generate documentation
  add_custom_target(doxygen
                    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
                    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                    COMMENT "Generating API documentation with Doxygen"
                    VERBATIM
  )

  message(STATUS "Found Doxygen: ${DOXYGEN_EXECUTABLE}")
  message(STATUS "Added 'doxygen' target to generate API documentation")
  message(STATUS "Documentation will be generated in: ${DOXYGEN_OUTPUT_DIR}/html")
else()
  message(STATUS "Doxygen not found - 'doxygen' target will not be available")
  message(STATUS "Install it to generate API documentation")
endif()

if(NOT AARU_BUILD_PACKAGE)
  add_subdirectory(tests)
  add_subdirectory(tool)
  add_subdirectory(docs/spec)
endif()
