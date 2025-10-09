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
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/mf2hd_v1.aif DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/mf2hd.aif DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/floptical_v1.aif DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/floptical.aif DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/gigamo_v1.aif DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/gigamo.aif DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/hifd_v1.aif DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/hifd.aif DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/mo540_v1.aif DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/mo540.aif DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)

# Test executable (all unit tests)
add_executable(tests_run crc64.cpp spamsum.cpp crc32.c crc32.h flac.cpp lzma.cpp sha256.cpp md5.cpp sha1.cpp open_image.cpp)

# Link libraries
target_link_libraries(tests_run PRIVATE gtest gtest_main aaruformat)

# Integrate with CTest (per-test reporting)
enable_testing()
include(GoogleTest)
gtest_discover_tests(tests_run)
