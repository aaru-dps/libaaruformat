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
    BLES-00905.ird
    ps3_param.sfo
    ps3_test.iso
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
    verify_image.cpp
    identify.cpp
    ps3_aes.cpp
    ps3_crypto.cpp
    ps3_encryption_map.cpp
    ps3_ird.cpp
    ps3_sfo.cpp
    ps3_iso9660.cpp
    ngcw.cpp
    large_file_io.cpp
    mode2_nocrc.cpp
    mode2_errored.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/lib/aes128.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ps3/ps3_crypto.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ps3/ps3_encryption_map.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ngcw/lfg.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ngcw/ngcw_junk.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ngcw/wii_crypto.c
               ../tool/ps3/ird.c
               ../tool/ps3/sfo.c
               ../tool/ps3/iso9660_mini.c
)

aaru_enable_large_file_support(tests_run)

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
find_package(ZLIB REQUIRED)
target_link_libraries(tests_run PRIVATE gtest gtest_main aaruformat ZLIB::ZLIB)

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

# On Windows, copy all runtime DLLs (aaruformat, gtest, ZLIB, etc.) next to the
# test executable so it can be discovered and run without PATH manipulation.
if(WIN32)
  add_custom_command(TARGET tests_run POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:aaruformat>
      $<TARGET_FILE_DIR:tests_run>
    COMMENT "Copying libaaruformat DLL next to tests_run"
  )
  add_custom_command(TARGET tests_run POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:gtest>
      $<TARGET_FILE_DIR:tests_run>
    COMMENT "Copying libgtest DLL next to tests_run"
  )
  add_custom_command(TARGET tests_run POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:gtest_main>
      $<TARGET_FILE_DIR:tests_run>
    COMMENT "Copying libgtest_main DLL next to tests_run"
  )
  # Copy all remaining runtime DLLs (e.g. ZLIB from vcpkg) if CMake >= 3.21
  if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.21")
    add_custom_command(TARGET tests_run POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:tests_run>
        $<TARGET_FILE_DIR:tests_run>
      COMMAND_EXPAND_LISTS
      COMMENT "Copying remaining runtime DLLs next to tests_run"
    )
  endif()
endif()

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

  # Discover tests at ctest-run time (PRE_TEST) to avoid running the exe during the
  # build phase where the ASAN runtime or DLLs may not yet be in PATH.
  gtest_discover_tests(tests_run
    DISCOVERY_TIMEOUT 30
    DISCOVERY_MODE PRE_TEST
  )

  # Clear the environment variable so it doesn't affect the actual test runs
  unset(ENV{ASAN_OPTIONS})
else()
  # PRE_TEST defers discovery to ctest time, avoiding the 0xC0000135 DLL-not-found
  # error that occurs when CMake tries to run tests_run.exe immediately after linking
  # (before the POST_BUILD DLL-copy commands have finished, or when PATH lacks MinGW).
  gtest_discover_tests(tests_run DISCOVERY_MODE PRE_TEST)
endif()

