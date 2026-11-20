#!/usr/bin/env python3
"""Supplementary portable C tests. Requires a host gcc-compatible compiler."""
import argparse
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc', default=os.environ.get('CC', 'gcc'))
args = parser.parse_args()
build = ROOT / 'build/host-tests'
build.mkdir(parents=True, exist_ok=True)
binary = build / ('protocol_tests.exe' if os.name == 'nt' else 'protocol_tests')
sources = sorted((ROOT / 'src/protocol').glob('*.c'))
sources += [ROOT / 'src/pico1/pico1_protocol.c', ROOT / 'src/pico2/pico2_protocol.c',
            ROOT / 'tests/test_protocol.c']
subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I' + str(ROOT / 'tests/stubs'), '-I' + str(ROOT / 'src'),
                *map(str, sources), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
