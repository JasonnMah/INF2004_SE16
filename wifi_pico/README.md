# Wi-Fi Pico

RP2040 Pico W application using the pinned course micro T-Kernel 3 SMP port.
Four application roles: UART Communication, UI, Status and Wi-Fi/HTTP Upload.
The separate Sensor stays bare-metal. Temperature decoding happens on the PC.

## Status

User-observed physical tests pass for live AHT10 -> RAM encoding -> UART ->
HTTP -> PC binary/CSV download, including a six-reading trial. Earlier physical
checks cover UART timeout/final-ACK recovery and HTTP retry/duplicate handling.
See [test evidence](tests/UART_INTEGRATION_RESULTS.md) for each test's scope.

Flash-backed retrieval/CLEAR, real RTC timestamps and sleep/wake/preamble remain
pending. The live Sensor init warning remains unresolved despite valid readings.
Wi-Fi DHCP/disconnect recovery is limited; the corrected diagnostics suppress
stale failure floods, without implementing a reconnect mechanism.

## Agreed next integration (not implemented in this checkpoint)

- PC interval input: 1..4095 whole minutes, with a current Unix timestamp for
  CONFIG. The existing UART CONFIG field remains seconds (minutes multiplied by 60).
- New compressed TIMEBASE interval: three nibbles / 12 bits. The tested local
  encoder/decoder currently uses four nibbles; coordinate both changes and retain
  a way to decode the existing test logs.
- GP21: first press prepares a stable log; second press automatically retrieves
  and uploads bounded batches, with final PC assembly/CSV only after completeness.
  The current implementation still retrieves one complete RAM snapshot first.
- Long-hold GP22: verified Sensor flash CLEAR. Current short-press CLEAR is RAM only.
- Preamble before Sensor commands/frames, coordinated with actual Sensor sleep
  and wake timing. Exact byte pattern/duration/frame coverage still need agreement.
- Buzzer during Sensor transmission is optional/TBD.

## Build and current firmware

From PowerShell in the repository root (tools: [desktop setup](../DESKTOP_SETUP.md)):

```powershell
.\wifi_pico\build.ps1 -Target combined -PicoSdkPath "$env:USERPROFILE\.pico-sdk\sdk\2.2.0"
```

Flash only the Wi-Fi Pico with `course/build_make/wifi_pico_combined.uf2`.
The same directory contains its ELF. The UART-only fallback is built with
`-Target uart`; `loopback`, `boot` and `http` remain separate test selections.
UART/loopback support `-Fault 0..3` and `-VerboseDiagnostics`; normal use is
fault 0. Old diagnostic binaries can be rebuilt from the retained source.
Generated build_make is ignored; course/build_rules is required source.

