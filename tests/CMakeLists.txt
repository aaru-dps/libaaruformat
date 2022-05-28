# 'Google_test' is the subproject name
project(tests)

# 'lib' is the folder with Google Test sources
add_subdirectory(lib)
include_directories(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/random
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/flac.flac
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/audio.bin
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data/)

# 'Google_Tests_run' is the target name
# 'test1.cpp tests2.cpp' are source files with tests
add_executable(tests_run crc64.cpp spamsum.cpp crc32.c crc32.h flac.cpp)
target_link_libraries(tests_run gtest gtest_main "aaruformat")
