# BLAKE3 - cryptographic hash function (https://github.com/BLAKE3-team/BLAKE3)
# Lightweight static library integration (pure C + optional assembly) for libaaruformat.

# Option to use hand-written x86-64 assembly when available
option(ENABLE_BLAKE3_ASM "Use hand-written x86-64 assembly for BLAKE3" ON)

set(BLAKE3_C_DIRECTORY "3rdparty/BLAKE3/c")

# Prevent redefinition if included multiple times
if(TARGET blake3)
  return()
endif()

message(STATUS "BLAKE3: Building static library (custom minimal integration)")

add_library(blake3 STATIC)

# Core required sources (portable implementation + dispatcher)
set(BLAKE3_CORE_SOURCES
    ${BLAKE3_C_DIRECTORY}/blake3.c
    ${BLAKE3_C_DIRECTORY}/blake3_dispatch.c
    ${BLAKE3_C_DIRECTORY}/blake3_portable.c
)

target_sources(blake3 PRIVATE ${BLAKE3_CORE_SOURCES})

# Architecture-specific optimizations / assembly
if(CMAKE_C_COMPILER_ID MATCHES "MSVC")
    # MSVC path: decide between assembly or portable-only (no intrinsics C files if assembly enabled)
    if(ENABLE_BLAKE3_ASM AND ((DEFINED CMAKE_C_COMPILER_ARCHITECTURE_ID AND CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "[Xx]64") OR CMAKE_SYSTEM_PROCESSOR MATCHES "[Xx]64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64"))
        message(STATUS "BLAKE3: Using MSVC x86-64 MASM assembly implementation")
        enable_language(ASM_MASM)
        set(BLAKE3_MASM_SOURCES
            ${BLAKE3_C_DIRECTORY}/blake3_avx2_x86-64_windows_msvc.asm
            ${BLAKE3_C_DIRECTORY}/blake3_avx512_x86-64_windows_msvc.asm
            ${BLAKE3_C_DIRECTORY}/blake3_sse2_x86-64_windows_msvc.asm
            ${BLAKE3_C_DIRECTORY}/blake3_sse41_x86-64_windows_msvc.asm)
        set_source_files_properties(${BLAKE3_MASM_SOURCES} PROPERTIES LANGUAGE ASM_MASM)
        target_sources(blake3 PRIVATE ${BLAKE3_MASM_SOURCES})
    else()
        message(STATUS "BLAKE3: Using portable-only (MSVC minimal) variant")
    endif()
else()
    # Non-MSVC compilers (Clang/GCC/AppleClang)
    if(ENABLE_BLAKE3_ASM AND (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64") AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        # Choose correct assembly source variants
        if(WIN32)
            message(STATUS "BLAKE3: Using x86-64 GNU (Windows) assembly implementation")
            set(_BLAKE3_ASM_SOURCES
                ${BLAKE3_C_DIRECTORY}/blake3_avx2_x86-64_windows_gnu.S
                ${BLAKE3_C_DIRECTORY}/blake3_avx512_x86-64_windows_gnu.S
                ${BLAKE3_C_DIRECTORY}/blake3_sse2_x86-64_windows_gnu.S
                ${BLAKE3_C_DIRECTORY}/blake3_sse41_x86-64_windows_gnu.S)
        else()
            message(STATUS "BLAKE3: Using x86-64 UNIX assembly implementation")
            set(_BLAKE3_ASM_SOURCES
                ${BLAKE3_C_DIRECTORY}/blake3_avx2_x86-64_unix.S
                ${BLAKE3_C_DIRECTORY}/blake3_avx512_x86-64_unix.S
                ${BLAKE3_C_DIRECTORY}/blake3_sse2_x86-64_unix.S
                ${BLAKE3_C_DIRECTORY}/blake3_sse41_x86-64_unix.S)
        endif()
        # Enable generic ASM language (GNU/Clang handle preprocessed .S automatically, but be explicit)
        enable_language(ASM)
        target_sources(blake3 PRIVATE ${_BLAKE3_ASM_SOURCES})
        unset(_BLAKE3_ASM_SOURCES)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "i386" OR CMAKE_SYSTEM_PROCESSOR MATCHES "i686")
        # Fallback to intrinsics C sources (no assembly or disabled)
        message(STATUS "BLAKE3: Enabling x86/x64 intrinsics (SSE2,SSE4.1,AVX2,AVX512*)")
        target_sources(blake3 PRIVATE
            ${BLAKE3_C_DIRECTORY}/blake3_sse2.c
            ${BLAKE3_C_DIRECTORY}/blake3_sse41.c
            ${BLAKE3_C_DIRECTORY}/blake3_avx2.c)
        set_source_files_properties(${BLAKE3_C_DIRECTORY}/blake3_sse2.c   PROPERTIES COMPILE_OPTIONS "-msse2")
        set_source_files_properties(${BLAKE3_C_DIRECTORY}/blake3_sse41.c  PROPERTIES COMPILE_OPTIONS "-msse4.1")
        set_source_files_properties(${BLAKE3_C_DIRECTORY}/blake3_avx2.c   PROPERTIES COMPILE_OPTIONS "-mavx2")
        if(NOT CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
          target_sources(blake3 PRIVATE ${BLAKE3_C_DIRECTORY}/blake3_avx512.c)
          set_source_files_properties(${BLAKE3_C_DIRECTORY}/blake3_avx512.c PROPERTIES COMPILE_OPTIONS "-mavx512f;-mavx512vl")
        endif()
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "arm64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64")
        message(STATUS "BLAKE3: Enabling NEON for AArch64")
        target_sources(blake3 PRIVATE ${BLAKE3_C_DIRECTORY}/blake3_neon.c)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm" OR CMAKE_SYSTEM_PROCESSOR MATCHES "ARM")
        # Test whether the target already supports NEON (via CMAKE_C_FLAGS or
        # compiler defaults).  We must NOT add -mfpu=neon ourselves: GCC uses
        # last-wins for -mfpu, so appending it would silently override a user's
        # -mfpu=vfpv3-d16 and produce a binary that crashes on non-NEON hardware.
        include(CheckCSourceCompiles)
        check_c_source_compiles("
            #include <arm_neon.h>
            int main(void) { uint32x4_t v = vdupq_n_u32(0); return vgetq_lane_u32(v, 0); }
        " BLAKE3_NEON_COMPILES)
        if(BLAKE3_NEON_COMPILES)
            message(STATUS "BLAKE3: Enabling NEON on 32-bit ARM")
            target_sources(blake3 PRIVATE ${BLAKE3_C_DIRECTORY}/blake3_neon.c)
            target_compile_definitions(blake3 PRIVATE BLAKE3_USE_NEON=1)
        else()
            message(STATUS "BLAKE3: 32-bit ARM without NEON -> portable only")
        endif()
    else()
        message(STATUS "BLAKE3: Unknown arch -> portable only")
    endif()
endif()

# Public include for blake3.h
target_include_directories(blake3 PUBLIC ${BLAKE3_C_DIRECTORY})

# PIC for inclusion into shared library
set_property(TARGET blake3 PROPERTY POSITION_INDEPENDENT_CODE TRUE)

# Optimize in Release
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        target_compile_options(blake3 PRIVATE /O2)
    else()
        target_compile_options(blake3 PRIVATE -O3 -ffast-math)
    endif()
endif()

# Do NOT link aaruformat here; top-level handles linkage.
