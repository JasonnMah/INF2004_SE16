#!/usr/bin/env python3
"""Host integration checks for the real GP20/GP21 UI and protocol paths."""
import argparse
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc', default=os.environ.get('CC', 'gcc'))
args = parser.parse_args()
build = ROOT / 'build/ui-host-tests'
build.mkdir(parents=True, exist_ok=True)
binary = build / ('ui_tests.exe' if os.name == 'nt' else 'ui_tests')
sources = sorted((ROOT / 'src/protocol').glob('*.c')) + [
    ROOT / 'src/pico1/pico1_protocol.c', ROOT / 'src/pico2/pico2_protocol.c',
    ROOT / 'src/pico2/pico2_ui.c', ROOT / 'src/demo/mock_logger.c', ROOT / 'tests/ui/test_pico2_ui.c']
subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I' + str(ROOT / 'tests/ui/stubs'),
                '-I' + str(ROOT / 'tests/stubs'), '-I' + str(ROOT / 'src'),
                *map(str, sources), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
