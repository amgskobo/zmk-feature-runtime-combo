# Held combo updates

From a resolved workspace containing this owner, ZMK and Custom Settings:

```sh
bash tests/active-update/run.sh "$(west topdir)"
```

The runner builds `native_sim//zmk_test_mock` once, then runs the identical
binary three times. Twelve groups exercise settings loaded at boot, editing a
held combo's behavior and positions count, successful reset, reset and combo
writes with a storage error, per-combo and global slow-release changes (one
with a storage error, one written past this module's API), disabling/deleting
the slot, and disabling a combo that is held back for a longer one. The fixture
asserts actual ZMK HID state after each original release and checks that a
subsequent activation uses the new binding, or, after a reset or disable, that
the positions type their own keys again. It uses the production combo code,
actual ZMK event manager, Custom Settings and Zephyr settings dispatch.
The fixture source uses the application target's public headers directly;
unrelated input modules are not needed to supply global include paths.

A registered test settings backend returns a controlled success or `-EIO`.
This tests the owner's existing RAM-before-storage contract without asserting
physical flash behavior. The original held binding must be released even when
reset has changed RAM but persistence fails. The same backend supplies a boot
record (global slow release on) and raises a key press inside its load
callback, before that record is applied, as when the system work queue
preempts `main()` during `settings_load()`. Kernel scheduling, rather than
host mocks, orders the position events. Raw timestamps and report order are
retained; there are no accepted or rewritten snapshots.

Run this owner command against its resolved DYA workspace. Stock ZMK lacks
this module's Custom Settings/RPC dependency and is NOT APPLICABLE.
`COMBO_NATIVE_ARTIFACTS=/absolute/output` retains build/configuration evidence,
the executable and all three raw logs. With an explicit `ZEPHYR_MODULES`, the
runner appends only its fixture module. No production feature is disabled to
clear a production build gate.

Before the fix, changing a held B binding to C produced B-down followed by
C-up; B stayed held. The original source failed identically in three runs.

A Studio edit only marks its slot (or the global settings) stale; the next key
event rebuilds what is stale on the matching thread. The end of the boot load
and every custom settings change event of this subsystem mark everything
stale. Each of these is needed: dropping the end-of-load or change-event mark,
the explicit mark of a combo write, a reset or a global setting write (only
these cover a write whose persistence fails, which raises no change event),
skipping the rebuild, or firing a held-back combo from its edited slot fails
this test. The groups that check a load, a failed global write or a foreign
write use the read-only default combo, so no write of their own marks the
cache. The test runs on one thread, so it does not race a write against a key
event.

This regression covers active combo lifetime. Of pending candidates it covers
only a held-back combo whose slot is disabled meanwhile; it does not establish
physical USB/BLE delivery, split timing or transport congestion behavior.
