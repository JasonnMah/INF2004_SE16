# INF2004 Two-Pico Temperature Logger

- [Desktop setup](DESKTOP_SETUP.md)
- [UART baseline and bare-metal tests](uart_protocol/README.md)
- [Wi-Fi Pico course SMP firmware](wifi_pico/README.md)
- sensor_pico/: reserved for JZ's Sensor integration.

JZ: Sensor encoding, RTC and flash. Jason: UART, Wi-Fi Pico, RTOS and HTTP.
Sensor stays bare-metal; Wi-Fi uses the course RP2040 SMP port.
Physical SMP boot has user-observed evidence. UART SMP integration is implemented
with physical testing pending. HTTP POST is planned, not implemented or tested.
