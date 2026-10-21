#!/usr/bin/env python3
"""Build/source checks only. Run in WSL after a clean build; no hardware access."""
from pathlib import Path
import subprocess, hashlib, json, csv, re, shlex, struct, collections, os
WIFI = Path(__file__).resolve().parents[1]
ROOT = WIFI.parent
BUILD = WIFI / "course/build_make"
EVIDENCE = WIFI / "tests/evidence"
PIN = "2e8e6ad480c2d84a369b12995694b9bf5e9ddfb3"
EVIDENCE.mkdir(exist_ok=True)

def require(condition, message):
    if not condition:
        raise SystemExit("FAIL: " + message)
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def run(args, success=True):
    result = subprocess.run(args, cwd=BUILD, text=True, capture_output=True)
    if success:
        require(result.returncode == 0, " ".join(args) + "\n" + result.stderr)
    return result

def source_sha(path, mode="text-lf"):
    content = path.read_bytes()
    if mode == "text-lf":
        content = content.replace(b"\r\n", b"\n")
    return hashlib.sha256(content).hexdigest()

origin = json.loads((WIFI / "course/ORIGIN.json").read_text())
require(origin["commit"] == PIN, "Wrong course source commit")
for entry in origin["files"]:
    require(source_sha(WIFI / "course" / entry.get("stored_path", entry["path"]), entry["hash_mode"]) == entry["portable_sha256"],
            "Unrecorded vendored change: " + entry["path"])
modified = sorted(e["path"] for e in origin["files"] if e["modified"])
require(modified == ["build_make/makefile", "config/config.h",
                     "kernel/sysdepend/pico_rp2040/hw_setting.c"],
        "Unexpected course patch set")

baseline = json.loads((WIFI / "tests/uart_baseline_sha256.json").read_text())
for name, expected in baseline.items():
    require(source_sha(ROOT / name.replace("\\", "/")) == expected, "UART baseline changed: " + name)
require((BUILD / ".build_profile").read_text().strip() ==
        "inf2004-milestone1-smp1-usb-only-v1", "Stale build profile")

raw = (BUILD / "milestone1-build.log").read_bytes()
log = raw.decode("utf-16" if raw.startswith(b"\xff\xfe") else "utf-8-sig")
flags = shlex.split(next(line[7:] for line in log.splitlines() if line.startswith("CFLAGS=")))
asflags = shlex.split(next(line[8:] for line in log.splitlines() if line.startswith("ASFLAGS=")))
require("-DCNF_SMP=1" in flags and "-DCNF_SMP=1" in asflags,
        "SMP=1 missing from C or assembler settings")
require("-mcpu=cortex-m0plus" in flags and "-DTM_CONSOLE_UART=0" in flags,
        "Wrong CPU or UART console setting")
compile_commands = [shlex.split(line) for line in log.splitlines()
                    if line.startswith("arm-none-eabi-gcc ") and " -c " in line]
require(compile_commands, "Capture a clean build so compiler invocations can be checked")
for command in compile_commands:
    require("-DCNF_SMP=1" in command and "-D_PICO_RP2040_" in command,
            "Wrong target/SMP in an actual compilation")
require(not any("../app_program/app_main.c" in c or "../app_program/demo_tasks.c" in c
                for c in compile_commands), "Upstream application compiled into profile")
require(any("../../app_program/smp_boot_main.c" in c for c in compile_commands),
        "Diagnostic application was not compiled")

common = ["arm-none-eabi-gcc", *flags, "-D_PICO_RP2040_",
          "-I../include", "-I../config", "-I../kernel/knlinc"]
app = "../../app_program/smp_boot_main.c"
macro_text = run([*common, "-dM", "-E", app]).stdout
macros = dict(re.findall(r"^#define (\w+) (.*)$", macro_text, re.M))
expected_macros = {"CNF_SMP":"1", "TK_SUPPORT_SMP":"1", "TK_MAX_CORE":"2",
                   "TM_CONSOLE_USB_CDC":"1", "TM_CONSOLE_UART":"0", "USE_SDEV_DRV":"(0)"}
for key, value in expected_macros.items():
    require(macros.get(key) == value, f"Effective macro {key}={macros.get(key)!r}, expected {value}")

