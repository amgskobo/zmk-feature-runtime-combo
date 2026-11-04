import platform
import os
import tempfile
import re
import subprocess
import unittest
from pathlib import Path

from dataclasses import dataclass

from native_verifier import verify_native
from test_native_verifier import NativeVerifierContract as NativeVerifierContract
from test_timeout_width import TimeoutWidth as TimeoutWidth

from test_build_budget import BuildBudgetContract as BuildBudgetContract

THIS_DIR = Path(__file__).resolve().parent.parent


def run_west(
    args: list[str], cwd: Path | None = None
) -> subprocess.CompletedProcess[str]:
    if args and args[0] == "zmk-build":
        parallelism = int(os.environ.get("OWNER_TEST_BUILD_PARALLELISM", "1"))
        if parallelism < 1:
            raise ValueError("OWNER_TEST_BUILD_PARALLELISM must be a positive integer")
        args = [args[0], "--parallelism", str(parallelism), *args[1:]]
    return subprocess.run(
        ["west", *args],
        capture_output=True,
        text=True,
        cwd=cwd or THIS_DIR,
    )


@dataclass
class NotFound:
    text: str


@dataclass
class ConfigAndDeviceTree:
    # Expected rows in .config
    config: list[str | NotFound]
    # Expected rows in devicetree_generated.h
    device: list[str | NotFound]


