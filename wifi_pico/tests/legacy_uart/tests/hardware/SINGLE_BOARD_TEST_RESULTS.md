# Single-board hardware test record

Date: 2026-10-20 (Asia/Singapore).

Hardware: one Pico W on a Maker Pi carrier, with an AHT10 available.
Physical actions and observations are reported by the user; the assistant has
not operated or independently verified the hardware. These are **single-board
tests**, not two-board integration results.

## Setup and diagnostics

| Check | User-reported result | Status |
| --- | --- | --- |
| VS Code Serial Monitor interface | Initially blank with a service-worker error; closing all VS Code windows and reopening restored the controls. | Resolved |
| USB port identity | COM4 disappears when this Pico is unplugged. | Confirmed |
| Original codec firmware startup | Monitor opens COM4 but no firmware text appears, including after USB restart and removal of the AHT10. DTR was enabled. | Original cause unconfirmed |
| Standalone `button_led_test` | After flashing the button test, user confirms GP20 output and GP6 flashes of approximately 100 ms. | PASS: USB/button/LED diagnostic |
| Codec diagnostic firmware startup | User supplied all startup checkpoints, including UART setup, protocol/UI initialization, main-loop entry and the ready banner. | PASS: startup/USB visibility |
| AHT10 initialization in diagnostic image | Initialization write returned 1 instead of 3; banner reports FAILED. | FAIL: initialization transaction incomplete |

The standalone pass establishes that this board can run firmware and produce
USB serial output. It does not establish that the codec application starts
successfully or that AHT10/UART loopback wiring works.

## Requested codec tests

| Test | Status |
| --- | --- |
| Normal CONFIG and live-temperature retrieval | PASS for serial/decoded-data criteria: CONFIG, 13 DATA frames, 196 samples decoded, one completion; retrieval LED observation not reported |
| Command ACK timeout and recovery without reboot | PASS for serial/recovery criteria: three retries, exhaustion, reconnect and complete recovery of the same 196-sample log; individual LED observations not separately supplied |
| Receive inactivity timeout after REQUEST_DATA ACK and recovery | PASS for serial/recovery criteria with one-shot software frame suppression: receiver watchdog fired, invalid/empty result, then 26 samples recovered without reboot; LED observations not supplied |

The normal loopback's local sender-failure callback can abort reception before
the receive watchdog expires. The optional `CODEC_RX_TIMEOUT_TEST` instrumentation
below isolates the third test; a sender retry failure alone is not evidence that
the receive inactivity watchdog fired.

## Firmware investigation and diagnostic build

The user requested direct code investigation instead of further small wiring
checks. No physical cause has yet been confirmed. The original ARM firmware
reached its startup banner in the local RP2040 emulator with no sensor present.
Inspection of the SDK's initialization write path confirms it checks the
specified I2C timeout; a missing sensor does not inherently require a startup
hang. Neither result proves the behaviour of the physical board.

The original app printed its first banner only after I2C, UART, protocol and UI
initialization. An optional `CODEC_STARTUP_DIAGNOSTICS` build now waits for the
USB monitor to open, allows 300 ms for the host receiver, and flushes checkpoints
before and after startup operations. This distinguishes early startup stalls
from main-loop/serial problems. It also reports the raw AHT10 initialization
write result (three bytes acknowledged is not a full sensor self-test).

Only the codec application and its target-specific CMake configuration changed.
Protocol modules, encoding, button-test firmware and split targets are unchanged.
The option defaults OFF; normal builds continue to start without a USB monitor.
The currently prepared diagnostic image has the option ON and therefore waits
for the monitor before initializing the sensor, UARTs or buttons.

```text
cmake -S . -B build -G Ninja -DPICO_BOARD=pico_w -DCODEC_SAMPLE_INTERVAL=1 -DCODEC_STARTUP_DIAGNOSTICS=ON
cmake --build build --target sensor_codec_loopback_test --parallel 4
```

Prepared UF2: `build/sensor_codec_loopback_test.uf2`

