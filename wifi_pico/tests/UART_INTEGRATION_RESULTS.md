# UART SMP checkpoint results

Recorded 2026-10-20; portability cleanup 2026-10-21.
Course port: 2e8e6ad480c2d84a369b12995694b9bf5e9ddfb3.

| Validation | Result | Evidence |
|---|---|---|
| Physical SMP boot | User-observed PASS; short single-board capture | [Record](hardware/SMP_BOOT_RESULTS.md) |
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
[Integration guide](../UART_INTEGRATION.md) owns architecture and test instructions.

The duplicate generated BUILD_RESULTS narrative was removed; its measurements,
source/import provenance and physical capture remain preserved separately.
