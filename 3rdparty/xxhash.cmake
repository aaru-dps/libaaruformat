# xxHash - Extremely Fast Hash algorithm
# Copyright (C) 2012-2023 Yann Collet
# BSD 2-Clause License

set("XXHASH_C_DIRECTORY" "3rdparty/xxHash")

message(STATUS "xxHash: Building static library")

# Create static library target for xxHash
add_library(xxhash STATIC)

# Add the main xxhash source file
target_sources(xxhash PRIVATE ${XXHASH_C_DIRECTORY}/xxhash.c)

# Set include directories for xxhash
target_include_directories(xxhash PUBLIC ${XXHASH_C_DIRECTORY})

# Enable x86/x64 dispatch for better performance on supported platforms
if(${CMAKE_SYSTEM_PROCESSOR} MATCHES "x86_64" OR
   ${CMAKE_SYSTEM_PROCESSOR} MATCHES "AMD64" OR
   ${CMAKE_SYSTEM_PROCESSOR} MATCHES "i386" OR
   ${CMAKE_SYSTEM_PROCESSOR} MATCHES "i686")
    message(STATUS "xxHash: Enabling x86 dispatch optimization")
    target_sources(xxhash PRIVATE ${XXHASH_C_DIRECTORY}/xxh_x86dispatch.c)
    target_compile_definitions(xxhash PRIVATE XXHSUM_DISPATCH=1)
endif()

# Set compile definitions for xxhash
target_compile_definitions(xxhash PRIVATE XXH_STATIC_LINKING_ONLY)

# Optimization flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        target_compile_options(xxhash PRIVATE /O2)
    else()
        target_compile_options(xxhash PRIVATE -O3 -ffast-math)

        # Enable specific optimizations for native x86/x64 builds.
        # Cross-compilation toolchain files provide their own -march/-mtune.
        if(NOT CMAKE_CROSSCOMPILING)
            if(${CMAKE_SYSTEM_PROCESSOR} MATCHES "x86_64" OR
               ${CMAKE_SYSTEM_PROCESSOR} MATCHES "AMD64" OR
               ${CMAKE_SYSTEM_PROCESSOR} MATCHES "i386" OR
               ${CMAKE_SYSTEM_PROCESSOR} MATCHES "i686")
                if(NOT "${CMAKE_C_COMPILER_ID}" MATCHES "AppleClang")
                    target_compile_options(xxhash PRIVATE -march=core2 -mtune=westmere)
                endif()
            endif()
        endif()
    endif()
endif()

# Set position independent code for shared library compatibility
set_property(TARGET xxhash PROPERTY POSITION_INDEPENDENT_CODE TRUE)

# Link xxhash to the main aaruformat target
target_link_libraries(aaruformat xxhash)

# Add include directory to main target so it can find xxhash headers
target_include_directories(aaruformat PRIVATE ${XXHASH_C_DIRECTORY})
