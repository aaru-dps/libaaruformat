# 3.1 is OK for most parts. However:
# 3.3 is needed in src/libFLAC
# 3.5 is needed in src/libFLAC/ia32
# 3.9 is needed in 'doc' because of doxygen_add_docs()
#cmake_minimum_required(VERSION 3.5)

#if(NOT (CMAKE_BUILD_TYPE OR CMAKE_CONFIGURATION_TYPES OR DEFINED ENV{CFLAGS} OR DEFINED ENV{CXXFLAGS}))
#    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build, options are: None Debug Release RelWithDebInfo")
#endif()

set(FLAC_VERSION 1.5.0) # HOMEPAGE_URL "https://www.xiph.org/flac/")

message(STATUS "FLAC VERSION: ${FLAC_VERSION}")
set(BUILD_SHARED_LIBS BOOL OFF)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/flac/cmake")

set(VERSION ${FLAC_VERSION})

set(OLD_PROJECT_SOURCE_DIR "${PROJECT_SOURCE_DIR}")
set(PROJECT_SOURCE_DIR "${PROJECT_SOURCE_DIR}/3rdparty/flac/")

#find_package(Iconv)
#set(HAVE_ICONV ${Iconv_FOUND})

#if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
#    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -funroll-loops")
#endif()

include(CMakePackageConfigHelpers)
include(CPack)
include(CTest)
include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include(CheckSymbolExists)
include(CheckFunctionExists)
include(CheckIncludeFile)
include(CheckCSourceCompiles)
include(CheckCXXSourceCompiles)
include(UseSystemExtensions)
include(TestBigEndian)

check_include_file("byteswap.h" HAVE_BYTESWAP_H)
check_include_file("inttypes.h" HAVE_INTTYPES_H)
check_include_file("stdint.h" HAVE_STDINT_H)
check_include_file("stdbool.h" HAVE_STDBOOL_H)

if (NOT HAVE_STDINT_H OR NOT HAVE_STDBOOL_H)
  message(SEND_ERROR "Header stdint.h and/or stdbool.h not found")
endif ()

if (MSVC)
  check_include_file("intrin.h" FLAC__HAS_X86INTRIN)
else ()
  check_include_file("x86intrin.h" FLAC__HAS_X86INTRIN)
endif ()

# Windows maps fseeko/ftello itself in share/compat.h. Elsewhere, without HAVE_FSEEKO,
# compat.h #defines fseeko to fseek, which conflicts with off_t-based declarations (macOS).
# All POSIX targets have fseeko (libaaruformat itself relies on it), but a symbol check
# would miss it under strict -std=c99, so set it directly.
if (NOT WIN32)
  set(HAVE_FSEEKO 1)
endif ()

check_c_source_compiles("int main() { return __builtin_bswap16 (0) ; }" HAVE_BSWAP16)
check_c_source_compiles("int main() { return __builtin_bswap32 (0) ; }" HAVE_BSWAP32)