negative = {}
for label, options, expected_error in [
    ("single_core_rejected", ["-UCNF_SMP", "-DCNF_SMP=0"], "requires the course two-core SMP"),
    ("uart_console_rejected", ["-UTM_CONSOLE_UART", "-DTM_CONSOLE_UART=1"], "Use USB diagnostics"),
    ("radio_rejected", ["-DTM_WIFI_CYW43=1"], "Use USB diagnostics"),
]:
    result = run([*common, *options, "-E", app], success=False)
    require(result.returncode != 0 and expected_error in result.stderr, label)
    negative[label] = "PASS"

tm = run([*common, "-E", "-P", "../lib/libtm/sysdepend/pico_rp2040/tm_com.c"]).stdout
init = re.search(r"void\s+tm_com_init\s*\(\s*void\s*\)\s*\{([^}]*)\}", tm, re.S)
require(init and not init.group(1).strip(), "Console still configures UART")
require("uart_snd_dat" not in tm, "UART console transmitter survived preprocessing")
hw = run([*common, "-E", "-P", "../kernel/sysdepend/pico_rp2040/hw_setting.c"]).stdout
pins = re.search(r"pinfnc_tbl\[\]\s*=\s*\{(.*?)\n\};", hw, re.S)
require(pins and re.sub(r"\s+", "", pins.group(1)) == "{0,0}",
        "Board startup still claims GPIO pins")

elf = BUILD / "wifi_pico_smp_boot.elf"
uf2 = BUILD / "wifi_pico_smp_boot.uf2"
require(elf.exists() and uf2.exists(), "Missing ELF/UF2 output")
symbols = run(["arm-none-eabi-nm", str(elf)]).stdout
for name in ("tk_get_prc", "knl_smp_launch_core1", "tm_usb_console_start",
             "diagnostic_task", "observed_cores"):
    require(re.search(r"\b" + name + r"$", symbols, re.M), "Missing symbol: " + name)
for name in ("dev_init_ser", "dev_ser_llinit", "uart_snd_dat", "usart_inthdr",
             "cyw43_utk_start", "producer_task", "consumer_task", "blink_task"):
    require(not re.search(r"\b" + name + r"$", symbols, re.M), "Unwanted symbol: " + name)
require(len(re.findall(r" T usermain$", symbols, re.M)) == 1, "Wrong application entry point")
size_text = run(["arm-none-eabi-size", str(elf)]).stdout.strip()
readelf = run(["arm-none-eabi-readelf", "-A", str(elf)]).stdout
require("v6S-M" in readelf, "ELF architecture is not ARMv6S-M")
data = uf2.read_bytes()
require(len(data) % 512 == 0 and len(data) > 0, "Invalid UF2 length")
blocks = len(data) // 512
for i in range(blocks):
    block = data[i*512:(i+1)*512]
    words = struct.unpack_from("<8I", block)
    require(words[0:2] == (0x0a324655, 0x9e5d5157), "Bad UF2 magic")
    require(words[5] == i and words[6] == blocks and words[7] == 0xe48bff56,
            "Bad UF2 ordering/count/RP2040 family")
    require(struct.unpack_from("<I",block,508)[0] == 0x0ab16f30, "Bad UF2 end marker")

# Measure compiler dependencies, not guesses based on folder names.
dependencies = set()
for dep in BUILD.rglob("*.d"):
    lines = dep.read_text().replace("\\\n", "").splitlines()
    if lines and ":" in lines[0]:
        for token in shlex.split(lines[0].split(":", 1)[1]):
            dependencies.add(Path(os.path.abspath(BUILD / token)))
make_line = next(line[len("MAKEFILE_LIST="):] for line in log.splitlines()
                 if line.startswith("MAKEFILE_LIST="))
makefiles = {os.path.abspath(BUILD / t).lower() for t in shlex.split(make_line)}

def role(name):
    path = Path(name)
    stem = path.stem.replace("_", " ")
    if name.startswith("build_make/"):
        return "Course Make recipe: " + name.removeprefix("build_make/")
    if name.startswith("etc/linker/"):
        return "Linker memory/section layout for " + path.parent.name
    if name.startswith("tools/elf2uf2/"):
        return "Host ELF-to-UF2 packager: " + path.name
    if name.startswith("config/"):
        return "Kernel/device/console configuration: " + path.name
    if name.startswith("kernel/"):
        return "Course kernel implementation/internal definitions: " + stem
    if name.startswith("include/"):
        return "Course public/architecture definitions: " + name.removeprefix("include/")
    if name.startswith("device/"):
        return "Inherited sample peripheral driver: " + name.removeprefix("device/")
    if name.startswith("lib/libnet/") or "lwip" in name or "mqtt" in name:
        return "Optional network integration/reference; not enabled in milestone 1: " + path.name
    if name.startswith("lib/libwifi/"):
        return "Optional Pico W radio integration/reference; not enabled: " + path.name
    if "/usb/" in name:
        return "USB CDC diagnostics support: " + path.name
    if name.startswith("lib/"):
        return "Course runtime/monitor/BSP support: " + name.removeprefix("lib/")
    return "Upstream source/reference: " + name

