cmake_minimum_required(VERSION 3.13)
project(libaaruformat C)

set(CMAKE_C_STANDARD 90)
add_compile_definitions(__STDC_FORMAT_MACROS=1)

add_library(aaruformat SHARED include/aaruformat/consts.h include/aaruformat/enums.h include/aaru.h include/aaruformat.h
            include/aaruformat/decls.h include/aaruformat/structs.h src/identify.c src/open.c include/aaruformat/context.h
            src/close.c include/aaruformat/errors.h src/read.c src/crc64.c src/cst.c src/ecc_cd.c src/helpers.c)

include_directories(include include/aaruformat)