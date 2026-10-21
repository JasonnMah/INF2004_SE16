# User-observed physical SMP boot evidence

Date: 2026-10-20 (Asia/Singapore).
Hardware: one RP2040 Pico W on Maker Pi carrier, USB serial; user removed UART
jumpers and AHT10 wiring. Firmware: wifi_pico_smp_boot.uf2, course commit
2e8e6ad480c2d84a369b12995694b9bf5e9ddfb3.
The user flashed and observed the board; the assistant did not operate it.

## Result: PASS for this short single-board SMP boot observation

The submitted excerpt shows diag0 reporting tk_prc=1 / physical core 0 and diag1
reporting tk_prc=2 / physical core 1. Both heartbeat counters advance from 30 to 31.
The kernel event-flag coordinator reports both-core mask 0x3.
No [FAIL] line is visible in this excerpt. This is not a long-run stability result.

The user reports an approximately 10-second observation. The pasted excerpt
contains only a short portion of it and is clipped at its first and last lines.
Preserve those clipped lines exactly; do not reconstruct missing output.
The raw supplied text is smp_boot_user_capture_2026-10-20.txt.

## Repeated BOOT/BUILD explanation

app_program/smp_boot_main.c calls print_profile() before starting its tasks AND
inside the coordinator's repeating loop. That loop waits for both event bits,
waits 300 ms for USB settling and sleeps 3000 ms between reports. Thus the
[BOOT]/[BUILD] label repeats deliberately approximately every three seconds,
including when the USB monitor is reopened.

The counters continuing 30 -> 31 across this banner support uninterrupted
execution in the supplied excerpt. The repeated banner is not evidence of a reset.
This does not rule out resets outside the excerpt. The misleading periodic BOOT
label will not be used by the new UART application: it will emit [READY]/[HEALTH]
for recurring diagnostics.

## Limits

Physical SMP boot / USB task reporting: user-observed PASS.
UART IRQ/ring/protocol integration: not tested by this capture.
Buttons and GP6 in the RTOS application: not tested by this capture.
Two-board Sensor integration, AHT10, HTTP and networking: not tested here.
Existing bare-metal evidence stays in
../../../uart_protocol/tests/hardware/SINGLE_BOARD_TEST_RESULTS.md.
