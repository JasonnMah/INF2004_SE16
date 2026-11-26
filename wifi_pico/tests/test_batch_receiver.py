"""Host/localhost tests; no physical Pico, UART, flash or SMP claim."""
from pathlib import Path
import hashlib
import http.client
import importlib.util
import json
import socket
import tempfile
import threading
import unittest

W = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('receiver', W / 'tools/http_receiver.py')
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)


class BatchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.start()

    def start(self):
        self.server = r.Receiver(('127.0.0.1', 0), self.temp.name)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def stop(self):
        self.server.shutdown(); self.server.server_close(); self.thread.join()

    def tearDown(self):
        self.stop(); self.temp.cleanup()

    def post(self, complete, offset, body=None, digest=None, total=None, extra=None):
        digest = digest or hashlib.sha256(complete).hexdigest()
        key = f'batch-{digest}-{offset:08x}'
        headers = {'Content-Type': 'application/octet-stream', 'X-Log-ID': key,
                   'X-Log-Total': str(len(complete) if total is None else total)}
        if extra:
            headers.update(extra)
        connection = http.client.HTTPConnection('127.0.0.1', self.server.server_port, timeout=5)
        try:
            connection.request('POST', '/logs', complete[offset:offset + 4096] if body is None else body, headers)
            response = connection.getresponse(); response.read()
            return response.status, dict(response.getheaders())
        finally:
            connection.close()

    def test_restart_duplicates_partial_tail_and_complete_hash(self):
        content = bytes(range(256)) * 33 + b'last'
        self.assertEqual(self.post(content, 0)[0], 201)
        self.assertFalse(list(Path(self.temp.name).glob('*.bin')))
        self.assertFalse(list(Path(self.temp.name).glob('*.csv')))
        self.stop(); self.start()
        self.assertEqual(self.post(content, 0)[0], 200)
        self.assertEqual(self.post(content, 8192)[0], 201)  # Out of order remains incomplete.
        self.assertFalse(list(Path(self.temp.name).glob('*.bin')))
        self.assertEqual(self.post(content, 4096)[0], 201)
        files = list(Path(self.temp.name).glob('*.bin'))
        self.assertEqual(len(files), 1)
        self.assertEqual(files[0].read_bytes(), content)
        original_time = files[0].stat().st_mtime_ns
        self.assertEqual(self.post(content, 8192)[0], 200)
        self.assertEqual(files[0].stat().st_mtime_ns, original_time)

    def test_lost_receipt_and_conflicts(self):
        content = b'A' * 8192
        self.server.drop_next_receipt = True
        with self.assertRaises((http.client.RemoteDisconnected, ConnectionResetError, socket.timeout)):
            self.post(content, 0)
        self.stop(); self.start()
        self.assertEqual(self.post(content, 0)[0], 200)
        self.assertEqual(self.post(content, 0, b'B' * 4096)[0], 409)
        self.assertEqual(self.post(content, 0, total=12288)[0], 409)
        self.assertEqual(self.post(content, 4096)[0], 201)

    def test_limits_damage_and_empty(self):
        content = b'Z' * 4096
        self.assertEqual(self.post(content, 1)[0], 400)
        self.assertEqual(self.post(content, 4096)[0], 400)
        self.assertEqual(self.post(content, 0, body=b'short')[0], 400)
        self.assertEqual(self.post(content, 0, total=1048577)[0], 400)
        self.assertEqual(self.post(content, 0, digest='0' * 64)[0], 422)
        self.assertFalse(list(Path(self.temp.name).glob('*.bin')))
        self.assertEqual(self.post(b'', 0)[0], 201)
        self.assertEqual(self.post(b'', 0)[0], 200)

    def test_full_one_mib_backend_without_whole_log_http_request(self):
        content = bytes(range(256)) * 4096
        digest = hashlib.sha256(content).hexdigest()
        storage = self.server.batch_storage
        for offset in range(0, len(content), 4096):
            result = storage.store_batch(self.temp.name, f'batch-{digest}-{offset:08x}',
                                         len(content), content[offset:offset + 4096])
            self.assertEqual(result['path'] is not None, offset == len(content) - 4096)
        self.assertEqual(result['path'].read_bytes(), content)

    def test_csv_runs_only_after_full_log_and_survives_receiver_restart(self):
        # A mocked exporter proves timing/whole-body handoff, not real decoding.
        calls = []
        content = b'C' * 4352
        def export(data, path, *args, **kwargs):
            self.assertEqual(data, content)
            calls.append(len(data)); path.write_text('sample,temperature_c,timestamp_sgt,status\n')
            return 1
        self.server.csv_exporter = export
        self.assertEqual(self.post(content, 0)[0], 201)
        self.assertEqual(calls, [])
        self.stop(); self.start(); self.server.csv_exporter = export
        self.assertEqual(self.post(content, 4096)[0], 201)
        self.assertEqual(calls, [4352])
        self.assertEqual(len(list(Path(self.temp.name).glob('*.csv'))), 1)


if __name__ == '__main__':
    unittest.main(verbosity=2)
