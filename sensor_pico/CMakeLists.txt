cmake_minimum_required(VERSION 3.13)

# Pico W board (2 MB flash). The Wi-Fi chip is never initialised: no cyw43 library is linked.
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Copy pico_sdk_import.cmake from <pico-sdk>/external/ into this folder first.
include(pico_sdk_import.cmake)

project(sensor_pico C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/lib/uart_protocol" shared_uart)

add_executable(sensor_pico
    main.c
    aht10.c
    encoder.c
    flash_log.c
    sensor_uart.c
)

target_link_libraries(sensor_pico
    pico_stdlib
    hardware_i2c
    hardware_flash
    hardware_uart
    hardware_irq
    hardware_sync
    uart::sensor
)

# SDK 2.x only: flash_safe_execute lives in pico_flash. On SDK 1.x flash_log.c disables interrupts instead.
if(PICO_SDK_VERSION_STRING VERSION_GREATER_EQUAL "2.0.0")
    target_link_libraries(sensor_pico pico_flash)
endif()

# Serial over USB. The default UART stdio uses GP0/GP1, which the AHT10's I2C needs.
pico_enable_stdio_usb(sensor_pico 1)
pico_enable_stdio_uart(sensor_pico 0)
# A missing/stalled Serial Monitor must not delay UART ACKs by 500 ms.
target_compile_definitions(sensor_pico PRIVATE PICO_STDIO_USB_STDOUT_TIMEOUT_US=2000)

# cmake -DERASE_LOG_ON_BOOT=ON ..   erases the log region at every boot (use once, on a fresh board)
option(ERASE_LOG_ON_BOOT "Erase the log region at boot" OFF)
if(ERASE_LOG_ON_BOOT)
    target_compile_definitions(sensor_pico PRIVATE ERASE_LOG_ON_BOOT=1)
endif()

target_compile_options(sensor_pico PRIVATE -Wall -Wextra)

pico_add_extra_outputs(sensor_pico)

# Build both batching variants from the same logger and protocol sources.
get_target_property(sensor_sources sensor_pico SOURCES)
get_target_property(sensor_links sensor_pico LINK_LIBRARIES)
function(add_batched_sensor target)
    add_executable(${target} ${sensor_sources} ${ARGN})
    target_link_libraries(${target} PRIVATE ${sensor_links} uart::sensor_batch)
    target_compile_definitions(${target} PRIVATE SENSOR_BATCHED=1
        ERASE_LOG_ON_BOOT=0 PICO_STDIO_USB_STDOUT_TIMEOUT_US=2000)
    target_compile_options(${target} PRIVATE -Wall -Wextra)
    pico_enable_stdio_usb(${target} 1)
    pico_enable_stdio_uart(${target} 0)
    pico_add_extra_outputs(${target})
endfunction()

add_batched_sensor(sensor_pico_batched)
add_batched_sensor(sensor_pico_batched_sleep low_power.c)
target_compile_definitions(sensor_pico_batched_sleep PRIVATE SENSOR_LIGHT_SLEEP=1)
target_link_libraries(sensor_pico_batched_sleep PRIVATE hardware_clocks hardware_timer)
