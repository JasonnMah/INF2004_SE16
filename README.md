# INF2004 SE16 Temperature Logger

The Sensor Pico reads an AHT10 and stores compressed temperature pages in flash.
The Wi-Fi Pico sends configuration over UART and uploads a frozen log to a PC
in 4 KiB batches. The PC verifies the complete log and offers a CSV download.

## Set up the PC

- Install Python 3, Git, CMake, Ninja, the Arm embedded toolchain, and Pico SDK
  2.2.0 with its submodules. The Raspberry Pi Pico extension can install the SDK
  and build tools. If installing the SDK manually, clone the 2.2.0 tag and run
  `git submodule update --init --recursive` inside the SDK checkout.
- For a native Windows Wi-Fi build, install MSYS2 and run
  `pacman -S --needed mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-gcc`
  in its UCRT64 terminal. The build checks the pinned SDK, TinyUSB, CYW43, and
  lwIP revisions. For WSL instead, use Ubuntu 24.04 with make, g++, Python 3,
  and gcc-arm-none-eabi, and add `-BuildEnvironment WSL` to the Wi-Fi command.
- Put both the PC and Wi-Fi Pico on the same reachable 2.4 GHz WPA2 network.
  If needed, copy `wifi_pico/config/http_test.example.h` to
  `wifi_pico/config/http_test.local.h`. In that local file, set
  `HTTP_TEST_CONFIG_SET` to 1, enter the hotspot credentials, and set
  `HTTP_PC_ADDRESS` to the PC's IPv4 address on that network. Leave port 8000
  unless you also change the receiver port. Git ignores the local config.
  Rebuild the Wi-Fi firmware after changing these settings.

## Build and flash

Run from the repository root in PowerShell with the Pico tools on PATH.

```powershell
.\wifi_pico\build.ps1 -Target batched
$env:PICO_SDK_PATH = "$env:USERPROFILE\.pico-sdk\sdk\2.2.0"
cmake -S .\sensor_pico -B .\sensor_pico\build -G Ninja -DPICO_BOARD=pico_w -DERASE_LOG_ON_BOOT=OFF
cmake --build .\sensor_pico\build --target sensor_pico_batched sensor_pico_batched_sleep
```

Set `PICO_SDK_PATH` to the actual installation if it differs. Hold BOOTSEL while
connecting each board, then copy the matching UF2 to its RPI-RP2 drive.

| Board | Firmware |
| --- | --- |
| Wi-Fi Pico | `wifi_pico/course/build_windows/wifi_pico_batched.uf2` (or `course/build_make` for WSL) |
| Sensor Pico with light sleep | `sensor_pico/build/sensor_pico_batched_sleep.uf2` |
| Sensor Pico awake fallback | `sensor_pico/build/sensor_pico_batched.uf2` |

Power off both boards before connecting the wires.

| Signal | Connection |
| --- | --- |
| UART, 115200 8N1 | Wi-Fi GP0 TX, Sensor GP5 RX |
| UART return | Sensor GP4 TX, Wi-Fi GP1 RX |
| Ground | Wi-Fi GND, Sensor GND |
| AHT10 data and clock | Sensor GP0 SDA, Sensor GP1 SCL |
| AHT10 power | Sensor 3V3 OUT VCC, Sensor GND |

## Run the logger

1. Launch the PC receiver with `.\wifi_pico\receive.ps1`. Open
   [the local receiver page](http://127.0.0.1:8000/) and save a whole-minute
   interval from 1 to 4095. Allow Python through the firewall for the hotspot
   network if Windows prompts.
2. On the Wi-Fi Pico serial monitor, wait for `[WIFI] Network ready`. Press
   **Wi-Fi GP20** to fetch the interval and current PC time and configure the
   Sensor. It takes the first reading immediately, samples on schedule, and
   flushes at the next sample wake after each hour.
3. Press **Wi-Fi GP21 once** when ready. The Sensor seals the current page and
   the log transfers automatically. Refresh the page after `[UI] Transfer complete`
   and download `Download_temp_YYYY-MM-DD_N.csv`.
4. To erase the Sensor log, **hold Wi-Fi GP22 for two seconds** when idle. The
   Sensor verifies the flash erase and pauses sampling until the next CONFIG.
   Uploading retains the Sensor log. Files already on the PC remain available.

The Sensor's local GP20, GP21, and GP22 are debug controls for sample, flush,
and USB dump. Use scheduled samples to demonstrate timestamps. The Sensor
anchors time to the PC clock and uses an elapsed timer between samples. After a
Sensor reset, use Wi-Fi GP20 again to resume scheduled logging.

The receiver uses `http_received/pc_interval` by default. Restart it with the
same output folder to retain partial uploads and allow the Wi-Fi Pico to resume
automatically. A Wi-Fi GP6 slow blink means loading or waiting, a fast blink
means retrieval or upload, and three short flashes with buzzer beeps indicate
a failure. A long flash confirms the completed upload.

## Code and verification

- sensor_pico contains the logger, AHT10 driver, encoder, flash, and sleep code.
  The two firmwares share one UART implementation in `sensor_pico/lib/uart_protocol`.
- wifi_pico contains the gateway and flash cache, the pinned µT-Kernel under
  course, and the HTTP receiver and CSV decoder under tools.
- Run `python3 -B wifi_pico/tests/run_batch_tests.py` in WSL for modeled UART,
  recovery, and maximum-size transfers plus integration with the real PC receiver.
  For JZ's host tests, run the commands below with a host C compiler installed.
- Earlier hardware runs verified a 4,352-byte two-batch CSV upload, a pending
  upload after a Wi-Fi reset, and verified CLEAR. Recheck wake-up, scheduled
  sampling, and end-to-end CSV export on the wired boards with the final UF2 pair.

```powershell
cmake -S sensor_pico/tests -B sensor_pico/build/host-unit
cmake --build sensor_pico/build/host-unit --config Debug
ctest --test-dir sensor_pico/build/host-unit -C Debug --output-on-failure
```

See [encoder.h](sensor_pico/encoder.h) for the compressed flash-page format.
