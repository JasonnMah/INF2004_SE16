#!/usr/bin/env python3
"""External profile with a host Sensor peer; no kernel/physical execution claims."""
from pathlib import Path
import json, subprocess, sys
from compare_uart_capture import extract_capture
w=Path(__file__).resolve().parents[1]
s=w.parent/"sensor_pico/lib/uart_protocol"
out=w/"course/build_make/uart-host-tests"
out.mkdir(parents=True,exist_ok=True)
sources=[w/n for n in
         ("rx_ring.c","buttons.c","uart_io.c","communication.c","lab_capture.c")]
sources += [w/"tests/test_uart_app.c"]
sources += list((s/"src/protocol").glob("*.c"))
sources += [s/"src/pico1/pico1_protocol.c",s/"src/pico2/pico2_protocol.c"]
cases={
    0: (("external_binary",300),("external_capture_busy",300),("external_lost_end_ack",300),
        ("external_capacity_recovery",300),("external_empty",0),("external_maximum",1024)),
    2: (("external_receive_timeout",300),("external_empty_timeout",0)),
    3: (("external_injected_end_ack",300),("external_empty_end_ack",0)),
}
rows=[]
for fault,tests in cases.items():
    binary=out/f"uart_host_two_board_f{fault}"
    test_sources=sources+([w/"tests/firmware/fault_injection.c"] if fault else [])
    subprocess.run(["gcc","-std=c11","-Wall","-Wextra","-Werror","-DAPP_SENSOR_EMULATOR=0",f"-DUART_FAULT={fault}",
        "-I"+str(w),"-I"+str(w/"tests/firmware"),
        "-I"+str(s/"src"),*map(str,test_sources),"-o",str(binary)],check=True)
    for case,length in tests:
        p=subprocess.run([str(binary),case],text=True,capture_output=True)
        (out/f"uart-host-{case}.log").write_text(p.stdout+p.stderr)
        if p.returncode: raise SystemExit(p.stdout+p.stderr)
        data,completion=extract_capture(p.stdout)
        expected=bytes(i%256 for i in range(length))
        assert data==expected and completion==1, (case,len(data),completion)
        if fault==2:
            assert "REQUEST_DATA acknowledged" in p.stdout and "Receive timeout: no transfer progress" in p.stdout
            assert "received_log_valid=0 received_bytes=0 completion_count=0" in p.stdout
            assert p.stdout.count("[BINARY BEGIN]")==1
        if fault==3:
            assert p.stdout.count("Dropping first TRANSFER_END ACK only")==1
            assert "Repeated TRANSFER_END ACKed; completion_count=1" in p.stdout
            assert p.stdout.count("[BINARY BEGIN]")==1
        rows.append({"case":case,"fault":fault,"result":"PASS","binary_bytes_compared":length,"kind":"host model"})
        print(f"PASS: {case}; exact {length}-byte capture comparison")
(out/"uart-two-board-host-results.json").write_text(json.dumps(rows,indent=2)+"\n")

# Exercise the user-facing command as well as its parser, including a negative
# comparison against a different source buffer with a perfectly valid capture.
expected_file=out/"expected-300.bin"
expected_file.write_bytes(bytes(i%256 for i in range(300)))
capture=out/"uart-host-external_binary.log"
subprocess.run([sys.executable,str(w/"tests/compare_uart_capture.py"),str(capture),str(expected_file)],check=True)
expected_file.write_bytes(b"wrong source")
p=subprocess.run([sys.executable,str(w/"tests/compare_uart_capture.py"),str(capture),str(expected_file)],capture_output=True,text=True)
assert p.returncode==1 and "Binary mismatch" in p.stderr
print("PASS: comparator CLI accepts equality and rejects different Sensor bytes")
