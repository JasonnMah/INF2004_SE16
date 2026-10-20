# Stage 1 refactor test results

Date: 2026-09-29. **Behavioral changes: none.** No protocol bug fix was introduced.
All five modes build and pass the local RP2040 emulator. Both UART transmit
streams match fresh pre-refactor captures byte for byte, including frames,
sequence numbers, retransmissions, and diagnostic text.

## Environment and scope

- Raspberry Pi Pico W / RP2040; Pico SDK **2.2.0**.
- Arm GNU Toolchain **14.2.Rel1**, GCC **14.2.1 20241119**.
- CMake 3.31.5, Ninja 1.12.1, Debug configuration (SDK `-Og`).
- Refactored firmware compiled with `-Wall -Wextra`: **zero build warnings**.
  SDK warnings are not fatal. Host checks use `-Wall -Wextra -Werror`.
- Local emulator: **rp2040js 1.4.0**, actual SDK-produced ARM BIN, boot ROM B1,
  crossed UART0/UART1 with ten-bit byte timing at 115200 baud.
- **Wokwi runtime: NOT RUN during this refactor.** The user's earlier manual
  Wokwi passes are acknowledged; they are not claimed as new verification.
- Physical hardware: not run. The emulator models byte transport, not GPIO
  waveforms or TX FIFO backpressure. Its existing unimplemented UART register
  warning also occurs with the original. Golden comparisons exclude timestamps;
  instruction timing and binary layout naturally change with modularization.

## Five-mode matrix

| Mode | Scenario | Original build/emulator | Refactored build | Refactored emulator | Both UART streams |
| --- | --- | --- | --- | --- | --- |
| 0 | Normal happy path | PASS / PASS | PASS | PASS | Identical |
| 1 | Corrupt first DATA frame | PASS / PASS | PASS | PASS | Identical |
| 2 | Drop first DATA ACK | PASS / PASS | PASS | PASS | Identical |
| 3 | Wrong first DATA block index | PASS / PASS | PASS | PASS | Identical |
| 4 | Drop first CONFIG ACK | PASS / PASS | PASS | PASS | Identical |

Every run retains the original required diagnostics, absence-of-failure checks,
DATA acceptance counts (three, except two logged ACKs in mode 2), and duplicate
count (one only in mode 2). Mode-specific injection and recovery messages remain
required. The demo compares all 24 reconstructed bytes before END is ACKed.
The additional golden comparison checks exact transmitted bytes on **both**
UARTs through the completion banner; fixtures are checksum-pinned.

## Additional checks

- **PASS:** native C tests for X-25 check vector `123456789` -> `0x906e`, maximum
  payload round-trip including reserved bytes, malformed escapes, CRC damage,
  encoding limits, and parser overflow recovery.
- **PASS:** 513-byte borrowed-buffer transfer (65 DATA frames, partial final
  chunk), empty transfer, no expected-data hook, and logical CLEAR_LOG without
  overwriting caller storage. Per-frame payload remains limited to 256 bytes.
- **PASS:** duplicate CONFIG/CLEAR_LOG and duplicate DATA handling, wrong session,
  invalid request start block, malformed CONFIG, out-of-order DATA, bounded
  receive capacity, wrong END count, and rejection by the demo-validation seam.
- **PASS:** 499 ms does not trigger a command retry; 500 ms does. Three retries
  are permitted; the next expiration fails/stops the respective command/transfer.
- **PASS:** `PROTOCOL_TEST=original` compiles with mode 4; invalid mode 5 is rejected.
- **PASS:** all five ELF/BIN/UF2 outputs exist. The interactive `build/` was rebuilt
  in its prior mode **3** and independently passed the emulator/golden check.
- **PASS:** original backup checksum, complete initial snapshot manifest, and
  byte-identical `diagram.json`/`wokwi.toml`. All active source/configuration
  hashes were unchanged during the final matrix run.

## Before/after metrics

Counts below are physical lines, including comments and blank lines, in active
`src/**/*.c` only. SDK, tests, backups and generated files are excluded.

| Metric | Before | After |
| --- | ---: | ---: |
| Total C source lines | 1,733 | 1,427 |
| `src/main.c` lines | 1,733 | 97 |
| Header lines, separately | 0 | 394 |
| C + header lines | 1,733 | 1,821 |

Firmware sizes use fresh original and refactored builds with the same SDK,
compiler and Debug configuration, rather than older packaged binaries.

