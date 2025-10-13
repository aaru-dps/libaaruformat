# Tool executable project
project(aaruformattool C CXX)

# Find required dependencies
find_package(ICU COMPONENTS uc REQUIRED)
find_package(Argtable3 CONFIG REQUIRED)

# Tool executable
add_executable(aaruformattool
    main.c
    version.h
    aaruformattool.h
    identify.c
    info.c
    helpers.c
    read.c
    printhex.c
    verify.c
    ecc_cd.c
    commands.h
    commands.c
    usage.h
    usage.c
    compare.c
    cli_compare.c
    convert.c
    termbox2.h
)

# Set C as the linker language (even though we enable CXX for stdlib)
set_target_properties(aaruformattool PROPERTIES LINKER_LANGUAGE C)

# Set up include directories
target_include_directories(aaruformattool PRIVATE ${ICU_INCLUDE_DIRS})

# Link libraries
target_link_libraries(aaruformattool
    PRIVATE
        aaruformat
        argtable3::argtable3
        ICU::uc
)

# On Linux, enable GNU/POSIX feature test macros so functions like strerror_r
# and cfmakeraw are declared by system headers. We limit this to UNIX and not
# Apple (macOS provides these declarations differently).
if(UNIX AND NOT APPLE)
    target_compile_definitions(aaruformattool PRIVATE
                               _GNU_SOURCE
                               _DEFAULT_SOURCE
                               _XOPEN_SOURCE=700)
endif()


# On macOS/iOS, explicitly link the C++ standard library for ICU dependencies
if(APPLE)
    target_link_libraries(aaruformattool PRIVATE "-lc++")
endif()
