# Sensor codec loopback: implementation and validation

This milestone follows the user's clarification: a NEW single-Pico live AHT10 /
encoder / UART / decoder target, leaving both verified split images unchanged.
The supplied Member 1 source arrived as `src/apps/main.c`, not the initially
specified `sensor_encoder_main.c`. It is retained unchanged as reference and is
not compiled into a firmware target. Its codec is extracted into `src/codec`.

## Build and physical setup

Target: `sensor_codec_loopback_test`, Pico W/RP2040, SDK 2.2.0.
In a configured Pico SDK terminal, from the project root:

```text
cmake -S . -B build -G Ninja -DPICO_BOARD=pico_w -DCMAKE_BUILD_TYPE=Debug -DCODEC_SAMPLE_INTERVAL=3600
cmake --build build --target sensor_codec_loopback_test --parallel 4
```

Flash `build/sensor_codec_loopback_test.uf2`. Matching ELF/BIN are generated.
For a practical bench run, use `-DCODEC_SAMPLE_INTERVAL=1` in the configure command
and rebuild this target. This changes only this new target's CONFIG interval;
3600 is its default and all older targets retain their existing values.

On ONE Maker Pi/Pico W, connect:

- GP0 (UART0 TX, Pico 2 role) -> GP5 (UART1 RX, Pico 1 role).
- GP4 (UART1 TX, Pico 1 role) -> GP1 (UART0 RX, Pico 2 role).
- AHT10 SDA -> GP8; SCL -> GP9; sensor GND -> Pico GND; use 3.3 V-compatible
  power/pull-ups for the sensor module.

Member 1's GP0/GP1 I2C mapping conflicts with UART0. Only this new target moves
I2C0 to GP8/GP9. No existing UART, button or split-target pins are changed.
Remove any cross-board UART wiring while using this single-board test.
USB serial is the only diagnostic output; UARTs carry protocol bytes only.
Open its USB COM port at 115200, 8N1, no flow control.

Startup reports `Sensor codec loopback ready`, AHT10 initialization status and
sample interval. No samples are acquired until GP20 CONFIG succeeds. The first
measurement starts immediately after CONFIG; subsequent measurements follow the
configured interval. AHT10 trigger/read failures or still-busy responses are not
encoded or counted. The original 80 ms measurement wait is a non-blocking deadline,
with bounded 5 ms I2C operations. Initialization waits occur before the UART loop.

GP20/GP21/GP22 and GP6 use the unchanged UI module:

- GP20: CONFIG / start or resume acquisition.
- GP21: snapshot retrieval and decoded table after TRANSFER_END.
- GP22: CLEAR_LOG; the application resets encoder count/anchor/length and pauses
  acquisition so a following GP21 reliably retrieves an empty log. GP20 resumes.

Existing button debounce, busy handling and LED patterns are unchanged. The
application pause after clear applies only to the new target. It is not a new
protocol rule. CONFIG does not clear an existing log. No auto-clear follows GP21.

## Corrections extracted from Member 1 source

The supplied source had two confirmed defects: signed negative steps could OR
ones into a preceding high nibble, and out-of-range readings cleared HAS_ANCHOR
without storing that current sample. In `temp_encoder.c`:

1. `store_step` masks every value with `((uint8_t)steps) & 0x0f`. A high nibble
   assigns `(uint8_t)(nibble << 4)` to clear stale contents; a low nibble ORs only
   that masked value, then advances the nibble position to the next byte.
2. Out-of-range rounded steps immediately call `store_anchor_t` for the SAME
   reading, retain/set the anchor state, update last_anchor and increment count.
3. All five anchor nibbles are reserved before writing any. Full-buffer failures
   change no bytes, counts or anchor state. Unlike the reference's pointer wrap,
   this bounded log stops accepting samples when full rather than corrupting its
   beginning. The test application pauses acquisition and prints the full status.
4. Differences use int32 rather than overflowing int16 on distant signed anchors.
   A rounded reconstruction outside int16 is represented by a new absolute anchor.
5. `sample_count` increments only on successful encoding; clear resets it.

The 25-hundredths resolution and Member 1 nearest-step rounding (signed half-up
integer expression) are preserved. Deltas are relative to the latest anchor,
never cumulative. The source stream buffer remains 100 bytes.

## Application blob and stable ownership

Zero samples produce ZERO transfer bytes. For a non-empty log:

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 2 | uint16 sample count, little-endian |
| 2 | ceil(used nibbles/2) | Original high-nibble-first compressed stream |

Maximum snapshot length is 102 bytes. `temp_encoder_snapshot` copies the header
and only valid compressed bytes. The new application's static snapshot is borrowed
by Pico 1. It is refreshed only when Pico 1 is idle with no pending transfer;
it remains immutable through DATA retries and the final END ACK. Acquisition
writes a separate encoder buffer, so later samples appear only in a later snapshot.
The receiver also has a separately bounded 102-byte buffer.

Accepted CLEAR_LOG resets the real encoder in the application after the existing
Pico 1 handler has accepted the command. Duplicate commands do not apply the
application reset twice. The borrowed snapshot is not modified during an active
transfer. No per-DATA metadata, frame change, TRANSFER_END change or protocol.h
change was needed.

## Decoder and display

`temp_decoder.c/.h` is Pico/UART independent and allocation-free.

- 0x8 is escape followed by four absolute-temperature nibbles, most significant
  first. Bits >= 0x8000 are explicitly converted by subtracting 65536, producing
  signed int16 hundredths without relying on an implementation-defined cast.