| Mode | Original BIN bytes | Refactored BIN bytes | Difference |
| --- | ---: | ---: | ---: |
| 0 | 20,272 | 20,840 | +568 |
| 1 | 20,440 | 21,016 | +576 |
| 2 | 20,368 | 20,936 | +568 |
| 3 | 20,496 | 21,080 | +584 |
| 4 | 20,352 | 20,928 | +576 |

`arm-none-eabi-size` output (filenames shortened for readability):

```text
   text    data     bss     dec     hex  filename
  24368       0    1192   25560    63d8  before/mode-0/uart_protocol.elf
  24536       0    1196   25732    6484  before/mode-1/uart_protocol.elf
  24464       0    1196   25660    643c  before/mode-2/uart_protocol.elf
  24592       0    1196   25788    64bc  before/mode-3/uart_protocol.elf
  24448       0    1196   25644    642c  before/mode-4/uart_protocol.elf
  24936       0    1200   26136    6618  after/mode-0/uart_protocol.elf
  25112       0    1200   26312    66c8  after/mode-1/uart_protocol.elf
  25032       0    1200   26232    6678  after/mode-2/uart_protocol.elf
  25176       0    1200   26376    6708  after/mode-3/uart_protocol.elf
  25024       0    1200   26224    6670  after/mode-4/uart_protocol.elf
```

BSS is not total RAM consumption; the firmware also uses bounded stack buffers.
The protocol allocates no memory dynamically. The small code-size increase is
accepted in exchange for module boundaries, buffer interfaces and explicit hooks.

## Evidence and preservation

Folder-cleanup note: the `results/`, `backup/`, `firmware/` and `TEST_RESULTS.md`
paths below now refer to entries in the external archive
`../uart-protocol-stage1-history-20260929-120838.zip`, not active project paths.
The original evidence is preserved there. Golden fixtures remain in `tests/golden/`.
New test runs write under `build/regression/` and `build/host-tests/`.

- `results/refactor/matrix.json`: final source hashes, per-mode results, binary
  sizes, hashes and raw size-tool output.
- `results/refactor/configure-N.log`, `build-N.log`, `emulator-N.log`,
  `uart-N.log`, `uart-N.log.wire.json`: final build/emulator evidence.
- `results/refactor/host-tests.log`, `configure-alias.log`,
  `configure-invalid.log`, and `*-interactive.log`: additional checks.
- `results/baseline/`: freshly built original metrics and five passing runs.
- `tests/golden/`: immutable original UART fixtures and their provenance.
- `backup/pre-refactor-2026-09-29/`: working source/configuration/test snapshot,
  initial SHA-256 manifest, and preserved earlier reports and firmware artifacts.
- `firmware/mode-N/`: copies of the newly tested refactored ELF/BIN/UF2 variants.
  Normal matrix runs do not replace these delivery copies or the interactive build.
- Root `TEST_RESULTS.md` and older top-level `results/` files are historical;
  this report and `results/refactor/` describe the refactor.

`backup/main.c.original` and `backup/SHA256.txt` were not modified. Original
source SHA-256 before and after:

`b4aa0d005fe2e74a3dbed5fcf31276fffa1424636ad876fa8a77bdd52d69c1f3`

The new initialization APIs accept caller-owned buffers and optional hooks as
requested. The shipped demo keeps the original 256-byte receive capacity,
24-byte source, eight-byte chunks, session/configuration values, all five faults,
and automatic CLEAR_LOG. No networking, RTOS, authentication, storage, sensor or
compression algorithm was added.

## Folder cleanup validation (2026-09-29)

The user reports manually verifying all five refactored modes in Wokwi before
this cleanup. The cleanup agent did not run Wokwi runtime.

- All source files, `.vscode/`, `pico_sdk_import.cmake`, `diagram.json`, and
  `wokwi.toml` are SHA-256 identical to their pre-cleanup versions. Protocol
  behavior and source modules were not changed.
- A fresh CMake configure (`--fresh`) and complete rebuild (`--clean-first`)
  passed for `pico_w`, SDK 2.2.0, Debug, `PROTOCOL_TEST=0`, with no compiler warnings.
- Active outputs: `build/uart_protocol.elf` (336,560 bytes), `.bin` (20,840 bytes),
  and `.uf2` (41,984 bytes). Wokwi retains its original active UF2/ELF paths.
- The active mode 0 firmware passed the local RP2040 emulator and exact golden
  UART comparison. The updated matrix runner passed all five builds/emulator
  runs without an in-project backup or external archive argument. Every UART
  stream remained identical to its golden capture. Host checks also passed.