class WestCommandsTests(unittest.TestCase):
    WEST_CWD: Path
    WEST_TOPDIR: Path
    BUILD_DIR: Path
    CUSTOM_SETTINGS_MODULE: Path
    ZMK_APP: Path

    @classmethod
    def setUpClass(cls):
        for cwd in (THIS_DIR, THIS_DIR.parent):
            if run_west(["zmk-build", "--help"], cwd=cwd).returncode == 0:
                cls.WEST_CWD = cwd
                break
        else:
            raise AssertionError("west zmk-build extension is unavailable")

        cls.WEST_TOPDIR = Path(run_west(["topdir"], cwd=cls.WEST_CWD).stdout.strip())
        requested_output = os.environ.get("OWNER_TEST_OUTPUT")
        if requested_output:
            cls.BUILD_DIR = Path(requested_output).resolve()
            cls.BUILD_DIR.mkdir(parents=True, exist_ok=False)
        else:
            cls.BUILD_DIR = Path(tempfile.mkdtemp(prefix=THIS_DIR.name + "-tests-"))
        print(f"Owner test output: {cls.BUILD_DIR}", flush=True)
        custom_settings_path = run_west(
            ["list", "-f", "{path}", "zmk-feature-custom-settings"], cwd=cls.WEST_CWD
        ).stdout.strip()
        cls.CUSTOM_SETTINGS_MODULE = cls.WEST_TOPDIR / custom_settings_path
        zmk_path = run_west(
            ["list", "-f", "{path}", "zmk"], cwd=cls.WEST_CWD
        ).stdout.strip()
        cls.ZMK_APP = cls.WEST_TOPDIR / zmk_path / "app"

    @unittest.skipUnless(
        platform.system() == "Linux", "zmk-test is only supported on Linux"
    )
    def test_zmk_test(self):
        cases = ("test", "studio", "overrides", "defaults", "discard")
        self.assertEqual(
            {
                path.parent.relative_to(THIS_DIR / "tests").as_posix()
                for path in (THIS_DIR / "tests").rglob("native_sim.keymap")
            },
            {*cases, "active-update"},
            "Every native fixture needs an explicit canonical owner entry",
        )
        for test_case in cases:
            test_build_dir = self.BUILD_DIR / THIS_DIR.name / test_case

            self._run_zmk_test_case(test_case, test_build_dir)

    @unittest.skipUnless(platform.system() == "Linux", "native_sim requires Linux")
    def test_active_updates(self):
        result = subprocess.run(
            [
                "bash",
                str(THIS_DIR / "tests" / "active-update" / "run.sh"),
                str(self.WEST_TOPDIR),
            ],
            env={
                **os.environ,
                "COMBO_NATIVE_ARTIFACTS": os.environ.get(
                    "COMBO_NATIVE_ARTIFACTS", str(self.BUILD_DIR / "active-update")
                ),
            },
            capture_output=True,
            text=True,
            cwd=self.WEST_CWD,
        )
        (self.BUILD_DIR / "test_active_updates.log").write_text(
            result.stdout + result.stderr, encoding="utf-8"
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipUnless(platform.system() == "Linux", "host build requires Linux")
    def test_rpc_lock(self):
        result = subprocess.run(
            ["python3", str(THIS_DIR / "tests" / "rpc-lock" / "run.py")],
            capture_output=True,
            text=True,
            cwd=self.WEST_CWD,
        )
        (self.BUILD_DIR / "test_rpc_lock.log").write_text(
            result.stdout + result.stderr, encoding="utf-8"
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipUnless(
        platform.system() == "Linux", "production DTS compile requires Linux"
    )
    def test_dt_defaults_validation(self):
        result = subprocess.run(
            ["python3", str(THIS_DIR / "tests/dt-defaults/run.py")],
            env={
                **os.environ,
                "COMBO_DT_ARTIFACTS": str(self.BUILD_DIR / "dt-defaults"),
            },
            capture_output=True,
            text=True,
            cwd=self.WEST_CWD,
        )
        (self.BUILD_DIR / "test_dt_defaults_validation.log").write_text(
            result.stdout + result.stderr, encoding="utf-8"
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("runtime combo DTS defaults cases=23 failures=0", result.stdout)

    def test_zmk_build(self):
        self._test_zmk_build(
            {
                "runtime_combo_board_feature_disabled": ConfigAndDeviceTree(
                    config=[
                        'CONFIG_ZMK_KEYBOARD_NAME="Module Test"',
                        "CONFIG_ZMK_USB=y",
                        "CONFIG_ZMK_BLE=y",
                        "# CONFIG_ZMK_RUNTIME_COMBO is not set",
                    ],
                    device=[
                        "DT_COMPAT_HAS_OKAY_zmk_keymap",
                    ],
                ),
                "runtime_combo_board_with_rpc": ConfigAndDeviceTree(
                    config=[
                        "CONFIG_ZMK_STUDIO=y",
                        "CONFIG_ZMK_RUNTIME_COMBO=y",
                        "CONFIG_ZMK_RUNTIME_COMBO_STUDIO_RPC=y",
                        "CONFIG_ZMK_BEHAVIOR_METADATA=y",
                        "CONFIG_ZMK_CUSTOM_SETTINGS=y",
                        "CONFIG_ZMK_CUSTOM_SETTINGS_STUDIO_RPC=y",
                    ],
                    device=[],
                ),
                "runtime_combo_board_without_rpc": ConfigAndDeviceTree(
                    config=[
                        "CONFIG_ZMK_RUNTIME_COMBO=y",
                        "# CONFIG_ZMK_STUDIO is not set",
                        "CONFIG_ZMK_BEHAVIOR_METADATA=y",
                        NotFound("CONFIG_ZMK_RUNTIME_COMBO_STUDIO_RPC"),
                    ],
                    device=[],
                ),
                "custom_settings_board": ConfigAndDeviceTree(
                    config=[
                        # Verify that zmk-feature-custom-settings is present and enabled
                        "zmk-feature-custom-settings",
                        "CONFIG_ZMK_STUDIO=y",
                        "CONFIG_ZMK_RUNTIME_COMBO=y",
                        "CONFIG_ZMK_RUNTIME_COMBO_STUDIO_RPC=y",
                        "CONFIG_ZMK_BEHAVIOR_METADATA=y",
                        "CONFIG_ZMK_CUSTOM_SETTINGS=y",
                        "CONFIG_ZMK_CUSTOM_SETTINGS_STUDIO_RPC=y",
                        "CONFIG_ZMK_STUDIO_RPC_RX_BUF_SIZE=128",
                        "CONFIG_ZMK_LOW_PRIORITY_THREAD_STACK_SIZE=2048",
                    ],
                    device=[],
                ),
            }
        )

    def _test_zmk_build(
        self, artifacts_and_expected_build_params: dict[str, ConfigAndDeviceTree]
    ):

        zephyr_cache_dir = self.BUILD_DIR / ".zephyr-cache"
        toolchain_cache_dir = zephyr_cache_dir / "ToolchainCapabilityDatabase"
        toolchain_cache_dir.mkdir(parents=True, exist_ok=True)

        result = run_west(
            [
                "zmk-build",
                str(THIS_DIR / "tests" / "zmk-config"),
                "-q",
                "-P",
                "1",
                "-m",
                str(THIS_DIR),
                str(self.CUSTOM_SETTINGS_MODULE),
                "-d",
                str(self.BUILD_DIR),
                "--cmake-args",
                (
                    f"-DUSER_CACHE_DIR={zephyr_cache_dir} "
                    f"-DZEPHYR_TOOLCHAIN_CAPABILITY_CACHE_DIR={toolchain_cache_dir}"
                ),
            ],
            cwd=self.WEST_CWD,
        )
        self.assertEqual(
            result.returncode,
            0,
            f"zmk-build failed\n{result.stdout}{result.stderr}",
        )

        for artifact, entries in artifacts_and_expected_build_params.items():
            artifact_dir = self.BUILD_DIR / artifact / "zephyr"
            config_path = artifact_dir / ".config"
            device_tree_path = (
                artifact_dir
                / "include"
                / "generated"
                / "zephyr"
                / "devicetree_generated.h"
            )
            self._test_strings_in_file(
                config_path, entries.config, f"{artifact} config"
            )
            if entries.device:
                self._test_strings_in_file(
                    device_tree_path, entries.device, f"{artifact} device tree"
                )
            self.assertTrue(
                (artifact_dir / "zmk.uf2").exists(),
                f"{artifact} zmk.uf2 is missing in {artifact_dir}",
            )

    def _run_zmk_test_case(self, test_case: str, build_dir: Path):
        test_path = THIS_DIR / "tests" / test_case
        case_build_dir = build_dir / "tests" / test_case
        case_build_dir.mkdir(parents=True, exist_ok=True)
        extra_modules = f"{THIS_DIR};{self.CUSTOM_SETTINGS_MODULE}"

        build_result = run_west(
            [
                "build",
                "-s",
                str(self.ZMK_APP),
                "-d",
                str(case_build_dir),
                "-b",
                "native_sim//zmk_test_mock",
                "-p",
                "--",
                "-DCONFIG_ASSERT=y",
                f"-DZMK_CONFIG={test_path}",
                f"-DZMK_EXTRA_MODULES={extra_modules}",
            ],
            cwd=self.WEST_CWD,
        )
        build_log = case_build_dir / "build.log"
        build_log.write_text(build_result.stdout + build_result.stderr)
        self.assertEqual(
            build_result.returncode,
            0,
            f"{test_case} did not build\n{self._zmk_test_logs(build_dir, test_case)}",
        )

        run_result = subprocess.run(
            [str(case_build_dir / "zephyr" / "zmk.exe")],
            capture_output=True,
            text=True,
            cwd=THIS_DIR,
        )
        full_log = "".join(
            re.sub(r".*> ", "", line)
            for line in (run_result.stdout + run_result.stderr).splitlines(True)
        )
        full_log_path = case_build_dir / "keycode_events_full.log"
        full_log_path.write_text(full_log)
        self.assertEqual(
            run_result.returncode,
            0,
            f"{test_case} executable failed\n{self._zmk_test_logs(build_dir, test_case)}",
        )

        patterns = test_path / "events.patterns"
        filtered = subprocess.run(
            ["sed", "-n", "-f", str(patterns)],
            input=full_log,
            capture_output=True,
            text=True,
        )
        keycode_log_path = case_build_dir / "keycode_events.log"
        keycode_log_path.write_text(filtered.stdout)
        self.assertEqual(
            filtered.returncode,
            0,
            f"{test_case} event filtering failed\n{filtered.stderr}",
        )

        verify_native(build_dir, THIS_DIR / "tests", fixtures=[test_case])

        expected = (test_path / "keycode_events.snapshot").read_text()
        self.assertEqual(
            self._normalize_snapshot(expected),
            self._normalize_snapshot(filtered.stdout),
            f"{test_case} snapshot mismatch\n{self._zmk_test_logs(build_dir, test_case)}",
        )

    def _normalize_snapshot(self, text: str) -> list[str]:
        return [line.rstrip() for line in text.splitlines()]

    def _test_strings_in_file(
        self, file_path: Path, expected_strings: list[str | NotFound], hint: str
    ):
        self.assertTrue(file_path.exists(), f"{hint}: {file_path} is missing")
        file_text = file_path.read_text()

        for expected in expected_strings:
            if isinstance(expected, NotFound):
                if expected.text in file_text:
                    self.fail(
                        f"{hint}: {expected.text} found in {file_path}, but it should not be present"
                    )
            else:
                if expected not in file_text:
                    self.fail(f"{hint}: {expected} not found in {file_path}")

    def _zmk_test_logs(self, build_dir: Path, test_case: str) -> str:
        log_dir = build_dir / "tests" / test_case
        logs = []
        for log_name in ("build.log", "keycode_events_full.log", "keycode_events.log"):
            log_path = log_dir / log_name
            if log_path.exists():
                logs.append(f"\n--- {log_path} ---\n{log_path.read_text()}")
        return "".join(logs)


if __name__ == "__main__":
    unittest.main()
