#!/usr/bin/env python3
"""PC binary receiver with optional JZ temperature decoding/CSV downloads.
An ID always names the same immutable body. Identical retries return a receipt
without another file; reusing an ID for different bytes returns HTTP 409.
"""
import argparse
import hashlib
import html
import importlib.util
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import os
import re
import socket
import threading

KNOWN_ID = 'pico-known-1024-v1'
KNOWN_BYTES = bytes(range(256)) * 4
MAX_BODY = 65536

class Receiver(ThreadingHTTPServer):
    daemon_threads = True
    def __init__(self, address, directory, expect_known=False, decode_jz=False,
                 timestamp_source='unconfirmed'):
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
        self.drop_next_receipt = False  # Test-only: committed storage, lost reply.
        super().__init__(address, Handler)

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

    def do_POST(self):
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
                            body, path.with_suffix('.csv'), self.server.timestamp_source)
                    except (ValueError, OSError) as error:
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
            print(f'[CSV] samples={csv_count} timestamps={self.server.timestamp_source} '
                  f'file={path.with_suffix(".csv").resolve()}', flush=True)
            print(f'[CSV] Download: /logs/{path.stem}.csv', flush=True)
        elif csv_error is not None:
            print(f'[CSV] Not exported: {csv_error}; binary preserved', flush=True)
        if drop:
            print('[PC TEST] Receipt dropped once; stored bytes retained', flush=True)
            self.close_connection = True
            return
        self.reply(code, key, length, result)

    def do_GET(self):
        if not self.server.csv_exporter:
            self.reply(405); return
        if self.path == '/':
            names = sorted(path.name for path in self.server.directory.glob('*.csv')
                           if re.fullmatch(r'[0-9a-f]{64}\.csv', path.name))
            links = ''.join(f'<li><a href="/logs/{name}">Download temperature-log-{name[:12]}.csv</a></li>'
                            for name in names)
            body = ('<!doctype html><html lang="en"><meta charset="utf-8">'
                    '<meta name="viewport" content="width=device-width,initial-scale=1">'
                    '<title>Temperature logs</title><h1>Temperature logs</h1>'
                    '<p>Temperatures use 0.25 &deg;C steps. CSV includes UTC and Singapore time.</p>'
                    f'<p>Current timestamp-source setting: <strong>{html.escape(self.server.timestamp_source)}</strong>. '
                    'Simulated timestamps are test values, not actual measurement dates. '
                    'Each CSV records its own timestamp-source label.</p>'
                    '<p>Refresh after uploading. Original binary logs remain saved on this PC.</p>'
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
            filename = f'temperature-log-{match[1][:12]}.csv'
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
    parser.add_argument('--decode-jz', action='store_true',
                        help='Validate JZ encoder pages and export CSV; enable browser downloads')
    parser.add_argument('--timestamp-source', choices=('unconfirmed', 'simulated', 'rtc'),
                        default='unconfirmed', help='Label the source of encoded timestamps; does not change them')
    parser.add_argument('--drop-first-receipt', action='store_true',
                        help='Test only: close the first valid POST after storage without replying')
    args = parser.parse_args()
    if args.expect_known and args.decode_jz:
        parser.error('--expect-known bytes are not a JZ temperature log; omit --decode-jz')
    with Receiver((args.bind, args.port), args.output, args.expect_known,
                  args.decode_jz, args.timestamp_source) as server:
        server.drop_next_receipt = args.drop_first_receipt
        print(f'[PC] Listening on {args.bind}:{server.server_port}/logs; output={args.output.resolve()}', flush=True)
        if args.expect_known:
            print('[PC] Expecting bytes 00..FF repeated four times (1024 bytes)', flush=True)
        if args.decode_jz:
            print(f'[CSV] JZ decoding enabled; timestamps={args.timestamp_source}; original binaries retained', flush=True)
            host = '127.0.0.1' if args.bind == '0.0.0.0' else args.bind
            print(f'[CSV] View/download: http://{host}:{server.server_port}/', flush=True)
        if args.drop_first_receipt:
            print('[PC TEST] First valid POST receipt will be dropped once after storage', flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass

if __name__ == '__main__':
    main()
