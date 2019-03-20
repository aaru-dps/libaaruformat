cmake_minimum_required(VERSION 3.13)
project(libdicformat C)

set(CMAKE_C_STANDARD 99)
add_compile_definitions(__STDC_FORMAT_MACROS=1)

add_library(libdicformat SHARED include/dicformat/consts.h include/dicformat/enums.h include/dic.h include/dicformat.h
            include/dicformat/decls.h include/dicformat/structs.h src/identify.c src/open.c include/dicformat/context.h src/close.c include/dicformat/errors.h src/read.c)

include_directories(include include/dicformat)