cmake_minimum_required(VERSION 3.21)
project(rnet_can_proxy LANGUAGES CXX)
add_executable(rnet-can-proxy main.cpp)
target_compile_features(rnet-can-proxy PRIVATE cxx_std_17)
target_compile_options(rnet-can-proxy PRIVATE -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# RNET-BROKER-SUBPROJECT-V35
# RNetMsgBroker is a native CMake subproject, shared with the R-Net Analyzer.
# CAN log comments are produced inside rnet-can-proxy; no filter process.
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Threads REQUIRED)

set(RNETMSGBROKER_DIR "${CMAKE_CURRENT_LIST_DIR}/../../external/RNetMsgBroker")
if(NOT EXISTS "${RNETMSGBROKER_DIR}/CMakeLists.txt" OR
   NOT EXISTS "${RNETMSGBROKER_DIR}/R-Net.json")
    message(FATAL_ERROR "RNetMsgBroker subproject incomplete: ${RNETMSGBROKER_DIR}")
endif()

add_subdirectory(
    "${RNETMSGBROKER_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/external/RNetMsgBroker"
)
if(NOT TARGET RNetMsgBroker)
    message(FATAL_ERROR "RNetMsgBroker CMake target missing")
endif()

target_sources(rnet-can-proxy PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/rnet_proxy_logdecode.cpp"
)
target_include_directories(rnet-can-proxy PRIVATE
    "${RNETMSGBROKER_DIR}"
    "${RNETMSGBROKER_DIR}/src"
)
target_link_libraries(rnet-can-proxy PRIVATE
    RNetMsgBroker
    Qt6::Core
    Threads::Threads
)
target_compile_features(rnet-can-proxy PRIVATE cxx_std_17)

target_compile_definitions(rnet-can-proxy PRIVATE
    RNET_PROXY_LOG_DECODE_V35=1
)

configure_file(
    "${RNETMSGBROKER_DIR}/R-Net.json"
    "${CMAKE_CURRENT_BINARY_DIR}/R-Net.json"
    COPYONLY
)

add_executable(rnet-proxy-decode-test
    "${CMAKE_CURRENT_LIST_DIR}/rnet_proxy_decode_test.cpp"
)
target_include_directories(rnet-proxy-decode-test PRIVATE
    "${RNETMSGBROKER_DIR}"
    "${RNETMSGBROKER_DIR}/src"
)
target_link_libraries(rnet-proxy-decode-test PRIVATE
    RNetMsgBroker
    Qt6::Core
)
target_compile_features(rnet-proxy-decode-test PRIVATE cxx_std_17)
target_compile_options(rnet-proxy-decode-test PRIVATE
    -Wall -Wextra -Wpedantic -Werror
)
# end RNET-BROKER-SUBPROJECT-V35

