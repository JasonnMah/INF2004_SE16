#!/usr/bin/env python3
"""PC binary receiver with optional JZ temperature decoding/CSV downloads.
An ID always names the same immutable body. Identical retries return a receipt
without another file; reusing an ID for different bytes returns HTTP 409.
"""
import argparse
import hashlib
import html
import importlib.util
import json
from datetime import date, datetime, timedelta, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import os
import re
import socket
import threading
import time
from urllib.parse import parse_qs

KNOWN_ID = 'pico-known-1024-v1'
KNOWN_BYTES = bytes(range(256)) * 4
MAX_BODY = 65536
MIN_INTERVAL_MINUTES = 1
MAX_INTERVAL_MINUTES = 4095

LOG_FORMATS = ('interval12', 'legacy16')
SGT = timezone(timedelta(hours=8))

class Receiver(ThreadingHTTPServer):
    daemon_threads = True
    def __init__(self, address, directory, expect_known=False, decode_jz=False,
                 timestamp_source='unconfirmed', log_format='legacy16', diagnostic_columns=False):
        if log_format not in LOG_FORMATS:
            raise ValueError('unknown log format')
        self.log_format = log_format
        self.diagnostic_columns = diagnostic_columns
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True)
        self.expect_known = expect_known
        self.timestamp_source = timestamp_source
        self.csv_exporter = None
        if decode_jz:
            path = Path(__file__).with_name('temperature_csv.py')
            spec = importlib.util.spec_from_file_location('temperature_csv', path)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            module.jz_decoder()  # Report a missing source dependency at startup.
            self.csv_exporter = module.export_csv
        self.store_lock = threading.Lock()
        # Load beside this file, also when tests import the receiver by path.
        batch_spec = importlib.util.spec_from_file_location(
            'batch_receiver', Path(__file__).with_name('batch_receiver.py'))
        self.batch_storage = importlib.util.module_from_spec(batch_spec)
        batch_spec.loader.exec_module(self.batch_storage)
        # Settings persist, but the timestamp is generated on each GP20 fetch.
        self.settings_path = self.directory / 'logger_settings.json'
        self.interval_minutes = None
        if self.settings_path.exists():
            settings = json.loads(self.settings_path.read_text(encoding='utf-8'))
            minutes = settings.get('interval_minutes') if isinstance(settings, dict) else None
            if type(minutes) is not int or not MIN_INTERVAL_MINUTES <= minutes <= MAX_INTERVAL_MINUTES:
                raise ValueError('Saved interval must be an integer from 1 to 4095 minutes')
            self.interval_minutes = minutes
        # Friendly download numbers are separate from immutable log IDs/files.
        # Keep assignments even if a CSV is deleted, so numbers are never reused.
        self.csv_names_path = self.directory / 'csv_download_names.json'
        self.csv_numbers = {}
        self.csv_dates = {}
        if self.csv_exporter and self.csv_names_path.exists():
            saved = json.loads(self.csv_names_path.read_text(encoding='utf-8'))
            numbers = saved.get('logs') if isinstance(saved, dict) else None
            if (not isinstance(numbers, dict) or saved.get('version') not in (1, 2) or
                    any(not re.fullmatch(r'[0-9a-f]{64}', key) or
                        type(value) is not int or value < 1
                        for key, value in numbers.items()) or
                    len(set(numbers.values())) != len(numbers)):
                raise ValueError('Invalid saved CSV download numbers')
            dates = saved.get('received_dates', {}) if saved['version'] == 2 else {}
            if (not isinstance(dates, dict) or dates.keys() - numbers.keys() or
                    any(not isinstance(value, str) or
                        not re.fullmatch(r'[0-9]{4}-[0-9]{2}-[0-9]{2}', value)
                        for value in dates.values())):
                raise ValueError('Invalid saved CSV receipt dates')
            try:
                for value in dates.values():
                    date.fromisoformat(value)
            except ValueError as error:
                raise ValueError('Invalid saved CSV receipt dates') from error
            self.csv_numbers = numbers
            self.csv_dates = dates
        if self.csv_exporter:
            self.csv_downloads()  # Assign names to older CSVs without renaming data.
        self.drop_next_receipt = False  # Test-only: committed storage, lost reply.
        super().__init__(address, Handler)

    def csv_downloads(self):
        """Return existing CSVs in numeric order; caller holds store_lock at runtime."""
        files = [path for path in self.directory.glob('*.csv')
                 if re.fullmatch(r'[0-9a-f]{64}\.csv', path.name)]
        numbers = dict(self.csv_numbers)
        dates = dict(self.csv_dates)
        next_number = max(numbers.values(), default=0) + 1
        for path in sorted(files, key=lambda item: (item.stat().st_mtime_ns, item.name)):
            if path.stem not in numbers:
                numbers[path.stem] = next_number
                next_number += 1
            if path.stem not in dates:
                # Binary is written once on receipt; duplicate uploads rewrite
                # the CSV. Prefer its original timestamp, never a sample time
                # or the date on which someone happens to download the CSV.
                binary = path.with_suffix('.bin')
                source = binary if binary.exists() else path
                dates[path.stem] = datetime.fromtimestamp(source.stat().st_mtime, SGT).date().isoformat()
        if numbers != self.csv_numbers or dates != self.csv_dates:
            temporary = self.csv_names_path.with_suffix('.tmp')
            try:
                with temporary.open('w', encoding='utf-8') as output:
                    json.dump({'version': 2, 'logs': numbers, 'received_dates': dates}, output)
                    output.write('\n'); output.flush(); os.fsync(output.fileno())
                os.replace(temporary, self.csv_names_path)
            finally:
                temporary.unlink(missing_ok=True)
            self.csv_numbers = numbers
            self.csv_dates = dates
        return sorted(((path.name, numbers[path.stem]) for path in files),
                      key=lambda item: item[1])

    def csv_filename(self, stem):
        return f'Download_temp_{self.csv_dates[stem]}_{self.csv_numbers[stem]}.csv'

