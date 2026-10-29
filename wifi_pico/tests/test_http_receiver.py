#!/usr/bin/env python3
"""Local PC tests. These do not exercise Pico RF, the kernel, or UART."""
from pathlib import Path
import importlib.util
import http.client
import socket
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest.mock import Mock
import csv
import io
W=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('receiver',W/'tools/http_receiver.py')
r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)

class ReceiverTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.server=r.Receiver(('127.0.0.1',0),self.temp.name)
        self.thread=threading.Thread(target=self.server.serve_forever,daemon=True);self.thread.start()
    def tearDown(self):
        self.server.shutdown();self.server.server_close();self.thread.join();self.temp.cleanup()
    def post(self,body=r.KNOWN_BYTES,key=r.KNOWN_ID,extra=None,path='/logs'):
        conn=http.client.HTTPConnection('127.0.0.1',self.server.server_port,timeout=2)
        headers={'Content-Type':'application/octet-stream','X-Log-ID':key}
        if extra:headers.update(extra)
        try:
            conn.request('POST',path,body,headers)
            response=conn.getresponse();data=response.read()
            return response.status,dict(response.getheaders()),data
        finally:conn.close()
    def test_binary_retry_and_persistent_dedup(self):
        self.assertEqual(self.post()[0],201)
        files=list(Path(self.temp.name).glob('*.bin'));self.assertEqual(len(files),1)
        self.assertEqual(files[0].read_bytes(),r.KNOWN_BYTES)
        self.assertEqual(self.post()[0],200)
        # Restart the receiver: dedup is based on persisted bytes, not a RAM set.
        self.server.shutdown();self.server.server_close();self.thread.join()
        self.server=r.Receiver(('127.0.0.1',0),self.temp.name)
        self.thread=threading.Thread(target=self.server.serve_forever,daemon=True);self.thread.start()
        code,receipt,body=self.post()
        self.assertEqual(code,200);self.assertEqual(body,b'')
        self.assertEqual(receipt['X-Log-Length'],'1024');self.assertEqual(receipt['X-Log-Result'],'duplicate')
        self.assertEqual(len(list(Path(self.temp.name).glob('*.bin'))),1)
    def test_lost_reply_after_storage(self):
        self.server.drop_next_receipt=True
        with self.assertRaises(http.client.RemoteDisconnected):self.post()
        self.assertEqual(self.post()[0],200)
        self.assertEqual(len(list(Path(self.temp.name).glob('*.bin'))),1)
    def test_key_collision_different_bytes(self):
        self.assertEqual(self.post()[0],201)
        self.assertEqual(self.post(b'other')[0],409)
    def test_known_comparison(self):
        self.server.expect_known=True
        self.assertEqual(self.post(b'wrong')[0],422)
        self.assertEqual(self.post()[0],201)
    def test_invalid_requests(self):
        for body,key,extra,path,expected in [
            (b'x','../bad',None,'/logs',400),
            (b'x','a',{'Content-Type':'text/plain'},'/logs',415),
            (b'x','a',{'Content-Length':'-1'},'/logs',400),
            (b'x','a',{'Content-Length':'65537'},'/logs',413),
            (b'x','a',{'Transfer-Encoding':'chunked'},'/logs',400),
            (b'x','a',None,'/wrong',404)]:
            self.assertEqual(self.post(body,key,extra,path)[0],expected)
        self.assertFalse(list(Path(self.temp.name).glob('*.bin')))
    def test_truncated_body_and_duplicate_length(self):
        for request in [
            b'POST /logs HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/octet-stream\r\nX-Log-ID: x\r\nContent-Length: 10\r\n\r\na',
            b'POST /logs HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/octet-stream\r\nX-Log-ID: x\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\na']:
            with socket.create_connection(('127.0.0.1',self.server.server_port),timeout=2) as sock:
                sock.sendall(request);sock.shutdown(socket.SHUT_WR)
                self.assertIn(b'400',sock.recv(4096).split(b'\r\n')[0])
        self.assertFalse(list(Path(self.temp.name).glob('*.bin')))
    @unittest.skipUnless(sys.platform == "linux", "C socket adapter runs in WSL; PC receiver tests still run on Windows")
    def test_real_client_over_host_tcp(self):
        self.server.expect_known=True
        executable=W/'course/build_make/http_host/http_socket_adapter'
        for lost_reply in (False,True):
            self.server.drop_next_receipt=lost_reply
            result=subprocess.run([str(executable),str(self.server.server_port)],capture_output=True,text=True,timeout=20)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('attempts='+('2' if lost_reply else '1'),result.stdout)
            print(result.stdout,end='')
        files=list(Path(self.temp.name).glob('*.bin'))
        self.assertEqual(len(files),1)
        self.assertEqual(files[0].read_bytes(),r.KNOWN_BYTES)
    def test_concurrent_retries(self):
        results=[]
        threads=[threading.Thread(target=lambda:results.append(self.post()[0])) for _ in range(4)]
        for thread in threads:thread.start()
        for thread in threads:thread.join()
        self.assertEqual(sorted(results),[200,200,200,201])
        self.assertEqual(len(list(Path(self.temp.name).glob('*.bin'))),1)

    def enable_csv(self):
        self.server.shutdown(); self.server.server_close(); self.thread.join()
        self.server = r.Receiver(('127.0.0.1', 0), self.temp.name,
                                 decode_jz=True, timestamp_source='simulated')
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.capture = bytes.fromhex('73716553f100003c8111716553f100003c8111741330ffffffffffffffffffff') + b'\xff' * 224
        self.capture_key = 'log-' + r.hashlib.sha256(self.capture).hexdigest()
        self.capture_stem = r.hashlib.sha256(self.capture_key.encode('ascii')).hexdigest()

    def get(self, path):
        conn = http.client.HTTPConnection('127.0.0.1', self.server.server_port, timeout=2)
        try:
            conn.request('GET', path)
            response = conn.getresponse()
            return response.status, dict(response.getheaders()), response.read()
        finally:
            conn.close()

    def test_csv_download_from_real_capture(self):
        self.enable_csv()
        self.assertEqual(self.post(self.capture, self.capture_key)[0], 201)
        code, headers, body = self.get(f'/logs/{self.capture_stem}.csv')
        self.assertEqual(code, 200)
        self.assertIn('attachment;', headers['Content-Disposition'])
        rows = list(csv.DictReader(io.StringIO(body.decode('utf-8-sig'))))
        self.assertEqual([row['temperature_c'] for row in rows], ['28.25', '28.25'])
        self.assertEqual(rows[0]['timestamp_source'], 'simulated')
        self.assertEqual(self.get('/')[0], 200)
        self.assertIn(f'/logs/{self.capture_stem}.csv'.encode(), self.get('/')[2])
        self.assertEqual((Path(self.temp.name) / (self.capture_stem + '.bin')).read_bytes(), self.capture)

    @unittest.skipUnless(sys.platform == 'linux', 'Real C socket client runs in WSL')
    def test_real_c_client_with_csv_and_lost_receipt(self):
        self.enable_csv()
        payload = Path(self.temp.name) / 'input.payload'
        payload.write_bytes(self.capture)
        executable = W / 'course/build_make/http_host/http_socket_adapter'
        for lost_receipt, attempts in ((False, 1), (True, 2)):
            self.server.drop_next_receipt = lost_receipt
            result = subprocess.run([str(executable), str(self.server.server_port), str(payload),
                                     self.capture_key], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(f'attempts={attempts}', result.stdout)
        self.assertEqual(self.get(f'/logs/{self.capture_stem}.csv')[0], 200)
        self.assertEqual(len(list(Path(self.temp.name).glob('*.bin'))), 1)
        self.assertEqual(len(list(Path(self.temp.name).glob('*.csv'))), 1)

    def test_csv_lost_receipt_dedup_and_missing_csv_recovery(self):
        self.enable_csv()
        self.server.drop_next_receipt = True
        with self.assertRaises(http.client.RemoteDisconnected):
            self.post(self.capture, self.capture_key)
        self.assertEqual(self.post(self.capture, self.capture_key)[0], 200)
        output = Path(self.temp.name) / (self.capture_stem + '.csv')
        first = output.read_bytes()
        output.unlink()
        self.assertEqual(self.post(self.capture, self.capture_key)[0], 200)
        self.assertEqual(output.read_bytes(), first)
        self.assertEqual(len(list(Path(self.temp.name).glob('*.bin'))), 1)
        self.assertEqual(len(list(Path(self.temp.name).glob('*.csv'))), 1)

    def test_csv_damage_or_wrong_format_keeps_raw_binary(self):
        self.enable_csv()
        damaged = bytearray(self.capture); damaged[19] ^= 1
        for index, body in enumerate((b'bad', bytes(damaged))):
            self.assertEqual(self.post(body, f'invalid-{index}')[0], 201)
        self.assertEqual(len(list(Path(self.temp.name).glob('*.bin'))), 2)
        self.assertEqual(len(list(Path(self.temp.name).glob('*.csv'))), 0)

    def test_csv_disk_error_does_not_undo_binary_receipt(self):
        self.enable_csv()
        self.server.csv_exporter = Mock(side_effect=OSError('simulated CSV disk error'))
        self.assertEqual(self.post(self.capture, self.capture_key)[0], 201)
        self.assertEqual(len(list(Path(self.temp.name).glob('*.bin'))), 1)
        self.assertEqual(len(list(Path(self.temp.name).glob('*.csv'))), 0)

    def test_csv_concurrent_duplicate_uploads(self):
        self.enable_csv()
        results = []
        threads = [threading.Thread(target=lambda: results.append(self.post(self.capture, self.capture_key)[0]))
                   for _ in range(4)]
        for thread in threads: thread.start()
        for thread in threads: thread.join()
        self.assertEqual(sorted(results), [200, 200, 200, 201])
        self.assertEqual(self.get(f'/logs/{self.capture_stem}.csv')[0], 200)
        self.assertEqual(len(list(Path(self.temp.name).glob('*.csv'))), 1)
        self.assertFalse(list(Path(self.temp.name).glob('*.tmp')))

    def test_csv_download_rejects_other_files_and_paths(self):
        self.enable_csv()
        for path in ('/../sensor_pico/main.c', '/logs/../../file.csv', '/logs/%2e%2e/file.csv',
                     '/logs/' + 'a' * 64 + '.csv', '/logs/' + 'a' * 64 + '.bin'):
            self.assertEqual(self.get(path)[0], 404)

    def test_downloads_off_by_default_and_known_mode_separate(self):
        self.assertEqual(self.get('/')[0], 405)
        result = subprocess.run([sys.executable, '-B', str(W / 'tools/http_receiver.py'),
                                 '--expect-known', '--decode-jz'], capture_output=True)
        self.assertNotEqual(result.returncode, 0)

if __name__=='__main__':unittest.main(verbosity=2)
