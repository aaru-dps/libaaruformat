# Tool executable project
project(aaruformattool C CXX)

# Find required dependencies
find_package(ICU COMPONENTS uc REQUIRED)
find_package(Argtable3 CONFIG REQUIRED)
find_package(Curses REQUIRED)
find_package(ZLIB REQUIRED)

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
               upgrade_ddt_to_alpha21.c
               inject_media_tag.c
               ps3/ird.c
               ps3/ird.h
               ps3/sfo.c
               ps3/sfo.h
               ps3/iso9660_mini.c
               ps3/iso9660_mini.h
               ps3/convert.c
               wiiu/convert.c
               wiiu/reader.c
               wiiu/reader.h
               ngcw/convert.c
               ${CMAKE_CURRENT_SOURCE_DIR}/../src/lib/aes128.c
               ${CMAKE_CURRENT_SOURCE_DIR}/../src/ps3/ps3_crypto.c
               ${CMAKE_CURRENT_SOURCE_DIR}/../src/ps3/ps3_encryption_map.c
               ${CMAKE_CURRENT_SOURCE_DIR}/../src/wiiu/wiiu_crypto.c
               ${CMAKE_CURRENT_SOURCE_DIR}/../src/ngcw/lfg.c
               ${CMAKE_CURRENT_SOURCE_DIR}/../src/ngcw/ngcw_junk.c
               ${CMAKE_CURRENT_SOURCE_DIR}/../src/ngcw/wii_crypto.c
               termbox2.h
)

# Set C as the linker language (even though we enable CXX for stdlib)
set_target_properties(aaruformattool PROPERTIES LINKER_LANGUAGE C)

# Set up include directories
target_include_directories(aaruformattool PRIVATE ${ICU_INCLUDE_DIRS} ${CURSES_INCLUDE_DIRS})

# Link libraries
target_link_libraries(aaruformattool
                      PRIVATE
                      aaruformat
                      argtable3::argtable3
                      ICU::uc
                      ZLIB::ZLIB
                      ${CURSES_LIBRARIES}
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