SHA-256:
`8a25b46db3464c47f8e3c1e020f431ddb704d9b10039ed4cc0258f12f9855a12`

Source/firmware snapshots before this instrumentation and build/emulator logs
are under `build/codec-startup-debug/`. Before instrumentation, the UF2 checksum
was `0645f36349b23e625195b3ab59a3bb547472a04e402917730f0dad47e8445832`.

Software validation on 2026-10-20:

- Diagnostic target builds with Pico SDK 2.2.0; no build warnings reported.
- Existing native codec/transport tests: PASS.
- Existing eight UART reliability groups: PASS.
- New actual-ARM-binary emulator checks: PASS for absent sensor, stuck I2C
  transaction (SDK timeout -2), and a responding sensor model. All exercise a
  late USB connection, CONFIG and UART retrieval. The responding model returns
  a synthetic 25 C sample which is encoded, transferred and decoded.

Reproduce the new emulator cases:

```text
node tests/run_codec_firmware.mjs build/sensor_codec_loopback_test.bin absent build/codec-startup-debug/emulator-absent.log
node tests/run_codec_firmware.mjs build/sensor_codec_loopback_test.bin stuck build/codec-startup-debug/emulator-stuck.log
node tests/run_codec_firmware.mjs build/sensor_codec_loopback_test.bin ready build/codec-startup-debug/emulator-ready.log
```

These are emulator results, not physical sensor or USB-host verification.

## User-reported diagnostic startup result

The user subsequently supplied this physical single-board output:

```text
[BOOT] codec startup diagnostics v1; USB output ready
[BOOT] I2C0 setup begin: SDA=GP8 SCL=GP9, 100 kHz
[BOOT] AHT10 init write begin: address=0x38, timeout=5000 us
[BOOT] AHT10 init write returned 1 (3=ACKed all init bytes)
[BOOT] UART0 init begin
[BOOT] UART0 initialized; UART1 init begin
[BOOT] UART1 initialized; assigning GP0/GP1/GP4/GP5 and 8N1
[BOOT] UART setup complete; protocol contexts init begin
[BOOT] Protocol contexts ready; button/LED init begin
[BOOT] Entering main loop
Sensor codec loopback ready
AHT10 init: FAILED; sample interval: 1 seconds
GP20: CONFIG/start sampling; GP21: retrieve/decode; GP22: clear/pause
```

The diagnostic image reaches the main loop. This is not yet proof of a UART
exchange or successful temperature sampling. In the SDK 2.2.0 I2C write path,
a positive partial count of 1 means a data-byte NACK after the first byte;
it is not a receive-byte count or a timeout. The cause of that NACK remains
unconfirmed. No command has been changed merely to make initialization appear
successful. Next: use the existing GP20 CONFIG/measurement path and capture its
ACK plus sample/error output.

## User-reported CONFIG and live sampling result

After pressing GP20 and warming the sensor by hand, the user supplied:

```text
[UI] GP20 pressed
[UI] Sending CONFIG
[P2] CONFIG acknowledged
[UI] CONFIG acknowledged
[ENCODE] #1 29.86 C  ANCHOR  encoded=29.86 C
[ENCODE] #2 29.86 C  DELTA +0  encoded=29.86 C
[ENCODE] #3 29.86 C  DELTA +0  encoded=29.86 C
[ENCODE] #4 29.88 C  DELTA +0  encoded=29.86 C
[ENCODE] #5 29.92 C  DELTA +0  encoded=29.86 C
[ENCODE] #6 30.03 C  DELTA +1  encoded=30.11 C
[ENCODE] #7 30.14 C  DELTA +1  encoded=30.11 C
[ENCODE] #8 30.25 C  DELTA +2  encoded=30.36 C
[ENCODE] #9 30.38 C  DELTA +2  encoded=30.36 C
[ENCODE] #10 30.52 C  DELTA +3  encoded=30.61 C
[ENCODE] #11 30.61 C  DELTA +3  encoded=30.61 C
[ENCODE] #12 30.73 C  DELTA +3  encoded=30.61 C
[ENCODE] #13 30.71 C  DELTA +3  encoded=30.61 C
[ENCODE] #14 30.70 C  DELTA +3  encoded=30.61 C
[ENCODE] #15 30.64 C  DELTA +3  encoded=30.61 C
[ENCODE] #16 30.59 C  DELTA +3  encoded=30.61 C
```

