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
import json
from datetime import datetime, timezone
from unittest.mock import patch
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
    def save_interval(self, value):
        conn=http.client.HTTPConnection('127.0.0.1',self.server.server_port,timeout=2)
        try:
            conn.request('POST','/config',value,
                         {'Content-Type':'application/x-www-form-urlencoded'})
            response=conn.getresponse();response.read()
            return response.status,dict(response.getheaders())
        finally:conn.close()

    def test_interval_form_bounds_and_no_automatic_command(self):
        self.enable_csv('interval12')
        html_body=self.get('/')[2]
        self.assertIn(b'min="1" max="4095" step="1"',html_body)
        self.assertEqual(self.get('/config')[0],503)
        for value in ('1','4095'):
            self.assertEqual(self.save_interval('interval_minutes='+value)[0],303)
            code,headers,body=self.get('/config')
            self.assertEqual(code,200);self.assertEqual(body,b'')
            self.assertEqual(headers['X-Config-Minutes'],value)
            self.assertEqual(headers['X-Config-Version'],'1')
            self.assertEqual(headers['Cache-Control'],'no-store')
            self.assertGreater(int(headers['X-Config-Unix-Seconds']),1700000000)
        self.assertFalse(list(Path(self.temp.name).glob('*.bin')))
        self.assertEqual(json.loads(self.server.settings_path.read_text()),{'interval_minutes':4095})

    def test_invalid_interval_does_not_replace_saved_setting(self):
        self.assertEqual(self.save_interval('interval_minutes=2')[0],303)
        before=self.server.settings_path.read_bytes()
        for value in ('0','4096','-1','1.5','1e2','','+1',' 1','true','9999999999'):
            self.assertEqual(self.save_interval('interval_minutes='+value)[0],400)
        for body in ('wrong=1','interval_minutes=1&interval_minutes=2','interval_minutes=1&x=2',
                     'interval_minutes=%FF','interval_minutes='+'1'*80):
            self.assertEqual(self.save_interval(body)[0],400)
        self.assertEqual(self.server.settings_path.read_bytes(),before)
        self.assertEqual(self.get('/config')[1]['X-Config-Minutes'],'2')

    def test_saved_interval_persists_but_timestamp_is_fresh(self):
        self.assertEqual(self.save_interval('interval_minutes=7')[0],303)
        self.server.shutdown();self.server.server_close();self.thread.join()
        self.server=r.Receiver(('127.0.0.1',0),self.temp.name)
        self.thread=threading.Thread(target=self.server.serve_forever,daemon=True);self.thread.start()
        for timestamp in (1793491200,1793491260):
            with patch.object(r.time,'time',return_value=timestamp):
                code,headers,_=self.get('/config')
            self.assertEqual(code,200)
            self.assertEqual(headers['X-Config-Unix-Seconds'],str(timestamp))
            self.assertEqual(headers['X-Config-Minutes'],'7')
        with patch.object(r.time,'time',return_value=4294967296):
            self.assertEqual(self.get('/config')[0],503)

    @unittest.skipUnless(sys.platform=='linux','Real C socket client runs in WSL')
    def test_real_c_config_client_uses_pc_headers(self):
        self.assertEqual(self.save_interval('interval_minutes=4095')[0],303)
        executable=W/'course/build_make/http_host/http_socket_adapter'
        with patch.object(r.time,'time',return_value=1793491200):
            result=subprocess.run([str(executable),str(self.server.server_port),'config'],
                                  capture_output=True,text=True,timeout=20)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('HOST CONFIG minutes=4095 timestamp=1793491200',result.stdout)

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

    def enable_csv(self, log_format='legacy16', diagnostic_columns=False):
        self.server.shutdown(); self.server.server_close(); self.thread.join()
        self.server = r.Receiver(('127.0.0.1', 0), self.temp.name,
                                 decode_jz=True, timestamp_source='simulated', log_format=log_format,
                                 diagnostic_columns=diagnostic_columns)
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

    def test_interval12_http_csv_lost_receipt_and_legacy_mismatch(self):
        from test_temperature_csv import start, seal, page
        self.enable_csv('interval12', diagnostic_columns=True)
        body = page(seal(start(minutes=4095, interval_nibbles=3) + [1]))
        key = 'new-format-log'
        stem = r.hashlib.sha256(key.encode('ascii')).hexdigest()
        self.server.drop_next_receipt = True
        with self.assertRaises(http.client.RemoteDisconnected):
            self.post(body, key)
        self.assertEqual(self.post(body, key)[0], 200)
        code, _, csv_body = self.get(f'/logs/{stem}.csv')
        self.assertEqual(code, 200)
        rows = list(csv.DictReader(io.StringIO(csv_body.decode('utf-8-sig'))))
        self.assertEqual([row['log_format'] for row in rows], ['interval12'] * 2)
        self.assertTrue(all(row['timestamp_source'] == 'simulated' for row in rows))
        self.assertEqual([row['temperature_c'] for row in rows], ['28.25', '28.50'])
        self.assertEqual(int(rows[1]['unix_seconds']) - int(rows[0]['unix_seconds']), 245700)
        self.assertEqual((Path(self.temp.name) / (stem + '.bin')).read_bytes(), body)
        self.assertIn(b'interval12', self.get('/')[2])
        # A different encoder format cannot produce a misleading CSV, but its
        # raw bytes must still be stored and acknowledged for later decoding.
        self.assertEqual(self.post(self.capture, self.capture_key)[0], 201)
        self.assertEqual(self.get(f'/logs/{self.capture_stem}.csv')[0], 404)
        self.assertEqual((Path(self.temp.name) / (self.capture_stem + '.bin')).read_bytes(), self.capture)

    def test_csv_download_from_real_capture(self):
        self.enable_csv()
        self.assertEqual(self.post(self.capture, self.capture_key)[0], 201)
        code, headers, body = self.get(f'/logs/{self.capture_stem}.csv')
        self.assertEqual(code, 200)
        self.assertRegex(headers['Content-Disposition'], r'^attachment; filename="Download_temp_[0-9]{4}-[0-9]{2}-[0-9]{2}_1\.csv"$')
        reader = csv.DictReader(io.StringIO(body.decode('utf-8-sig')))
        self.assertEqual(reader.fieldnames, ['sample', 'temperature_c', 'timestamp_sgt', 'status'])
        rows = list(reader)
        self.assertEqual([row['temperature_c'] for row in rows], ['28.25', '28.25'])
        self.assertNotIn('timestamp_source', rows[0])
        self.assertEqual(rows[0]['timestamp_sgt'], '2023-11-15 06:13:20')
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

    def test_csv_numbered_downloads_and_numeric_page_order(self):
        self.enable_csv()
        for number in range(1, 13):
            key = f'numbered-log-{number}'
            self.assertEqual(self.post(self.capture, key)[0], 201)
            stem = r.hashlib.sha256(key.encode('ascii')).hexdigest()
            code, headers, body = self.get(f'/logs/{stem}.csv')
            self.assertEqual(code, 200)
            self.assertRegex(headers['Content-Disposition'],
                             rf'^attachment; filename="Download_temp_[0-9]{{4}}-[0-9]{{2}}-[0-9]{{2}}_{number}\.csv"$')
            if number == 1:
                first = body
            else:
                self.assertEqual(body, first)  # Naming never changes CSV contents.
        page = self.get('/')[2].decode()
        numbers = r.re.findall(r'>Download_temp_[0-9]{4}-[0-9]{2}-[0-9]{2}_([0-9]+)\.csv<', page)
        self.assertEqual(list(map(int, numbers)), list(range(1, 13)))
        self.assertEqual(self.post(self.capture, 'numbered-log-1')[0], 200)
        self.assertEqual(len(self.server.csv_numbers), 12)
        self.assertEqual(len(list(Path(self.temp.name).glob('*.csv'))), 12)

    def test_csv_numbers_survive_restart(self):
        self.enable_csv()
        self.assertEqual(self.post(self.capture, self.capture_key)[0], 201)
        self.enable_csv()  # Same output folder and persisted index.
        self.assertEqual(self.post(self.capture, self.capture_key)[0], 200)
        self.assertRegex(self.get(f'/logs/{self.capture_stem}.csv')[1]['Content-Disposition'],
                         r'^attachment; filename="Download_temp_[0-9]{4}-[0-9]{2}-[0-9]{2}_1\.csv"$')
        self.assertEqual(self.post(self.capture, 'second-log')[0], 201)
        second = r.hashlib.sha256(b'second-log').hexdigest()
        self.assertRegex(self.get(f'/logs/{second}.csv')[1]['Content-Disposition'],
                         r'^attachment; filename="Download_temp_[0-9]{4}-[0-9]{2}-[0-9]{2}_2\.csv"$')

    def test_csv_existing_files_get_names_without_renaming(self):
        self.enable_csv()
        directory = Path(self.temp.name)
        older = directory / ('b' * 64 + '.csv')
        newer = directory / ('a' * 64 + '.csv')
        older.write_bytes(b'sample,temperature_c\n1,25.00\n')
        newer.write_bytes(b'sample,temperature_c\n1,26.00\n')
        r.os.utime(older, (100, 100)); r.os.utime(newer, (200, 200))
        self.enable_csv()
        for number, path in enumerate((older, newer), 1):
            code, headers, body = self.get(f'/logs/{path.name}')
            self.assertEqual(code, 200)
            self.assertEqual(headers['Content-Disposition'],
                             f'attachment; filename="Download_temp_1970-01-01_{number}.csv"')
            self.assertEqual(body, path.read_bytes())
        self.assertTrue(older.exists() and newer.exists())

    def test_csv_deleted_file_does_not_reuse_its_number(self):
        self.enable_csv()
        self.assertEqual(self.post(self.capture, self.capture_key)[0], 201)
        (Path(self.temp.name) / (self.capture_stem + '.csv')).unlink()
        self.assertEqual(self.post(self.capture, 'next-log')[0], 201)
        next_stem = r.hashlib.sha256(b'next-log').hexdigest()
        self.assertRegex(self.get(f'/logs/{next_stem}.csv')[1]['Content-Disposition'], r'Download_temp_[0-9]{4}-[0-9]{2}-[0-9]{2}_2\.csv')
        self.assertEqual(self.post(self.capture, self.capture_key)[0], 200)
        self.assertRegex(self.get(f'/logs/{self.capture_stem}.csv')[1]['Content-Disposition'], r'Download_temp_[0-9]{4}-[0-9]{2}-[0-9]{2}_1\.csv')

    def test_csv_naming_error_preserves_binary_receipt(self):
        self.enable_csv()
        with patch.object(self.server, 'csv_downloads', side_effect=OSError('index disk error')):
            self.assertEqual(self.post(self.capture, self.capture_key)[0], 201)
            self.assertEqual(self.get('/')[0], 500)
        original = Path(self.temp.name) / (self.capture_stem + '.bin')
        self.assertEqual(original.read_bytes(), self.capture)
        self.assertEqual(self.get(f'/logs/{self.capture_stem}.csv')[0], 200)
        self.assertRegex(self.get(f'/logs/{self.capture_stem}.csv')[1]['Content-Disposition'], r'Download_temp_[0-9]{4}-[0-9]{2}-[0-9]{2}_1\.csv')
        self.assertFalse(list(Path(self.temp.name).glob('*.tmp')))

    def test_csv_receipt_date_singapore_midnight_and_old_index_migration(self):
        self.enable_csv()
        keys = [self.capture_key, 'after-midnight']
        moments = [datetime(2026, 11, 5, 15, 59, tzinfo=timezone.utc).timestamp(),
                   datetime(2026, 11, 5, 16, 1, tzinfo=timezone.utc).timestamp()]
        expected = ['2026-11-05', '2026-11-06']
        for key, moment in zip(keys, moments):
            self.assertEqual(self.post(self.capture, key)[0], 201)
            stem = r.hashlib.sha256(key.encode('ascii')).hexdigest()
            binary = Path(self.temp.name) / (stem + '.bin')
            r.os.utime(binary, (moment, moment))
        original_numbers = dict(self.server.csv_numbers)
        self.server.csv_names_path.write_text(json.dumps({'version': 1, 'logs': original_numbers}))
        self.enable_csv()
        self.assertEqual(self.server.csv_numbers, original_numbers)
        saved = json.loads(self.server.csv_names_path.read_text())
        self.assertEqual(saved['version'], 2)
        for number, (key, received_date) in enumerate(zip(keys, expected), 1):
            stem = r.hashlib.sha256(key.encode('ascii')).hexdigest()
            self.assertEqual(self.get(f'/logs/{stem}.csv')[1]['Content-Disposition'],
                             f'attachment; filename="Download_temp_{received_date}_{number}.csv"')
            self.assertEqual(saved['received_dates'][stem], received_date)
            self.assertEqual((Path(self.temp.name) / (stem + '.bin')).read_bytes(), self.capture)

    def test_csv_receipt_date_not_sample_download_or_retry_date(self):
        self.enable_csv()
        self.assertEqual(self.post(self.capture, self.capture_key)[0], 201)
        binary = Path(self.temp.name) / (self.capture_stem + '.bin')
        output = binary.with_suffix('.csv')
        old_csv = output.read_bytes()  # These samples have 2023 timestamps.
        received = datetime(2026, 11, 5, 2, tzinfo=timezone.utc).timestamp()
        later = datetime(2026, 11, 9, 2, tzinfo=timezone.utc).timestamp()
        r.os.utime(binary, (received, received)); r.os.utime(output, (later, later))
        self.server.csv_names_path.write_text(json.dumps({'version': 1, 'logs': self.server.csv_numbers}))
        self.enable_csv()
        expected = 'attachment; filename="Download_temp_2026-11-05_1.csv"'
        self.assertEqual(self.get(f'/logs/{self.capture_stem}.csv')[1]['Content-Disposition'], expected)
        # After the initial assignment, stored metadata survives future changes
        # to file times, CSV regeneration, retries and receiver restarts.
        r.os.utime(binary, (later, later))
        self.assertEqual(self.post(self.capture, self.capture_key)[0], 200)
        self.enable_csv()
        code, headers, body = self.get(f'/logs/{self.capture_stem}.csv')
        self.assertEqual(code, 200)
        self.assertEqual(headers['Content-Disposition'], expected)
        self.assertEqual(body, old_csv)

    def test_invalid_saved_csv_receipt_date_does_not_renumber(self):
        self.enable_csv()
        self.assertEqual(self.post(self.capture, self.capture_key)[0], 201)
        index = self.server.csv_names_path
        saved = json.loads(index.read_text())
        saved['received_dates'][self.capture_stem] = '2026-02-30'
        index.write_text(json.dumps(saved)); before = index.read_bytes()
        with self.assertRaisesRegex(ValueError, 'Invalid saved CSV receipt dates'):
            r.Receiver(('127.0.0.1', 0), self.temp.name, decode_jz=True)
        self.assertEqual(index.read_bytes(), before)
        self.assertEqual((Path(self.temp.name) / (self.capture_stem + '.bin')).read_bytes(), self.capture)

    def test_downloads_off_by_default_and_known_mode_separate(self):
        self.assertEqual(self.get('/')[0], 405)
        for option in ('--decode', '--decode-jz'):
            with self.subTest(option=option):
                result = subprocess.run([sys.executable, '-B', str(W / 'tools/http_receiver.py'),
                                         '--expect-known', option], capture_output=True)
                self.assertNotEqual(result.returncode, 0)

if __name__=='__main__':unittest.main(verbosity=2)
