"""Independent rejection controls for the ordered ARM stack observation."""

import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location(
    "combo_stack_entry", Path(__file__).parent / "stack-budget/run.py"
)
ENTRY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ENTRY)


def trace():
    times = [
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
        102183,
    ]
    rows = []
    for case, offset, used, span in ((1, 0, 1292, 1176), (8, 1000000, 6052, 5936)):
        keys = list(range(8, 23)) + [4]
        for i in range(32):
            j = i % 16
            at = times[j] if i < 16 else (102220 if j == 15 else 102200 + j)
            rows.append(
                f"COMBO_STACK_EVENT case={case} index={i} key={keys[j]} state={int(i < 16)} time={at + offset}"
            )
        rows.append(
            f"COMBO_STACK case={case} configured=8192 usable=8192 unused={8192 - used} used={used} replay_span={span} reports=32"
        )
    return "\n".join(rows + [ENTRY.TERMINAL, ""])


class StackBudgetValidation(unittest.TestCase):
    def test_valid_trace_and_named_original_control(self):
        result = ENTRY.validate(trace())
        self.assertEqual(result["eight_candidates"]["reports"], 32)
        self.assertEqual(
            result["original4096_control"], "EXPECTED FAIL: COMBO_ARM_STACK_BUDGET"
        )
        ENTRY.validate(trace().replace("\n", "\r\n"))

    def test_every_incomplete_terminal_prefix_is_rejected(self):
        prefix = trace().split(ENTRY.TERMINAL)[0]
        for length in range(len(ENTRY.TERMINAL) + 1):
            with (
                self.subTest(length=length),
                self.assertRaisesRegex(AssertionError, "COMBO_ARM_COMPLETION"),
            ):
                ENTRY.validate(prefix + ENTRY.TERMINAL[:length])

    def test_order_time_missing_duplicate_and_release_controls(self):
        lines = trace().splitlines(keepends=True)
        cases = [
            trace().replace(
                "index=0 key=8 state=1 time=100000",
                "index=0 key=8 state=0 time=100000",
                1,
            ),
            trace().replace("time=100000\n", "time=100001\n", 1),
            "".join(lines[1:]),
            "".join(lines[:1] + lines),
            "".join([lines[1], lines[0], *lines[2:]]),
            trace().replace("index=31 key=4 state=0", "index=31 key=4 state=1", 1),
        ]
        for case in cases:
            with (
                self.subTest(case=case[:80]),
                self.assertRaisesRegex(AssertionError, "COMBO_ARM_ORDER_TIME"),
            ):
                ENTRY.validate(case)

    def test_missing_duplicate_and_inconsistent_metrics_rejected(self):
        line = "COMBO_STACK case=8 configured=8192 usable=8192 unused=2140 used=6052 replay_span=5936 reports=32\n"
        for case in (
            trace().replace(line, ""),
            trace().replace(line, line + line),
            trace().replace("unused=2140", "unused=2141"),
            trace().replace("case=8 configured=8192", "case=7 configured=8192"),
        ):
            with self.assertRaisesRegex(AssertionError, "COMBO_ARM_SAMPLES"):
                ENTRY.validate(case)

    def test_headroom_and_original_reproduction_are_required(self):
        for before, after, error in (
            (
                "unused=2140 used=6052 replay_span=5936",
                "unused=100 used=8092 replay_span=5936",
                "STACK_HEADROOM",
            ),
            (
                "unused=2140 used=6052 replay_span=5936",
                "unused=4096 used=4096 replay_span=4000",
                "ORIGINAL_CONTROL",
            ),
            (
                "unused=6900 used=1292 replay_span=1176",
                "unused=4000 used=4192 replay_span=4000",
                "SMALL_CONTROL",
            ),
        ):
            with self.assertRaisesRegex(AssertionError, "COMBO_ARM_" + error):
                ENTRY.validate(trace().replace(before, after))

    def test_diagnostics_cannot_be_hidden_by_complete_output(self):
        for diagnostic in (
            "ASSERTION FAIL",
            "FATAL ERROR",
            "Stack overflow",
            "runtime error:",
            "AddressSanitizer",
        ):
            with self.assertRaisesRegex(AssertionError, "COMBO_ARM_EXECUTION"):
                ENTRY.validate(trace() + diagnostic + "\n")

    def test_budget_boundary_has_only_the_named_assertion(self):
        ENTRY.assert_budget(3968, 4096, 128)
        ENTRY.assert_budget(6052, 8192, 128)
        for used, budget, reserve in (
            (3969, 4096, 128),
            (6052, 4096, 0),
            (8192, 8192, 1),
        ):
            with self.assertRaisesRegex(AssertionError, "^COMBO_ARM_STACK_BUDGET$"):
                ENTRY.assert_budget(used, budget, reserve)
