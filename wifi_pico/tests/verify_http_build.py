#!/usr/bin/env python3
"""Build/source checks only. Credentials are neither printed nor hashed here."""
from pathlib import Path
import hashlib,json,re,shlex,struct,subprocess,sys
W=Path(__file__).resolve().parents[1];R=W.parent;B=W/'course/build_make'
check_only=int(sys.argv[1]);combined=len(sys.argv)>2 and sys.argv[2]=='combined'
image=('wifi_pico_combined' if combined else 'wifi_pico_http_known_bytes')+('_buildcheck' if check_only else '')
profile=f'inf2004-combined-smp1-check{check_only}-v1' if combined else f'inf2004-http-known-smp1-check{check_only}-v2'
entry='../../main.c' if combined else '../../tests/firmware/http_test_main.c'
def check(ok,why):
 if not ok:raise SystemExit('FAIL: '+why)
def source_hash(p,mode='text-lf'):
 data=p.read_bytes()
 if mode=='text-lf':data=data.replace(b'\r\n',b'\n')
 return hashlib.sha256(data).hexdigest()
origin=json.loads((W/'course/ORIGIN.json').read_text())
for e in origin['files']:check(source_hash(W/'course'/e.get('stored_path',e['path']),e['hash_mode'])==e['portable_sha256'],'Course source mismatch: '+e['path'])
for n,h in json.loads((W/'tests/uart_baseline_sha256.json').read_text()).items():check(source_hash(R/n.replace('\\','/'))==h,'UART baseline changed: '+n)
check((B/'.build_profile').read_text().strip()==profile,'Actual object profile')
raw=(B/('combined-build.log' if combined else 'http-build.log')).read_bytes();log=raw.decode('utf-16' if raw.startswith(b'\xff\xfe') else 'utf-8-sig')
flags=shlex.split(next(s[7:] for s in log.splitlines() if s.startswith('CFLAGS=')))
asflags=shlex.split(next(s[8:] for s in log.splitlines() if s.startswith('ASFLAGS=')))
check('-DCNF_SMP=1' in flags and '-DCNF_SMP=1' in asflags,'SMP actual flags')
cmd=['arm-none-eabi-gcc',*flags,'-D_PICO_RP2040_','-I../include','-I../config','-I../kernel/knlinc']
def run(args):return subprocess.check_output(args,cwd=B,text=True)
macros=dict(re.findall(r'^#define (\w+) (.*)$',run([*cmd,'-dM','-E',entry]),re.M))
for k,v in {'CNF_SMP':'1','TK_MAX_CORE':'2','TM_APP_HTTP':'1','TM_CONSOLE_UART':'0','TM_CONSOLE_USB_CDC':'1','TM_WIFI_CYW43':'1','TM_WIFI_DHCP':'1','TM_WIFI_TCP':'0','APP_SENSOR_EMULATOR':'0'}.items():check(macros.get(k)==v,'Effective macro '+k)
# Check the actual TCP feature in a standard-C unit too.
rawmacros=dict(re.findall(r'^#define (\w+) (.*)$',run([*cmd,'-dM','-E','../../http_post.c']),re.M))
check(rawmacros.get('LWIP_TCP')=='(TM_WIFI_TCP || TM_APP_HTTP)','TCP enabled without echo test')
check(rawmacros.get('NO_SYS')=='1' and rawmacros.get('SYS_LIGHTWEIGHT_PROT')=='0','Expected owner model')
retry_macros=dict(re.findall(r'^#define (\w+) (.*)$',run([*cmd,'-dM','-E','../../http_service.c']),re.M))
check(retry_macros.get('WIFI_JOIN_MAX_ATTEMPTS')=='3u' and retry_macros.get('WIFI_JOIN_RETRY_GAP_MS')=='2000u','Bounded startup join policy')
check(retry_macros.get('WIFI_JOIN_WINDOW_US')=='30000000ULL' and retry_macros.get('WIFI_JOIN_NONET_RETRY_US')=='1000000ULL','NONET deadline/pacing')
symbols=run(['arm-none-eabi-nm',image+'.elf'])
for symbol in ['knl_smp_launch_core1','cyw43_utk_task','cyw43_utk_app_poll','cyw43_utk_app_scan','http_post_poll','tcp_connect','tk_snd_mbf','status_run']:
 check(re.search(r'\b'+symbol+r'$',symbols,re.M),'Missing symbol '+symbol)
