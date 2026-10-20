#!/usr/bin/env python3
"""Native fault-injection checks using the real UART modules and byte parser."""
import argparse
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc', default=os.environ.get('CC', 'gcc'))
parser.add_argument('cases', nargs='*', help='Optional individual case names')
args = parser.parse_args()
build = ROOT / 'build/reliability-tests'
build.mkdir(parents=True, exist_ok=True)
binary = build / ('reliability_tests.exe' if os.name == 'nt' else 'reliability_tests')
sources = sorted((ROOT / 'src/protocol').glob('*.c'))
sources += [ROOT / 'src/pico1/pico1_protocol.c', ROOT / 'src/pico2/pico2_protocol.c',
            ROOT / 'tests/test_reliability.c']
subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I' + str(ROOT / 'tests/stubs'), '-I' + str(ROOT / 'src'),
                *map(str, sources), '-o', str(binary)], check=True)
cases = args.cases or [
    'receive_timeout', 'state_guard', 'end_duplicate', 'changed_duplicate',
    'overflow_recovery', 'pending_failure', 'parser_resync', 'wire_faults',
]
failed = []
for case in cases:
    result = subprocess.run([str(binary), case])
    if result.returncode:
        failed.append(case)
if failed:
    raise SystemExit('FAIL: ' + ', '.join(failed))
print(f'PASS: {len(cases)} UART reliability groups')
