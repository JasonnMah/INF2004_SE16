# INF2004 Two-Pico Temperature Logger

Two RP2040 Pico W boards: a bare-metal Sensor and a Wi-Fi Pico running the
course micro T-Kernel 3 SMP port. JZ owns sensing, encoding, RTC and flash;
Jason owns UART, Wi-Fi, RTOS and HTTP.

## Current milestone

Physically tested: live AHT10 readings -> compressed RAM pages -> UART ->
Wi-Fi HTTP POST -> PC binary storage -> decoded CSV download. The latest
six-reading trial passed, with exact PC-file/capture and CSV checks.
Earlier physical tests cover UART timeout/recovery, final-ACK recovery and
HTTP duplicate/lost-receipt/outage recovery. These are scoped trial results.

Pending: flash-backed batched retrieval, PC interval configuration, coordinated
three-nibble TIMEBASE support, long-hold flash CLEAR, RTC, sleep/wake/preamble
and extended stress/error tests. AHT10 startup initialization still warns despite successful
measurements. Current timestamps are simulated; live storage is RAM only.

- [Wi-Fi firmware, live Sensor test, wiring and PC receiver](wifi_pico/README.md)
- [Current and historical test evidence](wifi_pico/tests/UART_INTEGRATION_RESULTS.md)
- [Desktop/toolchain setup](DESKTOP_SETUP.md)
- [Frozen UART baseline and regression tests](uart_protocol/README.md)

Folders: sensor_pico/ is JZ's unchanged reference; shared/uart_protocol/ is the
portable protocol; wifi_pico/ is the SMP application; uart_protocol/ is the
regression reference. Keep all four together. Local credentials, generated
builds and received data are ignored by Git.
