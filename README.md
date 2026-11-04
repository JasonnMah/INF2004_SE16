# INF2004 Two Pico Temperature Logger

A two-board temperature logger using Raspberry Pi Pico W boards. The Sensor
Pico reads an AHT10 and compresses the readings. The Wi-Fi Pico retrieves them
over UART and uploads them to a PC using HTTP. The PC decodes the data and
provides a downloadable CSV with Singapore timestamps.

The current demonstration stores readings in **Sensor RAM**. The Sensor is
bare-metal; the Wi-Fi Pico uses the course micro T-Kernel 3 SMP port.

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

To build the Wi-Fi firmware, install WSL Ubuntu from **Administrator PowerShell**:

```powershell
wsl --install -d Ubuntu-24.04
```

Restart if requested, open Ubuntu, complete its account setup, then install:

```sh
sudo apt update
sudo apt install -y make gcc g++ gcc-arm-none-eabi python3
```

See [desktop setup](DESKTOP_SETUP.md) for the exact SDK dependency revisions
and tool paths. If the boards already contain the correct firmware, running
the PC receiver requires only Python and the complete project folder.

## 2. Configure the network

Use a **2.4 GHz WPA2 network** that allows the Pico to reach the PC. A home
router or Windows laptop hotspot works. Campus username/password Wi-Fi is
not implemented. With a laptop hotspot, the Pico joins that hotspot while
the laptop can keep its existing Internet connection.

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

## 3. Build flash and connect

Run from the project root in PowerShell:

```powershell
.\wifi_pico\build.ps1 -Target combined -PicoSdkPath "$env:USERPROFILE\.pico-sdk\sdk\2.2.0"
.\wifi_pico\tests\sensor_live_logger\build.ps1
```

Hold BOOTSEL while connecting each board, then copy the matching UF2 to its
RPI-RP2 drive:

- **Wi-Fi Pico:** `wifi_pico/course/build_make/wifi_pico_combined.uf2`
- **Sensor Pico:** `wifi_pico/tests/sensor_live_logger/build/sensor_live_logger.uf2`

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
python .\wifi_pico\tools\http_receiver.py --bind 0.0.0.0 --port 8000 `
  --decode-jz --timestamp-source pc-clock --log-format interval12 `
  --output .\http_received\pc_interval
```

The receiver listens on the PC's network interfaces. Allow Python through
Windows Firewall on the network used for the Pico if Windows prompts.
Open **http://localhost:8000/** on the PC. Keep this terminal running while
configuring or uploading; press **Ctrl+C** to stop it. Restart it with the same
command and output folder. No firmware reflash is needed to restart the receiver.

## 5. Collect and download readings

1. On the PC webpage, save a whole-number sampling interval from **1 to 4095 minutes**.
   For a demonstration, choose **1 minute**.
2. Press **Wi-Fi GP20 once** to fetch the PC interval/current time and send CONFIG.
   The Sensor takes an initial reading, then samples automatically at that interval.
3. Wait a few minutes to collect several readings. Sensor button presses are
   not needed; the current live Sensor firmware is controlled through UART.
4. Press **Wi-Fi GP21 once** to retrieve the readings collected so far.
5. After retrieval completes, press **Wi-Fi GP21 again** to upload the saved log.
6. Refresh the PC webpage and download the CSV. Its columns are
   `sample`, `temperature_c`, `timestamp_sgt` and `status`.

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

Sensor reset/power loss discards the current RAM log. Flash-backed retrieval,
external RTC integration and waking the Sensor from sleep are still pending;
timestamps currently come from the PC clock. The AHT10 startup initialization
warning remains under investigation despite successful measurements.

- [Firmware details and tests](wifi_pico/README.md)
- [Physical and automated test results](wifi_pico/tests/UART_INTEGRATION_RESULTS.md)
- [Course dependencies and licences](wifi_pico/DEPENDENCIES.md)
