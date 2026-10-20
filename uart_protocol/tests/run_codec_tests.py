"""Portable decoder and extracted production encoder checks."""
import os
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parents[1]
build = ROOT / 'build/codec-tests'
build.mkdir(parents=True, exist_ok=True)
binary = build / ('codec_tests.exe' if os.name == 'nt' else 'codec_tests')
subprocess.run([os.environ.get('CC', 'gcc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
               '-I' + str(ROOT / 'src'), str(ROOT / 'src/codec/temp_decoder.c'),
               str(ROOT / 'src/codec/temp_encoder.c'), str(ROOT / 'src/codec/temp_log_print.c'),
               str(ROOT / 'tests/test_temp_decoder.c'), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)

transport = build / ('codec_transport.exe' if os.name == 'nt' else 'codec_transport')
sources = sorted((ROOT / 'src/protocol').glob('*.c')) + [
    ROOT / 'src/pico1/pico1_protocol.c', ROOT / 'src/pico2/pico2_protocol.c',
    ROOT / 'src/codec/temp_encoder.c', ROOT / 'src/codec/temp_decoder.c',
    ROOT / 'tests/test_codec_transport.c']
subprocess.run([os.environ.get('CC', 'gcc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
    '-I' + str(ROOT / 'src'), '-I' + str(ROOT / 'tests/stubs'),
    *map(str, sources), '-o', str(transport)], check=True)
subprocess.run([str(transport)], check=True)
