"""Compile real production DTS defaults before any narrowing or bit shift."""

from pathlib import Path
import datetime
import hashlib
import json
import os
import shlex
import subprocess
import tempfile


OWNER = Path(__file__).resolve().parents[2]


def west_path(name: str) -> Path:
    return Path(
        subprocess.check_output(
            ["west", "list", name, "-f", "{abspath}"], cwd=OWNER, text=True
        ).strip()
    )


def cases() -> list[dict]:
    result = [
        dict(name="omitted", positions=[0, 1, 2]),
        dict(
            name="upper-boundaries",
            positions=[0, 32768, 65535],
            layers=[0, 15, 31],
            timeout=65535,
            prior_idle=65535,
            slot=7,
        ),
        dict(
            name="zero-inherits",
            positions=[0, 1, 2],
            layers=[0],
            timeout=0,
            prior_idle=0,
        ),
    ]
    for field, invalids, diagnostic in (
        (
            "positions",
            [65536, 0xFFFFFFFF],
            "runtime combo default key position must fit uint16",
        ),
        ("layers", [32, 0xFFFFFFFF], "runtime combo default layer must be in 0..31"),
    ):
        for invalid in invalids:
            for index, location in enumerate(("first", "middle", "last")):
                case = dict(name=f"{field}-{invalid}-{location}", positions=[0, 1, 2])
                case[field] = [0, 1, 2]
                case[field][index] = invalid
                case["diagnostic"] = diagnostic
                result.append(case)
    for field, diagnostic in (
        ("timeout", "runtime combo default timeout-ms must fit uint16"),
        ("prior_idle", "runtime combo default require-prior-idle-ms must fit uint16"),
    ):
        for invalid in (65536, 0xFFFFFFFF):
            case = dict(name=f"{field}-{invalid}", positions=[0, 1, 2])
            case[field] = invalid
            case["diagnostic"] = diagnostic
            result.append(case)
    for invalid in (8, 0xFFFFFFFF):
        result.append(
            dict(
                name=f"slot-{invalid}",
                positions=[0, 1, 2],
                slot=invalid,
                diagnostic="runtime combo default slot is out of range",
            )
        )
    result.extend(
        [
            dict(
                name="too-few-positions",
                positions=[0],
                diagnostic="runtime combo default must have at least two key positions",
            ),
            dict(
                name="too-many-positions",
                positions=[0, 1, 2, 3],
                diagnostic="runtime combo default has too many key positions",
            ),
        ]
    )
    return result


def keymap(case: dict) -> str:
    properties = [
        "key-positions = <" + " ".join(map(str, case["positions"])) + ">;",
        "bindings = <&kp A>;",
    ]
    if "layers" in case:
        properties.append("layers = <" + " ".join(map(str, case["layers"])) + ">;")
    for field, prop in (
        ("slot", "slot"),
        ("timeout", "timeout-ms"),
        ("prior_idle", "require-prior-idle-ms"),
    ):
        if field in case:
            properties.append(f"{prop} = <{case[field]}>;")
    return (
        "#include <dt-bindings/zmk/keys.h>\n#include <behaviors.dtsi>\n"
        "/ {\n runtime_combo_defaults {\n"
        '  compatible = "cormoran,runtime-combo-defaults";\n'
        "  boundary {\n   " + "\n   ".join(properties) + "\n  };\n };\n"
        ' keymap { compatible = "zmk,keymap";\n'
        "  default_layer { bindings = <&kp A &kp A &kp A>; };\n };\n};\n"
    )


def run(command: list[str], cwd: Path, log: Path) -> int:
    with log.open("xb") as output:
        return subprocess.run(
            command, cwd=cwd, stdout=output, stderr=subprocess.STDOUT
        ).returncode


