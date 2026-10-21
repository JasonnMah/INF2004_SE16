# UART baseline and bare-metal tests

Pico W / RP2040 modular UART framing, CRC-16/X-25, stop-and-wait transfers,
timeout recovery and duplicate handling. Source and tests are regression
references for the separate [Wi-Fi SMP application](../wifi_pico/README.md).

## Build
Install SDK 2.2.0, Arm GCC, CMake, Ninja and Python; see
[desktop setup](../DESKTOP_SETUP.md). From this directory:
```sh
cmake -S . -B build -G Ninja -DPICO_BOARD=pico_w -DCMAKE_BUILD_TYPE=Debug -DPROTOCOL_TEST=0
cmake --build build --parallel 4
```
Outputs: build/uart_protocol.elf, .bin and .uf2. Select PROTOCOL_TEST=0..4.
diagram.json and wokwi.toml use this build directory.
UART0 represents Pico 2 (GP0 TX/GP1 RX); UART1 represents Pico 1
(GP4 TX/GP5 RX), 115200 8N1. Cross-jumpers: GP0->GP5, GP4->GP1.

Optional targets: button_led_test, pico2_ui_protocol_test, wifi_pico_firmware,
sensor_pico_protocol_test, sensor_codec_loopback_test.
Build with cmake --build build --target NAME.
Read [hardware instructions](tests/hardware/README.md) before flashing.

## Tests and evidence
```sh
npm ci --prefix tests
python tests/run_matrix.py
python tests/run_host_tests.py
python tests/run_reliability_tests.py
python tests/run_ui_tests.py
```
The matrix needs Node.js and the ARM tools; host tests also need native GCC.
Outputs stay under build/. Emulator results are distinct from Wokwi/hardware.

- [Reliability fixes/results](tests/STAGE1_UART_RELIABILITY.md)
- [Single-board hardware results](tests/hardware/SINGLE_BOARD_TEST_RESULTS.md)
- [Module and compressed-buffer interface guide](REFACTOR_GUIDE.md)
- [Refactor results/historical metrics](REFACTOR_TEST_RESULTS.md)
- [Current regression checkpoint](../wifi_pico/tests/UART_INTEGRATION_RESULTS.md)

Compressed bytes are opaque UART payloads. HTTP is not implemented here.