- 0x0..0x7 mean steps 0..+7; 0x9..0xf mean -7..-1 (nibble minus 16).
- Output equals latest anchor + steps*25.
- Exactly sample_count values are decoded. Padding/trailing bytes are not samples.
- Invalid arguments, delta-before-anchor, truncated anchor/count and signed
  overflow return explicit errors. A zero-byte log or zero-count header is empty.
- The whole declared stream is validated before callbacks; malformed data cannot
  emit a partial temperature table. Input must stay immutable during the passes.

`transfer_complete` in `src/apps/sensor_codec_loopback_main.c` calls
`temp_log_print(received_log, received_log_length, config_timestamp, config_interval)`.
Transport has already validated TRANSFER_END. A codec error prints `[DECODE] Invalid
log: ...`; it does not alter the transport ACK or the UI's transport success result.
No decoding is added to pico2_protocol.c. The verified Wi-Fi split main remains
unchanged, as requested; this target contains its Pico 2 role for the loopback.

Timestamps use uint64 arithmetic: CONFIG timestamp + zero-based sample index *
CONFIG interval. They are inferred sample slots, NOT actual acquisition times.
Failed reads, pauses or CONFIG changes can break correspondence with wall time;
there are no per-sample timestamps in this format. A new CONFIG applied to an
existing log also changes the inferred display timestamps for that log.

Example actual host-test output:

```text
[ENCODE] #1 24.00 C  ANCHOR  encoded=24.00 C
[ENCODE] #2 24.25 C  DELTA +1  encoded=24.25 C
[ENCODE] #3 24.00 C  DELTA +0  encoded=24.00 C
[ENCODE] #4 23.75 C  DELTA -1  encoded=23.75 C

                 DECODED TEMPERATURE LOG
Sample   Timestamp       Temperature       Encoding
1        1700000000       24.00 C       ANCHOR
2        1700003600       24.25 C       DELTA +1
3        1700007200       24.00 C       DELTA +0
4        1700010800       23.75 C       DELTA -1
Samples decoded : 4
Compressed bytes: 4 (plus 2-byte header)
```

The encoder line prints both original temperature and representable encoded value.
An original 24.13 C can therefore print `DELTA +1 encoded=24.25 C`. Decoded values
match the quantized representation, not always the exact sensor input (maximum
integer-hundredths delta quantization error 0.12 C). Anchors remain exact.
Negative temperatures, including -0.75 C, print correctly to two decimal places.
Verbose nibble and full-buffer dumps are absent.

After clear/retrieve:

```text
[DECODE] Logger is empty; no temperature samples to display.
```

## Tests and results

```text
python tests/run_codec_tests.py
python tests/run_host_tests.py
python tests/run_ui_tests.py
python tests/check_split_targets.py
python tests/run_matrix.py --results-dir build/codec-regression-results
```

New host tests use -Wall -Wextra -Werror and cover signed anchors; all fifteen
normal nibble mappings; zero; multiple anchors; anchor-relative values; odd/even
sample counts and padding; truncated anchors; missing initial anchors; excessive
counts; invalid arguments; signed arithmetic overflow; actual extracted encoder
round trips; immediate out-of-range anchors; all insufficient-anchor-capacity
boundaries; count reset; quantization; frozen snapshot ownership; and printed output.

Alignment detail: anchor,+1,-1 puts +1 LOW and -1 in the following HIGH nibble,
because an anchor consumes five nibbles. We also test anchor,0,+1,-1 to force
+1 HIGH / -1 LOW and assert the actual encoded byte is 0x1f.

A second host executable sends an 80-sample application blob through the existing
frame codec/parser, both protocol handlers and DATA/ACK machinery. It verifies
multi-block bytes, decoding, frozen snapshots while the encoder acquires another
sample, repeated retrieval without clear, explicit clear and empty retrieval.

New codec and UART-blob host tests: PASS.
Existing host protocol tests, UI tests and split-structure checks: PASS.
New codec target and all five existing firmware targets: build PASS, SDK 2.2.0.
Existing five UART regression modes: PASS with identical fixture wire streams.
Evidence: `build/codec-regression-results/matrix.json` and associated logs.
No physical sensor, hardware UART or Wokwi runtime verification was performed.

## Inventory and remaining limits

Added:
- `src/codec/temp_encoder.c`, `.h`
- `src/codec/temp_decoder.c`, `.h`
- `src/codec/temp_log_print.c`, `.h`
- `src/apps/sensor_codec_loopback_main.c`
- `tests/test_temp_decoder.c`, `tests/test_codec_transport.c`
- `tests/run_codec_tests.py`
- `tests/CODEC_STATUS.md`

Modified: `CMakeLists.txt` (new opt-in target and sampling option),
`tests/run_matrix.py` (exclude independent codec sources from the original target's
source-set check; all existing assertions/fixtures remain).
The user supplied `src/apps/main.c`; it is retained unmodified, including its
original defects, for provenance. The runnable corrected implementation is the
extracted encoder, not this unbuilt reference file.

UART framing/reliability, protocol/UI modules, verified split mains, old test
mains and mock bytes remain unchanged. No RTOS, networking, MQTT, authentication,
storage or new compression algorithm was added. The stream is not self-versioned;
old raw mock blobs are not compatible with the sample-count-prefixed format.
Capacity is fixed, padding is permissive, temperatures are quantized and timestamps
are inferred. AHT10 calibration/error recovery remains basic as in the supplied
source. The new target has not been flashed or physically verified by the agent.
