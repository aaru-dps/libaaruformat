project(aaruformattool)

add_executable(aaruformattool main.c main.h aaruformattool.h identify.c)
target_link_libraries(aaruformattool "aaruformat")
