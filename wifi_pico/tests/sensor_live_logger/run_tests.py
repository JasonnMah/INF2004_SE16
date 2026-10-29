#!/usr/bin/env python3
"""Actual RAM adapter + JZ encoder + shared protocol on host; no physical I2C test."""
from pathlib import Path
import importlib.util, json, subprocess, sys
sys.dont_write_bytecode = True
here = Path(__file__).resolve().parent
root = here.parents[2]
shared = root / "shared/uart_protocol/src"
sensor = root / "sensor_pico"
out = here / "build/host"
out.mkdir(parents=True, exist_ok=True)
flags = ["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
         "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
         "-I" + str(here), "-I" + str(shared), "-I" + str(sensor)]
sources = [here / "test_live_logger.c", here / "live_logger.c", sensor / "encoder.c",
           *sorted((shared / "protocol").glob("*.c")),
           shared / "pico1/pico1_protocol.c", shared / "pico2/pico2_protocol.c"]
executable = out / "live_logger_tests"
subprocess.run([*flags, *map(str, sources), "-o", str(executable)], check=True)
fixture = out / "live-encoded-pages.bin"
subprocess.run([str(executable), str(fixture)], check=True)
spec = importlib.util.spec_from_file_location("jz_decoder", sensor / "tools/decode.py")
decoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decoder)
data = fixture.read_bytes()
samples, notes = decoder.decode({i: data[i*256:(i+1)*256] for i in range(len(data)//256)})
assert [x["temp"] for x in samples] == [30.0, 30.25, 30.5, 31.0, None] + [31.0] * 600
assert all(x["status"] in ("ok", "SENSOR_ERR") for x in samples)
assert not any("mismatch" in n or "UNVERIFIED" in n for n in notes), notes
print("PASS JZ decoder: 605 sample slots across two pages, valid CHECKs and expected temperatures/errors")
result = {"kind": "host adapter/protocol tests, not RP2040 emulator or physical testing",
          "result": "PASS", "sanitizers": "AddressSanitizer and UndefinedBehaviorSanitizer",
          "decoder": "JZ actual decoder: 605 slots, two sealed pages, valid CHECKs",
          "physical": "PENDING", "scope": "live-reading RAM integration; no flash/RTC"}
(out / "results.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
