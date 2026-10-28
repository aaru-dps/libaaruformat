# Tool executable project
project(aaruformattool C CXX)

# Find required dependencies
find_package(ICU COMPONENTS uc REQUIRED)
find_package(Argtable3 CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

# termbox2-based compare is POSIX-only; on Windows use cli-compare instead
if(NOT WIN32)
    set(AARUFORMATTOOL_COMPARE_SOURCES compare.c termbox2.h)
else()
    set(AARUFORMATTOOL_COMPARE_SOURCES "")
endif()

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
               ${AARUFORMATTOOL_COMPARE_SOURCES}
               cli_compare.c
               convert.c
               upgrade_ddt_to_alpha21.c
               repair_cd_arena.c
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
)

# Set C as the linker language (even though we enable CXX for stdlib)
set_target_properties(aaruformattool PROPERTIES LINKER_LANGUAGE C)

aaru_enable_large_file_support(aaruformattool)

# Set up include directories
target_include_directories(aaruformattool PRIVATE ${ICU_INCLUDE_DIRS})

# Link libraries
target_link_libraries(aaruformattool
                      PRIVATE
                      aaruformat
                      argtable3::argtable3
                      ICU::uc
                      ZLIB::ZLIB
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

# On Windows, copy libaaruformat.dll and MinGW runtime DLLs to the output directory
if(WIN32)
  # Copy libaaruformat.dll
  add_custom_command(TARGET aaruformattool POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:aaruformat>
      $<TARGET_FILE_DIR:aaruformattool>
    COMMENT "Copying libaaruformat.dll"
  )

  # Copy MinGW runtime DLLs (GCC/MinGW builds only)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU")
    get_filename_component(MINGW_BIN_DIR "${CMAKE_C_COMPILER}" DIRECTORY)

    # DLLs to copy — cover both SEH (64-bit) and DWARF (32-bit) libgcc variants
    set(MINGW_RUNTIME_DLLS
      libgcc_s_seh-1.dll
      libgcc_s_dw2-1.dll
      libstdc++-6.dll
      libwinpthread-1.dll
    )

    foreach(MINGW_DLL IN LISTS MINGW_RUNTIME_DLLS)
      if(EXISTS "${MINGW_BIN_DIR}/${MINGW_DLL}")
        add_custom_command(TARGET aaruformattool POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MINGW_BIN_DIR}/${MINGW_DLL}"
            $<TARGET_FILE_DIR:aaruformattool>
          COMMENT "Copying ${MINGW_DLL}"
        )
      endif()
    endforeach()
  endif()
endif()

