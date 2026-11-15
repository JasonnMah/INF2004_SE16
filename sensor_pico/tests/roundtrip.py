"""Decode gen_dump's output with the real tools/decode.py and compare with what the encoder was given.

usage: roundtrip.py <gen_dump binary> <decode.py>
"""
import importlib.util
import subprocess
import sys

gen, decode_py = sys.argv[1], sys.argv[2]
out = subprocess.run([gen], capture_output=True, text=True, check=True).stdout

spec = importlib.util.spec_from_file_location("decode", decode_py)
decode = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decode)

expect = [tuple(int(x) for x in line.split()[2:]) for line in out.splitlines() if line.startswith("EXPECT")]
samples, notes = decode.decode(decode.parse_dump(out))

errors = []
if len(samples) != len(expect):
    errors.append(f"sample count: decoded {len(samples)}, expected {len(expect)}")
for i, (s, (q, unix)) in enumerate(zip(samples, expect)):
    got_q = -1 if s["q"] is None else s["q"]
    if got_q != q:
        errors.append(f"sample {i}: q {got_q}, expected {q}")
    if s["unix"] != unix:
        errors.append(f"sample {i}: time {s['unix']}, expected {unix}")
    want_status = "SENSOR_ERR" if q == -1 else "ok"       # the decoder labels failed reads SENSOR_ERR
    if s["status"] != want_status:
        errors.append(f"sample {i}: status {s['status']} (CRC or ordering problem)")
    if len(errors) >= 10:
        break

if errors:
    print("ROUND TRIP FAILED")
    print("\n".join(errors))
    sys.exit(1)
print(f"round trip ok: {len(samples)} samples, every CRC verified by the decoder")
