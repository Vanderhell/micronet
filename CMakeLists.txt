cmake_minimum_required(VERSION 3.16)
project(micronet VERSION 0.1.0 LANGUAGES C)

include(FetchContent)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compilation flags
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
    )
endif()

FetchContent_Declare(
    microcrypt
    GIT_REPOSITORY https://github.com/Vanderhell/microcrypt.git
    GIT_TAG master
)
FetchContent_Declare(
    microdb
    GIT_REPOSITORY https://github.com/Vanderhell/microdb.git
    GIT_TAG master
)
FetchContent_Declare(
    microbus
    GIT_REPOSITORY https://github.com/Vanderhell/microbus.git
    GIT_TAG master
)
FetchContent_Declare(
    microtimer
    GIT_REPOSITORY https://github.com/Vanderhell/microtimer.git
    GIT_TAG master
)
FetchContent_Declare(
    microfsm
    GIT_REPOSITORY https://github.com/Vanderhell/microfsm.git
    GIT_TAG master
)
FetchContent_Declare(
    iotspool
    GIT_REPOSITORY https://github.com/Vanderhell/iotspool.git
    GIT_TAG master
)
FetchContent_Declare(
    microhealth
    GIT_REPOSITORY https://github.com/Vanderhell/microhealth.git
    GIT_TAG master
)
FetchContent_Declare(
    microcodec
    GIT_REPOSITORY https://github.com/Vanderhell/microcodec.git
    GIT_TAG main
)
FetchContent_Declare(
    microdh
    GIT_REPOSITORY https://github.com/Vanderhell/microdh.git
    GIT_TAG master
)
FetchContent_Declare(
    microcbor
    GIT_REPOSITORY https://github.com/Vanderhell/microcbor.git
    GIT_TAG master
)
FetchContent_Declare(
    microlog
    GIT_REPOSITORY https://github.com/Vanderhell/microlog.git
    GIT_TAG master
)
FetchContent_Declare(
    microres
    GIT_REPOSITORY https://github.com/Vanderhell/microres.git
    GIT_TAG master
)
FetchContent_Declare(
    microtest
    GIT_REPOSITORY https://github.com/Vanderhell/microtest.git
    GIT_TAG master
)
FetchContent_GetProperties(microcrypt)
if(NOT microcrypt_POPULATED)
    FetchContent_Populate(microcrypt)
    add_library(microcrypt STATIC
        ${microcrypt_SOURCE_DIR}/src/mcrypt.c
    )
    target_include_directories(microcrypt PUBLIC
        ${microcrypt_SOURCE_DIR}/include
    )
endif()

FetchContent_GetProperties(microdb)
if(NOT microdb_POPULATED)
    FetchContent_Populate(microdb)
    add_library(microdb STATIC
        ${microdb_SOURCE_DIR}/src/microdb.c
    )
    target_include_directories(microdb PUBLIC
        ${microdb_SOURCE_DIR}/include
    )
endif()

FetchContent_GetProperties(microbus)
if(NOT microbus_POPULATED)
    FetchContent_Populate(microbus)
    add_library(microbus INTERFACE)
    target_include_directories(microbus INTERFACE
        ${microbus_SOURCE_DIR}/include
    )
endif()

FetchContent_GetProperties(microtimer)
if(NOT microtimer_POPULATED)
    FetchContent_Populate(microtimer)
    add_library(microtimer INTERFACE)
    target_include_directories(microtimer INTERFACE
        ${microtimer_SOURCE_DIR}/include
    )
endif()

FetchContent_GetProperties(microfsm)
if(NOT microfsm_POPULATED)
    FetchContent_Populate(microfsm)
    add_library(microfsm INTERFACE)
    target_include_directories(microfsm INTERFACE
        ${microfsm_SOURCE_DIR}/include
    )
endif()

FetchContent_GetProperties(iotspool)
if(NOT iotspool_POPULATED)
    FetchContent_Populate(iotspool)
    add_library(iotspool INTERFACE)
    target_include_directories(iotspool INTERFACE
        ${iotspool_SOURCE_DIR}/include
    )
endif()

FetchContent_GetProperties(microhealth)
if(NOT microhealth_POPULATED)
    FetchContent_Populate(microhealth)
    add_library(microhealth INTERFACE)
    target_include_directories(microhealth INTERFACE
        ${microhealth_SOURCE_DIR}/include
    )
