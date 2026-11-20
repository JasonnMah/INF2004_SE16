#!/usr/bin/env python3
from pathlib import Path
import subprocess,sys
W=Path(__file__).resolve().parents[1]
B=W/'course/build_make/http_host';B.mkdir(parents=True,exist_ok=True)
commands=[['gcc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',
 '-I'+str(W),str(W/'tests/test_network_retry.c'),'-o',str(B/'test_network_retry')],
 [str(B/'test_network_retry')],['gcc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',
 '-I'+str(W/'tests/http_mock'),'-I'+str(W),str(W/'http_post.c'),
 str(W/'tests/test_http_post.c'),'-o',str(B/'test_http_post')],
 [str(B/'test_http_post')],['gcc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',
 '-I'+str(W/'tests/http_mock'),'-I'+str(W),str(W/'http_post.c'),
 str(W/'tests/http_socket_adapter.c'),'-o',str(B/'http_socket_adapter')],[sys.executable,'-B',str(W/'tests/test_temperature_csv.py')],
 [sys.executable,'-B',str(W/'tests/test_http_receiver.py')]]
# Compile the actual application poll hook against narrow host network inputs.
reporting=B/'reporting_mock';(reporting/'lwip').mkdir(parents=True,exist_ok=True)
for name in ('cyw43.h','lwip/netif.h','lwip/dhcp.h'):
 (reporting/name).write_text('/* Host fixture supplies the required declarations. */\n')
(reporting/'http_test_config.h').write_text('#define UTK_WIFI_SSID "host-fixture"\n')
commands[0:0]=[['gcc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',
 '-fno-pie','-no-pie','-DAPP_COMBINED=1','-I'+str(reporting),'-I'+str(W),
 '-I'+str(W.parent/'shared/uart_protocol/src'),
 '-I'+str(W/'course/lib/libwifi/sysdepend/pico_rp2040'),
 str(W/'tests/test_wifi_reporting.c'),'-o',str(B/'test_wifi_reporting')],
 [str(B/'test_wifi_reporting')]]
output=[]
commands[0:0]=[['gcc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',
 '-fno-pie','-no-pie','-I'+str(W),'-I'+str(W.parent/'shared/uart_protocol/src'),
 str(W/'buttons.c'),str(W/'tests/test_flash_buttons.c'),'-o',str(B/'test_flash_buttons')],
 [str(B/'test_flash_buttons')]]
commands[0:0]=[['gcc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',
 '-fno-pie','-no-pie','-I'+str(W),'-I'+str(W.parent/'shared/uart_protocol/src'),
 str(W/'batch_store.c'),str(W/'tests/test_batch_store.c'),
 str(W.parent/'shared/uart_protocol/src/protocol/frame.c'),
 str(W.parent/'shared/uart_protocol/src/protocol/crc16.c'),'-o',str(B/'test_batch_store')],
 [str(B/'test_batch_store')],[sys.executable,'-B',str(W/'tests/test_batch_receiver.py')]]
for command in commands:
 p=subprocess.run(command,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 output.append(p.stdout);print(p.stdout,end='')
 if p.returncode:raise SystemExit(p.returncode)
(W/'tests/evidence/http-host-tests.log').write_text('Host tests only; physical Pico/HTTP: NOT TESTED\n'+''.join(output))
