# Source and external dependencies

## Vendored course source
Repository https://github.com/sirfonzie/mtk3smp-rp2040.git, branch main,
commit 2e8e6ad480c2d84a369b12995694b9bf5e9ddfb3.
The snapshot is in course/; original build_make recipes are stored as build_rules.
Wrappers copy them into ignored build_make. ORIGIN.json maps logical/stored paths.
It supersedes the provisional import from INF2004_ProjectTemplate main commit
c3b09544cb1fd6dac6ad4b8f523be0af59b44482. No files from that different revision
are mixed into the selected port. The old import's per-file record remains in tests/.

The course release derives from TRON Forum mtk3_bsp branch pico_rp2040,
tag v1.00.00.B5-pico_rp2040; see the unchanged upstream README for attribution.

## External build dependencies (not copied as source)
- Pico SDK 2.2.0: a1438dff1d38bd9c65dbd693f0e5db4b9ae91779.
- Its TinyUSB submodule: 86ad6e56c1700e85f1c5678607a762cfe3aa2f47.
- SDK path: PICO_SDK_PATH or -PicoSdkPath; see ../DESKTOP_SETUP.md.
- Ubuntu-24.04 WSL; GNU Make 4.3; Arm GNU GCC 13.2.1; host g++ 13.3.0;
  Python 3 for source/build verification.
- Git and PowerShell for the Windows wrapper and dependency commit checks.

Only selected SDK headers and TinyUSB sources are compiled; the Pico SDK CMake
runtime, startup, multicore launch and interrupt implementation are not linked.
The course port owns startup, both-core scheduling, vectors and USB IRQ integration.
CYW43, lwIP, MQTT and HTTP are disabled; no network credentials are required.
The optional radio/lwIP integration remains upstream reference material only.

## Licence texts and notices
course/LICENSE, course/README.md (Licensing), and original file headers remain.
Included under course/LICENSES/:
- T-License 2.2 for the kernel/BSP and port SMP additions.
- T-License 2.1 because some original headers still reference it; see the upstream
  explanation rather than treating this as a new project licensing decision.
- Apache-2.0 for the course integration code, USB glue and upstream examples.
- BSD-3-Clause_Raspberry-Pi.txt for course-packaged Raspberry Pi-derived code.
- BSD-3-Clause_Pico-SDK.txt for external SDK code used by this build.
- MIT_TinyUSB.txt for linked TinyUSB code.

No upstream notices are removed. Radio firmware and external radio driver sources
are not redistributed or linked by milestone 1. Refer to the upstream licensing
section before enabling those external dependencies later.

## Deliberate integration boundaries
A separate UART target now implements Communication, UI and Status tasks, an RX
ISR/ring adapter and a clearly labelled Sensor Emulator Task. The boot regression
target remains available. HTTP, radio integration, Upload Preparation and PC
decoding are absent. See UART_INTEGRATION.md for exact ownership and API use. Later production tasks remain the proposed UART, UI,
Upload Preparation, Wi-Fi/HTTP Upload, and Status Indicator tasks; the diagnostic
tasks are a temporary boot harness, not a five-task implementation.

The real separate Sensor Pico remains bare-metal. The single-board Sensor
emulator is explicitly a kernel task, never a reserved bare-metal core.
