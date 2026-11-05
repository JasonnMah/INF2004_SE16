# UART SMP checkpoint results

> Current paths/build commands are in [../README.md](../README.md). Historical
> entries below keep their original commands and source names. The 2026-10-25
> layout cleanup preserves those test records and raw captures.


## Latest verified milestone - 2026-10-29

User-observed physical six-reading live AHT10 -> RAM encoding -> UART -> HTTP
-> PC binary/CSV storage -> browser CSV download: PASS. Independent PC checks
validate all six decoded rows, page CHECK, UART CRC and exact saved/downloaded
bytes. The detailed 2026-10-29 entries below preserve that evidence and the
Wi-Fi diagnostic-flood correction. Flash/RTC/sleep/wake/preamble remain pending;
AHT10 initialization warning remains unresolved. Older dated entries describe
the implementation and pending items at the time of those tests.

## Live-data milestone - 2026-10-28

Real AHT10 readings (29.29/29.30 C) -> unchanged JZ encoder in the separate
Sensor RAM target -> 256-byte/32-block physical UART retrieval -> physical
Wi-Fi HTTP POST -> PC storage: PASS for this trial (HTTP 201, attempt 1).
Full user-supplied Sensor USB dump and Wi-Fi UART capture independently compare
byte-for-byte equal to the actual PC file: all 256 bytes match. Actual JZ decoder
gives two valid 29.25 C samples and valid page CHECK. This first live RAM happy
path is physically observed and its full source-to-PC byte equality verified.
Startup AHT10 init error remains unresolved despite successful reads. This is
RAM only; simulated timestamp, no external RTC or flash retrieval. Automatic
HTTP decoding/CSV export is not implemented. Details and scope below.

## USB console follow-up - 2026-10-27

The removable fixed-250-ms Sensor reference comparison passes physical CONFIG,
100-byte retrieval and HTTP duplicate acknowledgement with exact PC bytes.
Its Sensor USB banner is visible in PowerShell after explicit DTR off/on, and
now also in VS Code COM6 after toggling DTR while monitoring. Current USB path
works; ordinary late-open monitoring alone previously appeared blank. Use DTR
off for 1 second then on to reannounce. This identifies a reproducible connection/
one-time-banner interaction, not a proven VS Code defect or a full explanation
of earlier different-image startup failures. No production Sensor/protocol source
was changed. Detailed chronological results are below.

## Latest physical milestone - 2026-10-26

Two real Pico W boards: temporary bare-metal Sensor startup probe with a fixed
100-byte RAM fixture, plus four-role SMP Wi-Fi combined UART/HTTP application.
Firmware output is user-observed; exact PC files were independently inspected.

| Physical check | Result | Evidence |
|---|---|---|
| CONFIG and 100-byte UART retrieval | PASS; 13 blocks, zero command retries | User captures below; [byte comparison](evidence/probe-physical-retrieval-comparison.json) |
| First combined POST | PASS; HTTP 201 in one attempt; exact 100 bytes | [PC comparison](evidence/combined-physical-pc-comparison.json) |
| Repeated retrieval/upload | PASS; HTTP 200, same file/data | [Duplicate test](evidence/combined-physical-duplicate-upload.json) |
| Lost HTTP receipt after storage | PASS; automatic attempt 2; one exact file | [Lost receipt](evidence/combined-physical-lost-receipt.json) |
| Receiver unavailable, then restored | PASS; three attempts exhausted; one GP21 retry reused saved copy without UART retrieval | [Recovery](evidence/combined-physical-outage-recovery.json) |

This milestone does not establish live AHT10/flash-to-PC integration or physical
UART responsiveness under concurrent network-fault traffic. JZ's unchanged
sensor_pico application still needs its UART interface. The original reference
Sensor startup failure is unresolved; the removable diagnostic was used instead.
Upload/failure GP6 patterns were not explicitly reported in these HTTP captures.
The earlier checkpoint tables and chronological records below remain historical.

---

Original checkpoint recorded 2026-10-20; portability cleanup 2026-10-21.
Course port: 2e8e6ad480c2d84a369b12995694b9bf5e9ddfb3.

