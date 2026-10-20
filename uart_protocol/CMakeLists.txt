# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13...3.31)

# The uploaded prototype targets the RP2040 Pico W only.
set(PICO_BOARD pico_w CACHE STRING "Target board")
if(NOT PICO_BOARD STREQUAL "pico_w")
    message(FATAL_ERROR "Stage 1 must be built with PICO_BOARD=pico_w")
endif()
include(pico_sdk_import.cmake)
project(uart_protocol C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
pico_sdk_init()

# Keep the original default (mode 4) and accept existing VS Code caches.
set(PROTOCOL_TEST "4" CACHE STRING "Failure-test number 0..4 (original aliases 4)")
set_property(CACHE PROTOCOL_TEST PROPERTY STRINGS original 0 1 2 3 4)
if(PROTOCOL_TEST STREQUAL "original")
    set(PROTOCOL_TEST_MODE 4)
elseif(PROTOCOL_TEST MATCHES "^[0-4]$")
    set(PROTOCOL_TEST_MODE "${PROTOCOL_TEST}")
else()
    message(FATAL_ERROR "PROTOCOL_TEST must be original or 0, 1, 2, 3, 4")
endif()

add_executable(uart_protocol
    src/main.c
    src/protocol/crc16.c
    src/protocol/frame.c
    src/protocol/stream_parser.c
    src/protocol/reliability.c
    src/pico1/pico1_protocol.c
    src/pico2/pico2_protocol.c
    src/demo/failure_injection.c
    src/demo/mock_logger.c
    src/demo/demo.c
)
target_include_directories(uart_protocol PRIVATE src)
target_compile_definitions(uart_protocol PRIVATE PROTOCOL_TEST=${PROTOCOL_TEST_MODE})
# Useful warnings without making SDK diagnostics fatal.
target_compile_options(uart_protocol PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra>)
target_link_libraries(uart_protocol pico_stdlib hardware_uart)
# The prototype drives BOTH UARTs itself. Do not add a stdio driver
# that could inject output or claim a protocol UART. debug_line() is unchanged.
pico_enable_stdio_uart(uart_protocol 0)
pico_enable_stdio_usb(uart_protocol 0)
pico_add_extra_outputs(uart_protocol)
message(STATUS "Protocol test: ${PROTOCOL_TEST}")

# Standalone physical Maker Pi Pico test. Build explicitly so normal protocol
# builds and regression runs still build only uart_protocol by default.
add_executable(button_led_test EXCLUDE_FROM_ALL tests/hardware/button_led_test.c)
target_link_libraries(button_led_test pico_stdlib)
target_compile_options(button_led_test PRIVATE -Wall -Wextra)
pico_enable_stdio_uart(button_led_test 0)
pico_enable_stdio_usb(button_led_test 1)
pico_add_extra_outputs(button_led_test)

# Opt-in UI/CONFIG/retrieval integration firmware; does not replace either existing target.
add_executable(pico2_ui_protocol_test EXCLUDE_FROM_ALL
    src/demo/mock_logger.c
    tests/hardware/pico2_ui_protocol_test.c
    src/pico2/pico2_ui.c
    src/pico2/pico2_protocol.c
    src/pico1/pico1_protocol.c
    src/protocol/crc16.c
    src/protocol/frame.c
    src/protocol/stream_parser.c
    src/protocol/reliability.c
)
target_include_directories(pico2_ui_protocol_test PRIVATE src)
target_link_libraries(pico2_ui_protocol_test pico_stdlib hardware_uart)
target_compile_options(pico2_ui_protocol_test PRIVATE -Wall -Wextra)
pico_enable_stdio_uart(pico2_ui_protocol_test 0)
pico_enable_stdio_usb(pico2_ui_protocol_test 1)
pico_add_extra_outputs(pico2_ui_protocol_test)

# Independent real-board images. Existing default/regression targets stay intact.
add_executable(wifi_pico_firmware EXCLUDE_FROM_ALL
    src/apps/wifi_pico_main.c
    src/pico2/pico2_protocol.c
    src/pico2/pico2_ui.c
    src/protocol/crc16.c
    src/protocol/frame.c
    src/protocol/stream_parser.c
    src/protocol/reliability.c
)
add_executable(sensor_pico_protocol_test EXCLUDE_FROM_ALL
    src/apps/sensor_pico_test_main.c
    src/pico1/pico1_protocol.c
    src/demo/mock_logger.c
    src/protocol/crc16.c
    src/protocol/frame.c
    src/protocol/stream_parser.c
    src/protocol/reliability.c
)
foreach(split_target wifi_pico_firmware sensor_pico_protocol_test)
    target_include_directories(${split_target} PRIVATE src)
    target_link_libraries(${split_target} pico_stdlib hardware_uart)
    target_compile_options(${split_target} PRIVATE -Wall -Wextra)
    pico_enable_stdio_uart(${split_target} 0)
    pico_enable_stdio_usb(${split_target} 1)
    pico_add_extra_outputs(${split_target})
endforeach()

# Opt-in live-sensor codec milestone; all verified targets above stay unchanged.
set(CODEC_SAMPLE_INTERVAL "3600" CACHE STRING "Codec loopback CONFIG interval in seconds")
option(CODEC_STARTUP_DIAGNOSTICS "Wait for USB and print codec hardware startup checkpoints" OFF)
option(CODEC_RX_TIMEOUT_TEST "Suppress DATA/END during the first codec retrieval to test the RX watchdog" OFF)
if(NOT CODEC_SAMPLE_INTERVAL MATCHES "^[1-9][0-9]*$" OR CODEC_SAMPLE_INTERVAL GREATER 86400)
    message(FATAL_ERROR "CODEC_SAMPLE_INTERVAL must be 1..86400 seconds")
endif()
add_executable(sensor_codec_loopback_test EXCLUDE_FROM_ALL
    src/apps/sensor_codec_loopback_main.c
    src/codec/temp_encoder.c
    src/codec/temp_decoder.c
    src/codec/temp_log_print.c
    src/pico1/pico1_protocol.c
    src/pico2/pico2_protocol.c
    src/pico2/pico2_ui.c
    src/protocol/crc16.c
    src/protocol/frame.c
    src/protocol/stream_parser.c
    src/protocol/reliability.c
)
target_include_directories(sensor_codec_loopback_test PRIVATE src)
target_compile_definitions(sensor_codec_loopback_test PRIVATE CODEC_SAMPLE_INTERVAL=${CODEC_SAMPLE_INTERVAL})
target_compile_definitions(sensor_codec_loopback_test PRIVATE
    CODEC_STARTUP_DIAGNOSTICS=$<BOOL:${CODEC_STARTUP_DIAGNOSTICS}>
    CODEC_RX_TIMEOUT_TEST=$<BOOL:${CODEC_RX_TIMEOUT_TEST}>)
target_compile_options(sensor_codec_loopback_test PRIVATE -Wall -Wextra)
target_link_libraries(sensor_codec_loopback_test pico_stdlib hardware_uart hardware_i2c)
pico_enable_stdio_usb(sensor_codec_loopback_test 1)
pico_enable_stdio_uart(sensor_codec_loopback_test 0)
pico_add_extra_outputs(sensor_codec_loopback_test)
