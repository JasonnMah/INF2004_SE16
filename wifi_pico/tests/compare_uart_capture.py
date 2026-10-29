#!/usr/bin/env python3
"""Compare a completed Wi-Fi USB hex capture against the Sensor's original binary.
The input file must be an exact export of the Sensor source buffer, not encoded
UART frames or a textual temperature/hex printout. Does not decode temperatures.
"""
from pathlib import Path
import argparse, hashlib, re

def crc16_x25(data):
    crc=0xffff
    for byte in data:
        crc ^= byte
        for _ in range(8): crc=(crc>>1)^0x8408 if crc&1 else crc>>1
    return crc^0xffff

def extract_capture(text, completion=None):
    begin=re.compile(r"\[BINARY BEGIN\] completion=(\d+) length=(\d+) crc16_x25=([0-9a-fA-F]{4})")
    starts=list(begin.finditer(text))
    if completion is not None: starts=[s for s in starts if int(s[1])==completion]
    if not starts: raise ValueError("No matching BINARY BEGIN record")
    # Select the last started capture, so a truncated new capture cannot silently
    # fall back to an older successful retrieval. Explicit IDs select earlier ones.
    start=starts[-1]; identity,length,crc=int(start[1]),int(start[2]),int(start[3],16)
    data=bytearray()
    for line in text[start.end():].splitlines():
        if "[BINARY BEGIN]" in line: raise ValueError("Capture interrupted by a new BEGIN")
        end=re.search(r"\[BINARY END\] completion=(\d+)",line)
        if end:
            if int(end[1])!=identity: raise ValueError("Completion ID mismatch")
            if len(data)!=length: raise ValueError(f"Length mismatch: expected {length}, captured {len(data)}")
            if crc16_x25(data)!=crc: raise ValueError("Capture CRC mismatch")
            return bytes(data),identity
        chunk=re.search(r"\[BINARY\] offset=([0-9a-fA-F]{4}) hex=([0-9a-fA-F]+)",line)
        if chunk:
            if int(chunk[1],16)!=len(data): raise ValueError("Missing, duplicated or out-of-order capture line")
            data.extend(bytes.fromhex(chunk[2]))
    raise ValueError("Incomplete capture: missing BINARY END")

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture",type=Path)
    parser.add_argument("expected",type=Path,help="Sensor source buffer exported as raw binary")
    parser.add_argument("--completion",type=int)
    args=parser.parse_args()
    try:
        actual,identity=extract_capture(args.capture.read_text(encoding="utf-8-sig"),args.completion)
        expected=args.expected.read_bytes()
        if actual!=expected:
            first=next((i for i,(a,b) in enumerate(zip(actual,expected)) if a!=b),min(len(actual),len(expected)))
            raise ValueError(f"Binary mismatch: received={len(actual)} expected={len(expected)} first_difference={first}")
    except (OSError,ValueError) as error: parser.exit(1,f"FAIL: {error}\n")
    print(f"PASS: completion={identity}; exact binary match, {len(actual)} bytes")
    print("SHA-256: "+hashlib.sha256(actual).hexdigest())
if __name__=="__main__": main()
