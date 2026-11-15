# Host-side unit tests: no Pico, no SDK. Build and run:
#   cmake -S tests -B build_tests && cmake --build build_tests && ctest --test-dir build_tests --output-on-failure
cmake_minimum_required(VERSION 3.13)
project(sensor_pico_tests C)
enable_testing()

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(CMAKE_C_STANDARD 11)
if(MSVC)
    add_compile_options(/W3)            # keep the IDE's own /O and /RTC settings (they conflict if forced)
else()
    add_compile_options(-Wall -Wextra -g -O1)
endif()

# --- encoder: pure logic ---
add_executable(test_encoder test_encoder.c ${SRC}/encoder.c)
target_include_directories(test_encoder PRIVATE ${SRC})
add_test(NAME encoder COMMAND test_encoder)

# --- flash log, against a fake flash (small region so "full" is quick to reach) ---
function(flash_test name source image_end)
    add_executable(${name} ${source} ${SRC}/flash_log.c fake_flash.c)
    target_include_directories(${name} PRIVATE fake ${CMAKE_CURRENT_SOURCE_DIR} ${SRC})
    # flash_log.c checks that the program image (symbol __flash_binary_end) does not reach into the log region.
    # Map that symbol onto a byte inside the fake flash, so the test controls where the "image" ends.
    target_compile_definitions(${name} PRIVATE FLOG_REGION_BYTES=16384
                               "__flash_binary_end=fake_flash_mem[${image_end}]")
    add_test(NAME ${name} COMMAND ${name})
endfunction()
flash_test(test_flash_log     test_flash_log.c     4096)                      # program image far below the region
flash_test(test_flash_overlap test_flash_overlap.c 2093056)                   # image reaches into the region (2 MiB - 4 KiB)

# --- round trip through tools/decode.py ---
find_package(Python3 COMPONENTS Interpreter)
set(DECODE_PY ${SRC}/tools/decode.py CACHE FILEPATH "decoder used by the round-trip test")
if(Python3_FOUND AND EXISTS ${DECODE_PY})
    add_executable(gen_dump gen_dump.c ${SRC}/encoder.c)
    target_include_directories(gen_dump PRIVATE ${SRC})
    add_test(NAME roundtrip COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/roundtrip.py $<TARGET_FILE:gen_dump> ${DECODE_PY})
else()
    message(STATUS "round-trip test skipped (needs python3 and ${DECODE_PY})")
endif()
