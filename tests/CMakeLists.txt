cmake_minimum_required(VERSION 3.13)

# 'Google_test' is the subproject name
project(tests)

# 'lib' is the folder with Google Test sources
set(_aaru_saved_warn_deprecated __aaru_warn_not_set__)
if(DEFINED CMAKE_WARN_DEPRECATED)
  set(_aaru_saved_warn_deprecated ${CMAKE_WARN_DEPRECATED})
endif()
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)

add_subdirectory(lib)

if(_aaru_saved_warn_deprecated STREQUAL __aaru_warn_not_set__)
  unset(CMAKE_WARN_DEPRECATED CACHE)
  unset(CMAKE_WARN_DEPRECATED)
else()
  set(CMAKE_WARN_DEPRECATED ${_aaru_saved_warn_deprecated} CACHE BOOL "" FORCE)
endif()
unset(_aaru_saved_warn_deprecated)
include_directories(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR} ../include ../3rdparty/uthash/include ../3rdparty/uthash/src)

# Copy test data files into build tree
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/random DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/flac.flac DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/audio.bin DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/lzma.bin DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/data.bin DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)

# Download test file from aaru.app if not already present
set(TEST_FILE_URL "https://www.claunia.com/tests_aaru/mf2hd_v1.aif")
set(TEST_FILE_PATH "${CMAKE_CURRENT_BINARY_DIR}/data/mf2hd_v1.aif")

if(NOT EXISTS "${TEST_FILE_PATH}")
  message(STATUS "Downloading test file from ${TEST_FILE_URL}")
  file(DOWNLOAD "${TEST_FILE_URL}" "${TEST_FILE_PATH}"
       SHOW_PROGRESS
       STATUS download_status
       TIMEOUT 60)
  list(GET download_status 0 status_code)
  list(GET download_status 1 status_string)

  if(NOT status_code EQUAL 0)
    message(WARNING "Failed to download ${TEST_FILE_URL}: ${status_string}")
  else()
    message(STATUS "Successfully downloaded test file to ${TEST_FILE_PATH}")
  endif()
else()
  message(STATUS "Test file already exists: ${TEST_FILE_PATH}")
endif()

# Test executable (all unit tests)
add_executable(tests_run crc64.cpp spamsum.cpp crc32.c crc32.h flac.cpp lzma.cpp sha256.cpp md5.cpp sha1.cpp open_image.cpp)

# Link libraries
target_link_libraries(tests_run PRIVATE gtest gtest_main aaruformat)

# Integrate with CTest (per-test reporting)
enable_testing()
include(GoogleTest)
gtest_discover_tests(tests_run)
