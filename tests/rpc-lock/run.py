#!/usr/bin/env python3
"""Studio lock policy of the runtime combo RPC handler with real nanopb messages."""

import argparse
from contextlib import nullcontext
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from list_wire import load_protocol, verify_response_storage, verify_vectors

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument(
    "--source",
    type=Path,
    default=root / "src/studio/runtime_combo_handler.c",
    help="override the handler source for regression reproduction",
)
parser.add_argument("--output", type=Path, help="fresh retained evidence directory")
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

folder = args.output or Path(tempfile.mkdtemp(prefix="runtime-combo-rpc-lock-"))
if args.output:
    if not folder.is_absolute() or folder.exists() or folder.is_symlink():
        parser.error("Use a fresh --output directory; existing evidence is preserved")
    folder = folder.resolve()
    if folder.is_relative_to(root.resolve()):
        parser.error("output must be outside the owner source tree")
    folder.mkdir(parents=True)
print(f"Runtime combo RPC evidence: {folder}", flush=True)
with nullcontext(folder) as folder:
    work = Path(folder)
    commands = []

    def invoke(name, argv, **kwargs):
        argv = [str(value) for value in argv]
        result = subprocess.run(argv, capture_output=True, text=True, **kwargs)
        (work / f"{name}.stdout").write_text(result.stdout)
        (work / f"{name}.stderr").write_text(result.stderr)
        commands.append({"argv": argv, "exit": result.returncode, "name": name})
        (work / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        return result

    inputs = [
        args.source.resolve(),
        root / "proto/cormoran/runtime_combo/runtime_combo.proto",
    ]
    inputs.extend((root / "proto").rglob("*.options*"))
    (work / "source-inputs.json").write_text(
        json.dumps(
            {
                str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in inputs
            },
            indent=2,
        )
        + "\n"
    )
    # The generator may regenerate its own Python proto; keep the checkout read-only.
    shutil.copytree(nanopb / "generator", work / "generator")

    def generate(count=None):
        label = str(count) if count else "standalone"
        destination = work / ("generated-" + label)
        destination.mkdir()
        options = root / "proto/cormoran/runtime_combo/runtime_combo.options"
        if count:
            template = options.with_suffix(".options.in").read_text()
            text = template.replace(
                "@RUNTIME_COMBO_NANOPB_OPTIONS@", options.read_text()
            )
            text = text.replace("@CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS@", str(count))
            if "@" in text:
                raise AssertionError("Unresolved Combo options template")
            options = destination / "runtime_combo.options"
            options.write_text(text)
        options_args = ["-f", options] if count else []
        generated = invoke(
            "generate-" + label,
            [
                "python3",
                work / "generator/nanopb_generator.py",
                "-I",
                root / "proto",
                *options_args,
                "-D",
                destination,
                root / "proto/cormoran/runtime_combo/runtime_combo.proto",
            ],
            timeout=60,
        )
        if generated.returncode:
            raise SystemExit(generated.stdout + generated.stderr)
        return destination

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
    protocol = load_protocol(work, root)

    def compile_handler(binary, label, max_combos, flags, generated):
        return invoke(
            binary.name + "-compile",
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
                "-I" + str(generated),
                "-I" + str(work),
                "-I" + str(nanopb),
                "-I" + str(root / "include"),
                str(root / "tests/rpc-lock/handler_test.c"),
                str(generated / "cormoran/runtime_combo/runtime_combo.pb.c"),
                *(
                    str(nanopb / f"pb_{part}.c")
                    for part in ("common", "encode", "decode")
                ),
                "-o",
                str(binary),
            ],
            timeout=60,
        )

    for count in (8, 16):
        generated = generate(count)
        for label, flags in variants.items():
            binary = work / f"rpc-lock-{label}-{count}"
            built = compile_handler(binary, label, count, flags, generated)
            if built.returncode:
                raise SystemExit(built.stdout + built.stderr)
            result = invoke(
                binary.name + "-run",
                [str(binary)],
                timeout=20,
                env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"},
            )
            if result.returncode:
                raise SystemExit(result.stdout + result.stderr)
            print(result.stdout)
            verify_vectors(result.stdout, protocol, count)
            size = re.findall(
                r"^runtime combo response bytes: (\d+)$", result.stdout, re.M
            )
            if len(size) != 1:
                raise AssertionError("Missing/duplicate response size")
            abi = re.findall(
                r"^runtime combo ABI: pointer=(\d+) slow_enum=(\d+) source_enum=(\d+)$",
                result.stdout,
                re.M,
            )
            if len(abi) != 1:
                raise AssertionError("Missing/duplicate response ABI")
            verify_response_storage(int(size[0]), count, tuple(map(int, abi[0])))

    # A plain nanopb invocation still uses the full protocol-16 .options file.
    standalone = generate()
    binary = work / "rpc-lock-standalone"
    built = compile_handler(binary, "standalone", 16, ["-O2"], standalone)
    if built.returncode:
        raise SystemExit(built.stdout + built.stderr)
    result = invoke(binary.name + "-run", [binary], timeout=20)
    if result.returncode:
        raise SystemExit(result.stdout + result.stderr)
    verify_vectors(result.stdout, protocol, 16)

    # More slots than one ListCombos response holds must not build: the list
    # would be written past its fixed-capacity array.
    oversized = compile_handler(
        work / "rpc-lock-oversized", "oversized", 17, ["-O0"], standalone
    )
    if (
        oversized.returncode == 0
        or "ListCombosResponse capacity" not in oversized.stderr
    ):
        raise SystemExit("17 combo slots built against a 16-entry ListCombos response")
    print("runtime combo RPC capacity: PASS (17 slots rejected at build time)")
