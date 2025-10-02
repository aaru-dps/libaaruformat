cmake_minimum_required(VERSION 3.13)

# This file is part of the Aaru Data Preservation Suite.
# Copyright (c) 2019-2025 Natalia Portillo.
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

IF(APPLE)
  IF("${CMAKE_OSX_ARCHITECTURES}" STREQUAL "")
    SET(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Build architectures for Mac OS X" FORCE)
  ENDIF()
ENDIF(APPLE)

project(libaaruformat C)

add_compile_definitions(__STDC_FORMAT_MACROS=1)

if("${CMAKE_C_COMPILER_ID}" MATCHES "MSVC" AND "${CMAKE_C_COMPILER_ARCHITECTURE_ID}" MATCHES "ARMV7")
  set(CMAKE_C_STANDARD 11)
else()
  set(CMAKE_C_STANDARD 99)
endif()

if("${CMAKE_C_PLATFORM_ID}" MATCHES "MinGW")
  if("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "aarch64" OR "${CMAKE_SYSTEM_PROCESSOR}" MATCHES "arm")
    set(WIN32 TRUE)
  endif()

  add_link_options(-static-libgcc)
endif()

message("Detected system processor: ${CMAKE_SYSTEM_PROCESSOR}")
message("Detected vs platform name: ${CMAKE_C_COMPILER_ARCHITECTURE_ID}")
message("Detected compiler: ${CMAKE_C_COMPILER_ID}")
message("Detected build type: ${CMAKE_BUILD_TYPE}")
message("Detected platform: ${CMAKE_C_PLATFORM_ID}")
message("Size of (void*): ${CMAKE_SIZEOF_VOID_P}")

# Check if target is 64-bit
if("${CMAKE_SIZEOF_VOID_P}" MATCHES "8" OR "${CMAKE_C_COMPILER_ARCHITECTURE_ID}" MATCHES "x64" OR "${CMAKE_SYSTEM_PROCESSOR}" MATCHES "x86_64" OR "${CMAKE_SYSTEM_PROCESSOR}" MATCHES "AMD64" OR "${CMAKE_SYSTEM_PROCESSOR}" MATCHES "aarch64")
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

    if(${CMAKE_SYSTEM_PROCESSOR} MATCHES "x86_64" OR ${CMAKE_SYSTEM_PROCESSOR} MATCHES "i686" OR ${CMAKE_SYSTEM_PROCESSOR} MATCHES "AMD64")
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

add_library(aaruformat SHARED include/aaruformat/consts.h include/aaruformat/enums.h include/aaru.h include/aaruformat.h
            include/aaruformat/decls.h include/aaruformat/structs.h src/identify.c src/open.c include/aaruformat/context.h
            src/close.c include/aaruformat/errors.h src/read.c include/aaruformat/crc64.h src/compression/cst.c src/checksum/ecc_cd.c src/helpers.c
            src/checksum/simd.c include/aaruformat/simd.h src/crc64/crc64.c src/crc64/crc64_clmul.c src/crc64/crc64_vmull.c
            src/crc64/arm_vmull.c src/crc64/arm_vmull.h src/checksum/spamsum.c include/aaruformat/spamsum.h include/aaruformat/flac.h
            src/compression/flac.c src/compression/lzma.c src/lru.c include/aaruformat/lru.h include/aaruformat/endian.h src/verify.c
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
            src/md5.c
            include/md5.h)

include_directories(include include/aaruformat 3rdparty/uthash/include 3rdparty/uthash/src)

include(3rdparty/flac.cmake)
include(3rdparty/lzma.cmake)
include(3rdparty/xxhash.cmake)

macro(TARGET_LINK_LIBRARIES_WHOLE_ARCHIVE target)
  if(MSVC)
    foreach(lib IN LISTS ARGN)
      set_target_properties(${target} PROPERTIES LINK_FLAGS "/WHOLEARCHIVE:${lib}")
    endforeach()
  elseif(APPLE)
    foreach(lib IN LISTS ARGN)
      target_link_libraries(${target} "LINKER:--whole-archive" ${lib} "LINKER:--no-whole-archive")
    endforeach()
  elseif(UNIX)
    foreach(lib IN LISTS ARGN)
      target_link_libraries(${target} "LINKER:--whole-archive" ${lib} "LINKER:--no-whole-archive")
    endforeach()
  endif()
endmacro()

if(NOT "${CMAKE_C_PLATFORM_ID}" MATCHES "MinGW" OR (NOT ${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm" AND NOT ${CMAKE_SYSTEM_PROCESSOR} MATCHES "aarch64"))
  set_property(TARGET aaruformat PROPERTY POSITION_INDEPENDENT_CODE TRUE)
else()
  set_property(TARGET aaruformat PROPERTY POSITION_INDEPENDENT_CODE FALSE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake-modules")

# include(FindLibreSSL.cmake)

find_package(OpenSSL QUIET)
find_package(LibreSSL QUIET)

if(OpenSSL_FOUND)
  message("-- OpenSSL VERSION: ${OPENSSL_VERSION}")
endif()

if(LIBRESSL_FOUND)
  message("-- LibreSSL VERSION: ${LIBRESSL_VERSION}")
endif()

if(OpenSSL_FOUND OR LIBRESSL_FOUND)
  add_compile_definitions(AARU_HAS_SHA256)
endif()

if(LIBRESSL_FOUND)
  TARGET_LINK_LIBRARIES_WHOLE_ARCHIVE(aaruformat ${LIBRESSL_CRYPTO_LIBRARY})
elseif(OpenSSL_FOUND)
  TARGET_LINK_LIBRARIES_WHOLE_ARCHIVE(aaruformat ${OPENSSL_CRYPTO_LIBRARY})
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Add slog submodule
if(POLICY CMP0000)
  cmake_policy(VERSION 3.5)
endif()
add_subdirectory(3rdparty/slog)

include_directories(include 3rdparty/uthash/src)

# Include slog headers
include_directories(aaruformat 3rdparty/slog/src)

# Enable TRACE and slog output
add_compile_definitions(aaruformat ENABLE_TRACE USE_SLOG)

include(CheckLibraryExists)

check_library_exists(m log "" HAVE_LIB_M)
if(HAVE_LIB_M)
  TARGET_LINK_LIBRARIES_WHOLE_ARCHIVE(aaruformat m)
endif()

# Link slog
target_link_libraries(aaruformat slog)

add_subdirectory(tests)
add_subdirectory(tool)