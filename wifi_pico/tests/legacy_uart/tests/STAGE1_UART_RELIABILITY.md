# Stage 1 UART reliability milestone

Date: 2026-10-19. Scope: UART fixes and validation only. RTOS and HTTP integration
have not started. No physical hardware or Wokwi runtime tests were performed.

## Inspected implementation

- `src/protocol/`: CRC, frame encoding/decoding, byte parser and tracked retries.
- `src/pico1/`: borrowed RAM data source, commands, stop-and-wait DATA/END sender.
- `src/pico2/`: command/retrieval receiver plus separate GP20/21/22 UI.
- `src/main.c` and `src/demo/`: original dual-UART five-mode regression demo.
- `src/apps/wifi_pico_main.c`: physical Wi-Fi-side UART/UI loop; currently
  bare-metal, with a 256-byte receive buffer, no networking and no temperature decoder.
- `src/apps/sensor_pico_test_main.c`: separate mock-RAM Sensor endpoint.
- `src/apps/sensor_codec_loopback_main.c`: separate live-sensor/codec experiment;
  its decoder is not part of the physical Wi-Fi firmware.
- CMake retains all six targets. The course kernel template was not changed.

User-reported two-board RAM transfer remains the physical baseline. Flash,
external RTC, dormant UART wake-up, HTTP and physical error recovery remain
unverified/unimplemented as previously recorded. The codec tests below verify
the existing experiment only; they do not establish JZ's final encoding format.

## Confirmed issues and focused corrections

| Previous problem | Correction |
| --- | --- |
| No receiver deadline after REQUEST_DATA ACK | Fail after 2,500 ms without a new accepted DATA block; also covers waiting for END. |
| DATA/END accepted outside retrieval | Only process new transfer content in `P2_RECEIVING_DATA`. Early DATA/END while waiting for REQUEST ACK are ignored; existing request/data retries recover a lost ACK. |
| Repeated END reruns completion | Cache the completed END sequence/count; ACK repetitions without changing state or invoking completion again, including during a later command/retrieval. |
| Previous block index alone counted as a duplicate | Require matching sequence, length and stored bytes before ACKing it as a duplicate. |
| Partial buffers remain after failure; overflow can hang | Abort on receive capacity/block-count exhaustion; reset partial length/count and validity; a fresh request starts cleanly. |
| Failed command remains tracked | Abort deactivates pending retries, preventing late ACK revival and repeated timeout handling. |
| Truncated/missing-delimiter traffic can leave the parser misaligned | Ignore adjacent delimiters as empty boundaries; preserve a new delimiter when abandoning an oversized candidate. |
| `P2_COMPLETE` also means CONFIG/CLEAR success | Add `received_log_valid`, set only after END validation; completion runs once per retrieval. |

Timing is derived from the unchanged **500 ms ACK timeout and three retries**:
the sender transmits at approximately 0, 500, 1,000 and 1,500 ms and exhausts
its budget at 2,000 ms. The receiver allows one extra interval, totaling 2,500 ms.
Duplicates, malformed traffic and out-of-order blocks do not extend this deadline.
A valid empty retrieval is REQUEST ACK followed by END with count zero.

Wire layout, IDs, CRC, byte stuffing, chunk size, baud rate and existing retry
settings are unchanged. Changes affect previously faulty rejection/failure paths;
the five existing golden traces remain identical. Zero-byte DATA blocks are now
rejected: empty logs use END, as the existing Sensor sender already does.

No Sensor wire-format or encoder change is required from JZ. The shared parser
fix should also be incorporated when rebuilding his Sensor firmware to improve
recovery of damaged incoming commands. Sensor command/DATA logic is unchanged.

## Completion boundary and remaining limits

`pico2_abort()` stops tracking and clears partial metadata, without clearing
Sensor logs. RAM bytes are not erased, but their public length is zero and
`received_log_valid` is false. Starting another retrieval invalidates the previous
result. The two single-Pico failure callbacks now call this same abort helper.

Future upload code must use the completion callback/`received_log_valid`, not
`P2_COMPLETE` alone, and must copy or reserve the completed buffer before allowing
another retrieval. This milestone does **not** implement HTTP, buffer ownership
across tasks, RX interrupts or SMP synchronization. Existing split firmware still
polls UART and must keep calling the receiver tick while retrieval is pending.

The existing fixed session ID and 16-bit sequences do not provide a new transfer
token or reboot handshake. These fixes are not general replay protection across
endpoint restarts/sequence reuse. END validates block count and per-frame CRC;
the physical Wi-Fi target does not know the original source bytes or a whole-log
hash. Physical UART overrun/framing flags and electrical faults still need hardware
testing. No automatic CLEAR_LOG was added; the original demo's existing automatic
CLEAR_LOG remains confined to that demo.

## Validation evidence

