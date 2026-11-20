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

The default build uses **Windows tools directly**. WSL is no longer required
to build Wi-Fi firmware. Install the Pico extension's SDK 2.2.0 and Windows
ARM toolchain 14_2_Rel1, plus Python 3 and Git as described above.

Install [MSYS2](https://www.msys2.org/) at `C:\msys64` and run this once in
its UCRT64 terminal:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-gcc
```

These are the [Windows GNU Make](https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-make)
and [host C++ compiler](https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-gcc)
packages. The script uses Windows executables and the shell/file helpers already
used by the course recipes. The host compiler builds `elf2uf2.exe`; it does not
compile the ARM firmware. An existing Windows GNU Make on PATH also works.

Default discovery searches `C:\msys64`, Git for Windows, PATH, and
`%USERPROFILE%\.pico-sdk\toolchain\14_2_Rel1`. For other installations pass
`-MakePath`, `-ShellPath`, `-HostCompilerPath` or `-ArmToolchainPath` explicitly.
The last option names the ARM toolchain root containing `bin/`. Checkout and SDK
paths must not contain spaces; tool executable paths may contain spaces.

The pinned course port and licences are vendored; the old external
INF2004_ProjectTemplate checkout is not needed.
From PowerShell at the repository root:
```powershell
.\wifi_pico\build.ps1 -Target combined -PicoSdkPath "$env:USERPROFILE\.pico-sdk\sdk\2.2.0"
.\wifi_pico\build.ps1 -Target uart
```
The script copies tracked `course/build_rules` into ignored `course/build_windows`,
enables SMP=1, uses USB diagnostics and runs the existing static checks. It does
not modify the pinned course source, protocol or application code. Outputs:

```text
wifi_pico/course/build_windows/wifi_pico_combined.elf
wifi_pico/course/build_windows/wifi_pico_combined.uf2
```

Each route saves evidence under its own build directory's `checks/`, preserving
the older tracked evidence. On the original laptop the native build used Windows ARM GCC
14.2.1; the earlier WSL build used ARM GCC 13.2.1. Different compilers can produce
different firmware bytes/sizes. Native builds/static checks do not replace
physical SMP/UART/HTTP testing of the newly compiled image.

The previous WSL route remains optional:

```powershell
.\wifi_pico\build.ps1 -Target combined -BuildEnvironment WSL -PicoSdkPath "$env:USERPROFILE\.pico-sdk\sdk\2.2.0"
```

For that route install Ubuntu-24.04, make, g++, native gcc, Python 3 and
gcc-arm-none-eabi. Use `-Distribution` for another installed WSL distribution.
WSL outputs remain under `course/build_make`. The two object/tool directories
are deliberately separate. Do not copy either build directory, CMake caches or
binaries to the desktop; rebuild there.

The Sensor's separate flash/UART application uses its existing CMake project:
configure `sensor_pico/` with SDK 2.2.0, Pico W and `ERASE_LOG_ON_BOOT=OFF`, then
build target `sensor_pico`. Its UF2 is `sensor_pico/build/sensor_pico.uf2`.
The older RAM target under `wifi_pico/tests/sensor_live_logger` is preserved.

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
and the configured ARM tools. Some sanitizer-based host tests continue to use
Linux/WSL; that is separate from the Windows firmware build. For SMP host checks
use WSL at the repository root:
```sh
python3 wifi_pico/tests/run_uart_host_tests.py
```

## Portability and evidence
Source, tests/golden captures, selected test evidence, licence texts and course
provenance are shared. Generated builds, build_make, node_modules, credentials
and local editor configuration are ignored. Both Windows/WSL build directories
are ignored. Source-only Make recipes are retained
under build_rules. Source checks normalize text line endings for Windows/WSL.

Historical evidence/provenance intentionally retains the original C:\Embedded_Proj,
old SDK user-folder and /mnt/c paths used during those runs. They are not desktop
dependencies. Current scripts derive the checkout path dynamically.
UART/boot builds need no credentials. HTTP uses ignored config/http_test.local.h;
network configuration and .env files are excluded, while examples stay shareable.
Use build.ps1 -Target http -Clean after editing the example into a local file.

Physical SMP boot, UART error recovery and HTTP retry/duplicate handling have
user-observed evidence. Live AHT10 RAM -> UART -> HTTP -> PC CSV download also
passed, including a six-reading trial. The Sensor flash/UART target is implemented
and has build/host checks; physical flash validation, full-region batching, real
RTC and sleep/wake integration remain pending. AHT10 initialization still warns
despite successful reads. See README.md for the current receiver/CSV command and
wifi_pico/tests/UART_INTEGRATION_RESULTS.md for scoped test evidence.
