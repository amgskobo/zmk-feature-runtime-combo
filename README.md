# zmk-feature-runtime-combo

## Reviewed fork

This fork's reviewed source is on `codex/runtime-combo-review` in
[`amgskobo/zmk-feature-runtime-combo`](https://github.com/amgskobo/zmk-feature-runtime-combo/tree/codex/runtime-combo-review).
Select that repository URL and revision in the module's west project entry.
Keep the module's documented imports enabled where required. The original
upstream installation examples below describe the upstream repository.

The dependency manifests select the available Custom Settings revision
`c4071b095c3007d1deb055c609603ca9b5897d14` from its reviewed fork;
production imports and isolated tests use the same registry implementation.

![ZMK Version](https://img.shields.io/badge/ZMK-master-blue)
[![Test](https://github.com/cormoran/zmk-feature-runtime-combo/actions/workflows/zmk-module.yml/badge.svg?branch=main)](https://github.com/cormoran/zmk-feature-runtime-combo/actions/workflows/zmk-module.yml)

Runtime Combo is a ZMK module that adds combos which can be edited at runtime
through the unofficial custom ZMK Studio RPC protocol.

A combo watches multiple key positions. When all configured positions are pressed
within the combo timeout, the configured ZMK behavior is invoked. Each combo is
stored by numeric slot internally and can also have a display name for the Web UI.

## Features

- Runtime editable combo slots.
- An active combo retains its original behavior, positions count and release
  policy until release, even if its slot is edited, disabled or reset meanwhile.
- Compile-time default combos declared in the `.keymap`, active immediately
  after flashing and restorable after a runtime edit.
- Separate RPC methods for combo content and combo names to keep request payloads
  small.
- Compact binary storage for combo bodies.
- Global timeout, slow-release, and require-prior-idle settings shared by all
  runtime combos, with per-combo overrides for all three.
- Native-parity overlap resolution: if one combo's positions are a subset of
  another enabled combo's positions, the longer combo always wins.
- Up to 16 combo slots (`CONFIG_ZMK_RUNTIME_COMBO_MAX_COMBOS`, default 8;
  the Studio list stores every configured slot within the protocol's16-slot
  limit), decoded into a RAM cache so the key-event hot
  path never touches the settings registry. An edit marks its slot stale and
  the next key event rebuilds it, so the cache is only touched by the thread
  that matches combos. Settings loaded at boot and changes made past this
  module's API (the generic custom settings RPC) are picked up the same way.
- Like ZMK's own combos, runtime combos resolve on the split central (or a
  non-split keyboard); enable them in the central's configuration only.
- Name and combo arrays stored through
  [zmk-feature-custom-settings](https://github.com/cormoran/zmk-feature-custom-settings).
- React Web UI for listing and editing combo slots, with a clickable layout
  for picking positions and a searchable behavior list.

The Web editor checks each reply against the requested operation before applying
it or sending a follow-up name write. Reads require their matching response and
the global-settings read requires its settings submessage; mutations require a
status response. Empty, unknown, mismatched or mixed response envelopes show an
error and preserve the loaded catalog and drafts. An explicit empty combo list
and a default-valued status response remain valid. A blank server error has a
visible fallback message.

The three global-setting save buttons change one field each. Saving one keeps
the other unsent fields and the persistence choice. A refresh cannot replace a
field edited after that request began. See the [Web guide](web/README.md) for
explicit refresh and pending-settings behavior.

Combos and the global settings can be read while Studio is locked. Every change
(a combo, its name, delete, reset, the global timeout, slow release and
require-prior-idle settings, save and discard) needs an unlocked Studio,
because a combo can run any behavior, including the bootloader or clearing
bonds; a locked keyboard answers "Unlock required" and changes nothing.

## Storage Format

The combo body is saved as one packed byte-array custom setting per combo slot:

```text
version, flags, position_count, layer_mask, timeout_ms,
require_prior_idle_ms, positions[]
```

`positions[]` are 16-bit values. `timeout_ms` and `require_prior_idle_ms` are
per-combo overrides; `0` means inherit the corresponding global setting. The
`flags` byte also carries a tri-state slow-release override (inherit / on /
off).

The behavior binding is stored separately as a
[`BEHAVIOR`-typed custom setting](https://github.com/cormoran/zmk-feature-custom-settings)
array with the same index, so its behavior local ID and parameters are
validated against the target behavior's parameter metadata on write and exported
as a first-class behavior binding rather than opaque bytes. The name is likewise
saved as a string-array custom setting with the same index.

The module reads and applies a combo's body and binding as one operation.
Key events continue with the previous complete cached configuration while a
module edit or persistence operation is running. Generic Custom Settings
operations on individual arrays remain independent edits; clients needing a
paired body/binding change should use the runtime-combo operation. Stored
binding parameters are validated again before matching, including their full
width before a behavior can narrow them. Runtime Combo enables behavior
metadata for this validation. Layer-typed parameters must name a currently
available stable layer ID; the module checks this domain itself because the
selected core's metadata validator uses the key-position count as its bound.
Other parameter types retain the core's metadata validation.

Display-name changes leave pending combos intact. Names accept at most 63
UTF-8 bytes, matching the RPC string capacity; the Web editor checks this
before sending either the body or name mutation. This is a byte limit, so
multibyte names can reach it with fewer characters.

Timeout, slow-release, and require-prior-idle mode are saved as separate global
custom settings and apply to every runtime combo unless overridden. The global
settings RPC response also reports `max_combo`, the maximum number of combo
slots configured in the firmware.

## Compile-Time Default Combos

Default combos can be declared directly in the `.keymap` using a
`cormoran,runtime-combo-defaults` node, so they work immediately after
flashing and survive a settings erase:

```dts
/ {
    runtime_combo_defaults {
        compatible = "cormoran,runtime-combo-defaults";

        combo_esc {
            slot = <0>;             // optional; defaults to definition order
            key-positions = <0 1>;
            bindings = <&kp ESC>;
            layers = <0 1>;         // optional; omitted = all layers
            display-name = "Esc";   // optional; defaults to the node name
            timeout-ms = <30>;      // optional per-combo override
            require-prior-idle-ms = <125>; // optional per-combo override
            slow-release;           // optional; forces slow-release on
        };
    };
};
```

Each raw `key-positions` cell must be in `0..65535`; each `layers` cell must be
in `0..31`, the width of the runtime layer mask. Both timer overrides accept
`0..65535` ms, with zero or omission inheriting the global value. These limits
are checked at compile time before narrowing or bit shifts, including every
element of a list. The slot must fit the configured slot count, and the number
of positions must be between two and the configured maximum. The owner's
[production DTS boundary fixture](tests/dt-defaults/README.md) retains valid
boundaries and named compiler failures; ordinary defaults/RPC/native/board
checks remain required.

A slot's effective value follows this order:

1. **Runtime override** — if the slot has ever been written or explicitly
   disabled through the RPC, that value always wins (a disabled/deleted slot
   suppresses its default rather than falling back to it).
2. **Compile-time default** — used whenever the slot has no stored override.
3. **Empty** — a slot with neither a default nor a stored value.

The Web UI shows each slot's source (`Default`, `Overridden`, `Runtime`, or
`Empty`) as a badge, and a slot with a default shows a **Reset to Default**
button that erases the stored override and restores the compile-time values.

## Overlap Resolution

If pressing a set of keys exactly completes one combo, but a second enabled
combo also matches every currently pressed position and needs one or more
additional positions, the shorter combo does not fire immediately. It only
fires once no longer combo remains viable: because an unrelated key is pressed,
the timeout expires, or one of the pressed keys is released. Whichever combo
ends up firing always uses only its own positions; any positions pressed while
waiting on a longer candidate are replayed as normal key presses if that
candidate never completes.

If the active-combo table is full, an unactivated candidate replays every
captured key as ordinary input. Replayed presses and their releases retain
their order even when a replayed press starts another candidate.

Before replay dispatch, the pending presses or unused tail are copied into a
bounded local array. Reentrant position events can start another candidate
without overwriting that snapshot. A replay generation records when multiple
presses reenter matching, so the triggering release or unrelated press is
processed after them with the updated pending state. Replay preserves the core's
synchronous listener ordering and normal active-combo releases.

Replay can reenter matching and create another local snapshot before an outer
snapshot returns. Its stack cost therefore depends on the selected settings and
downstream listeners. For the eight-slot/16-position profile, the owner ARM
fixture reproduces a fixed input path that exceeds a 4096-byte system workqueue.
Use `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=8192` for that keyboard profile and
remeasure with its complete enabled modules and installed image. This reserves
4096 more static RAM bytes than a 4096-byte setting on the central; it creates
no dedicated Combo thread or heap. The pinned Cortex-M fixture observes 6052
bytes used and 2140 remaining for that path, with all ordered presses/releases,
and retains a small one-candidate control. This is a measured fixture path, not
a bound for every binding, 16-slot configuration or external synchronous
reentry. See [the stack fixture](tests/stack-budget/README.md) and preserve those
separate hardware/worst-case gates.

Each candidate keeps its own timeout window. Expiring a shorter window does
not end the wait for a longer candidate whose window is still open. If another
press eliminates the earliest candidate, the timer moves to the next surviving
deadline; held keys replay at that deadline even without another input.

## User Guide

1. Add the module to `config/west.yml`.

   ```yml
   manifest:
     remotes:
       - name: cormoran
         url-base: https://github.com/cormoran
     projects:
       - name: zmk-feature-runtime-combo
         remote: cormoran
         revision: main
         import: true
       - name: zmk
         remote: cormoran
         revision: main+custom-studio-protocol
         import:
           file: app/west.yml
   ```

   This module imports `zmk-feature-custom-settings`. The custom Studio RPC Web
   UI requires ZMK from cormoran's `main+custom-studio-protocol` branch.

2. Enable the feature in `config/<shield>.conf`.

   ```conf
   CONFIG_ZMK_RUNTIME_COMBO=y

   # Required for the custom Studio RPC Web UI
   CONFIG_ZMK_STUDIO=y
   CONFIG_ZMK_RUNTIME_COMBO_STUDIO_RPC=y
   CONFIG_ZMK_STUDIO_RPC_RX_BUF_SIZE=128
   CONFIG_ZMK_STUDIO_RPC_CUSTOM_SUBSYSTEM_REQUEST_PAYLOAD_MAX_BYTES=96
   CONFIG_ZMK_LOW_PRIORITY_THREAD_STACK_SIZE=2048
   ```

3. Open the Web UI from the custom subsystem URL or run it locally:

   ```bash
   cd web
   npm install
   npm run dev
   ```

4. Connect to the keyboard, choose a slot, and set:

   - Name: display name only.
   - Behavior: pick by name from the connected firmware's behavior list (falls
     back to a raw numeric id until the list loads).
   - Param 1, Param 2: the same binding fields ZMK Studio uses.
   - Layer mask: `0` means all layers.
   - Positions: click keys on the rendered layout below the form to toggle
     them in or out of the combo.
   - Timeout ms / Require prior idle ms: `0` inherits the corresponding global
     setting; a nonzero value overrides it for this combo only.
   - Slow release: `Inherit global` follows the Global Settings panel; `On`/`Off`
     overrides it for this combo only.

Set global timeout, slow-release mode, and require-prior-idle time in the
Global Settings panel. Require-prior-idle suppresses a combo if a non-modifier
key was tapped within that many milliseconds before the combo's first key press
(useful for avoiding accidental combos while typing fast); `0` disables it.
Enable the corresponding persist checkbox before saving if the setting should
survive reboot. The panel also shows the maximum number of runtime combo slots
available on the connected firmware.

## Development

```bash
# Run unit test + build test and verify results
python3 -m unittest discover -s tests -p test_owner.py

# Native registration/snapshot and held-combo update regressions only,
# from an already resolved owner workspace (no west update)
PYTHONPATH=tests python3 -m unittest -v test_owner.WestCommandsTests.test_zmk_test test_owner.WestCommandsTests.test_active_updates

# Run build test directly
west zmk-build tests/zmk-config

# Run unit test directly
west zmk-test tests -m .

# Run web tests
cd web && npm test
```

The held-combo regression uses actual ZMK events, HID state and Zephyr settings
dispatch. See [tests/active-update/README.md](tests/active-update/README.md) for
its controlled storage boundary and upstream/DYA commands.

The Web protobuf TypeScript files are generated with:

```bash
cd web
npm run generate
```