| Check | Result |
| --- | --- |
| New native reliability suite | PASS: eight groups; each group reproduced a failure before fixes. |
| Existing native protocol suite | PASS, including 513-byte/empty transfers and rejection checks. |
| UI integration suite | PASS; added separate-board-style receive timeout, failure LEDs and fresh GP21 recovery without the local Sensor failure callback. |
| Codec and codec/UART suites | PASS; existing encoding code unchanged. |
| Split-target structure/output checks | PASS. |
| Six existing Pico SDK targets | PASS; ELF, BIN and UF2 present for each. |
| Modes 0, 1, 2, 3, 4 builds | PASS for all five. |
| Modes 0, 1, 2, 3, 4 local RP2040 emulator | PASS for all five; both UART byte streams identical to protected 100-byte golden captures. |
| Wokwi / physical hardware | NOT RUN for this milestone. |

New fault coverage includes no first DATA, partial transfer, missing END, single
and persistent corruption, truncation/missing delimiters, oversized parser input,
lost REQUEST/DATA/END ACKs, all END ACKs lost, changed duplicate payload/sequence,
out-of-state input, retry exhaustion, overflow, late completion and fresh recovery.
The lost REQUEST ACK case also tests both endpoint tick orders. All END ACKs lost
can exhaust the Sensor's retries even though the receiver has the complete bytes;
receiver completion still occurs only once and no automatic clear follows.

Toolchain: Pico SDK 2.2.0, pico_w/RP2040, Arm GCC 14.2.Rel1, CMake 3.31.5,
Ninja 1.12.1, Debug. Native tests compile with `-Wall -Wextra -Werror`;
firmware uses `-Wall -Wextra`. No warnings appeared in the firmware build logs.
The Windows sandbox initially prevented Arm GCC from launching; builds succeeded
outside that sandbox using the same installed compiler, without source workarounds.

Generated evidence stays under `build/`:

- `reliability-before-20261019/`: source snapshot, SHA-256 manifest, failing-test output.
- `reliability-results/matrix/`: five build/emulator logs, wire captures and `matrix.json`.
- `reliability-results/`: six-target build log, reliability and UI host logs.

## Changed files

| File | Purpose |
| --- | --- |
| `src/pico2/pico2_protocol.c`, `.h` | Deadline, state guards, duplicate identity, completion validity/cache and shared abort API. |
| `src/protocol/stream_parser.c` | Recover delimiter alignment after damaged/truncated input. |
| `src/apps/sensor_codec_loopback_main.c` | Local failure callback uses `pico2_abort()`. |
| `tests/hardware/pico2_ui_protocol_test.c` | Same local failure cleanup. |
| `tests/test_protocol.c` | Establish an acknowledged retrieval before injecting DATA; retain malformed/count/validator/overflow checks with new abort expectations. |
| `tests/ui/test_pico2_ui.c` | Abort helper plus separate-board-style timeout/recovery regression. |
| `tests/test_reliability.c`, `tests/run_reliability_tests.py` (new) | Deterministic endpoint/byte-stream fault tests. |
| `README.md`, this new document | Latest milestone pointer, evidence and handover notes. |

SHA-256 comparison confirms `src/main.c`, `pico1_protocol.c/.h`, the frame/CRC/retry
modules, codec modules, split firmware entry points, UI implementation,
`tests/hardware/button_led_test.c`, CMake, Wokwi files and golden fixtures are
unchanged. Existing targets and test selectors remain available.

## Reproduce

From the project directory, with native GCC and the Pico SDK tools on PATH:

```text
python tests/run_reliability_tests.py
python tests/run_host_tests.py
python tests/run_ui_tests.py
python tests/run_codec_tests.py
python tests/run_matrix.py --results-dir build/reliability-results/matrix
cmake --build build --target uart_protocol button_led_test pico2_ui_protocol_test wifi_pico_firmware sensor_pico_protocol_test sensor_codec_loopback_test --parallel 4
python tests/check_split_targets.py
```

The active `build/` retains its previous mode 4 selection; the matrix independently
builds modes 0–4 under `build/regression/`. No golden capture was regenerated.

## Next physical tests (to be performed by the team)

1. Flash `build/wifi_pico_firmware.uf2` on the Wi-Fi-side board. Keep the known
   working Sensor firmware/data source. Connect Wi-Fi GP0 TX -> Sensor GP5 RX,
   Sensor GP4 TX -> Wi-Fi GP1 RX, and common GND. Open Wi-Fi USB serial.
2. Press/release GP20, then GP21. Expect CONFIG ACK, successful retrieval and two
   GP6 success flashes. Do not press GP22 during these data-preservation tests.
3. Disconnect Wi-Fi GP0 -> Sensor GP5 before another GP21 press. Expect command
   retry exhaustion at about 2 seconds, `[UI] Data retrieval failed`, three short
   GP6 flashes, and no completion. Reconnect, release/repress GP21 after the LED
   pattern ends; retrieval should succeed without rebooting or clearing the log.
4. To exercise the **new receive timeout**, pause only the Sensor in a debugger
   after it sends the REQUEST_DATA ACK but before DATA. Expect
   `[P2] Receive timeout: no transfer progress` about 2.5 seconds later, followed
   by UI failure/three flashes. Resume the Sensor; late frames must not report
   success. A fresh GP21 press should retrieve successfully. The 100-byte transfer
   is too brief to reliably reproduce this by manually pulling a jumper mid-transfer.

Record both USB logs and LED results. Hardware CRC/error injection and UART
responsiveness during networking remain later validation work; networking has
not been added at this milestone.
