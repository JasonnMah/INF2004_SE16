#!/usr/bin/env python3
"""Host UART tests using JZ's encoder read-only; no Sensor firmware or flash testing."""
from pathlib import Path
import subprocess,importlib.util,json,re,sys
sys.dont_write_bytecode=True
s=Path(__file__).resolve().parents[1];sensor=s.parents[1]
out=s/"build/host";out.mkdir(parents=True,exist_ok=True)
flags=["gcc","-std=c11","-Wall","-Wextra","-Werror","-I"+str(s/"src"),"-I"+str(sensor)]
codec=out/"codec"
subprocess.run([*flags,str(s/"tests/test_codec_transfer.c"),str(sensor/"encoder.c"),"-o",str(codec)],check=True)
fixture=out/"actual-codec-pages.bin"
subprocess.run([str(codec),str(fixture)],check=True)
backend=out/"backend"
libraries=out/"libraries"
libraries.mkdir(parents=True,exist_ok=True)
recipe=out/"libraries.mk"
recipe.write_text(".DEFAULT_GOAL := all\nGCC := gcc\nCFLAGS := -std=c11\nTARGET := HOST\nRM := rm -rf\n"
    +f"UART_PROTOCOL_ROOT := {s}\nUART_LIBRARY_DIR := {libraries}\n"
    +"UART_ENDPOINTS := gateway sensor\n"
    +f"include {s}/library.mk\n-include $(C_DEPS)\nall: $(OBJS)\n")
subprocess.run(["make","-B","-f",str(recipe),"--jobs=4","all"],check=True)
sources=[libraries/"libuart_gateway.a",libraries/"libuart_sensor.a",libraries/"libuart_wire.a"]
subprocess.run([*flags,str(s/"tests/test_backends.c"),*map(str,sources),"-o",str(backend)],check=True)
subprocess.run([str(backend),str(fixture)],check=True)
spec=importlib.util.spec_from_file_location("jz_decoder",sensor/"tools/decode.py")
decoder=importlib.util.module_from_spec(spec);spec.loader.exec_module(decoder)
data=fixture.read_bytes();samples,notes=decoder.decode({i:data[i*256:(i+1)*256] for i in range(len(data)//256)})
assert [x["temp"] for x in samples]==[30.0,30.25,30.5,31.0,None],samples
assert all(x["status"] in ("ok","SENSOR_ERR") for x in samples),samples
assert not any("mismatch" in n or "UNVERIFIED" in n for n in notes),notes
print("PASS: JZ's actual Python decoder validates both preserved pages/CHECKs and expected previous-value deltas")
# Reuse the baseline assertions unchanged; only adapt their old link constructors.
# The original test sources are preserved in tests/fixtures/.
compat = """
typedef struct { unsigned id; } uart_inst_t;
uint64_t time_us_64(void);
void uart_write_blocking(uart_inst_t *, const uint8_t *, size_t);
static uint64_t compat_now(void *ctx) { (void)ctx; return time_us_64()/1000u; }
static void compat_write(void *ctx,const uint8_t *bytes,size_t length)
{ uart_write_blocking((uart_inst_t *)ctx,bytes,length); }
static protocol_link_t compat_link(uart_inst_t *uart)
{ return (protocol_link_t){.context=uart,.session_id=SESSION,.write=compat_write,.now_ms=compat_now}; }
"""
for name in ("test_protocol", "test_reliability"):
    original=(s/"tests/fixtures"/(name+".c")).read_text()
    converted=re.sub(r"(#define SESSION[^\n]*\n)",lambda m:m[0]+compat,original,count=1)
    converted,count=re.subn(r"\(protocol_link_t\)\{(&\w+), SESSION, NULL\}",r"compat_link(\1)",converted)
    assert count==2
    generated=out/(name+"_shared.c");generated.write_text(converted)
    executable=out/(name+"_shared")
    subprocess.run([*flags,str(generated),*map(str,sources),"-o",str(executable)],check=True)
    cases=[[]] if name=="test_protocol" else [[case] for case in (
        "receive_timeout","state_guard","end_duplicate","changed_duplicate",
        "overflow_recovery","pending_failure","parser_resync","wire_faults")]
    for case in cases: subprocess.run([str(executable),*case],check=True)
print("PASS: baseline protocol assertions and all 8 reliability groups also pass against the shared library")
evidence={"kind":"host UART tests with simulated wire/storage callbacks", "result":"PASS", "physical":"NOT TESTED", "sensor_firmware":"interval12 encoder/decoder; other Sensor modules unchanged", "baseline_protocol_assertions":"PASS", "shared_reliability_groups_passed":8, "checks":[
 "two interval12 encoder pages transferred byte-for-byte", "JZ decoder validates expected samples and page CHECKs",
 "shared endpoints release source on completion, abort and retry exhaustion", "CONFIG validation and rejected oversized source",
 "simulated delayed CLEAR completion, lost ACK without repeated clear, failure NACK"]}
(out/"results.json").write_text(json.dumps(evidence,indent=2)+"\n")
