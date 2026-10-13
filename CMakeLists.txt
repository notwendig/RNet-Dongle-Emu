cmake_minimum_required(VERSION 3.21)

project(RNet_DongleInterface VERSION 2.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "ftd2xx.dll is a Windows PE32 target; use preset mingw32-release.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "ftd2xx.dll must be built as 32-bit PE/i386.")
endif()

foreach(req IN ITEMS
    "${CMAKE_SOURCE_DIR}/ftd2xx/ftd2xx_emu.cpp"
    "${CMAKE_SOURCE_DIR}/ftd2xx/ftd2xx_emu.h"
    "${CMAKE_SOURCE_DIR}/ftd2xx/ftd2xx.def"
    "${CMAKE_SOURCE_DIR}/common/DongleMessages.hpp"
    "${CMAKE_SOURCE_DIR}/common/DongleMessages.cpp"
    "${CMAKE_SOURCE_DIR}/common/RNetStream.hpp"
    "${CMAKE_SOURCE_DIR}/examples/ftd2xx-emu.ini.example")
    if(NOT EXISTS "${req}")
        message(FATAL_ERROR "Required source/runtime file missing: ${req}")
    endif()
endforeach()

# IMPORTANT:
# ftd2xx_emu.cpp currently includes ../common/DongleMessages.cpp itself.
# Therefore common/DongleMessages.cpp must NOT also be compiled as a separate
# translation unit, otherwise duplicate symbols result.
add_library(ftd2xx SHARED
    ftd2xx/ftd2xx_emu.cpp
    ftd2xx/ftd2xx_emu.h
    ftd2xx/ftd2xx.def
)

set_target_properties(ftd2xx PROPERTIES
    OUTPUT_NAME "ftd2xx"
    PREFIX ""
)

target_compile_features(ftd2xx PRIVATE cxx_std_17)
target_include_directories(ftd2xx PRIVATE
    "${CMAKE_SOURCE_DIR}/ftd2xx"
    "${CMAKE_SOURCE_DIR}/common"
)

target_compile_definitions(ftd2xx PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

target_compile_options(ftd2xx PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_link_options(ftd2xx PRIVATE
    -static-libgcc
    -static-libstdc++
)

add_custom_command(TARGET ftd2xx POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_SOURCE_DIR}/examples/ftd2xx-emu.ini.example"
            "$<TARGET_FILE_DIR:ftd2xx>/ftd2xx-emu.ini"
    COMMENT "Copying CAN-only FTD2XX runtime INI"
    VERBATIM
)
