#!/usr/bin/env python3
"""Real protocol/application code with a host wire/IPC model; no physical ISR claims."""
from pathlib import Path
import subprocess,json
w=Path(__file__).resolve().parents[1]
r=w.parent/"uart_protocol"
out=w/"course/build_make/uart-host-tests"
out.mkdir(parents=True,exist_ok=True)
sources=[w/"app_program/uart"/n for n in
    ("rx_ring.c","ui_logic.c","protocol_io.c","communication.c","sensor_emulator.c","fault_injection.c")]
sources += [w/"app_program/platform/protocol_adapter.c",w/"tests/test_uart_app.c"]
sources += list((r/"src/protocol").glob("*.c"))
sources += [r/"src/pico1/pico1_protocol.c",r/"src/pico2/pico2_protocol.c",r/"src/demo/mock_logger.c"]
cases={0:["normal","overflow_recovery","corrupt_frame","bounded_queue"],
       1:["command_timeout"],2:["receive_timeout"],3:["lost_end_ack"]}
rows=[]
for mode,tests in cases.items():
    binary=out/f"uart_host_f{mode}"
    subprocess.run(["gcc","-std=c11","-Wall","-Wextra","-Werror",f"-DUART_FAULT={mode}",
        "-I"+str(w/"app_program/uart"),"-I"+str(w/"app_program/platform/include"),
        "-I"+str(r/"src"),*map(str,sources),"-o",str(binary)],check=True)
    for case in tests:
        p=subprocess.run([str(binary),case],text=True,capture_output=True)
        (w/"tests/evidence"/f"uart-host-{case}.log").write_text(p.stdout+p.stderr)
        if p.returncode:
            print(p.stdout+p.stderr); raise SystemExit(f"FAIL: {case}")
        rows.append({"case":case,"fault":mode,"result":"PASS","kind":"host model"})
        print(f"PASS: {case}")
(w/"tests/evidence/uart-host-results.json").write_text(json.dumps(rows,indent=2)+"\n")
print("PASS: 7 host application cases, ring wrap/loss recovery, debounce and flash timing")
