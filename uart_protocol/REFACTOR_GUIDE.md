# Stage 1 refactor guide

The old implementation put hardware setup, framing, both peers, mock data, and
fault injection in a 1,733-line `src/main.c`. The new `main.c` only sets up the
UARTs and contexts, starts the demo, polls incoming bytes, dispatches frames,
runs timeout ticks, and reports the result. Protocol behavior is unchanged.

## Module map

| Module | Responsibility |
| --- | --- |
| `src/protocol/protocol.h` | Wire constants, message/NACK codes, frame type and named offsets |
| `src/protocol/crc16.c/.h` | CRC-16/X-25 only |
| `src/protocol/frame.c/.h` | Little-endian fields, byte stuffing, encoding and validation |
| `src/protocol/stream_parser.c/.h` | Delimiter-based incoming-byte accumulation |
| `src/protocol/link.h` | Application-selected UART, session ID and optional diagnostic sink |
| `src/protocol/reliability.c/.h` | Retained transmissions, 500 ms timeout, three retries, ACK/NACK helpers |
| `src/pico1/pico1_protocol.c/.h` | Sensor-side context, commands, duplicate commands, DATA/END sender |
| `src/pico2/pico2_protocol.c/.h` | Receiver context, command sequence, block storage, duplicates and END validation |
| `src/demo/failure_injection.c/.h` | All four faults and mode banners; selected at compilation |
| `src/demo/mock_logger.c/.h` | Original 24 mock bytes, demo receive capacity and byte comparison |
| `src/demo/demo.c/.h` | Demo configuration, hooks, automatic CLEAR_LOG, start/completion/failure banners |

The protocol modules do not include demo headers. Empty hook structs select
normal behavior. Diagnostic text still uses UART0 in the demo, as before.

## Member 1's future RAM-buffer connection

Replace the mock-data arguments at the `pico1_init()` call in `demo_init()` with
Member 1's existing buffer and length. For a non-demo application:

```c
const uint8_t *compressed_data = /* Member 1's completed RAM buffer */;
size_t compressed_length = /* number of compressed bytes */;

bool ok = pico1_init(&p1, sensor_link, compressed_data, compressed_length,
                    (pico1_hooks_t){0});
```

Pico 1 borrows the buffer: keep it alive and unchanged while transfers/retries
can read it. No allocation or copy of the complete source is required.
`CLEAR_LOG` clears the context's logical length and transfer state; it does not
erase or free caller-owned storage. A future application must coordinate its
logger's lifecycle with this logical clear. Compression and storage are not
implemented here.

`MAX_PAYLOAD_SIZE` remains **256 bytes per frame**. `DATA_CHUNK_SIZE` remains
**8 compressed bytes** plus the two-byte block index. Total source length uses
`size_t`; the existing 16-bit block-count field limits a source to
`UINT16_MAX * DATA_CHUNK_SIZE` (524,280 bytes with current chunks). Initialization
rejects unrepresentable lengths instead of truncating the block count.

Pico 2 accepts a caller-owned receive buffer and a separate capacity through
`pico2_init()`. The current demo's `DEMO_RECEIVE_CAPACITY` remains 256 bytes,
independently named and configured. A host test supplies 513 bytes of capacity
and transfers 513 bytes in 65 DATA frames. The protocol does not need the source
bytes: only the demo installs `mock_logger_matches` as a validation hook. The
demo still compares every byte **before** acknowledging TRANSFER_END. Normal
operation with no hook validates the received block count. Automatic CLEAR_LOG
also belongs to the demo completion hook.

## Build a selected mode

Use Pico SDK **2.2.0**, the Arm toolchain, CMake, Ninja and Python 3. In the
configured VS Code/Pico terminal, run from the project root:

```text
cmake -S . -B build -G Ninja -DPICO_BOARD=pico_w -DCMAKE_BUILD_TYPE=Debug -DPROTOCOL_TEST=0
cmake --build build --parallel 4
```

Choose `0` happy path, `1` corrupt first DATA, `2` drop first DATA ACK,
`3` wrong first block, or `4` drop first CONFIG ACK. A fresh build defaults to
mode 4; `original` is retained as a compatibility alias for 4. Existing CMake
caches retain their selected mode. Selection is a compile definition; source
files are never rewritten. `-Wall -Wextra` are enabled without `-Werror` for SDK
builds. If CMake cannot find your Python, append
`-DPython3_EXECUTABLE=<absolute-path-to-python.exe>`.

Outputs remain `build/uart_protocol.elf`, `.bin`, and `.uf2`. `diagram.json` and
`wokwi.toml` are unchanged. Stop/restart Wokwi after rebuilding each mode.
After folder cleanup, the interactive `build/` was cleanly rebuilt for mode **0**.

## Run regression tests

With build tools on PATH and `PICO_SDK_PATH` pointing to SDK 2.2.0:

```text
npm ci --prefix tests
python tests/run_matrix.py
python tests/run_host_tests.py
```

The matrix builds the five modes under `build/regression/mode-N`, runs their actual
RP2040 binaries, checks all previous success/failure assertions, and compares
both complete UART transmit streams with checksum-protected pre-refactor fixtures.
It verifies all active source/configuration hashes remain unchanged during testing.
Results go to `build/regression/results/`; use
`--results-dir <path>` to preserve another run or `--build-only` to skip emulation.
The runner uses its own Python interpreter for CMake.

Host tests need a native GCC-compatible compiler, separately from the Arm
compiler (`--cc <compiler>` selects it). They check framing/CRC, larger and empty
logs, duplicates, malformed commands, capacity rejection, validation failure,
and timeout/retry boundaries. They supplement the firmware emulator and generate
their executable under `build/host-tests/`.

The original, pre-refactor snapshot and baseline evidence were preserved in
`../uart-protocol-stage1-history-20260929-120838.zip` during folder cleanup.
Its entries retain the old `backup/` and `results/` paths. The original source's
SHA-256 remains `b4aa0d005fe2e74a3dbed5fcf31276fffa1424636ad876fa8a77bdd52d69c1f3`.
Builds and ordinary tests no longer depend on a backup folder. Use the matrix's
optional `--history-archive <zip>` argument to verify the archived original.
