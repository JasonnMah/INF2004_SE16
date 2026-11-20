# Pre-refactor UART captures

These five fixtures came from the unchanged, checksum-verified 1,733-line
implementation on 2026-09-29. Only its existing test selector was varied by the
original CMake build-copy mechanism. SDK 2.2.0, RP2040 `pico_w`, Arm GNU
14.2.Rel1, Debug (`-Og`), rp2040js 1.4.0 were used.

Each JSON array contains UART0 and UART1 transmit streams as hexadecimal,
captured through delivery of the completion banner. Diagnostic text is included.
Capture timestamps are intentionally excluded: compiler layout changes instruction
timing. The UART byte order, contents, sequence numbers, CRCs, and retransmissions
must match exactly. The emulator still models 115200 baud / ten bits per byte.

The unchanged original has SHA-256:
`b4aa0d005fe2e74a3dbed5fcf31276fffa1424636ad876fa8a77bdd52d69c1f3`.
`results/baseline/metrics.json` in the external history ZIP records the binaries
used to capture the fixtures (archive location is recorded in the project README).
`tests/run_matrix.py` pins the fixture hashes and does not regenerate them.
Never refresh these fixtures from refactored output merely to make a test pass.
