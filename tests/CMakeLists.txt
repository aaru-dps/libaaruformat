cmake_minimum_required(VERSION 3.13)

# 'Google_test' is the subproject name
project(tests)

# 'lib' is the folder with Google Test sources
add_subdirectory(lib)
include_directories(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR} ../include ../3rdparty/uthash/include ../3rdparty/uthash/src)

# Find OpenSSL for tests that use it directly
find_package(OpenSSL QUIET)
find_package(LibreSSL QUIET)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/random
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/flac.flac
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/audio.bin
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/lzma.bin
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/data.bin
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)

# 'Google_Tests_run' is the target name
# 'test1.cpp tests2.cpp' are source files with tests
add_executable(tests_run crc64.cpp spamsum.cpp crc32.c crc32.h flac.cpp lzma.cpp sha256.cpp md5.cpp)

# Link libraries including OpenSSL for SHA256 test
target_link_libraries(tests_run PRIVATE gtest gtest_main aaruformat)

# Link OpenSSL/LibreSSL for tests that use crypto functions directly
if(LIBRESSL_FOUND)
  target_link_libraries(tests_run PRIVATE ${LIBRESSL_CRYPTO_LIBRARY})
elseif(OpenSSL_FOUND)
    target_link_libraries(tests_run ${OPENSSL_CRYPTO_LIBRARY})
  target_link_libraries(tests_run PRIVATE ${OPENSSL_CRYPTO_LIBRARY})
endif()
