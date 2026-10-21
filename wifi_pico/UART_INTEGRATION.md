# UART Communication Task integration: single-board harness

Date: 2026-10-20 (Asia/Singapore).
Physical SMP boot: user-observed PASS, see tests/hardware/SMP_BOOT_RESULTS.md.
**Physical UART/ISR/IPC/buttons/LED integration: PENDING.**
No HTTP, networking, AHT10 sampling, RTC, flash, compression or PC decoding is added.

## Images and source boundaries

The existing boot regression remains available through build_smp_boot.ps1.
The new build is build_uart_loopback.ps1. Its normal output is
course/build_make/wifi_pico_uart_loopback_f0.uf2.
The same course commit 2e8e6ad480c2d84a369b12995694b9bf5e9ddfb3 is retained.
Only the existing three-file course patch set changes: the Makefile now selects
either boot or UART application; board/sample-driver settings are unchanged.

Seven files compile directly from ../uart_protocol/src without modification:
protocol/crc16.c, frame.c, stream_parser.c, reliability.c,
pico1/pico1_protocol.c, pico2/pico2_protocol.c, demo/mock_logger.c.
Framing, CRC, escaping, session checks, stop-and-wait, block indices, duplicate
handling, retries and final ACK behavior remain in those existing modules.
No Sensor firmware change or wire-format change is required.

The agreed session remains 0x11223344. As in the verified bare-metal UI target,
no HELLO is required before manual CONFIG. Timestamp=1700000000 and
sampling interval=3600 seconds reuse the existing test values.
The 100 source bytes are opaque test data, not an assumed temperature format.

## Tasks and ownership

| Task | Kernel processor / physical core | Priority | Owned resources |
|---|---|---|---|
| Course USB service | 1 / core 0 | 3 | TinyUSB, USBCTRL IRQ and USB drain |
| UART Communication | 1 / core 0 | 5 | UART0 TX, its RX parser, Pico 2 context, 1024-byte reconstructed-log buffer |
| Sensor Emulator (TEST ONLY) | 1 / core 0 | 6 | UART1 TX, its RX parser, Pico 1 context and 100-byte constant RAM-source view |
| UI | 2 / core 1 | 8 | GP20/GP21/GP22 sampling, 30 ms active-LOW debounce |
| Status | 2 / core 1 | 9 | GP6 LED and non-blocking flash schedule |

Lower priority numbers run first. USB can preempt application tasks; Communication
has priority over its test peer. UI and Status use core 1, with no bare-metal loop
on either core. The initial kernel task supplies low-rate READY text; it is not
an extra production task placeholder.

The actual separate Sensor Pico remains bare-metal. Sensor Emulator is a
single-board test endpoint running under this kernel, not the final architecture.
There are three production-role tasks here: Communication, UI and Status.
Upload Preparation and Wi-Fi/HTTP Upload are deliberately absent. The test Sensor
task and internal kernel/USB tasks do not count toward the proposed five tasks.
The later radio/lwIP owner must stay on core 0 under the course constraints;
UART responsiveness while networking runs remains a future measured test.

Application task stacks are static and aligned: Communication=8192 bytes,
Sensor Emulator=8192, UI=4096, Status=4096. The unchanged kernel/USB service retains
its startup bounded-RAM allocation mechanism. No application log/queue allocation
occurs per command or frame.

## RX ISR and bounded storage

Each UART has a 2048-byte ring (2047 usable bytes). Its IRQ is registered with the
actual course tk_def_int(...TA_HLNG...) wrapper, enabled only on core 0, priority 2.
The handler drains at most 64 FIFO entries, records UART errors/loss counters,
and calls non-waiting tk_set_flg(). It performs no frame parsing, logging, protocol
state handling, HTTP or blocking operation.

Each owner task takes at most 64 bytes under a short local interrupt mask, then
restores interrupts before decoding/dispatching. This local mask is valid ONLY
because both ISR and consumer are pinned to core 0. It is not used to protect
anything shared across cores. Rings and UART statistics never have a core-1 reader.

Ring overflow/UART errors discard the uncertain queued byte stream and reset the
parser. Communication calls pico2_abort(), making partial results invalid and
empty; a later fresh button request can recover. The emulator stops its pending
transfer while retaining its source data. A sticky loss cause is acknowledged
by the owner task before bytes are accepted again.

TX waits with interrupts enabled and yields if FIFO is full. A 250 ms adapter
guard prevents a stuck peripheral from trapping a task; the owner observes the
fault and abandons the partial operation. This is below the existing 500 ms ACK
interval, with no change to wire timeouts or retry limits. The UART baud divider
uses the course 125 MHz peripheral clock: IBRD=67, FBRD=52, 115200 8N1.

## Cross-core IPC and timing

UI sends copied 16-byte messages into an eight-message kernel buffer using
tk_snd_mbf(...TMO_POL), then sets the Communication event flag. Communication
receives using tk_rcv_mbf and is the only task calling pico2_send_config(),
pico2_request_data(), pico2_clear_log(), pico2_handle_frame() and pico2_tick().
Busy presses are consumed, not deferred until the current command finishes.

