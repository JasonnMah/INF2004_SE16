# GP20 CONFIG, GP21 retrieval and GP22 CLEAR_LOG integration test

Single Pico W on a Maker Pi Pico carrier: UART0 is Pico 2 and UART1 is the
Pico 1 test endpoint. GP22 explicitly clears the logical log. No physical or Wokwi runtime test
was performed for this change.

## Build, flash and connect

From the project root in the configured Pico SDK 2.2.0 terminal:

```text
cmake -S . -B build -G Ninja -DPICO_BOARD=pico_w -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target pico2_ui_protocol_test --parallel 4
```

Flash `build/pico2_ui_protocol_test.uf2` via BOOTSEL / RPI-RP2.
The debugger image is `build/pico2_ui_protocol_test.elf`; `.bin` is also generated.
The separate original firmware remains `build/uart_protocol.uf2`.

Keep these two jumpers on the same board:

| From | To |
| --- | --- |
| GP0 (UART0 TX) | GP5 (UART1 RX) |
| GP4 (UART1 TX) | GP1 (UART0 RX) |

The built-in GP20/GP21/GP22 buttons and GP6 indicator need no extra wiring. GP6 is
the carrier LED, not the Pico W onboard LED. Leave its external connection free.
Connect a USB data cable and open the Pico USB COM port in a serial terminal
(e.g. PuTTY) at 115200, 8N1, no flow control. Diagnostics use USB only.
The banner prints on connection and reconnect without blocking the polling loop.

```text
Pico 2 UI protocol test ready
GP20: send CONFIG
GP21: retrieve data
GP22: clear log
```

Press GP20:

```text
[UI] GP20 pressed
[UI] Sending CONFIG
[P2] CONFIG acknowledged
[UI] CONFIG acknowledged
```

GP6 flashes once for 200 ms. After the indication, press GP21:

```text
[UI] GP21 pressed
[UI] Requesting stored data
[P2] REQUEST_DATA acknowledged
[P2] DATA block received and ACKed
... 13 DATA block diagnostics in total ...
[P2] TRANSFER_END validated
[P2] Retrieved compressed log matches Pico 1 data
[UI] Data retrieval complete
```

Other existing Pico 1 diagnostics may appear between these lines. GP6 flashes
twice for 150 ms, separated by 100 ms off. Release and press GP21 again after
the indication to retrieve the same 100 bytes again. Retrieval never sends CLEAR_LOG automatically.

Press GP22 after the indication finishes:

```text
[UI] GP22 pressed
[UI] Clearing stored log
[P2] CLEAR_LOG acknowledged
[UI] Log cleared
```

GP6 flashes once for 500 ms. Pico 1 sets its logical logger length to zero and
resets its transfer state. Its source pointer and the caller-owned 100 source
bytes remain untouched; no flash erase or source-buffer overwrite is performed.
Repeating GP22 on an already-empty log is safe. CONFIG does not refill the log;
restarting this test firmware initializes the original mock buffer again.

After clearing, release and press GP21 again:

```text
[UI] GP21 pressed
[UI] Requesting stored data
[P2] REQUEST_DATA acknowledged
[P2] TRANSFER_END validated
[P2] Retrieved log is empty (0 bytes)
[UI] Data retrieval complete
```

There are zero DATA frames. The existing zero-block TRANSFER_END is validated
and ACKed; no new wire message is used. The existing retrieval wrapper resets
receive length/block count before requesting, so old buffer contents are not
reconstructed or reported as current data. GP6 still gives two 150 ms flashes.

Before the first successful CONFIG, GP21 or GP22 prints `[UI] Configure first using GP20`
and sends nothing. Each button uses active-LOW input with pull-up, 30 ms stable
press and release debounce, and one event per hold. Busy presses are consumed,
not queued, including during LED indications. Simultaneous presses prioritize CONFIG, then retrieval, then clear; lower-priority
buttons require a fresh press.

Failure prints `[UI] CONFIG failed`, `[UI] Data retrieval failed`, or
`[UI] CLEAR_LOG failed`, once per
operation, with three 100 ms pulses separated by 100 ms off. All LED and debounce
handling is non-blocking. Existing 500 ms timeouts and maximum three retries
remain unchanged. A fresh press after release and indication can retry.

## API and completion

- CONFIG still calls `pico2_send_config(pico2_ctx_t *p2)`. UI success requires
  its outstanding CONFIG operation, `P2_COMPLETE`, and no pending command.
  The existing ACK handler validates session, payload and acknowledged sequence.
- GP21 calls `pico2_request_data(pico2_ctx_t *p2)`. This wrapper resets the
  receive length and expected block to zero and calls the existing tracked
  REQUEST_DATA command with start block zero. Busy states reject the wrapper.
- Retrieval success requires its outstanding UI operation, `P2_COMPLETE`, and
  no pending command. The existing TRANSFER_END handler validates the block
  count and calls the test's `validate_log` hook before acknowledging and completing.
  This hook compares length and bytes against Pico 1's CURRENT logical log,
  including zero length after clear. The completion hook logs a match or an
  explicit empty result and sends no further command.
