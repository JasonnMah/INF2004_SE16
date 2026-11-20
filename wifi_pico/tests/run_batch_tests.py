#!/usr/bin/env python3
"""Actual batching sources with modeled hardware and localhost HTTP. No Pico claim."""
from pathlib import Path
import hashlib,importlib.util,subprocess,sys,tempfile,threading
W=Path(__file__).resolve().parents[1];R=W.parent;S=R/'shared/uart_protocol/src'
B=W/'course/build_make/batch_host';B.mkdir(parents=True,exist_ok=True)
flags=['gcc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined','-fno-pie','-no-pie']
evidence=[]
def run(command):
 p=subprocess.run(list(map(str,command)),text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 evidence.append(p.stdout)
 if p.returncode:raise SystemExit(p.stdout)
 print('\n'.join(s for s in p.stdout.splitlines() if s.startswith(('PASS','HOST HTTP'))))
 return p.stdout
sources=[W/'tests/test_batch_flow.c',W/'batch_gateway.c',W/'batch_store.c',
 S/'batch/batch_sensor.c',S/'pico1/pico1_protocol.c',S/'pico2/pico2_protocol.c',
 *sorted((S/'protocol').glob('*.c'))]
run(flags+['-I'+str(W),'-I'+str(S),*sources,'-o',B/'flow']);run([B/'flow'])
app_sources=[W/'tests/test_batch_application.c',W/'batch_communication.c',W/'uart_io.c',
 W/'buttons.c',W/'batch_gateway.c',W/'batch_store.c',S/'batch/batch_sensor.c',
 S/'pico1/pico1_protocol.c',S/'pico2/pico2_protocol.c',*sorted((S/'protocol').glob('*.c'))]
run(flags+['-DAPP_BATCHED=1','-DAPP_COMBINED=1','-DAPP_SENSOR_EMULATOR=0',
 '-I'+str(W),'-I'+str(S),*app_sources,'-o',B/'application']);run([B/'application'])
# Extract production IPC functions, as in the legacy runner, with the larger
# batch descriptor. No duplicated queue implementation or fifth message buffer.
kernel=(W/'kernel_io.c').read_text()
combined=kernel.split('#if APP_COMBINED\n',1)[1].split('#endif',1)[0]
globals_=kernel[kernel.index('static ID rx_flags'):kernel.index('#define UART0_ADDRESS')]
setup=kernel[kernel.index('int rt_setup(void)'):kernel.index('static int receive_message')]
radio=kernel.rsplit('#if APP_COMBINED\n',1)[1].rsplit('#endif',1)[0]
(B/'radio_ipc_actual.inc').write_text(combined+globals_+setup+radio)
run(flags+['-DAPP_BATCHED=1','-DAPP_COMBINED=1','-I'+str(B),'-I'+str(W),'-I'+str(S),
 W/'tests/test_radio_ipc.c',W/'rx_ring.c','-o',B/'ipc']);run([B/'ipc'])
# Produce the real Sensor's flash-sourced encoded fixture and SDK model.
run([sys.executable,'-B',R/'sensor_pico/tests/run_tests.py'])
sensor=R/'sensor_pico';stubs=sensor/'build/host/sdk_stubs'
run(flags+['-DSENSOR_BATCHED=1','-Wl,--defsym,__flash_binary_end=0x10010000',
 '-I'+str(stubs),'-I'+str(sensor),'-I'+str(S),sensor/'tests/test_batched_storage.c',
 sensor/'sensor_uart.c',sensor/'flash_log.c',sensor/'encoder.c',S/'batch/batch_sensor.c',
 S/'pico1/pico1_protocol.c',S/'pico2/pico2_protocol.c',*sorted((S/'protocol').glob('*.c')),
 '-o',B/'sensor_storage']);run([B/'sensor_storage'])
run(flags+['-I'+str(W/'tests/http_mock'),'-I'+str(W),W/'http_post.c',
 W/'tests/http_socket_adapter.c','-o',B/'http_adapter'])
sys.dont_write_bytecode=True
spec=importlib.util.spec_from_file_location('receiver',W/'tools/http_receiver.py')
receiver=importlib.util.module_from_spec(spec);spec.loader.exec_module(receiver)
with tempfile.TemporaryDirectory() as directory:
 server=receiver.Receiver(('127.0.0.1',0),directory,decode_jz=True,
                          timestamp_source='pc-clock',log_format='interval12')
 # Use the actual decoder/exporter after reassembly, not a mocked CSV callback.
 worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
 try:
  page=(sensor/'build/host/flash-snapshot-interval12.bin').read_bytes()
  content=page*33;digest=hashlib.sha256(content).hexdigest()
  server.drop_next_receipt=True
  for offset in range(0,len(content),4096):
   body=B/'body.bin';body.write_bytes(content[offset:offset+4096])
   key=f'batch-{digest}-{offset:08x}'
   result=run([B/'http_adapter',server.server_port,body,key,len(content)])
   assert f'attempts={2 if offset==0 else 1}' in result
   if offset+4096<len(content):assert not list(Path(directory).glob('*.csv'))
  bins=list(Path(directory).glob('*.bin'));csvs=list(Path(directory).glob('*.csv'))
  assert len(bins)==len(csvs)==1 and bins[0].read_bytes()==content
  import csv
  with csvs[0].open(newline='',encoding='utf-8-sig') as f:rows=list(csv.DictReader(f))
  assert len(rows)==66 and set(rows[0])=={'sample','temperature_c','timestamp_sgt','status'}
  assert [float(row['temperature_c']) for row in rows]==[25,26.25]*33
  print('PASS actual HTTP client -> PC: three batches, lost receipt retry, exact assembled Sensor bytes, real CSV with 66 temperatures')
  evidence.append('PASS actual client/PC/CSV: 8448 bytes, 3 batches, 66 samples; lost receipt duplicate\n')
 finally:server.shutdown();server.server_close();worker.join()
(B/'results.log').write_text('Host/localhost only; physical UART/Wi-Fi/SMP flash batching PENDING\n'+''.join(evidence))
