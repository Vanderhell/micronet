cmake_minimum_required(VERSION 3.16)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/MicronetSources.cmake)

if(ESP_PLATFORM)
    idf_component_register(
        SRCS
            ${MICRONET_CORE_SOURCES}
            arduino/micronet/src/mcrypt.c
            arduino/micronet/src/mdh.c
            hal/p2p_hal_esp32.c
        INCLUDE_DIRS
            include
            src
            arduino/micronet/src
        REQUIRES
            lwip
            esp_timer
            nvs_flash
            mbedtls
            esp_wifi
            esp_netif
    )
else()
    project(micronet VERSION 0.1.0 LANGUAGES C)

    include(FetchContent)
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()

    set(CMAKE_C_STANDARD 99)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_C_EXTENSIONS OFF)

    option(MICRONET_WARNINGS_AS_ERRORS "Treat Micronet warnings as errors" OFF)

    function(micronet_enable_warnings target_name)
        if(MSVC)
            target_compile_options(${target_name} PRIVATE /W4)
            if(MICRONET_WARNINGS_AS_ERRORS)
                target_compile_options(${target_name} PRIVATE /WX)
            endif()
        else()
            target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
            if(MICRONET_WARNINGS_AS_ERRORS)
                target_compile_options(${target_name} PRIVATE -Werror)
            endif()
        endif()
    endfunction()

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

    # Offline-friendly vendored deps (Arduino sources)
    set(MICRONET_VENDORED_DEPS_DIR ${CMAKE_CURRENT_LIST_DIR}/arduino/micronet)

    add_library(microcrypt STATIC
        ${MICRONET_VENDORED_DEPS_DIR}/src/mcrypt.c
    )
    target_include_directories(microcrypt PUBLIC
        ${MICRONET_VENDORED_DEPS_DIR}/src
    )

    add_library(microdh STATIC
        ${MICRONET_VENDORED_DEPS_DIR}/src/mdh.c
    )
    target_include_directories(microdh PUBLIC
        ${MICRONET_VENDORED_DEPS_DIR}/src
    )

    # microtest is vendored locally under tests/mtest.h (header-only)
    add_library(microtest INTERFACE)
    target_include_directories(microtest INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/tests
    )

    add_library(micronet STATIC
        ${MICRONET_CORE_SOURCES}
        hal/p2p_hal_linux.c
    )
    micronet_enable_warnings(micronet)

    target_include_directories(micronet PUBLIC include src)
    target_link_libraries(micronet PUBLIC
        microcrypt
        microdh
    )

    if(WIN32)
        target_link_libraries(micronet PUBLIC ws2_32)

        add_library(micronet_bridge SHARED
            tools/native/micronet_bridge.c
        )
        target_include_directories(micronet_bridge PUBLIC
            include
            src
            tools/native
        )
        target_link_libraries(micronet_bridge PRIVATE micronet)
        micronet_enable_warnings(micronet_bridge)
    endif()

    option(P2P_UART_DEBUG "Enable UART debug shell" OFF)
    if(P2P_UART_DEBUG AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/p2p_uart.c" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/debug/p2p_uart_cmds.c")
        target_sources(micronet PRIVATE
            src/debug/p2p_uart.c
            src/debug/p2p_uart_cmds.c
        )
        target_compile_definitions(micronet PRIVATE P2P_UART_DEBUG=1)
    endif()

    option(P2P_BUILD_TESTS "Build tests" ON)
    if(P2P_BUILD_TESTS
       AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_runner.c"
       AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_transport.c"
       AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_security.c"
       AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_network.c"
       AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_data.c"
        AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_protocol.c"
       AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_api.c"
       AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_arduino_parity.c")
        enable_testing()
        add_executable(micronet_tests
            tests/test_runner.c
            tests/test_transport.c
            tests/test_security.c
            tests/test_network.c
            tests/test_data.c
            tests/test_protocol.c
            tests/test_api.c
            tests/test_arduino_parity.c
        )
        target_link_libraries(micronet_tests micronet microtest)
        micronet_enable_warnings(micronet_tests)
        add_test(NAME micronet_tests_transport COMMAND micronet_tests transport)
        add_test(NAME micronet_tests_security COMMAND micronet_tests security)
        add_test(NAME micronet_tests_network COMMAND micronet_tests network)
        add_test(NAME micronet_tests_data COMMAND micronet_tests data)
        add_test(NAME micronet_tests_protocol COMMAND micronet_tests protocol)
        add_test(NAME micronet_tests_api COMMAND micronet_tests api)
        add_test(NAME micronet_tests_arduino_parity COMMAND micronet_tests arduino_parity)
    endif()

    option(P2P_BUILD_EXAMPLES "Build examples" OFF)
    if(P2P_BUILD_EXAMPLES AND UNIX)
        add_subdirectory(examples/linux_chat)
    endif()
endif()