For the live Sensor test (separate target; JZ's reference files stay unchanged):

```powershell
.\wifi_pico\tests\sensor_live_logger\build.ps1
```

Flash the Sensor with `tests/sensor_live_logger/build/sensor_live_logger.uf2`.
Add `-I2CDiagnostics` to build the separate `sensor_live_logger_i2c_diag.uf2`;
this currently tested image records startup I2C transactions without fixing them.
Both images use 1024-byte RAM storage, JZ's encoder and simulated timestamps;
resetting the Sensor loses its RAM readings. Send USB `s` for status or `d` for
its last UART snapshot. Use 115200 USB serial with DTR enabled on both boards.

## Wiring and controls

Connect with both boards powered off; use a USB supply for each board.

| Connection | From | To |
| --- | --- | --- |
| Wi-Fi -> Sensor UART | Wi-Fi GP0 TX | Sensor GP5 RX |
| Sensor -> Wi-Fi UART | Sensor GP4 TX | Wi-Fi GP1 RX |
| Common ground | Wi-Fi GND | Sensor GND |
| AHT10 data | Sensor GP0 | SDA |
| AHT10 clock | Sensor GP1 | SCL |
| AHT10 power | Sensor 3V3 OUT / GND | VCC / GND |

| Wi-Fi Maker Pi button | Action | GP6 success indication |
| --- | --- | --- |
| GP20 | CONFIG/start logging | One 200 ms flash |
| GP21 | Retrieve; next press uploads saved log | Retrieval: two short flashes; upload: one 700 ms flash |
| GP22 | Explicit Sensor CLEAR | One 500 ms flash |

Failure: three short flashes. Buttons use active LOW and 30 ms debounce;
holds produce one event. Busy presses are ignored. A failed upload keeps the
same saved bytes/ID for the next GP21 retry; success leaves the Sensor log intact.
CLEAR discards the Wi-Fi copy only after ACK and leaves PC files untouched.
The current live Sensor CLEAR clears RAM and pauses sampling, not flash.
CONFIG is refused while an unuploaded copy is waiting; upload or clear first.

## PC receiver and CSV

Use a 2.4 GHz WPA2 home/laptop hotspot on the same network as the PC.
University username/password Wi-Fi is not implemented. Put SSID/password,
PC address/port and HTTP_TEST_CONFIG_SET=1 in ignored config/http_test.local.h;
copy config/http_test.example.h only if the local file is absent. Rebuild after
changing embedded settings. Never include local credentials in shared source.

From the repository root, keep this receiver running for multiple uploads:

```powershell
python .\wifi_pico\tools\http_receiver.py --bind 192.168.137.1 --decode-jz --timestamp-source simulated
```

Use the laptop hotspot's actual address if different. Open
http://192.168.137.1:8000/ to download CSVs; refresh after upload. Files are also
saved under http_received/. The receiver retains the original .bin, validates
page CHECKs, then reuses ../sensor_pico/tools/decode.py for CSV temperatures.
Keep that decoder with the project; no extra Python packages are needed.

CSV includes sample number, temperature_c, UTC/SGT time, unix_seconds,
timestamp_source, status, segment, slot and page. Temperatures use 0.25 C steps.
Current simulated timestamps come from CONFIG, not the actual wall clock;
repeated CONFIG repeats timestamps/slot 0. `rtc` is an operator label to use
only after real RTC integration is verified. Invalid/incomplete pages remain
saved as binary with a CSV error; empty logs produce header-only CSV.
HTTP success confirms binary storage. Downloads serve only CSV files.

Convert a previously received binary without another Pico transfer:

```powershell
python .\wifi_pico\tools\temperature_csv.py .\http_received\<filename>.bin --timestamp-source simulated
```

The separate known-byte HTTP image uses receiver `--expect-known`, without
`--decode-jz`. `--drop-first-receipt` is a controlled lost-response test only.
Without CSV flags, the old binary-only receiver behavior remains available.
No automatic Sensor CLEAR follows an upload.

## UART interface to confirm with JZ

UART0 GP0/GP1 on Wi-Fi, UART1 GP4/GP5 on the live Sensor: 115200 8N1,
no flow control, version 1, flags 0, session 0x11223344. This test accepts CONFIG
without HELLO. All multibyte UART fields are little-endian.

```text
7E | version:u8 | type:u8 | flags:u8 | session:u32 | sequence:u16 |
payload_length:u16 | payload[0..256] | CRC:u16 | 7E
```

CRC-16/X-25 covers version through payload. Interior 7E/7D bytes are escaped
with 7D and XOR20. Frame encoding is independent of JZ's inner log format.

| Message / code | Payload |
| --- | --- |
| HELLO / 01 | Empty |
| CONFIG / 02 | timestamp:u32, interval_seconds:u32 |
| REQUEST_DATA / 03 | start_block:u16 |
| DATA / 04 | block_index:u16, then compressed bytes |
| ACK / 05 | acknowledged_sequence:u16 |
| NACK / 06 | rejected_sequence:u16, reason:u8 |
| CLEAR_LOG / 07 | Empty |
| TRANSFER_END / 08 | total_blocks:u16 |

DATA chunks are 8 bytes. Frame payload limit: 256 bytes; total receive capacity:
1024 bytes. ACK timeout: 500 ms with up to three retransmissions; receive
inactivity: 2500 ms after REQUEST ACK or new DATA progress. Duplicates are ACKed
without appending/completing twice. CLEAR's provisional 120-second ACK window
does not prove flash erase integration. Current CONFIG is timestamp1700000000,
interval3600 seconds; the live adapter requires whole-minute intervals.

Sensor UART integration supplies write/time callbacks and connects JZ's
configure/snapshot/clear/release operations to shared/uart_protocol. Snapshot bytes and length must stay stable
until release. Flash CLEAR must ACK only after erase and verification complete.
JZ's sensor_pico/CMakeLists.txt does not yet link uart_sensor; the live example
currently compiles the same shared sources directly. No JZ source was changed.

## Code, tasks and tests

main.c starts UART-only tests; combined_main.c starts the combined application.
ui.c/buttons.c handle inputs; communication.c owns protocol/workflow state;
uart_io.c connects shared protocol to kernel_io.c hardware/IPC operations;
status.c owns GP6. upload.c/log_workflow.c/log_id.c handle immutable upload copies
and deduplication IDs. lab_capture.c prints bounded binary diagnostics.
rx_ring.c provides bounded receive storage. Shared headers declare interfaces.

Combined priorities: USB infrastructure 3, Communication 4, radio/HTTP 6 on
core 0; UI 8 and Status 9 on core 1. Lower numbers run first. Only the radio
owner calls raw lwIP. UART RX ISR buffers/notifies; the Communication Task
parses frames. Cross-task messages use kernel queues. Upload bytes remain
protected until the radio owner returns a result. No decoder task is added.

course/ contains the pinned kernel, board/USB/network support, required licences
and source provenance. patches/ records its local changes. tests/firmware/
contains active HTTP code as well as test entries; it is needed by the build.
Main regression targets are retained. Historical standalone probe/scan programs
remain local diagnostics and are not dependencies of this checkpoint.

Run host checks in WSL from the repository root:

```sh
python3 wifi_pico/tests/run_http_host_tests.py
python3 wifi_pico/tests/run_combined_host_tests.py
python3 wifi_pico/tests/run_uart_host_tests.py
python3 wifi_pico/tests/run_two_board_host_tests.py
```

Host/build results do not establish physical behavior. Read the
[test record](tests/UART_INTEGRATION_RESULTS.md),
[dependencies/licences](DEPENDENCIES.md) and
[frozen UART reference](../uart_protocol/README.md).