endif()

FetchContent_GetProperties(microcodec)
if(NOT microcodec_POPULATED)
    FetchContent_Populate(microcodec)
    add_library(microcodec INTERFACE)
    target_include_directories(microcodec INTERFACE
        ${microcodec_SOURCE_DIR}/include
    )
endif()

FetchContent_GetProperties(microdh)
if(NOT microdh_POPULATED)
    FetchContent_Populate(microdh)
    add_library(microdh STATIC
        ${microdh_SOURCE_DIR}/src/mdh.c
    )
    target_include_directories(microdh PUBLIC
        ${microdh_SOURCE_DIR}/include
    )
endif()

FetchContent_GetProperties(microcbor)
if(NOT microcbor_POPULATED)
    FetchContent_Populate(microcbor)
    add_library(microcbor INTERFACE)
    target_include_directories(microcbor INTERFACE
        ${microcbor_SOURCE_DIR}/include
    )
endif()

FetchContent_GetProperties(microlog)
if(NOT microlog_POPULATED)
    FetchContent_Populate(microlog)
    add_library(microlog INTERFACE)
    target_include_directories(microlog INTERFACE
        ${microlog_SOURCE_DIR}/include
    )
endif()

FetchContent_GetProperties(microres)
if(NOT microres_POPULATED)
    FetchContent_Populate(microres)
    add_library(microres INTERFACE)
    target_include_directories(microres INTERFACE
        ${microres_SOURCE_DIR}/include
    )
endif()

FetchContent_GetProperties(microtest)
if(NOT microtest_POPULATED)
    FetchContent_Populate(microtest)
    add_library(microtest INTERFACE)
    target_include_directories(microtest INTERFACE
        ${microtest_SOURCE_DIR}/include
    )
endif()

# Library
add_library(micronet STATIC
    src/micronet.c
    src/transport/p2p_transport.c
    src/transport/p2p_transport_stun.c
    src/security/p2p_security.c
    src/security/p2p_security_handshake.c
    src/security/p2p_security_keys.c
    src/network/p2p_network.c
    src/network/p2p_network_gossip.c
    src/network/p2p_network_group.c
    src/network/p2p_network_sync.c
    src/data/p2p_data.c
    src/data/p2p_data_vars.c
    src/data/p2p_data_query.c
    src/data/p2p_data_metrics.c
    src/protocol/p2p_protocol.c
    src/protocol/p2p_protocol_dispatch.c
    src/protocol/p2p_protocol_serialize.c
    hal/p2p_hal_linux.c
)

target_include_directories(micronet PUBLIC include src)
target_link_libraries(micronet PUBLIC
    microcrypt
    microdb
    microbus
    microtimer
    microfsm
    iotspool
    microhealth
    microcodec
    microdh
    microcbor
    microlog
    microres
)

if(WIN32)
    target_link_libraries(micronet PUBLIC ws2_32)
endif()

# UART debug (optional)
option(P2P_UART_DEBUG "Enable UART debug shell" OFF)
if(P2P_UART_DEBUG AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/p2p_uart.c" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/p2p_uart_cmds.c")
    target_sources(micronet PRIVATE
        src/debug/p2p_uart.c
        src/debug/p2p_uart_cmds.c
    )
    target_compile_definitions(micronet PRIVATE P2P_UART_DEBUG=1)
endif()

# Tests
option(P2P_BUILD_TESTS "Build tests" ON)
if(P2P_BUILD_TESTS
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_runner.c"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_transport.c"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_security.c"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_network.c"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_data.c"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_protocol.c"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_api.c")
    enable_testing()
    add_executable(micronet_tests
        tests/test_runner.c
        tests/test_transport.c
        tests/test_security.c
        tests/test_network.c
        tests/test_data.c
        tests/test_protocol.c
        tests/test_api.c
    )
    target_link_libraries(micronet_tests micronet microtest)
    add_test(NAME micronet_tests COMMAND micronet_tests)
endif()

# Examples
option(P2P_BUILD_EXAMPLES "Build examples" OFF)
if(P2P_BUILD_EXAMPLES)
    add_subdirectory(examples/linux_chat)
endif()
