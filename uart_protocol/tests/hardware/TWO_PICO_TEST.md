# First two-Pico protocol test

Two independent Pico W/RP2040 firmware images, built with Pico SDK 2.2.0.
No networking, sensor/compression integration, RTOS, authentication or storage
was added. Existing shared protocol and UI sources were not modified.

## Firmware and source boundaries

Both targets link `pico_stdlib`, `hardware_uart`, and these shared sources:

- `src/protocol/crc16.c`
- `src/protocol/frame.c`
- `src/protocol/stream_parser.c`
- `src/protocol/reliability.c`

`wifi_pico_firmware` additionally links exactly:

- `src/apps/wifi_pico_main.c`
- `src/pico2/pico2_protocol.c`
- `src/pico2/pico2_ui.c`

`sensor_pico_protocol_test` additionally links exactly:

- `src/apps/sensor_pico_test_main.c`
- `src/pico1/pico1_protocol.c`
- `src/demo/mock_logger.c`

Each image instantiates only its own endpoint. No failure injection is linked.
The Wi-Fi image does not include the mock bytes or an expected-data comparison.
Normal frame CRC, block sequencing and TRANSFER_END validation remain active.
It reports the received length, not a claim that it compared against known data.
Total receive capacity is 256 bytes (`RECEIVE_CAPACITY`), separate from the
frame payload limit. Future larger logs require deliberate capacity planning.

## Build and flash

From the project root in the configured Pico SDK terminal:

```text
cmake -S . -B build -G Ninja -DPICO_BOARD=pico_w -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target wifi_pico_firmware --parallel 4
cmake --build build --target sensor_pico_protocol_test --parallel 4
```

Flash via BOOTSEL / RPI-RP2, identifying one board at a time:

| Board | UF2 to flash |
| --- | --- |
| Maker Pi / Wi-Fi Pico | `build/wifi_pico_firmware.uf2` |
| Sensor Pico | `build/sensor_pico_protocol_test.uf2` |

Matching `.elf` and `.bin` files are also generated for each target. Both new
targets are explicit-build-only; no old target was renamed or repurposed.
`PROTOCOL_TEST` still selects modes for the original regression target only.

## Wiring

Remove the old same-board loopback jumpers. Use the following cross-board wiring:

| Wi-Fi Pico | Sensor Pico | Purpose |
| --- | --- | --- |
| GP0, UART0 TX | GP5, UART1 RX | Commands and receiver ACKs |
| GP1, UART0 RX | GP4, UART1 TX | Responses and DATA |
| GND | GND | Common reference |

Both UARTs use 115200 baud, 8N1. Each board normally uses its own USB cable for
power and its own USB COM port. Do not connect 3V3, VBUS or VSYS between boards
when both are USB powered. GP20/21/22 and GP6 remain on the Maker Pi carrier;
GP6 is the carrier indicator, not the Pico W onboard LED.

Both new targets enable USB stdio and disable UART stdio locally in CMake.
`printf`/protocol log sinks therefore use USB; GP0/1 and GP4/5 carry protocol
bytes only. No global or existing-target stdio settings were changed.
Open both USB COM ports, e.g. PuTTY at 115200, 8N1, no flow control. Banners
appear on connection/reconnection without waiting for USB before polling UART.

## Session and startup

Both endpoints use common session **0x11223344**. No HELLO is sent or required,
matching the verified single-board UI test. CONFIG still sends the fixed demo
timestamp **1700000000** and sampling interval **3600 seconds**. There is no new
RTC, time source or session negotiation.

Wi-Fi COM port:

```text
Wi-Fi Pico ready
GP20: send CONFIG
GP21: retrieve data
GP22: clear log
```

Sensor COM port at first startup:

```text
Sensor Pico protocol test ready
Logger length: 100 bytes
```

On reconnect, Sensor reports its current logical length (zero if cleared).
Existing Pico 1 diagnostics are routed to USB; no new per-command logging hook
was added. The readiness banner does not promise a new line for every command.

## Physical acceptance sequence (to run manually)

1. Power/connect both boards and open both COM ports.
2. Press GP20 on Wi-Fi Pico. CONFIG is received and ACKed by Sensor:

```text
[UI] GP20 pressed
[UI] Sending CONFIG
[P2] CONFIG acknowledged
[UI] CONFIG acknowledged
```

3. After the LED indication finishes, press GP21:

```text
[UI] GP21 pressed
[UI] Requesting stored data
[P2] REQUEST_DATA acknowledged
[P2] DATA block received and ACKed
... 13 DATA diagnostics in total ...
[P2] TRANSFER_END validated
[P2] Retrieved log: 100 bytes
[UI] Data retrieval complete
```

