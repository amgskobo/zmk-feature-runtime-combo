"""Use real synthetic ELF programs to check native-result admission."""

import json
import os
import hashlib
from pathlib import Path
import subprocess
import tempfile
import time
import unittest
from unittest.mock import patch

from native_verifier import verify_native


class NativeVerifierContract(unittest.TestCase):
    def setUp(self):
        retained = os.environ.get("NATIVE_VERIFIER_CONTROL_OUTPUT")
        if retained:
            self.root = Path(retained).resolve() / self._testMethodName
            self.root.mkdir(parents=True, exist_ok=False)
        else:
            self.temp = tempfile.TemporaryDirectory()
            self.addCleanup(self.temp.cleanup)
            self.root = Path(self.temp.name)
        self.tests = self.root / "fixtures"
        fixture = self.tests / "case"
        fixture.mkdir(parents=True)
        (fixture / "native_sim.keymap").write_text(
            "/* Synthetic native fixture marker. */\n"
        )
        (fixture / "native_sim.conf").write_text("CONFIG_OWNER_TEST=y\n")
        (fixture / "events.patterns").write_text("/^EVENT /p\n")
        (fixture / "keycode_events.snapshot").write_text("EVENT 1\n")
        (self.tests / "required_hooks.json").write_text(
            json.dumps([{"configs": ["CONFIG_OWNER_TEST"], "hook": "control"}])
        )
        self.output = self.root / "build"
        binary_dir = self.output / "case" / "zephyr"
        binary_dir.mkdir(parents=True)
        (binary_dir / ".config").write_text("CONFIG_OWNER_TEST=y\n")
        self.binary = binary_dir / "zmk.exe"

    def compile(
        self,
        *,
        hook=True,
        reported=0,
        process_exit=0,
        fatal=False,
        failed_log=False,
        sanitizer=None,
        fault=False,
        labels=False,
    ):
        source = self.root / "program.c"
        body = "#include <stdio.h>\n#include <stdlib.h>\n#include <limits.h>\n"
        if hook:
            body += (
                "int owner_checked_control(void) { "
                f'puts("OWNER_TEST_RESULT control={reported}"); return 0; }}\n'
            )
        body += "int main(void) { "
        if hook:
            body += "owner_checked_control(); "
        if fatal:
            body += 'puts("ASSERTION FAIL"); '
        if failed_log:
            body += 'puts("<err> owner: FAIL: controlled assertion"); '
        if labels:
            body += 'puts("CONFIG_ASAN=y; AddressSanitizer instrumentation"); '
            body += 'puts("runtime error: label for a synthetic setting"); '
        flags = []
        if sanitizer == "address":
            flags = ["-fsanitize=address", "-fsanitize-recover=address"]
            body += "volatile int *value = malloc(sizeof(*value)); "
            body += f"volatile int index = {1 if fault else 0}; value[index] = 7; "
            body += "free((void *)value); "
        elif sanitizer == "undefined":
            flags = ["-fsanitize=undefined", "-fsanitize-recover=all"]
            body += f"volatile int value = {('INT_MAX' if fault else '0')}; "
            body += "value += 1; "
        body += f'puts("EVENT 1"); return {process_exit}; }}\n'
        source.write_text(body)
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-O1",
                "-g",
                "-fno-omit-frame-pointer",
                "-no-pie",
                *flags,
                str(source),
                "-o",
                str(self.binary),
            ],
            check=True,
        )

    def recovering_sanitizer(self, sanitizer):
        self.compile(sanitizer=sanitizer, fault=True)
        options = {
            "ASAN_OPTIONS": "detect_leaks=0:halt_on_error=0:log_path=stdout",
            "UBSAN_OPTIONS": "halt_on_error=0:print_stacktrace=1",
        }
        with patch.dict(os.environ, options):
            started = time.time_ns()
            result = subprocess.run([str(self.binary)], capture_output=True, text=True)
            (self.root / "probe-stdout.log").write_text(result.stdout)
            (self.root / "probe-stderr.log").write_text(result.stderr)
            (self.root / "probe-command.json").write_text(
                json.dumps(
                    {
                        "argv": [str(self.binary)],
                        "exit": result.returncode,
                        "start_ns": started,
                        "end_ns": time.time_ns(),
                        "environment": options,
                        "binary_sha256": hashlib.sha256(
                            self.binary.read_bytes()
                        ).hexdigest(),
                    },
                    indent=2,
                )
                + "\n"
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("OWNER_TEST_RESULT control=0", result.stdout)
            self.assertIn("EVENT 1", result.stdout)
            if sanitizer == "address":
                self.assertIn(
                    "ERROR: AddressSanitizer: heap-buffer-overflow", result.stdout
                )
                self.assertEqual(result.stderr, "")
            else:
                self.assertRegex(result.stderr, r"program\.c:\d+:\d+: runtime error:")
                self.assertNotIn("runtime error:", result.stdout)
            with self.assertRaisesRegex(AssertionError, "sanitizer diagnostic"):
                verify_native(self.output, self.tests)

    def test_recovering_asan_stdout_is_not_snapshot_success(self):
        self.recovering_sanitizer("address")

    def test_recovering_ubsan_stderr_is_not_snapshot_success(self):
        self.recovering_sanitizer("undefined")

    def test_clean_asan_binary_remains_snapshot_success(self):
        self.compile(sanitizer="address")
        with patch.dict(os.environ, {"ASAN_OPTIONS": "detect_leaks=0:halt_on_error=0"}):
            verify_native(self.output, self.tests)

    def test_clean_ubsan_binary_remains_snapshot_success(self):
        self.compile(sanitizer="undefined")
        verify_native(self.output, self.tests)

    def test_sanitizer_configuration_and_locationless_labels_are_not_reports(self):
        self.compile(labels=True)
        verify_native(self.output, self.tests)

    def test_positive_repeats_preserve_raw_output(self):
        self.compile()
        verify_native(self.output, self.tests)
        runs = self.binary.parent.parent / "verified-runs"
        self.assertEqual(
            sorted(path.name for path in runs.iterdir() if path.is_dir()),
            ["1", "2", "3"],
        )
        for trial in range(1, 4):
            self.assertEqual(
                (runs / str(trial) / "events.log").read_text(), "EVENT 1\n"
            )
            self.assertIn(
                "OWNER_TEST_RESULT control=0",
                (runs / str(trial) / "raw.log").read_text(),
            )

    def test_missing_checked_init_is_not_snapshot_success(self):
        self.compile(hook=False)
        with self.assertRaisesRegex(AssertionError, "missing checked init"):
            verify_native(self.output, self.tests)

    def test_failed_init_is_not_snapshot_success(self):
        self.compile(reported=-22)
        with self.assertRaisesRegex(AssertionError, "completion"):
            verify_native(self.output, self.tests)

    def test_real_process_exit_is_required(self):
        self.compile(process_exit=7)
        with self.assertRaisesRegex(AssertionError, "native exit 7"):
            verify_native(self.output, self.tests)

    def test_requested_test_cannot_be_disabled_in_build(self):
        self.compile()
        (self.binary.parent / ".config").write_text("# CONFIG_OWNER_TEST is not set\n")
        with self.assertRaisesRegex(AssertionError, "required configuration"):
            verify_native(self.output, self.tests)

    def test_logged_assertion_cannot_pass_with_zero_init_result(self):
        self.compile(failed_log=True)
        with self.assertRaisesRegex(AssertionError, "failed owner assertion"):
            verify_native(self.output, self.tests)

    def test_fixture_requires_its_test_even_when_both_configs_omit_it(self):
        self.compile()
        (self.tests / "required_hooks.json").write_text(
            json.dumps(
                [
                    {
                        "fixtures": ["case"],
                        "configs": ["CONFIG_OWNER_TEST"],
                        "hook": "control",
                    }
                ]
            )
        )
        (self.tests / "case/native_sim.conf").write_text("")
        (self.binary.parent / ".config").write_text("")
        with self.assertRaisesRegex(AssertionError, "required configuration"):
            verify_native(self.output, self.tests)

    def test_fixture_role_cannot_be_replaced_by_a_wrong_built_role(self):
        self.compile()
        (self.tests / "required_hooks.json").write_text(
            json.dumps(
                [
                    {
                        "fixtures": ["case"],
                        "configs": ["CONFIG_OWNER_TEST"],
                        "not_configs": ["CONFIG_ROLE_CENTRAL"],
                        "hook": "control",
                    }
                ]
            )
        )
        (self.binary.parent / ".config").write_text(
            "CONFIG_OWNER_TEST=y\nCONFIG_ROLE_CENTRAL=y\n"
        )
        with self.assertRaisesRegex(AssertionError, "required configuration"):
            verify_native(self.output, self.tests)

    def test_unrelated_fixture_declaration_does_not_require_a_hook(self):
        self.compile(hook=False)
        (self.tests / "required_hooks.json").write_text(
            json.dumps(
                [
                    {
                        "fixtures": ["other"],
                        "configs": ["CONFIG_OWNER_TEST"],
                        "hook": "control",
                    }
                ]
            )
        )
        verify_native(self.output, self.tests)

    def test_missing_native_fixture_cannot_be_a_partial_success(self):
        self.compile()
        other = self.tests / "missing-case"
        other.mkdir()
        (other / "native_sim.keymap").write_text("/* A required second scenario. */\n")
        with self.assertRaisesRegex(AssertionError, "missing native fixtures"):
            verify_native(self.output, self.tests)

    def test_fatal_with_zero_exit_is_rejected(self):
        self.compile(fatal=True)
        with self.assertRaisesRegex(AssertionError, "fatal assertion"):
            verify_native(self.output, self.tests)

    def test_explicit_partition_requires_every_selected_fixture(self):
        self.compile()
        other = self.tests / "missing-case"
        other.mkdir()
        (other / "native_sim.keymap").write_text("/* Required selected case. */\n")
        with self.assertRaisesRegex(AssertionError, "missing native fixtures"):
            verify_native(self.output, self.tests, fixtures=["case", "missing-case"])

    def test_explicit_partition_cannot_name_an_unknown_fixture(self):
        self.compile()
        with self.assertRaisesRegex(AssertionError, "Invalid explicit"):
            verify_native(self.output, self.tests, fixtures=["unknown"])

    def test_explicit_partition_preserves_all_three_real_runs(self):
        self.compile()
        other = self.tests / "other-entry"
        other.mkdir()
        (other / "native_sim.keymap").write_text("/* Separate canonical entry. */\n")
        verify_native(self.output, self.tests, fixtures=["case"])
        self.assertEqual(
            {
                path.name
                for path in (self.binary.parent.parent / "verified-runs").iterdir()
                if path.is_dir()
            },
            {"1", "2", "3"},
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
