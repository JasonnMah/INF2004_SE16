# Pre-UI-integration baseline: 100-byte demo

These five UART transmit-stream captures were made on 2026-09-29, **before**
editing `pico2_protocol.c/.h` for the CONFIG-only UI API. They contain the current
100-byte mock logger, in contrast to the historical 24-byte captures one directory
above. Neither the 100-byte mock source nor any original fixture was changed.

The old runner initially failed its stale three-block assertion (`13 !== 3`).
The baseline binaries were retained under `build/ui-baseline/mode-N/`, then all
five were rerun with the corrected expected length of 100 before integration
comparison. Their normal diagnostics, failure/recovery assertions, duplicate
counts and complete byte comparisons all passed.

Each JSON contains hex strings for UART0 and UART1, including protocol and
diagnostic bytes, through the completion banner. No timestamps are compared.
The matrix pins their SHA-256 values in `tests/run_matrix.py`; normal tests never
regenerate them. Keep the historical fixtures as independent prior evidence.

SDK 2.2.0, `pico_w`, Debug, Arm GNU 14.2.Rel1, rp2040js 1.4.0.
