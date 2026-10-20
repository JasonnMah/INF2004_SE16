#!/usr/bin/env python3
"""Check configured/built split-target isolation; this is not a hardware test."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
args = parser.parse_args()
commands = json.loads((args.build_dir / "compile_commands.json").read_text())
shared = {"src/protocol/" + name + ".c" for name in
          ("crc16", "frame", "stream_parser", "reliability")}
expected = {
    "wifi_pico_firmware": shared | {"src/apps/wifi_pico_main.c",
        "src/pico2/pico2_protocol.c", "src/pico2/pico2_ui.c"},
    "sensor_pico_protocol_test": shared | {"src/apps/sensor_pico_test_main.c",
        "src/pico1/pico1_protocol.c", "src/demo/mock_logger.c"},
}
for target, sources in expected.items():
    entries = [e for e in commands if "CMakeFiles/" + target + ".dir/" in
               e["command"].replace("\\", "/")]
    assert entries, target
    own = {Path(e["file"]).resolve().relative_to(ROOT).as_posix()
           for e in entries if Path(e["file"]).resolve().is_relative_to(ROOT / "src")}
    assert own == sources, (target, own)
    for e in entries:
        assert "LIB_PICO_STDIO_UART=1" not in e["command"]
        assert Path(e["file"]).name != "stdio_uart.c"
    for e in entries:
        if Path(e["file"]).resolve().is_relative_to(ROOT / "src"):
            assert "LIB_PICO_STDIO_USB=1" in e["command"]
            assert "-Wall" in e["command"] and "-Wextra" in e["command"]
    for ext in ("elf", "bin", "uf2"):
        assert (args.build_dir / (target + "." + ext)).stat().st_size > 0

wifi = (ROOT / "src/apps/wifi_pico_main.c").read_text()
sensor = (ROOT / "src/apps/sensor_pico_test_main.c").read_text()
for source, uart, tx, rx in ((wifi, "uart0", 0, 1), (sensor, "uart1", 4, 5)):
    assert f"uart_init({uart}, 115200)" in source
    assert f"uart_set_format({uart}, 8, 1, UART_PARITY_NONE)" in source
    for pin in (tx, rx):
        assert f"gpio_set_function({pin}, GPIO_FUNC_UART)" in source
    assert "#define SESSION_ID 0x11223344u" in source
assert "pico1_ctx_t" not in wifi and "mock_logger" not in wifi
assert "pico2_ctx_t" not in sensor and "pico2_ui" not in sensor
assert "mock_logger_data(), mock_logger_length()" in sensor
assert "pico2_send_command(" not in wifi and "pico2_clear_log(" not in wifi
print("PASS split structure: isolated source sets, UART/pins/session, USB-only stdio, all firmware outputs")
