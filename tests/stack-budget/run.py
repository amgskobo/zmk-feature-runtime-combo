#!/usr/bin/env python3
"""Fixed-input Cortex-M replay stack measurement; three runs of one ARM ELF."""

import argparse
import glob
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

PRESS_TIMES = [
    100000,
    101100,
    101650,
    101925,
    102060,
    102125,
    102160,
    102175,
    102176,
    102177,
    102178,
    102179,
    102180,
    102181,
    102182,
]
TERMINAL = "COMBO_STACK_RESULT PASS groups=2 reports=64"


def assert_budget(used, budget, reserve=0):
    if used + reserve > budget:
        raise AssertionError("COMBO_ARM_STACK_BUDGET")


def validate(text):
    if re.search(
        r"ASSERTION FAIL|FATAL ERROR|[Ss]tack overflow|runtime error:|AddressSanitizer",
        text,
    ):
        raise AssertionError("COMBO_ARM_EXECUTION: fatal/assertion/sanitizer output")
    if text.count(TERMINAL) != 1 or not re.search(
        r"^" + TERMINAL + r"\r?\n", text, re.M
    ):
        raise AssertionError(
            "COMBO_ARM_COMPLETION: exactly two completed cases required"
        )
    events = [
        tuple(map(int, m))
        for m in re.findall(
            r"^COMBO_STACK_EVENT case=(\d+) index=(\d+) key=(\d+) state=(\d+) time=(\d+)\r?$",
            text,
            re.M,
        )
    ]
    expected = []
    for case, offset in [(1, 0), (8, 1000000)]:
        keys = list(range(8, 23)) + [4]  # HID E..S, then the unrelated A.
        times = PRESS_TIMES + [102183] + list(range(102200, 102215)) + [102220]
        expected += [
            (case, i, keys[i % 16], int(i < 16), times[i] + offset) for i in range(32)
        ]
    if events != expected or text.count("COMBO_STACK_EVENT ") != 64:
        raise AssertionError("COMBO_ARM_ORDER_TIME: missing/reordered input or release")
    rows = [
        tuple(map(int, m))
        for m in re.findall(
            r"^COMBO_STACK case=(\d+) configured=(\d+) usable=(\d+) unused=(\d+) used=(\d+) replay_span=(\d+) reports=(\d+)\r?$",
            text,
            re.M,
        )
    ]
    if (
        len(rows) != 2
        or [r[0] for r in rows] != [1, 8]
        or text.count("COMBO_STACK case=") != 2
    ):
        raise AssertionError("COMBO_ARM_SAMPLES: both ordered metrics required")
    for case, configured, usable, unused, used, span, reports in rows:
        if (
            configured != 8192
            or usable != configured
            or unused + used != usable
            or reports != 32
        ):
            raise AssertionError("COMBO_ARM_SAMPLES: inconsistent stack accounting")
        if not 0 < span <= used or unused < 128:
            raise AssertionError(
                "COMBO_ARM_STACK_HEADROOM: measured path needs128B reserve"
            )
        assert_budget(used, configured, 128)
    if rows[0][4] > 4096 - 128:
        raise AssertionError(
            "COMBO_ARM_SMALL_CONTROL: one candidate must fit the former budget"
        )
    if rows[1][4] <= 4096:
        raise AssertionError(
            "COMBO_ARM_ORIGINAL_CONTROL: original4096B failure not reproduced"
        )
    try:
        assert_budget(rows[1][4], 4096)
    except AssertionError as error:
        if str(error) != "COMBO_ARM_STACK_BUDGET":
            raise
    else:
        raise AssertionError(
            "COMBO_ARM_ORIGINAL_CONTROL: expected budget assertion missing"
        )
    return {
        "one_candidate": dict(
            zip(
                [
                    "case",
                    "configured",
                    "usable",
                    "unused",
                    "used",
                    "replay_span",
                    "reports",
                ],
                rows[0],
            )
        ),
        "eight_candidates": dict(
            zip(
                [
                    "case",
                    "configured",
                    "usable",
                    "unused",
                    "used",
                    "replay_span",
                    "reports",
                ],
                rows[1],
            )
        ),
        "original4096_control": "EXPECTED FAIL: COMBO_ARM_STACK_BUDGET",
        "configured8192": "PASS for this fixed-input path; no universal/hardware claim",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    owner = Path(__file__).resolve().parents[2]
    workspace = args.workspace.resolve()
    out = args.output.parent.resolve() / args.output.name
    if out.exists() or out.is_symlink():
        raise FileExistsError("Use a fresh output path: " + str(out))
    out.mkdir(parents=True)

    def git(path, *arguments):
        return subprocess.check_output(
            ["git", "--no-optional-locks", "-C", str(path), *arguments], text=True
        ).strip()

    def project(name):
        return Path(
            subprocess.check_output(
                ["west", "list", name, "-f", "{abspath}"], cwd=workspace, text=True
            ).strip()
        ).resolve()

    projects = {
        name: project(name) for name in ["zmk", "zephyr", "zmk-feature-custom-settings"]
    }
    assert (
        git(projects["zmk"], "rev-parse", "HEAD")
        == "e5c9b6915b56801193e359dd9bad4a167ce0d1b8"
    )
    assert (
        git(projects["zephyr"], "rev-parse", "HEAD")
        == "10ba6d0cb38bc3d258775d27982f707599320085"
    )
    for path in projects.values():
        assert not git(path, "status", "--porcelain=v1", "--untracked-files=all"), str(
            path
        )

    def source_hashes():
        names = git(
            owner, "ls-files", "--cached", "--others", "--exclude-standard"
        ).splitlines()
        return {
            n: hashlib.sha256(
                os.fsencode(os.readlink(owner / n))
                if (owner / n).is_symlink()
                else (owner / n).read_bytes()
            ).hexdigest()
            for n in names
        }

    before = source_hashes()
    identity = {
        "owner_head": git(owner, "rev-parse", "HEAD"),
        "owner_files": before,
        "projects": {
            name: {"path": str(path), "head": git(path, "rev-parse", "HEAD")}
            for name, path in projects.items()
        },
    }
    (out / "inputs.json").write_text(json.dumps(identity, indent=2) + "\n")
    env = dict(
        os.environ,
        PYTHONDONTWRITEBYTECODE="1",
        ZEPHYR_BASE=str(projects["zephyr"]),
        Zephyr_DIR=str(projects["zephyr"] / "share/zephyr-package/cmake"),
    )
    if env.get("ZEPHYR_MODULES"):
        env["ZEPHYR_MODULES"] += ";" + str(owner / "tests/stack-budget/module")
    command = [
        "west",
        "build",
        "-s",
        str(projects["zmk"] / "app"),
        "-b",
        "mps2/an386",
        "-d",
        str(out / "build"),
        "-p",
        "always",
        "--",
        "-DZMK_CONFIG=" + str(owner / "tests/stack-budget"),
        "-DKEYMAP_FILE=" + str(owner / "tests/stack-budget/fixture.keymap"),
        "-DZMK_EXTRA_MODULES="
        + str(owner)
        + ";"
        + str(projects["zmk-feature-custom-settings"])
        + ";"
        + str(owner / "tests/stack-budget/module"),
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
    ]
    (out / "command.json").write_text(json.dumps(command, indent=2) + "\n")
    with (out / "build.log").open("x") as log:
        built = subprocess.run(
            command, cwd=workspace, env=env, stdout=log, stderr=subprocess.STDOUT
        )
    if built.returncode:
        (out / "result.json").write_text(
            json.dumps({"status": "BUILD BLOCKED", "exit": built.returncode}, indent=2)
            + "\n"
        )
        print((out / "build.log").read_text()[-7000:], flush=True)
        raise SystemExit(built.returncode)
    config = (out / "build/zephyr/.config").read_text().splitlines()
    required = {
        "ARM": "y",
        "ARM_MPU": "y",
        "MPU_STACK_GUARD": "y",
        "THREAD_STACK_INFO": "y",
        "INIT_STACKS": "y",
        "ASSERT": "y",
        "ZMK_RUNTIME_COMBO": "y",
        "ZMK_RUNTIME_COMBO_MAX_COMBOS": "8",
        "ZMK_RUNTIME_COMBO_MAX_POSITIONS_PER_COMBO": "16",
        "SYSTEM_WORKQUEUE_STACK_SIZE": "8192",
    }
    for key, value in required.items():
        assert "CONFIG_" + key + "=" + value in config, key
    # A dependency-hidden bool may be absent from .config, which also means n.
    assert "CONFIG_STACK_SENTINEL=y" not in config
    commands = json.loads((out / "build/compile_commands.json").read_text())
    compiled = {str(Path(row["file"]).resolve()) for row in commands}
    required_sources = [
        owner / "src/runtime_combo/runtime_combo.c",
        owner / "tests/stack-budget/module/kscan.c",
        projects["zmk"] / "app/src/event_manager.c",
        projects["zmk"] / "app/src/keymap.c",
        projects["zmk"] / "app/src/hid.c",
        projects["zmk"] / "app/src/behaviors/behavior_key_press.c",
        projects["zephyr"] / "kernel/work.c",
    ]
    for source in required_sources:
        assert str(source.resolve()) in compiled, str(source)
    (out / "compiled-source-hashes.json").write_text(
        json.dumps(
            {
                path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
                for path in sorted(compiled)
            },
            indent=2,
        )
        + "\n"
    )
    elf = out / "build/zephyr/zmk.elf"
    digest = hashlib.sha256(elf.read_bytes()).hexdigest()
    qemu = sorted(glob.glob("/opt/**/qemu-system-arm", recursive=True))[0]
    rows = []
    for trial in range(1, 4):
        raw = out / f"run-{trial}.log"
        command = [
            qemu,
            "-cpu",
            "cortex-m4",
            "-machine",
            "mps2-an386",
            "-display",
            "none",
            "-monitor",
            "none",
            "-net",
            "none",
            "-icount",
            "shift=6,align=off,sleep=off",
            "-rtc",
            "clock=vm",
            "-serial",
            "file:" + str(raw),
            "-kernel",
            str(elf),
        ]
        start = time.monotonic()
        with (out / f"qemu-{trial}.log").open("x") as log:
            process = subprocess.Popen(
                command, stdin=subprocess.DEVNULL, stdout=log, stderr=log
            )
            try:
                while time.monotonic() - start < 30 and process.poll() is None:
                    text = raw.read_text(errors="replace") if raw.exists() else ""
                    if re.search(
                        r"^COMBO_STACK_RESULT PASS groups=2 reports=64\r?\n", text, re.M
                    ):
                        break
                    if re.search(r"ASSERTION FAIL|FATAL ERROR", text):
                        break
                    time.sleep(0.02)
            finally:
                if process.poll() is None:
                    process.terminate()
                process.wait(timeout=5)
        text = raw.read_text(errors="replace") if raw.exists() else ""
        row = {
            "trial": trial,
            "command": command,
            "elf_sha256": digest,
            "raw_sha256": hashlib.sha256(
                raw.read_bytes() if raw.exists() else b""
            ).hexdigest(),
            "process_exit": process.returncode,
            "elapsed_seconds": time.monotonic() - start,
        }
        try:
            row.update(validate(text))
            row["status"] = "PASS"
        except AssertionError as error:
            row.update(status="FAIL", error=str(error))
        rows.append(row)
        print("ARM_STACK trial=" + str(trial) + " " + json.dumps(row), flush=True)
    after = source_hashes()
    stable = before == after
    (out / "source-after.json").write_text(json.dumps(after, indent=2) + "\n")
    result = {
        "status": "PASS"
        if stable and all(r["status"] == "PASS" for r in rows)
        else "FAIL",
        "source_stable": stable,
        "elf_sha256": digest,
        "trials": rows,
        "hardware": "NOT RUN",
        "scope": "Fixed8-slot/16-position ARM input trace on an8192B workqueue; controlled Kscan and no physical transport/NVS; no arbitrary reentry proof",
    }
    (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    raise SystemExit(result["status"] != "PASS")


if __name__ == "__main__":
    main()
