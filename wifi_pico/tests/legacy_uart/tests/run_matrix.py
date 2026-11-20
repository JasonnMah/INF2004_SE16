#!/usr/bin/env python3
"""Build five test selections and optionally execute their actual RP2040 binaries.

Run from any directory. Needs CMake, Ninja, arm-none-eabi-gcc and PICO_SDK_PATH.
Local simulation also needs Node.js and `npm ci --prefix tests`.
No project source files are changed; each mode uses its own build directory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
NAMES = ["happy_path", "corrupt_first_data", "drop_first_data_ack",
         "wrong_first_block", "drop_config_ack"]
ORIGINAL_SHA256 = "b4aa0d005fe2e74a3dbed5fcf31276fffa1424636ad876fa8a77bdd52d69c1f3"
LEGACY_GOLDEN_SHA256 = [
    "13ebe6b09cc6f4624b4c9fa461db8793c65b94831ae3ab67e0ff8021dcadd150",
    "be6a645ce764d27acb3582e243062105922561afa847f2ad79e8ef4fdc7b7dbc",
    "8b7c6ece635693f092511beda1fcb06b44b12c1a7609b0dd4b47da8d8a5bf4bd",
    "d09cd6a4d2905b7659adaf425edc6ae4fcead035cd30bf48de331ed299f19a26",
    "07b0231edfcc374b6fe81d8ef6fa93a853fcd74e2f7842e8f6582fb8b0618a0e",
]
GOLDEN_SHA256 = [
    "a26424b99ca6ad45c9bcdc3c9daec6f5d1772b2df2f23f4f1c4b9422c0e61357",
    "c9864941438329e0ab7c67c59c9fe338c250907d4fcea19acddb6c658f52aec9",
    "808d62d08a93d2caa3fc61d0233e0ae9c3783c80dc5b70ae59a7ca826f7cf7f0",
    "8c65fd1e65df5a0c4dd2840b0d34bb64dd1c01858a1b1b48c2ef5bbd96a641fa",
    "bebc7bf5940c968a85aa9245e28e4d827eff3fefb39d746d40c0f4a018215a8e",
]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_hashes():
    paths = sorted((ROOT / "src").rglob("*.c")) + sorted((ROOT / "src").rglob("*.h"))
    paths += [ROOT / "CMakeLists.txt", ROOT / "diagram.json", ROOT / "wokwi.toml"]
    return {str(path.relative_to(ROOT)): sha256(path) for path in paths}


def verify_history(archive):
    # History is optional; active builds/tests do not depend on an external backup.
    if archive is not None:
        with zipfile.ZipFile(archive) as history:
            original = history.read("backup/main.c.original")
        assert hashlib.sha256(original).hexdigest() == ORIGINAL_SHA256, "Archived original checksum mismatch"


def run(command, log):
    with log.open("w", encoding="utf-8") as output:
        output.write("Command: " + repr(command) + "\n")
        output.flush()
        result = subprocess.run(command, cwd=ROOT, stdout=output,
                                stderr=subprocess.STDOUT, timeout=600)
    if result.returncode:
        raise RuntimeError(f"Exit {result.returncode}; see {log}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-only", action="store_true")
    parser.add_argument("--results-dir", type=Path, default=ROOT / "build/regression/results")
    parser.add_argument("--history-archive", type=Path,
                        help="Optionally verify the original source inside the external history ZIP")
    args = parser.parse_args()
    results = args.results_dir.resolve()
    results.mkdir(parents=True, exist_ok=True)
    before = source_hashes()
    verify_history(args.history_archive)
    for mode, expected in enumerate(LEGACY_GOLDEN_SHA256):
        assert sha256(ROOT / f"tests/golden/uart-{mode}.log.wire.json") == expected, "Baseline fixture checksum mismatch"
    for mode, expected in enumerate(GOLDEN_SHA256):
        assert sha256(ROOT / f"tests/golden/100-byte/uart-{mode}.log.wire.json") == expected, "100-byte fixture checksum mismatch"
    rows = []
    for mode, name in enumerate(NAMES):
        build = ROOT / "build/regression" / f"mode-{mode}"
        row = {"mode": mode, "name": name, "build": "NOT RUN",
               "local_emulator": "NOT RUN", "wokwi": "NOT RUN"}
        rows.append(row)
        try:
            run(["cmake", "-S", str(ROOT), "-B", str(build), "-G", "Ninja",
                 "-DPICO_BOARD=pico_w", "-DCMAKE_BUILD_TYPE=Debug",
                 f"-DPython3_EXECUTABLE={sys.executable}",
                 f"-DPROTOCOL_TEST={mode}"], results / f"configure-{mode}.log")
            # Compile the actual modules; select the demo mode without rewriting source.
            commands = json.loads((build / "compile_commands.json").read_text())
            own_commands = [entry for entry in commands
                            if Path(entry["file"]).resolve().is_relative_to(ROOT / "src")
                            and "CMakeFiles/uart_protocol.dir/" in entry["command"].replace("\\", "/")]
            # UI code belongs only to its separate firmware, not uart_protocol.
            expected_sources = {path.resolve() for path in (ROOT / "src").rglob("*.c")
                                if path != ROOT / "src/pico2/pico2_ui.c"
                                and ROOT / "src/apps" not in path.parents
                                and ROOT / "src/codec" not in path.parents}
            assert {Path(entry["file"]).resolve() for entry in own_commands} == expected_sources
            assert all(f"-DPROTOCOL_TEST={mode}" in entry["command"] and
                       "-Wall" in entry["command"] and "-Wextra" in entry["command"]
                       for entry in own_commands), "Missing selector or warnings"
            run(["cmake", "--build", str(build), "--parallel", "4"],
                results / f"build-{mode}.log")
            row["build"] = "PASS"
            row["binary_sha256"] = hashlib.sha256((build / "uart_protocol.bin").read_bytes()).hexdigest()
            row["bin_bytes"] = (build / "uart_protocol.bin").stat().st_size
            assert all((build / f"uart_protocol.{ext}").is_file() for ext in ["elf", "bin", "uf2"])
            try:
                row["size"] = subprocess.check_output(
                    ["arm-none-eabi-size", str(build / "uart_protocol.elf")], text=True)
            except FileNotFoundError:
                row["size"] = "arm-none-eabi-size unavailable"
            if not args.build_only:
                run(["node", str(ROOT / "tests/run_firmware.mjs"),
                     str(build / "uart_protocol.bin"), str(mode),
                     str(results / f"uart-{mode}.log"),
                     str(ROOT / f"tests/golden/100-byte/uart-{mode}.log.wire.json"), "100"], results / f"emulator-{mode}.log")
                row["local_emulator"] = "PASS"
                row["baseline_uart_bytes"] = "IDENTICAL (both UARTs)"
            print(f"Mode {mode} {name}: build {row['build']}; local emulator {row['local_emulator']}", flush=True)
        except (RuntimeError, AssertionError, OSError, subprocess.TimeoutExpired) as error:
            row["error"] = str(error)
            print(f"Mode {mode} {name}: FAILED: {error}", flush=True)
    assert before == source_hashes(), "Active source/configuration was modified during tests"
    verify_history(args.history_archive)
    report = {"source_sha256": before,
              "original_backup_sha256": ORIGINAL_SHA256,
              "history_archive_verified": args.history_archive is not None,
              "total_c_lines": sum(len(p.read_text().splitlines()) for p in (ROOT / "src").rglob("*.c")),
              "main_c_lines": len((ROOT / "src/main.c").read_text().splitlines()),
              "board": "pico_w", "platform": "rp2040", "tests": rows,
              "note": "Local emulator != Wokwi VS Code/cloud or physical hardware."}
    (results / "matrix.json").write_text(json.dumps(report, indent=2) + "\n")
    return 1 if any("error" in row for row in rows) else 0


if __name__ == "__main__":
    sys.exit(main())
