cmake_minimum_required(VERSION 3.21)

project(rnet_rollstuhl_emu LANGUAGES CXX)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)
find_package(Threads REQUIRED)

add_executable(rnet_rollstuhl_emu
    src/main.cpp
    resources.qrc
    src/cyclic/CyclicMessageThread.cpp
    src/cyclic/CyclicMessageContainer.cpp
    src/cyclic/Cyclic02000300.cpp
    src/cyclic/Cyclic14300000.cpp
    src/cyclic/Cyclic140C0001.cpp
    src/cyclic/Cyclic0C140000.cpp
    src/cyclic/Cyclic0C140100.cpp
    src/cyclic/Cyclic1C0C0000.cpp
    src/cyclic/Cyclic1C300004.cpp
    src/session/SessionState.cpp
    src/statemachine/ProtocolClassifier.cpp
    src/statemachine/ThreadStateMachine.cpp
    src/statemachine/StateMachineContainer.cpp
    src/statemachine/PowerOnStateMachine.cpp
    src/statemachine/PowerOffStateMachine.cpp
    src/statemachine/ConnectStateMachine.cpp
    src/statemachine/ReadBlockStateMachine.cpp
    src/statemachine/WriteBlockStateMachine.cpp
    src/statemachine/FallbackStateMachine.cpp
)

set_target_properties(rnet_rollstuhl_emu PROPERTIES OUTPUT_NAME "rollstuhl.emu")
target_compile_features(rnet_rollstuhl_emu PRIVATE cxx_std_17)
target_include_directories(rnet_rollstuhl_emu PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(rnet_rollstuhl_emu PRIVATE Qt6::Widgets Threads::Threads)
target_compile_options(rnet_rollstuhl_emu PRIVATE -Wall -Wextra -Wpedantic)


# RNET-CPOP-CLOG-V1
# Instanziiert/loggt CAN-RX/TX an der echten SocketCAN-I/O-Grenze.
target_sources(rnet_rollstuhl_emu PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/RNetCanIOWrap.cpp"
)
target_include_directories(rnet_rollstuhl_emu PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../common"
)
target_link_options(rnet_rollstuhl_emu PRIVATE
    "-Wl,--wrap=write"
    "-Wl,--wrap=read"
    "-Wl,--wrap=send"
    "-Wl,--wrap=recv"
    "-Wl,--wrap=sendto"
    "-Wl,--wrap=recvfrom"
    "-Wl,--wrap=sendmsg"
    "-Wl,--wrap=recvmsg"
    "-Wl,--wrap=writev"
    "-Wl,--wrap=readv"
)