| Validation | Result | Evidence |
|---|---|---|
| Physical SMP boot | User-observed PASS; short single-board capture | [Record](#physical-smp-boot-evidence-2026-10-20) |
| UART normal firmware build | PASS, SMP=1 / USB diagnostics | [Checks](evidence/uart-build-f0.json) |
| RX ISR/IPC static checks | PASS; no frame parsing/logging in ISR | Same build checks |
| Application host model | 7 cases PASS; not kernel/hardware execution | [Results](evidence/uart-host-results.json) |
| Baseline protocol/UI host tests | PASS | [Summary](evidence/baseline-host-summary.json) |
| Baseline reliability groups | 8 PASS | Same summary |
| Baseline modes 0-4 | Builds/local emulator PASS; both UART streams match golden traces | [Matrix](evidence/baseline-matrix.json) |
| UART/buttons/GP6 physical tests | PENDING | Await user observations |
| HTTP/networking | Not implemented/tested | Outside checkpoint |

Host cases cover normal/empty retrieval, timeout recovery, buffer loss, corruption,
bounded commands and a lost final ACK. [END ACK trace](evidence/uart-host-lost_end_ack.log)
shows repeated END is ACKed with completion_count=1; physical recovery is pending.

[Boot build metrics](evidence/milestone1-build-checks.json) and
[configuration](evidence/milestone1-effective-config.txt) preserve earlier evidence.
Old absolute paths are historical; see [desktop setup](../../DESKTOP_SETUP.md).
[Integration guide](../README.md) owns architecture and test instructions.

The duplicate generated BUILD_RESULTS narrative was removed; its measurements,
source/import provenance and physical capture remain preserved separately.

## Two-board lab preparation â€” 2026-10-22

[Lab guide](../README.md) and [current validation summary](evidence/two-board-lab-validation.json).
The updated final architecture has four application roles; only Communication,
UI and Status run now. HTTP remains pending. No preparation/decoder task is added.

| Check | Current result | Evidence / scope |
|---|---|---|
| External Sensor SMP build | PASS; Sensor Emulator, Pico 1 and mock/fault code absent | [Build/static checks](evidence/uart-build-two-board.json), actual SMP=1 compiler/assembler flags |
| Preserved single-board SMP build | PASS, separate loopback UF2 retained | [Build/static checks](evidence/uart-build-f0.json) |
| Existing SMP application host cases | 7 PASS | [Host model](evidence/uart-host-results.json), not kernel execution |
| External profile host cases | 6 PASS | [Exact binary checks](evidence/uart-two-board-host-results.json): 300/1024/empty bytes, captured-buffer ownership, capacity recovery, lost END ACK |
| Capture comparator | 8 unit cases PASS; CLI accepts equality/rejects different source | tests/test_capture_comparison.py and run_two_board_host_tests.py |
| Baseline protocol, reliability, UI host tests | PASS; 8 reliability groups | Current validation summary |
| Baseline modes 0-4 | Fresh builds/local RP2040 emulator all PASS; both wire streams identical to golden | Current validation summary; reproducible run_matrix.py |
| Baseline/provenance protection | 86 UART file hashes match; vendored course hashes match | Build verification; uart_protocol/ and course/ have no source edits |
| New two-board physical test | NOT TESTED | Await both board captures + JZ source export and LED observations |
| Actual JZ UART support | Not implemented in merged Sensor source | GP4/GP5 below belong to the preserved UART reference target only |
| HTTP / Wokwi execution this work | NOT IMPLEMENTED / NOT RUN | No HTTP or hardware/runtime claim |

The lost-final-ACK host case retransmits END twice, delivers one ACK after the
first is dropped by the host peer, and retains exactly one completion/capture.
No UART frame, CRC, retry timing, message ID or baseline protocol source changed.

## Current scope â€” 2026-10-23

Sensor source is restored exactly to JZ's merged version. No UART adapter,
log_snapshot helper, new flash code or new Sensor target is included there.
The earlier [experimental record](evidence/shared-sensor-validation.json) is
historical: its Sensor build/flash tests do not describe the current source.

Shared UART tests use JZ's encoder/decoder read-only and simulated storage replies;
they do not prove physical Sensor UART or flash operation. Current shared host
results: [results.json](../../shared/uart_protocol/tests/results.json).
Wi-Fi build and host results remain linked above. Real Sensor integration requires
JZ to add matching UART commands; HTTP and wake-up remain pending.

## Physical two-board CONFIG and retrieval â€” 2026-10-24

User-observed hardware result, not a host/emulator result. Wi-Fi Pico uses
wifi_pico_uart_two_board.uf2 with SMP=1, SensorEmulator=DISABLED and HTTP=PENDING.
The separate bare-metal Sensor runs sensor_pico_protocol_test.uf2 with 100 fixed
RAM bytes. Sensor is powered through a charge-only cable; Wi-Fi USB COM5 provides
the submitted serial output. This does not test JZ's real temperature/flash code.

| Check | Observed result |
|---|---|
| Task execution | Communication tk_prc=1/core0; UI tk_prc=2/core1 |
| GP20 CONFIG | PASS: sequence=1, retries=0; user observed one GP6 flash |
| GP21 retrieval | PASS: REQUEST ACK, 13 DATA blocks, TRANSFER_END validated |
| Completion | received_log_valid=1, received_bytes=100, blocks=13, completion_count=1 |
| Command / LED | sequence=5, retries=0; user observed two GP6 flashes |
| Exact bytes | PASS: all 100 pasted capture bytes match unchanged mock_logger.c |
| CRC-16/X-25 | Recomputed 6d8c, matching captured 6d8c |
| Communication health | reports=40..42, irq=119, bytes=424; dropped=0, faults=0, notify_errors=0 |

Submitted binary capture (offsets are hexadecimal):

```text
[BINARY BEGIN] completion=1 length=100 crc16_x25=6d8c
[BINARY] offset=0000 hex=80960000000000000000000000000081f4400080bfe809ec0000000000000000
[BINARY] offset=0020 hex=0000000000000000000000000000000000000000000000000000000000000000
[BINARY] offset=0040 hex=0000000000000000000000000000000000000000000000000000000000000000
[BINARY] offset=0060 hex=00000000
[BINARY END] completion=1
```

SHA-256 of the received and expected 100 bytes:
508ce5ef84f333f067394c426c37e537693ab9224d29c117150c294e93cb72d4.

Repeat retrieval, physical timeout/recovery, lost final ACK, CLEAR, long-run
stress, real Sensor sampling/flash, wake-up and HTTP remain unverified by this
capture. Earlier PENDING rows above describe their dated checkpoints.

### Repeat retrieval without reboot â€” 2026-10-24

The user pressed Wi-Fi GP21 twice more and supplied both serial captures.
Both 100-byte captures were compared against unchanged mock_logger.c; each
matches every byte and the original capture above. Each has contiguous offsets
0000/0020/0040/0060, CRC 6d8c and exactly one completed capture per request.

| Request | Command sequence / retries | Valid bytes / blocks | Completion / BINARY END | Communication health |
|---|---|---|---|---|
| Second retrieval | 20 / 0 | 100 / 13 | 2 / 2 | reports=69..71, irq=218, bytes=779; all three error counters zero |
| Third retrieval | 35 / 0 | 100 / 13 | 3 / 3 | reports=72, irq=317, bytes=1135; all three error counters zero |

Result: PASS for repeat physical two-board retrieval of fixed RAM data without
reboot. UI still reports core1 and Communication core0. LED behavior for these
two additional requests was not supplied; the first retrieval's two flashes
were already user-confirmed. Sequence numbers also cover outgoing ACK frames.
This is repeated new retrieval, not a physical lost-final-ACK/duplicate-END test.
Timeout/recovery, CLEAR, real Sensor sampling/flash and HTTP remain pending.

### Physical command ACK timeout â€” 2026-10-24

Following the instruction to interrupt Sensor GP4 TX -> Wi-Fi GP1 RX, the user
submitted the Wi-Fi capture: one GP20 CONFIG, three retransmissions, command
retry-limit failure, CONFIG failed, sequence=65 and retries=3. User confirmed
three short GP6 flashes. After failure: received_log_valid=0, received_bytes=0,
completion_count=4. Communication reports=120, irq=416, bytes=1490 with dropped=0,
faults=0 and notify_errors=0. UI/core1 and Communication/core0 still report.

PASS for observed timeout/retry exhaustion, invalidation and failure LED.
Recovery was subsequently observed in the CONFIG recovery capture below. The user subsequently
clarified that GP21 was pressed once more before the wire was disconnected.
Completion_count=4 is therefore consistent with that additional retrieval; the
previously unexplained count difference is resolved by this user clarification.
The subsequent successful CONFIG confirms communication after the user reconnected the wire.

### CONFIG recovery after reconnect â€” 2026-10-24

After reconnecting Sensor GP4 TX -> Wi-Fi GP1 RX and pressing Wi-Fi GP20 once,
the user supplied CONFIG acknowledged from both protocol/UI diagnostics:
sequence=68, retries=0. This follows the instructed recovery without reboot.
Communication reports=170..172, irq=421 and bytes=1507; dropped=0, faults=0 and
notify_errors=0. UI still reports core1 and Communication core0. The additional
17 received bytes are consistent with the successful command ACK.

PASS for physical command ACK timeout followed by successful fresh CONFIG
without reboot. Recovery LED behavior was not supplied in this capture.
Fresh retrieval after the timeout is recorded below. Receive-inactivity failure/
recovery and lost final ACK recovery remain pending; no Sensor source or HTTP
changes were made.

### Data retrieval after timeout recovery â€” 2026-10-24

User supplied a fresh GP21 capture after successful CONFIG recovery without
reboot: REQUEST acknowledged, 13 DATA blocks, TRANSFER_END validated,
received_log_valid=1, received_bytes=100, blocks=13, completion_count=5.
Command sequence=69, retries=0. BINARY BEGIN/END both identify completion=5.
The four capture rows match every byte of mock_logger.c and previous captures;
CRC-16/X-25 recomputes to 6d8c and the same SHA-256 recorded above.

Communication reports=190..191, irq=520, bytes=1862; dropped=0, faults=0,
notify_errors=0. UI/core1 and Communication/core0 continue reporting.
PASS for exact-data retrieval after physical command ACK timeout/recovery
without reboot. No LED observation was supplied for this retrieval or preceding
CONFIG recovery; those LED checks are not newly claimed PASS. CLEAR, physical
receive-inactivity timeout, lost final ACK recovery, real Sensor sampling/flash,
wake-up and HTTP remain pending. Sensor firmware/source was not changed.

### Explicit CLEAR command and indicator â€” 2026-10-24

User supplied GP22 CLEAR_LOG acknowledged / Log cleared, command sequence=84,
retries=0. Communication reports=224..227, irq=525, bytes=1879; dropped=0,
faults=0, notify_errors=0. UI/core1 and Communication/core0 still report.
The first LED observation was missed; on a second GP22 press the user observed
one long GP6 flash, consistent with the programmed 500 ms indication. No timing
measurement or second serial capture was supplied.

PASS for observed CLEAR command acknowledgement and the separately observed
repeat CLEAR LED indication. The Sensor test only sets its RAM log length to
zero. The Wi-Fi text "Waiting for verified flash CLEAR" is misleading for this
test endpoint and is not evidence of physical flash erasure or erase timing.
The subsequent empty retrieval below verifies the visible data result.
Sensor code remains unchanged; no real Sensor flash, wake-up or HTTP result is
claimed. Physical receive-inactivity and lost-final-ACK tests remain pending.

### Empty retrieval after CLEAR â€” 2026-10-24

User supplied GP21 REQUEST acknowledged and TRANSFER_END validated, with no
DATA blocks or BINARY hex rows. received_log_valid=1, received_bytes=0, blocks=0,
completion_count=6. BINARY BEGIN/END both identify completion=6, length=0 and
CRC=0000 (correct CRC-16/X-25 for empty bytes). Command sequence=87, retries=0.
Communication reports=257..258, irq=544, bytes=1947; dropped=0, faults=0 and
notify_errors=0. UI/core1 and Communication/core0 continue reporting.

PASS for physical empty retrieval after explicit RAM CLEAR: one additional
completion, no stale 100-byte payload. No LED observation was supplied for this
step. This completes the observed normal/repeat/command-ACK-timeout recovery/
CLEAR/empty-data checks with the two-board reference firmware. Physical
receive-inactivity failure/recovery and lost-final-ACK handling are NOT TESTED.
The two-board image has UART_FAULT=0 and no controlled fault hooks; the existing
fault modes belong to the separate single-board Sensor Emulator test image.
JZ's real temperature/flash integration, wake-up, HTTP and stress remain pending.

### Quiet USB output and separate Wi-Fi fault-test builds â€” 2026-10-24

Changes after the physical observations above: READY now prints once per USB
connection; core/HEALTH reports print at startup or when error counters change.
-VerboseDiagnostics restores five-second healthy reports in a separately named
_verbose image. UI results, protocol errors/retries and binary capture remain.
The CLEAR wait text now says Sensor acknowledgement and does not claim flash
erasure. Task priorities, affinities, wire format and reliability timing are
unchanged. Sensor, baseline and shared protocol sources match all 60 protected
pre-change file hashes; each build additionally verifies 86 baseline hashes and
the pinned course source. No commit/push, Sensor edits or new Markdown files.

| Clean build / static checks | Result | UF2 bytes | Evidence |
|---|---|---|---|
| two-board | PASS, SMP=1 | 135168 | [checks](evidence/uart-build-two-board.json) |
| f0 | PASS, SMP=1 | 139264 | [checks](evidence/uart-build-f0.json) |
| two-board-f2 | PASS, SMP=1 | 136192 | [checks](evidence/uart-build-two-board-f2.json) |
| two-board-f3 | PASS, SMP=1 | 135680 | [checks](evidence/uart-build-two-board-f3.json) |
| two-board-verbose | PASS, SMP=1 | 134656 | [checks](evidence/uart-build-two-board-verbose.json) |

- Existing single-board application model: all 7 cases PASS.
- External-profile host tests: all 10 cases PASS (the previous 6 plus incoming
  DATA/END suppression and first final-ACK drop, each with nonempty/empty logs).
  First retrieval REQUEST is ACKed; state remains receiving before the 2500 ms
  deadline, fails with valid=0/bytes=0/completions=0, then a fresh request succeeds.
  Final-ACK cases produce two END sends, one delivered final ACK and one completion.
- The normal two-board build still excludes Sensor/Pico1/mock/fault code. Separate
  _f2/_f3 builds exclude Sensor/Pico1/mock code and include only Wi-Fi test hooks.
- The hooks execute in task context, after CRC validation/before protocol delivery
  or before UART TX; no filtering/frame processing is added to the ISR.
- CSV availability check: JZ's unchanged tools/decode.py on sensor_pico/data.txt
  returned 229 CSV samples and three CHECK OK reports. This is an existing-example
  PC check, not UART/HTTP/CSV end-to-end validation or confirmation of a final format.

All images in this table are BUILD/STATIC PASS only; their new logging and
fault behavior still require physical observations. Earlier two-board PASS
records apply to the previous image. Its local pre-change UF2 SHA-256 was
ee62a54a0abdf3504169c1f9690ef7ecdb9d3e2e0e37575ac3204cefad68522b.
That UF2 and sources are retained in the outside-repository safety archive.
Host assertions are in test_uart_app.c; results in uart-two-board-host-results.json.
No HTTP, automatic CSV handoff, real Sensor UART adapter, flash, RTC or wake-up
was implemented here. The next physical step is the separate _f2 image; restart
Sensor to restore the 100-byte mock log after CLEAR, without changing its firmware.

### Physical receive-timeout image startup and CONFIG â€” 2026-10-24

User flashed Wi-Fi wifi_pico_uart_two_board_f2.uf2 and power-cycled the unchanged
Sensor reference firmware after the earlier CLEAR. User-supplied COM5 output
identifies course 2e8e6ad480c2d84a369b12995694b9bf5e9ddfb3, SMP=1, cores=2,
SensorEmulator=DISABLED, UART_FAULT=2, DIAGNOSTICS=0, WIFI=none, HTTP=PENDING.
UI reports tk_prc=2/core1; Communication reports tk_prc=1/core0. Both startup
reports have zero error counters. User observed that READY/HEALTH no longer
repeat routinely while idle. This is not a long-run stability measurement.

One GP20 press produced CONFIG acknowledged in both protocol/UI messages,
command sequence=1 and retries=0. PASS for new-image startup, quiet idle output
and physical CONFIG exchange. No GP6 observation was supplied for this step.
Receive-inactivity failure/recovery has not yet been triggered or verified in
this capture. The next step is first GP21 with all UART wires connected.

### Receive-timeout image: fresh retrieval capture, first failure missing â€” 2026-10-24

User reports pressing GP21 twice because the first LED indication was missed.
The supplied excerpt starts with "Suppression released; fresh retrieval receives
normally" and shows REQUEST acknowledged, 13 accepted DATA blocks, TRANSFER_END
validated, valid=1, bytes=100, completion_count=1, command sequence=4/retries=0,
and matching BINARY BEGIN/END completion=1. All 100 captured bytes exactly match
mock_logger.c; CRC recomputes to 6d8c and SHA-256 matches prior captures.
User observed two GP6 flashes for this successful retrieval.

PASS for the captured fresh retrieval and its success LED on the _f2 test image.
The supplied excerpt does not contain the initial REQUEST ACK, receive-inactivity
failure, invalidation or first failure LED. Do not infer the first timeout from
the release message or sequence number; the full physical timeout/recovery test
remains incomplete until the missing first-failure evidence is supplied or rerun.
No reboot was reported between the two presses, but the missing first-failure
capture is still needed. Sensor, UART baseline and shared protocol code unchanged.

### Physical receive-inactivity timeout and recovery: complete capture â€” 2026-10-24

Result: PASS for the two-board hardware test with deliberate Wi-Fi-side receive
filtering. The user's [full serial capture](hardware/two_board_receive_timeout_user_capture_2026-10-24.txt)
resolves the missing first-failure evidence noted above. No further rerun is
needed for protocol timeout/recovery. Sensor uses unchanged 100-byte reference
firmware and remains bare-metal; Wi-Fi uses the _f2 SMP image, no Sensor Emulator.

- Two CONFIG button events were acknowledged, sequences 1/2, retries=0.
- First retrieval sequence=3: REQUEST ACK is accepted, then Receive timeout: no
  transfer progress. Failure leaves valid=0, bytes=0, completion_count=0, retries=0.
  No DATA success lines, completed-log capture or completion was published.
- Second retrieval sequence=4: suppression released, REQUEST ACK accepted,
  13 DATA blocks, TRANSFER_END validated, valid=1, bytes=100, completion_count=1,
  retries=0. Exactly one BINARY BEGIN/END identifies completion=1. All received
  bytes match mock_logger.c, CRC=6d8c, SHA-256 matches prior 100-byte captures.
- The continuous supplied capture contains one START and no intervening restart;
  user reports only GP20/GP21 interactions. Recovery is observed without reboot.
- User previously confirmed two success flashes; first failure LED was missed and
  is not marked physically observed. This does not require repeating protocol
  tests. The configured receive deadline is 2500 ms; capture has no timestamps
  and does not independently measure elapsed time.

This is physical execution of the receive-timeout mechanism under controlled
frame suppression, not proof of recovery from all electrical UART errors.
Lost-final-ACK hardware recovery remains the next test. HTTP, PC received-byte
CSV integration and real Sensor temperature/flash/wake behavior remain pending.

### Physical lost-final-ACK recovery and repeat retrieval â€” 2026-10-24

Result: PASS for physical two-board lost-final-ACK recovery under a deliberate
Wi-Fi-side one-shot ACK drop. [Raw user attachment](hardware/two_board_lost_final_ack_user_capture_2026-10-24.txt)
is saved byte-for-byte. Its first section contains the older receive-timeout
session; assessment here uses only the new START/UART_FAULT=3 session after USB
reconnection. SensorEmulator=DISABLED, SMP=1, DIAGNOSTICS=0 and HTTP=PENDING.

- Two GP20 CONFIG events acknowledged, sequences 1/2, retries=0.
- First GP21 request sequence=3, retries=0: 13 DATA blocks, first TRANSFER_END ACK
  deliberately dropped once, END validated once, valid=1, bytes=100, completion=1.
  Exactly one BINARY BEGIN/END for completion=1.
- After that capture ends, "Repeated TRANSFER_END ACKed; completion_count=1"
  appears before the second GP21 press. No second completion/BINARY capture is
  emitted for the repeated END. This is the observed final-ACK recovery path.
- Second GP21 request sequence=19, retries=0: a normal 13-block retrieval with
  completion=2 and one BINARY BEGIN/END. The drop hook does not trigger again.
- Both 100-byte captures match unchanged mock_logger.c exactly; CRC=6d8c and
  SHA-256=508ce5ef84f333f067394c426c37e537693ab9224d29c117150c294e93cb72d4.
- User reports GP6 indications as expected for the button actions. No separate
  timed LED observation is claimed. No timeout/failure line occurs in the f3
  session. Post-transfer detailed HEALTH counters are not in this quiet capture.

REQUEST command retries=0 does not mean the Sensor made no retransmission:
the repeated END is the Sensor's retransmission after its missing final ACK.
The repeated END is ACKed without running completion again. Only the later new
GP21 retrieval advances completion_count from 1 to 2.

Focused fixed-RAM two-board tests now pass for CONFIG, exact/repeated transfer,
command ACK timeout/recovery, controlled receive-inactivity timeout/recovery,
explicit RAM CLEAR/empty transfer and lost final ACK recovery. This does not
validate all electrical UART faults, sustained load or real Sensor flash/data.
HTTP/PC received-byte decoding/CSV integration, RTC and wake-up remain pending.
Restore the normal UART_FAULT=0 Wi-Fi image for ordinary operation; no Sensor
firmware change is needed. No code, commit or push was performed for this result.

### Normal firmware restored after fault tests â€” 2026-10-24

User flashed wifi_pico_uart_two_board.uf2 and supplied COM5 startup output:
course=2e8e6ad480c2d84a369b12995694b9bf5e9ddfb3, SMP=1, cores=2,
SensorEmulator=DISABLED, UART0=PROTOCOL, UART_FAULT=0, DIAGNOSTICS=0,
WIFI=none and HTTP=PENDING. No fault-test banner appears. UI reports
 tk_prc=2/core1; Communication reports tk_prc=1/core0. Both startup error
counters are zero; UART0 GP0/GP1 is ready at 115200 8N1.

PASS for observed restoration/boot of the normal quiet image. This final
capture contains no button commands or retrieval; it does not independently
retest transfer after restoration. The prior dated physical observations
establish the focused fixed-RAM UART milestone. Sensor firmware and source
remain unchanged. Real Sensor temperature/flash integration, broader electrical
fault/stress tests, HTTP delivery and PC received-byte decoding/CSV remain
pending. No additional firmware changes, commit or push were made.

### Final normal-image button and data regression â€” 2026-10-24

User supplied a complete normal-image COM5 capture: SensorEmulator=DISABLED,
SMP=1, cores=2, UART_FAULT=0, DIAGNOSTICS=0, WIFI=none and HTTP=PENDING.
UI reports core1, Communication core0; startup error counters are zero. User
confirms GP6 corresponds to each intended button action; no timings measured.

| Observed action | Command sequence / retries | Result |
|---|---|---|
| GP20 CONFIG twice | 1/0, 2/0 | Both CONFIG acknowledgements |
| GP21 retrieval | 3/0 | Valid 100 bytes, 13 blocks, completion=1 |
| GP21 repeat | 18/0 | Valid 100 bytes, 13 blocks, completion=2 |
| GP22 CLEAR | 33/0 | CLEAR_LOG acknowledged, Log cleared |
| GP21 after CLEAR | 34/0 | Valid empty log, 0 blocks, completion=3, CRC=0000 |
| GP20 CONFIG again | 36/0 | CONFIG acknowledged |
| GP21 after re-CONFIG | 37/0 | Still valid empty log, 0 blocks, completion=4, CRC=0000 |

Both nonempty binary captures match every byte of unchanged mock_logger.c,
CRC=6d8c and SHA-256=508ce5ef84f333f067394c426c37e537693ab9224d29c117150c294e93cb72d4.
BINARY BEGIN/END IDs are 1, 2, 3 and 4 in order, with one completion per retrieval.
No command retries or failure lines are present. The revised CLEAR text correctly
says Sensor acknowledgement rather than verified flash erasure. Detailed post-
transfer HEALTH counters are not present in quiet mode; no new counter values
are inferred. LEDs are user-confirmed as expected for these button actions.

Result: PASS for final normal-image CONFIG, repeated exact transfer, explicit
RAM CLEAR, empty transfer and button/status indications. CONFIG does not restore
the reference Sensor's cleared log length or take temperature samples, so the
last empty retrieval is expected. A Sensor restart restores its constant 100-byte
mock source. This capture supplements the preceding boot-only restoration check.

Firmware/source unchanged for this result. The fixed-RAM UART milestone is
validated on the normal image as well as the separate fault images. Real Sensor
sampling/flash/RTC/wake-up, broader UART stress/error cases, HTTP delivery and PC
received-byte decoding/CSV remain pending. No commit or push was performed.

## Known-byte HTTP milestone — 2026-10-24

This is a separate firmware, not completed UART-to-HTTP integration. No physical
Wi-Fi/HTTP test has been performed. User will use a 2.4 GHz home network; campus
username/password support is not implemented/confirmed by the current join path.

| Validation | Result | Scope |
| --- | --- | --- |
| HTTP client scripted host TCP tests | PASS, 10 groups | Exact binary body, fragmented receipts, partial writes/backpressure, lost reply retry, HTTP failures, bounded exhaustion, invalid receipts, network loss/recovery, uptime wraparound. ASan/UBSan enabled. |
| PC receiver and real client over host TCP | PASS, 8 Python tests | Exact 1024-byte save, simultaneous/persisted duplicate requests, lost reply after save, ID conflict, rejected malformed/truncated requests, negative byte comparison, actual client interoperability. |
| PC receiver on Windows | PASS, 7 tests | WSL C socket test skipped on Windows; all receiver tests ran. |
| Course RP2040 SMP HTTP firmware | PASS, build-check settings | SMP=1 in actual C/ASM flags, cores=2, USB console, UART0 free, radio-owner hook, TCP enabled without course echo demos, no Sensor/protocol symbols. POST disabled until real settings supplied. |
| Normal two-board UART rebuild | PASS | UF2 SHA-256 identical to the previously physically tested normal image. |
| Preserved UART host application cases | PASS, 7 | Existing normal/fault/queue/button/status cases. |
| Preserved external UART host cases | PASS, 10 | Existing exact-byte, empty, capacity, timeout and duplicate-END cases plus comparator negative test. |
| USB-only SMP boot rebuild | PASS | Existing source/599-file audit, effective flags, rejected invalid profiles and console checks. |
| Protected source hashes | PASS, 60 files | Sensor, original UART source and shared UART source match pre-HTTP snapshot exactly. |
| Physical home Wi-Fi / HTTP / concurrent UART+HTTP | NOT TESTED | No new hardware claim. Existing UART/SMP user observations above remain separate. |

The HTTP fixture is 00..FF repeated four times, length 1024, SHA-256
785b0751fc2c53dc14a4ce3d800e69ef9ce1009eb327ccf458afe09c242c26c9.
POST /logs uses application/octet-stream and fixed test ID pico-known-1024-v1.
The PC confirms storage using explicit ID/length/result headers. A retry saves no
second file; a conflicting body with the same ID is rejected. This fixed ID must
not be used for different future Sensor logs. No automatic Sensor CLEAR occurs.

Network ownership: existing course core-0 priority-4 radio task executes all raw
lwIP/HTTP calls and callbacks; USB remains priority 3. UI and Status use copied
kernel messages on core 1 at priorities 8/9. No extra raw-lwIP task or bare-metal
core is added. The HTTP test has these three application roles; combining it with
the Communication Task to reach the final four-task design remains pending.

Timeouts: HTTP has three total attempts, 5 seconds for connect, 10 seconds for
send/response and 1 second between retries. UART timing is unchanged. Initial
association uses the existing bounded course join; automated rejoin is deferred.

Sources added: app_program/http_test_main.c; app_program/http/http_post.c/.h and
http_test.c; config/http_test.example.h; http_milestone.mk; build_http_test.ps1;
tools/http_receiver.py; HTTP tests/mock headers/build verifier under tests/.
Existing port files changed only behind the HTTP profile: build selection,
credential include selection, TCP configuration and owner-task application hook.
patches/http-owner-hook.patch and course/ORIGIN.json retain provenance. Applicable
external driver/lwIP licences were added under course/LICENSES. Existing README,
DEPENDENCIES and import audit notes were updated; no new Markdown guide was added.

Evidence: evidence/http-host-tests.log, evidence/http-pc-windows.log,
evidence/http-build-check.json, evidence/uart-build-two-board.json and existing
milestone1-build-checks.json. Negative tests deliberately log rejected requests;
they are not failed positive test cases.

Build-check artifact: course/build_make/wifi_pico_http_known_bytes_buildcheck.uf2
(661504 bytes), SHA-256 802cb061ffd2200042eb0f1f531f8da874509de93ca8d2b51c8588c3e6295843.
It has POST disabled and is not the physical-test image. Configure the ignored
config/http_test.local.h and run build_http_test.ps1 -Clean to generate
course/build_make/wifi_pico_http_known_bytes.uf2 for the home test. See README.

Normal UART UF2 retained: 135168 bytes, SHA-256
c2942977f5014887bc49234c611fc25e864d22e7a0c5457a6d055326fed7aa16.
No UART wire-format/timing/behaviour changes. No Sensor source modifications.
No commit or push. Before-change snapshot is outside the repository at
%LOCALAPPDATA%/Temp/inf2004-before-http-20261024-214929/checkpoint.zip.

## Send-enabled hotspot HTTP build — 2026-10-25

Built wifi_pico_http_known_bytes.uf2 using ignored local network settings.
The settings-ready flag was enabled after the user entered their credentials;
no credential values were printed or added to tracked files.

The first send-enabled link exposed a dependency hidden by the prior build-check
image: snprintf pulled newlib heap/file syscalls (_sbrk, _write, etc.) which the
course runtime does not supply. Replaced only the HTTP request-header formatting
with bounded fixed-buffer string/decimal helpers. HTTP headers/body and retry
behaviour remain the same. No UART or Sensor source was changed.

Revalidation: all 10 scripted HTTP client test groups and 8 PC receiver/host TCP
tests PASS, with AddressSanitizer/UndefinedBehaviorSanitizer on the C tests.
Actual send-enabled firmware build/static checks PASS: SMP=1, two cores, USB
console, UART0 free, linked http_post_start, and no snprintf/_sbrk/_malloc_r.
Evidence: evidence/http-build.json and refreshed evidence/http-host-tests.log.

UF2: 664576 bytes; SHA-256 1e7dfb9c7b79f2529c50d5885a2f4bf6ed33f6eede954a5f337d53f54d9b99bc.
Physical hotspot association and Pico-to-PC HTTP POST are NOT TESTED yet.
The user will flash the Wi-Fi Pico and provide startup diagnostics first.

## Application layout cleanup — 2026-10-25

Scope: simplify the Wi-Fi application using sensor_pico's small root-level module
layout. No Sensor/shared/bare-metal UART source, protocol behaviour, timings,
pins, task priorities, buffer ownership or serial message behaviour changed.
There was no HTTP integration or new hardware test in this cleanup.

Application C/H files now sit beside main.c. UI and Status have separate files;
UART receive dispatch and transmit callbacks share uart_io.c. Test-only boot,
Sensor emulator, fault injection and known-byte HTTP sources are under
 tests/firmware/. Five duplicate build/setup scripts became build.ps1, selected by
-Target uart/loopback/boot/http. Make profiles are in config/. Profile identifiers
were advanced to invalidate old object caches; UF2 names/locations stay unchanged.
Two obsolete fake SDK headers and the redundant .gitkeep were removed.

Three overlapping Markdown guides were consolidated into README.md and
DEPENDENCIES.md. No new Markdown file was added. Test results/raw physical
captures, licences, provenance, the original 599-row import audit and required
course documentation remain. Historical result paths/commands are labelled as
historical, rather than rewritten as if old runs used the new layout.

Only course/build_rules/makefile changed in the vendored course during this
cleanup (three project-profile include paths). ORIGIN.json and
patches/layout-cleanup.patch record it; the course revision is unchanged.
Saved local hotspot settings remained in place and unchanged.

Validation:
- Eight clean ARM firmware builds/static checks PASS (table below).
- All 17 Wi-Fi UART host cases PASS; all 17 complete UART host transcripts match
  the pre-cleanup snapshot after CRLF/LF normalization.
- All 10 HTTP client groups and 8 receiver/host TCP tests PASS, with C ASan/UBSan.
- Eight capture-comparison negative/positive tests PASS on Windows.
- Shared-library codec/backend tests plus original protocol assertions and all
  eight reliability groups PASS; JZ encoder/decoder are read-only inputs.
- Frozen baseline protocol, eight reliability groups and UI host suite PASS.
- Modes 0..4 of the five preserved checksum-matched baseline .bin files were
  rerun in RP2040JS: all PASS with exact golden UART wire bytes. Those frozen
  binaries were reused, not rebuilt during this cleanup. No Wokwi claim.
- All 119 protected shareable Sensor/baseline/shared files and every Wi-Fi raw
  physical capture/boot record match the external pre-cleanup snapshot.
- Original moved C bodies and extracted UI/Status/UART function bodies match the
  pre-cleanup source exactly, apart from include names/locations.
- Verbose diagnostics build PASS. Invalid target/fault/settings combinations
  are rejected before building. Current guide links and Python syntax PASS.

| Current firmware build | Result | UF2 bytes | UF2 SHA-256 |
| --- | --- | --- | --- |
| Normal two-board UART | PASS | 135168 | 19b9081a803efe80aea873188440e84634687210c2c6fdc521ce23d43e3957a3 |
| Receive-timeout UART | PASS | 136192 | 0a5e66a6e7099db03771bce00f3813f4a991124e83547dcc329ebec05d34d12f |
| Lost-final-ACK UART | PASS | 135680 | d9739347d928c4d564b3f0220808fd5f53d8b2d50b3fe940b1d2981d81277d47 |
| Single-board loopback f0 | PASS | 139264 | 1ddfbb51b0207494097bc48701510e8f27afba446b4ae63224459431489755f1 |
| Verbose two-board UART | PASS | 134656 | b7e516465c2d3376880e79e01391791293a1e7071a61af87db232b3c6ac45ab2 |
| USB SMP boot | PASS | 116736 | efed93fe5def9562f06266a4a7d52284b91f1de7a18082ea7de10a1c5f23adab |
| HTTP build check | PASS | 661504 | 802cb061ffd2200042eb0f1f531f8da874509de93ca8d2b51c8588c3e6295843 |
| HTTP send enabled | PASS | 664576 | 1e7dfb9c7b79f2529c50d5885a2f4bf6ed33f6eede954a5f337d53f54d9b99bc |

Application C/H files: 23 -> 21;
application C source lines: 1387 -> 1388.
main.c is 81 lines. Build scripts: 5 -> 1 (183 -> 104 lines).
Overlapping Wi-Fi guide files: 5 -> 2 (761 -> 303 lines).
Source readability, not minimum C line count, was the goal.

Physical results above remain evidence for their original images. The reorganized
UART images have different binary layouts/hashes after moving/merging modules;
no new physical test is claimed. Physical HTTP remains NOT TESTED. Next hardware
step is the existing known-byte HTTP test, then a UART smoke test when available.

Safe snapshot: %LOCALAPPDATA%/Temp/inf2004-before-layout-20261025-124508/checkpoint.zip.
Automatic review rejected a credential-inclusive backup. The completed snapshot
therefore contains only shareable source/tests and radio-disabled UART/boot
firmware; private settings/generated headers and HTTP firmware were excluded.
No commit or push was performed.

## First HTTP hardware startup â€” user observation, 2026-10-25

The user opened Wi-Fi COM5 and Sensor COM6. Wi-Fi reported the known-binary HTTP
image, SMP=1/cores=2, UART0 free and Sensor emulator disabled. User-observed task
diagnostics were UI tk_prc=2/rp2040_core=1 and Wi-Fi/HTTP owner tk_prc=1/core=0.
This is physical task/core evidence for the HTTP image, not an HTTP POST result.
The exact flashed UF2 hash was not supplied, so no exact-image hash is inferred.

Wi-Fi setup reported init=0, join=-2, dhcp=-5. The installed CYW43 header defines
CYW43_LINK_NONET=-2 as no matching SSID found; the course service stores negative
join/link results in join_result. DHCP -5 is its initial pending value here; this
capture does not demonstrate a separate DHCP exchange failure. Association/IP
setup did not succeed and no POST was attempted. The user subsequently said the
PC had not been connected to the iPhone hotspot and has now connected it. PC
connection and Pico association are separate; a fresh Pico attempt is pending.
The original firmware starts one association attempt rather than automatically
starting another after this failure. No code or protocol change was made.

Sensor COM6 showed only port open/close messages. Its running firmware and Sensor
operation cannot be confirmed from silence. The Sensor is not part of this
standalone known-byte HTTP path. Physical HTTP upload remains NOT TESTED.

### HTTP hotspot retry â€” user observation, 2026-10-25

After restarting, the user observed the same init=0/join=-2/dhcp=-5 result.
Read-only checks found local settings enabled, a non-placeholder quoted ASCII
SSID without surrounding whitespace, and generated build settings equal to the
local file. No SSID/password was printed. A host Windows Wi-Fi query then showed
PC connected to the configured SSID on **5 GHz, channel 149, 802.11ax**. This is a
concrete band-compatibility issue to address before changing firmware. Apple
states Maximise Compatibility restricts iPhone Personal Hotspot to 2.4 GHz/WPA2.
Next: enable that setting and restart the Wi-Fi Pico for a fresh association.
Resolution/HTTP POST remain pending; no code or credential values were changed.


### Laptop 2.4 GHz hotspot configuration/build — 2026-10-25

The user configured laptop Internet sharing from the iPhone connection over a
2.4 GHz laptop hotspot and updated the ignored SSID/password locally. Initial
host queries found its Wi-Fi Direct adapter had only 169.254.215.165. After the
user toggled sharing, their ipconfig showed Local Area Connection* 2 at
192.168.137.1/24 and upstream Wi-Fi at 172.20.10.11. The ignored HTTP_PC_ADDRESS
was changed to 192.168.137.1; SSID/password and all firmware source were preserved.

Clean send-enabled HTTP build/static checks PASS using build.ps1 -Target http
-Clean (SMP=1, USB diagnostics, one radio-owner task, UART0 free, frozen UART
baseline). No host-test rerun was needed for this configuration-only change.
UF2: 664576 bytes; SHA-256 c91302237e0f086f54373f72e905dd3802d0cd2125df0c02ccad5c6f7f980ce1.
Flashing, association/DHCP on the laptop hotspot and physical HTTP POST remain
pending user observation. Sensor firmware is unchanged. No credentials printed.


### Repeated laptop-hotspot failure and diagnostic firmware — 2026-10-25

User observed the same init=0/join=-2/dhcp=-5 on the laptop hotspot. Windows API
readback initially confirmed hotspot On, TwoPointFourGigahertz, Wpa2, configured
SSID match, zero clients, and host IP 192.168.137.1. A later readback confirmed
both SSID and password match the local file but the hotspot was Off. No secret
values were printed/saved. Auto-off with no clients is a possibility, not proven
as the cause of any earlier captured failure. Keep hotspot On during testing and
disable its no-client power-saving option if available.

The old join_result line is ambiguous: this course port stores either an
immediate cyw43_wifi_join return or a later link-state code in that field. The
SDK defines LINK_NONET=-2, but the old log alone did not establish which path
produced -2. Earlier SSID/band troubleshooting was not proof of the root cause.

Added bounded one-time HTTP-only diagnostics: scan result count, exact configured
SSID-match count, strongest matching channel/RSSI/auth, and separate join request
return/link status/elapsed time. It logs no network names/keys. Scan callbacks
only retain fixed metadata in the existing radio task before USB is ready;
printing stays in that task after status publication/IPC readiness. Startup now
prints WIFI_DIAGNOSTICS=1 so the newly flashed image can be identified.

Changed tests/firmware/http_test.c and http_test_main.c, added an HTTP-only scan
callback in course cyw43_utk.c, and updated ORIGIN.json plus
patches/http-network-diagnostics.patch. The course commit/revision is unchanged.
No join/retry timing, network state handling, UART behaviour, Sensor/shared source
or credentials changed. Existing guide/dependency/result files were updated;
no new Markdown file was added.

Validation: clean send-enabled HTTP build/static checks PASS; existing 10 HTTP
client groups and 8 PC receiver/host TCP tests PASS. Rebuilt normal UART image:
static/source checks PASS and UF2 identical byte-for-byte to its pre-diagnostic
hash. The new scan/join diagnostics require user-observed physical output; HTTP
POST and successful association on this setup are still NOT TESTED/PENDING.
Diagnostic HTTP UF2: 665600 bytes; SHA-256 68df0974800d20e09af611f0815faecb1906f3ff5732cdc04cf0bcda3b38efe1.

### Diagnostic firmware scan result â€” user observation, 2026-10-25

The startup marker WIFI_DIAGNOSTICS=1 is now present. User-observed scan:
results=30, target_matches=0, ssid_bytes=7. Join request=0, link=-2,
elapsed_ms=939; final init=0/join=-2/dhcp=-5. These are physical diagnostic
observations, not a successful association or POST. Scan results count records,
not necessarily distinct SSIDs. The join API accepted the request and a later
link state reported NONET; the earlier API-return ambiguity is resolved here.

Fresh Windows readback: hotspot On, TwoPointFourGigahertz, Wpa2, zero clients;
SSID and password match the local settings, SSID matches the generated build,
both SSID UTF-8 lengths are 7, and generated/local settings are identical.
No SSID/password values were printed. Upstream PC IPv4 was 10.132.6.191, separate
from the hotspot's 192.168.137.1. Windows configuration readback does not by itself
prove the actual RF beacon/channel visible to a Pico. Root cause is not yet
established. Next check: independent phone visibility/connection to laptop AP.
No UART/Sensor/network code change was made on this observation.

### Independent hotspot client check â€” user observation, 2026-10-25

User confirmed another phone sees and joins the laptop hotspot. Windows readback
corroborated hotspot On, WPA2, configured SSID/password matches and ClientCount=1.
A read-only WlanQueryInterface(channel_number) query returned channel 11 for the
hotspot's Wi-Fi Direct interface; the laptop's separate upstream station returned
channel 112. Hotspot IPv4 remains 192.168.137.1/24. Thus the next Pico attempt can
be made with an active client keeping the laptop AP up, on a reported 2.4 GHz
channel. This does not prove why earlier Pico scans missed the AP. Keep the phone
connected and reset only the Wi-Fi Pico for a fresh diagnostic scan. No new code,
firmware build or credential changes were made for this check.

### Active-hotspot retry and independent scan control - 2026-10-25

User reset the Wi-Fi Pico with the phone still connected to the laptop hotspot.
Physical course/SMP output: scan results=34, target_matches=0, ssid_bytes=7;
join request=0, link=-2, elapsed_ms=22; final init=0/join=-2/dhcp=-5.
The configured AP was still not found; association and HTTP POST did not pass.
This does not prove a password error, hardware fault or course-port defect.

Added only tests/wifi_sdk_scan/main.c and CMakeLists.txt as an independent
single-core Pico SDK control. It uses the same pinned SDK 2.2.0, CYW43 driver,
TinyUSB, configured SSID and worldwide country; poll servicing is provided by
the standard SDK rather than the course port. No RTOS, UART, join, HTTP, buttons
or LED patterns. It waits for USB monitoring, runs three scans, reports target
matches/channel/RSSI/auth plus channel record counts, and prints no SSIDs/MACs
or passwords. It does not validate credentials or replace the final SMP design.

Build PASS using native Arm GCC 14.2.1 and installed Pico assembler/picotool,
with -Wall -Wextra; an initial attempt to build pioasm from source failed because
the host C++ runtime libraries were absent, resolved by selecting installed
pioasm_DIR. Build/static evidence: evidence/wifi-sdk-scan-build.json. Existing
HTTP/UART UF2 hashes and frozen UART baseline verified unchanged. Sensor and
shared source were not modified. Physical scan control is PENDING user testing.

From repository root after putting CMake/Ninja/Arm GCC on PATH and setting
PICO_SDK_PATH (2.2.0) and PICO_TOOLCHAIN_PATH:

```powershell
cmake -S wifi_pico/tests/wifi_sdk_scan -B wifi_pico/build-wifi-sdk-scan -G Ninja -DPICO_BOARD=pico_w -DCMAKE_BUILD_TYPE=Release
cmake --build wifi_pico/build-wifi-sdk-scan --parallel 4
```

If needed, pass -Dpioasm_DIR and -Dpicotool_DIR to the installed Pico tool
package directories; the desktop's paths are external dependencies, not source.
Flash only wifi_pico/build-wifi-sdk-scan/wifi_pico_sdk_scan.uf2 to the Wi-Fi Pico.
Keep the hotspot/phone client active; open USB 115200 with DTR and send output
through Finished. A match suggests investigating differences in the course-port
path; continued misses mean that port alone cannot explain the problem. Neither
outcome proves a specific root cause without the next controlled comparison.

### Standard SDK scan control - user-observed physical result, 2026-10-25

User flashed wifi_pico_sdk_scan.uf2 to the Wi-Fi Pico. USB enumerated as COM4.
Banner identified Pico SDK 2.2.0, RP2040, single core, NO RTOS; init=0 and all
three scan starts=0. Reported results:

| Round | Scan records | Target matches | Matching target metadata |
| --- | --- | --- | --- |
| 1 | 24 | 0 | None |
| 2 | 23 | 3 | Channel 9, RSSI -31 dBm, auth=5 |
| 3 | 23 | 0 | None |

Finished: matched_rounds=1/3; no connection attempted. These are scan records,
not unique AP counts. This establishes that the standard SDK can detect the
configured SSID, intermittently in this capture. It does not establish password
validity, association, DHCP, HTTP POST, or physical SMP operation for this image.
The course port cannot be the sole explanation for missed scans, since misses
also occurred outside it; course-specific differences remain to be investigated.

User also reports the phone can no longer connect to the laptop hotspot. Their
screenshot shows hotspot On, 2.4 GHz, automatic no-client power saving Off, and
zero clients. Combined observations justify checking hotspot/client stability
before changing firmware; they do not identify a Windows-driver or radio fault.
The scan target's channel 9 differs from the earlier Windows channel-11 readback;
these were at different times and must not be treated as simultaneous evidence.

Next physical step: toggle laptop hotspot Off, wait 10 seconds, turn On, then
retry phone connection only and report whether it remains connected. No firmware,
credentials, Sensor, shared protocol or UART baseline source changed in this step.


### Scan repeat with phone connected; bounded SMP join retries - 2026-10-25

User clarified the phone CAN connect to the laptop hotspot. A further user-run
standard SDK control found: round 1 results=6, matches=2, channel=9, RSSI=-34,
auth=5; round 2 results=6, matches=0; round 3 results=5, matches=0. Summary:
matched_rounds=1/3; no connection attempted. Thus configured-SSID detection is
possible but intermittent in these scans; this is not association/DHCP or HTTP
evidence. The earlier phone connection failure was not persistent. No specific
hotspot, hardware or course-port defect has been proven.

The existing HTTP application stopped after a single terminal join failure.
Added up to three startup scan/join attempts with non-blocking two-second gaps.
The course's 30-second join timeout, credentials, auth/country, HTTP retry policy
and binary body/receiver are unchanged. Retries only follow failed association
with no live link or active HTTP request. They do not implement general recovery
after a successful join, DHCP timeout recovery, or an unlimited background loop.
A failed disassociate/scan-start API stops the sequence and reports setup failure.

Changed tests/firmware/http_test.c (retry request and per-attempt diagnostics),
http_test_main.c (identifying startup line), added network_retry.h and focused
host test test_network_retry.c. The existing run_http_host_tests.py now runs that
test too. The HTTP-only hook in course cyw43_utk.c returns the retry request and
restarts scanning inside the SAME locked radio-owner task; UI/core 1 performs
no network work. Course commit remains 2e8e6ad480c2d84a369b12995694b9bf5e9ddfb3.
ORIGIN.json and patches/http-network-retries.patch record the local change;
existing README/DEPENDENCIES and verify_http_build.py describe/check the bounds.
No new Markdown file was created. No commit/push was performed.

Validation: retry policy tests PASS (spacing, exact attempt cap, in-flight/success
cancellation, clock wrap); existing 10 HTTP-client host test groups and 8 PC
receiver/host TCP tests PASS under ASan/UBSan. Clean send-enabled SMP HTTP build
and static checks PASS (SMP=1, 2 cores, USB, radio ownership, no UART/Sensor link).
Protected Sensor/shared/UART files and existing normal UART UF2 checked unchanged
against before-change hashes. UART firmware/regressions were not rebuilt/rerun
for this HTTP-only step. Physical connection retry behavior and HTTP POST remain
PENDING user observation. Updated UF2: 667136 bytes; SHA-256 dd6c8e14b9770d7050037136ff79e7033f296abde4acb7c290c9c7aa20222d45.

Build from repository root: .\wifi_pico\build.ps1 -Target http -Clean
UF2: wifi_pico/course/build_make/wifi_pico_http_known_bytes.uf2
Next physical test: flash Wi-Fi Pico only, keep laptop hotspot/phone connected,
open USB 115200/DTR and capture until Connected/IPv4 or attempts exhausted.
Do not press GP21 yet; the PC receiver and physical POST are the following step.

### SMP hotspot connection success; first physical POST timeout - 2026-10-25

User flashed the updated SMP HTTP known-byte image and reported:
- Attempt 1: scan records=21, target matches=0; join request=0, link=-2,
  elapsed=939 ms; automatic two-second retry announced.
- Attempt 2: scan records=15, target matches=2; target channel=9, RSSI=-32,
  auth=5; join request=0, link=1, elapsed=3200 ms.
- Connected; Pico IPv4=192.168.137.69; GP21 ready message printed.
- GP21: POST /logs, length=1024, fixed test ID; three HTTP attempts followed by
  FAILED: connect timeout (HTTP=0 attempts=3).

This is user-observed physical evidence that startup scan/join retry recovered
and the SMP HTTP image associated and acquired an IPv4 address on this hotspot.
The one-time task reports also identify UI on RP2040 core 1 and Wi-Fi/HTTP owner
on core 0. This does NOT prove HTTP receipt, byte equality or PC storage.
HTTP=0 here means no HTTP response was received, not an HTTP server status code.

Read-only PC checks after this capture: hotspot adapter is 192.168.137.1;
both local and generated firmware destination macros are 192.168.137.1:8000.
Get-NetTCPConnection found zero listening sockets on port 8000. This is the
first concrete PC-side issue to address; firewall/network reachability are not
therefore established. No firmware/source/configuration was changed in this step.

Next: run the existing PC receiver bound specifically to 192.168.137.1:8000,
with --expect-known, and confirm its Listening line before the next GP21 press.
The previous failed POST can be retried with a fresh GP21 press without reflashing.
Physical HTTP upload remains PENDING until a receiver receipt and exact-byte
comparison are captured.


### First physical known-byte HTTP POST PASS - user observation, 2026-10-25

With the existing PC receiver listening on 192.168.137.1:8000 and --expect-known,
the user pressed GP21 again without reflashing/restarting the Wi-Fi Pico. This
followed the earlier bounded connect-timeout failure, so a fresh button command
recovered once the receiver was available. User-observed Pico output:

```text
[UI] GP21 pressed
[HTTP] POST /logs: length=1024 ID=pico-known-1024-v1
[HTTP] Attempt 1/3
[HTTP] SUCCESS: receiver confirmed storage (HTTP=201 attempts=1)
```

User-observed PC receiver output:

```text
[PC] stored: ID=pico-known-1024-v1 bytes=1024 sha256=785b0751fc2c53dc14a4ce3d800e69ef9ce1009eb327ccf458afe09c242c26c9 known-byte comparison=PASS
```

Read-only verification of http_received found exactly one .bin file, length
1024. Its bytes equal bytes(range(256))*4, and its SHA-256 matches the receiver
line above. Filename is SHA256(test ID).bin, as designed. This is a successful
physical Pico-to-PC known-byte POST with exact PC-storage verification, separate
from build and host-test evidence. The existing SMP HTTP target was used; UART0
was free and the Sensor endpoint was not involved. No GP6 observation was
provided for this POST, so no physical LED result is inferred here.

Physical duplicate handling, HTTP response loss/retry, later network disconnect
recovery, combined UART-payload upload and UART responsiveness during upload are
not proven by this successful request. Next: keep the receiver/Pico running,
press GP21 once more, and expect HTTP 200/duplicate with the same single file.
No source/firmware changes were made for this observation. The existing Wi-Fi
README status was updated; no new Markdown file, commit or push was created.


### Physical repeated-POST duplicate protection PASS - 2026-10-25

User pressed GP21 again with the same receiver, file and fixed test ID.
Pico: SUCCESS: receiver confirmed storage (HTTP=200 attempts=1).
PC: duplicate: ID=pico-known-1024-v1 bytes=1024
sha256=785b0751fc2c53dc14a4ce3d800e69ef9ce1009eb327ccf458afe09c242c26c9
known-byte comparison=PASS.

Read-only PC verification confirms exactly one .bin file remains, containing
exactly 1024 known bytes with the same SHA-256. This is a user-observed physical
repeat-request duplicate test plus independent file verification. It does not
yet establish physical behavior when a stored request loses its HTTP reply.
No Pico firmware was changed. The earlier missing-receiver three-connect timeout
and fresh-button recovery already provide bounded connection-failure evidence.

Prepared the next controlled test by exposing the receiver's existing
one-shot drop_next_receipt hook as optional --drop-first-receipt. The receiver
prints an explicit test banner and drop event. It still stores/compares bytes
and closes without a reply only once; normal behavior is unchanged without the
flag. Existing files are retained. Restarting the receiver with this option and
pressing GP21 once should give automatic retry, HTTP 200 attempts=2 and one file.
Physical lost-reply behavior remains PENDING until the user supplies output.

Validation for this PC-only change: retry-policy host tests, 10 HTTP-client test
groups and 8 receiver/host TCP tests PASS, including lost reply and exact bytes.
The --help command recognises the new switch. Existing HTTP/UART UF2 hashes
match their prior build records. Only tools/http_receiver.py, the existing Wi-Fi
README/results and host-test evidence were updated in this step. No new Markdown
file, firmware rebuild, commit or push was made.

### Lost-reply test blocked by Wi-Fi startup failure - 2026-10-25

User started the receiver with --drop-first-receipt; its Listening, fixture and
one-shot test-armed banners appeared. The provided Pico capture is a new SMP
HTTP startup, not a continuation of the earlier connected session:
- Attempt 1: scan records=28, matches=0; join request=0, link=-2, elapsed=22 ms.
- Attempt 2: scan records=24, matches=1; channel=9, RSSI=-28, auth=5;
  join request=0, link=-2, elapsed=22 ms.
- Attempt 3: scan records=18, matches=0; join request=0, link=-2, elapsed=22 ms.
- Connection attempts exhausted printed after exactly three attempts.

No Connected/IPv4, POST, dropped-receipt or HTTP success/failure output was
provided for this run. Thus the physical lost-HTTP-reply test was NOT EXECUTED;
the receiver remained armed. Earlier physical 201 storage/exact-byte and 200
repeat-request duplicate PASS results remain valid. This capture establishes
bounded startup retry exhaustion on this run; target detection in attempt 2 did
not result in association. It does not prove a password problem or a specific
hotspot/driver defect. Intermittent association remains unresolved.

Next user action: keep the receiver terminal open and the laptop hotspot/phone
client active, reset only the Wi-Fi Pico once and wait for Connected and READY.
Only then press GP21 once. If startup attempts exhaust again, report that output
rather than repeatedly resetting. No firmware, credentials or source code was
changed for this observation; only this existing test record was appended.


### Second startup exhaustion; HTTP-only NONET deadline correction - 2026-10-25

User reset once and observed another SMP HTTP connection failure: scan record
counts 18,23,17 with zero configured-SSID matches in all three attempts. Each
join request returned 0, then link=-2 at elapsed=22 ms. Three-attempt exhaustion
printed. GP21 was rejected as Wi-Fi unavailable; the PC receiver remained
listening with --drop-first-receipt armed. No POST or lost-reply test occurred.
Earlier physical 201/exact bytes and 200/duplicate PASS results remain valid.

Read-only Windows check: hotspot On, 2.4 GHz, WPA2, client count=1; SSID/password
match local settings. No secret values were logged. The earlier suggestion to
compare an SDK join prompted source inspection, which revealed a concrete policy
difference before another diagnostic firmware was needed: Pico SDK 2.2.0
src/rp2_common/pico_cyw43_arch/cyw43_arch.c resubmits CYW43_LINK_NONET until its
original connect deadline. Our course-port handler ended an attempt immediately
on any negative link status. Thus a nominal 30-second attempt could finish at
22 ms; the earlier outer three retries did not correct this inner early exit.
This difference is not proof of why the hotspot is intermittently missed.

Applied only to TM_APP_HTTP: NONET may resubmit the configured join at most once
per second within the ORIGINAL 30-second deadline. The timer is never restarted
by a resubmission; after expiry, the existing bounded outer scan/join retry
policy applies (three attempts, two-second gaps). Success and authentication/
other terminal errors keep their existing handling. No blocking SDK connect
loop is linked; the same course radio owner continues poll/HTTP/UI IPC service.
Join request API failures stop the current attempt. DHCP recovery and reconnect
after a successful join remain outside this change.

Changed existing network_retry.h and its host test for pacing/deadline policy;
course cyw43_utk.c for the HTTP-only branch; http_test.c/main for one-time NONET
notice, revised unavailable message and new build marker; verify_http_build.py
for effective timing checks. ORIGIN.json plus patches/http-nonet-window.patch
record the local course change. The pinned course commit remains unchanged.
Existing README/DEPENDENCIES were updated; no new Markdown file was added.
Sensor, shared UART, baseline and HTTP upload/receiver implementations were not
modified. No commit/push was made.

Validation: policy host tests PASS, including early NONET, one-second minimum
spacing, exactly 29 resubmissions before a 30-second deadline, no deadline
extension, and clock wrap. Existing 10 HTTP-client groups and 8 PC receiver/
host TCP tests PASS under ASan/UBSan. Incremental SMP HTTP build/static checks
PASS (SMP=1, two cores, USB, radio owner, original course pin/baseline checks).
Protected Sensor/shared/UART files and existing UART UF2 were checked unchanged.
UART regressions were not rerun for this HTTP-only change. No physical validation
of the new NONET handling is claimed; stable association and the lost-reply test
remain pending. UF2 bytes=667648; SHA-256=23b953336f1b2a8731048e573ba1830cce7931cea8c545339061cec001bc55b3.

Next: flash only wifi_pico/course/build_make/wifi_pico_http_known_bytes.uf2.
Keep the existing one-shot PC receiver and hotspot running. Confirm startup
WIFI_NONET_RETRY_MS=1000 JOIN_WINDOW_MS=30000, and capture through Connected/READY
or exhaustion before attempting GP21 again. No new standard-SDK join firmware
was added because the source comparison identified the early-exit difference.

## 2026-10-25 — combined UART / HTTP application (physical test pending)

User-observed standalone network evidence: latest known-byte firmware reported
WIFI_NONET_RETRY_MS=1000, JOIN_WINDOW_MS=30000, then connected on outer attempt 1
after 8280 ms with Pico address 192.168.137.69. This confirms that run connected;
it does not establish stable association or count internal join resubmissions.

Implemented a separate `build.ps1 -Target combined` image. Four application
roles: Communication core 0 priority 4, existing course radio/HTTP owner core 0
priority 6, UI/status core 1 priorities 8/9. USB infrastructure stays priority 3.
Sensor Emulator disabled; UART0 GP0/GP1 115200 8N1; USB diagnostics. Course source
pin unchanged. No shared wire-format, retry or Sensor-side change.

GP21 first retrieves, second uploads a protected completed copy. Failure keeps
the copy/ID; success makes GP21 retrieve again. Busy/stale presses cannot cause
delayed uploads or CLEAR. CONFIG is refused while a saved log waits. CLEAR only
discards that copy on Sensor ACK, works on an empty log, and never deletes PC
files. No automatic Sensor CLEAR. A SHA-256 content ID deduplicates identical
snapshots across retries/reboots; there is no separate file for identical data
retrieved twice. Capacity remains 1024 bytes; no decoding on the Wi-Fi Pico.

Files added: combined_main.c, log_workflow.c, log_id.c/.h, upload.c/.h,
config/combined.mk; tests/test_combined.c, test_upload.c, test_log_id.c and
run_combined_host_tests.py; patches/combined-application.patch. Existing common
app/kernel/communication/status declarations and HTTP client/owner glue gained
the opt-in combined path. Build script/verifier and existing host tests were
extended; existing READMEs/dependency documentation/evidence updated. No new
Markdown file, credentials, commit or push. Sensor/shared/UART baseline source
and tests are protected by before/after hashes.

Host results: combined workflow PASS, including receive inactivity after
REQUEST ACK, fresh recovery, oversized retrieval, failed CLEAR, repeated empty
CLEAR, zero-length log, lost final ACK/one completion, all buttons busy while
uploading, stale queued press, and RX fault leaving upload bytes immutable.
Radio handoff unit PASS (offline failure, exact pointer/length/ID, result once).
SHA-256 IDs match Python for every length 0..1024. Actual host UART reassembly
produced 300 bytes; those bytes passed through the actual HTTP client over host
TCP to the actual receiver with exact equality. First receipt was dropped after
storage: retry returned HTTP 200, two attempts, one file; another transaction
returned 200 in one attempt and still one file. This staged host test is NOT a
physical Pico/kernel/ISR test. Details: evidence/combined-host-tests.log.

Existing host regressions PASS: 7 SMP UART application cases, 10 two-board
profile cases with binary comparisons; network retry policy; 11 HTTP client
groups and 8 Python receiver/host TCP tests. Frozen baseline host codec checks
and all 8 reliability groups PASS. The five original RP2040 emulator modes were
not rerun in this turn; original source/tests remain unchanged.

Combined ARM build/static checks PASS: Pico W RP2040, actual SMP=1, two cores,
USB console, UART and HTTP linked, Sensor endpoint absent, same pinned course
source and SDK. Size: text=341812, data=2480, bss=77800. Output/effective settings
and SHA-256: evidence/combined-build.json. Standalone regression build results
are recorded separately below after completion.

Physical combined flow, UART responsiveness during real RF upload, real AHT10
through split Sensor UART, and lost-HTTP-receipt recovery remain PENDING. The
unchanged sensor_pico/main.c samples/encodes/flushes/dumps via its local buttons
but has no UART command handling. Do not equate the reference 100-byte RAM
Sensor tests with real temperature/flash integration. JZ must connect his
application to the agreed UART interface before that full physical test.

Next physical step: flash only wifi_pico_combined.uf2 to Wi-Fi Pico and capture
startup through Connected or exhaustion. Keep the existing RAM Sensor UART test
firmware and wiring. Then start the PC receiver without --expect-known, CONFIG,
retrieve, and separately press GP21 to upload. Compare PC binary with the Wi-Fi
binary capture before proceeding to fault cases. Follow up one step at a time.

Final regression builds: standalone known-byte HTTP PASS (text 331544, data
2472, bss 67128); UART two-board clean build/static checks PASS (text 67322,
data 44, bss 42804). Combined UF2 remains 688640 bytes. No compiler warnings
were found in these three build logs. All 125 protected Sensor/shared/baseline
files matched the pre-change SHA-256 snapshot. Credentials and UF2s remain
ignored; no files staged, committed or pushed. Physical combined tests pending.

## 2026-10-25 — user-observed combined firmware startup PASS

User flashed wifi_pico_combined.uf2 and captured COM5 USB output. The firmware
reported SensorEmulator=DISABLED, application_tasks=4, HTTP=COMPRESSED_BYTES,
SMP=1, cores=2, UART0=PROTOCOL, UART_FAULT=0, DIAGNOSTICS=0, WIFI=cyw43, and
course commit 2e8e6ad480c2d84a369b12995694b9bf5e9ddfb3.

Physical task diagnostics reported UI tk_prc=2/rp2040_core=1, Communication
tk_prc=1/rp2040_core=0 and Wi-Fi/HTTP owner tk_prc=1/rp2040_core=0.
Initial UART counters were zero for IRQs, received bytes, dropped bytes, faults
and notification errors. This is startup evidence, not a UART traffic test.

Wi-Fi output: scan results=18, target_matches=0; subsequent join request=0,
link=1, elapsed_ms=8220; Connected, Pico IPv4=192.168.137.69; HTTP available.
Association succeeded despite no match in the preceding diagnostic scan.

Combined physical startup, observed core placement and Wi-Fi/DHCP: PASS.
Combined CONFIG/retrieval/HTTP upload, exact PC binary comparison, concurrent
UART/network behavior and real AHT10 integration: still PENDING.
Next action: press GP20 once and capture CONFIG ACK plus observe GP6.

## 2026-10-25 — combined physical CONFIG attempt FAILED, cause pending

User pressed GP20 three times in separate attempts. Each printed Sending CONFIG,
three timeout/retransmitted messages, retry limit reached, CONFIG failed,
received_log_valid=0, received_bytes=0, completion_count=0. Command sequences
1, 2 and 3 each reported retries=3/limit=3. User observed three short GP6 flashes
after failure. The application accepted another CONFIG attempt without reboot.

This confirms observed retry exhaustion/failure indication, not successful UART
communication or an intentionally injected fault test. No valid CONFIG ACK was
observed. Combined retrieval and HTTP upload remain pending. USB COM5 and COM6
were present during read-only diagnosis; Sensor power alone does not identify
its flashed firmware. Source check confirms matching session 0x11223344,
115200 8N1, Wi-Fi GP0 TX/GP1 RX and reference Sensor GP4 TX/GP5 RX. No firmware
change or wiring cause has been established. Next check: COM6 Sensor USB banner
to identify the active endpoint before reflashing or changing protocol code.

Follow-up: user opened Sensor COM6 and reported no output. Active Sensor
firmware remains unidentified; no wiring or combined-firmware cause established.
Verified reference image exists at uart_protocol/build/sensor_pico_protocol_test.uf2.
Next controlled step is to flash that existing RAM protocol endpoint to Sensor
and verify its USB readiness banner before another CONFIG attempt.

After the user-reported reference Sensor flash, COM6 remained blank. Reopening
COM5 printed only its READY lines; this matches the combined application's USB
reconnect announcement and does not itself indicate reboot. Static inspection
of the Sensor ELF confirmed main, pico1_init, stdio_usb_init, stdio_usb_connected
and TinyUSB CDC symbols; UF2 contains the Sensor readiness text. No new Sensor
firmware or source change was made. Next: retry CONFIG once after this Sensor
flash with COM6 closed, using COM5 ACK/failure output to test UART independently
of the unresolved Sensor console issue.

Further user test with COM6 closed: combined CONFIG sequence=4 still exhausted
three retries, valid=0, bytes=0, completions=0. User confirmed hotspot still
lists utk3-picow at 192.168.137.69. Network association is working; this does
not establish the separate UART path. Static checks of Sensor startup/handler,
USB symbols and Wi-Fi UART pins/session found no confirmed cause. No protocol
or firmware source was changed in this diagnostic turn.

Next controlled comparison: temporarily flash the existing normal UART-only
wifi_pico_uart_two_board.uf2 to Wi-Fi Pico; keep the reference Sensor firmware,
power and wiring identical; press GP20 once. This comparison distinguishes a
combined-application regression from a failure also present without networking.
The comparison is pending user execution, not a passed test.

## 2026-10-25 — UART-only comparison also FAILED

User flashed wifi_pico_uart_two_board.uf2. Startup confirmed application_tasks=3,
HTTP=PENDING, WIFI=none, UART0=PROTOCOL, UART_FAULT=0, DIAGNOSTICS=0,
SensorEmulator=DISABLED and the same course commit. On GP20, CONFIG sequence=1
printed three retransmissions and retry exhaustion; valid=0, bytes=0,
completion_count=0, retries=3. No CONFIG ACK was observed.

The missing ACK is reproducible without networking. This does not prove a
physical wiring fault or identify the Sensor's execution state; it does mean
there is no evidence that HTTP is a necessary cause. Sensor USB output remains
unresolved. No further firmware/source changes were made. Next physical check:
with both boards powered off, verify Wi-Fi GP0 (pin 1) -> Sensor GP5 (pin 7),
Sensor GP4 (pin 6) -> Wi-Fi GP1 (pin 2), and common GND (e.g. pin 3 on both).
Keep the two USB power supplies separate; no power-rail jumper is needed.

## 2026-10-25 — direct Sensor USB check still silent; clean reference build

User confirmed direct PowerShell check opened COM6 with DTR enabled, then
printed No USB text received. This rules out the VS Code display alone as a
sufficient explanation; Sensor application execution is still unconfirmed.

Built the existing sensor_pico_protocol_test source in an isolated ignored
wifi_pico/course/build_make/sensor_reference_release directory, SDK 2.2.0,
pico_w RP2040, Release, USB enabled, TinyUSB debug=0. The previously flashed
artifact was Debug with TinyUSB debug=1; no evidence establishes that difference
as the fault. No application/protocol/source change was made, and the old UF2
was retained. All 125 protected source/reference files still match their hashes.

Build/static PASS: UF2 family and all payload bytes match the generated binary;
expected Sensor banner present. Distinct copy for physical check:
wifi_pico/course/build_make/sensor_reference_release.uf2, 70656 bytes,
SHA256 8d7163f0fe346051c662e41fa377372eca21e80e0f38e8c1266169ed8a7652ea.
Evidence: evidence/sensor-reference-release-build.json. Physical result PENDING.

Next controlled setup: power off both boards, disconnect all Sensor jumpers
(UART and AHT10), leave Wi-Fi board unplugged, flash clean reference to Sensor,
and inspect its USB output with Sensor as the only connected Pico. This checks
standalone Sensor startup and port identity before restoring the integration.
No cause/fix success is claimed until the user captures the result.

## 2026-10-26 — refactor and Sensor compatibility review

User asked whether refactoring or Sensor incompatibility explains the physical
failure. UART-only reproduction excludes networking as a necessary cause; it
does not exclude a regression in code shared by both Wi-Fi profiles.

Confirmed current sensor_pico application builds main/aht10/encoder/flash_log
only, handles local sample/flush/dump buttons and has no protocol UART handler.
That application cannot answer Wi-Fi CONFIG in its present form. It is distinct
from uart_protocol's sensor_pico_protocol_test target, which uses UART1 GP4/GP5,
USB and a 100-byte RAM fixture. The real temperature byte format is not assumed
to match the old fixture; Wi-Fi treats retrieved payloads as opaque bytes.

Additional host cross-implementation check PASS, ASan/UBSan and warnings enabled:
actual current shared Pico 2 sender emitted CONFIG; actual unchanged baseline
Pico 1 handler decoded it, accepted timestamp=1700000000 and interval=3600,
ACKed it and repeated CONFIG; current shared Pico 2 accepted that ACK. The two
programs compiled against their own respective headers/link implementations.
Evidence: evidence/config-reference-compat.json and .log. This checks actual
CONFIG/frame/ACK compatibility, not physical boot, UART registers, ISR handling,
USB operation or end-to-end temperature acquisition. No Sensor/protocol source
was changed. Refactor runtime regression remains possible; no specific code
fault has been established. Isolated Sensor clean-build USB test remains pending
until the user reports its result.

## 2026-10-26 — isolated Sensor still silent; editor includes verified

User reports clean reference flashed with no Sensor jumper wires: COM6 appears
but opening it prints only the monitor's port-open message. Isolated reference
Sensor USB startup remains FAILED/unexplained. No physical CONFIG success or
combined upload is established.

User also reports red editor errors for hardware/flash.h and pico/flash.h in
sensor_pico/flash_log.c. Both headers exist in installed SDK 2.2.0; the original
local editor configuration selected only uart_protocol. Configured unchanged
Sensor CMake in ignored course/build_make/sensor_editor with SDK 2.2.0,
Release and ERASE_LOG_ON_BOOT=OFF, exported compile commands, and compiled the
flash_log.c object successfully. Both required SDK include directories are in
that command. No Sensor application was flashed or changed by this check.

Updated only ignored .vscode/settings.json to use generated Sensor and UART
compile-command databases, keeping UART as the active CMake source directory.
No global VS Code setting or tracked source was changed. A separate local
configuration backup is in the system temporary folder. Editor refresh remains
user-observed, not automatically claimed. flash_log.c is not linked into the
flashed sensor_pico_protocol_test/reference image, so these editor diagnostics
cannot explain that image's missing output.

Read-only Windows inventory with only Sensor connected lists COM6 as Pico USB
VID_2E8A/PID_000A. This establishes USB enumeration, not entry to the application
loop. Next evidence needed: Sensor BOOTSEL INFO_UF2.TXT contents and whether COM6
disappears in BOOTSEL, to verify board identification before another image.

## 2026-10-26 — Sensor bootloader and COM mapping confirmed

User read Sensor INFO_UF2.TXT: UF2 Bootloader v3.0; Model: Raspberry Pi RP2;
Board-ID: RPI-RP2. User confirms COM6 disappears in Sensor BOOTSEL mode.
This establishes functioning bootloader and COM6 association with the board;
it does not prove that the reference application reaches its main loop.

Next comparison uses the existing independently tested bare-metal SDK scan
image solely for its early USB banner. Its first messages follow USB connection
and precede cyw43_arch_init; it does not initialize protocol UART, Sensor code
or HTTP. Existing image hash matches evidence/wifi-sdk-scan-build.json:
51e5dca62d32d3c6f3717a2b29150d8f16550c7cb89a455ff86d9cf5b95d1943.
Scan/network results are irrelevant to this USB-only pass criterion. Physical
execution on the Sensor board is PENDING. No new firmware source was created.

## 2026-10-26 — Sensor physical USB control PASS; reference emulator PASS

User flashed the existing SDK scan control to the isolated physical Sensor
board and captured its initial SDK 2.2.0, Pico W/RP2040, single-core/NO RTOS
banner over COM6. CYW43 init=0; all three scan rounds finished (31/29/27 results,
zero target matches), ending with no connection attempted. Physical USB output
and execution of this SDK control on the Sensor: PASS. This does not establish
UART1 operation, Sensor protocol startup, or a hotspot connection. The earlier
Sensor reference's missing output remains a distinct failure.

Executed the exact clean sensor_pico_protocol_test.bin under local RP2040JS,
with a simulated USB host asserting DTR after 700 ms and the current Wi-Fi
CONFIG frame fed into UART1 at 2 seconds. Five simulated seconds completed.
Observed actual binary entries to main, stdio/USB init, UART init/format,
pico1_init and the main poll loop. USB printed Sensor Pico protocol test ready
and Logger length: 100 bytes; UART1 emitted ACK hex
7e010500443322110100020001002ec67e. Emulator PASS. Evidence:
evidence/sensor-reference-startup-emulator.json. Models do not prove physical
UART electrical behavior, startup timing, RX noise or reset/clock behavior.

The next proposed diagnostic is a separate temporary Sensor startup test,
reusing unchanged protocol code, with before/after USB/UART1/format/context
checkpoints plus bounded RX counters. It would not modify sensor_pico or any
verified baseline source. Explicit user approval has been requested because
of the earlier prohibition on adding Sensor-side code. Diagnostic image is
NOT yet implemented; approval pending. No commit or push made.

## 2026-10-26 — approved removable Sensor startup probe built

User explicitly approved a separate temporary Sensor diagnostic and requested
it be deletable after the test. Added only wifi_pico/tests/sensor_startup_probe/
(CMakeLists.txt, main.c, build.ps1, test_firmware.mjs). Its build/ subfolder is
ignored. No production CMake/build script references the folder; deleting the
entire folder after diagnosis removes its code and outputs. No new Markdown
file added. JZ's sensor_pico and all 125 protected source files are unchanged.

The bare-metal RP2040 probe uses SDK 2.2.0, USB only stdio, UART1 GP4 TX/GP5 RX,
115200 8N1, session 0x11223344, and baseline Pico 1/framing/CRC/reliability plus
the existing 100-byte fixture. It waits for USB then prints before/after UART
initialization, GPIO setup, UART format, protocol context initialization and
entry to its main loop. Every two seconds reports RX counts/errors/decoded and
rejected frames, GPIO RX level and UART registers. Complete valid frames pass
to the original Pico 1 handler; the probe builds no protocol frames itself.

Diagnostic differences are deliberate and confined to this target: contexts
are static, startup waits for an open USB terminal, RX draining is bounded to
64 bytes per poll, errored UART characters are counted/dropped, and extra USB
logging precedes dispatch. No RX pull-up is added (original pull state observed).
It is not a production fix or a reliability timing acceptance test; if it works
physically, the original reference's root cause is not thereby established.

Build PASS; own diagnostic main uses -Wall/-Wextra/-Werror while SDK warnings
remain nonfatal. UF2 RP2040 family and exact binary payload comparison PASS.
Local RP2040 emulator PASS for the actual diagnostic binary: all checkpoints,
late USB open, CONFIG dispatch, configured=1 and exact baseline ACK bytes.
Source/UF2 hashes and result are inside the removable folder's build/validation.json;
emulator text is build/emulator.log. No physical test performed automatically.

Build from root: .\wifi_pico\tests\sensor_startup_probe\build.ps1
Flash file: wifi_pico/tests/sensor_startup_probe/build/sensor_startup_probe.uf2.
Next user step: Sensor USB only, Wi-Fi unplugged, all Sensor jumpers removed;
flash this probe and capture all USB output through repeated ALIVE or its last
checkpoint. Physical result PENDING. No commit or push made.

## 2026-10-26 — physical Sensor startup probe PASS, isolated USB only

User flashed sensor_startup_probe.uf2 on Sensor with all jumper wires removed.
COM6 printed all checkpoints: USB ready; SDK 2.2.0/bare-metal/baseline fixture;
stdio_init_all=1; clk_peri=125000000; UART1 init complete actual_baud=115207;
GP4 TX/GP5 RX setup; UART format complete; protocol initialization complete;
main loop READY. ALIVE continued every 2000 ms from 21453 to 49453 ms.
No blocked initialization call or repeated reboot is apparent in this capture.

Unconnected GP5 reported LOW. Counters remained rx_bytes=1, errors=1, frames=0,
rejected=0, dispatches=0 throughout. One errored character on an unconnected LOW
UART input is consistent with that test setup; the specific error bits were not
logged, so no particular UART error type is asserted. It is not evidence of
corrupted protocol traffic or a framing incompatibility. UART registers stable:
CR=00000301, FR=00000197, IBRD=67, FBRD=52. The reported 115207 baud is normal
integer divider rounding of the requested 115200 at 125 MHz.

This is physical probe startup/UART configuration/main-loop evidence only.
It does not establish GPIO-to-GPIO UART traffic, CONFIG ACK, reference firmware
root cause, physical retrieval or HTTP. The probe differs from the reference in
USB-before-UART timing, static contexts and bounded/error-aware RX draining;
its success does not identify which difference matters. No production fix is
claimed. No firmware changes made following this observation.

Next user test: power off both boards, reconnect Wi-Fi GP0 TX -> Sensor GP5 RX,
Sensor GP4 TX -> Wi-Fi GP1 RX, common GND; keep AHT10 disconnected. Keep Sensor
probe and Wi-Fi UART-only firmware (no reflash). Open both USB monitors, wait
for Wi-Fi Communication ready, observe Sensor RX idle HIGH, press Wi-Fi GP20
once, and capture both monitors. Actual CONFIG/ACK result remains pending.

## 2026-10-26 — physical CONFIG with Sensor probe and UART-only Wi-Fi PASS

User reports their monitor UI closes one COM port when opening the other;
concurrent USB capture was unavailable. Both applications had passed their
initial USB/startup gates. Sequential monitoring is valid: these targets keep
processing protocol after USB disconnect; messages emitted while closed may
be absent from the capture, but counters and protocol state remain.

Sensor capture: all startup checkpoints complete; GP5 idle HIGH; initially
rx_bytes=1/errors=1/frames=0. Received CONFIG type=02, sequence=1, payload=8,
session=11223344; dispatch returned configured=1/next_tx_sequence=2. Counters
then rx_bytes=24/errors=1/frames=1/rejected=0/dispatches=1. Exactly 23 additional
bytes formed a valid CONFIG; no new receive error was observed.

Wi-Fi UART-only capture: GP20 -> Sending CONFIG -> CONFIG acknowledged, sequence=2,
retries=0/limit=3. The two snippets have different sequence numbers and are not
claimed as a byte-correlated capture of one single transaction. Together they
establish physical command reception in the probe and successful CONFIG/ACK
operation for the current pairing. GP6 observation not supplied in this report.

Result: physical CONFIG with temporary Sensor probe + UART-only SMP Wi-Fi PASS.
No cause for the original reference's silence is established: probe changes
USB startup order, static contexts and diagnostic RX handling. Keep that
reference failure unresolved rather than labeling the probe as a production
fix. No source or firmware change made in this observation turn.

Next user step: on Wi-Fi COM5 press GP21 once, with current UART-only image and
Sensor probe. Expect 100 bytes/13 blocks/valid completion and no retry. This
step retrieves only; HTTP is absent in the currently flashed Wi-Fi image.
Full retrieval and combined HTTP physical result remain pending.

## 2026-10-26 — physical 100-byte retrieval with Sensor probe PASS

User reports Wi-Fi GP21 retrieval with GP6 two-flash indication. UART-only Wi-Fi
capture: REQUEST_DATA ACK; 13 DATA blocks accepted/ACKed; TRANSFER_END validated;
received_log_valid=1, received_bytes=100, blocks=13, completion_count=2;
command sequence=18, retries=0/limit=3. CRC-16/X-25=6d8c. Compared all 100 bytes
from the pasted BINARY offsets 0000/0020/0040/0060 against the unchanged mock
fixture: exact equality PASS. SHA256
508ce5ef84f333f067394c426c37e537693ab9224d29c117150c294e93cb72d4.
Evidence: evidence/probe-physical-retrieval-comparison.json. This comparison
uses user-observed USB hex, not a direct HTTP-received PC file.

Sequential Sensor capture shows another REQUEST_DATA, sequence=33 (different
from Wi-Fi snippet's 18), followed by 14 ACK frames. RX counts 558 -> 813,
frames/dispatches 32 -> 47, errors stayed 1 and rejected stayed 0; RX idle HIGH.
This supports successful stop-and-wait processing in that later observation,
but the snippets are not claimed as one byte-correlated transaction. Cumulative
completion_count=2 alone is not evidence of duplicate completion; the surrounding
sequence histories indicate other requests occurred outside the snippets.

Physical probe + UART-only Wi-Fi CONFIG and 100-byte retrieval now PASS. Data
is the fixed RAM fixture, not current AHT10 readings. Original reference startup
root cause remains unresolved. HTTP is absent from the current Wi-Fi image.

Next: restore existing wifi_pico_combined.uf2 to Wi-Fi only, keep Sensor probe,
turn on the 2.4 GHz laptop hotspot and capture combined boot/connection. Probe
requires its monitor once per power-up to pass its diagnostic startup gate;
then sequential monitoring is sufficient. PC receiver must later run without
--expect-known before any actual retrieved-buffer upload. Combined physical
upload/PC equality remain pending. No code changes in this observation turn.

## 2026-10-26 — combined firmware restored with Sensor probe; network PASS

User flashed combined Wi-Fi image while retaining the Sensor probe. COM5 reports
4 application tasks, SensorEmulator=DISABLED, HTTP=COMPRESSED_BYTES, SMP=1,
cores=2, UART0=PROTOCOL, UART_FAULT=0, DIAGNOSTICS=0, course commit unchanged.
Communication/radio owner report core 0; UI reports core 1. Initial IRQ/RX/error
counters zero. Network join request=0, link=1, elapsed=3300 ms; connected Pico
address=192.168.137.232. This address differs from yesterday's DHCP lease but
does not change the PC destination.

Read-only PC check confirms hotspot interface Local Area Connection* 2 at
192.168.137.1; local firmware settings use HTTP_PC_ADDRESS=192.168.137.1 and
HTTP_PC_PORT=8000. No listener was present on port 8000 at this check.

Physical combined boot/network with probe setup PASS. Combined UART retrieval
and POST are still pending. Next action is start tools/http_receiver.py bound
to PC 192.168.137.1:8000 without --expect-known or fault injection, retaining
PC output in http_received. The known-byte-only option would reject this
100-byte retrieved payload. No source change, commit or push made.

## 2026-10-26 — combined physical CONFIG/retrieval before POST PASS

User started normal PC receiver on 192.168.137.1:8000/logs with output in
http_received, then pressed Wi-Fi GP20 and GP21 once. Combined Wi-Fi capture:
CONFIG ACK, command sequence=1/retries=0; REQUEST_DATA ACK, 13 DATA ACKs,
TRANSFER_END validated; Saved 100 compressed bytes; valid=1/bytes=100/blocks=13/
completion_count=1; retrieval command sequence=2/retries=0; CRC16-X25=6d8c.
BINARY offsets 0000/0020/0040/0060 repeat exactly the previously verified
100-byte fixture capture (SHA256
508ce5ef84f333f067394c426c37e537693ab9224d29c117150c294e93cb72d4).

This is physical retrieval in the combined UART/radio image with the temporary
Sensor probe, not live AHT10 data. It establishes one completed saved snapshot
before upload. HTTP transmission has not been observed yet. Next user action:
press GP21 once more to upload that saved snapshot; capture Wi-Fi receipt and
PC stored/duplicate output. No CLEAR, source change, commit or push made.

## 2026-10-26 — first physical combined UART -> HTTP -> PC file PASS

User pressed GP21 a second time after a complete retrieval. Wi-Fi output:
Uploading saved compressed log; POST /logs length=100, content ID
log-508ce5ef84f333f067394c426c37e537693ab9224d29c117150c294e93cb72d4;
Attempt 1/3; receiver confirmed storage HTTP=201/attempts=1; Upload complete;
Sensor log retained; GP21 retrieves again. This is user-observed physical
firmware output, not an automated hardware test. Upload LED observation was
not supplied in this capture.

Agent independently read the existing PC output file following that user test:
http_received/f6a67aec3fd78cfd6fa69d39439ca281be21a6166bd71842ee49346599c10a03.bin.
It exists, is 100 bytes, has SHA256
508ce5ef84f333f067394c426c37e537693ab9224d29c117150c294e93cb72d4,
and every byte matches the unchanged Sensor mock fixture and earlier UART
capture. Equality PASS, no extra protocol headers/block indices in the body.
Evidence: evidence/combined-physical-pc-comparison.json. No serial port was
opened or physical hardware action performed by the agent for this comparison.

Milestone: temporary bare-metal Sensor probe RAM fixture -> physical UART ->
Wi-Fi Pico four-role SMP combined application -> HTTP POST -> exact PC binary
storage PASS. Source is a fixed 100-byte fixture, not live AHT10 samples; this
does not validate JZ's Sensor UART/flash integration, temperature decoding/CSV,
concurrent UART errors during networking, or physical lost HTTP receipt recovery.
The earlier reference firmware startup cause remains unconfirmed. Firmware
reports no automatic Sensor clear; fresh retrieval is the next physical check.
Updated existing project summaries to reflect this limited passed milestone.
No firmware source changes, commit or push made in this observation turn.

Next user test: GP21 retrieves again; after completion GP21 uploads again.
Expect 100 retained bytes, same content ID, HTTP 200/duplicate and one stored
file. Capture both final HTTP output and PC duplicate line; do not CLEAR.

## 2026-10-26 — repeat retrieval observed on Sensor; duplicate POST pending

During the requested repeat retrieve/upload test, user supplied Sensor COM6
capture after GP21. Sensor received REQUEST_DATA sequence=17, payload=2,
session=11223344, then 14 ACK frames sequence=18..31. RX bytes 1093 -> 1348
(+255), frames/dispatches 63 -> 78 (+15), rejected=0, errors remained 2 and RX
idle HIGH. These are cumulative counters for incoming command/ACK wire bytes,
not the compressed payload length. The error count had already reached 2
before this capture; the time/cause of its earlier increment is unobserved.
No new error occurred across the shown retrieval window.

This is consistent with a REQUEST followed by 13 DATA ACKs and a final END ACK
for the 100-byte fixture. It supports repeat UART transfer activity but does
not independently verify Wi-Fi completion length or the repeated HTTP upload.
Sensor does not perform HTTP and cannot log that upload. Duplicate POST result
is still PENDING until Wi-Fi HTTP/PC receiver output is supplied.

Next: monitor Wi-Fi COM5 for the second GP21 press that uploads the saved copy,
then capture HTTP result and PC stored/duplicate line. If the user already
performed both presses, inspect existing output rather than issuing another
press (successful upload makes the next GP21 start a new retrieval).
No code, firmware, commit or push changes made.

Follow-up in repeat-upload test: after switching to Wi-Fi COM5 and pressing GP21,
user captured another retrieval (REQUEST sequence=32, valid=1/bytes=100/blocks=13,
completion_count=3, retries=0, CRC6d8c). LOG explicitly reports a waiting saved
copy. This differs from the upload phase inferred from the earlier Sensor-only
capture. The phase mismatch is not explained by the supplied excerpts; prior
unobserved upload/button activity must not be assumed or labeled as a code bug.
Source review confirms successful upload resets GP21 to retrieve; saved copy
sets it to upload; no new change was made. Duplicate POST remains unconfirmed.

Next controlled action: keep COM5 open, wait for retrieval indication to finish,
press GP21 once immediately after the visible Saved-100-byte state, then capture
both Wi-Fi HTTP output and the PC receiver line. No monitor switching, CLEAR,
reset or reflash is needed for this observation.

## 2026-10-26 — physical repeated combined upload / duplicate storage PASS

User stopped and reopened Wi-Fi COM5, then pressed GP21 once from the visible
Saved-100-byte state. Capture shows Uploading saved compressed log; length=100;
same log-508ce5ef... ID; HTTP 200/attempts=1; upload complete and Sensor retained.
This controlled observation confirms monitor reconnect preserved the waiting
snapshot and the next press uploaded it. It does not explain earlier missing
transitions in the separate monitor excerpts; no state-machine bug is asserted.

Independent PC inspection confirms the same expected output file still contains
all 100 fixture bytes with SHA256
508ce5ef84f333f067394c426c37e537693ab9224d29c117150c294e93cb72d4.
Exactly one root-level PC .bin matches this fixture; the older 1024-byte known
fixture file is a separate test. Evidence:
evidence/combined-physical-duplicate-upload.json. No PC console line supplied,
but physical Wi-Fi receipt plus existing file equality are recorded directly.
Upload LED observation remains unreported.

Physical repeated retrieval/upload retention and duplicate-file protection PASS
with the temporary Sensor probe. No reset/reflash or source change required.
Next recovery case: restart PC receiver with --drop-first-receipt and a fresh
http_received/lost_receipt_test output folder, preserving earlier evidence.
This makes the first POST store new bytes before deliberately omitting its
receipt. User will then retrieve/upload through two GP21 presses. Expected
retry uses the same ID/bytes and receives 200 in attempt 2 with one file in the
case folder. This physical lost-receipt test remains PENDING.

## 2026-10-26 — physical lost HTTP receipt / automatic recovery PASS

User started receiver bound to 192.168.137.1:8000 with --drop-first-receipt and
fresh output http_received/lost_receipt_test. Terminal explicitly reported:
listening/one-shot armed; stored 100 bytes; Receipt dropped once, stored bytes
retained; duplicate 100 bytes with the same ID and SHA256. The final Wi-Fi
upload printed Attempt 1/3, Attempt 2/3, HTTP=200/attempts=2, then upload complete
and Sensor retained. No manual retry was required within that final transaction.
The preceding one-attempt HTTP 200 serial entry is not counted as the injected
fault transaction; its relative timing to the receiver restart is not proven by
these excerpts. The final two-attempt output and PC store/drop/duplicate sequence
are the recovery evidence. Upload GP6 observation was not supplied.

Between those serial upload entries, user capture shows another successful
REQUEST_DATA sequence=47/retries=0, 13 blocks, 100 bytes, valid=1,
completion_count=4, CRC=6d8c and the unchanged fixture hex. This supports log
retention through earlier successful upload. No Sensor CLEAR was sent by this
flow.

Independent PC inspection: exactly one .bin in the fresh case folder, no .tmp;
filename f6a67aec3fd78cfd6fa69d39439ca281be21a6166bd71842ee49346599c10a03.bin;
all 100 bytes equal original fixture and prior UART capture; SHA256
508ce5ef84f333f067394c426c37e537693ab9224d29c117150c294e93cb72d4.
Evidence: evidence/combined-physical-lost-receipt.json. Physical receipt loss,
automatic retry with unchanged ID/body, duplicate storage protection and PC
byte equality PASS using the bare-metal Sensor probe plus combined SMP Wi-Fi.
No automated physical button, flash or serial action was performed by the agent.

Existing root/Wi-Fi summaries updated. This remains the 100-byte RAM-fixture
milestone; live AHT10/JZ flash integration, original reference startup cause,
and actual UART reception under simultaneous network fault load are unverified.
Next distinct failure case: stop PC receiver, retrieve then upload, allow three
HTTP attempts to exhaust, confirm saved-copy retention, then restart normal
receiver and retry that copy with one GP21 press without reboot/retrieval.
No firmware source change, commit or push made in this observation turn.

## 2026-10-26 — physical receiver-unavailable retry exhaustion PASS (recovery pending)

After stopping PC receiver as instructed, user pressed GP21 to retrieve and
again to upload. Combined capture: REQUEST_DATA sequence=62/retries=0; 13 blocks,
valid=1/bytes=100/completion_count=5; CRC6d8c; same fixture hex and content ID.
Upload then printed attempts 1/3, 2/3, 3/3 and FAILED: connect timeout,
HTTP=0/attempts=3. UI explicitly reported saved log retained; GP21 retries upload,
GP22 clears. This is the expected bounded failure, not successful storage.
No GP6 observation was supplied with this capture.

Physical bounded three-attempt exhaustion PASS. Actual saved-copy reuse is
still pending until receiver restoration and a single GP21 retry complete.
Do not infer preserved bytes solely from the UI message. Next user action:
restart normal receiver on 192.168.137.1:8000, output http_received (no fault
flags), wait for listening, then GP21 exactly once. Expect direct Uploading
saved compressed log (no new REQUEST_DATA/TRANSFER_END), same length/ID and
HTTP=200/attempts=1 against the existing PC file. No reboot, CLEAR or CONFIG.
No source/firmware changes, commit or push made.


## 2026-10-26 - physical retained-copy recovery after receiver outage PASS

User restarted the normal receiver on 192.168.137.1:8000 without fault flags,
then pressed GP21 once. Wi-Fi immediately printed Uploading saved compressed
log (100 bytes, unchanged log-508ce5ef... ID), HTTP attempt 1/3, success HTTP=200/
attempts=1, upload complete and Sensor retained. There is no CONFIG,
REQUEST_DATA, DATA or TRANSFER_END in the recovery capture. PC terminal reports
duplicate with length=100 and the original fixture SHA256. This follows the
captured receiver-off three-attempt exhaustion; retained-copy reuse now passes.

Independent PC read confirms original file still matches all 100 fixture bytes
and SHA256 508ce5ef84f333f067394c426c37e537693ab9224d29c117150c294e93cb72d4.
One root-level .bin matches this fixture; earlier 1024-byte tests are separate.
Evidence: evidence/combined-physical-outage-recovery.json. No new physical
action or serial-port operation was performed by the agent.

Current RAM-fixture physical HTTP milestone passes normal POST, repeated-data
deduplication, deliberately lost receipt recovery, bounded receiver-off failure,
and successful single-press retry without fresh UART retrieval. Normal receiver
has been restored. Sensor log is not automatically cleared. Source remains the
temporary probe; actual AHT10/JZ flash integration and original reference startup
root cause remain pending. Existing project summaries updated; no production
source change, commit or push made in this observation turn.

## 2026-10-26 - next development step: controlled reference startup comparison

User asks whether to reconnect AHT10 and what to fix next. Current Sensor image
still sends a fixed fixture; wiring AHT10 alone cannot add measurement or flash
retrieval. The unchanged JZ application does read AHT10 on I2C0 GP0 SDA/GP1 SCL
and has page-oriented encoder/flash APIs, but no UART command handler. Live
integration requires that handler and agreed stable transfer data ownership;
no production Sensor changes are authorized by the diagnostic exception.

Within the existing approved removable diagnostic folder, added a second build
selection: build.ps1 -Test static-contexts -> sensor_reference_static.uf2.
CMake generates a copy of the original Sensor entry point in ignored build/;
exact comparison verifies ONLY three declarations gain static storage:
pico1_ctx_t p1, stream_parser_t parser, protocol_frame_t frame. Original file,
UART/USB startup order, polling loop, 100-byte fixture, wire codec and all
protocol functions remain unchanged. No extra USB-wait gate or RX changes are
introduced in this comparison. No production build includes this folder.

Compiler -fstack-usage measurement with equivalent Release settings: original
main frame 1504 bytes; comparison main frame 96 bytes. This measures one frame,
not total nested/IRQ use, and does NOT establish a physical stack overflow or
root cause. It motivates a controlled storage-placement test after the earlier
probe changed several things together. All 125 protected files match hashes.

Comparison build PASS, UF2 family/payload checks PASS, local actual-binary
RP2040 emulator late-USB banner + exact CONFIG ACK PASS. Original startup probe
still builds and its emulator test passes after the additional selection.
Details: sensor_startup_probe/build/reference-static-validation.json and
sensor_reference_static-emulator.log, all deletable with that one folder.
UF2 size=70656, SHA256
0e413199fbb74edde2e8960e9f3cd7564db73ea6fb5040c5f97bc49cdc976311.
No new Markdown, JZ source modification, commit or push.

Next physical check PENDING: Wi-Fi unplugged, Sensor UART/AHT10 jumpers removed,
flash sensor_reference_static.uf2 to Sensor, open USB monitor/DTR and report
original readiness banner or silence. The current passing combined image on
Wi-Fi stays unchanged. Reconnect AHT10 later when testing the real Sensor app,
not as an attempted fix to this reference startup issue.

## 2026-10-26 - physical static-context-only comparison FAILED; readback next

User reports sensor_reference_static flashed on isolated Sensor, all jumpers
removed, serial port present but blank. Thus moving contexts alone did not
restore startup on this observation. Do not adopt it as a fix or diagnose a
stack overflow; compiler frame reduction is not proof of the physical cause.
The diagnostic validation JSON now records this user-observed failure.

Prepared another target inside the same removable folder: build.ps1 -Test
usb-wait -> sensor_reference_usb_wait.uf2. It compiles original reference main
unchanged, including automatic contexts and its original RX loop, plus a
link-time stdio_init_all wrapper. The wrapper waits for USB/DTR, waits 200 ms,
then prints/flushes REF-WAIT before original main enters UART setup. Disassembly
confirms main calls the wrapper. This is diagnostic only: a blocking USB gate
would be unsuitable as a final unattended Sensor startup fix.

Emulator test caught a host-model error: USBCDC asserts DTR during enumeration
unless overridden. Earlier tests labelled late-USB did not actually keep DTR
low. Corrected the helper to hold DTR low until 700 ms after enumeration, as in
the existing codec harness. Reran exact original reference, static comparison,
USB-wait comparison and working startup probe: all PASS startup/banner/CONFIG
ACK with this corrected model. USB-wait additionally asserts that UART divisor
writes occur only after simulated terminal opening. Existing physical evidence
is unaffected. Emulator still does not reproduce the Sensor reference failure.

New USB-wait build and UF2 payload/family checks PASS; all 125 protected source
files unchanged. Details: removable folder build/reference-usb-wait-validation.json.
No production source, JZ folder, commit or push changed.

Before requesting another reflash, next user action is BOOTSEL readback identity
and picotool verify of the CURRENT static image. This has not yet been checked
on hardware. Offline picotool info confirms the expected file identifies as
sensor_reference_static, SDK 2.2.0, pico_w, Release. Read-only verify syntax was
checked against installed picotool help. USB-wait physical test remains pending
until current-flash identity/integrity is established.

## 2026-10-26 - BOOTSEL readback blocked by missing Windows interface driver

User entered Sensor BOOTSEL and ran picotool info -a and verify against static
UF2. Both detected RP2040 bus 3/address 1 but could not connect. These are USB
access failures; program identity/flash verification have NOT run, and no
firmware mismatch/corruption is inferred.

Read-only Windows PnP confirms composite parent and interface 0 mass storage
OK; RP2 Boot interface 1 (VID2E8A/PID0003/MI01) Error. ProblemCode=28, Service=null,
DriverInfPath=null. Runtime COM6 driver is a different interface/device mode and
previously worked with the scan/probe images. Missing BOOTSEL interface driver
blocks picotool; it does not by itself explain original application silence.

Consulted Raspberry Pi official picotool README Zadig section:
https://github.com/raspberrypi/picotool#zadig
It specifies RP2 Boot (Interface 1), WinUSB. Official Zadig download:
https://zadig.akeo.ie/. Only interface 1 is to be changed; retain interface 0
mass storage and runtime USB serial. Agent made no driver or hardware changes.
Added this concrete desktop dependency to existing DESKTOP_SETUP.md.

Next user action: install WinUSB for the correct interface using Zadig, then
rerun info and read-only verify. Physical flash identity/integrity remain
PENDING until access works. No reflash, source change, commit or push made.

## 2026-10-26 - BOOTSEL driver fixed; actual static image verified PASS

After user installed WinUSB on the explicitly selected RP2 Boot Interface 1,
user ran info -a and verify successfully. Device reported RP2040 revision B2,
2048K flash. Program sensor_reference_static, USB stdin/stdout, SDK 2.2.0,
pico_w, Release, boot2_w25q080, binary 10000000..100089ec. Verification against
sensor_reference_static.uf2 reached 100% and OK. This establishes the exact
static-comparison image was stored on the board; wrong-file/failed-copy is not
the explanation for that specific observed blank-startup trial. It does not
prove the cause of the earlier unverified original image's failure.

The Windows interface-1 access blocker is resolved. Runtime startup remains
unresolved; missing BOOTSEL driver was not itself a serial-application fix.
Agent did not install drivers, flash or open physical serial devices.

Next user action: with only isolated Sensor in BOOTSEL, use picotool load -v -x
on prepared sensor_reference_usb_wait.uf2, then open its USB monitor/DTR. This
writes/verifies the new diagnostic and starts it in one command. Syntax confirmed
from installed picotool help: -v verifies, -x executes. Expected first REF-WAIT
marker before original UART calls, then original readiness banner if startup
completes. User should supply loader verification plus all serial output.
USB-wait physical result is PENDING; no permanent fix or source change claimed.


## 2026-10-26 - USB-before-UART comparison: physical startup PASS

User observed on Sensor COM6: `[REF-WAIT] USB ready before original UART setup;
stdio=1`, followed by `Sensor Pico protocol test ready` and `Logger length:
100 bytes`. This comparison retains the original automatic contexts and exact
baseline protocol sources. It demonstrates successful startup with the USB gate,
settling delay and diagnostic print/flush; it does not isolate their individual
contributions or prove a specific SDK fault. The earlier static-only comparison
remained blank despite verified flash, so stack storage alone was insufficient.

The indefinite USB gate is diagnostic only: the Sensor must eventually answer
UART without a serial terminal. Preparing a removable fixed 250 ms delay-only
comparison of the unchanged original entry point. No production Sensor, shared
protocol or frozen baseline source changed. Physical delay-only result pending.


## 2026-10-26 - Bounded startup-delay comparison built; physical result pending

Added removable `sensor_reference_delay` target and `-Test delay` selector only
inside `wifi_pico/tests/sensor_startup_probe/`. The exact original Sensor test
main still uses its automatic contexts and unchanged baseline libraries. Its
stdio call is wrapped privately to add `sleep_ms(250)` before UART setup; no
USB-connection wait or extra startup print is added. Disassembly confirms the
wrapper calls real stdio initialization then the 250 ms delay before uart_init.
This is a diagnostic comparison, not an established production correction.

Build PASS: pinned SDK 2.2.0, pico_w, Release, -Wall -Wextra, diagnostic wrapper
-Werror. Local RP2040JS emulator PASS with terminal opening late, and separately
PASS with no USB host constructed: exact CONFIG ACK in both. UART initializes
after at least 250 ms and before simulated terminal opening at 700 ms. These
emulator results do not establish physical headless operation or reproduce the
original physical failure. All 125 protected files match the prior snapshot.
UF2 payload matches BIN; SHA-256:
`e1b106a50e3af49d828ecd66144c358f4986e0419c298558768ec8571c9f75c5`.
UF2 71168 bytes; arm-none-eabi-size: text 39424, data 0, bss 4032, total 43456.

Build from repository root:
`./wifi_pico/tests/sensor_startup_probe/build.ps1 -Test delay`.
Emulator checks:
`node wifi_pico/tests/sensor_startup_probe/test_firmware.mjs sensor_reference_delay`
and the same command with trailing `no-usb`.

Next physical check: flash delay image on Sensor; with power off restore Wi-Fi
GP0 TX -> Sensor GP5 RX, Sensor GP4 TX -> Wi-Fi GP1 RX, common GND. Keep AHT10
disconnected and Sensor COM6 monitor closed. Power both, open Wi-Fi COM5 and
press GP20 once. Capture CONFIG acknowledgement or retry exhaustion. A pass
proves this physical UART trial does not require an open Sensor terminal; USB
power through a data cable still has a host, so this alone is not a no-host test.
No Wi-Fi reflash is needed. Agent did not flash boards or open serial ports.


## 2026-10-27 - Fixed-delay Sensor comparison: physical CONFIG PASS

Following the instructed `sensor_reference_delay` comparison, user supplied
Wi-Fi COM5 capture: GP20 pressed, Sending CONFIG, P2 CONFIG acknowledged,
UI CONFIG acknowledged, CMD sequence=2 retries=0 limit=3. User reports pressing
GP20 twice; supplied capture contains one complete command/acknowledgement
sequence, so only one successful command is counted. No second result or LED
observation is inferred. COM6 closed was the requested test condition; the user
did not separately restate monitor state in this result.

This confirms a physical CONFIG/ACK exchange with zero retries for the supplied
trial. It supports the bounded-delay startup comparison but does not prove the
original startup failure's precise mechanism or unattended no-USB-host operation.
The trailing Wi-Fi join diagnostic is separate from the UART acknowledgement;
it is not an HTTP POST/storage result. No production source changed, no new
build or emulator test was required, and the agent did not operate hardware.

Next physical check, one button action only: keep the same wiring and Sensor
monitor closed; press Wi-Fi GP21 once and capture retrieval. Expected fixture
100 bytes, 13 DATA blocks, valid TRANSFER_END and CRC16-X25 6d8c. This remains a
RAM-fixture test, not live AHT10/flash data. Do not press again to upload yet.


## 2026-10-27 - Fixed-delay Sensor comparison: physical retrieval PASS

User COM5 capture after GP21: REQUEST_DATA acknowledged, 13 DATA blocks ACKed,
TRANSFER_END validated, saved 100 bytes, received_log_valid=1, received_bytes=100,
blocks=13, completion_count=1, command sequence=4 retries=0 limit=3. Host
comparison of all four supplied hex rows (offsets 0, 32, 64, 96) against the
actual baseline mock_logger.c array is exactly equal (100 bytes). Independently
calculated CRC16-X25=6d8c and SHA-256=
508ce5ef84f333f067394c426c37e537693ab9224d29c117150c294e93cb72d4.
This is user-observed physical two-board UART evidence plus host byte comparison;
no build/emulator rerun and no HTTP storage result are implied. GP6 behaviour was
not supplied. COM6 closed was the instructed setup, not separately restated.

CONFIG and retrieval now pass for the bounded 250 ms startup-delay comparison.
The exact original startup-failure cause remains unproven. This is a RAM test
fixture, not real AHT10 sampling/flash integration. The temporary comparison
remains removable, with no production Sensor/shared/baseline source change.
Next physical check: open Sensor COM6/DTR without reset or reflash and capture
the original Sensor readiness banner. This checks the originally blank USB
output on the bounded-delay comparison directly; no button action is needed.


## 2026-10-27 - Fixed-delay comparison: late Sensor USB banner absent

After successful CONFIG and exact 100-byte UART retrieval, user switched to
Sensor COM6 and observed no Sensor text. Supplied Wi-Fi binary/Wi-Fi connection
lines precede the COM6 closed/opened markers; they are retained monitor history,
not new Sensor output. Expected late-open Sensor readiness banner check FAIL.
Prior physical UART pass remains valid; neither a restart nor UART failure is
inferred. No LED, USB/DTR internal state, or exact failure mechanism is proven.

Source inspection: unchanged Sensor reference prints readiness once when
stdio_usb_connected() becomes true and its local announced flag is false; it
resets that flag only when its polling loop observes disconnected. SDK 2.2.0
stdio_usb_connected() uses TinyUSB's CDC/DTR check with the default configuration.
The capture does not tell whether readiness was missed or whether the USB
connection check/output path failed. The 250 ms comparison therefore is NOT a
complete USB fix and must not be documented as one. The earlier indefinite
USB-wait image's successful banner remains a separate diagnostic result.

No new firmware or production source change made. Sensor USB output remains an
open issue; it is not needed to POST the already validated Wi-Fi RAM snapshot.
Next controlled check: normal PC receiver running, switch back to COM5 and press
GP21 once to upload the saved 100 bytes. Capture HTTP and PC storage/duplicate
result. No AHT10 or flash integration is implied by this fixture upload.


## 2026-10-27 - Fixed-delay Sensor fixture to HTTP: physical PASS

User COM5 capture after one upload press: POST /logs length=100,
ID=log-508ce5ef84f333f067394c426c37e537693ab9224d29c117150c294e93cb72d4,
attempt 1/3, success HTTP=200 attempts=1; UI reports Sensor log retained and
GP21 returns to retrieval. PC receiver at 192.168.137.1:8000 reports duplicate,
100 bytes and the matching SHA-256. This confirms an existing identical log was
accepted, not a new file created. No post-upload Sensor retrieval or LED result
was supplied in this particular capture.

Independent host read of existing http_received/
f6a67aec3fd78cfd6fa69d39439ca281be21a6166bd71842ee49346599c10a03.bin
confirms 100 bytes exactly equal the actual baseline mock_logger.c fixture,
SHA-256 508ce5ef84f333f067394c426c37e537693ab9224d29c117150c294e93cb72d4.
Together with the preceding physical CONFIG and 13-block UART capture, this
completes the fixed-delay RAM-fixture Sensor -> Wi-Fi Pico -> PC flow. Evidence
is user-observed hardware output plus host file comparison; no new build or
emulator run was needed. No source change, commit, push or agent hardware action.

Scope remains limited: Sensor late-open USB output is still unresolved; this is
not proof of live AHT10, RTC, flash retrieval, temperature decoding or CSV export.
No further repetitive fixture button test is needed for this milestone. Real
Sensor logger UART integration remains separate and requires coordination with
JZ; no changes to sensor_pico are authorized by these test captures.


## 2026-10-27 - Current Sensor USB works with controlled PowerShell DTR transition

On the current fixed-delay comparison, with no instructed reset or reflash, user
ran the independent PowerShell serial reader: opened COM6, requested DTR false
for 500 ms then true, read for 10 seconds. User observed original readiness line,
Logger length: 100 bytes, and Received 59 characters. Physical USB output PASS.
This is not a new build/emulator result. Earlier VS Code late-open blank result
is retained as an observation, not generalized to a permanently failed USB path.

The unchanged Sensor reference can therefore detect a USB connection and send
its original banner on the current firmware. This narrows the investigation to
the reader/open/reconnect sequence and one-time announcement interaction. It
does not by itself establish a specific VS Code defect, which part of the DTR
transition was necessary, or the cause of earlier different-image startup trials.

Read-only inspection of installed Serial Monitor 0.13.1 webview shows DTR/RTS
checkbox watchers invoke setSerialPortLines; these controls support changes
while monitoring. Next controlled user check: on VS Code COM6 start monitoring,
toggle DTR off, wait 1 second, then on while keeping the monitor active. Capture
whether original banner appears. No reset, reflash, UART command, production
source change, commit or push. Agent did not open a serial port.


## 2026-10-27 - VS Code live DTR toggle: physical Sensor USB PASS

User started COM6 monitoring, unchecked then checked DTR, and observed both
original lines: Sensor Pico protocol test ready; Logger length: 100 bytes. No
reflash/reset/button command was requested. Combined with the PowerShell result,
the current Sensor comparison's USB output works in both readers. The baseline
main announces only once per detected connection and rearms only after observing
disconnected; SDK default connection checking uses the CDC DTR state. An explicit
DTR off/on cycle gives a reliable reannouncement with the monitor already open.

This is a reproducible workaround and a narrowed diagnosis of the current blank
monitor symptom. These observations alone do not distinguish an initially missed
banner from initial-open DTR behaviour, nor establish a specific VS Code version
bug. They do not retroactively explain the original/static image startup trials.
Do not generalize the 250 ms comparison to a proven permanent production fix.

No further repetitive USB reflashes needed for the current reference. Keep this
workflow: monitor COM6, DTR off for one second, DTR on if banner is absent. A future
removable diagnostic can offer a user-requested status response without relying
on a single connection banner or adding repeated console clutter. JZ's Sensor,
shared UART and frozen baseline remain unchanged. No new build/emulator run,
agent hardware operation, commit or push occurred for this evidence update.


## 2026-10-27 - Live AHT10 RAM integration target prepared; physical pending

Added standalone `wifi_pico/tests/sensor_live_logger/`, target sensor_live_logger.
It compiles JZ's existing aht10.c and encoder.c READ ONLY plus the existing shared
pico1 protocol/framing/reliability. JZ main.c/flash_log.c are not compiled into this
target. No production Sensor file, shared protocol implementation, frozen UART
source/target, or Wi-Fi application source changed. All 125 protected files match
the prior snapshot. No commit/push or agent physical hardware operation.

This is the first live-temperature RAM milestone, not a completed persistent
Sensor logger. The unchanged shared pico1_backend_t callbacks connect CONFIG to
configuration/sampling, snapshot to an immutable sealed RAM copy, release to the
end of UART ownership (final ACK, abort or retry failure), and CLEAR to pausing
and resetting RAM. No wire format, IDs, timeouts, retry count or frame CRC changes.
The Sensor remains bare-metal/single core; the current four-role Wi-Fi SMP image
stays unchanged and continues opaque-byte HTTP uploads.

AHT10 conversion can block main for up to about 210 ms on error paths. UART1 RX
ISR stores bytes in a bounded 512-byte ring throughout; main alone parses frames,
dispatches protocol callbacks and samples. Critical sections cover only ring pop
on this single-core Sensor. Overflow aborts/releases the current transfer and
resets parsing; no silent publication of partial data. No ISR printing or I2C.

Logger RAM capacity is separately bounded to 1024 bytes (four encoder pages).
The UART frame payload limit remains 256, DATA chunk 8. Snapshots carry complete
256-byte JZ pages, including CHECK and 0xFF padding; no new container/header is
introduced. A one-page reading gives 32 DATA blocks, not the old fixture's 13.
Snapshot sealing operates on a copy; live samples may continue without changing
bytes being retransmitted. Full RAM stops sampling and preserves accepted data.
CLEAR pauses and empties this RAM test only; it never erases persistent flash.
A missing/failing AHT10 produces no invented temperature. Before the first valid
anchor, failures leave an empty log; physical pass requires a real successful
sample, not just a valid empty UART/HTTP transfer.

CONFIG still sends timestamp u32 then interval-seconds u32, little-endian. The
existing Wi-Fi sends timestamp 1700000000, interval 3600. The new adapter converts
only exact whole minutes for JZ TIMEBASE (3600 -> 60); zero, sub-minute/nonmultiple
and >65535-minute intervals are rejected by existing NACK behaviour. First sample
is immediate after each fresh CONFIG; subsequent samples use its interval.
A fresh CONFIG emits a new TIMEBASE with its supplied timestamp; duplicate
command retries do not restart sampling. Timestamp remains a simulated CONFIG
value, not a real RTC or current date. Quantization and delta/CHECK format are
JZ's actual encoder, not the older 100-byte fixture format.

USB never gates startup. Bounded 250 ms startup delay; connection status waits
200 ms before showing the banner. Send s for explicit status or d for the most
recent snapshot hex, so diagnosis no longer relies on catching one banner.
DTR off/on also reprints status. No repeated automatic heartbeat clutter. Output
wait is target-limited to 2 ms; snapshot dump emits one short line per main loop.

| Validation | Result and scope |
|---|---|
| New firmware build | PASS SDK 2.2.0, pico_w, Release; -Wall -Wextra, new main/adapter -Werror |
| New adapter host tests | PASS with address/undefined-behaviour sanitizers: capacity, exact unit conversion, immutable snapshots, duplicate CONFIG, lost END ACK, retry exhaustion/recovery, RAM CLEAR |
| Actual JZ decoder on adapter output | PASS: 605 sample slots across two pages, expected temperatures/error, valid CHECKs |
| Actual ARM binary, responding I2C model | PASS: 25 C reading -> sealed 256 bytes -> 32 UART DATA blocks; request arrives during conversion; USB s/d dump matches UART bytes |
| Actual ARM binary, absent I2C model | PASS: error reported, no fake temperature or DATA blocks; valid empty transfer only |
| Actual ARM binary, no USB host model | PASS: CONFIG/retrieval without a host/serial monitor |
| Actual JZ decoder on ARM emulator payloads | PASS one 25 C sample and valid CHECK for both responding-model variants |
| Existing shared host suite | PASS baseline assertions, all eight reliability groups and codec/backend tests |
| Existing two-board profile host suite | PASS all ten cases and binary capture comparator |
| Existing combined UART/HTTP host suite | PASS ownership, byte equality, retries, clear, lost receipt and duplicate storage |
| Original UART emulator modes 0-4 | PASS using existing unchanged binaries; golden wire captures identical; not rebuilt this turn |
| Physical AHT10/live UART/HTTP | PENDING; must be performed by user |

New BIN 43740 bytes; UF2 87552 bytes, RP2040 family/payload verified.
Final UF2 SHA-256: `312cea41712f969d3947601409219840ea931f0053dcf6c8e88fc2f02532b661`.
arm-none-eabi-size: text 47832, data 0, bss 8672, total 56504.
Source checkout HEAD 3b461a58ca7f9eab59dd5eacc984aef84fe69ffd plus existing local
changes. Generated local validation hashes/results: target build/validation.json.
Build directory/output files remain ignored. No new README/MD file was added.

Build from repository root:
`./wifi_pico/tests/sensor_live_logger/build.ps1`.
UF2: `wifi_pico/tests/sensor_live_logger/build/sensor_live_logger.uf2`.
Host tests: under a GCC environment, run
`python3 wifi_pico/tests/sensor_live_logger/run_tests.py`.
Firmware emulator tests: with existing uart_protocol/tests Node dependencies,
`node wifi_pico/tests/sensor_live_logger/test_firmware.mjs ready` (also absent,
no-usb). They model peripherals; they do not establish physical sensor behaviour.

First physical step only: keep current Wi-Fi firmware; flash the new UF2 only
on Sensor. With both boards powered off, connect AHT10 VCC -> Sensor 3V3 OUT
(pin 36), GND -> Sensor GND, SDA -> Sensor GP0 (pin 1), SCL -> Sensor GP1 (pin 2).
These are JZ's actual pins, not older codec-loopback GP8/GP9. Keep UART crossed:
Wi-Fi GP0 TX (pin 1) -> Sensor GP5 RX (pin 7); Sensor GP4 TX (pin 6) -> Wi-Fi GP1
RX (pin 2), plus common GND. Each board uses its own USB power. Official board
pinout: https://datasheets.raspberrypi.com/picow/PicoW-A4-Pinout.pdf.

Power up and open Sensor COM6 at 115200, DTR on. Expected START says live AHT10
UART/RAM only; SENSOR AHT10 init: ok; LOG samples=0 running=0; UART dropped=0.
If banner is missed, send s (or DTR off/on). Capture this startup result before
pressing buttons. Then later test GP20 first sample, GP21 retrieval, second GP21
upload, and compare full snapshot bytes at PC. These physical actions are not
performed or marked PASS by the agent. This milestone does not implement flash
retrieval, external RTC/dormant wake, or automatic PC temperature decode/CSV.


## 2026-10-28 - First live Sensor startup: USB PASS, AHT10 initialization I2C error

User flashed/connected the live target and AHT10, then supplied COM6 output:
START live AHT10 UART / bare-metal RAM only; AHT10 init: I2C error at 0x38 on
I2C0 GP0/GP1; samples=0 errors=0 running=0 full=0 capacity=1024; UART frames=0,
rejected=0 errors=1 dropped=0. USB/startup status works. Sampling not started;
zero samples before CONFIG is expected. AHT10 init is NOT a physical PASS.
No live-temperature retrieval or HTTP equality is inferred. The single UART
error has no demonstrated cause and does not identify an I2C failure.

Read-only inspection of JZ aht10.c: I2C error can mean reset write 0xBA failed,
init write E1/08/00 failed, or final status read failed. The supplied summary
cannot identify which operation or distinguish timeout from NACK/short write.
Do not assume wiring is wrong or silently modify JZ's driver. The live target
allows a separate bounded measurement attempt after CONFIG even if init failed;
previous codec hardware history also included failed init with valid readings,
which is not proof the same cause applies here.

Next one-action test: keep COM6 monitoring; use an additional monitor for COM5,
wait for Wi-Fi READY, press Wi-Fi GP20 once. Capture Wi-Fi CONFIG ACK/failure and
Sensor CONFIG/SAMPLE outcome. COM5 must be opened after Wi-Fi power-up because
current combined_main.c still gates application startup on tm_usb_state()==3.
No GP21/GP22, upload, wiring change or reflash is requested. Actual measurement
result is pending; no code, build or emulator change made for this observation.


## 2026-10-28 - Real AHT10 reads and RAM encoding: physical PASS; init warning remains

User pressed Wi-Fi GP20 once while watching COM5: CONFIG acknowledged,
CMD sequence=1 retries=0 limit=3. After switching to COM6, initial status showed
samples=1 errors=0 running=1, latest raw 29.29 C, UART frames=1 rejected=0
errors=1 dropped=0. User pressed Wi-Fi GP20 a second time while watching COM6:
CONFIG interval=3600 seconds timestamp=1700000000 (demo/no RTC), then
SAMPLE 29.30 C stored=1 total=2. These are two fresh user commands, not evidence
of duplicate-command retry behaviour. Both actual sensor readings were stored
through the unchanged JZ encoder into the new RAM adapter.

Physical CONFIG/measurement/RAM append PASS. AHT10 init: I2C error still refers
to the saved startup result; init_status is not rewritten by later measurements.
Successful reads do not prove startup init was correct. The exact failing init
transaction remains unresolved. The one UART error is accumulated and still has
no established cause; status precedes the second command. No GP6 observation
was supplied. No live log retrieval, live HTTP, persistent flash, RTC or automatic
CSV result is inferred. No source, build or emulator change for this capture.

Installed Microsoft Serial Monitor 0.13.1 supports multiple view instances:
monitor0 (Serial Monitor), monitor1 (Serial Monitor (2)), monitor2. The webview
button is 'Open an additional monitor' at the top above Monitor Mode. Changing
the current view's port dropdown replaces that view's connection; it does not
create another monitor. An additional view may be below the first in the same
panel and require expanding/scrolling. Alternative: COM5 in VS Code and COM6 in
the already tested PowerShell reader; never open the same port in both readers.
Concurrent monitors are convenient, not required once Wi-Fi startup has run.

Next one-button test: monitor COM5, press GP21 once only to retrieve the actual
RAM log. Expect valid 256-byte JZ page with 32 DATA blocks, one completion and
GP6 two flashes. Exact CRC/ID differs from the old 100-byte fixture. Capture
Wi-Fi output and LED; do not upload yet. Sensor snapshot can later be obtained
using its d command even if COM6 was not monitored during the transfer.


## 2026-10-28 - Live AHT10 RAM page retrieved over physical UART: PASS

User successfully opened two serial monitor views and supplied COM5/COM6
captures. Wi-Fi GP21: REQUEST_DATA acknowledged, 32 DATA blocks ACKed,
TRANSFER_END validated, saved 256 bytes, valid=1, completion_count=1,
command sequence=3 retries=0. Sensor: Snapshot=1 bytes=256 CRC16-X25=0f87,
then snapshot released: final ACK received. The Sensor source lease therefore
completed on actual final ACK, not merely on the receiver's completion log.
No LED observation supplied.

Host reconstruction of all eight supplied 32-byte rows gives 256 bytes.
First row:
`73716553f100003c8115716553f100003c811574fe81ffffffffffffffffffff`
Remaining seven rows are each 32 FF bytes. Computed CRC16-X25=0f87 matches both
board reports. Received-data SHA-256:
`8cfd1bb81171003e66917742a60f276e1875c17519b021b21499a63ed4954597`.
JZ's actual unchanged Python decoder validates CHECK 0xFE81 and two samples
q=277, temperature=29.25 C, status=ok. Raw readings 29.29/29.30 C quantize to
29.25 C through JZ's quarter-degree encoder. Both sample timestamps are the
same demo 1700000000 because each fresh GP20 CONFIG supplied that same value
and emitted a new TIMEBASE. This does not establish RTC or real-time ordering.

Physical sensor-read/encoding/UART milestone PASS, with host decoding of the
captured data. Full Sensor byte dump has not been supplied: Sensor's matching
CRC/length is not alone a byte-for-byte source comparison. That can be obtained
later by Sensor USB d without a new retrieval. No physical live HTTP upload or
PC storage is yet established for this 256-byte page. AHT10 init warning remains
unresolved; RAM only, no flash/RTC/automatic CSV. No source/build changes here.

Next one-button check: normal PC receiver listening, Wi-Fi connected with an
IPv4 address, press GP21 once to upload the saved page. Expected length=256,
ID=log-8cfd1bb81171003e66917742a60f276e1875c17519b021b21499a63ed4954597,
HTTP 201 if new or 200 if identical data already exists. Capture both HTTP client
and PC receiver results. No reset/retrieval is needed before this upload.


## 2026-10-28 - Physical live RAM data POST and PC equality: PASS

User COM5: Uploading saved compressed log, POST /logs length=256,
ID=log-8cfd1bb81171003e66917742a60f276e1875c17519b021b21499a63ed4954597,
HTTP=201 attempts=1. UI reports Sensor log retained; next GP21 retrieves again.
PC receiver listening at 192.168.137.1:8000/logs reports stored, bytes=256,
matching SHA-256 8cfd1bb81171003e66917742a60f276e1875c17519b021b21499a63ed4954597.

Independent read of actual PC file http_received/
5811e5175c3f514a742dacfe3f37229faf408d88759c03082296d0cdb5c9b452.bin
confirms all 256 bytes exactly equal the previously supplied Wi-Fi UART capture.
JZ's actual unchanged decoder validates CHECK FE81 and two q=277 / 29.25 C samples
with status ok. Host decoding was a validation action; the HTTP receiver still
only stores binary, without automatic temperature decoding/CSV export.

This establishes physical live AHT10 -> JZ RAM encoding -> UART -> Wi-Fi HTTP ->
PC storage for this trial, plus exact Wi-Fi-capture/PC equality. Full Sensor
snapshot bytes have not been supplied; its matching CRC/length and final ACK
are evidence but not by themselves a full source-byte comparison. GP6 observation
and a fresh post-upload retrieval were not supplied. No new build/emulator run,
production code change, commit, push or agent board operation.

Remaining scope: initial AHT10 init I2C warning unresolved; RAM target does not
perform flash retrieval/erase or external RTC; CONFIG timestamps remain simulated.
Next single action: on COM6 send d, then supply BINARY BEGIN through BINARY END.
It reads the last UART snapshot without a new retrieval or sample, allowing
comparison to both Wi-Fi capture and PC file. Do not reset, clear or press GP21
before obtaining this dump, as those can replace the retained snapshot.


## 2026-10-28 - Full Sensor / Wi-Fi / PC byte equality: physical live RAM milestone PASS

User sent lowercase d through COM6 and supplied the entire BINARY BEGIN/END
snapshot=1, length=256, CRC16-X25=0f87. All eight 32-byte rows reconstruct to
exactly the previous Wi-Fi UART capture and the independently read PC file.
All 256 bytes compare equal across Sensor dump, Wi-Fi capture and PC storage.
SHA-256 8cfd1bb81171003e66917742a60f276e1875c17519b021b21499a63ed4954597;
CRC16-X25 independently recalculated 0f87. Together with prior actual ADC values,
JZ CHECK validation, UART final ACK and HTTP201/attempt1, this establishes the
first physical live AHT10 -> JZ RAM encoding -> UART -> Wi-Fi HTTP -> PC trial.

The d command exists only in this separate Sensor live test application. Its
console_poll reads a USB character and prints logger.snapshot, the immutable
copy used for the most recent UART retrieval. It is a diagnostic display command,
not a UART wire message, another retrieval, sample trigger, log clear or flash
operation. FF bytes after the encoder CHECK are unused page padding. s prints
status. Uppercase variants are also accepted. No firmware/source changes needed
for this evidence; no new build/emulator run and no agent physical board action.

Remaining limitations are unchanged: AHT10 startup init I2C warning unresolved,
RAM-only logging, simulated and repeated CONFIG timestamp, no external RTC/flash
retrieval, no automatic HTTP receiver decoding/CSV export. First successful live
happy path does not establish physical fault recovery/concurrency for this new
Sensor target. GP6 behaviour not reported for these particular live captures.
No further byte-equality test is needed for this captured trial. Historical
pending entries above describe what was known at each earlier observation.


## 2026-10-28 - Physical warming response and explicit status: PASS

User COM6 after two fresh Wi-Fi GP20 commands: SAMPLE 29.34 C stored=1 total=3,
then SAMPLE 31.30 C stored=1 total=4. USB s reports samples=4 errors=0 running=1,
full=0, latest=31.30 C; UART frames=38 rejected=0 errors=1 dropped=0. Thus fresh
reads and RAM encoding respond to warming. Measurement errors/dropped bytes
remain zero; the single accumulated UART error is unchanged from earlier status
but its cause is not established. Initial AHT10 I2C warning remains cached and
unresolved. No new build/emulator result is implied by this physical observation.

The last PC file has only the first two samples. Keep Sensor powered and save
these additional RAM readings before any diagnostic reflash, because reset loses
RAM. Prepare an optional, separately named I2C trace image: wrap actual SDK
blocking-until functions (timeout_us helpers are inline), capture only original
aht10_init calls and print records later through USB status. No new bus commands,
timeout/retry changes, or JZ/protocol source changes. This will identify reset,
init-write or status-read failure and distinguish short write from timeout.


## 2026-10-28 - Optional I2C startup trace built; physical result pending

Added only i2c_diag.c/.h in the separate live Sensor test folder, an optional
main status call, a separate CMake executable and build.ps1 -I2CDiagnostics.
Output sensor_live_logger_i2c_diag.uf2 has distinct identity/name. Original live
BIN/UF2 were preserved under ignored build/verified-live, then rebuilt and proved
bit-identical (normal UF2 SHA-256 still 312cea41712f969d3947601409219840ea931f0053dcf6c8e88fc2f02532b661).
All 125 protected files, JZ driver/encoder and shared/baseline protocol unchanged.
No source in sensor_pico modified; no commit/push or agent board operation.

SDK inspection confirms timeout_us helpers inline into JZ code and call the
exported blocking_until routines. Only the diagnostic target wraps aht10_init,
i2c_write_blocking_until and i2c_read_blocking_until. It captures up to four
original startup operations: reset BA, init E10800, status read, with requested
and returned counts, bytes, duration and initial SDA/SCL levels. Actual driver
results and deadlines are returned unchanged; no extra bus operation or retry.
Printing is deferred to USB status after startup, not done in the I2C/ISR path.
Measurement I2C calls are not recorded. s can reprint the saved trace; USB need
not be open during boot. A short write is reported rather than silently accepted.

Build PASS: SDK 2.2.0 pico_w Release, -Wall -Wextra, diagnostic own code -Werror.
Actual linked wrapper symbols inspected. UF2 RP2040 family and BIN payload match;
BIN 44908 bytes, UF2 90112 bytes. SHA-256
7dc799ed61ebf5779e64d3dc9c3b879c85b70c505b73c4cd3c9e7691268492a5.
arm-none-eabi-size: text 49000 data 0 bss 8776 total 57776.

Actual diagnostic binary RP2040JS cases PASS:
- ready: three complete init operations; 25 C sample, sealed page, UART IRQ/
  retrieval and explicit USB s/d outputs remain correct;
- init-short-write: reset 1/1, E10800 returns 1/3 and correctly reports short
  write/I2C error, while subsequent driver measurement and UART transfer work;
- absent: first reset returns SDK timeout -2 at about 20 ms, correctly reported,
  no invented sample and an empty transfer;
- no-usb: startup/CONFIG/retrieval without any USB host.

Initial absent-test assertion assumed generic -1; the trace revealed -2 timeout.
Model source inspection shows its abort flushes FIFO without restoring TX_EMPTY,
so the SDK waits to timeout. Test corrected to require -2 and 19.9..20.1 ms;
no firmware, SDK or vendor model changed to pass it. This is a model limitation,
not proof of a physical NACK/timeout cause. Modeled GPIO levels/timings similarly
do not establish real I2C electrical behaviour. The short-write model reproduces
a possible symptom only; actual Sensor init diagnosis remains PENDING.

Build: ./wifi_pico/tests/sensor_live_logger/build.ps1 -I2CDiagnostics.
Emulator: node wifi_pico/tests/sensor_live_logger/test_firmware.mjs
init-short-write sensor_live_logger_i2c_diag (also ready, absent, no-usb).
Generated detailed checks: build/i2c-diagnostics-validation.json. Existing protocol
regressions not rerun this turn because their sources and normal firmware bytes
are unchanged; prior baseline and live tests remain separate recorded results.

Do not flash yet: user currently has four RAM samples, but only first two were
saved to PC. Next single physical action is GP21 once in COM5 to retrieve the
updated RAM page, followed later by its upload. Preserve those results before
reset/reflash. Then the one diagnostic flash can capture every init operation
in one report; no repeated trial delays or protocol redesign is required.


## 2026-10-28 - Updated four-reading RAM retrieval: physical PASS, upload pending

User pressed GP21 and supplied COM5: 32 DATA blocks ACKed, TRANSFER_END validated,
received_log_valid=1 received_bytes=256 blocks=32 completion_count=2,
CMD sequence=39 retries=0 limit=3. New received page CRC16-X25=e070. Complete
user-supplied eight-row capture reconstructed and CRC independently verified.
SHA-256 a061d4eee7dfd8cd2b8b4a3f5e5fd5dd5d005683ac55edc0b8dcac027417b631.
JZ's unchanged decoder gives [29.25,29.25,29.25,31.25] C, all status ok and
CHECK 25BA valid. These match quarter-degree quantization of the four reported
raw readings 29.29,29.30,29.34,31.30 C. Timestamps remain simulated/repeated.

Completion increased once (1 -> 2). Current new page holds all four samples
and its warming response; it differs from the earlier two-sample PC file.
Physical updated UART retrieval PASS, plus host validation of the capture.
New PC storage and full Sensor snapshot-2 byte comparison are not yet supplied;
prior full snapshot-1/Sensor/Wi-Fi/PC equality remains separate evidence.
No LED observation, new build/emulator run, source change or agent hardware test.

Next one-button step: keep normal PC receiver running and both boards powered;
press GP21 once to upload this saved page. Expected ID
log-a061d4eee7dfd8cd2b8b4a3f5e5fd5dd5d005683ac55edc0b8dcac027417b631,
length=256, HTTP201 if new or HTTP200 if identical already stored. Capture PC
and COM5 results before resetting/flashing startup I2C diagnostic. No CLEAR or
new retrieval needed. Diagnostic physical root-cause test is still pending.


## 2026-10-28 - Four-reading warmed log saved to PC: physical PASS

User COM5 reports POST /logs length=256,
ID=log-a061d4eee7dfd8cd2b8b4a3f5e5fd5dd5d005683ac55edc0b8dcac027417b631,
HTTP201 attempts=1; UI says Sensor log retained, next GP21 retrieves again.
PC receiver reports stored, bytes=256, matching SHA-256. Independent read of
http_received/aee994f5efcdb2676f628ca9da3c4738aa68155a35323df1f71af88b813abd4b.bin
confirms exact equality with all 256 completion-2 Wi-Fi capture bytes. Actual
unchanged JZ decoder validates 29.25,29.25,29.25,31.25 C and CHECK25BA. Thus the
four captured readings, including the warming response, are now saved on PC.
No new source/build/emulator change or agent hardware action for this result.
No GP6 observation or source snapshot-2 full dump supplied; earlier full
snapshot-1 source/Wi-Fi/PC equality remains distinct evidence.

It is now safe to reset RAM for startup diagnostics without losing these four
captured samples. Prepared diagnostic UF2 SHA-256 reverified against its local
build record (7dc799ed61ebf5779e64d3dc9c3b879c85b70c505b73c4cd3c9e7691268492a5).
Physical diagnostic not yet run; no actual init failure mode identified.
Next single physical step: flash only Sensor with sensor_live_logger_i2c_diag.uf2,
keep AHT10/UART wiring and current Wi-Fi image, then open Sensor COM6 and capture
all I2C INIT lines. USB s reprints saved trace; DTR off/on also works. No buttons
or another retrieval/upload before recording this startup trace. This target
observes unchanged JZ init calls and does not claim to fix the warning.


## 2026-10-29 - User-observed live RAM baseline and PC CSV milestone

Physical results supplied by the user before the CSV receiver change:

- Four-task combined Wi-Fi image, SMP=1, Sensor emulator disabled, UART_FAULT=0.
- Wi-Fi joined the laptop hotspot on its second attempt; DHCP address
  192.168.137.199. This is an observed connection result, not a reliability guarantee.
- Two GP20 presses: CONFIG sequence 1/2 ACKed with zero retries; GP6 flashed once
  per command. Sensor sampled 28.22 and 28.25 C, stored totals 1 and 2.
- One GP21 retrieval: 256 bytes / 32 blocks / one completion / retries=0;
  Sensor and Wi-Fi CRC16-X25 both ed6a; Sensor received final ACK and released
  the snapshot. Retrieval LED observation was not separately supplied in this run.
- Next GP21: HTTP201 attempts=1, PC stored 256 bytes, user observed one long GP6
  flash. SHA-256 2b54ff11296595b9e593c9231450c52bcbe2cd8280ceacd93d5f3c50ae5fffec.
- Independent PC-file read matches every byte of the complete Wi-Fi hex capture.
  JZ CHECK=1330 validates. Both reconstructed temperatures are 28.25 C because
  the encoder quantizes to 0.25 C. Both timestamps are the repeated CONFIG
  value 1700000000; these are simulated, not real RTC observations.
- AHT10 label confirmed by user. Diagnostic startup: SDA/SCL HIGH, BA reset
  write returns 1/1, E1 08 00 init returns 1/3. SDK return-path inspection places
  the data NACK at the second byte (08). Subsequent reading 24.64 C also passed
  in the earlier diagnostic run. Initialization cause/fix remains unresolved.
  One startup UART receive error was reported; no drops. No cause was proven.

PC-only CSV implementation:

- tools/temperature_csv.py reuses the unchanged sensor_pico/tools/decode.py;
  prechecks complete pages, record boundaries, padding and all CHECK checksums.
- tools/http_receiver.py adds opt-in --decode-jz and an explicit
  --timestamp-source label. Binary storage/receipts and dedup stay intact.
  Invalid payload/export errors retain the binary and report CSV unavailable.
- CSV has numeric temperature_c, UTC/SGT timestamps, original Unix seconds,
  source label, status, segment/slot/page. SENSOR_ERR has a blank temperature.
  Empty logs yield header-only CSV. Current lab uses --timestamp-source simulated.
- Browser GET / lists available CSV downloads; /logs/<64-hex-name>.csv provides
  attachments. No arbitrary paths, raw binary downloads, authentication, decoder
  task on Pico, new network credentials or changes to firmware were introduced.
- Existing physical-test binary exported offline as
  http_received/7fe2ccdf51f64e9bb547d7fe6d8856aede7e28fded7a30a22a175d6d4db92f50.csv.
  Original .bin and its SHA-256 are unchanged. This conversion is a PC test using
  real captured bytes, not another physical HTTP test.

Validation on 2026-10-29:

- Windows: 14 decoder/CSV tests passed; initial receiver suite passed 14 checks
  with its Linux-only C-client check skipped. Linux checks below cover that path.
- WSL Ubuntu-24.04: HTTP host runner includes 14 decoder/CSV and 16 receiver tests;
  all passed, including actual C client with valid JZ bytes, CSV enabled and a
  lost receipt, plus known-byte client/receiver tests. C network retry/client
  checks passed with -Wall -Wextra -Werror and address/undefined sanitizers.
- Existing combined host suite passed: SHA IDs, buffer ownership, busy buttons,
  final-ACK duplicates, failure/clear/oversize/timeout recovery and exact UART
  reassembly -> HTTP -> PC bytes with lost-receipt deduplication.
- All 75 protected C/header files across Sensor/shared/baseline/Wi-Fi application,
  plus JZ's decoder, remain byte-identical to the pre-change snapshot.
- No Pico rebuild, new RP2040 emulator run or assistant physical hardware test.
  Physical upload with CSV-enabled receiver remains PENDING user confirmation.

Run receiver from repository root:
`python .\wifi_pico\tools\http_receiver.py --bind 192.168.137.1 --decode-jz --timestamp-source simulated`
Then open http://192.168.137.1:8000/ . Keep the process running for multiple
uploads. Existing binaries can also be converted with tools/temperature_csv.py.
No firmware reflash, commit or push is required/performed for this milestone.


## 2026-10-29 - Repeated stale DHCP diagnostic: reproduced and fixed

User reports COM5 continuously prints Setup failed: init=0 join=0 dhcp=-3,
obscuring button output. COM6 nevertheless ACKed CONFIG and sampled 28.57 C,
stored=1 total=5. Five current RAM readings are therefore reported; the last
uploaded/converted CSV still contains its earlier two-reading snapshot.

Root cause in tests/firmware/http_test.c: the course startup DHCP result can
remain ERR_TIMEOUT (-3) after the live interface acquires an address. When
ready, the application cleared its reported-failure mask on every poll and
immediately printed the old error again. This was an application diagnostic
bug; the PC CSV receiver did not change or reflash the running firmware.

Focused test tests/test_wifi_reporting.c executes the actual application poll
hook with controlled host network inputs and the actual course status type.
Before the fix: one initial timeout plus 1000 ready polls printed 1001 failures;
the regression correctly failed. Minimal fix: report setup failures only while
the actual connection is not ready. After the fix: one report per offline
failure episode, no stale error flood over 1000 ready polls; another genuine
loss still reports once; upload_poll receives every ready/offline poll.

Additional build correction: two explanatory lines in kernel_io.c began with
# hardware status masks / # kernel notification objects. The compiler rejected
these as invalid preprocessing directives. Their wording is preserved as C
comments; hardware definitions and operations are unchanged.

Validation:
- Actual-poll regression PASS, -Wall -Wextra -Werror with address/undefined
  sanitizers. The HTTP runner now includes it and generates its tiny host-only
  headers under ignored build_make/http_host/reporting_mock/.
- Existing Wi-Fi retry/C HTTP client checks and all 30 Python CSV/receiver
  checks PASS in WSL. No new RP2040 emulator run or physical test is claimed.
- Combined Pico W firmware build and static verifier PASS: SDK 2.2.0, SMP=1,
  USB diagnostics, Sensor emulator disabled, four application tasks.
- ELF size: text=341872 data=2480 bss=77800 dec=422152.
- Sensor sources, shared protocol and frozen UART baseline unchanged. Root
  application changes are limited to the two comment corrections; actual
  networking application change is only the diagnostic reporting condition.
- Physical no-flood confirmation PENDING user flashing Wi-Fi only. This is a
  diagnostic fix; it does not implement DHCP reconnect or alter UART/retries.
- No commit or push. Keep Sensor powered to retain its RAM samples.


## 2026-10-29 - Six-reading live UART/HTTP/CSV download: physical PASS

User flashed the Wi-Fi reporting fix. First association failed; the automatic
second attempt obtained 192.168.137.199 and reported HTTP available. No flood
appears in the supplied fresh startup capture. This run did not deliberately
reproduce late DHCP timeout recovery on hardware; the focused host regression
covers that specific reporting branch.

Sensor remained powered with five RAM readings. GP20: CONFIG sequence=1 ACKed,
retries=0; Sensor sampled 28.43 C, stored=1 total=6. GP21: REQUEST_DATA ACKed,
256 bytes in 32 DATA blocks, TRANSFER_END validated, completion_count=1,
command sequence=2 retries=0. Sensor Snapshot=2 CRC16-X25=a275 matches Wi-Fi;
Sensor reports final ACK received and releases the snapshot. Different endpoint
snapshot/completion counters reflect only the Wi-Fi having restarted.

Next GP21: user COM5 reports HTTP201 attempts=1, length=256,
ID=log-beae8c36b09eedd5b5f755e6eb1b56185f127052af0132318f6bd00a7ad34983.
PC confirms stored bytes=256 with identical SHA-256 and CSV samples=6,
timestamps=simulated. User supplied downloaded temperature-log-813e6ffeda75.csv.
Independent read-only checks confirm the downloaded file exactly equals the
receiver CSV at http_received/813e6ffeda755e75bad888e8cfcd8ce566fa6a8d1451cced43629fe3d252b03a.csv.
Its source .bin has the expected SHA-256 and independently calculated UART
CRC a275. JZ CHECK63C0 validates; all six rows agree with decoded binary:
[28.25,28.25,30.25,30.75,28.50,28.50] C, all status ok. Each row preserves
Unix1700000000, UTC2023-11-14 22:13:20 and SGT2023-11-15 06:13:20. Repeated
simulated timestamps arise from repeated demo CONFIG and do not prove RTC use.

Result: user-observed real AHT10 -> RAM encoding -> physical UART -> HTTP ->
PC binary/CSV storage -> downloaded CSV PASS, with independent PC validation.
No new LED observation was supplied for this six-reading retrieval/upload.
Sensor source full hex was not supplied in this run; source/destination CRCs
match, and all destination bytes match the earlier complete Wi-Fi capture.
No agent physical actions, new source change, build or emulator run for this
result. Flash/RTC/sleep/wake/preamble remain pending; AHT10 startup init warning
remains unresolved. No commit or push.


## Physical SMP boot evidence 2026-10-20

Consolidated here to keep one Wi-Fi test-results document; the original raw
capture remains in hardware/. Historical scope and observations follow.

### User-observed physical SMP boot evidence

Date: 2026-10-20 (Asia/Singapore).
Hardware: one RP2040 Pico W on Maker Pi carrier, USB serial; user removed UART
jumpers and AHT10 wiring. Firmware: wifi_pico_smp_boot.uf2, course commit
2e8e6ad480c2d84a369b12995694b9bf5e9ddfb3.
The user flashed and observed the board; the assistant did not operate it.

#### Result: PASS for this short single-board SMP boot observation

The submitted excerpt shows diag0 reporting tk_prc=1 / physical core 0 and diag1
reporting tk_prc=2 / physical core 1. Both heartbeat counters advance from 30 to 31.
The kernel event-flag coordinator reports both-core mask 0x3.
No [FAIL] line is visible in this excerpt. This is not a long-run stability result.

The user reports an approximately 10-second observation. The pasted excerpt
contains only a short portion of it and is clipped at its first and last lines.
Preserve those clipped lines exactly; do not reconstruct missing output.
The raw supplied text is hardware/smp_boot_user_capture_2026-10-20.txt.

#### Repeated BOOT/BUILD explanation

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

#### Limits

Physical SMP boot / USB task reporting: user-observed PASS.
UART IRQ/ring/protocol integration: not tested by this capture.
Buttons and GP6 in the RTOS application: not tested by this capture.
Two-board Sensor integration, AHT10, HTTP and networking: not tested here.
Existing bare-metal evidence stays in
../../uart_protocol/tests/hardware/SINGLE_BOARD_TEST_RESULTS.md.


## 2026-10-29 - GitHub checkpoint validation from staged files

Checkpoint scope: tested live RAM UART + micro T-Kernel SMP + HTTP + PC CSV.
Agreed batching, PC interval settings, three-nibble TIMEBASE, long-hold flash
CLEAR, preamble and real Sensor flash/RTC/sleep integration remain pending.
No firmware behavior or JZ source was changed for commit preparation.

The stale USB boot verifier expected six course patches although ORIGIN.json
records seven; its list now includes cyw43_utk_kernel.c. A temporary clean copy
of the staged Git tree passed shared protocol/backend checks, all eight reused
reliability groups, UART host/two-board cases, live adapter/JZ decoder tests,
all 30 Python CSV/receiver tests, HTTP client/network regression tests and the
combined UART/HTTP host flow. The USB-only SMP boot build/static verifier also
passed against the pinned SDK/course and 86 frozen baseline hashes.
The clean combined example-configuration build/static verifier also passed
(SMP=1, USB, radio-owner hook, unchanged baseline). This temporary build disables
POST and does not replace the active physical-test image.
Existing physical evidence above remains distinct from these fresh host/build
checks. Current live Sensor/Wi-Fi source and active firmware hashes are preserved.
Historical standalone probe/scan programs and regenerated raw logs are kept
local, outside this checkpoint. Required licences/provenance, current test
source, compact summaries and physical records are included. Local credentials,
received data, generated binaries/builds and editor settings are excluded.


## 2026-10-30 - Three-nibble interval compatibility milestone

Scope: the compressed TIMEBASE interval is now 12 bits (1..4095 whole minutes).
UART CONFIG remains timestamp:u32 + interval_seconds:u32, little-endian; the
live adapter validates whole minutes (60..245700 seconds). Frame layout, CRC,
byte stuffing, ACK/retry/receive timing and duplicate handling are unchanged.
PC configuration textbox/network delivery to GP20 is NOT implemented here;
current GP20 still sends timestamp1700000000 and interval3600 seconds.

Compatibility: new encoder pages are `interval12`; old captures are `legacy16`.
Raw pages have no embedded format version. Both Python tools accept explicit
--log-format selection; receiver/CSV defaults remain legacy16 for existing
flashed images. The Sensor dump decoder defaults to interval12 and accepts
--log-format legacy16. New CSV rows include log_format. Wrong-format input
cannot generate a CSV in the tested cases; HTTP still preserves its raw binary.
Do not mix old/new pages in one flash log. No flash migration or erase is done.

| Validation type | Result | Scope |
| --- | --- | --- |
| Host shared UART/backend | PASS | Interval boundaries/golden bytes/no-room atomicity, stable source leases, baseline assertions, eight reliability groups |
| Host live RAM adapter | PASS | Invalid seconds rejected without mutation; 1/4095-minute boundaries; 605 slots over two pages; snapshots, capacity, duplicate/final-ACK recovery, retry exhaustion and CLEAR |
| Host PC CSV/receiver | PASS | 18 CSV tests and 17 receiver tests; explicit current/legacy formats, timestamps, CRC validation, CLI, downloads, immutable binary storage, lost receipt/dedup |
| Host HTTP client/network | PASS | Fragmented replies, bounded failures/retries, offline recovery and diagnostic reporting checks |
| Host combined workflow | PASS | Stable upload buffer, incomplete transfer rejection, retry ownership and Sensor-to-PC exact bytes over host TCP |
| SDK firmware builds | PASS | Pico SDK 2.2.0, pico_w/RP2040, normal and I2C-diagnostic Sensor images, -Wall/-Wextra, no new source warnings |
| Local RP2040 emulator | PASS | New normal image: ready and no-USB; new diagnostic image: init-short-write with later valid measurement; CONFIG/retrieval and exact interval12 prefix |
| Frozen UART emulator modes 0..4 | PASS | Existing binaries reused; both UART byte streams match saved 100-byte golden captures; baseline source unchanged |
| Physical hardware / Wokwi | PENDING | This format change has not been flashed or physically tested by the agent/user in this milestone |

Read-only compatibility checks also decode the existing six-reading physical
capture as legacy16: 28.25, 28.25, 30.25, 30.75, 28.50, 28.50 C. The new actual
ARM emulator snapshot validates as interval12 and decodes to its modeled 25 C.
This rechecks saved evidence; it is not a new physical test.

Changed files:
- sensor_pico/encoder.c and encoder.h: interval width, constants and range guard.
- sensor_pico/tools/decode.py: current/legacy interval widths and CLI selection.
- wifi_pico/tests/sensor_live_logger/live_logger.c and main.c: seconds-range guard
  and explicit startup format label; test_live_logger.c, run_tests.py and
  test_firmware.mjs: boundary/adapter/ARM checks updated for the new record width.
- shared/uart_protocol/tests/test_codec_transfer.c, run_tests.py and results.json:
  current codec fixtures and boundary/reliability evidence. Shared protocol source
  and the frozen uart_protocol project source are unchanged.
- wifi_pico/tools/temperature_csv.py and http_receiver.py: explicit decoding
  profile and CSV label; test_temperature_csv.py and test_http_receiver.py cover it.
- wifi_pico/README.md and this record: instructions/results. No extra Markdown file.

Sensor main/AHT10/RTC/flash code, Wi-Fi application source and active combined
Wi-Fi UF2 remain unchanged. SHA-256 checks also preserve all existing received
binary/CSV files. Old Sensor images and edited source originals were copied to
an external checkpoint before changes:
C:/Users/idesp/.codex/visualizations/2026/09/29/01a0eafd-f500-7250-864c-b3e0eb56dbd9/interval12-20261030/before/

New build commands from repository root:

```powershell
.\wifi_pico\tests\sensor_live_logger\build.ps1
.\wifi_pico\tests\sensor_live_logger\build.ps1 -I2CDiagnostics
```

New files (generated locally, not committed):
- wifi_pico/tests/sensor_live_logger/build/sensor_live_logger.uf2
  SHA-256: 95a78d91baa83f77aa4616e7243d3d506ead7a2dfffcf72c4facfa6973145a28
- wifi_pico/tests/sensor_live_logger/build/sensor_live_logger_i2c_diag.uf2
  SHA-256: 92e427d09675dbd23ce39a010be511ff27a1b93055db1fe44b0c208e94add2fb

The next physical check uses the new live Sensor RAM-only image with the
existing Wi-Fi combined image. The Sensor startup must say format=interval12.
Run the PC receiver with --decode-jz --timestamp-source simulated
--log-format interval12. GP20 takes a sample with the existing 60-minute
configuration; GP21 retrieves, and the next GP21 uploads. Verify the Sensor and
Wi-Fi lengths/CRC match, HTTP stores the same binary, and CSV shows the sample
with log_format=interval12. Use --log-format legacy16 for older saved logs.
No need to reflash Wi-Fi solely for this inner-format change. No physical test
is claimed here; the startup AHT10 initialization warning remains unresolved.

## 2026-11-01 - Interval12 physical test: Sensor startup

Evidence: user-observed COM6 output after flashing sensor_live_logger_i2c_diag.uf2;
not an agent-operated hardware test, emulator result or two-board transfer result.

Startup identification: PASS. USB reports format=interval12 (1..4095 minutes),
bare-metal RAM-only mode, 1024-byte capacity, flash=OFF, RTC=OFF and simulated
CONFIG timestamps. UART1 is GP4 TX / GP5 RX at 115200 8N1, session11223344.
Initial samples=0, running=0, frames=0, UART errors=1 and dropped=0 were reported.
The existing AHT10 warning remains: reset BA returned 1/1 bytes, init E10800
returned 1/3 bytes (short write), with SDA/SCL high before reset. This startup
check does not establish that a measurement will succeed or resolve that warning.
Next: press Wi-Fi GP20 once, confirm CONFIG ACK and one valid stored Sensor
sample. Interval12 UART retrieval, HTTP upload and CSV physical checks are pending.

### 2026-11-01 - Interval12 CONFIG and two-board retrieval

User-observed physical PASS: Wi-Fi GP20 once produced CONFIG ACK, sequence=1,
retries=0, and Sensor interval=3600 seconds/timestamp=1700000000 (simulated).
Sensor measured 25.17 C and stored its first sample. User then pressed GP21 once:
REQUEST_DATA ACK, 32 DATA blocks, validated TRANSFER_END, 256 received bytes,
received_log_valid=1 and completion_count=1; request sequence=2, retries=0.
Sensor snapshot1 and Wi-Fi completion1 both report CRC-16/X-25 e7a0. Sensor
reported snapshot released after the final ACK. User observed GP6 once for
CONFIG and twice for retrieval. No physical HTTP/CSV result is established yet.

Read-only host verification of the user's pasted 256-byte capture: CRC e7a0,
interval12 decoding yields one valid sample, q=261 / 25.25 C, simulated Unix
1700000000, with inner CHECK C600 valid. The difference from raw 25.17 C is the
existing 0.25 C quantization. This host analysis is separate from the physical
observations. The original AHT10 initialization warning remains unresolved,
although this physical measurement succeeded. Flash and RTC remain OFF.
Next: start the PC receiver explicitly using --log-format interval12, then
perform the second GP21 press for HTTP upload and verify the generated CSV.

### 2026-11-01 - Interval12 HTTP upload and CSV generation

User-observed physical PASS: PC receiver bound 192.168.137.1:8000 with JZ CSV
export enabled, timestamp_source=simulated and log_format=interval12. The next
Wi-Fi GP21 press uploaded the saved 256-byte log; HTTP201 on attempt1. Wi-Fi
reported upload complete with Sensor log retained (no automatic CLEAR).
PC reported one stored binary and a generated one-sample interval12 CSV.
ID: log-a8ce8847103d81256cc7f022f1475ac3b047680f506b7c5d5bbb251ef5a52686
Body SHA-256: a8ce8847103d81256cc7f022f1475ac3b047680f506b7c5d5bbb251ef5a52686
Files: http_received/7e852202bd6e7ab812fd49caaf1decd4dafb57b467557b9bc3d9ca2abd2ebcd2.bin
and the corresponding .csv. GP6 upload indication was not reported in this step.

Separate read-only host verification of those actual saved files: PASS. The
256-byte PC binary equals the entire user-pasted Wi-Fi UART capture byte for
byte, matches the reported SHA-256, and validates inner CHECK C600. CSV has
one row: sample1, 25.25 C, Unix1700000000, UTC2023-11-14 22:13:20,
SGT2023-11-15 06:13:20, simulated, status=ok, segment0/slot0/page0,
log_format=interval12. The raw Sensor reading was 25.17 C before existing
quarter-degree quantization. Browser download/view has not yet been reported.

The interval12 live AHT10 -> RAM encoder -> two-board UART -> Wi-Fi HTTP -> PC
binary/CSV generation physical milestone now passes for this one-sample log.
This does not establish the full interval range on hardware, flash retrieval,
RTC, sleep/wake/preamble or PC-configured interval delivery. Existing AHT10
startup short-write warning remains unresolved; the tested sample succeeded.


### 2026-11-01 - User confirmed interval12 CSV viewing

User opened the generated one-reading CSV in Excel and confirmed the information
was displayed correctly. Screenshot shows 25.25 C, simulated, status=ok and
log_format=interval12. This completes the browser/download/view check for the
preceding physical interval12 test. It is not evidence for the new PC interval
input feature described below.

## 2026-11-01 - PC interval input to GP20 CONFIG (physical validation pending)

Implemented: receiver page accepts and persists 1..4095 whole minutes. Clicking
Save interval does not send a Sensor command. Combined Wi-Fi GP20 performs a
bounded GET /config through the existing radio/HTTP owner, receives fresh PC Unix
time plus saved minutes, and calls the existing pico2_send_config() with minutes
multiplied by 60. The unchanged UART payload is timestamp:u32, interval_seconds:u32
(little-endian). No changes to framing, CRC, retries, timeout, DATA or duplicate
handling; no shared/frozen UART or Sensor code changed in this milestone.

HTTP settings use Content-Length:0 and headers X-Config-Version:1,
X-Config-Minutes and X-Config-Unix-Seconds. The client requires HTTP200, version1,
all unique required fields, a bounded integer interval and nonzero uint32 time.
It rejects incomplete/duplicate/malformed fields, overflow, bodies, chunking and
nonmatching pages. Network failures retain the existing three-attempt bound and
GETs cannot overlap log POSTs. No interval/time defaults are sent when the fetch
fails. Existing UART-only, loopback and known-byte HTTP target behaviour remains.
The PC saves only minutes in logger_settings.json; each GET generates a fresh
PC timestamp. PC clock accuracy is not validated, and no Sensor RTC is added.

Ownership/task design: same four application tasks and priorities/core placement.
Communication owns the pending GP20 operation, validates copied results and calls
UART APIs. The radio task alone uses lwIP, serializing GET and POST. The existing bounded
radio request/result message buffers carry tagged uploads or copied settings; no
additional kernel object is allocated. The course CNF_MAX_MBFID is four, and
setup still creates exactly four buffers (UI, status, radio request, radio result).
Radio alone owns its pending-request cache, and Communication alone owns its
pending-result cache. There is no shared writable settings buffer or extra task. UI commands are busy
while the fetch is pending; UART reception/ticks and duplicate-END ACKs continue.
GP20 success indication still requires the Sensor CONFIG ACK, not merely HTTP200.

| Check | Result | Scope |
| --- | --- | --- |
| HTTP client C host tests | PASS | Existing POST/receipt/retry tests plus GET fragmentation, 1/4095 limits, timestamp overflow, version/framing, 503 retry, exhausted timeout and GET/POST isolation |
| PC Python host tests | PASS | 18 CSV and 21 receiver tests; invalid input preserves previous settings, settings persist across restart, timestamp fresh, actual C socket GET interoperates with PC receiver |
| Actual IPC host test | PASS | Production setup/radio functions compiled against a four-buffer model: capacity, copied values, request/result routing, queue backpressure and notifications; no physical SMP claim |
| Combined C host tests | PASS | New GP20 path, conversion to 60/245700 seconds, timestamp equality, failed/invalid PC fetch sends no UART, fresh recovery, busy buttons, repeated END ACK during pending fetch completes only once; prior retrieval/upload cases retained |
| UART application host | PASS | All seven existing cases |
| Two-board UART host | PASS | All ten existing cases and exact binary comparator |
| Shared UART/backend host | PASS | Baseline assertions and eight reliability groups; current encoder fixtures |
| Frozen RP2040 emulator | PASS | Modes0..4, existing binaries reused; both UART streams match existing golden captures |
| Combined SDK/course build | PASS | Pinned SDK2.2.0/course2e8e6ad; RP2040; SMP=1, USB diagnostics, UART0 protocol, emulator disabled, four app tasks; application -Wall/-Wextra/-Werror; no compiler warnings in build log |
| New-feature physical test | PENDING | No agent flashing, reset, serial-port operation or hardware claim |

Build command from repository root:

```powershell
.\wifi_pico\build.ps1 -Target combined -PicoSdkPath "$env:USERPROFILE\.pico-sdk\sdk\2.2.0"
```

UF2: wifi_pico/course/build_make/wifi_pico_combined.uf2 (694784 bytes).
SHA-256: b605b7c1654ad08e1acd31bf715bb11d64e0ae3465165bfe28350742c0f0c4b7
ELF size: text344668, data2480, bss77872, total425020 bytes.
Build log: wifi_pico/course/build_make/combined-build.log.
Build/static report: wifi_pico/tests/evidence/combined-build.json.
Source/binary originals backed up outside the project under the Codex session
pc-config-20261101/before checkpoint before editing. SHA-256 verification keeps
Sensor source, shared/frozen UART source and existing received binary/CSV intact.
No commit or push was performed.

Changed files: combined_main.c (startup help), communication.c (asynchronous PC
fetch/validation and UART CONFIG), kernel_io.c and new pc_config.h (copied IPC),
upload.c (serialized radio-owner GET/POST), tests/firmware/http_post.c/.h (GET
using existing bounded HTTP transport), tools/http_receiver.py (form/persistence/
GET response) and temperature_csv.py (pc-clock label). Tests updated in
 test_http_post.c, test_http_receiver.py, test_combined.c, test_upload.c,
 http_socket_adapter.c, new test_radio_ipc.c, run_combined_host_tests.py and
 verify_http_build.py. The IPC test compiles actual production code with a bounded
 host kernel model; the build verifier enforces the four-message-buffer limit.
README and this record updated; existing generated test evidence refreshed.
No additional documentation file or course/kernel source modification.

Physical checks to guide individually (all pending for this new feature):
1. Restart the PC receiver with --decode-jz --log-format interval12
   --timestamp-source pc-clock --output ./http_received/pc_interval. Open its
   page, save 1 minute; terminal must confirm saved setting. Invalid0/4096 or
   fractional inputs must not replace it. Saving alone sends no UART command.
2. Flash only the Wi-Fi Pico with the new combined UF2. Restart Sensor with RUN
   for fresh test RAM while retaining its already-tested interval12 image.
   Its static old "demo/simulated"/3600-second banner is not updated in this
   milestone; use the actual CONFIG values printed on both boards.
3. GP20 once: Wi-Fi prints reading PC settings, interval1 (60 seconds), current
   PC Unix timestamp, sending CONFIG and Sensor ACK. Sensor prints matching
   CONFIG and one immediate sample. GP6 success flash follows Sensor ACK.
4. Wait at least65 seconds without another GP20: another stored sample should
   appear. GP21 retrieves; next GP21 uploads; CSV should contain at least two
   readings with consecutive timestamps60 seconds apart, interval12 and pc-clock.
5. Save2 minutes and GP20: new CONFIG120 seconds without another firmware build.
6. Stop receiver then GP20: bounded PC-fetch failure, three failure flashes,
   no UART CONFIG. Restart receiver and GP20: fresh recovery without board reset.
7. An unuploaded saved log or active upload still blocks a new CONFIG; current
   GP21 upload/retry and GP22 explicit clear policies remain unchanged.

Wiring stays Wi-Fi GP0 TX -> Sensor GP5 RX, Sensor GP4 TX -> Wi-Fi GP1 RX and
common GND; AHT10 Sensor GP0 SDA / GP1 SCL / 3V3 / GND. Sensor remains bare-metal,
live test RAM-only; flash, external RTC, sleep/wake and preamble remain pending.

### 2026-11-01 - PC interval form: user-observed result

PASS: user restarted the PC receiver on 192.168.137.1:8000 with output
http_received/pc_interval, timestamp_source=pc-clock and log_format=interval12.
After saving 1 minute on the browser page, the terminal reported:
[CONFIG] Saved interval=1 minutes; press Wi-Fi GP20 to apply
Separate read-only file check confirms logger_settings.json contains
{"interval_minutes": 1}. No Sensor command or physical GP20 fetch is established
by this step. Next is flashing the verified combined Wi-Fi image, then checking
its startup and network readiness before proceeding with CONFIG.
The UF2 still matches SHA-256 b605b7c1654ad08e1acd31bf715bb11d64e0ae3465165bfe28350742c0f0c4b7.

### 2026-11-01 - New PC-CONFIG Wi-Fi firmware startup: user observed

PASS startup/network readiness after user flashed the new combined Wi-Fi image.
COM5 identifies the GP20 CONFIG-from-PC-settings build, SensorEmulator disabled,
four application tasks, pinned course2e8e6ad, SMP1/cores2, UART0 protocol and
UART_FAULT0. UI reports tk_prc2/core1; Communication and Wi-Fi/HTTP owner report
tk_prc1/core0. Their first health reports show zero dropped bytes/faults/notify
errors. Kernel IPC setup progressed to running tasks. Join succeeded and DHCP
assigned 192.168.137.249; HTTP availability was announced. This does not yet test
the physical GET /config or Sensor CONFIG payload for PC-provided settings.

COM6 is still the unchanged interval12 RAM-only Sensor test image, with its
known AHT10 init E10800 short write. It reports samples1, running1, frames35,
UART errors2/dropped0 and latest25.17 C from the preceding test. These are
retained counters/data, not evidence of a new CONFIG after this Wi-Fi flash.
Next: reset only Sensor for empty RAM (samples0/running0), then test GP20.
Sensor's static simulated-time/3600-second labels remain legacy diagnostics;
actual CONFIG values will be checked against the new Wi-Fi output.

### 2026-11-01 - Clean Sensor reset before PC-CONFIG test

User-observed PASS: after resetting/reconnecting only Sensor, COM6 reports
samples0/errors0/running0/full0, capacity1024, UART frames0/rejected0/errors0/
dropped0, interval12, flashOFF and RTCOFF. This establishes fresh RAM without
mixing the previous simulated-time sample into the PC-clock trial. The same
AHT10 reset1/1 then init1/3 short-write warning remains; no new measurement has
yet been established in this clean run. PC-saved interval remains1 minute from
the preceding verified form step. Next: Wi-Fi GP20 once; compare PC, Wi-Fi and
Sensor interval/timestamp, require Sensor ACK and a stored reading.

### 2026-11-01 - PC interval -> GP20 -> Sensor CONFIG: physical PASS

User-observed COM5: GP20 once fetched saved PC settings with HTTP200 on attempt1,
then sent UART CONFIG with interval1 minute /60 seconds and timestamp1793529552.
Sensor ACK was received; command sequence1, retries0/limit3.
User-observed COM6: CONFIG interval60 seconds, timestamp1793529552, followed by
22.52 C stored=1/total1. User-observed PC terminal: GET returned interval1 minute
and the identical timestamp1793529552 (PC clock). Therefore the configured
minutes-to-seconds conversion and exact PC/Wi-Fi/Sensor timestamp delivery pass
on the physical two-board setup. GP6 indication was not reported in this step.
Sensor's fixed '(demo, no RTC)' suffix remains a legacy diagnostic label; no
external RTC was added. This run uses the PC-supplied timestamp.
Next: no further button presses; wait at least65 seconds after CONFIG and check
a second automatically stored sample. PC-clock retrieval/upload/CSV verification
and new PC-fetch failure/recovery tests remain pending.

### 2026-11-01 - Automatic sampling after one-minute PC CONFIG

User-observed PASS for continued automatic acquisition: after the single CONFIG
interval60/timestamp1793529552, COM6 showed four successful stored samples with
no additional CONFIG line: 22.52 C total1, 22.71 C total2, 22.73 C total3 and
22.57 C total4 (each stored=1). No repeated GP20 was required for these readings.
The sample lines carry no per-reading wall-clock timestamp, so they establish
continued automatic sampling, not an independently measured exact wall-clock
period. Encoded 60-second timestamp spacing will be checked after retrieval/CSV.
Next: GP21 once for UART retrieval; compare Sensor/Wi-Fi length and CRC and
require validated END/final ACK. Upload remains the following GP21 step.

### 2026-11-01 - PC-clock log retrieval: physical PASS

User-observed Wi-Fi GP21 retrieval: REQUEST ACK, 32 DATA frames ACKed, validated
TRANSFER_END, received_log_valid1, bytes256, blocks32, completion_count1, command
sequence2/retries0. Sensor snapshot1 reports the same256 bytes/CRC16-X25 4949,
then final-ACK release. Sensor continued with sample105 (22.03 C stored1), which
is subsequent live data and does not change the already captured Wi-Fi snapshot.
GP6 indication was not reported in this step.

Separate read-only host verification of all256 user-pasted UART bytes: CRC4949,
SHA-256 5dc01fac579dd762d67a09c080f140236d58e6b711a3ac539e9dcea6c54513b8.
Interval12 decoding yields104 valid sample slots, first Unix1793529552/22.50 C,
last Unix1793535732/22.00 C; every consecutive encoded timestamp is60 seconds
apart. The first four quantized values are22.50,22.75,22.75,22.50 C, matching
the earlier raw readings under the existing0.25 C quantization. Inner CHECK8237
validates. UTC span2026-11-01 10:39:12 to12:22:12 (SGT18:39:12 to20:22:12).
These are decoded CONFIG-based timestamps, not an independently verified RTC or
per-sample wall-clock measurement. Host analysis is distinct from user-observed
physical transmission. Next GP21 uploads this immutable104-sample snapshot;
PC-clock HTTP/CSV storage and viewing remain pending.

### 2026-11-01 - PC-clock 104-sample HTTP/CSV flow: physical PASS

User-observed second GP21 press uploads the saved256-byte snapshot with
ID log-5dc01fac579dd762d67a09c080f140236d58e6b711a3ac539e9dcea6c54513b8.
Wi-Fi reports HTTP201 on attempt1 and Sensor log retained. User observed GP6
one long success flash. PC reports identical SHA-256, stored256 bytes and
CSV samples104, timestamp_source=pc-clock, log_format=interval12.
Files under http_received/pc_interval use stem
334b19836a86d8d0bc97011a30ff55d453a7097b954b07af09d96fd4c759f000.

Separate read-only verification of actual saved files: PASS. The .bin exactly
equals all256 previously user-pasted UART bytes and matches the reported SHA.
CSV has104 rows, all status=ok, interval12 and pc-clock; each consecutive encoded
Unix timestamp increases60 seconds. First sample22.50 C, Unix1793529552,
SGT2026-11-01 18:39:12; last sample22.00 C, Unix1793535732,
SGT2026-11-01 20:22:12. The first four quantized temperatures match the raw
readings reported earlier. This is file verification, not an agent hardware test.

Physical happy-path milestone passes: PC save1 minute -> GP20 HTTP settings/time
fetch -> unchanged UART CONFIG60 seconds -> Sensor automatic AHT10/RAM logging ->
GP21 UART retrieval -> next GP21 HTTP POST -> PC exact binary and decoded CSV.
The current CSV's browser/download/view confirmation, changing interval without
reflash, invalid input and PC-fetch failure/recovery tests remain pending.
Real flash/RTC/sleep/wake/preamble integration remains outside this result.


## 2026-11-01 - Simplified user CSV (PC-only change)

User requested removing unix_seconds, log_format, segment, slot and page from
user-visible CSV. Default export now contains six columns: sample,
temperature_c, timestamp_utc, timestamp_sgt, timestamp_source and status.
Internal decoded slot/segment/page/Unix values remain available for reconstruction
and validation. Sample is the global export row number; slot is an internal
counter that can reset at a new TIMEBASE, so they are not interchangeable inside
the decoder. Explicit interval12/legacy16 parsing and all CRC checks remain.

Both temperature_csv.py and http_receiver.py support --diagnostic-columns to
export the former eleven columns on request. Timestamp source remains explicit
because PC clock, earlier simulated data and a future RTC are distinct sources.
No Sensor, shared protocol, Wi-Fi firmware, kernel, button or HTTP policy change.
Only the exporter, receiver, their existing Python tests and existing docs changed.

Validation: PASS20 CSV tests and21 receiver tests, plus retained C HTTP/network
host checks. Coverage includes current/legacy formats, identical readings in
normal/diagnostic views, SENSOR_ERR blanks/status, sample numbering across a
TIMEBASE slot reset, CLI defaults, HTTP downloads and optional diagnostic fields.
No new physical firmware test is claimed for this PC presentation change.

The user's Downloads/temperature-log-334b19836a86.csv matched the original
104-row PC CSV byte for byte. Before changing output, the full PC CSV and edited
source originals were backed up outside the project in the Codex session
csv-cleanup-20261101/before checkpoint. The PC's corresponding current CSV under
http_received/pc_interval was regenerated from its original256-byte .bin using
the new default. Verification confirms104 rows and exact equality of every
retained cell to the original, including both timestamps and all temperatures.
The .bin retains SHA-256
5dc01fac579dd762d67a09c080f140236d58e6b711a3ac539e9dcea6c54513b8.
The original downloaded copy and other earlier PC files are untouched. The same
CSV download URL now serves the regenerated six-column file. Restart the PC
receiver with its existing command to use the updated exporter for later uploads;
saved one-minute settings persist. User viewing of the new six-column download
has not yet been reported. The earlier104-row downloaded evidence remains valid.
No commit/push performed; no new Markdown file added.


### 2026-11-01 - Final user CSV layout: four columns

User requested sample, temperature_c, timestamp_sgt and status only. This is now
the default in both PC exporters. UTC and timestamp_source join Unix seconds,
segment, slot, page and log_format in optional --diagnostic-columns output.
No additional field is required for the current single-Sensor user view; status
retains failed-reading visibility. Timestamp reconstruction, source validation,
legacy/current decoding and original binary storage are unchanged.
PASS20 CSV tests and21 receiver tests plus existing HTTP/network host checks.
The existing104-row PC CSV was regenerated with exactly the four requested
columns; every retained value equals the prior six-column file. Diagnostic
export reproduces the original downloaded eleven-column CSV byte for byte.
Original binary SHA5dc01fac579dd762d67a09c080f140236d58e6b711a3ac539e9dcea6c54513b8
and original Downloads copy are unchanged. Before-edit files are backed up outside
the project in the Codex session csv-four-columns-20261101 checkpoint.
PC receiver restart is needed for future uploads; current download is refreshed.
No Pico code/build/flash action, commit/push or extra Markdown file.

### 2026-11-01 - Interval change without reflash: physical PASS

User saved2 minutes on the existing PC page and pressed Wi-Fi GP20. COM5 reports
HTTP200 on fetch attempt1, interval2 minutes/120 seconds, timestamp1793538100,
then Sensor CONFIG ACK with sequence36 and UART retries0/limit3. COM6 reports
CONFIG120 seconds with exactly1793538100, followed by21.44 C stored1/total144.
This confirms PC settings changes reach Sensor through the existing UART payload
without a firmware rebuild/reflash under this test procedure. The continued
sample total shows existing RAM readings were retained. GP6 was not reported.
User previously confirmed the refreshed PC receiver/CSV displays the requested
four-column layout. Earlier raw data and download preservation checks remain.
Next: stop only the PC receiver and press GP20 once, require bounded fetch
failure/no new Sensor CONFIG and three failure flashes; recovery is tested after
that observation. Sensor should continue sampling at the last accepted120 seconds.

### 2026-11-01 - PC receiver unavailable: physical failure handling PASS

User stopped only the PC receiver and pressed Wi-Fi GP20. COM5 reports settings
fetch attempts1/3,2/3,3/3, then connect timeout with HTTP0/attempts3 and
'[UI] CONFIG not sent: PC settings unavailable or invalid...'. User observed
GP6 three short flashes after failure. The bounded network-failure/UI indication
passes; Wi-Fi reports no UART CONFIG started and no demo fallback. A COM6 trace
was not supplied for this step, so no separate Sensor-side observation is claimed.
This is an expected injected PC-service outage, not evidence of a new UART fault.
Next: restart the same PC receiver/output folder (saved interval2 minutes), then
GP20 once and verify HTTP fetch/UART ACK/matching Sensor CONFIG120 seconds and
fresh timestamp without resetting or reflashing either board. Recovery pending.


### 2026-11-01 - PC receiver restart: no-reboot recovery physical PASS

User restarted only the PC receiver with the same output folder; saved interval2
minutes loaded without another settings save. On GP20, COM5 reports HTTP200 on
fetch attempt1, PC interval2 minutes/120 seconds and timestamp1793539267. UART
CONFIG was acknowledged with sequence37/retries0. COM6 reports the identical
interval/timestamp and a24.09 C reading stored1/total154. PC terminal independently
reports GET interval2 minutes/timestamp1793539267. Neither Pico was reset or
reflashed under the test procedure; Sensor count144->154 and UART command
sequence36->37 are consistent with continuity across the injected PC outage.
The sequence advancing only once is also consistent with no UART CONFIG sent
while the PC fetch failed. GP6 success indication was not reported in this step.

The PC-interval milestone's physical functional checks now pass: persisted
1..4095-minute input mechanism (1 and2 minutes physically exercised; range bounds
covered by host tests), fresh PC timestamp delivery, minute-to-second UART CONFIG,
automatic sampling, exact104-reading RAM/UART/HTTP/CSV flow, four-column CSV user
view, settings change without firmware update, bounded PC-unavailable failure and
recovery without resetting either Pico. The4095-minute boundary has host coverage,
not a physical multi-day wait. No independent RTC/clock-accuracy claim is made.
Sensor remains the RAM-only live AHT10 test; real flash-backed UART retrieval,
batched retrieval/upload, external RTC, sleep/wake and preamble remain pending.
The existing AHT10 initialization short-write warning remains unresolved despite
successful readings. No source code or firmware changed for recording this result;
no commit/push performed. Next development milestone is flash-backed UART with
bounded batches while retaining these tested RAM/standalone regression targets.


## 2026-11-01 - GP6 activity indication (physical validation pending)

User requested GP6 blinking while data transfers. Implemented in the Wi-Fi
application: GP6 blinks100 ms on/off while a GP21 UART retrieval attempt or HTTP
upload is active, including existing retry/backoff waits. CONFIG/PC-settings fetch
and CLEAR keep their existing finite result indications. Success/failure events
end the activity blink and use the same patterns: CONFIG200 ms, retrieval two
150 ms pulses/100 ms gap, CLEAR500 ms, upload700 ms, failure three100 ms pulses.
No artificial transfer delay, UART/HTTP wire change or reliability timing change.
Very short successful uploads may finish before multiple busy blinks are visible.

Status Task alone owns GP6. Communication sends a bounded activity event at most
once per250 ms while its UART operation or borrowed HTTP snapshot is active.
Events use the existing status queue. Each activity event expires after1000 ms;
a delayed old event is ignored, and losing a final indication cannot leave the
LED blinking forever. Heartbeats extend activity without restarting blink phase.
A bounded queue drain lets completion/failure supersede older activity events.
The same four application tasks and four kernel message buffers remain.

Files: app.h (named UI constants/Status owner-step declarations), communication.c
(activity events), status.c (non-blocking timing), combined_main.c (single startup
build marker), new tests/test_status.c (actual state-machine host tests),
tests/test_combined.c and run_combined_host_tests.py (actual workflow event checks).
Existing README and this record updated; no additional Markdown file. Sensor,
shared/frozen UART source, HTTP client and four-column CSV code are unchanged.

Host results: PASS actual Status Task timing with manual clock, preserved finite
patterns, heartbeat phase, stale events, expired indication and no stuck LED.
PASS UART/HTTP workflow activity, stop after UART timeout and upload failure,
new retry activity, success stop and no activity during PC CONFIG. Existing
combined ownership/retry/duplicate-END and exact host TCP tests pass. All seven
UART application and ten two-board host cases pass. These tests do not establish
physical LED timing or physical SMP behavior for the new image.

Build/static: PASS pinned SDK2.2.0/course2e8e6ad, RP2040 Pico W, SMP1, USB,
SensorEmulator disabled, four app tasks/four message buffers. No compiler
warnings/errors; own application compiles with -Wall/-Wextra/-Werror. Protected
SHA-256 checks confirm Sensor/shared/frozen UART source unchanged. Previous
working image/source backed up outside the project in Codex session checkpoint
transfer-led-20261101/before. No agent flashing/reset or commit/push performed.

Build from repository root:

```powershell
.\wifi_pico\build.ps1 -Target combined -PicoSdkPath "$env:USERPROFILE\.pico-sdk\sdk\2.2.0"
```

UF2: wifi_pico/course/build_make/wifi_pico_combined.uf2 (695808 bytes).
SHA-256: 6d8c7b9945742519e5bdeb1a07bcfb80bc79cef7a565c7ddc4168e1db244f07d
ELF size: text345252, data2480, bss77912, total425644 bytes.
Compared with the preceding working image, text grows584 bytes and bss40 bytes;
no dynamic allocation is added. Startup prints:
[BUILD] GP6 activity blink=100 ms on/off during UART retrieval and HTTP upload

Physical checks to guide individually: flash only the Wi-Fi Pico, confirm this
startup marker and existing Wi-Fi/UART readiness. GP20 CONFIG must retain its
normal ACK/success pattern. First GP21: activity blink while retrieving, then two
success flashes. Second GP21: blink while uploading, then one long success flash.
For a clearly visible sustained indication, a later test can stop only the PC
receiver after retrieval: upload waits/retries must blink, then end with three
failure flashes; saved bytes must remain available for the existing retry flow.
No need to change Sensor firmware, wiring, PC CSV format or stored PC settings.
Preamble/sleep work remains pending JZ's exact sleep-function confirmation.

### 2026-11-01 - Activity-LED image physical transfer flow; visual activity pending

User flashed the activity build; COM5 includes GP6 activity blink100 ms marker,
SMP1/cores2/four tasks, external Sensor and Wi-Fi ready192.168.137.249. GP20
fetched2 minutes/current PC timestamp1793542650 (HTTP200 attempt1) and Sensor
CONFIG ACK (sequence1/retries0). Sensor stored24.27 C as sample183.

Physical transfer functionality PASS: COM5 capture contains retrieval completion1,
upload HTTP201 on attempt1, then retrieval completion2. Both captures are256
bytes/32 blocks/CRC16-X25 cc17. Sensor snapshots2 and3 match that length/CRC and
both report final-ACK release. PC stored bodySHA256
0bae1516db707b70b636f2faf930325b8dbda508a9e31a3b99f37a325360f066,
with183 CSV readings in pc_interval/3d02495e0ceb7a428dbce4620171f14e7c075b6837dfa52b68d3f7ef7e86bebe.csv.
User described GP21 twice; the supplied COM5 transcript contains three accepted
GP21 events (retrieve/upload/retrieve). No button/debounce fault is inferred;
last captured workflow state is a saved log waiting to upload.

Separate read-only host verification PASS: both full UART snapshots are identical,
match the actual PC .bin exactly, CRCcc17 and inner CHECKD297 validate, and
interval12 decoding/actual four-column CSV each contain183 readings. Last encoded
reading is24.25 C under existing0.25 C quantization. These183 readings still fit
in one256-byte page, so their transfer size is unchanged from earlier shorter
logs. More readings within the same page do not lengthen UART transmission.

User has not yet confirmed sustained GP6 activity blink or result pulses on this
new image; no physical visual timing PASS is claimed. Next: stop only the PC
receiver and press GP21 once from the captured waiting-upload state. Busy blink
should continue through bounded HTTP connect/retry waits, then switch to three
failure flashes and retain the saved snapshot. This verifies visible upload
activity/wait indication, not sustained successful payload transmission. No
firmware change, generated samples or artificial transport delay is required.

### 2026-11-01 - GP6 sustained activity observed; error feedback usability

User confirms GP6 blinked continuously during the instructed upload retry/wait
check. This is user-observed physical confirmation of sustained activity
indication. User reports the final three-flash error pattern is easy to miss
because it follows continuous blinking. No fresh COM5 failure trace was supplied
for this observation; no additional transfer/recovery claim is made.
User proposes an error-only buzzer. Hardware/source inspection identifies the
Maker Pi Pico onboard piezo at GP18 with mute switch (Cytron Rev1.2 datasheet);
checked active Wi-Fi application pin assignments do not use GP18. Recommended
policy is silent activity/success/recovered retries, three short beeps only for
terminal user-operation failure, keeping the LED failure pattern as a visual
backup. Existing Status Task can own both outputs without a new task. This is
currently a recommendation: buzzer code, build and physical test are not yet done.
No firmware/source changes made for this design discussion.


### 2026-11-02 - GP18 terminal-error buzzer implemented; physical check pending

Approved error-only feedback implemented in the existing Status Task: three
100 ms, 2 kHz beeps with 100 ms gaps, synchronized with the existing three
failure LED flashes. Activity, successes and intermediate retries remain silent.
All existing LED timings and UART/HTTP behavior are preserved. No task, message
buffer, blocking tone delay or PWM interrupt was added. New valid activity or
result events cancel the previous tone; initialization and completion are silent.

Changed application files: app.h (adapter declarations), kernel_io.c (GP18 PWM
adapter using course BSP), status.c (non-blocking envelope), combined_main.c
(build marker). Extended tests/test_status.c and updated README.md/this record;
no new documentation files. Course sources, shared protocol sources, Sensor
sources and uart_protocol baseline remain unchanged from pre-buzzer state:
333 protected-file SHA-256 comparisons PASS. Existing unrelated local changes
were preserved. Pre-change files and working ELF/UF2 are backed up outside the
repository under the Codex visualization workspace, error-buzzer-20261102/before.

Hardware review: Cytron Maker Pi Pico Rev1.2 datasheet identifies GP18 and its
mute switch. GP18 uses PWM slice 1/A; no active application uses its other aliases.
The course USE_PTMR=0 profile leaves PWM in reset, so the adapter releases PWM
through the RP2040 atomic-clear alias, avoiding concurrent UART/radio reset races.
Only slice 1 is configured; divider 1, TOP 62499 and compare 31250/0 at 125 MHz.
The kernel uses SysTick; enabling course physical timers is guarded at compile
time until slice ownership is explicitly resolved. Tone generation needs no IRQ.
Sources: https://files.seeedstudio.com/products/103030396/Maker%20Pi%20Pico%20Datasheet.pdf
and https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf (atomic register
access and PWM).

Build/static PASS: pinned Pico SDK 2.2.0, Pico W RP2040, course commit
2e8e6ad480c2d84a369b12995694b9bf5e9ddfb3; actual SMP=1, USB console,
four application tasks, Sensor emulator disabled. No compiler warnings reported
in the final build log. From the repository root:

```powershell
.\wifi_pico\build.ps1 -Target combined -PicoSdkPath "$env:USERPROFILE\.pico-sdk\sdk\2.2.0"
```

UF2: wifi_pico/course/build_make/wifi_pico_combined.uf2
Bytes: 696832; SHA-256: 900aa25773bd165cfde7da08f3b503f12bc7e290e037ffc3a0f8145e2ed3f806
arm-none-eabi-size: text 345828, data 2480, bss 77912, total 426220.

Host PASS (ASAN/UBSAN): actual Status Task tests verify exactly three beep
onsets, 100 ms envelope timings, silent activity/success, cancellation and silent
reinitialization. Existing combined tests also PASS: UART failure/recovery,
completed-log ownership, repeated END ACK without double completion, PC CONFIG,
HTTP lost receipt/duplicate handling and exact Sensor-model-to-PC byte equality.
Existing UART host suite: 7/7 PASS. External-endpoint host suite: 10/10 PASS,
plus binary comparator positive/negative checks. These are host models, not new
RP2040 emulator runs or physical board results.

Physical buzzer result: PENDING. First step: flash only Wi-Fi Pico, unmute its
carrier buzzer, open USB serial and confirm the marker below with silent startup:

```
[BUILD] Error buzzer=GP18; three short beeps on final failure only
```

Then test one failure at a time: with PC receiver stopped, GP20 should exhaust
its existing settings fetch attempts, send no UART CONFIG and produce three
beeps/three short flashes only at final failure. Restart receiver; successful
GP20 should be silent with the usual success flash. After retrieval, stopping
receiver before the upload press should give silent activity/retries followed
by three failure beeps and retained upload bytes. Restart receiver and retry:
success should be silent and preserve the Sensor log. Do not use GP22 to induce
an error, since CLEAR changes the stored log. No hardware action was performed
by the agent. No commit or push was made.


### 2026-11-02 - Pre-SOF transport preamble; awake-peer milestone

Implemented at Wi-Fi uart_io.c's existing write adapter, combined image only:
32 bytes of 0x55 immediately precede every outgoing UART0 frame. This covers
CONFIG, REQUEST_DATA, CLEAR and their retransmissions, plus DATA/END ACKs and
NACKs. No preamble is sent for an HTTP-only upload press or a refused/invalid
command. Sensor replies are unchanged. The same Communication Task writes the
prefix and original stuffed frame in one bounded buffer/write; no new task,
dynamic allocation, sleep/gap or retry/timeout change. Prefix duration is about
2.78 ms at 115200 8N1. Existing GP6 activity and GP18 error buzzer are retained.

The original encoded frame is copied unchanged after the prefix: framing, CRC,
escaping, payload and session/sequence fields remain identical. Preamble bytes
never enter the reconstructed compressed log or HTTP body. The current shared
Sensor parser ignores bytes before SOF; it needs no new protocol code. Host
wire tests deliver every prefix byte to that real parser, rather than stripping
it from the simulated Sensor input. Frame-level test observers alone inspect
a stripped view for their existing counters/fault injection.

Changed this milestone: app.h (named provisional prefix constants), uart_io.c
(transport wrapper), combined_main.c (one startup diagnostic),
tests/test_combined.c and tests/run_combined_host_tests.py (focused regression),
README.md and this record. No new documentation files. Existing unrelated local
changes are preserved. 333 protected-file SHA-256 comparisons PASS: Sensor,
shared protocol and frozen uart_protocol sources are unchanged. UART-only and
single-board loopback image send paths remain unchanged (APP_COMBINED=0).
Pre-change files plus working buzzer ELF/UF2 are backed up outside the repository
in the Codex visualization workspace, uart-preamble-20261102/before.

Build/static PASS: Pico W RP2040, pinned SDK 2.2.0/course revision
2e8e6ad480c2d84a369b12995694b9bf5e9ddfb3, actual SMP=1/cores2/four application
tasks, external Sensor, USB diagnostics. No compiler warnings in the build log.
Command from repository root:

```powershell
.\wifi_pico\build.ps1 -Target combined -PicoSdkPath "$env:USERPROFILE\.pico-sdk\sdk\2.2.0"
```

UF2: wifi_pico/course/build_make/wifi_pico_combined.uf2
Bytes: 697344; SHA-256: 6dcfcaf0b5106f4f72e6bb93550f6776aa719ac5adaf9057a2fc30746bd72391
Size: text346060 data2480 bss77912 total426452.

Host regression PASS (ASAN/UBSAN): prefix exact byte/count/order before SOF;
awake Sensor CONFIG/retrieval/CLEAR; original plus three CONFIG retries each
prefixed; retry exhaustion/fresh CONFIG recovery; empty retrieval; HTTP-only
press emits no UART prefix. Existing combined cases PASS, including lost final
ACK/repeated END with exactly one completion, incomplete/oversize transfer
rejection, stable upload ownership, PC CONFIG limits and unchanged exact bytes
through HTTP/lost-receipt deduplication. UART-only host tests7/7 PASS;
external-endpoint host tests10/10 PASS plus binary comparator checks.
No new RP2040 emulator run or physical preamble test is claimed.

Sleep/wake result: NOT IMPLEMENTED/NOT TESTED. User reports JZ tentatively uses
sleep_goto_sleep_until; actual setup remains unavailable. Unmodified pico-extras
RP2040 implementation gates UART clocks. This prefix alone does not enable a
wake source or restore clocks/UART1. A measured wake/recovery interval and GPIO
wake setup still need the actual Sensor implementation. Current32-byte duration
is adjustable in app.h and provisional, with no guarantee for a sleeping peer.
The Sensor must remain awake during a command/transfer. Reference inspected:
https://github.com/raspberrypi/pico-extras/blob/master/src/rp2_common/pico_sleep/sleep.c

Physical next step only: flash Wi-Fi image, keep current live Sensor firmware
awake and unchanged, start USB monitoring and confirm:

```
[BUILD] UART preamble=32 bytes of 0x55 before SOF; sleep wake-up UNVERIFIED
```

Then, one test at a time, run normal PC CONFIG/retrieval/upload and compare
Sensor/Wi-Fi/PC length and integrity evidence; test CLEAR only after preserving
wanted logs. This verifies awake compatibility, not waking from sleep. No board
was flashed, reset or physically tested by the agent. No commit/push performed.


### 2026-11-02 - User-observed GP20 failure buzzer PASS

User flashed the combined image whose startup includes GP18 error buzzer and
32x0x55 preamble markers. Wi-Fi eventually connected at192.168.137.12.
With PC receiver stopped, a GP20 press produced PC configuration attempts1/3,
2/3 and3/3, then connect timeout (HTTP=0 attempts=3) and CONFIG-not-sent.
User explicitly reports three beeps and three GP6 flashes synchronized with
the beeps. Physical error-buzzer pattern PASS for final PC CONFIG fetch failure.
No new COM6 capture was provided for this failure. Silence on every retry,
successful CONFIG with the new image, upload error beeps and sleeping-Sensor
wake-up are not inferred from this observation. This is a user-observed
physical result, separate from host and build checks.


### 2026-11-02 - GP20 loading indication implemented; physical check pending

Requested user-facing change: accepted GP20 CONFIG operations blink GP6 slowly,
400 ms ON/400 ms OFF, throughout PC settings fetch and Sensor CONFIG ACK waits,
including retries. Loading is silent. Completion replaces loading with the
existing CONFIG success flash or three synchronized failure flashes/beeps.
GP21 retrieval/upload retains its100 ms activity blink. No artificial delays
or changes to command sequencing, timeout/retry values, UART frames/preamble,
HTTP behavior or Sensor code. Instant responses need not show a full blink.

The existing Communication Task sends a loading status while config_fetching
or an APP_CONFIG operation is active. The existing Status Task chooses the
blink period and shares existing250 ms heartbeats/1000 ms expiry. Heartbeats
preserve phase; expired events cannot restart loading. No new task, timer,
queue, dynamic allocation or buzzer hardware code. Existing priorities/core
assignments remain unchanged. UART-only/loopback images do not emit this new
CONFIG loading event (it is enabled only in APP_COMBINED application logic).

Files changed this milestone: app.h, status.c, communication.c, combined_main.c;
extended tests/test_status.c, tests/test_combined.c and
tests/run_combined_host_tests.py; existing README.md and this record. Existing
unrelated changes preserved. Pre-edit source/image backup is outside repository
under Codex visualization workspace gp20-loading-20261102/before.333 protected
file SHA-256 comparisons PASS for frozen UART, shared protocol and Sensor files.

Build/static PASS: pinned SDK2.2.0, Pico W RP2040, course
2e8e6ad480c2d84a369b12995694b9bf5e9ddfb3; actual SMP=1/cores2/four application
tasks, USB diagnostics, external Sensor. No compiler warnings in final build log.
Command from repository root:

```powershell
.\wifi_pico\build.ps1 -Target combined -PicoSdkPath "$env:USERPROFILE\.pico-sdk\sdk\2.2.0"
```

UF2: wifi_pico/course/build_make/wifi_pico_combined.uf2
Bytes:697856; SHA-256:167b0b539ed8d8578dddb70e24fb4ab0882b2062998394e49f383ad79f67cbd9
arm-none-eabi-size: text346220 data2480 bss77920 total426620.

Host PASS (ASAN/UBSAN): actual Status Task400 ms timing, silent loading,
heartbeat phase preservation, loading/transfer speed changes, stale events,
expiry, success transition and three-beep failure transition. Actual application
checks PC-fetch hold/failure without a UART command, loading through Sensor
CONFIG retries/exhaustion, stopping on final results and successful fresh retry.
Existing combined ownership, protocol/preamble, loss recovery, byte equality
and HTTP deduplication regressions PASS. Preserved UART host tests7/7 PASS;
external-endpoint host tests10/10 PASS plus comparator checks. No new RP2040
emulator or physical slow-blink test was performed.

New physical check PENDING: flash only Wi-Fi image and confirm startup marker:

```
[BUILD] GP20 loading blink=400 ms on/off while waiting for PC settings and Sensor ACK
```

With receiver stopped and Wi-Fi connected, GP20 should blink slowly and
silently through all existing fetch attempts; final failure stops loading and
produces three short flashes/beeps. Afterwards restart receiver, retry GP20,
and check silent loading followed by CONFIG success flash/ACK. Test one step
at a time. User's preceding three-beep physical PASS applies to the previous
image and is recorded separately above; it does not establish physical slow
loading behavior for this new image. No commit/push performed.


### 2026-11-04 - Source cleanup and shared library checkpoint

Behavioral changes: none intended. Active combined_main.c moved to main.c;
UART-only main.c preserved under tests/firmware/uart_main.c. Active HTTP
client/service and network_retry.h moved to the application root. The service
activation name is now http_service_enable. Build/test references were updated.

shared/uart_protocol now supplies real static archives to the course Make
profiles through library.mk. CMake endpoint aliases uart::sensor/gateway/wire
are available; both live Sensor CMake targets now link uart::sensor. Shared
wire/endpoint source, JZ Sensor source and pinned course source remain byte-exact
relative to the pre-cleanup working tree. No existing uncommitted work was reset.
Protected source comparison: 695 files PASS. All baseline source/fixture hash entries are unchanged.
Five obsolete guide presence entries were removed and the shortened baseline
README checksum was updated. This is explicitly documentation cleanup, not
weakened firmware or golden-wire comparison. All prior files were preserved in
the checksum-verified external INF2004_SE16_before_cleanup.zip (900 source and
evidence files). Historical startup/scan diagnostic projects and five guides
were removed from the active tree; their source remains in that archive.
Licences, course provenance/import audit, golden fixtures and test evidence
remain. No new Markdown guide was added. Current READMEs were shortened.

Build/static PASS using SDK2.2.0: combined, UART-only, Sensor loopback, known-byte
HTTP and SMP boot profiles, with actual SMP=1 and USB console checks. Normal
and I2C diagnostic live Sensor targets both build against uart::sensor.
The combined build is the final active profile.

Local RP2040 emulator PASS for baseline modes0/1/2/3/4; all five firmware builds
PASS and original golden UART byte comparisons remain strict. New baseline
results are in uart_protocol/build/regression/results; Wokwi was not run.

Host PASS: shared archive/backend and eight reliability groups; combined
workflow/byte equality/preamble/loading/buzzer; HTTP/client reporting/retries;
20 CSV tests and21 receiver tests;7 UART application cases;10 external-peer
cases and comparator checks; live adapter605 sample slots/two pages.
These are automated host/model results, not new physical evidence.

User-observed update recorded from chat: GP20 slow blinking through PC retries
followed by three synchronized error flashes and beeps PASS. Earlier RAM
end-to-end, timeout/recovery, duplicate, HTTP and SMP physical evidence remains
scoped to its recorded runs. No new physical or Wokwi test after cleanup.

Still pending: integrated flash retrieval/verified CLEAR and batching, external
RTC, sleeping-Sensor wake configuration/timing, long-hold flash CLEAR, AHT10
startup fix and stronger reconnect/stress validation. Repeat the ordinary
CONFIG/retrieve/upload flow on cleaned images before the lab demonstration.

A Word handover was created outside the repository and rendered for page review.
It covers current progress, code/file map, shared API, UART frames, HTTP/PC flow,
builds/wiring and unfinished work. No commit or push was performed.
Final combined UF2 SHA-256: 5259f5072fe7fe378fb0f067cc856c87577cbc10ea9f7b3bbaeae4a53b9e897d
