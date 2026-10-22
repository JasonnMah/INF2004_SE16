#!/usr/bin/env python3
"""Decode a serial capture of the Sensor Pico's flash dump (GP22).

Usage:  decode.py capture.txt [--csv]

Reads the lines between "FLASHDUMP BEGIN" and "FLASHDUMP END" (format "@<page>:<offset> <hex bytes>")
and decodes the nibble stream described in encoder.h. Every CHECK is verified; the samples in a chunk
whose CRC does not match are reported as BAD, and samples after the last CHECK on a page as UNVERIFIED.
Each encountered CHECK also reports its stored CRC chunk.
"""
import re
import sys
from datetime import datetime, timezone

PAGE_BYTES = 256
PAGE_NIBBLES = PAGE_BYTES * 2


def parse_dump(text):
    pages = {}
    inside = False
    for line in text.splitlines():
        if line.startswith("FLASHDUMP BEGIN"):
            inside, pages = True, {}          # a later dump replaces an earlier one
            continue
        if line.startswith("FLASHDUMP END"):
            inside = False
            continue
        m = re.match(r"@(\d+):(\d+)((?: [0-9A-Fa-f]{2})+)\s*$", line)
        if inside and m:
            page, off = int(m.group(1)), int(m.group(2))
            data = bytes.fromhex(m.group(3).replace(" ", ""))
            buf = pages.setdefault(page, bytearray(b"\xff" * PAGE_BYTES))
            buf[off:off + len(data)] = data
    return {p: bytes(b) for p, b in pages.items()}


def crc16_nibbles(nibs):
    crc = 0xFFFF
    for n in nibs:
        crc ^= n << 12
        for _ in range(4):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def to_nibbles(page):
    out = []
    for b in page:
        out += [b >> 4, b & 0x0F]
    return out


def decode(pages):
    samples = []          # dicts: seg, slot, unix, q, temp, status, page
    notes = []
    seg = 0
    seen_boot = False     # segments are numbered from 0; every BOOT after the first starts a new one
    v = None
    t0 = None
    interval = None
    slot = 0

    def emit(page, q, status="ok"):
        nonlocal slot
        unix = t0 + slot * interval * 60 if t0 is not None else None
        samples.append(dict(seg=seg, slot=slot, unix=unix, q=q,
                            temp=None if q is None else q * 0.25 - 40.0,
                            status=status, page=page))
        slot += 1
        return samples[-1]

    for pg in sorted(pages):
        nibs = to_nibbles(pages[pg])
        i = 0
        chunk_start = 0
        chunk = []

        def need(n):
            return i + n <= PAGE_NIBBLES

        while i < PAGE_NIBBLES:
            start = i
            n = nibs[i]
            i += 1
            if n == 0xF:
                break
            if n <= 6:
                if v is None:
                    notes.append(f"page {pg}: delta before any anchor")
                    chunk.append(emit(pg, None, "NO-ANCHOR"))
                else:
                    v += n
                    chunk.append(emit(pg, v))
            elif n >= 9:
                if v is None:
                    notes.append(f"page {pg}: delta before any anchor")
                    chunk.append(emit(pg, None, "NO-ANCHOR"))
                else:
                    v -= n - 8
                    chunk.append(emit(pg, v))
            elif n == 8:
                if not need(3):
                    notes.append(f"page {pg}: truncated anchor")
                    break
                v = (nibs[i] << 8) | (nibs[i + 1] << 4) | nibs[i + 2]
                i += 3
                chunk.append(emit(pg, v))
            else:  # 0x7 metadata
                if not need(1):
                    notes.append(f"page {pg}: truncated metadata")
                    break
                ty = nibs[i]
                i += 1
                if ty == 1:
                    if not need(12):
                        notes.append(f"page {pg}: truncated TIMEBASE")
                        break
                    t0 = int("".join("%X" % x for x in nibs[i:i + 8]), 16)
                    interval = int("".join("%X" % x for x in nibs[i + 8:i + 12]), 16)
                    i += 12
                    slot = 0
                elif ty == 2:
                    chunk.append(emit(pg, None, "SENSOR_ERR"))
                elif ty == 3:
                    if seen_boot:
                        seg += 1
                    seen_boot = True
                    v = None
                    t0 = None
                    slot = 0
                    notes.append(f"page {pg}: BOOT (segment {seg})")
                elif ty == 4:
                    if not need(4):
                        notes.append(f"page {pg}: truncated CHECK")
                        break
                    stored = int("".join("%X" % x for x in nibs[i:i + 4]), 16)
                    i += 4
                    ok = stored == crc16_nibbles(nibs[chunk_start:start])
                    notes.append(f"page {pg}: CHECK CRC chunk 0x{stored:04X} ({'OK' if ok else 'mismatch'})")
                    for s in chunk:
                        if s["status"] == "ok":
                            s["status"] = "ok" if ok else "BAD"
                    if not ok:
                        notes.append(f"page {pg}: CHECK mismatch")
                    chunk = []
                    chunk_start = i
                else:
                    notes.append(f"page {pg}: unknown metadata type {ty}, page abandoned")
                    break
        for s in chunk:
            if s["status"] == "ok":
                s["status"] = "UNVERIFIED"
        if chunk:
            notes.append(f"page {pg}: {len(chunk)} samples after the last CHECK")
    return samples, notes


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    text = open(sys.argv[1], errors="replace").read()
    csv = "--csv" in sys.argv[2:]
    pages = parse_dump(text)
    if not pages:
        print("no flash dump found")
        return 1
    samples, notes = decode(pages)
    if csv:
        print("seg,slot,unix,q,temp_c,status,page")
    for s in samples:
        temp = "" if s["temp"] is None else f"{s['temp']:.2f}"
        unix = "" if s["unix"] is None else s["unix"]
        q = "" if s["q"] is None else s["q"]
        if csv:
            print(f"{s['seg']},{s['slot']},{unix},{q},{temp},{s['status']},{s['page']}")
        else:
            when = ("?" if s["unix"] is None else
                    datetime.fromtimestamp(s["unix"], tz=timezone.utc).strftime("%Y-%m-%d %H:%M:%S"))
            shown = "  --  " if s["temp"] is None else f"{s['temp']:6.2f}"
            print(f"seg {s['seg']} slot {s['slot']:4d}  {when}  {shown} C  {s['status']}  (page {s['page']})")
    print(f"# {len(samples)} samples, {len(pages)} pages", file=sys.stderr)
    for n in notes:
        print(f"# {n}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
