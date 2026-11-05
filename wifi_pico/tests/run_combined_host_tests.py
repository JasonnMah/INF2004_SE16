#!/usr/bin/env python3
"""Host-only UART/workflow/client/PC tests. No SMP or physical timing claims."""
from pathlib import Path
import hashlib,importlib.util,subprocess,threading,tempfile
W=Path(__file__).resolve().parents[1];S=W.parent/'shared/uart_protocol/src'
B=W/'course/build_make/combined_host';B.mkdir(parents=True,exist_ok=True)
out=[]
def run(args):
 p=subprocess.run(list(map(str,args)),text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 if p.returncode:raise SystemExit(p.stdout)
 out.append(p.stdout);return p.stdout
flags=['gcc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined','-fno-pie','-no-pie']
run(flags+['-I'+str(W),'-I'+str(S),W/'status.c',W/'buttons.c',
           W/'tests/test_status.c','-o',B/'test_status'])
run([B/'test_status'])
# Compile the real setup/radio IPC sections without unrelated RP2040 MMIO.
# Four permitted message buffers is the course port's actual configured limit.
kernel=(W/'kernel_io.c').read_text()
combined=kernel.split('#if APP_COMBINED\n',1)[1].split('#endif',1)[0]
globals_=kernel[kernel.index('static ID rx_flags'):kernel.index('#define UART0_ADDRESS')]
setup=kernel[kernel.index('int rt_setup(void)'):kernel.index('static int receive_message')]
radio=kernel.rsplit('#if APP_COMBINED\n',1)[1].rsplit('#endif',1)[0]
(B/'radio_ipc_actual.inc').write_text(combined+globals_+setup+radio)
run(flags+['-DAPP_COMBINED=1','-I'+str(B),'-I'+str(W),'-I'+str(S),
           W/'tests/test_radio_ipc.c',W/'rx_ring.c','-o',B/'test_radio_ipc'])
run([B/'test_radio_ipc'])
run(flags+['-I'+str(W),W/'log_id.c',W/'tests/test_log_id.c','-o',B/'test_log_id'])
ids=run([B/'test_log_id']).splitlines();out.pop()
for n,line in enumerate(ids):assert line=='log-'+hashlib.sha256(bytes(i%256 for i in range(n))).hexdigest()
assert len(ids)==1025
out.append('PASS SHA-256 IDs match Python for every length 0..1024, including padding boundaries\n')
sources=[W/n for n in ('tests/test_combined.c','communication.c','log_workflow.c','log_id.c','lab_capture.c','uart_io.c','buttons.c','rx_ring.c')]
sources+=list((S/'protocol').glob('*.c'))+[S/'pico1/pico1_protocol.c',S/'pico2/pico2_protocol.c']
run(flags+['-DAPP_COMBINED=1','-DAPP_SENSOR_EMULATOR=0','-DUART_FAULT=0','-I'+str(W),'-I'+str(W),'-I'+str(S),*sources,'-o',B/'test_combined'])
(B/'http_test_config.h').write_text('#define HTTP_BUILD_CHECK 0\n#define HTTP_PC_ADDRESS "127.0.0.1"\n#define HTTP_PC_PORT 8000\n')
run(flags+['-I'+str(B),'-I'+str(W),'-I'+str(S),'-I'+str(W),W/'upload.c',W/'tests/test_upload.c','-o',B/'test_upload'])
run([B/'test_upload'])
snapshot=B/'uart_snapshot.bin'
flow=run([B/'test_combined',snapshot]);key=next(s.split('=',1)[1] for s in flow.splitlines() if s.startswith('SNAPSHOT_ID='))
assert snapshot.read_bytes()==bytes(i%256 for i in range(300))
for case in ('clear','clear_failure','oversize','timeout','pc_config','transfer_status','preamble','config_loading'):run([B/'test_combined',case])
run(flags+['-I'+str(W/'tests/http_mock'),'-I'+str(W),W/'http_post.c',W/'tests/http_socket_adapter.c','-o',B/'http_socket_adapter'])
spec=importlib.util.spec_from_file_location('receiver',W/'tools/http_receiver.py');r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
with tempfile.TemporaryDirectory() as directory:
 server=r.Receiver(('127.0.0.1',0),directory);server.drop_next_receipt=True
 worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
 try:
  for expected in (2,1):
   result=run([B/'http_socket_adapter',server.server_port,snapshot,key]);assert f'attempts={expected}' in result
  files=list(Path(directory).glob('*.bin'));assert len(files)==1 and files[0].read_bytes()==snapshot.read_bytes()
  out.append('PASS Sensor host bytes -> UART reassembly -> HTTP client -> PC exact equality; lost receipt retries once, persistent duplicate stored once\n')
 finally:server.shutdown();server.server_close();worker.join()
(W/'tests/evidence/combined-host-tests.log').write_text('Host tests only; physical combined system PENDING\n'+''.join(out))
print(''.join(line+'\n' for item in out for line in item.splitlines() if line.startswith(('PASS','HOST HTTP'))))