class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def log_message(self, format, *args):
        pass  # Print one useful line per stored/duplicate request below.

    def reply(self, code, key=None, length=0, result=None):
        self.send_response(code)
        self.send_header('Content-Length', '0')
        self.send_header('Connection', 'close')
        if key is not None:
            self.send_header('X-Log-ID', key)
            self.send_header('X-Log-Length', str(length))
            self.send_header('X-Log-Result', result)
        self.end_headers()
        self.close_connection = True

    def save_configuration(self):
        self.connection.settimeout(5)
        for field in ('Content-Type', 'Content-Length'):
            if len(self.headers.get_all(field, [])) != 1:
                self.reply(400); return
        if self.headers.get('Transfer-Encoding'):
            self.reply(400); return
        if self.headers['Content-Type'].lower() != 'application/x-www-form-urlencoded':
            self.reply(415); return
        raw_length = self.headers['Content-Length']
        if not re.fullmatch(r'[0-9]{1,3}', raw_length) or not 1 <= int(raw_length) <= 64:
            self.reply(400); return
        try:
            data = self.rfile.read(int(raw_length))
            if len(data) != int(raw_length):
                self.reply(400); return
            values = parse_qs(data.decode('ascii'), keep_blank_values=True,
                              strict_parsing=True, max_num_fields=1)
            value = values.get('interval_minutes', [])
            if (set(values) != {'interval_minutes'} or len(value) != 1 or
                    not re.fullmatch(r'[0-9]{1,4}', value[0]) or
                    not MIN_INTERVAL_MINUTES <= int(value[0]) <= MAX_INTERVAL_MINUTES):
                self.reply(400); return
            minutes = int(value[0])
        except (ValueError, UnicodeError):
            self.reply(400); return
        except (TimeoutError, socket.timeout):
            self.reply(408); return
        try:
            with self.server.store_lock:
                temporary = self.server.settings_path.with_suffix('.tmp')
                try:
                    with temporary.open('w', encoding='utf-8') as output:
                        json.dump({'interval_minutes': minutes}, output)
                        output.write('\n'); output.flush(); os.fsync(output.fileno())
                    os.replace(temporary, self.server.settings_path)
                finally:
                    temporary.unlink(missing_ok=True)
                self.server.interval_minutes = minutes
        except OSError:
            self.reply(500); return
        print(f'[CONFIG] Saved interval={minutes} minutes; press Wi-Fi GP20 to apply', flush=True)
        self.send_response(303)
        self.send_header('Location', '/')
        self.send_header('Content-Length', '0')
        self.send_header('Connection', 'close')
        self.end_headers(); self.close_connection = True

    def configuration(self):
        with self.server.store_lock:
            minutes = self.server.interval_minutes
        if minutes is None:
            self.reply(503); return  # No silent fallback to the former demo interval.
        timestamp = int(time.time())
        if not 1 <= timestamp <= 0xffffffff:
            self.reply(503); return
        self.send_response(200)
        self.send_header('Content-Length', '0')
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Connection', 'close')
        # Small, versioned headers keep the embedded parser bounded. These are
        # PC/Wi-Fi HTTP settings; the Sensor UART CONFIG stays eight bytes.
        self.send_header('X-Config-Version', '1')
        self.send_header('X-Config-Minutes', str(minutes))
        self.send_header('X-Config-Unix-Seconds', str(timestamp))
        self.end_headers(); self.close_connection = True
        print(f'[CONFIG] GP20 fetch: interval={minutes} minutes timestamp={timestamp} (PC clock)', flush=True)

    def do_POST(self):
        if self.path == '/config':
            self.save_configuration(); return
        self.connection.settimeout(5)
        if self.path != '/logs':
            self.reply(404); return
        # Reject ambiguous framing instead of treating binary as text/chunks.
        for field in ('Content-Type', 'Content-Length', 'X-Log-ID'):
            if len(self.headers.get_all(field, [])) != 1:
                self.reply(400); return
        if self.headers.get('Transfer-Encoding'):
            self.reply(400); return
        if self.headers['Content-Type'].lower() != 'application/octet-stream':
            self.reply(415); return
        raw_length = self.headers['Content-Length']
        if not re.fullmatch(r'[0-9]{1,8}', raw_length):
            self.reply(400); return
        length = int(raw_length)
        if length > MAX_BODY:
            self.reply(413); return
        key = self.headers['X-Log-ID']
        if not re.fullmatch(r'[A-Za-z0-9_-]{1,80}', key):
            self.reply(400); return
        try:
            body = self.rfile.read(length)
        except (TimeoutError, socket.timeout):
            self.reply(408); return
        if len(body) != length:
            self.reply(400); return
        if key.startswith('batch-'):
            self.receive_batch(key, body); return
        if self.server.expect_known and (key != KNOWN_ID or body != KNOWN_BYTES):
            print('[PC] FAIL: known-byte comparison mismatch', flush=True)
            self.reply(422); return
        # A restart still finds the same filename and compares its bytes.
        path = self.server.directory / (hashlib.sha256(key.encode('ascii')).hexdigest() + '.bin')
        try:
            with self.server.store_lock:
                if path.exists():
                    if path.read_bytes() != body:
                        self.reply(409); return
                    result, code = 'duplicate', 200
                else:
                    temporary = path.with_suffix('.tmp')
                    try:
                        with temporary.open('wb') as output:
                            output.write(body)
                            output.flush()
                            os.fsync(output.fileno())
                        os.replace(temporary, path)
                    finally:
                        temporary.unlink(missing_ok=True)
                    result, code = 'stored', 201
                csv_count, csv_error = None, None
                if self.server.csv_exporter:
                    try:
                        csv_count = self.server.csv_exporter(
                            body, path.with_suffix('.csv'), self.server.timestamp_source,
                            log_format=self.server.log_format, diagnostic_columns=self.server.diagnostic_columns)
                        self.server.csv_downloads()
                    except (ValueError, OSError) as error:
                        csv_count = None
                        # Receipts confirm durable binary storage. An export
                        # problem must never discard or alter the original log.
                        csv_error = str(error)
                drop = self.server.drop_next_receipt
                self.server.drop_next_receipt = False
        except OSError:
            self.reply(500); return
        digest = hashlib.sha256(body).hexdigest()
        print(f'[PC] {result}: ID={key} bytes={length} sha256={digest}'
              + (' known-byte comparison=PASS' if self.server.expect_known else ''), flush=True)
        if csv_count is not None:
            print(f'[CSV] samples={csv_count} timestamps={self.server.timestamp_source} format={self.server.log_format} '
                  f'file={path.with_suffix(".csv").resolve()}', flush=True)
            print(f'[CSV] Download: /logs/{path.stem}.csv', flush=True)
        elif csv_error is not None:
            print(f'[CSV] Not exported: {csv_error}; binary preserved', flush=True)
        if drop:
            print('[PC TEST] Receipt dropped once; stored bytes retained', flush=True)
            self.close_connection = True
            return
        self.reply(code, key, length, result)

    def receive_batch(self, key, body):
        """Batch receipts confirm fragments; CSV is exported only after assembly."""
        totals = self.headers.get_all('X-Log-Total', [])
        if (self.server.expect_known or len(totals) != 1 or
                not re.fullmatch(r'[0-9]{1,8}', totals[0])):
            self.reply(400); return
        try:
            with self.server.store_lock:
                result = self.server.batch_storage.store_batch(
                    self.server.directory, key, int(totals[0]), body)
                path = result['path']
                csv_count, csv_error = None, None
                if path is not None and self.server.csv_exporter:
                    try:
                        csv_count = self.server.csv_exporter(
                            path.read_bytes(), path.with_suffix('.csv'), self.server.timestamp_source,
                            log_format=self.server.log_format, diagnostic_columns=self.server.diagnostic_columns)
                        self.server.csv_downloads()
                    except (ValueError, OSError) as error:
                        csv_error = str(error)
                drop = self.server.drop_next_receipt
                self.server.drop_next_receipt = False
        except self.server.batch_storage.BatchError as error:
            print(f'[BATCH] Rejected: {error}', flush=True)
            self.reply(error.status); return
        except OSError:
            self.reply(500); return
        print(f"[BATCH] {result['result']}: offset={result['offset']} bytes={len(body)} "
              f"total={result['total']} complete={int(path is not None)}", flush=True)
        if path is not None:
            print(f"[PC] Complete log verified: bytes={result['total']} file={path.resolve()}", flush=True)
        if csv_count is not None:
            print(f'[CSV] samples={csv_count} file={path.with_suffix(".csv").resolve()}', flush=True)
            print(f'[CSV] Download: /logs/{path.stem}.csv', flush=True)
        elif csv_error is not None:
            print(f'[CSV] Not exported: {csv_error}; binary preserved', flush=True)
        if drop:
            print('[PC TEST] Receipt dropped once; stored batch retained', flush=True)
            self.close_connection = True
            return
        self.reply(200 if result['result'] == 'duplicate' else 201,
                   key, len(body), result['result'])

    def do_GET(self):
        if self.path == '/config':
            self.configuration(); return
        if not self.server.csv_exporter:
            self.reply(405); return
        if self.path == '/':
            try:
                with self.server.store_lock:
                    names = self.server.csv_downloads()
                    minutes = self.server.interval_minutes
            except OSError:
                self.reply(500); return
            shown = '' if minutes is None else str(minutes)
            links = ''.join(f'<li><a href="/logs/{name}">{self.server.csv_filename(Path(name).stem)}</a></li>'
                            for name, number in names)
            body = ('<!doctype html><html lang="en"><meta charset="utf-8">'
                    '<meta name="viewport" content="width=device-width,initial-scale=1">'
                    '<title>Temperature logger</title><h1>Temperature logger</h1>'
                    '<h2>Sampling interval</h2>'
                    '<p>Enter a whole number from 1 to 4095 minutes. Save it, then press GP20 '
                    'on the Wi-Fi Pico to apply it with the current PC time.</p>'
                    '<form method="post" action="/config">'
                    '<label for="interval">Interval (minutes)</label> '
                    f'<input id="interval" name="interval_minutes" type="number" '
                    f'min="1" max="4095" step="1" required value="{shown}"> '
                    '<button type="submit">Save interval</button></form>'
                    + ('<p>No interval saved yet. GP20 will not send CONFIG until one is saved.</p>'
                       if minutes is None else f'<p>Saved interval: <strong>{minutes} minutes</strong>. '
                       'Saving does not send a command to the Sensor.</p>') +
                    '<h2>Temperature logs</h2>'
                    '<p>Temperatures use 0.25 &deg;C steps. CSV timestamps use Singapore time (UTC+8).</p>'
                    f'<p>Current timestamp-source setting: <strong>{html.escape(self.server.timestamp_source)}</strong>. '
                    'Simulated timestamps are test values, not actual measurement dates. '
                    'Timestamp source remains available in diagnostic exports.</p>'
                    f'<p>Current decoder format: <strong>{self.server.log_format}</strong>. '
                    'CSV shows sample number, temperature, Singapore time and status. '
                    'Internal positions and format are optional diagnostic columns.</p>'
                    '<p>Download filenames use the date this PC first received each log in Singapore time. '
                    'Older files use their saved file timestamp. Refresh after uploading. '
                    'Original binary logs remain saved on this PC.</p>'
                    f'<ul>{links}</ul>' + ('<p>No CSV files yet.</p>' if not names else '') + '</html>').encode('utf-8')
            content_type, filename = 'text/html; charset=utf-8', None
        else:
            match = re.fullmatch(r'/logs/([0-9a-f]{64})\.csv', self.path)
            if not match:
                self.reply(404); return
            try:
                body = (self.server.directory / (match[1] + '.csv')).read_bytes()
            except FileNotFoundError:
                self.reply(404); return
            except OSError:
                self.reply(500); return
            content_type = 'text/csv; charset=utf-8'
            try:
                with self.server.store_lock:
                    self.server.csv_downloads()
                    filename = self.server.csv_filename(match[1])
            except (OSError, KeyError):
                self.reply(500); return
        self.send_response(200)
        self.send_header('Content-Type', content_type)
        if filename:
            self.send_header('Content-Disposition', f'attachment; filename="{filename}"')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Connection', 'close')
        self.end_headers()
        self.wfile.write(body)
        self.close_connection = True

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bind', default='0.0.0.0')
    parser.add_argument('--port', type=int, default=8000)
    parser.add_argument('--output', type=Path, default=Path('http_received'))
    parser.add_argument('--expect-known', action='store_true')
    parser.add_argument('--decode', '--decode-jz', dest='decode_jz', action='store_true',
                        help='Decode temperature pages and export CSV; enable browser downloads')
    parser.add_argument('--timestamp-source', choices=('unconfirmed', 'simulated', 'pc-clock', 'rtc'),
                        default='unconfirmed', help='Label the source of encoded timestamps; does not change them')
    parser.add_argument('--drop-first-receipt', action='store_true',
                        help='Test only: close the first valid POST after storage without replying')
    parser.add_argument('--log-format', choices=LOG_FORMATS, default='legacy16',
                        help='Use interval12 for new Sensor firmware; legacy16 for old logs')
    parser.add_argument('--diagnostic-columns', action='store_true',
                        help='Include UTC time, timestamp source and internal log details')
    args = parser.parse_args()
    if args.expect_known and args.decode_jz:
        parser.error('--expect-known bytes are not a JZ temperature log; omit --decode')
    with Receiver((args.bind, args.port), args.output, args.expect_known,
                  args.decode_jz, args.timestamp_source, args.log_format, args.diagnostic_columns) as server:
        server.drop_next_receipt = args.drop_first_receipt
        print(f'[PC] Listening on {args.bind}:{server.server_port}/logs; output={args.output.resolve()}', flush=True)
        if args.expect_known:
            print('[PC] Expecting bytes 00..FF repeated four times (1024 bytes)', flush=True)
        if args.decode_jz:
            print(f'[CSV] Temperature decoding enabled; timestamps={args.timestamp_source}; format={args.log_format}; original binaries retained', flush=True)
            host = '127.0.0.1' if args.bind == '0.0.0.0' else args.bind
            print(f'[CSV] Configure/view/download: http://{host}:{server.server_port}/', flush=True)
        if args.drop_first_receipt:
            print('[PC TEST] First valid POST receipt will be dropped once after storage', flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass

if __name__ == '__main__':
    main()