Recorded as physical **single-board** results:

- CONFIG command/ACK over UART loopback: PASS.
- Live sensor reads and encoder output: PASS for the reported 16 samples.
- Sensor output responds to warming; absolute accuracy/calibration not verified.
- CONFIG LED indication: not reported in this run.
- The partial initialization-write failure remains recorded separately. It did
  not prevent subsequent measurement. The startup `FAILED` label describes the
  write result rather than overall sensor operation; calibration status has not
  been read/verified by this firmware.
- Log retrieval/decode and both timeout/recovery tests remain pending.

Next hardware action: reopen USB monitoring without resetting the Pico, then
press/release GP21 once to retrieve the current RAM snapshot. Closing the monitor
does not pause acquisition; a later snapshot may contain more than 16 samples.

## Test 1: normal CONFIG, live sampling and UART retrieval

The user supplied the GP21 capture in the attachment
`3543c5b0-f07d-4d7d-99cc-c7e1c1ad6945/Pasted text.txt`.
A verbatim copy is retained in
`build/hardware-results/20261020-codec-normal-retrieval.log`.

Observed in this physical single-board run:

- One GP21 request and REQUEST_DATA ACK.
- Thirteen accepted/ACKed DATA frames.
- Exactly one TRANSFER_END validation and one UI retrieval completion.
- 196 decoded sample rows, numbered 1 through 196.
- 100 compressed stream bytes plus the two-byte sample-count header: 102 UART
  log bytes, consistent with thirteen chunks of up to eight bytes.
- All first 16 decoded temperatures match the user's earlier encoded output,
  including the original anchor 29.86 C and subsequent 30.11/30.36/30.61 C deltas.
- No timeout, failure or invalid-log message in the capture.
- More than 16 samples is consistent with sampling continuing while the monitor
  was closed. This snapshot fills the configured 100-byte compressed stream.
- Retrieval LED behaviour has not been reported and is not claimed as verified.

Result: **PASS for Test 1's serial and decoded-data criteria**. This is not a
complete byte-for-byte source/destination comparison: the physical firmware does
not expose both binary buffers in this capture. It is not two-board integration.
The timestamps are the existing fixed CONFIG epoch plus one second per sample,
not evidence of external RTC operation.

Next: Test 2, disconnect only the GP0 -> GP5 command jumper, confirm bounded
REQUEST_DATA ACK timeout, reconnect and retrieve again without reset/reflash.
Test 3 remains pending and needs isolated receive-watchdog instrumentation.

## Test 2: command ACK timeout and recovery

The user reported expected results after disconnecting GP0 -> GP5 and reconnecting
it for a fresh GP21 retrieval, supplying attachment
`819e2347-a628-4fc8-a840-52746353ab61/Pasted text.txt`.
The raw capture is copied to
`build/hardware-results/20261020-codec-command-timeout-recovery.log`.
SHA-256: `e13591bb3f2ab48f962873bd787a9e74bbfc6a8865fbd0e9109699d4eaaca237`.

The capture contains a full request attempt with exactly three retransmissions,
then command retry exhaustion and UI failure. A later GP21 request receives its
ACK, thirteen DATA frames, one END validation and one successful completion.
All 196 decoded rows match Test 1 exactly. The capture begins with an additional
failure line from an attempt whose preceding context was not included; it is
not counted as a second complete failure test. No log timestamps were supplied,
so the exact physical timeout duration is not measured by this capture.

Result: **PASS for single-board timeout/recovery serial criteria**, using the
user's reported disconnect/reconnect sequence without reboot. The user described
the results as expected; failure/success LED patterns were not separately stated.

## Test 3 preparation: isolated receiver watchdog

