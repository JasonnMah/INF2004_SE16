#!/usr/bin/env python3
"""Flash qualification build/static checks; never claims physical flash safety."""
from pathlib import Path
import hashlib
import json
import os
import re
import shlex
import struct
import subprocess

W = Path(__file__).resolve().parents[1]
R = W.parent
B = Path(os.environ.get('INF2004_BUILD_DIR', W / 'course/build_make')).resolve()
E = Path(os.environ.get('INF2004_EVIDENCE_DIR', B / 'checks'))
E.mkdir(parents=True, exist_ok=True)

def check(condition, message):
    if not condition:
        raise SystemExit('FAIL: ' + message)

def sha(path, mode='text-lf'):
    content = path.read_bytes()
    if mode == 'text-lf':
        content = content.replace(b'\r\n', b'\n')
    return hashlib.sha256(content).hexdigest()

origin = json.loads((W / 'course/ORIGIN.json').read_text())
for entry in origin['files']:
    check(sha(W / 'course' / entry.get('stored_path', entry['path']), entry['hash_mode']) ==
          entry['portable_sha256'], 'Course source changed: ' + entry['path'])
for name, digest in json.loads((W / 'tests/uart_baseline_sha256.json').read_text()).items():
    check(sha(R / name.replace('\\', '/')) == digest, 'UART baseline changed: ' + name)
check((B / '.build_profile').read_text().strip() == 'inf2004-flash-cache-smp1-v1', 'Wrong build profile')
raw = (B / 'flash-build.log').read_bytes()
log = raw.decode('utf-16' if raw.startswith(b'\xff\xfe') else 'utf-8-sig')
flags = shlex.split(next(line[7:] for line in log.splitlines() if line.startswith('CFLAGS=')))
assembler = shlex.split(next(line[8:] for line in log.splitlines() if line.startswith('ASFLAGS=')))
check('-DCNF_SMP=1' in flags and '-DCNF_SMP=1' in assembler, 'Actual C/assembler SMP flags')
macros = subprocess.check_output(['arm-none-eabi-gcc', *flags, '-D_PICO_RP2040_',
    '-I../include', '-I../config', '-I../kernel/knlinc', '-dM', '-E',
    '../../tests/firmware/flash_cache_main.c'], cwd=B, text=True)
definitions = dict(re.findall(r'^#define (\w+) (.*)$', macros, re.M))
for key, value in {'CNF_SMP': '1', 'TK_MAX_CORE': '2', 'TM_CONSOLE_UART': '0',
                   'TM_CONSOLE_USB_CDC': '1', 'TM_WIFI_CYW43': '0'}.items():
    # An undefined feature flag evaluates to zero in the port's #if gates.
    check(definitions.get(key, '0').strip('() ') == value, 'Effective ' + key)
elf = B / 'wifi_pico_flash_cache_test.elf'
nm = subprocess.check_output(['arm-none-eabi-nm', '-n', str(elf)], text=True)
symbols = {name: int(address, 16) for address, kind, name in
           re.findall(r'^([0-9a-f]+)\s+(\w)\s+(\S+)$', nm, re.M)}
for function in ('flash_port_core1_poll', 'flash_operation'):
    check(0x20000000 <= symbols[function] < 0x20040000, function + ' must execute in RAM')
    disassembly = subprocess.check_output(['arm-none-eabi-objdump', '-D',
        '--disassemble=' + function, str(elf)], text=True)
    for target in re.findall(r'\bbl\s+([0-9a-f]+)', disassembly):
        address = int(target, 16)
        check(address < 0x4000 or 0x20000000 <= address < 0x20042000,
              function + ' calls code outside RAM/ROM')
check(symbols['__flash_image_end'] <= 0x101fc000, 'Firmware overlaps reserved cache')
for absent in ('cyw43_utk_task', 'rt_uart_open', 'pico1_handle_frame', 'pico2_handle_frame'):
    check(absent not in symbols, 'Unexpected protocol/network application: ' + absent)
for required in ('knl_smp_launch_core1', 'batch_store_open', 'batch_store_save', 'batch_store_confirm'):
    check(required in symbols, 'Missing ' + required)
button_task = subprocess.check_output(['arm-none-eabi-objdump', '-d',
    '--disassemble=core1_task', str(elf)], text=True)
for function in ('app_button_event', 'tk_get_otm'):
    check(re.search(r'\bbl\s+[0-9a-f]+\s+<' + function + r'>', button_task),
          'Flash test must use elapsed-time debounce: ' + function)
uf2 = B / 'wifi_pico_flash_cache_test.uf2'
data = uf2.read_bytes()
check(data and len(data) % 512 == 0, 'UF2 block size')
for offset in range(0, len(data), 512):
    address, count = struct.unpack_from('<II', data, offset + 12)
    check(struct.unpack_from('<I', data, offset + 28)[0] == 0xe48bff56, 'RP2040 UF2 family')
    check(0x10000000 <= address and address + count <= 0x101fc000, 'UF2 overwrites cache')
result = {'kind': 'build/static only', 'physical_flash_smp': 'NOT TESTED',
    'course_commit': origin['commit'], 'smp': 1, 'cores': 2,
    'reserved_flash_offset': '0x001fc000', 'reserved_bytes': 16384,
    'uart_baseline_changed': False, 'uf2_sha256': hashlib.sha256(data).hexdigest(),
    'source_sha256': {name: sha(W / name) for name in
        ('flash_port.c', 'flash_port.h', 'batch_store.c', 'batch_store.h',
         'config/flash.mk', 'config/flash_map.ld', 'buttons.c', 'buttons.h',
         'tests/firmware/flash_cache_main.c')}}
(E / 'flash-build.json').write_text(json.dumps(result, indent=2) + '\n')
print('PASS flash build/static checks: SMP=1; RAM rendezvous; cache excluded from firmware and UF2; course/UART baseline unchanged')
print(subprocess.check_output(['arm-none-eabi-size', str(elf)], text=True).strip())