old = json.loads((WIFI / "tests/previous_template_import.json").read_text())
rows = []
for entry in old["files"]:
    name = entry["path"]
    dest = WIFI / "course" / name
    upstream = next((x for x in origin["files"] if x["path"] == name), None)
    if not upstream:
        disposition = "removed_old_template_only"
        usage = "Not needed: local-template-only material; excluded to avoid revision mixing."
        new_sha = ""
    else:
        new_sha = upstream["upstream_sha256"]
        disposition = ("same_as_pinned_source" if entry["sha256"] == new_sha
                       else "replaced_with_pinned_source")
        if upstream["modified"]:
            disposition += "_project_patch_documented"
        if Path(os.path.abspath(dest)) in dependencies:
            usage = "Measured compiler input; retained as dependency of the unchanged course build."
            if name.startswith("device/"):
                usage += " Sample driver code is disabled; no UART driver symbols in ELF."
        elif os.path.abspath(dest).lower() in makefiles:
            usage = "Parsed build recipe; required by the selected course Make dependency graph."
        elif name == "etc/linker/pico_rp2040/tkernel_map.ld":
            usage = "Required: selected RP2040 linker script."
        elif name.startswith("tools/elf2uf2/"):
            usage = "Required: host converter input for the RP2040 UF2 output."
        else:
            usage = ("Not required by this boot image; retained only as part of the complete pinned "
                     "upstream reference snapshot, not as a runtime dependency.")
    rows.append({"old_path":name, "old_sha256":entry["sha256"],
                 "disposition":disposition, "pinned_upstream_sha256":new_sha,
                 "purpose":role(name), "milestone1_need":usage})
require(len(rows) == 599 and len({r["old_path"] for r in rows}) == 599, "599-file audit incomplete")
with (WIFI / "tests/IMPORT_AUDIT.csv").open("w", newline="") as f:
    writer=csv.DictWriter(f,fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
summary = {
    "validation_kind":"build/static-source checks only; no hardware or emulator execution",
    "course_commit":PIN, "tracked_upstream_files":len(origin["files"]),
    "course_files_patched":modified, "uart_baseline_files_checked":len(baseline),
    "uart_baseline_changes":0, "import_audit_rows":len(rows),
    "audit_dispositions":dict(collections.Counter(r["disposition"] for r in rows)),
    "actual_compile_commands_checked":len(compile_commands),
    "effective_macros":{k:macros[k] for k in expected_macros},
    "negative_configuration_checks":negative,
    "uart_console_init_empty_after_preprocessing":True,
    "startup_gpio_table_empty_after_preprocessing":True,
    "uart_driver_symbols_absent":True,
    "compiler":run(["arm-none-eabi-gcc","--version"]).stdout.splitlines()[0],
    "uf2_bytes":len(data), "uf2_sha256":sha(uf2), "elf_bytes":elf.stat().st_size,
    "elf_sha256":sha(elf), "arm_none_eabi_size":size_text,
    "physical_smp_status":"NOT TESTED; awaiting user USB transcript",
}
(EVIDENCE / "milestone1-build-checks.json").write_text(json.dumps(summary,indent=2)+"\n")
(EVIDENCE / "milestone1-effective-config.txt").write_text(
    "\n".join(f"{k}={v}" for k,v in summary["effective_macros"].items())+
    "\nCFLAGS="+shlex.join(flags)+"\nASFLAGS="+shlex.join(asflags)+"\n")
print(f"PASS: pinned source, {len(baseline)} baseline hashes, 599-file audit, actual SMP compiler/assembler flags")
print("PASS: USB-only console; UART0 drivers absent; GPIO startup table empty")
print("PASS: single-core / UART console / radio configurations rejected; valid RP2040 UF2")
print(size_text)
print("Physical dual-core operation: NOT TESTED")
