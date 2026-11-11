import unittest

from arm_memory import input_allocation, verify_active_storage


class ArmMemoryContract(unittest.TestCase):
    def test_actual_active_layout_and_original_budget_control(self):
        verify_active_storage(352, 16, 4)
        with self.assertRaisesRegex(AssertionError, "active storage budget exceeded"):
            verify_active_storage(2144, 16, 4)

    def test_wrapped_owner_allocation_ignores_discarded_other_object(self):
        text = """RAM 0x20000000 0x40000 xrw
 .bss.active_combos 0x0 0x860 app/libapp.a(runtime_combo.c.obj)
 .bss.active_combos 0x20000000 0x20 app/libapp.a(other.c.obj)
 .bss.active_combos
  0x20000020 0x160 app/libapp.a(runtime_combo.c.obj)
"""
        self.assertEqual(
            input_allocation(text, "active_combos", "runtime_combo.c.obj"),
            {"address": 0x20000020, "bytes": 352},
        )

    def test_missing_regions_duplicate_and_outside_ram_fail_closed(self):
        row = " .bss.active_combos 0x20000000 0x160 app/libapp.a(runtime_combo.c.obj)\n"
        for text in [
            row,
            "RAM 0x20000000 0x40000 xrw\n" + row + row,
            "RAM 0x20000000 0x100 xrw\n" + row,
        ]:
            with self.subTest(text=text), self.assertRaises(AssertionError):
                input_allocation(text, "active_combos", "runtime_combo.c.obj")
