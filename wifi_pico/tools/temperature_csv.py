#!/usr/bin/env python3
"""Export received JZ encoder pages as CSV, without changing the binary log.

Uses sensor_pico/tools/decode.py for temperature/time reconstruction. The extra
page checks reject incomplete or damaged input before offering a readable CSV.
The timestamp source is an operator label, not evidence that an RTC was used.
Raw pages have no format tag. Select interval12 for new firmware, legacy16 for
old captures. The default remains legacy16 to preserve existing receiver use.
"""
import argparse
import csv
from datetime import datetime, timedelta, timezone
from functools import lru_cache
import importlib.util
import os
from pathlib import Path
import tempfile

PAGE_BYTES = 256
TIMESTAMP_SOURCES = ('unconfirmed', 'simulated', 'pc-clock', 'rtc')
LOG_FORMATS = {'interval12': 3, 'legacy16': 4}
CSV_FIELDS = ('sample', 'temperature_c', 'timestamp_sgt', 'status')
DIAGNOSTIC_FIELDS = ('sample', 'temperature_c', 'timestamp_utc', 'timestamp_sgt',
                     'unix_seconds', 'timestamp_source', 'status', 'segment', 'slot', 'page', 'log_format')
SGT = timezone(timedelta(hours=8))


@lru_cache(maxsize=1)
def jz_decoder():
    path = Path(__file__).resolve().parents[2] / 'sensor_pico/tools/decode.py'
    if not path.is_file():
        raise ValueError(f'JZ decoder is required: {path}')
    spec = importlib.util.spec_from_file_location('jz_temperature_decoder', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def validate_page(page, page_number, decoder, interval_nibbles):
    """Check page structure/CRC; leave sample reconstruction to JZ's decoder.

    His serial-dump tool intentionally tolerates partial pages. HTTP CSV export
    instead requires sealed pages, with no hidden data after the padding marker.
    """
    nibs = decoder.to_nibbles(page)
    cursor = chunk_start = 0

    def fail(reason):
        raise ValueError(f'page {page_number}: {reason}')

    def field(length):
        nonlocal cursor
        if cursor + length > len(nibs):
            fail('truncated record')
        value = 0
        for nib in nibs[cursor:cursor + length]:
            value = (value << 4) | nib
        cursor += length
        return value

    while cursor < len(nibs):
        start = cursor
        code = field(1)
        if code == 0xF:
            if chunk_start != start:
                fail('data is not covered by a final CHECK')
            if any(nib != 0xF for nib in nibs[start:]):
                fail('non-padding data follows the end marker')
            return
        if code <= 6 or 9 <= code <= 14:
            continue
        if code == 8:
            if field(3) > 500:
                fail('anchor temperature is outside the encoder range')
            continue
        kind = field(1)  # 0x7 introduces metadata.
        if kind == 1:
            field(8)  # Timestamp, interpreted by the original decoder.
            if field(interval_nibbles) == 0:
                fail('sampling interval is zero')
            if cursor == len(nibs) or nibs[cursor] != 8:
                fail('TIMEBASE must be followed by an anchor')
        elif kind in (2, 3):
            pass  # SENSOR_ERR / BOOT have no following fields.
        elif kind == 4:
            stored = field(4)
            if start == chunk_start:
                fail('CHECK covers no records')
            if stored != decoder.crc16_nibbles(nibs[chunk_start:start]):
                fail('CHECK checksum mismatch')
            chunk_start = cursor
        else:
            fail(f'unknown metadata type {kind}')
    if cursor != chunk_start:
        fail('data is not covered by a final CHECK')


def decode_binary(data, log_format='legacy16'):
    if log_format not in LOG_FORMATS:
        raise ValueError('unknown log format; choose interval12 or legacy16')
    interval_nibbles = LOG_FORMATS[log_format]
    if len(data) % PAGE_BYTES:
        raise ValueError('expected complete 256-byte JZ encoder pages')
    decoder = jz_decoder()
    pages = {number: data[offset:offset + PAGE_BYTES]
             for number, offset in enumerate(range(0, len(data), PAGE_BYTES))}
    for number, page in pages.items():
        validate_page(page, number, decoder, interval_nibbles)
    samples, notes = decoder.decode(pages, interval_nibbles=interval_nibbles)
    for sample in samples:
        if sample['status'] not in ('ok', 'SENSOR_ERR'):
            raise ValueError(f"unverified sample: {sample['status']}")
        if sample['unix'] is None:
            raise ValueError('sample has no TIMEBASE timestamp')
        if sample['q'] is not None and not 0 <= sample['q'] <= 500:
            raise ValueError('decoded temperature is outside the encoder range')
    return samples, notes


def export_csv(data, destination, timestamp_source='unconfirmed', *, log_format='legacy16',
               diagnostic_columns=False):
    if timestamp_source not in TIMESTAMP_SOURCES:
        raise ValueError('unknown timestamp source')
    samples, _ = decode_binary(data, log_format)
    destination = Path(destination)
    temporary = None
    try:
        # UTF-8 BOM helps Excel; numeric temperatures remain ordinary CSV cells.
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8-sig', newline='',
                                         dir=destination.parent, prefix=destination.stem + '-',
                                         suffix='.tmp', delete=False) as output:
            temporary = Path(output.name)
            fields = DIAGNOSTIC_FIELDS if diagnostic_columns else CSV_FIELDS
            writer = csv.DictWriter(output, fieldnames=fields, extrasaction='ignore')
            writer.writeheader()
            for number, sample in enumerate(samples, 1):
                when = datetime.fromtimestamp(sample['unix'], tz=timezone.utc)
                writer.writerow({
                    'sample': number,
                    'temperature_c': '' if sample['temp'] is None else f"{sample['temp']:.2f}",
                    'timestamp_utc': when.strftime('%Y-%m-%d %H:%M:%S'),
                    'timestamp_sgt': when.astimezone(SGT).strftime('%Y-%m-%d %H:%M:%S'),
                    'unix_seconds': sample['unix'], 'timestamp_source': timestamp_source,
                    'status': sample['status'], 'segment': sample['seg'],
                    'slot': sample['slot'], 'page': sample['page'], 'log_format': log_format,
                })
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, destination)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return len(samples)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--output', type=Path, help='Default: same filename with .csv extension')
    parser.add_argument('--timestamp-source', choices=TIMESTAMP_SOURCES, default='unconfirmed')
    parser.add_argument('--log-format', choices=LOG_FORMATS, default='legacy16',
                        help='Explicit encoder format; default preserves old four-nibble captures')
    parser.add_argument('--diagnostic-columns', action='store_true',
                        help='Include UTC time, timestamp source and internal log details')
    args = parser.parse_args()
    output = args.output or args.binary.with_suffix('.csv')
    if output.resolve() == args.binary.resolve():
        parser.error('CSV output must not replace the original binary')
    try:
        count = export_csv(args.binary.read_bytes(), output, args.timestamp_source,
                           log_format=args.log_format, diagnostic_columns=args.diagnostic_columns)
    except (OSError, ValueError) as error:
        parser.exit(1, f'[CSV] Not exported: {error}; binary unchanged\n')
    print(f'[CSV] Saved {output.resolve()}; samples={count}; timestamps={args.timestamp_source}; format={args.log_format}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
