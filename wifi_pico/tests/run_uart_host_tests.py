#!/usr/bin/env python3
"""Real protocol/application code with a host wire/IPC model; no physical ISR claims."""
from pathlib import Path
import subprocess,json
w=Path(__file__).resolve().parents[1]
fixtures=w/"tests/fixtures"
s=w.parent/"sensor_pico/lib/uart_protocol"
out=w/"course/build_make/uart-host-tests"
out.mkdir(parents=True,exist_ok=True)
sources=[w/n for n in
    ("rx_ring.c","buttons.c","uart_io.c","communication.c")]
sources += [w/"tests/firmware/sensor_emulator.c",w/"tests/firmware/fault_injection.c",w/"tests/test_uart_app.c"]
sources += list((s/"src/protocol").glob("*.c"))
sources += [s/"src/pico1/pico1_protocol.c",s/"src/pico2/pico2_protocol.c",fixtures/"demo/mock_logger.c"]
cases={0:["normal","overflow_recovery","corrupt_frame","bounded_queue"],
       1:["command_timeout"],2:["receive_timeout"],3:["lost_end_ack"]}
rows=[]
for mode,tests in cases.items():
    binary=out/f"uart_host_f{mode}"
    subprocess.run(["gcc","-std=c11","-Wall","-Wextra","-Werror",f"-DUART_FAULT={mode}",
        "-I"+str(w),"-I"+str(w/"tests/firmware"),
        "-I"+str(s/"src"),"-I"+str(fixtures),*map(str,sources),"-o",str(binary)],check=True)
    for case in tests:
        p=subprocess.run([str(binary),case],text=True,capture_output=True)
        (out/f"uart-host-{case}.log").write_text(p.stdout+p.stderr)
        if p.returncode:
            print(p.stdout+p.stderr); raise SystemExit(f"FAIL: {case}")
        rows.append({"case":case,"fault":mode,"result":"PASS","kind":"host model"})
        print(f"PASS: {case}")
(out/"uart-host-results.json").write_text(json.dumps(rows,indent=2)+"\n")
print("PASS: 7 host application cases, ring wrap/loss recovery, debounce and flash timing")
