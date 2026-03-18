# Zstandard compression library (https://github.com/facebook/zstd)
# BSD/GPLv2 dual license

set(ZSTD_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/zstd/lib")

if(NOT EXISTS "${ZSTD_LIB_DIR}/zstd.h")
  message(FATAL_ERROR "Zstandard submodule not found. Run: git submodule update --init 3rdparty/zstd")
endif()

message(STATUS "Zstandard: Building static library")

add_library(zstd_static STATIC
    ${ZSTD_LIB_DIR}/common/debug.c
    ${ZSTD_LIB_DIR}/common/entropy_common.c
    ${ZSTD_LIB_DIR}/common/error_private.c
    ${ZSTD_LIB_DIR}/common/fse_decompress.c
    ${ZSTD_LIB_DIR}/common/pool.c
    ${ZSTD_LIB_DIR}/common/threading.c
    ${ZSTD_LIB_DIR}/common/xxhash.c
    ${ZSTD_LIB_DIR}/common/zstd_common.c
    ${ZSTD_LIB_DIR}/compress/fse_compress.c
    ${ZSTD_LIB_DIR}/compress/hist.c
    ${ZSTD_LIB_DIR}/compress/huf_compress.c
    ${ZSTD_LIB_DIR}/compress/zstd_compress.c
    ${ZSTD_LIB_DIR}/compress/zstd_compress_literals.c
    ${ZSTD_LIB_DIR}/compress/zstd_compress_sequences.c
    ${ZSTD_LIB_DIR}/compress/zstd_compress_superblock.c
    ${ZSTD_LIB_DIR}/compress/zstd_double_fast.c
    ${ZSTD_LIB_DIR}/compress/zstd_fast.c
    ${ZSTD_LIB_DIR}/compress/zstd_lazy.c
    ${ZSTD_LIB_DIR}/compress/zstd_ldm.c
    ${ZSTD_LIB_DIR}/compress/zstd_opt.c
    ${ZSTD_LIB_DIR}/compress/zstd_preSplit.c
    ${ZSTD_LIB_DIR}/compress/zstdmt_compress.c
    ${ZSTD_LIB_DIR}/decompress/huf_decompress.c
    ${ZSTD_LIB_DIR}/decompress/zstd_ddict.c
    ${ZSTD_LIB_DIR}/decompress/zstd_decompress.c
    ${ZSTD_LIB_DIR}/decompress/zstd_decompress_block.c
)

# x86_64 assembly fast path for Huffman decompression
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  enable_language(ASM)
  target_sources(zstd_static PRIVATE ${ZSTD_LIB_DIR}/decompress/huf_decompress_amd64.S)
endif()

target_include_directories(zstd_static PUBLIC ${ZSTD_LIB_DIR})
set_property(TARGET zstd_static PROPERTY POSITION_INDEPENDENT_CODE TRUE)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  if(MSVC)
    target_compile_options(zstd_static PRIVATE /O2)
  else()
    target_compile_options(zstd_static PRIVATE -O3)
  endif()
endif()

# Link to aaruformat
target_link_libraries(aaruformat zstd_static)
target_include_directories(aaruformat PRIVATE ${ZSTD_LIB_DIR})
