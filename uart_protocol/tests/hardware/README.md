# Standalone Maker Pi Pico button test

This independent image tests the GP20, GP21 and GP22 active-LOW buttons on a
Cytron Maker Pi Pico carrier fitted with a Raspberry Pi Pico W. It does not
include the UART protocol. No existing source module or regression test changes.

## Indicator choice

The **carrier's GP6 GPIO indicator LED** lights when GP6 is HIGH. The project
uses GP0/GP1 and GP4/GP5 for UART, leaving GP6 free. GP6 also avoids the carrier's
buttons, SD slot, ESP-01 socket, audio outputs, and GP28 RGB LED. Leave external
devices disconnected from GP6's header/Grove connection for this test.

This is not the LED on the Pico W itself. Pico W's GP25 is used by the wireless
interface; its onboard LED is controlled through the wireless chip. No wireless
driver is needed for the chosen carrier LED.

References: [Cytron's board details and pin assignments](https://www.cytron.io/p-maker-pi-pico)
and the installed SDK's `boards/pico_w.h`.

## Build

From the project root in the configured Pico SDK terminal:

```text
cmake -S . -B build -G Ninja -DPICO_BOARD=pico_w -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target button_led_test --parallel 4
```

The existing cached `PROTOCOL_TEST` selection is preserved and has no effect on
this hardware test. `button_led_test` is excluded from the default build so
ordinary protocol builds/tests retain their existing target behavior.

Outputs are `build/button_led_test.elf`, `.bin`, and `.uf2`.
Use the UF2 for drag-and-drop flashing or the ELF with an SWD debugger.

## Flash and open USB serial (Windows)

1. Hold the Pico W's **BOOTSEL** button while connecting a USB data cable to
   the Pico W's USB connector, then release BOOTSEL.
2. Copy `build/button_led_test.uf2` onto the `RPI-RP2` drive. It will reboot.
3. In Device Manager, look under **Ports (COM & LPT)** for its USB serial COM port.
4. Open that port in a serial terminal, for example PuTTY: connection type
   **Serial**, the corresponding **COMx**, **115200**, **8N1**, flow control **None**.
   USB CDC does not use a physical UART baud clock; 115200 is a convenient setting.
5. Press the carrier buttons after opening the terminal. No startup banner is
   required, and presses made before the terminal connects are not replayed.

Expected lines, once per respective press:

```text
GP20 pressed
GP21 pressed
GP22 pressed
```

Each press must remain LOW for at least 30 ms. Holding a button prints only once.
Release it for at least 30 ms before pressing again. The GP6 indicator lights
for about 100 ms on any accepted press. Simultaneous presses each print a line;
they share one LED pulse. Other buttons continue working while one is held.

Flashing this standalone image replaces the program running on the physical
Pico. To return to the protocol, flash `build/uart_protocol.uf2`. The protocol's
files and Wokwi configuration remain unchanged.
