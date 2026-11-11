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

Each run also rejects actual sanitizer report syntax, including diagnostics
from a recovering sanitizer with exit0 and all complete group markers. This
uses the same `tests/sanitizer_diagnostics.py` admission check as the snapshot
verifier. Ordinary setting labels that merely mention sanitizers or
`runtime error:` without a source location are not reports. Selected dependency
diagnostics remain a separate sanitizer-clean gate even when every owner group
finishes; they do not authorize dependency edits or dropping required features.

Before the fix, changing a held B binding to C produced B-down followed by
C-up; B stayed held. The original source failed identically in three runs.

A Studio edit only marks its slot (or the global settings) stale; the next key
event rebuilds what is stale on the matching thread. The end of the boot load
marks everything stale; change events mark the affected body/binding slot or
global state. Display names do not invalidate matching state. Each of these
is needed: dropping the end-of-load or matching change-event mark,
the explicit mark of a combo write, a reset or a global setting write (only
these cover a write whose persistence fails, which raises no change event),
skipping the rebuild, or firing a held-back combo from its edited slot fails
this test. The groups that check a load, a failed global write or a foreign
write use the read-only default combo, so no write of their own marks the
cache. These original groups run on one thread.

The original twelve active-lifetime groups remain unchanged. Ten additional
pending groups exercise layer and prior-idle eligibility (including completion
after the idle interval), an excluded-slot control, early/late superset
completion, subset expiry and an unrelated key, and moving a pending deadline
later when the shortest candidate is filtered out. The timer control starts
with that shortest candidate disabled. Positions and timeout callbacks use the
actual system work queue; every group checks HID state and final release. The
canonical runner requires both complete group markers in all three runs.
`COMBO_PENDING_CASE` selects one named pending group only for a focused control;
it cannot satisfy the canonical complete-run admission.

Nine review groups retain independent physical positions with distinct ordinary
keycodes, so a late press cannot hide behind another key's identical HID usage.
They cover `replay-release`, `replay-unrelated`, `replay-control`,
`name-same-slot`, `name-other-slot`, `full-active-table`, `paired-update`,
`stored-binding` and `name-bounds`. The paired-update group pauses the real
writer in its body-change event, after the registry guard is released and before
the binding write. A second thread reads the pair while system-queue key events
continue against the previous cache; after the writer completes, both the reader
and the next activation must use the complete new pair. The stored-binding group
uses the real settings loader with layer parameters 2, 256, 257 and UINT32_MAX,
plus a valid layer-1 record, and verifies actual layer state and releases. The
record uses the registry's packed local-ID/uint32/uint32 storage format. A raw
registry read must return the exact injected ID and full-width parameters before
the combo reader validates them. The first invalid value2 is below the fixture's
34-position count but outside its two-layer domain. Each case also checks direct
module-write refusal without changing the preceding valid layer-1 binding.

`COMBO_REVIEW_CASE` selects one review group for before-source controls. Restore
the production source from the pre-repair working snapshot while retaining these
fixtures, current public declarations and metadata configuration. Replay groups,
name groups, full-table, paired-update, stored-binding and name-bounds must fail
their corresponding assertions before the repair; `replay-control` remains a
positive control. Run each selected binary three times from fresh state. Focused
selection cannot replace the complete canonical markers.

Two further groups retain the ordinary 2048-byte system workqueue stack and
the configured maximum of16 positions. `replay-max` resolves15 pending keys
from a16-position candidate and verifies every downstream keycode press/release
pair, first-accepted HID state and final clearing. `replay-reentrant` raises15
new physical-position events synchronously from the first replayed E keycode.
Those events qualify for another16-position candidate. They must not overwrite
the immutable original tail or lose a press/release during nested replay. The
first modifier press also releases a separately held combo, checking its original
binding's releases during nested dispatch. Repeated modifier usages deliberately
test counts at the keycode boundary without exceeding the fixture's original
HID rollover limit. The original distinct E/F/G assertions remain. The canonical
runner additionally requires `runtime combo replay: PASS groups=2` in every run.

The production implementation copies the pending presses or unused tail into
a local array before synchronous dispatch. Each pending record retains its
full-width position, source, original timestamp and listener-resume index.
The event wrapper and pressed state are reconstructed at dispatch; each ARM
record and immutable snapshot element is16 bytes rather than32. Each snapshot
is bounded by the configured maximum positions. New candidates may reuse the
mutable pending array without replacing a local replay tail. The generation counter retains
the triggering input's decision to revisit the listener chain after replay;
upstream listeners and active-combo releases retain their ordinary ordering.
The complete fixture keeps the ordinary 2048-byte system workqueue stack and
checks both the maximum-length and reentrant cases without a separate replay
queue or overload policy.

Held active entries retain the captured binding, release policy and full-width
position identities, rather than complete events whose headers/state and
per-key times are unused after activation. Both activation paths capture the
original first press time before resetting/reusing the pending state. Two
additional `active-time`/`subset-time` groups require that exact downstream
press time and the actual incoming release time; the canonical runner requires
`runtime combo active storage: PASS groups=2` as well as every original marker.
`apply_active_timestamp_fault.py ISOLATED_OWNER` selects the last press time in
both activation paths. Each focused group must reject that named time assertion
in three runs of the unchanged control binary; build errors/timeouts do not
qualify. The active representation and these checks retain the pending replay
arrays, stack sizes and input limits. These cases do not establish arbitrary
nested-replay stack acceptance.

For the mutable-tail negative control, invoke the unchanged helper from a
read-only owner source against a distinct disposable container staging copy:

```sh
python3 "$OWNER_SOURCE/tests/active-update/apply_replay_fault.py" "$STAGED_OWNER"
```

Build that copy once and run `COMBO_REVIEW_CASE=replay-reentrant` three times
with fresh initial state. Require a failure of the immutable-tail, press-before-
release or accepted-event assertion in each run. Compilation errors, unrelated
failures, crashes and timeouts do not establish detection. The helper replaces
only the immutable local copy with an alias of `pending_keys`, retaining the
actual generation logic. E-triggered positions can then overwrite the unprocessed
tail. It does not alter an assertion or the configured workqueue stack.

The fixture's matrix and both keymap layers cover all34 positions. Compile-time
and runtime bounds checks reject a test position outside that matrix. Earlier
out-of-range fixture events caused invalid keymap access; those crashes do not
establish a production replay or stack defect. The corrected fixture preserves
the original positions0..3 and every original group/assertion. Its maximum and
reentrant checks apply equally to the simple local-snapshot implementation;
they do not require a continuation pump. New complete runs and an executable
mutable-tail counterexample remain required for the final source snapshot.

These regressions do not establish physical USB/BLE delivery, split timing or
transport congestion behavior.

The `release-reentrant` group starts two independently held combos. The final
slow-release callback of the second synchronously releases the first and
activates a replacement. The replacement must still release its original
binding. The original implementation fails the named replacement assertion
in three runs of one binary because it removes the old active-array index
after that index has been reused. The repaired path completes its table
mutation and retains only the captured binding and slot across dispatch.
The runner requires `runtime combo release reentry: PASS groups=1` in addition
to every earlier marker.

`binding-tap` and `binding-hold` use the actual core Hold Tap behavior, its
metadata, event capture and timer. A runtime combo retains that behavior
through a slot edit until its tap or held modifier releases; the next press
uses the new binding. The runner also requires
`runtime combo hold-tap binding: PASS groups=2`. These selected bindings and
controlled reentry do not establish a maximum for arbitrary external recursion.
