import unittest

from arm_memory import (
    input_allocation,
    verify_active_storage,
    verify_pending_storage,
    verify_cache_storage,
)


class ArmMemoryContract(unittest.TestCase):
    def test_pending_layout_and_original_event_wrapper_control(self):
        verify_pending_storage(256, 16)
        with self.assertRaisesRegex(AssertionError, "pending storage budget exceeded"):
            verify_pending_storage(512, 16)

    def test_actual_active_layout_and_original_budget_control(self):
        verify_active_storage(336, 16, 4)
        with self.assertRaisesRegex(AssertionError, "active storage budget exceeded"):
            verify_active_storage(352, 16, 4)
        with self.assertRaisesRegex(AssertionError, "active storage budget exceeded"):
            verify_active_storage(2144, 16, 4)

    def test_cache_layout_and_original_enum_storage_control(self):
        for slots in (8, 16):
            for positions, current, original in (
                (2, 32, 36),
                (3, 36, 40),
                (16, 60, 64),
            ):
                verify_cache_storage(current * slots, positions, slots)
                with self.assertRaisesRegex(
                    AssertionError, "cache storage budget exceeded"
                ):
                    verify_cache_storage(original * slots, positions, slots)

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
