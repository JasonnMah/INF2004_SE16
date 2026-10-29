#!/usr/bin/env python3
"""PC-only checks of JZ decoding, validation and CSV export; no Pico hardware."""
import csv
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

W = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('temperature_csv', W / 'tools/temperature_csv.py')
exporter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(exporter)

# The user's 2026-10-29 physical UART capture: two CONFIG-triggered samples.
CAPTURE = bytes.fromhex('73716553f100003c8111716553f100003c8111741330ffffffffffffffffffff') + b'\xff' * 224


def field(value, length):
    return [int(char, 16) for char in f'{value:0{length}x}']


def start(q=273, minutes=60, timestamp=1700000000):
    return [7, 3, 7, 1, *field(timestamp, 8), *field(minutes, 4), 8, *field(q, 3)]


def seal(nibs):
    return [*nibs, 7, 4, *field(exporter.jz_decoder().crc16_nibbles(nibs), 4)]


def page(nibs):
    assert len(nibs) <= 512
    padded = nibs + [15] * (512 - len(nibs))
    return bytes((padded[i] << 4) | padded[i + 1] for i in range(0, 512, 2))


class CsvTests(unittest.TestCase):
    def test_physical_capture_temperatures_and_repeated_config_timestamps(self):
        samples, notes = exporter.decode_binary(CAPTURE)
        self.assertEqual([s['temp'] for s in samples], [28.25, 28.25])
        self.assertEqual([s['unix'] for s in samples], [1700000000, 1700000000])
        self.assertTrue(any('0x1330 (OK)' in note for note in notes))

    def test_deltas_anchor_and_sensor_error(self):
        samples, _ = exporter.decode_binary(page(seal(start(160) + [2, 9, 7, 2, 8, 0, 0, 0, 6])))
        self.assertEqual([s['temp'] for s in samples], [0, 0.5, 0.25, None, -40, -38.5])
        self.assertEqual(samples[3]['status'], 'SENSOR_ERR')
        self.assertEqual([s['unix'] for s in samples], [1700000000 + n * 3600 for n in range(6)])

    def test_multiple_pages_and_boot_segments(self):
        first = page(seal(start()))
        second = page(seal([8, 1, 1, 2, 0]))
        third = page(seal(start(timestamp=1700010000)))
        samples, _ = exporter.decode_binary(first + second + third)
        self.assertEqual([s['page'] for s in samples], [0, 1, 1, 2])
        self.assertEqual([s['seg'] for s in samples], [0, 0, 0, 1])
        self.assertEqual([s['temp'] for s in samples], [28.25, 28.5, 28.5, 28.25])

    def test_multiple_checks(self):
        samples, _ = exporter.decode_binary(page(seal(start()) + seal([1, 0])))
        self.assertEqual([s['temp'] for s in samples], [28.25, 28.5, 28.5])

    def test_csv_numeric_temperatures_timezones_and_simulated_label(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'log.csv'
            self.assertEqual(exporter.export_csv(CAPTURE, output, 'simulated'), 2)
            self.assertTrue(output.read_bytes().startswith(b'\xef\xbb\xbf'))
            with output.open(encoding='utf-8-sig', newline='') as source:
                rows = list(csv.DictReader(source))
            self.assertEqual(rows[0]['temperature_c'], '28.25')
            self.assertEqual(rows[0]['timestamp_utc'], '2023-11-14 22:13:20')
            self.assertEqual(rows[0]['timestamp_sgt'], '2023-11-15 06:13:20')
            self.assertEqual(rows[0]['timestamp_source'], 'simulated')
            self.assertEqual(rows[1]['sample'], '2')

    def test_sensor_error_csv_has_blank_temperature(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'log.csv'
            exporter.export_csv(page(seal(start() + [7, 2])), output)
            with output.open(encoding='utf-8-sig', newline='') as source:
                rows = list(csv.DictReader(source))
            self.assertEqual(rows[1]['temperature_c'], '')
            self.assertEqual(rows[1]['status'], 'SENSOR_ERR')
            self.assertEqual(rows[1]['timestamp_source'], 'unconfirmed')

    def test_empty_log_and_erased_page(self):
        for body in (b'', b'\xff' * 256):
            self.assertEqual(exporter.decode_binary(body)[0], [])
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'empty.csv'
            self.assertEqual(exporter.export_csv(b'', output), 0)
            self.assertEqual(len(output.read_text(encoding='utf-8-sig').splitlines()), 1)

    def test_incomplete_or_unsealed_pages_rejected(self):
        for body in (CAPTURE[:-1], b'x' * 100, page(start()), page(seal(start()) + [0])):
            with self.subTest(length=len(body)), self.assertRaises(ValueError):
                exporter.decode_binary(body)

    def test_bad_crc_including_sensor_error_rejected(self):
        for nibs in (start(), start() + [7, 2]):
            damaged = seal(nibs)
            damaged[-1] ^= 1
            with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                exporter.decode_binary(page(damaged))

    def test_unknown_metadata_or_hidden_padding_data_rejected(self):
        for body in (page(seal(start() + [7, 5])), page(seal(start()) + [15, 1])):
            with self.assertRaises(ValueError):
                exporter.decode_binary(body)

    def test_invalid_interval_missing_timebase_and_temperature_range(self):
        for nibs in (start(minutes=0), [8, 1, 1, 1], [0], start(q=501), start(q=500) + [1],
                     [7, 3, 7, 1, *field(1700000000, 8), *field(60, 4), 0]):
            with self.subTest(nibs=nibs), self.assertRaises(ValueError):
                exporter.decode_binary(page(seal(nibs)))

    def test_rejected_export_keeps_previous_csv(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'log.csv'
            exporter.export_csv(CAPTURE, output, 'simulated')
            before = output.read_bytes()
            with self.assertRaises(ValueError):
                exporter.export_csv(CAPTURE[:-1], output)
            self.assertEqual(output.read_bytes(), before)
            self.assertEqual(len(list(Path(directory).glob('*.tmp'))), 0)

    def test_cli_preserves_original_binary(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'log.bin'
            source.write_bytes(CAPTURE)
            result = subprocess.run([sys.executable, '-B', str(W / 'tools/temperature_csv.py'),
                                     str(source), '--timestamp-source', 'simulated'],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(source.read_bytes(), CAPTURE)
            self.assertTrue(source.with_suffix('.csv').exists())
            result = subprocess.run([sys.executable, '-B', str(W / 'tools/temperature_csv.py'),
                                     str(source), '--output', str(source)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(source.read_bytes(), CAPTURE)

    @unittest.skipUnless((W / 'tests/sensor_live_logger/build/host/live-encoded-pages.bin').exists(),
                         'Optional fixture generated by the actual JZ C encoder host tests')
    def test_actual_c_encoder_multi_page_fixture(self):
        fixture = W / 'tests/sensor_live_logger/build/host/live-encoded-pages.bin'
        samples, _ = exporter.decode_binary(fixture.read_bytes())
        self.assertEqual([s['temp'] for s in samples], [30, 30.25, 30.5, 31, None] + [31] * 600)


if __name__ == '__main__':
    unittest.main(verbosity=2)
