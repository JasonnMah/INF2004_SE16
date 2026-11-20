# INF2004 Two Pico Temperature Logger

A two-board temperature logger using Raspberry Pi Pico W boards. The Sensor
Pico reads an AHT10 and compresses the readings. The Wi-Fi Pico retrieves them
over UART and uploads them to a PC using HTTP. The PC decodes the data and
provides a downloadable CSV with timestamps.

The Sensor is bare-metal; the Wi-Fi Pico uses the micro T-Kernel 3 SMP port.
The earlier RAM demonstration remains available. The new **flash batching**
firmware is described in section 6; its software/build tests pass and its
complete flow still needs testing on both boards.

## 1. Install the requirements

Hardware: two Pico W boards, an AHT10, jumper wires, USB data cables and a
Maker Pi Pico carrier for the Wi-Fi Pico buttons, GP6 LED and GP18 buzzer.

On Windows, install:

- [Python 3](https://www.python.org/downloads/windows/) and make `python`
  available in your terminal. The PC receiver uses the Python standard library;
  **no pip packages are required**.
- [Git](https://git-scm.com/download/win) to obtain/update the project.
- [VS Code](https://code.visualstudio.com/) with the
  [Raspberry Pi Pico extension](https://github.com/raspberrypi/pico-vscode).
  Install **Pico SDK 2.2.0** and its CMake, Ninja and Arm compiler tools.
  The Sensor build expects the extension's tools under `%USERPROFILE%\.pico-sdk`.
  Add the Serial Monitor extension to view board messages.

The Wi-Fi firmware now builds directly on Windows; **WSL is optional**.
Install [MSYS2](https://www.msys2.org/) at its default location, then open its
**UCRT64** terminal once and install the Windows Make and host C++ tools:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-gcc
```

Return to PowerShell for project builds. The script finds these tools and the
Pico extension's ARM compiler automatically. The host C++ compiler builds the
UF2 converter; the ARM compiler builds the Pico firmware. Keep the checkout
and SDK in paths without spaces, as required by the course Make source lists.

See [desktop setup](DESKTOP_SETUP.md) for the exact SDK dependency revisions
and tool paths. If the boards already contain the correct firmware, running
the PC receiver requires only Python and the complete project folder.

## 2. Configure the network

Use a **2.4 GHz WPA2 network** that allows the Pico to reach the PC. A home
router or mobile/laptop hotspot works.

Open PowerShell in the project root. Create the local settings file once:

```powershell
if (-not (Test-Path .\wifi_pico\config\http_test.local.h)) {
    Copy-Item .\wifi_pico\config\http_test.example.h .\wifi_pico\config\http_test.local.h
}
```

Edit `wifi_pico/config/http_test.local.h`:

- Set `HTTP_TEST_CONFIG_SET` to `1`.
- Set `UTK_WIFI_SSID` and `UTK_WIFI_PASSWORD` to the network the Pico will join.
- Set `HTTP_PC_ADDRESS` to the PC's IPv4 address on that network and keep
  `HTTP_PC_PORT` as `8000`. Use `ipconfig` to find the address. For a Windows
  hotspot it is commonly `192.168.137.1`; do not use the Pico's address or localhost.

The local file is ignored by Git. Rebuild/reflash the Wi-Fi firmware whenever
these settings change. Do not overwrite an existing local file with the example.

## 3. Earlier RAM demonstration: build, flash and connect

Run from the project root in PowerShell:

```powershell
.\wifi_pico\build.ps1 -Target combined -PicoSdkPath "$env:USERPROFILE\.pico-sdk\sdk\2.2.0"
.\wifi_pico\tests\sensor_live_logger\build.ps1
```

Hold BOOTSEL while connecting each board, then copy the matching UF2 to its
RPI-RP2 drive:

- **Wi-Fi Pico (Windows build):** `wifi_pico/course/build_windows/wifi_pico_combined.uf2`
- **Sensor Pico:** `wifi_pico/tests/sensor_live_logger/build/sensor_live_logger.uf2`

The Sensor path above is the physically tested RAM demonstration. The newer
`sensor_pico/build/sensor_pico.uf2` adds flash-backed UART integration and still
needs its first physical validation. See desktop setup for its separate build.
To explicitly retain the previous WSL build, add `-BuildEnvironment WSL`; its
outputs remain under `wifi_pico/course/build_make/`.

Wire with power disconnected, then power both boards through USB:

| Connection | From | To |
| --- | --- | --- |
| Wi-Fi TX to Sensor RX | Wi-Fi GP0 | Sensor GP5 |
| Sensor TX to Wi-Fi RX | Sensor GP4 | Wi-Fi GP1 |
| Common ground | Wi-Fi GND | Sensor GND |
| AHT10 data | Sensor GP0 | AHT10 SDA |
| AHT10 clock | Sensor GP1 | AHT10 SCL |
| AHT10 power | Sensor 3V3 OUT / GND | AHT10 VCC / GND |

Open the Wi-Fi Pico USB Serial Monitor with DTR enabled. Wait for
`[WIFI] Connected` and `[WIFI] HTTP available`. A Sensor monitor is optional
for viewing readings. COM numbers vary between PCs. Unmute the Maker Pi
buzzer switch if audible error feedback is wanted.

## 4. Start the PC receiver

In another PowerShell terminal, from the project root, run:

```powershell
.\wifi_pico\receive.ps1
```

The launcher uses port **8000**, enables temperature decoding/CSV, uses the
PC clock and `interval12`, and saves files under `http_received/pc_interval`.
It works from any working directory when invoked using its path.
The receiver listens on the PC's network interfaces. Allow Python through
Windows Firewall on the network used for the Pico if Windows prompts.
Open **http://localhost:8000/** on the PC. Keep this terminal running while
configuring or uploading; press **Ctrl+C** to stop it. Restart it with the same
command and output folder. No firmware reflash is needed to restart the receiver.

## 5. Earlier RAM demonstration: collect and download readings

1. On the PC webpage, save a whole-number sampling interval from **1 to 4095 minutes**.
   For a demonstration, choose **1 minute**.
2. Press **Wi-Fi GP20 once** to fetch the PC interval/current time and send CONFIG.
   The Sensor takes an initial reading, then samples automatically at that interval.
3. Wait a few minutes to collect several readings. Sensor button presses are
   not needed; the current live Sensor firmware is controlled through UART.
4. Press **Wi-Fi GP21 once** to retrieve the readings collected so far.
5. After retrieval completes, press **Wi-Fi GP21 again** to upload the saved log.
6. Refresh the PC webpage and download files such as `Download_temp_2026-11-05_1.csv`.
   The date is when the PC first received the log, in Singapore time. The existing
   sequence continues across dates and receiver restarts in the same output folder;
   retries and repeat downloads keep the same name. Columns remain `sample`,
   `temperature_c`, `timestamp_sgt` and `status`.

GP21 retrieves stored readings; it does not trigger a new measurement.
Readings collected after that snapshot appear in the next retrieval/upload.
Upload success retains the Sensor log. A failed upload keeps the saved Wi-Fi
copy, so GP21 retries that upload. **Wi-Fi GP22 clears the current Sensor RAM
log and pauses sampling**; PC files remain. Upload or clear a waiting copy
before applying another CONFIG.

GP6 blinks slowly during GP20 loading and faster during retrieval/upload.
Final errors produce three short flashes and three beeps. If CONFIG cannot
reach the PC, start/check the receiver and retry GP20.

## Current limits and further information

Sensor reset/power loss discards the verified RAM demo's current log. Flash-backed
UART code is implemented in `sensor_pico/` and has build/host checks, with physical
validation pending. Full-flash batching is implemented as the opt-in firmware pair
below and awaits physical integration testing. External RTC integration and
waking the Sensor from sleep remain pending;
timestamps currently come from the PC clock. The AHT10 startup initialization
warning remains under investigation despite successful measurements.

- [Build and desktop setup](DESKTOP_SETUP.md)
- [Physical and automated test results](wifi_pico/tests/UART_INTEGRATION_RESULTS.md)
- [Pinned course source](wifi_pico/course/ORIGIN.json) and [licence](wifi_pico/course/LICENSE)

## 6. Complete flash batching (new firmware pair)

Build from the project root using the existing WSL setup for Wi-Fi and the
Pico extension's configured CMake tools for Sensor:

```powershell
.\wifi_pico\build.ps1 -Target batched -BuildEnvironment WSL -Clean
cmake --build .\sensor_pico\build --target sensor_pico_batched -j 4
```

For a fresh Sensor build directory, follow the Sensor CMake setup in
[desktop setup](DESKTOP_SETUP.md) first. Flash **both** matching files:

- Wi-Fi: `wifi_pico/course/build_make/wifi_pico_batched.uf2`
- Sensor: `sensor_pico/build/sensor_pico_batched.uf2`

Use the wiring in section 3 and start the PC receiver with
`.\wifi_pico\receive.ps1`. The new endpoints add three range-control messages;
they must be used together. The old `combined` and Sensor targets remain available.

1. Save a 1-minute interval on the PC page. Press Wi-Fi GP20 once and wait for ACK.
2. Wait for some readings, or use Sensor GP20 for extra debug samples.
3. Press **Wi-Fi GP21 once**. It automatically retrieves and uploads the entire
   frozen log, up to **1 MiB**, in **4 KiB batches**. There is no second upload press.
4. Refresh the PC page after `[UI] Transfer complete` and download the CSV.

Each full batch contains 512 existing eight-byte DATA frames. The first 511
are acknowledged normally. The last DATA ACK is held while the gateway saves
and verifies the batch in its flash, POSTs it, validates the PC's storage receipt,
commits progress and erases the temporary bytes. It then ACKs and continues.
The PC exports CSV only after all ranges have been assembled and their full
SHA-256 matches. Empty logs and short final batches are supported.

The Sensor freezes a flash prefix for retrieval. New samples go into later pages
and appear in the next retrieval. Sensor GP20/GP21/GP22 still mean sample, flush
and USB dump. **Hold Wi-Fi GP22 for two seconds** to request verified Sensor flash
CLEAR when idle. Uploading never automatically clears the original Sensor log.

Wi-Fi restart recovery reads its flash journal automatically: unfinished UART
ranges are fetched again, saved batches are uploaded again with the same ID, and
confirmed ranges are skipped. A Serial Monitor is not required for recovery.
Keep/restart the PC receiver with the same output
folder so its saved fragments remain available. Failed HTTP requests use three
attempts, then retry the retained batch automatically after five seconds.
GP20/GP21/GP22 are ignored while transfer/recovery owns the saved data.

The Wi-Fi linker reserves its last 16 KiB for cache/journal. The earlier separate
`-Target flash` firmware is a storage qualification test only, with simulated
receipts; use **`-Target batched`** for the complete system. Do not flash a cache
test or firmware that overwrites that reserved region during a pending transfer.

Software checks: run `python3 -B wifi_pico/tests/run_batch_tests.py` in WSL.
They cover 1 MiB, partial/empty logs, held ACKs, resets, duplicate uploads,
actual Sensor flash callbacks, and the actual HTTP client/receiver/CSV path.
Physical tests still needed: a small complete transfer, more than one 4 KiB
batch, receiver outage/recovery, Wi-Fi reset with a saved batch, and power loss
during a write. Flash safety with live Wi-Fi/UART load is not yet established
by the earlier Wi-Fi-off cache test.
