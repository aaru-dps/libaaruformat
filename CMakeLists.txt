cmake_minimum_required(VERSION 3.13)

# This file is part of the Aaru Data Preservation Suite.
# Copyright (c) 2019-2022 Natalia Portillo.
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
        SET(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures for Mac OS X" FORCE)
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
            src/close.c include/aaruformat/errors.h src/read.c include/aaruformat/crc64.h src/cst.c src/ecc_cd.c src/helpers.c
            src/simd.c include/aaruformat/simd.h src/crc64/crc64.c src/crc64/crc64_clmul.c src/crc64/crc64_vmull.c src/crc64/arm_vmull.c src/crc64/arm_vmull.h src/spamsum.c include/aaruformat/spamsum.h include/aaruformat/flac.h src/flac.c src/lzma.c)

include_directories(include include/aaruformat)

include(3rdparty/flac.cmake)
include(3rdparty/lzma.cmake)

MACRO(TARGET_LINK_LIBRARIES_WHOLE_ARCHIVE target)
    if("${CMAKE_C_COMPILER_ID}" MATCHES "MSVC")
        FOREACH(arg IN LISTS ARGN)
            SET_TARGET_PROPERTIES(
                    ${target} PROPERTIES LINK_FLAGS "/WHOLEARCHIVE:${lib}"
            )
        ENDFOREACH()
    ELSE()
        if("${CMAKE_C_COMPILER_ID}" MATCHES "AppleClang")
            SET(LINK_FLAGS "-Wl,-all_load")
            SET(UNDO_FLAGS "-Wl,-noall_load")
        ELSE()
            SET(LINK_FLAGS "-Wl,--whole-archive")
            SET(UNDO_FLAGS "-Wl,--no-whole-archive")
        ENDIF()
        TARGET_LINK_LIBRARIES(${target} ${LINK_FLAGS} ${ARGN} ${UNDO_FLAGS})
    ENDIF()
ENDMACRO()

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

include_directories(include 3rdparty/uthash/src)

add_subdirectory(tests)