- Historical files were archived to
  `../uart-protocol-stage1-history-20260929-120838.zip`. Every one of its 228
  file entries was read back and SHA-256 verified before the originals were
  removed from the project. The original source checksum remains unchanged.
- CMake no longer depends on a historical backup. The test runner preserves
  golden checks and supports optional archived-original verification through
  `--history-archive`. Generated test files now live under `build/`.
- Temporary validation builds were removed after testing. The final matrix
  summary is `build/cleanup-matrix.json`; clean build, host and active emulator
  logs use the `build/cleanup-` prefix. Earlier evidence remains in the ZIP.

Removed from the active root: `.npm-cache/`, `build-baseline/`,
`build-host-tests/`, `build-selector-check/`, `build-tests/`, `results/`,
`firmware/`, `backup/`, and historical `TEST_RESULTS.md`. The final useful
refactor summary remains in this document. `cmake/PICO_SDK_LICENSE.txt` is kept
as the distribution notice for the SDK import; the installed emulator under
`tests/node_modules/` is kept so regression tests remain immediately runnable.


## Post-refactor compressed-data integration verification (2026-09-29)

After completion of the refactor and folder-cleanup validation, the Wokwi
demo was updated to use a 100-byte compressed temperature-data buffer captured
from the Sensor Pico compression prototype.

The purpose of this test was to verify that the UART protocol can transfer a
realistic compressed-data buffer from the Sensor Pico side to the Wi-Fi Pico
side without modifying the data.

The compressed buffer is treated as an opaque byte sequence by the UART
protocol. Compression correctness is outside the scope of this verification.

### Test configuration

- Target: Raspberry Pi Pico W / RP2040
- Pico SDK: 2.2.0
- UART: 115200 baud, 8N1
- Source compressed buffer: 100 bytes
- DATA chunk size: 8 compressed bytes per DATA frame
- Total DATA frames: 13
  - 12 full frames × 8 bytes = 96 bytes
  - 1 final frame × 4 bytes = 4 bytes
- Validation: byte-for-byte comparison of the reconstructed Pico 2 buffer
  against the original Pico 1 source buffer.

### Wokwi integration test matrix

| Mode | Scenario | Result |
| --- | --- | --- |
| 0 | Normal happy path | PASS |
| 1 | Corrupt first DATA frame | PASS |
| 2 | Drop first DATA ACK | PASS |
| 3 | Wrong first DATA block index | PASS |
| 4 | Drop first CONFIG ACK | PASS |

### Mode 0 — normal transfer

The complete 100-byte compressed buffer was transferred over 13 DATA frames.
Pico 2 validated TRANSFER_END and confirmed that the reconstructed buffer
matched the Pico 1 source data byte for byte.

### Mode 1 — corrupted DATA frame

The first DATA frame was intentionally corrupted. Pico 2 rejected the invalid
frame after CRC validation. Pico 1 timed out and retransmitted the pending
frame. The remaining transfer completed successfully and the reconstructed
100-byte buffer matched the original source.

### Mode 2 — dropped DATA ACK

The ACK for the first DATA block was intentionally dropped. Pico 1 timed out
and retransmitted the pending DATA frame. Pico 2 detected the duplicate block,
ACKed it without storing the data twice, and the complete reconstructed buffer
still matched the source.

### Mode 3 — incorrect DATA block index

The first DATA frame was transmitted with an intentionally incorrect block
index. Pico 2 returned a NACK and Pico 1 retransmitted the correct transfer
frame. The remaining transfer completed successfully and the reconstructed
buffer matched the source.

### Mode 4 — dropped CONFIG ACK

The first CONFIG ACK was intentionally dropped. Pico 2 reached the command
timeout and retransmitted CONFIG. The configuration was then acknowledged and
the subsequent 100-byte data transfer completed successfully.

### Result

All five Wokwi protocol modes passed using the 100-byte compressed
temperature-data buffer.

The tests demonstrate that the UART protocol can:

- transfer compressed logger data across multiple DATA frames;
- detect CRC-corrupted frames;
- recover from lost acknowledgements;
- detect and safely handle duplicate DATA frames;
- reject unexpected block indexes using NACK;
- retransmit timed-out configuration commands; and
- reconstruct the source compressed-data buffer byte for byte.

This verification tests UART transport and protocol reliability only. It does
not validate whether the compression algorithm correctly represents every
temperature reading.

The 100-byte compressed test vector does not contain the reserved framing bytes
0x7E or 0x7D. Byte-stuffing behavior remains covered separately by the protocol
unit tests and the earlier reserved-byte test fixture.