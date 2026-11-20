#!/usr/bin/env python3
"""Run actual Sensor flash/UART sources with modeled SDK hardware, using Linux GCC.

No connected Pico is used. Generated stubs, binaries and evidence stay in build/.
"""
from pathlib import Path
import importlib.util
import json
import subprocess
import sys

sys.dont_write_bytecode = True
sensor = Path(__file__).resolve().parents[1]
root = sensor.parent
shared = root / "shared/uart_protocol/src"
out = sensor / "build/host"
stubs = out / "sdk_stubs"
stubs.mkdir(parents=True, exist_ok=True)

# Only the SDK surface used by the actual Sensor code is modeled. Flash storage
# is mapped at RP2040's XIP address; program/erase operations can be faulted.
common = r'''#ifndef TEST_SDK_STUBS_H
#define TEST_SDK_STUBS_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef unsigned uint;
typedef uint64_t absolute_time_t;
typedef struct { int unused; } i2c_inst_t;
typedef struct { int unused; } uart_inst_t;
typedef struct { uint32_t dr, rsr; } uart_hw_t;
extern i2c_inst_t *i2c0;
extern uart_inst_t *uart1;
#define PICO_SDK_VERSION_MAJOR 2
#define PICO_FLASH_SIZE_BYTES (2u * 1024u * 1024u)
#define XIP_BASE 0x10000000u
#define FLASH_PAGE_SIZE 256u
#define FLASH_SECTOR_SIZE 4096u
#define PICO_OK 0
#define PICO_ERROR_TIMEOUT (-2)
#define GPIO_IN false
#define GPIO_FUNC_UART 2u
#define GPIO_FUNC_I2C 3u
#define UART1_IRQ 21u
#define UART_PARITY_NONE 0u
uint64_t time_us_64(void);
void sleep_ms(uint32_t ms);
bool stdio_init_all(void);
bool stdio_usb_connected(void);
int getchar_timeout_us(uint32_t us);
void tight_loop_contents(void);
void gpio_init(uint pin);
void gpio_set_dir(uint pin, bool output);
void gpio_set_function(uint pin, uint function);
void gpio_pull_up(uint pin);
bool gpio_get(uint pin);
void i2c_init(i2c_inst_t *i2c, uint32_t baud);
uint32_t save_and_disable_interrupts(void);
void restore_interrupts(uint32_t saved);
uint32_t uart_init(uart_inst_t *uart, uint32_t baud);
void uart_set_hw_flow(uart_inst_t *uart, bool cts, bool rts);
void uart_set_format(uart_inst_t *uart, uint bits, uint stop, uint parity);
void uart_set_irq_enables(uart_inst_t *uart, bool rx, bool tx);
void irq_set_exclusive_handler(uint irq, void (*handler)(void));
void irq_set_enabled(uint irq, bool enabled);
bool uart_is_readable(uart_inst_t *uart);
uart_hw_t *uart_get_hw(uart_inst_t *uart);
void uart_write_blocking(uart_inst_t *uart, const uint8_t *bytes, size_t length);
void flash_range_program(uint32_t offset, const uint8_t *data, size_t length);
void flash_range_erase(uint32_t offset, size_t length);
int flash_safe_execute(void (*function)(void *), void *arg, uint32_t timeout);
#endif
'''
(stubs / "sdk_stubs.h").write_text(common, encoding="utf-8")
for name in ["pico/stdlib.h", "pico/stdio_usb.h", "pico/version.h", "pico/flash.h",
             "hardware/i2c.h", "hardware/flash.h", "hardware/irq.h",
             "hardware/sync.h", "hardware/uart.h"]:
    path = stubs / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('#include "sdk_stubs.h"\n', encoding="utf-8")

executable = out / "sensor_flash_uart_tests"
sources = [sensor / "tests/test_flash_uart.c", sensor / "sensor_uart.c",
           sensor / "encoder.c", sensor / "flash_log.c",
           *sorted((shared / "protocol").glob("*.c")),
           shared / "pico1/pico1_protocol.c", shared / "pico2/pico2_protocol.c"]
command = ["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
           "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
           "-Wl,--defsym,__flash_binary_end=0x10010000",
           "-I" + str(stubs), "-I" + str(sensor), "-I" + str(shared),
           *map(str, sources), "-o", str(executable)]
subprocess.run(command, check=True)
fixture = out / "flash-snapshot-interval12.bin"
result = subprocess.run([str(executable), str(fixture)], text=True, capture_output=True)
(out / "test-output.log").write_text(result.stdout + result.stderr, encoding="utf-8")
if result.returncode:
    print(result.stdout + result.stderr)
    result.check_returncode()
print(result.stdout.splitlines()[-1])

spec = importlib.util.spec_from_file_location("sensor_decoder", sensor / "tools/decode.py")
decoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decoder)
data = fixture.read_bytes()
samples, notes = decoder.decode({0: data})
assert [sample["temp"] for sample in samples] == [25.0, 26.25], samples
assert [sample["unix"] for sample in samples] == [1700000000, 1700000060], samples
assert all(sample["status"] == "ok" for sample in samples), notes
assert not any("mismatch" in note or "UNVERIFIED" in note for note in notes), notes
print("PASS actual Sensor decoder: flash-sourced page, interval12, two temperatures, PC-based timestamps and CHECK")

# Exercise the sleep build's real UART module with corrupted preamble bytes and
# the complete command already buffered. GPIO and current are checked on-board.
sleep_executable = out / "sensor_sleep_uart_tests"
sleep_sources = [sensor / "tests/test_sleep_uart.c", sensor / "sensor_uart.c",
                 shared / "batch/batch_sensor.c", shared / "pico1/pico1_protocol.c",
                 *sorted((shared / "protocol").glob("*.c"))]
subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
                "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                "-DSENSOR_BATCHED=1", "-DSENSOR_LIGHT_SLEEP=1",
                "-I" + str(stubs), "-I" + str(sensor), "-I" + str(shared),
                *map(str, sleep_sources), "-o", str(sleep_executable)], check=True)
subprocess.run([str(sleep_executable)], check=True)

(out / "results.json").write_text(json.dumps({
    "kind": "host integration with modeled flash/UART/SDK; not physical testing",
    "result": "PASS", "sanitizers": "address,undefined",
    "transfer_capacity": 1024, "flash_region_bytes": 1048576,
    "physical": "PENDING", "scope": "sensor_pico actual main/flash/UART and unchanged shared endpoints"
}, indent=2) + "\n", encoding="utf-8")
