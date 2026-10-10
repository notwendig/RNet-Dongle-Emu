cmake_minimum_required(VERSION 3.21)
project(rnet_rollstuhl_emu LANGUAGES CXX)
find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)
add_executable(rnet_rollstuhl_emu
    src/main.cpp
    resources.qrc
)
set_target_properties(rnet_rollstuhl_emu PROPERTIES OUTPUT_NAME "rollstuhl.emu")
target_compile_features(rnet_rollstuhl_emu PRIVATE cxx_std_17)
target_link_libraries(rnet_rollstuhl_emu PRIVATE Qt6::Widgets)
target_compile_options(rnet_rollstuhl_emu PRIVATE -Wall -Wextra -Wpedantic)