`CODEC_RX_TIMEOUT_TEST` defaults OFF and applies only to the codec loopback target.
When ON, the first accepted REQUEST_DATA arms one-shot suppression of decoded
DATA/TRANSFER_END dispatch on the receiver side. The REQUEST ACK is delivered.
Both UART polling loops and existing sender/receiver timers keep running.
Only during suppression, the local sender-failure callback does not abort the
receiver. Thus its independent 2,500 ms no-progress watchdog can expire.

After failure, suppression is released automatically. The next GP21 retrieval
uses the normal path. It must be initiated after the three failure flashes end.
No shared protocol, AHT10 command, codec or wire-format change is involved.

```text
cmake -S . -B build -G Ninja -DPICO_BOARD=pico_w -DCODEC_SAMPLE_INTERVAL=1 -DCODEC_STARTUP_DIAGNOSTICS=ON -DCODEC_RX_TIMEOUT_TEST=ON
cmake --build build --target sensor_codec_loopback_test --parallel 4
```

Flash `build/sensor_codec_loopback_test.uf2`, keep both UART jumpers and the AHT10
connected, open USB monitoring, press GP20 and wait for actual encoded samples.
The first GP21 must show REQUEST ACK, then the receiver timeout/UI failure,
zero received bytes and invalid log status; it must not print a decoded table.
After three short GP6 flashes, a second GP21 must complete and flash twice,
without reset/reflash between these two retrieval attempts.

Preparing this image requires a reflash and therefore loses the old RAM samples;
the previous hardware captures are retained. The failure-to-recovery portion
itself must run without rebooting. Disable `CODEC_RX_TIMEOUT_TEST` for normal use
afterwards, or the first retrieval after every boot intentionally fails.

Physical Test 3 results were subsequently supplied and are recorded below.

Software validation of this preparation:

- RX-test image and normal (injection OFF) image: Pico SDK builds PASS, no
  warnings reported in their build logs.
- Actual ARM-binary emulator, `rx_timeout`: PASS. REQUEST ACK precedes the
  receiver's approximately 2,500 ms timeout, failure exposes invalid/zero-length
  receive metadata, and a later GP21 succeeds. GPIO model records six LED rising
  edges: one CONFIG, three failure, two recovery.
- Normal ARM-binary emulator, `ready`: PASS with injection disabled.
- Existing native codec/transport suite and all eight UART reliability groups:
  PASS. Shared Pico 1/Pico 2 protocol and stream-parser source hashes unchanged.
- This exercises software-suppressed transfer-frame dispatch with real firmware
  logic; it does not emulate every electrical UART failure.

The **RX-timeout test** image used for this run was built at
`build/sensor_codec_loopback_test.uf2` and is now retained at
`build/codec-rx-timeout-test/receive-timeout-test.uf2`.
SHA-256: `791aee1c0ad0db221b2e290f0cc926a69aa18e09e4189176fd326894bae32e5e`.
Logs are under `build/codec-rx-timeout-test/`.
The normal image, retaining startup diagnostics but with RX injection OFF, is
`build/codec-rx-timeout-test/normal/sensor_codec_loopback_test.uf2`.

## Test 3: physical receiver inactivity timeout and recovery

The user supplied a complete serial transcript in the conversation after two
GP20 presses and two GP21 presses. Both CONFIG exchanges were acknowledged.
The second CONFIG did not clear the existing log: sample numbering continued
from 5 to 6, as expected for the existing command behaviour.

First retrieval excerpts, in observed order:

```text
[UI] GP21 pressed
[UI] Requesting stored data
[TEST] Suppressing DATA/TRANSFER_END for this retrieval; REQUEST ACK is delivered
[P2] REQUEST_DATA acknowledged
[P1] Transfer timeout: retransmitted pending frame
[P1] Transfer timeout: retransmitted pending frame
[P1] Transfer timeout: retransmitted pending frame
[P1] Transfer timeout: retry limit reached
[P2] Receive timeout: no transfer progress
[UI] Data retrieval failed
[TEST] RX suppression released; received_log_valid=0, received_bytes=0
[TEST] Wait for failure flashes, then press GP21 again for normal retrieval
```

