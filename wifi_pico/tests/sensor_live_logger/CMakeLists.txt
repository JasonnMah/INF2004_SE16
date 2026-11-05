cmake_minimum_required(VERSION 3.13)
get_filename_component(REPOSITORY "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(PICO_BOARD pico_w CACHE STRING "Board")
include("${REPOSITORY}/uart_protocol/pico_sdk_import.cmake")
project(sensor_live_logger C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()
add_subdirectory("${REPOSITORY}/shared/uart_protocol" shared_uart)
set(JZ "${REPOSITORY}/sensor_pico")
# Read-only reuse; neither JZ's main/flash code nor the frozen reference is compiled.
set(LIVE_SENSOR_SOURCES
    main.c live_logger.c "${JZ}/aht10.c" "${JZ}/encoder.c")
add_executable(sensor_live_logger ${LIVE_SENSOR_SOURCES})
target_include_directories(sensor_live_logger PRIVATE "${JZ}")
target_link_libraries(sensor_live_logger uart::sensor pico_stdlib hardware_uart hardware_i2c hardware_irq hardware_sync)
target_compile_options(sensor_live_logger PRIVATE -Wall -Wextra)
set_source_files_properties(main.c live_logger.c PROPERTIES COMPILE_OPTIONS -Werror)
# A disconnected or stalled USB reader must not stall UART processing for 500 ms.
target_compile_definitions(sensor_live_logger PRIVATE PICO_STDIO_USB_STDOUT_TIMEOUT_US=2000)
pico_enable_stdio_usb(sensor_live_logger 1)
pico_enable_stdio_uart(sensor_live_logger 0)
pico_add_extra_outputs(sensor_live_logger)

# Separately named image: original live-RAM firmware remains available unchanged.
add_executable(sensor_live_logger_i2c_diag ${LIVE_SENSOR_SOURCES} i2c_diag.c)
target_include_directories(sensor_live_logger_i2c_diag PRIVATE "${JZ}")
target_link_libraries(sensor_live_logger_i2c_diag uart::sensor pico_stdlib hardware_uart hardware_i2c hardware_irq hardware_sync)
target_compile_options(sensor_live_logger_i2c_diag PRIVATE -Wall -Wextra)
set_source_files_properties(i2c_diag.c PROPERTIES COMPILE_OPTIONS -Werror)
target_compile_definitions(sensor_live_logger_i2c_diag PRIVATE
    SENSOR_I2C_DIAGNOSTICS=1 PICO_STDIO_USB_STDOUT_TIMEOUT_US=2000)
target_link_options(sensor_live_logger_i2c_diag PRIVATE
    "-Wl,--wrap=aht10_init" "-Wl,--wrap=i2c_write_blocking_until"
    "-Wl,--wrap=i2c_read_blocking_until")
pico_enable_stdio_usb(sensor_live_logger_i2c_diag 1)
pico_enable_stdio_uart(sensor_live_logger_i2c_diag 0)
pico_add_extra_outputs(sensor_live_logger_i2c_diag)
