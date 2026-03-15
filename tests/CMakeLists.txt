cmake_minimum_required(VERSION 3.13)

# Test suite project
project(tests CXX)

# Temporarily disable deprecation warnings for Google Test
set(_aaru_saved_warn_deprecated __aaru_warn_not_set__)
if(DEFINED CMAKE_WARN_DEPRECATED)
  set(_aaru_saved_warn_deprecated ${CMAKE_WARN_DEPRECATED})
endif()
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)

# Add Google Test subdirectory
add_subdirectory(lib)

# Restore original deprecation warning setting
if(_aaru_saved_warn_deprecated STREQUAL __aaru_warn_not_set__)
  unset(CMAKE_WARN_DEPRECATED CACHE)
  unset(CMAKE_WARN_DEPRECATED)
else()
  set(CMAKE_WARN_DEPRECATED ${_aaru_saved_warn_deprecated} CACHE BOOL "" FORCE)
endif()
unset(_aaru_saved_warn_deprecated)

# Define test data files to copy
set(TEST_DATA_FILES
    random
    flac.flac
    audio.bin
    lzma.bin
    data.bin
    mf2hd_v1.aif
    mf2hd.aif
    floptical_v1.aif
    floptical.aif
    gigamo_v1.aif
    gigamo.aif
    hifd_v1.aif
    hifd.aif
    mo540_v1.aif
    mo540.aif
    mo640_v1.aif
    mo640.aif
    cdmode1_v1.aif
    cdmode1.aif
    cdmode2_v1.aif
    cdmode2.aif
)

# Create data directory in build tree
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/data)

# Copy test data files at build time (not configure time)
# Only copy if source and destination are different (out-of-tree builds)
if(NOT "${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${CMAKE_CURRENT_BINARY_DIR}")
  foreach(data_file ${TEST_DATA_FILES})
    add_custom_command(
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/data/${data_file}
      COMMAND ${CMAKE_COMMAND} -E copy
              ${CMAKE_CURRENT_SOURCE_DIR}/data/${data_file}
              ${CMAKE_CURRENT_BINARY_DIR}/data/${data_file}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/data/${data_file}
      COMMENT "Copying test data file: ${data_file}"
    )
    list(APPEND TEST_DATA_OUTPUTS ${CMAKE_CURRENT_BINARY_DIR}/data/${data_file})
  endforeach()

  # Add custom target to ensure data files are copied
  add_custom_target(copy_test_data ALL DEPENDS ${TEST_DATA_OUTPUTS})
else()
  # For in-tree builds, just create a dummy target
  add_custom_target(copy_test_data ALL
    COMMENT "In-tree build detected - test data files already in place"
  )
endif()

# Test executable (all unit tests)
add_executable(tests_run
    crc64.cpp
    spamsum.cpp
    crc32.c
    crc32.h
    flac.cpp
    lzma.cpp
    sha256.cpp
    md5.cpp
    sha1.cpp
    open_image.cpp
    create_image.cpp
    ps3_aes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ps3/aes128.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ps3/ps3_crypto.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ps3/ps3_encryption_map.c
)

# Set up include directories using modern target-specific approach
target_include_directories(tests_run
  PRIVATE
    ${gtest_SOURCE_DIR}/include
    ${gtest_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/uthash/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/uthash/src
)

# Link libraries
target_link_libraries(tests_run PRIVATE gtest gtest_main aaruformat)

# Apply address sanitizer flags to tests if enabled
if(USE_ASAN)
  if("${CMAKE_C_COMPILER_ID}" MATCHES "MSVC")
    # MSVC address sanitizer flags for tests
    target_compile_options(tests_run PRIVATE /fsanitize=address)
    target_link_options(tests_run PRIVATE /fsanitize=address)
  else()
    # GCC/Clang address sanitizer flags for tests
    target_compile_options(tests_run PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(tests_run PRIVATE -fsanitize=address)
  endif()
  message(STATUS "Address sanitizer enabled for tests")
endif()

# Ensure test data is copied before running tests
add_dependencies(tests_run copy_test_data)

# Integrate with CTest (per-test reporting)
enable_testing()
include(GoogleTest)

# Configure test discovery with proper ASAN_OPTIONS
if(USE_ASAN)
  # On macOS, leak detection is not supported and causes abort during test discovery
  # We need to set ASAN_OPTIONS only for the discovery phase, not for actual test execution
  # The discovery phase runs during CMake configuration
  if(APPLE)
    set(ENV{ASAN_OPTIONS} "print_stats=0")
  else()
    set(ENV{ASAN_OPTIONS} "detect_leaks=0:print_stats=0")
  endif()

  # Discover tests - this runs with the ASAN_OPTIONS we just set
  gtest_discover_tests(tests_run
    DISCOVERY_TIMEOUT 30
  )

  # Clear the environment variable so it doesn't affect the actual test runs
  unset(ENV{ASAN_OPTIONS})
else()
  gtest_discover_tests(tests_run)
endif()

