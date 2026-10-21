# Wi-Fi Pico course SMP application

Pico W / RP2040 Cortex-M0+, course micro T-Kernel 3 SMP commit
2e8e6ad480c2d84a369b12995694b9bf5e9ddfb3.

Implemented: Communication Task, UART RX ISR/bounded rings, button IPC and GP6
Status Task. A labelled Sensor Emulator Task supports single-board tests.
[UART baseline sources](../uart_protocol/README.md) compile unchanged.
The separate real Sensor Pico remains bare-metal.

**User-observed physical SMP boot passed. UART integration is implemented with
physical testing pending. HTTP/Wi-Fi upload is not implemented or tested.**

## Build
Follow [desktop setup](../DESKTOP_SETUP.md). From the repository root:
```powershell
.\wifi_pico\build_uart_loopback.ps1 -Clean -Fault 0
# USB-only SMP boot regression:
.\wifi_pico\build_smp_boot.ps1 -Clean
```
Wrappers accept -PicoSdkPath and -Distribution. SDK selection uses PICO_SDK_PATH
or the current user's .pico-sdk folder; the SDK/TinyUSB commits are checked.
Both builds explicitly enable SMP=1 with USB diagnostics and no UART0 console.

Outputs in wifi_pico/course/build_make/:
wifi_pico_uart_loopback_f0.uf2/.elf and wifi_pico_smp_boot.uf2/.elf.
Required course Make recipes are tracked as course/build_rules; wrappers copy
them into the ignored build_make workspace. Rebuild rather than copying caches.

## Hardware/tests
Cross-jumpers: GP0->GP5, GP4->GP1. GP20 CONFIG, GP21 retrieve, GP22 clear;
active-LOW buttons, 30 ms debounce, GP6 status LED. USB serial needs DTR.
AHT10 is not used by the emulator. Follow the guide one physical test at a time.

In WSL at the repository root:
```sh
python3 wifi_pico/tests/run_uart_host_tests.py
```

- [Integration ownership, wiring and first test](UART_INTEGRATION.md)
- [Build/host/emulator results](tests/UART_INTEGRATION_RESULTS.md)
- [Physical SMP boot record](tests/hardware/SMP_BOOT_RESULTS.md)
- [Dependencies/licences](DEPENDENCIES.md)
- [Import/provenance audit](tests/IMPORT_AUDIT.md)
- [Required course documentation](course/README.md)
