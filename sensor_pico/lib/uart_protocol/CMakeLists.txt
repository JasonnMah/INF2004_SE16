cmake_minimum_required(VERSION 3.13)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(uart_protocol_library C)
    set(CMAKE_C_STANDARD 11)
endif()
# Two endpoint libraries share one transport-independent wire implementation.

# common protocol code
add_library(uart_wire STATIC
    src/protocol/log_digest.c src/protocol/crc16.c src/protocol/frame.c src/protocol/stream_parser.c src/protocol/reliability.c)
target_include_directories(uart_wire PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_options(uart_wire PRIVATE -Wall -Wextra)

# Sensor side protocol
add_library(uart_sensor STATIC src/pico1/pico1_protocol.c)
target_link_libraries(uart_sensor PUBLIC uart_wire)
target_compile_options(uart_sensor PRIVATE -Wall -Wextra)

# Wi-Fi side protocol
add_library(uart_gateway STATIC src/pico2/pico2_protocol.c)
target_link_libraries(uart_gateway PUBLIC uart_wire)
target_compile_options(uart_gateway PRIVATE -Wall -Wextra)

# Namespaced aliases make the endpoint choice explicit to consumers.
add_library(uart::wire ALIAS uart_wire)
add_library(uart::sensor ALIAS uart_sensor)
add_library(uart::gateway ALIAS uart_gateway)

add_library(uart_sensor_batch STATIC src/batch/batch_sensor.c)
target_link_libraries(uart_sensor_batch PUBLIC uart_sensor)
target_compile_options(uart_sensor_batch PRIVATE -Wall -Wextra)
add_library(uart::sensor_batch ALIAS uart_sensor_batch)
