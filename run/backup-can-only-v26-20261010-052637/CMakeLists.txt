cmake_minimum_required(VERSION 3.21)

project(ftd2xx_rnet_emu VERSION 0.24.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "ftd2xx.dll is a Windows PE32 target; use the mingw32 preset.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "ftd2xx.dll must be built as 32-bit PE/i386.")
endif()

set(FTD2XX_EMU_INI "${CMAKE_SOURCE_DIR}/ftd2xx-emu.ini")
if(NOT EXISTS "${FTD2XX_EMU_INI}")
    message(FATAL_ERROR "Missing runtime INI: ${FTD2XX_EMU_INI}")
endif()

foreach(req IN ITEMS
        "${CMAKE_SOURCE_DIR}/src/ftd2xx_emu.cpp"
        "${CMAKE_SOURCE_DIR}/src/ftd2xx_emu.h"
        "${CMAKE_SOURCE_DIR}/src/ftd2xx.def")
    if(NOT EXISTS "${req}")
        message(FATAL_ERROR "Required DLL source file is missing: ${req}")
    endif()
endforeach()

add_library(ftd2xx SHARED
    src/ftd2xx_emu.cpp
    src/ftd2xx_emu.h
    src/ftd2xx.def
)

set_target_properties(ftd2xx PROPERTIES
    OUTPUT_NAME "ftd2xx"
    PREFIX ""
)

target_compile_features(ftd2xx PRIVATE cxx_std_17)
target_compile_definitions(ftd2xx PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    target_compile_options(ftd2xx PRIVATE /W4 /EHsc)
else()
    target_compile_options(ftd2xx PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(ftd2xx PRIVATE -static-libgcc -static-libstdc++)
endif()

# CAN-only runtime: no DLL-local chair replay is installed or copied.
add_custom_command(TARGET ftd2xx POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${FTD2XX_EMU_INI}"
            "$<TARGET_FILE_DIR:ftd2xx>/ftd2xx-emu.ini"
    COMMENT "Copying CAN-only FTD2XX runtime INI"
    VERBATIM
)
