cmake_minimum_required(VERSION 3.13)

# 'Google_test' is the subproject name
project(tests)

# 'lib' is the folder with Google Test sources
add_subdirectory(lib)
include_directories(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR} ../include ../3rdparty/uthash/include ../3rdparty/uthash/src)

# Copy test data files into build tree
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/random DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/flac.flac DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/audio.bin DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/lzma.bin DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/data.bin DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)

# Test executable (all unit tests)
add_executable(tests_run crc64.cpp spamsum.cpp crc32.c crc32.h flac.cpp lzma.cpp sha256.cpp md5.cpp sha1.cpp)

# Link libraries
target_link_libraries(tests_run PRIVATE gtest gtest_main aaruformat)