- GP22 calls `pico2_clear_log(pico2_ctx_t *p2)`. The wrapper accepts idle/failed
  contexts and reuses `pico2_send_command(..., MSG_CLEAR_LOG, NULL, 0,
  P2_WAIT_CLEAR_ACK)`. No framing or retry logic is added to the UI.
  The existing CLEAR_LOG ACK handler validates the reply and sets `P2_COMPLETE`.
  The UI requires its `clear_pending` flag, that completed state, and no pending
  command before printing success and ending the operation. Missing ACKs follow
  the existing retry/failure path; failure does not prove the endpoint was not
  cleared, because an ACK can be lost after the clear took effect.
- The test endpoint borrows the unchanged 100-byte mock buffer; 8-byte chunks
  yield 13 frames (12 full chunks and one 4-byte chunk). Receiver capacity is
  separately named `DEMO_RECEIVE_CAPACITY` (256 bytes).
- No HELLO is required by the existing CONFIG handler. Both contexts use the
  established session 0x11223344, timestamp 1700000000 and interval 3600 seconds.

The only Pico 1 addition is an optional `transfer_failed` notification after
its existing timeout retry limit is reached. The single-chip test endpoint
maps that notification to receiver failure when still receiving. It sends no
new frame and does not change retries. The original demo leaves it NULL.
This is a local test notification, not a receive timeout or failure signal for
a future two-board deployment; that stage is outside this task.

Existing wire behavior is unchanged. The original demo still automatically
runs its command sequence and CLEAR_LOG; the UI target uses its own completion
hook instead of the demo's auto-clear hook. No protocol handlers or frame
construction are duplicated in the UI.

## Validation on 2026-10-01

```text
cmake --build build --target uart_protocol button_led_test pico2_ui_protocol_test --parallel 4
python tests/run_host_tests.py
python tests/run_ui_tests.py
python tests/run_matrix.py --results-dir build/gp22-regression-results
```

| Check | Result |
| --- | --- |
| All three firmware targets, SDK 2.2.0 / pico_w | PASS, no build warnings |
| Existing host protocol checks | PASS |
| Extended host UI checks, -Wall -Wextra -Werror | PASS |
| Mode 0 happy path | build PASS, emulator PASS |
| Mode 1 corrupt first DATA | build PASS, emulator PASS |
| Mode 2 drop first DATA ACK | build PASS, emulator PASS |
| Mode 3 wrong first block | build PASS, emulator PASS |
| Mode 4 drop first CONFIG ACK | build PASS, emulator PASS |
| Physical hardware / Wokwi runtime | NOT RUN |

All five emulator runs matched both existing checksum-protected 100-byte UART
recordings byte-for-byte. The golden files and five-mode runners were unchanged.
Evidence: `build/gp22-regression-results/matrix.json` and accompanying logs.

Host UI tests exercise real framing, parsing, both peers, reliability and UI;
only GPIO, transport and time are substituted. Checks cover pre-CONFIG gating,
CONFIG enabling retrieval, full 100-byte equality, exactly 13 DATA frames,
second retrieval, no automatic CLEAR_LOG, all three buttons' hold/release
behavior, busy presses during CONFIG/DATA/CLEAR_LOG, receiver reset, sender
retry exhaustion and recovery, ACK/NACK validation, CONFIG retries and LED timing.
New checks cover GP22 before CONFIG, the CONFIG -> 100-byte retrieve -> clear ->
empty retrieve sequence, exact logical length and source-byte preservation,
zero DATA frames and valid TRANSFER_END after clear, rejection of stale data
by the test validator, repeated clear, lost CLEAR_LOG ACK retry, exhaustion,
recovery and the 500 ms success / three 100 ms failure indications.

## Files modified in this GP22 extension

- `src/pico2/pico2_ui.c` and `.h`: GP22 debounce, CONFIG prerequisite, clear
  operation tracking, request/result reporting and 500 ms success indication.
- `src/pico2/pico2_protocol.c` and `.h`: public `pico2_clear_log` wrapper.
- `tests/hardware/pico2_ui_protocol_test.c`: banner, clear timeout ticks,
  comparison against current logical source length, and empty-result message.
- `tests/ui/test_pico2_ui.c`: extend integration checks while retaining previous checks.
- `tests/hardware/PICO2_UI_PROTOCOL_TEST.md`: this guide and validation record.

No new source files or build-system changes were needed. `src/main.c`,
`tests/hardware/button_led_test.c`, Pico 1, mock logger bytes, framing/reliability
modules, original demo, regression runners and fixtures remain unchanged.
GP20 behavior and GP21 non-empty/repeated retrieval remain unchanged. The new
user-controlled clear makes subsequent retrieval empty by design. No wire or
protocol behavior changed, and no automatic clear was added to this UI target.