Communication sends copied indication messages through a second bounded kernel
message buffer. Status alone drives GP6. Queue-full cases are reported explicitly.
Buffers use TA_USERBUF and static storage. No task sends a pointer to mutable
protocol state or a stack-local message that another task retains.

The monotonic time adapter uses the real course tk_get_otm() uptime API. The
course timer period is 10 ms, so debounce/polling and deadline observation have
that scheduling granularity. Constants remain 500 ms ACK timeout, 3 retries,
and 2500 ms no-progress receive timeout ((3+2)*500). A command is exhausted after
its initial send plus three retry intervals, approximately 2 seconds.
Every wait yields or waits on a kernel event; button holds never block UART work.

## Completion, byte equality and final ACK

The test's byte oracle is outside the protocol: initial retrieval must match
the exact 100-byte mock logger; after an explicit acknowledged GP22 CLEAR,
a retrieval must be empty. No automatic CLEAR is sent.

Communication owns both received bytes and validity metadata. Only validated
TRANSFER_END calls completion. Incomplete transfers do not become completed log
events. No other task reads the log buffer and no HTTP handoff exists yet;
future upload ownership must freeze/hand off the completed bytes explicitly.

Repeated TRANSFER_END is passed to the existing handler, which ACKs its cached
sequence/block count and does not invoke completion again. Host scenario f3
drops the first END ACK: the sender retransmits END, receives another ACK and
completion_count remains 1. This has not yet been observed on physical hardware
in the new RTOS application.

## Build and test

From PowerShell at your repository root; see ../DESKTOP_SETUP.md:

```powershell
.\wifi_pico\build_uart_loopback.ps1 -Clean -Fault 0
```

The wrapper copies course/build_rules into ignored course/build_make, pins
SDK/TinyUSB commits, explicitly sets SMP=1, disables
UART console/sample drivers/radio, isolates its object cache, builds and checks
the compiled ISR/IPC symbols and source hashes. Project and reused protocol C
use -Wall -Wextra -Werror; SDK/course warnings are not made fatal.

Host application model:
```powershell
# In WSL at the repository root:
python3 wifi_pico/tests/run_uart_host_tests.py
```

This model tests real application/protocol code with simulated wire and IPC.
It does not execute the kernel, real IRQ controller or both hardware cores.
The preserved baseline's five actual firmware binaries are also tested separately
in the local RP2040 emulator. Neither test is Wokwi or new-target physical evidence.

## First physical UART test only: GP20 CONFIG

1. Unplug USB before reconnecting the jumpers on the same Pico W:
   GP0 (physical pin 1) -> GP5 (physical pin 7);
   GP4 (physical pin 6) -> GP1 (physical pin 2).
   On Maker Pi, use the labelled GP headers. This is one board, so ground is
   already shared. Leave AHT10 disconnected.
2. Flash course/build_make/wifi_pico_uart_loopback_f0.uf2 using BOOTSEL.
3. Open this board's USB serial COM port at 115200 with DTR enabled.
4. Wait for both Communication and Sensor Emulator READY lines. Press/hold GP20
   for about one second, then release. Do not press GP21/GP22 for this first test.
5. Capture the CONFIG output and note the GP6 flash.

Expected relevant output (health/readiness lines may interleave):

```text
[START] Wi-Fi Pico UART SMP single-board test
[EMU] Sensor Emulator Task: UART1 GP4/GP5; immutable 100-byte RAM log
[READY] Communication Task: UART0 GP0/GP1, 115200 8N1
[UI] GP20 pressed
[UI] Sending CONFIG
[P2] CONFIG acknowledged
[UI] CONFIG acknowledged
```

GP6 should flash once for about 200 ms. Holding GP20 must produce only one press
and one CONFIG operation. Later health lines should show Communication on
tk_prc=1/core0, UI on tk_prc=2/core1, and nonzero UART0/UART1 IRQ/byte counters
with dropped=0, faults=0, notify_errors=0.
Repeated [READY]/[HEALTH] text is deliberate; [START] is not periodically printed.
No DATA retrieval should start automatically.

Pass: matching ACK, one GP6 flash, one event during a hold, no FAIL/fault messages,
and continuing health output. Record as a single-board RTOS UART/IPC test.
Stop and provide serial output plus the LED observation before the next test.

## Prepared later scenarios, not physical results

UART_FAULT here is a NEW harness selector, independent of the unchanged
PROTOCOL_TEST=0..4 baseline selector:
- 0: normal GP20/GP21/GP22 integration.
- 1: drop all ACKs for only the first CONFIG command sequence; later new CONFIG
  sequences work without reboot.
- 2: deliver REQUEST ACK but suppress first retrieval DATA/END; next fresh retrieval
  works normally.
- 3: drop only the first END ACK to exercise duplicate-END acknowledgement.

Only normal f0 has been built as ARM firmware at this point; all four selectors
are compiled/run in host application checks. Build/flash later scenarios only as
their individual physical-test steps are reached. Do not infer physical recovery
from the host results.
