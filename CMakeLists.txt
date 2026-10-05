cmake_minimum_required(VERSION 3.21)

project(RNetDongleEmu
    VERSION 0.1.0
    DESCRIPTION "Offline FTD2XX/R-Net Programmer interoperability emulator and analysis tools"
    LANGUAGES CXX
)

if(NOT WIN32)
    message(FATAL_ERROR
        "The emulator DLL is a Windows target. Use the provided MinGW32 preset/toolchain on Linux."
    )
endif()

add_library(ftd2xx SHARED
    src/ftd2xx_emu.cpp
    src/ftd2xx_emu.h
)

target_compile_features(ftd2xx PRIVATE cxx_std_17)
target_include_directories(ftd2xx PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_definitions(ftd2xx PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

if(MINGW)
    target_compile_options(ftd2xx PRIVATE -Wall -Wextra -Wpedantic)
    # Keep the DLL self-contained for Wine deployment.
    target_link_options(ftd2xx PRIVATE -static-libgcc -static-libstdc++)
endif()

set_target_properties(ftd2xx PROPERTIES
    PREFIX ""
    OUTPUT_NAME "ftd2xx"
    SUFFIX ".dll"
)

install(TARGETS ftd2xx RUNTIME DESTINATION . LIBRARY DESTINATION .)
install(FILES examples/ftd2xx-emu.ini.example DESTINATION . RENAME ftd2xx-emu.ini)
