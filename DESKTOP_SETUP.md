# Desktop setup

Clone the checkpoint branch anywhere; keep uart_protocol and wifi_pico together:
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
git -C pico-sdk submodule update --init lib/tinyusb
```
Required SDK commit: a1438dff1d38bd9c65dbd693f0e5db4b9ae91779.
TinyUSB: 86ad6e56c1700e85f1c5678607a762cfe3aa2f47.
Set PICO_SDK_PATH or pass the Windows SDK path with -PicoSdkPath.
The fallback is the current user's .pico-sdk/sdk/2.2.0, not a laptop username.

## Course SMP build
Install WSL Ubuntu-24.04 (or pass another installed name with -Distribution).
Inside WSL install make, g++, native gcc, Python 3 and gcc-arm-none-eabi.
The validated WSL Arm GCC is 13.2.1; the previous bare-metal Windows SDK
toolchain was 14_2_Rel1. These are separate environments.

The pinned course port and licences are vendored; the old external
INF2004_ProjectTemplate checkout is not needed.
From PowerShell at the repository root:
```powershell
.\wifi_pico\build_smp_boot.ps1 -Clean
.\wifi_pico\build_uart_loopback.ps1 -Clean -Fault 0
```
Wrappers copy tracked course/build_rules into ignored course/build_make, build
SMP=1 and run checks. The SDK Windows directory must be accessible inside WSL.
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
No credentials are required now; real Wi-Fi/network configuration headers and
.env files are excluded, while example templates remain shareable.

UART integration is implemented with physical testing pending. Physical SMP boot
has user-observed evidence. HTTP upload is not implemented or tested.
