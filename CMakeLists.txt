cmake_minimum_required(VERSION 3.16)
project(micronet VERSION 0.1.0 LANGUAGES C)

include(FetchContent)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

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

function(micronet_fetch_content name repository tag)
    FetchContent_Declare(
        ${name}
        GIT_REPOSITORY ${repository}
        GIT_TAG ${tag}
    )
    FetchContent_GetProperties(${name})
    if(NOT ${name}_POPULATED)
        FetchContent_Populate(${name})
    endif()
endfunction()

micronet_fetch_content(microcrypt https://github.com/Vanderhell/microcrypt.git d3734d16bc43bcc8ee9045626b7d2f67c54c7291)
micronet_fetch_content(microdh https://github.com/Vanderhell/microdh.git 4eae701295875a7245ce76406cf9fedc3f63edca)
micronet_fetch_content(microtest https://github.com/Vanderhell/microtest.git 0dc22958afbffc3d3cb18623273f69d0ce960489)

add_library(microcrypt STATIC
    ${microcrypt_SOURCE_DIR}/src/mcrypt.c
)
target_include_directories(microcrypt PUBLIC
    ${microcrypt_SOURCE_DIR}/include
)

add_library(microdh STATIC
    ${microdh_SOURCE_DIR}/src/mdh.c
)
target_include_directories(microdh PUBLIC
    ${microdh_SOURCE_DIR}/include
)

add_library(microtest INTERFACE)
target_include_directories(microtest INTERFACE
    ${microtest_SOURCE_DIR}/include
)

# Library
set(MICRONET_HAL_SOURCES)
if(ESP_PLATFORM)
    list(APPEND MICRONET_HAL_SOURCES hal/p2p_hal_esp32.c)
else()
    list(APPEND MICRONET_HAL_SOURCES hal/p2p_hal_linux.c)
endif()

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
    ${MICRONET_HAL_SOURCES}
)

target_include_directories(micronet PUBLIC include src)
target_link_libraries(micronet PUBLIC
    microcrypt
    microdh
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
