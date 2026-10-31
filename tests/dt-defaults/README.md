# Production DTS defaults boundaries

Run `python3 tests/dt-defaults/run.py` from this owner's resolved west workspace.
The canonical owner test class includes this entry. `COMBO_DT_ARTIFACTS` selects
a fresh output directory; an existing directory fails before execution.

All 23 cases configure the actual pinned DYA ZMK application for native_sim
through west/CMake, the owner's real binding, generated devicetree headers and
enabled Runtime Combo source. The fixture then compiles the actual
`runtime_combo.c` object selected from that build's compilation database. It
does not copy an algorithm or provide substitute macros. Per-case configuration,
DTS, build inputs, commands, raw compiler output and outcomes remain in artifacts.

- Omitted/default properties, zero timer inheritance and the upper ABI
  boundaries compile: position 65535, layer 31, timers 65535 and slot 7 of 8.
- Position cells 65536 and 0xffffffff must fail at the first, middle and last
  list element with the exact position diagnostic.
- Layer cells 32 and 0xffffffff must fail at the first, middle and last
  element with the exact layer diagnostic, before a bit shift can narrow them.
- Both timer overrides reject 65536/0xffffffff with their own exact diagnostic.
  Existing slot and position-count guards also retain their named failures.

Before the repair the same owner entry reports sixteen failures because these
invalid position/layer/timer inputs compiled. The unchanged assertions and raw
before logs are the negative control; no compiler warning is treated as refusal.
The checks prove compile-time refusal and preserved valid ABI boundaries. The
owner's ordinary defaults, overrides, discard, active-update, RPC and board
matrix remains required. This object check does not establish host HID,
transport, physical timing, reset or power acceptance.
