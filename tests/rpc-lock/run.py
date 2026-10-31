#!/usr/bin/env python3
"""Studio lock policy of the runtime combo RPC handler with real nanopb messages."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument(
    "--source",
    type=Path,
    default=root / "src/studio/runtime_combo_handler.c",
    help="override the handler source for regression reproduction",
)
args = parser.parse_args()
nanopb = (
    Path(os.environ["NANOPB_SOURCE"])
    if "NANOPB_SOURCE" in os.environ
    else Path(
        subprocess.check_output(
            ["west", "list", "nanopb", "-f", "{abspath}"], text=True
        ).strip()
    )
)

with tempfile.TemporaryDirectory(prefix="runtime-combo-rpc-lock-") as folder:
    work = Path(folder)
    # The generator may regenerate its own Python proto; keep the checkout read-only.
    shutil.copytree(nanopb / "generator", work / "generator")
    subprocess.run(
        [
            "python3",
            str(work / "generator/nanopb_generator.py"),
            "-I",
            str(root / "proto"),
            "-D",
            str(work),
            str(root / "proto/cormoran/runtime_combo/runtime_combo.proto"),
        ],
        check=True,
        timeout=60,
    )
    for rel in (
        "zephyr/logging/log.h",
        "zephyr/sys/util.h",
        "zmk/behavior.h",
        "zmk/studio/core.h",
        "zmk/studio/custom.h",
        "cormoran/zmk/custom_settings.h",
    ):
        path = work / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("/* Controlled framework API; see handler_test.c. */\n")
    variants = {
        "optimized": ["-O2"],
        "sanitized": [
            "-O1",
            "-g",
            "-fsanitize=address,undefined",
            "-fno-sanitize-recover=all",
        ],
    }

    def compile_handler(binary, label, max_combos, flags):
        return subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wno-unused-parameter",
                "-Wno-unused-function",
                *flags,
                f"-DCONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS={max_combos}",
                "-DCONFIG_ZMK_RUNTIME_COMBO_MAX_POSITIONS_PER_COMBO=16",
                f'-DHANDLER_SOURCE="{args.source.resolve()}"',
                f'-DTEST_VARIANT="{label}"',
                "-I" + str(work),
                "-I" + str(nanopb),
                "-I" + str(root / "include"),
                str(root / "tests/rpc-lock/handler_test.c"),
                str(work / "cormoran/runtime_combo/runtime_combo.pb.c"),
                *(
                    str(nanopb / f"pb_{part}.c")
                    for part in ("common", "encode", "decode")
                ),
                "-o",
                str(binary),
            ],
            capture_output=True,
            text=True,
            timeout=60,
        )

    for label, flags in variants.items():
        binary = work / f"rpc-lock-{label}"
        built = compile_handler(binary, label, 16, flags)
        if built.returncode:
            raise SystemExit(built.stdout + built.stderr)
        subprocess.run(
            [str(binary)],
            check=True,
            timeout=20,
            env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"},
        )

    # More slots than one ListCombos response holds must not build: the list
    # would be written past its fixed-capacity array.
    oversized = compile_handler(work / "rpc-lock-oversized", "oversized", 17, ["-O0"])
    if (
        oversized.returncode == 0
        or "ListCombosResponse capacity" not in oversized.stderr
    ):
        raise SystemExit("17 combo slots built against a 16-entry ListCombos response")
    print("runtime combo RPC capacity: PASS (17 slots rejected at build time)")