def main() -> None:
    requested = os.environ.get("COMBO_DT_ARTIFACTS")
    if requested:
        out = Path(requested).resolve()
        out.mkdir(parents=True, exist_ok=False)
    else:
        out = Path(tempfile.mkdtemp(prefix="runtime-combo-dt-defaults-"))
    zmk = west_path("zmk")
    registry = west_path("zmk-feature-custom-settings")
    source = OWNER / "src/runtime_combo/runtime_combo.c"
    (out / "identities.json").write_text(
        json.dumps(
            dict(
                source=str(source),
                source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                zmk=str(zmk),
                registry=str(registry),
                backend="actual native_sim ZMK app DTS/CMake/production object compile",
                limits="compile guards only; ordinary owner native/board/RPC tests remain required",
            ),
            indent=2,
        ),
        encoding="utf-8",
    )
    failures = []
    results = []
    for case in cases():
        folder = out / case["name"]
        config = folder / "config"
        config.mkdir(parents=True)
        (config / "native_sim.keymap").write_text(keymap(case), encoding="utf-8")
        (config / "native_sim.conf").write_text(
            "CONFIG_GPIO=n\nCONFIG_ZMK_BLE=n\nCONFIG_LOG=y\nCONFIG_SETTINGS=y\n"
            "CONFIG_ZMK_RUNTIME_COMBO=y\nCONFIG_ZMK_RUNTIME_COMBO_TEST=n\n"
            "CONFIG_ZMK_RUNTIME_COMBO_MAX_POSITIONS_PER_COMBO=3\n"
            "CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS=8\n",
            encoding="utf-8",
        )
        build = folder / "build"
        configure = [
            "west",
            "build",
            "--cmake-only",
            "-p",
            "always",
            "-b",
            "native_sim//zmk_test_mock",
            "-s",
            str(zmk / "app"),
            "-d",
            str(build),
            "--",
            f"-DZMK_CONFIG={config}",
            f"-DZMK_EXTRA_MODULES={OWNER};{registry}",
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        ]
        record = dict(
            case=case,
            configure=configure,
            start_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        )
        record["configure_exit"] = run(configure, OWNER, folder / "configure.log")
        if record["configure_exit"]:
            record.update(
                passed=False,
                failure="configuration failed before the production C assertion",
            )
        else:
            config_text = (build / "zephyr/.config").read_text()
            assert "\nCONFIG_ZMK_RUNTIME_COMBO=y\n" in "\n" + config_text
            assert "CONFIG_ZMK_RUNTIME_COMBO_MAX_POSITIONS_PER_COMBO=3" in config_text
            commands = json.loads((build / "compile_commands.json").read_text())
            matching = [
                entry for entry in commands if Path(entry["file"]).resolve() == source
            ]
            assert len(matching) == 1, (
                "real production runtime_combo.c object missing/duplicated"
            )
            arguments = shlex.split(matching[0]["command"])
            target = arguments[arguments.index("-o") + 1]
            compile_command = [
                "cmake",
                "--build",
                str(build),
                "--target",
                target,
                "--",
                "-j2",
            ]
            record["compile"] = compile_command
            record["compile_exit"] = run(compile_command, OWNER, folder / "compile.log")
            raw = (folder / "compile.log").read_text(errors="replace")
            diagnostic = case.get("diagnostic")
            record["passed"] = (
                record["compile_exit"] != 0 and diagnostic in raw
                if diagnostic
                else record["compile_exit"] == 0
            )
            if not record["passed"]:
                record["failure"] = (
                    "invalid input compiled or exact guard diagnostic missing"
                    if diagnostic
                    else "valid boundary refused"
                )
        record["end_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        (folder / "result.json").write_text(
            json.dumps(record, indent=2), encoding="utf-8"
        )
        results.append(record)
        (out / "results.json").write_text(
            json.dumps(results, indent=2), encoding="utf-8"
        )
        print(("PASS " if record["passed"] else "FAIL ") + case["name"], flush=True)
        if not record["passed"]:
            failures.append(case["name"])
    print(
        f"runtime combo DTS defaults cases={len(results)} failures={len(failures)}",
        flush=True,
    )
    raise SystemExit(bool(failures))


if __name__ == "__main__":
    main()
