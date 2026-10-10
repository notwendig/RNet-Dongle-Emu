cmake_minimum_required(VERSION 3.20)

project(rollstuhl_emu LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_add_executable(rollstuhl_emu
    src/main.cpp
)

target_link_libraries(rollstuhl_emu
    PRIVATE
        Qt6::Widgets
)

target_compile_options(rollstuhl_emu
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

set_target_properties(rollstuhl_emu PROPERTIES
    OUTPUT_NAME "rollstuhl.emu"
)
