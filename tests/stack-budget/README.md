# ARM replay stack observation

Run the declared owner command in its prepared Docker workspace:

```bash
python3 tests/stack-budget/run.py --workspace RESOLVED_WORKSPACE --output FRESH_OUTPUT
```

The full owner entry also runs it. The unchanged selected DYA application,
Runtime Combo, Custom Settings, event manager, keymap, key behavior, HID and
Zephyr system workqueue run on Cortex-M4 QEMU. An inert owner Kscan fixture
allows direct, in-range position events without a core mock rewrite. The test
uses an 8192-byte system stack, initialized stack paint, thread information,
MPU guard. The selected Zephyr excludes the software stack sentinel while that
hardware guard is enabled; the effective configuration must match. It preserves
full compiler/config/DTS/ELF/source
identities and runs the same binary three times with fresh process state.

One candidate is the small control. Eight candidates then exercise the fixed
16-position replay trace that exceeded the previous 4096-byte budget. Each
case must produce all 32 ordered keycode press/release events with exact input
timestamps and no held HID usage. A listener records ARM SP without instrumenting
production functions; kernel stack paint also captures deeper callees. The
independent parser checks every ordered event and both coherent measurements,
requires at least 128 bytes remaining and separately applies the named
`COMBO_ARM_STACK_BUDGET` assertion to the original 4096-byte limit. Expected
rejection is recorded separately from the 8192-byte product result. The complete
terminal newline is mandatory, including every-prefix rejection controls.

This models the declared 8-slot/16-position input and downstream key behavior.
It does not exercise physical GPIO/radio/USB, NVS, the keyboard's complete module
graph, every binding, 16-slot settings or arbitrary external reentry. Native
stack-byte observations are not used as ARM measurements. A matching production
image still needs actual stack high-water/HID observations before hardware
acceptance; no flashing or settings erasure is implied by this command.
