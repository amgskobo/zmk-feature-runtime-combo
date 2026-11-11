#!/usr/bin/env python3
"""Actual owner CMake generation, configured ARM types and dependency refresh."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def main():
    sys.path.insert(0, str(ROOT / "tests/rpc-lock"))
    from list_wire import verify_response_storage

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--fault", choices=("missing-base-dependency", "fixed-capacity")
    )
    args = parser.parse_args()
    if (
        not args.output.is_absolute()
        or args.output.exists()
        or args.output.is_symlink()
    ):
        parser.error("Use a fresh --output directory; existing evidence is preserved")
    out = args.output.resolve()
    if out.is_relative_to(ROOT.resolve()):
        parser.error("output must be outside the owner source tree")
    out.mkdir(parents=True)
    commands, profiles = [], []

    def run(name, argv, cwd=ROOT):
        argv = [str(value) for value in argv]
        result = subprocess.run(
            argv, cwd=cwd, capture_output=True, text=True, timeout=120
        )
        (out / (name + ".stdout")).write_text(result.stdout)
        (out / (name + ".stderr")).write_text(result.stderr)
        commands.append(
            {"argv": argv, "cwd": str(cwd), "exit": result.returncode, "name": name}
        )
        (out / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        if result.returncode:
            raise RuntimeError(f"{name} failed; see {out}")
        return result.stdout

    names = [
        os.fsdecode(value)
        for value in subprocess.check_output(
            ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
            cwd=ROOT,
        ).split(b"\0")
        if value
    ]
    source = {
        name: hashlib.sha256(
            os.fsencode(os.readlink(ROOT / name))
            if (ROOT / name).is_symlink()
            else (ROOT / name).read_bytes()
        ).hexdigest()
        for name in names
    }
    (out / "source-inputs.json").write_text(json.dumps(source, indent=2) + "\n")
    target = out / "input"
    target.mkdir()
    for name in names:
        src, dst = ROOT / name, target / name
        dst.parent.mkdir(parents=True, exist_ok=True)
        if src.is_symlink():
            dst.symlink_to(os.readlink(src))
        else:
            shutil.copy2(src, dst)

    cmake = target / "CMakeLists.txt"
    template = target / "proto/cormoran/runtime_combo/runtime_combo.options.in"
    base = template.with_suffix("")
    if args.fault == "missing-base-dependency":
        text = cmake.read_text()
        original = "set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS\n                     ${RUNTIME_COMBO_NANOPB_OPTIONS_FILE})"
        assert text.count(original) == 1
        cmake.write_text(text.replace(original, ""))
    elif args.fault == "fixed-capacity":
        text = template.read_text()
        assert text.count("@CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS@") == 1
        template.write_text(text.replace("@CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS@", "16"))

    zephyr = run("zephyr", ["west", "list", "zephyr", "-f", "{abspath}"]).strip()
    nanopb = run("nanopb", ["west", "list", "nanopb", "-f", "{abspath}"]).strip()
    compiler = shutil.which("arm-zephyr-eabi-gcc")
    nm = shutil.which("arm-zephyr-eabi-nm")
    if not compiler or not nm:
        raise RuntimeError(
            "Selected ARM compiler/nm are required in the prepared environment"
        )
    build = out / "build"
    common = [
        "cmake",
        "-S",
        ROOT / "tests/proto-layout",
        "-B",
        build,
        "-G",
        "Ninja",
        "-DOWNER_INPUT=" + str(target),
        "-DSELECTED_ZEPHYR=" + zephyr,
        "-DSELECTED_NANOPB=" + nanopb,
        "-DCMAKE_SYSTEM_NAME=Generic",
        "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
        "-DCMAKE_C_COMPILER=" + compiler,
        "-DCMAKE_C_FLAGS=-mcpu=cortex-m4 -mthumb",
    ]

    def inspect(count, label):
        run(
            "build-" + label,
            ["cmake", "--build", build, "--target", "layout_witness", "-j", "1"],
        )
        objects = list(build.glob("CMakeFiles/layout_witness.dir/layout.c.*"))
        objects = [path for path in objects if path.suffix in (".o", ".obj")]
        assert len(objects) == 1, "Missing/duplicate compiled ARM type witness"
        output = run("symbols-" + label, [nm, "-S", "--size-sort", objects[0]])
        sizes = {
            line.split()[-1]: int(line.split()[1], 16) for line in output.splitlines()
        }
        verify_response_storage(sizes["response_type"], count, (4, 1, 1))
        assert sizes["combo_type"] == 160, "Configured Combo element RAM changed"
        for name in [
            "position_word",
            "layer_word",
            "binding_param1",
            "binding_param2",
            "request_index",
            "request_timeout",
            "request_prior",
            "request_position",
        ]:
            assert sizes[name] == 4, "Full-width protocol field narrowed: " + name
        profiles.append({"count": count, "stage": label, "symbols": sizes})
        return sizes

    run("configure-8", [*common, "-DCONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS=8"])
    inspect(8, "8")
    generated = build / "owner/proto/cormoran/runtime_combo/runtime_combo.options"
    for kind, path in [("base", base), ("template", template)]:
        marker = f"# {kind} dependency refresh sentinel"
        path.write_text(path.read_text() + "\n" + marker + "\n")
        inspect(8, kind + "-refresh")
        assert marker in generated.read_text(), (
            f"{kind.title()} options dependency did not reconfigure"
        )
    run("configure-16", [*common, "-DCONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS=16"])
    inspect(16, "16")
    stable = all(
        hashlib.sha256(
            os.fsencode(os.readlink(ROOT / name))
            if (ROOT / name).is_symlink()
            else (ROOT / name).read_bytes()
        ).hexdigest()
        == source[name]
        for name in names
    )
    assert stable, "Owner source drift during protocol layout check"
    (out / "result.json").write_text(
        json.dumps(
            {
                "status": "PASS",
                "profiles": profiles,
                "source_stable": stable,
                "fault": args.fault,
                "scope": "Actual owner CMake and generated ARM protocol objects; not a firmware/application execution",
            },
            indent=2,
        )
        + "\n"
    )
    print("runtime combo protocol layout: PASS profiles=2 refreshes=2")


if __name__ == "__main__":
    main()
