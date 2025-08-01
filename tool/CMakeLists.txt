project(aaruformattool)

find_package(ICU COMPONENTS uc REQUIRED)
find_package(Argtable3 CONFIG REQUIRED)

include_directories(${ICU_INCLUDE_DIRS})

add_executable(aaruformattool main.c version.h aaruformattool.h identify.c info.c helpers.c read.c printhex.c verify.c ecc_cd.c
               commands.h
               commands.c
               usage.h
               usage.c)
target_link_libraries(aaruformattool "aaruformat" argtable3::argtable3)
target_link_libraries(aaruformattool "aaruformat" ICU::uc)