The unchanged 100-byte mock buffer uses chunks of 8: twelve full chunks and one
4-byte chunk, with existing stop-and-wait ACKs. Release and press GP21 again:
the same 100-byte log is transferred again. Completion never sends CLEAR_LOG.

4. Press GP22 after the previous indication finishes:

```text
[UI] GP22 pressed
[UI] Clearing stored log
[P2] CLEAR_LOG acknowledged
[UI] Log cleared
```

Sensor's existing handler clears logical length/transfer state only. The borrowed
source pointer and 100 static bytes remain untouched; no erase or overwrite is
performed. Repeating CLEAR_LOG is safe. CONFIG does not repopulate the log;
rebooting Sensor initializes its mock length to 100 again.

5. Press GP21 again:

```text
[UI] GP21 pressed
[UI] Requesting stored data
[P2] REQUEST_DATA acknowledged
[P2] TRANSFER_END validated
[P2] Retrieved log is empty (0 bytes)
[UI] Data retrieval complete
```

There are zero DATA frames and the existing zero-block TRANSFER_END. The receiver
starts each retrieval with zero length/block count, so old bytes are not reported
as current data.

GP21/GP22 before successful CONFIG print `[UI] Configure first using GP20` and
send nothing. Active-LOW, pull-ups, 30 ms press/release debounce, one event per
hold and busy-press rejection are all supplied by unchanged `pico2_ui`.
GP6 patterns remain: CONFIG 1 x 200 ms; retrieval 2 x 150 ms; clear 1 x 500 ms;
failure 3 x 100 ms. Debounce and LED updates do not block UART processing.

## Existing failure limits, intentionally unchanged

Command timeouts/retries remain 500 ms and at most three retries. DATA retries
remain owned by Sensor. Once REQUEST_DATA has been ACKed, Pico 2 has no receive
inactivity timeout. If the link then stalls and Sensor exhausts retries, Sensor
logs failure, but Wi-Fi can remain busy receiving. The single-board test's local
failure callback cannot run on another chip. No new remote-failure message or
receiver timeout was introduced. This split milestone tests the specified healthy
link sequence; recovery from that stalled receive currently requires restarting
the test. Resetting just one peer mid-session is also not a new supported recovery
procedure: restart both for a fresh physical test.

## Member 1 integration seam (future work only)

In `src/apps/sensor_pico_test_main.c`, replace the two source arguments in the
`pico1_init` call:

```c
mock_logger_data(), mock_logger_length()
```

with Member 1's `compressed_data, compressed_length`. This is a borrowed immutable
buffer; no duplicate array or allocation was introduced. Later integration must
address, without assuming the startup snapshot remains current:

1. Refreshing the valid logger length when compression produces more bytes.
2. Passing accepted CONFIG timestamp/interval into the real logger.
3. Notifying the actual logger/storage layer on CLEAR_LOG.
4. Keeping the buffer and its length stable during an active UART transfer.
5. Validating live received data without a pre-known byte array on Pico 2.

None of these changes is implemented in this milestone.

## Automated results, 2026-10-01

```text
cmake --build build --target wifi_pico_firmware sensor_pico_protocol_test pico2_ui_protocol_test button_led_test uart_protocol --parallel 4
python tests/run_host_tests.py
python tests/run_ui_tests.py
python tests/check_split_targets.py
python tests/run_matrix.py --results-dir build/split-regression-results
```

| Check | Result |
| --- | --- |
| All five firmware targets build, SDK 2.2.0 / pico_w | PASS |
| Both split targets generate ELF/BIN/UF2 | PASS |
| Existing host protocol tests | PASS |
| Existing UI tests (100-byte/13-frame, repeated/empty retrieval, clear) | PASS |
| Split compile/source/stdio/pin isolation checks | PASS |
| Original UART modes 0, 1, 2, 3, 4 builds and emulator runs | All PASS |
| Both UART byte streams versus existing fixtures in every mode | IDENTICAL |
| Physical two-board or Wokwi runtime verification | NOT PERFORMED |

Regression evidence is in `build/split-regression-results/matrix.json` and its
logs. Structural checks inspect actual CMake compilation entries and generated
artifacts, plus entry-point setup. They do not establish electrical operation
or run the two split images together. Existing host tests exercise the reused
protocol/UI behavior.

Added: the two `src/apps` entry points, `tests/check_split_targets.py`, and this
guide. Modified: `CMakeLists.txt` to add two targets; `tests/run_matrix.py` to
exclude the new app entry points from the original target's expected source set.
All previous regression assertions and fixtures are retained. Existing source
modules, `src/main.c`, hardware tests, Wokwi files and mock bytes are unchanged.
