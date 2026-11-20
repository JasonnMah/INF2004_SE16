# Host-side unit tests

Run on the PC; no Pico or SDK needed.

    cmake -S tests -B build_tests
    cmake --build build_tests --config Debug
    ctest --test-dir build_tests -C Debug --output-on-failure

(`--config` / `-C` are needed with Visual Studio generators and harmless elsewhere.)

| Test | What it checks |
|---|---|
| `encoder` | the nibble format: TIMEBASE bytes, delta limits (±6), anchors, errors, CHECK/CRC, page-full reserve, round trip with no drift |
| `test_flash_log` | the log module against a fake flash that enforces real flash rules (1→0 only, page/sector alignment): fresh page after reboot, full region, program/verify failures |
| `test_flash_overlap` | refusal to start when the program image reaches into the log region |
| `roundtrip` | encodes ~1500 samples (errors, mid-log TIMEBASEs, flushes, page changes), decodes them with `tools/decode.py`, compares values and times, and requires every CRC to verify |

`tools/decode.py` is the decoder used by the round-trip test (override with `-DDECODE_PY=...`).
`flash_log.c` is compiled with a 16 KiB region here so a "full" log is quick to reach.
