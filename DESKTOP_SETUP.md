# Desktop setup

Clone the checkpoint branch anywhere; keep sensor_pico, shared, uart_protocol and wifi_pico together:
```sh
git clone --branch feature/wifi-t-kernel-smp https://github.com/JasonnMah/INF2004_SE16.git
cd INF2004_SE16
```

## Tools and external SDK
Install Git, Python 3, Node.js/npm, CMake, Ninja and an Arm embedded compiler.
Optional VS Code extensions: Raspberry Pi Pico, C/C++, CMake Tools, Serial
Monitor and Wokwi. Local .vscode configuration is excluded; regenerate it
with the desktop's SDK/toolchain paths.

Clone/install Pico SDK 2.2.0 outside this repository:
```sh
git clone --branch 2.2.0 https://github.com/raspberrypi/pico-sdk.git
git -C pico-sdk submodule update --init lib/tinyusb lib/cyw43-driver lib/lwip
```
Required SDK commit: a1438dff1d38bd9c65dbd693f0e5db4b9ae91779.
TinyUSB: 86ad6e56c1700e85f1c5678607a762cfe3aa2f47.
Set PICO_SDK_PATH or pass the Windows SDK path with -PicoSdkPath.
The fallback is the current user's .pico-sdk/sdk/2.2.0, not a laptop username.

## Windows BOOTSEL access for picotool

Drag-and-drop UF2 uses BOOTSEL interface 0 (mass storage). picotool reads and
verifies flash through a separate interface 1. On this laptop, interface 1
reported Windows problem code 28 with no service/driver INF, so picotool could
see the RP2040 but could not open it. This does not establish a failed UF2 write.

Follow the [official picotool Windows instructions](https://github.com/raspberrypi/picotool#zadig):
run [Zadig](https://zadig.akeo.ie/), select RP2 Boot (Interface 1), and install
WinUSB. Options -> List All Devices may be needed. Select only interface 1
(VID 2E8A, PID 0003, MI 01); preserve interface 0's mass-storage driver and the
runtime USB serial driver. Leave only the intended Pico connected in BOOTSEL.
After installation, rerun picotool info -a and verify against the exact UF2.
No SDK/source change or firmware erase is needed for this driver setup.

## Course SMP build
Install WSL Ubuntu-24.04 (or pass another installed name with -Distribution).
Inside WSL install make, g++, native gcc, Python 3 and gcc-arm-none-eabi.
The validated WSL Arm GCC is 13.2.1; the previous bare-metal Windows SDK
toolchain was 14_2_Rel1. These are separate environments.

The pinned course port and licences are vendored; the old external
INF2004_ProjectTemplate checkout is not needed.
From PowerShell at the repository root:
```powershell
.\wifi_pico\build.ps1 -Target combined -PicoSdkPath "$env:USERPROFILE\.pico-sdk\sdk\2.2.0"
.\wifi_pico\build.ps1 -Target uart
```
The build script copies tracked course/build_rules into ignored course/build_make,
enables SMP=1 and runs checks. The SDK Windows directory must be accessible inside WSL.
Do not copy laptop build_make, CMake caches or binaries to the desktop.

## Baseline build and tests
Put Arm GCC, CMake and Ninja on PATH. Set PICO_TOOLCHAIN_PATH if needed.
From uart_protocol:
```sh
cmake -S . -B build -G Ninja -DPICO_BOARD=pico_w -DCMAKE_BUILD_TYPE=Debug -DPROTOCOL_TEST=0
cmake --build build --parallel 4
npm ci --prefix tests
python tests/run_matrix.py
python tests/run_host_tests.py
python tests/run_reliability_tests.py
python tests/run_ui_tests.py
```
Host tests need native GCC. The firmware matrix needs Node.js/rp2040js (npm ci)
and the configured ARM tools. For SMP host checks use WSL at the repository root:
```sh
python3 wifi_pico/tests/run_uart_host_tests.py
```

## Portability and evidence
Source, tests/golden captures, selected test evidence, licence texts and course
provenance are shared. Generated builds, build_make, node_modules, credentials
and local editor configuration are ignored. Source-only Make recipes are retained
under build_rules. Source checks normalize text line endings for Windows/WSL.

Historical evidence/provenance intentionally retains the original C:\Embedded_Proj,
old SDK user-folder and /mnt/c paths used during those runs. They are not desktop
dependencies. Current scripts derive the checkout path dynamically.
UART/boot builds need no credentials. HTTP uses ignored config/http_test.local.h;
network configuration and .env files are excluded, while examples stay shareable.
Use build.ps1 -Target http -Clean after editing the example into a local file.

Physical SMP boot, UART error recovery and HTTP retry/duplicate handling have
user-observed evidence. Live AHT10 RAM -> UART -> HTTP -> PC CSV download also
passed, including a six-reading trial. Flash, real RTC and sleep/wake integration
remain pending; AHT10 initialization still warns despite successful reads.
See wifi_pico/README.md for the current receiver/CSV command and
wifi_pico/tests/UART_INTEGRATION_RESULTS.md for scoped test evidence.
