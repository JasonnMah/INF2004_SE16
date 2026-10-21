#!/usr/bin/env python3
"""Static/build checks, not RP2040 runtime or physical UART validation."""
from pathlib import Path
import subprocess, hashlib, json, sys, shlex, re, struct
W = Path(__file__).resolve().parents[1]
R = W.parent
B = W/"course/build_make"
mode = int(sys.argv[1]) if len(sys.argv)>1 else 0
def check(ok, message):
    if not ok: raise SystemExit("FAIL: "+message)
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def run(args):
    p=subprocess.run(args,cwd=B,text=True,capture_output=True)
    check(p.returncode==0,p.stderr)
    return p.stdout
def source_sha(path, mode="text-lf"):
    content = path.read_bytes()
    if mode == "text-lf":
        content = content.replace(b"\r\n", b"\n")
    return hashlib.sha256(content).hexdigest()

origin=json.loads((W/"course/ORIGIN.json").read_text())
for e in origin["files"]:
    check(source_sha(W/"course"/e.get("stored_path",e["path"]),e["hash_mode"])==e["portable_sha256"],"Course source changed: "+e["path"])
baseline=json.loads((W/"tests/uart_baseline_sha256.json").read_text())
for n,h in baseline.items(): check(source_sha(R/n.replace("\\","/"))==h,"UART baseline changed: "+n)
check((B/".build_profile").read_text().strip()==f"inf2004-uart-smp1-fault{mode}-v1","Wrong cache profile")
raw=(B/f"uart-build-f{mode}.log").read_bytes()
log=raw.decode("utf-16" if raw.startswith(b"\xff\xfe") else "utf-8-sig")
flags=shlex.split(next(s[7:] for s in log.splitlines() if s.startswith("CFLAGS=")))
aflags=shlex.split(next(s[8:] for s in log.splitlines() if s.startswith("ASFLAGS=")))
check("-DCNF_SMP=1" in flags and "-DCNF_SMP=1" in aflags,"Missing actual SMP compiler/assembler flags")
check("-DTM_CONSOLE_UART=0" in flags and "-mcpu=cortex-m0plus" in flags,"Console/CPU mismatch")
commands=[shlex.split(s) for s in log.splitlines() if s.startswith("arm-none-eabi-gcc ") and " -c " in s]
check(commands,"Run a clean build to record actual compiler invocations")
for c in commands:
    check("-DCNF_SMP=1" in c and "-D_PICO_RP2040_" in c,"Wrong source target")
check(any("../../app_program/uart_app_main.c" in c for c in commands),"UART entry missing")
check(not any("../app_program/app_main.c" in c or "../../app_program/smp_boot_main.c" in c for c in commands),
      "Wrong app compiled")
sources=[t for c in commands for t in c if t.endswith(".c") and "uart_protocol/src/" in t]
expected={"protocol/crc16.c","protocol/frame.c","protocol/stream_parser.c","protocol/reliability.c",
          "pico1/pico1_protocol.c","pico2/pico2_protocol.c","demo/mock_logger.c"}
check({s.split("uart_protocol/src/")[1] for s in sources}==expected,"Wrong baseline source set")
common=["arm-none-eabi-gcc",*flags,"-D_PICO_RP2040_","-I../include","-I../config","-I../kernel/knlinc"]
macros=dict(re.findall(r"^#define (\w+) (.*)$",run([*common,"-dM","-E","../../app_program/uart_app_main.c"]),re.M))
for k,v in {"CNF_SMP":"1","TK_MAX_CORE":"2","TM_CONSOLE_USB_CDC":"1","TM_CONSOLE_UART":"0",
            "USE_SDEV_DRV":"(0)","UART_FAULT":str(mode)}.items():
    check(macros.get(k)==v,"Wrong effective macro "+k)
elf=B/f"wifi_pico_uart_loopback_f{mode}.elf"
uf2=B/f"wifi_pico_uart_loopback_f{mode}.uf2"
symbols=run(["arm-none-eabi-nm",str(elf)])
for n in ("communication_run","sensor_emulator_run","uart_rx_interrupt","rt_command_send",
          "rt_status_send","tk_snd_mbf","tk_rcv_mbf","tk_set_flg","tk_wai_flg",
          "pico1_handle_frame","pico2_handle_frame","tk_get_prc","knl_smp_launch_core1"):
    check(re.search(r"\b"+n+r"$",symbols,re.M),"Missing symbol "+n)
for n in ("dev_init_ser","dev_ser_llinit","uart_snd_dat","cyw43_utk_start","diagnostic_task"):
    check(not re.search(r"\b"+n+r"$",symbols,re.M),"Unexpected owner/app symbol "+n)
disasm=run(["arm-none-eabi-objdump","-dr","mtkernel_3/uart/platform/rt_bridge.o"])
handler=re.search(r"<uart_rx_interrupt>:(.*?)(?=\n[0-9a-f]+ <|\Z)",disasm,re.S)
check(handler is not None,"Missing compiled RX ISR")
for n in ("frame_decode","pico1_handle_frame","pico2_handle_frame","tm_printf","rt_sleep"):
    check(n not in handler.group(1),"ISR contains forbidden work: "+n)
check("tk_set_flg" in handler.group(1),"ISR lacks kernel notification")
data=uf2.read_bytes()
check(len(data)>0 and len(data)%512==0,"Invalid UF2 length")
for i in range(len(data)//512):
    block=data[i*512:(i+1)*512]; words=struct.unpack_from("<8I",block)
    check(words[:2]==(0x0a324655,0x9e5d5157) and words[5]==i and words[6]==len(data)//512
          and words[7]==0xe48bff56 and struct.unpack_from("<I",block,508)[0]==0x0ab16f30,"Invalid RP2040 UF2")
size=run(["arm-none-eabi-size",str(elf)]).strip()
result={"kind":"build/static checks only","fault":mode,"course_commit":origin["commit"],
        "actual_compile_commands":len(commands),"uart_baseline_files":len(baseline),"baseline_changes":0,
        "baseline_sources_reused":sorted(expected),"isr_notification":"tk_set_flg within TA_HLNG",
        "isr_frame_processing":"absent","effective_macros":{k:macros[k] for k in
            ("CNF_SMP","TK_MAX_CORE","TM_CONSOLE_USB_CDC","TM_CONSOLE_UART","USE_SDEV_DRV","UART_FAULT")},
        "uf2_bytes":len(data),"uf2_sha256":sha(uf2),"elf_sha256":sha(elf),
        "size":size,"physical_uart_status":"NOT TESTED"}
out=W/"tests/evidence"/f"uart-build-f{mode}.json"
out.write_text(json.dumps(result,indent=2)+"\n")
print(f"PASS: UART firmware f{mode}, SMP=1, USB only, unchanged baseline, ISR/IPC symbol checks")
print(size)
print("Physical UART/IPC/button/LED test: NOT TESTED; awaiting user capture")