if not check_only:
 check(re.search(r'\b'+('http_post_start_data' if combined else 'http_post_start')+r'$',symbols,re.M),'Send-enabled HTTP entry missing')
for symbol in ['snprintf','_sbrk','_malloc_r']:
 check(not re.search(r'\b'+symbol+r'$',symbols,re.M),'Unexpected stdio/heap dependency '+symbol)
if combined:
 check(int(macros['CNF_MAX_MBFID']) >= 4,'Course message-buffer capacity')
 check(len(re.findall(r'\btk_cre_mbf\s*\(', (W/'kernel_io.c').read_text())) == 4,'Application must stay within four message buffers')
 for symbol in ['communication_run','uart_rx_interrupt','pico2_handle_frame','log_workflow_completed','upload_poll','rt_upload_send','rt_upload_result_take','rt_config_request_send','rt_config_result_take']:
  check(re.search(r'\b'+symbol+r'$',symbols,re.M),'Combined symbol missing: '+symbol)
 if not check_only:check(re.search(r'\bhttp_config_start$',symbols,re.M),'PC configuration GET entry missing')
 check(macros.get('APP_COMBINED')=='1','Combined flag')
 radio=dict(re.findall(r'^#define (\w+) (.*)$',run([*cmd,'-dM','-E','../lib/libwifi/sysdepend/pico_rp2040/cyw43_utk_kernel.c']),re.M))
 check(radio.get('CYW43_TASK_PRIORITY')=='6','Combined radio priority')
for symbol in (['sensor_emulator_run','pico1_handle_frame','dev_init_ser','lwip_utk_tcp_poll'] if combined else ['communication_run','uart_rx_interrupt','sensor_emulator_run','pico1_handle_frame','pico2_handle_frame','dev_init_ser','lwip_utk_tcp_poll']):
 check(not re.search(r'\b'+symbol+r'$',symbols,re.M),'Unexpected UART/test symbol '+symbol)
data=(B/(image+'.uf2')).read_bytes();check(len(data)%512==0 and data,'UF2 alignment')
for i in range(len(data)//512):check(struct.unpack_from('<I',data,i*512+28)[0]==0xe48bff56,'RP2040 family')
size=run(['arm-none-eabi-size',image+'.elf']).strip()
result={'kind':'build/static only','physical_http':'NOT TESTED','build_check_only':bool(check_only),'course_commit':origin['commit'],
 'smp':1,'cores':2,'radio_owner':'core 0 priority 6' if combined else 'core 0 priority 4','uart0':'protocol' if combined else 'free','uart_baseline_changed':False,'wifi_diagnostics':1,'wifi_join_max_attempts':3,'wifi_join_retry_gap_ms':2000,'wifi_join_window_ms':30000,'wifi_nonet_retry_ms':1000,
 'external_dependencies':{'pico_sdk':'a1438dff1d38bd9c65dbd693f0e5db4b9ae91779','tinyusb':'86ad6e56c1700e85f1c5678607a762cfe3aa2f47','cyw43_driver':'dd7568229f3bf7a37737b9e1ef250c26efe75b23','lwip':'77dcd25a72509eb83f72b033d219b1d40cd8eb95'},
 'application_source_sha256':{str(p.relative_to(W)):source_hash(p) for p in sorted((W/'tests/firmware').glob('*')) if p.suffix in ('.c','.h')},
 'uf2_bytes':len(data),'uf2_sha256':hashlib.sha256(data).hexdigest(),'size':size}
if combined:
 result['combined_source_sha256']={str(p.relative_to(W)):source_hash(p) for p in sorted(list(W.glob('*.c'))+list(W.glob('*.h'))+[W/'config/combined.mk'])}
(W/'tests/evidence'/(('combined' if combined else 'http')+('-build-check.json' if check_only else '-build.json'))).write_text(json.dumps(result,indent=2)+'\n')
print('PASS build/static checks: '+image+'; SMP=1, USB, radio-owner hook, unchanged UART baseline')
print(size)
