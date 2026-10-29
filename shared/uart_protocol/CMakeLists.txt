# Two endpoint libraries share one transport-independent wire implementation.

# common protocol code
add_library(uart_wire STATIC
    src/protocol/crc16.c src/protocol/frame.c src/protocol/stream_parser.c src/protocol/reliability.c)
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