ENCODE messages were interleaved with the retries and continued after failure.
No accepted DATA, decoded table, END validation or completion appeared for this
failed retrieval. The sender's earlier failure did not prematurely abort the
receiver: the explicit receiver-watchdog message was observed. Exact elapsed
physical time cannot be measured from this transcript without log timestamps.

Recovery on the next GP21 press, with no intervening boot banner:

- REQUEST_DATA acknowledged; three DATA blocks accepted/ACKed.
- One TRANSFER_END validation and one UI completion.
- 26 samples decoded; 15 compressed bytes plus two-byte count header = 17 bytes,
  consistent with three eight-byte chunks (last chunk partial).
- First sample is the 29.91 C anchor; samples 2–26 are DELTA +0, each decoding
  to 29.91 C. These match every corresponding earlier encoded value in the
  supplied transcript. Differences from raw temperature reflect quantization.
- New samples 27–33 appear after completion, showing acquisition remains active.
- No reboot/reflash is shown between failure and recovery. LED patterns were
  not included in the user's report and are not claimed as physically observed.

Result: **PASS for physical single-board receive-watchdog and recovery criteria,
using software suppression of decoded transfer frames**. This does not establish
two-board fault recovery or arbitrary electrical UART error handling.

## Milestone status after the three runs

All three requested single-board scenarios pass their serial/data criteria.
The LED patterns have software/emulator coverage; physical observations were
not separately supplied for these codec runs. Temperature accuracy/calibration,
the partial AHT10 init-write NACK and the original normal-image USB startup
cause remain unconfirmed. Repeated-END/final-ACK-loss handling has host regression
coverage but was not deliberately injected in these three physical runs.

The active build was restored with `CODEC_RX_TIMEOUT_TEST=OFF`, retaining
startup diagnostics and one-second sampling. Its normal UF2 is
`build/sensor_codec_loopback_test.uf2`. Flashing that image resets the RAM log;
the recovery test itself had already completed without reboot. The user's
subsequent normal-image run is recorded below.

The restored normal target builds without warnings and passes the actual-ARM
`ready` emulator case. Its UF2 SHA-256 is
`8a25b46db3464c47f8e3c1e020f431ddb704d9b10039ed4cc0258f12f9855a12`.
This also matches the previous startup-diagnostic image used for Tests 1 and 2.
Restoration build/emulator logs are under `build/codec-rx-timeout-test/`.

No RTOS, HTTP, flash or RTC integration was started during this milestone.

## Normal-image restoration: subsequent physical confirmation

The user supplied attachment
`db68d87c-5722-45f5-8e8e-503b1867b8a6/Pasted text.txt` after flashing the normal
image. Raw capture: `build/hardware-results/20261020-codec-normal-restored.log`.
The capture contains the earlier timeout-test session, followed by a USB
restart and a new normal session. Only the session after the last boot banner
is counted for this restoration check.

- No RX-timeout injection banner in the new session.
- Two CONFIG commands acknowledged, with sample numbering continuing.
- First GP21 succeeds immediately: eight samples, six compressed bytes plus
  two-byte header, one DATA frame, one END validation/completion.
- Second GP21 succeeds immediately: eleven samples, eight compressed bytes plus
  two-byte header, two DATA frames, one END validation/completion.
- Acquisition continues after both retrieves; no automatic clear or intervening
  reboot appears between them. No UART failure appears in this new session.
- AHT10 initialization still reports partial write result 1 / FAILED, followed
  by successful measurements. This remains an open sensor initialization issue.

Result: **PASS for repeated normal single-board retrieval after restoring the
normal image**. The final capture does not test HTTP POST: this executable links
USB, UART and I2C, with no Wi-Fi, network client, kernel tasks or HTTP uploader.
The local decoded table is produced by the codec test callback on the Pico.
USB serial delivery of text is not evidence of binary delivery to a PC server.
