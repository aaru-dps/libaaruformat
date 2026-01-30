# Benchmark tool project
project(aarubenchmark C)

# Find required compression libraries
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(ZSTD libzstd)
endif()

# Fallback to find_package if pkg-config fails
if(NOT ZSTD_FOUND)
  find_package(ZSTD QUIET)
  if(ZSTD_FOUND)
    set(ZSTD_LIBRARIES zstd::libzstd_static)
  endif()
endif()

# Download and build bzip3 from git using FetchContent
include(FetchContent)

message(STATUS "Fetching bzip3 from GitHub...")

FetchContent_Declare(
        bzip3
        GIT_REPOSITORY https://github.com/kspalaiologos/bzip3.git
        GIT_TAG master
        GIT_SHALLOW TRUE
        PATCH_COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=<SOURCE_DIR> -P ${CMAKE_CURRENT_SOURCE_DIR}/patch_bzip3.cmake
)

# Configure bzip3 options
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
set(BZIP3_ENABLE_ARCH_NATIVE ON CACHE BOOL "Enable CPU-specific optimizations" FORCE)
set(BZIP3_BUILD_APPS OFF CACHE BOOL "Build bzip3 command-line applications" FORCE)

# Make bzip3 available - this will run their CMakeLists.txt (now patched)
FetchContent_MakeAvailable(bzip3)

# Check if bzip3 target was created
if(TARGET bz3)
  set(BZ3_AVAILABLE TRUE)
  set(BZ3_LIBRARY bz3)
  message(STATUS "Bzip3: Built from source (FetchContent)")
else()
  set(BZ3_AVAILABLE FALSE)
  message(STATUS "Bzip3: Failed to build from source")
endif()

# Download and build Brotli from git using FetchContent
message(STATUS "Fetching Brotli from GitHub...")

FetchContent_Declare(
        brotli
        GIT_REPOSITORY https://github.com/google/brotli.git
        GIT_TAG v1.1.0
        GIT_SHALLOW TRUE
)

# Configure Brotli options - build static libraries only
set(BROTLI_DISABLE_TESTS ON CACHE BOOL "Disable Brotli tests" FORCE)
set(BROTLI_BUNDLED_MODE ON CACHE BOOL "Enable bundled mode" FORCE)

# Make Brotli available
FetchContent_MakeAvailable(brotli)

# Check if Brotli targets were created
if(TARGET brotlienc AND TARGET brotlidec AND TARGET brotlicommon)
  set(BROTLI_AVAILABLE TRUE)
  message(STATUS "Brotli: Built from source (FetchContent)")
else()
  set(BROTLI_AVAILABLE FALSE)
  message(STATUS "Brotli: Failed to build from source")
endif()


# Benchmark executable
add_executable(aarubenchmark
               benchmark.c
               benchmark.h
               image_ops.c
               compression_benchmark.c
               compression.c
               compression.h
)

# Set C as the linker language
set_target_properties(aarubenchmark PROPERTIES LINKER_LANGUAGE C)

# Set up include directories for accessing library headers
target_include_directories(aarubenchmark PRIVATE
                           ${CMAKE_SOURCE_DIR}/include
                           ${CMAKE_SOURCE_DIR}/3rdparty/BLAKE3
                           ${CMAKE_SOURCE_DIR}/3rdparty/lzma-21.03beta/C
                           ${CMAKE_SOURCE_DIR}/3rdparty/xxHash
)

# Add ZSTD include if found
if(ZSTD_FOUND)
  target_include_directories(aarubenchmark PRIVATE ${ZSTD_INCLUDE_DIRS})
  target_compile_definitions(aarubenchmark PRIVATE HAVE_ZSTD=1)
endif()

# Add bzip3 compile definition if available (includes come from bz3 target)
if(BZ3_AVAILABLE)
  target_compile_definitions(aarubenchmark PRIVATE HAVE_BZ3=1)
endif()

# Add Brotli compile definition and includes if available
if(BROTLI_AVAILABLE)
  target_compile_definitions(aarubenchmark PRIVATE HAVE_BROTLI=1)
  # Get include directories from FetchContent
  FetchContent_GetProperties(brotli SOURCE_DIR BROTLI_SOURCE_DIR)
  target_include_directories(aarubenchmark PRIVATE ${BROTLI_SOURCE_DIR}/c/include)
endif()

# Link to the main library and compression libraries
target_link_libraries(aarubenchmark
                      PRIVATE
                      # Link to the aaruformat library
                      aaruformat
)

# Link ZSTD if available
if(ZSTD_FOUND)
  if(ZSTD_LINK_LIBRARIES)
    target_link_libraries(aarubenchmark PRIVATE ${ZSTD_LINK_LIBRARIES})
  elseif(ZSTD_LIBRARIES)
    target_link_libraries(aarubenchmark PRIVATE ${ZSTD_LIBRARIES})
  else()
    # Fallback: try to find the library directly
    find_library(ZSTD_LIB NAMES zstd libzstd PATHS /usr/local/lib /opt/homebrew/lib)
    if(ZSTD_LIB)
      target_link_libraries(aarubenchmark PRIVATE ${ZSTD_LIB})
    endif()
  endif()
endif()

# Link bzip3 if available
if(BZ3_AVAILABLE)
  target_link_libraries(aarubenchmark PRIVATE ${BZ3_LIBRARY})
endif()

# Link Brotli if available
if(BROTLI_AVAILABLE)
  target_link_libraries(aarubenchmark PRIVATE brotlienc brotlidec brotlicommon)
endif()


# On Linux, enable GNU/POSIX feature test macros
if(UNIX AND NOT APPLE)
  target_compile_definitions(aarubenchmark PRIVATE _GNU_SOURCE=1 _POSIX_C_SOURCE=200809L)
endif()

# Set output directory
set_target_properties(aarubenchmark PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

message(STATUS "Configured aarubenchmark tool")
if(ZSTD_FOUND)
  message(STATUS "  - Zstd support: YES")
else()
  message(STATUS "  - Zstd support: NO (will be disabled)")
endif()
if(BZ3_AVAILABLE)
  message(STATUS "  - Bzip3 support: YES (downloaded from git)")
else()
  message(STATUS "  - Bzip3 support: NO (will be disabled)")
endif()
if(BROTLI_AVAILABLE)
  message(STATUS "  - Brotli support: YES (downloaded from git)")
else()
  message(STATUS "  - Brotli support: NO (will be disabled)")
endif()