if (NOT "${CMAKE_C_PLATFORM_ID}" MATCHES "MinGW" OR (NOT ${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm" AND NOT ${CMAKE_SYSTEM_PROCESSOR} MATCHES "aarch64"))
  test_big_endian(CPU_IS_BIG_ENDIAN)
endif ()

check_c_compiler_flag(-mstackrealign HAVE_STACKREALIGN_FLAG)

include_directories("3rdparty/flac/include")

include_directories("${CMAKE_CURRENT_BINARY_DIR}/3rdparty/flac")
add_definitions(-DHAVE_CONFIG_H)

if (MSVC)
  add_definitions(
          -D_CRT_SECURE_NO_WARNINGS
          -D_USE_MATH_DEFINES)
endif ()

option(WITH_ASM "Use any assembly optimization routines" ON)

check_include_file("cpuid.h" HAVE_CPUID_H)
check_include_file("sys/param.h" HAVE_SYS_PARAM_H)

set(CMAKE_REQUIRED_LIBRARIES m)
check_function_exists(lround HAVE_LROUND)

include(CheckCSourceCompiles)
include(CheckCPUArch)

include(CheckA64NEON)

check_include_file("arm_neon.h" FLAC__HAS_NEONINTRIN)

set(FLAC_AVX_SOURCES
        3rdparty/flac/src/libFLAC/fixed_intrin_avx2.c
        3rdparty/flac/src/libFLAC/lpc_intrin_avx2.c
        3rdparty/flac/src/libFLAC/lpc_intrin_fma.c
        3rdparty/flac/src/libFLAC/stream_encoder_intrin_avx2.c)

set(FLAC_SIMD_SOURCES
        ${FLAC_AVX_SOURCES}
        3rdparty/flac/src/libFLAC/fixed_intrin_sse2.c
        3rdparty/flac/src/libFLAC/fixed_intrin_ssse3.c
        3rdparty/flac/src/libFLAC/fixed_intrin_sse42.c
        3rdparty/flac/src/libFLAC/lpc_intrin_sse2.c
        3rdparty/flac/src/libFLAC/lpc_intrin_sse41.c
        3rdparty/flac/src/libFLAC/stream_encoder_intrin_sse2.c
        3rdparty/flac/src/libFLAC/stream_encoder_intrin_ssse3.c)

check_cpu_arch_x64(FLAC__CPU_X86_64)
if (NOT FLAC__CPU_X86_64)
  check_cpu_arch_x86(FLAC__CPU_IA32)
endif ()

if (FLAC__CPU_X86_64 OR FLAC__CPU_IA32)
  set(FLAC__ALIGN_MALLOC_DATA 1)
  option(WITH_AVX "Enable AVX, AVX2 optimizations (runtime detected, binary does not require AVX2)" ON)

  # MinGW GCC cannot align the stack to 32 bytes (GCC bug 54412), so spills of __m256 locals with
  # aligned moves would crash. Ask the assembler to emit unaligned moves instead, or drop AVX.
  if (WITH_AVX AND MINGW AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
    check_c_compiler_flag(-Wa,-muse-unaligned-vector-move HAVE_UNALIGNED_VECTOR_MOVE_FLAG)
    if (HAVE_UNALIGNED_VECTOR_MOVE_FLAG)
      set_property(SOURCE ${FLAC_AVX_SOURCES} APPEND PROPERTY COMPILE_OPTIONS -Wa,-muse-unaligned-vector-move)
    else ()
      message(STATUS "FLAC: assembler lacks -muse-unaligned-vector-move, disabling AVX for MinGW GCC")
      set(WITH_AVX OFF CACHE BOOL "Enable AVX, AVX2 optimizations" FORCE)
    endif ()
  endif ()

  if (WITH_AVX AND MSVC)
    set_property(SOURCE ${FLAC_AVX_SOURCES} APPEND PROPERTY COMPILE_OPTIONS /arch:AVX2)
  endif ()

  # 32-bit x86 callers (Win32 ABI, some JITs) may only guarantee 4-byte stack alignment
  if (FLAC__CPU_IA32 AND HAVE_STACKREALIGN_FLAG)
    set_property(SOURCE ${FLAC_SIMD_SOURCES} APPEND PROPERTY COMPILE_OPTIONS -mstackrealign)
  endif ()
else ()
  check_cpu_arch_arm64(FLAC__CPU_ARM64)
  if (FLAC__CPU_ARM64)
    check_a64neon(FLAC__HAS_A64NEONINTRIN)
  endif ()
endif ()

if (NOT WITH_ASM)
  add_definitions(-DFLAC__NO_ASM)
endif ()

if (FLAC__CPU_IA32)
  option(WITH_SSE "Enable SSE2 optimizations" ON)
  check_c_compiler_flag(-msse2 HAVE_MSSE2_FLAG)
  if (WITH_SSE)
    add_compile_options(
            $<$<BOOL:${HAVE_MSSE2_FLAG}>:-msse2>
            $<$<BOOL:${MSVC}>:/arch:SSE2>)
  endif ()
endif ()

include_directories("3rdparty/flac/src/libFLAC/include")

target_sources(aaruformat PRIVATE
        3rdparty/flac/src/libFLAC/bitmath.c
        3rdparty/flac/src/libFLAC/bitreader.c
        3rdparty/flac/src/libFLAC/bitwriter.c
        3rdparty/flac/src/libFLAC/cpu.c
        3rdparty/flac/src/libFLAC/crc.c
        3rdparty/flac/src/libFLAC/fixed.c
        3rdparty/flac/src/libFLAC/fixed_intrin_sse2.c
        3rdparty/flac/src/libFLAC/fixed_intrin_ssse3.c
        3rdparty/flac/src/libFLAC/fixed_intrin_sse42.c
        3rdparty/flac/src/libFLAC/fixed_intrin_avx2.c
        3rdparty/flac/src/libFLAC/float.c
        3rdparty/flac/src/libFLAC/format.c
        3rdparty/flac/src/libFLAC/lpc.c
        3rdparty/flac/src/libFLAC/lpc_intrin_neon.c
        3rdparty/flac/src/libFLAC/lpc_intrin_sse2.c
        3rdparty/flac/src/libFLAC/lpc_intrin_sse41.c
        3rdparty/flac/src/libFLAC/lpc_intrin_avx2.c
        3rdparty/flac/src/libFLAC/lpc_intrin_fma.c
        3rdparty/flac/src/libFLAC/md5.c
        3rdparty/flac/src/libFLAC/memory.c
        3rdparty/flac/src/libFLAC/metadata_iterators.c
        3rdparty/flac/src/libFLAC/metadata_object.c
        3rdparty/flac/src/libFLAC/stream_decoder.c
        3rdparty/flac/src/libFLAC/stream_encoder.c
        3rdparty/flac/src/libFLAC/stream_encoder_intrin_sse2.c
        3rdparty/flac/src/libFLAC/stream_encoder_intrin_ssse3.c
        3rdparty/flac/src/libFLAC/stream_encoder_intrin_avx2.c
        3rdparty/flac/src/libFLAC/stream_encoder_framing.c
        3rdparty/flac/src/libFLAC/window.c
        $<$<BOOL:${WIN32}>:3rdparty/flac/include/share/win_utf8_io.h>
        $<$<BOOL:${WIN32}>:3rdparty/flac/src/share/win_utf8_io/win_utf8_io.c>
        $<$<BOOL:${OGG_FOUND}>:ogg_decoder_aspect.c>
        $<$<BOOL:${OGG_FOUND}>:ogg_encoder_aspect.c>
        $<$<BOOL:${OGG_FOUND}>:ogg_helper.c>
        $<$<BOOL:${OGG_FOUND}>:ogg_mapping.c>)

target_compile_definitions(aaruformat PUBLIC FLAC__NO_DLL)
target_compile_definitions(aaruformat PUBLIC FLAC__NO_FILEIO)

# Disable fortify source when not-release or when cross-building with MingW for WoA
if (CMAKE_BUILD_TYPE STREQUAL Debug OR CMAKE_BUILD_TYPE STREQUAL RelWithDebInfo OR "${CMAKE_C_PLATFORM_ID}" MATCHES "MinGW")
  set(DODEFINE_FORTIFY_SOURCE 0)
endif ()

set_property(TARGET aaruformat PROPERTY C_VISIBILITY_PRESET hidden)

if (ARCHITECTURE_IS_64BIT)
  set(ENABLE_64_BIT_WORDS 1)
endif ()

configure_file(3rdparty/flac/config.cmake.h.in 3rdparty/flac/config.h)

set(PROJECT_SOURCE_DIR "${OLD_PROJECT_SOURCE_DIR}")
