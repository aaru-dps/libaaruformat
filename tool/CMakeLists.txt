project(aaruformattool)

find_package(ICU COMPONENTS uc REQUIRED)

include_directories(${ICU_INCLUDE_DIRS})

add_executable(aaruformattool main.c main.h aaruformattool.h identify.c info.c)
target_link_libraries(aaruformattool "aaruformat" ICU::uc)
