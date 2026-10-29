# Dependencies and licences

## Course source

course/ is the tracked snapshot of https://github.com/sirfonzie/mtk3smp-rp2040.git,
branch main, commit 2e8e6ad480c2d84a369b12995694b9bf5e9ddfb3. Course hub:
https://github.com/sirfonzie/mtk3-hub.git at 9b33a1090ae64882435022087667be63073c5656.
Hardware is RP2040 Cortex-M0+, not RP2350. No kernel versions are mixed.

The earlier INF2004_ProjectTemplate commit
c3b09544cb1fd6dac6ad4b8f523be0af59b44482 is historical only. The selected release
derives from TRON Forum mtk3_bsp pico_rp2040, tag v1.00.00.B5-pico_rp2040;
see the unchanged course README for its attribution and required documentation.

course/ORIGIN.json records upstream/current hashes and stored paths. The original
build_make recipes are tracked under build_rules and copied into ignored
build_make by build.ps1. Text hashes normalize CRLF to LF; original hashes remain.
Local changes are recorded, in order, in patches/:
- milestone1-board-config.patch: project build selection, sample drivers off,
  USB-only startup with UART0/GP16 unclaimed.
- http-owner-hook.patch: HTTP-profile credentials, TCP without echo examples,
  and an application hook in the sole raw-lwIP owner after status publication.
- layout-cleanup.patch: paths to the relocated project Make profiles only.
- http-network-diagnostics.patch: HTTP-only scan-result callback; no join, retry,
  timeout or UART behaviour changes.
- http-network-retries.patch: HTTP-only request for a fresh scan/join after a
  terminal association failure. Three startup attempts, two-second gaps; same
  radio owner and original join timeout. Other course profiles are unchanged.
- http-nonet-window.patch: HTTP-only NONET rejoin within the original 30-second
  window, using one-second pacing in the existing radio task. Mirrors the SDK
  2.2.0 choice to retry NONET before its deadline; no blocking SDK loop is linked.

The complete course snapshot preserves its Make dependencies, licences and
reference material. Disabled drivers, other processor ports and upstream demos
are not claimed as runtime requirements. tests/IMPORT_AUDIT.csv has one purpose/
disposition/build-use row for each of the original 599 template files, backed by
tests/previous_template_import.json. Keep those records; they replace a separate
repeated import explanation.

## External tools and source

Use the installed Pico SDK 2.2.0 through PICO_SDK_PATH or build.ps1 -PicoSdkPath.
The default uses the current user's .pico-sdk/sdk/2.2.0. build.ps1 checks:

| Dependency | Commit |
| --- | --- |
| Pico SDK 2.2.0 | a1438dff1d38bd9c65dbd693f0e5db4b9ae91779 |
| TinyUSB | 86ad6e56c1700e85f1c5678607a762cfe3aa2f47 |
| CYW43 driver, HTTP only | dd7568229f3bf7a37737b9e1ef250c26efe75b23 |
| lwIP, HTTP only | 77dcd25a72509eb83f72b033d219b1d40cd8eb95 |

Use Windows Git/PowerShell and WSL Ubuntu-24.04 with Make, Arm GCC, host GCC/g++
and Python 3. Validated Arm GCC is 13.2.1. HTTP C host tests use ASan/UBSan.
The Windows PC receiver uses Python 3 standard library, no pip packages.
See ../DESKTOP_SETUP.md for installation. No external old course checkout is needed.

The course owns startup, scheduling, vectors and USB interrupts. Selected SDK
headers/TinyUSB sources are linked; SDK startup/multicore/RTOS code is not linked.
HTTP additionally links the external CYW43 driver/radio firmware and lwIP through
the course polling port. UART/boot profiles have radio disabled. No MQTT,
FreeRTOS or authentication layer is added. All raw network calls share the
course's core-0 radio task because NO_SYS=1 and SYS_LIGHTWEIGHT_PROT=0.

## Licences

Keep course/LICENSE, its README Licensing section, original source headers and
course/LICENSES/. They include T-License 2.2 (kernel/BSP/SMP), T-License 2.1 for
older headers, Apache-2.0 (course integration), Raspberry Pi/Pico SDK BSD notices,
MIT TinyUSB, CYW43-Pico-LICENSE.RP.txt and lwIP-COPYING.txt. External source/header
notices stay at their SDK paths. No upstream notices have been removed.

## Settings and evidence

config/http_test.local.h and generated headers/binaries are ignored. Firmware
with real credentials must remain local. Copy the public example only when a
local file does not exist; do not overwrite saved hotspot settings.
The frozen uart_protocol source/tests and shared wire codec stay checksum checked.
sensor_pico remains JZ's unchanged reference; its encoder/decoder may be read by
host tests, which do not establish physical sampling or flash/UART operation.
Results distinguish builds, host/emulator checks and user-observed hardware.

Combined application local change: patches/combined-application.patch selects
the project-owned combined Make profile and changes only that profile's radio
priority from 4 to 6, below UART priority 4. ORIGIN.json records both changed
course files against the same upstream commit. No additional external library
is introduced. log_id.c implements SHA-256 content IDs (not authentication);
host checks compare all lengths 0..1024 with Python hashlib.
