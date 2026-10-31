# Owner tests

This repository owns its executable test logic and fixtures under `tests/`.

## Current test ownership and compatibility

This owner supports DYA custom Studio RPC. Stock ZMK is NOT APPLICABLE;
no feature-disabled stock check is required or counted as compatibility.

Canonical entries (run from the repository root):

```bash
python3 -m unittest discover -v -s tests -p test_owner.py
bash tests/run-web.sh
```

[maintenance.json](maintenance.json) records supported cores, current commands
and explicit gaps. It is a reference map, not a passing test or a claim that
registration/coverage totals establish completeness. Preserve ordered outputs,
raw timestamps and the first failure. Read actual dependencies before execution.

`OWNER_TEST_OUTPUT=/absolute/new/directory` retains owner native/build outputs.
An existing directory fails before tests. Without it a fresh temporary directory
is printed and retained. No shared build directory is deleted. Use
`PYTHONPATH=tests python3 -m unittest -v test_owner.WestCommandsTests.<method>`
for a focused case; discovery runs the complete documented class.

Web assertions and Jest configuration live in `tests/web/`; application source
and installed dependencies remain in `web/`. The package scripts only dispatch
the owner configuration. No dependencies are installed implicitly by the tests.

Guarded production includes in `tests/support/` contain test-only scenarios.
Small production probes remain beside the state they expose. Public sample
settings and diagnostic/fault-injection APIs are production contracts, not
duplicated test algorithms.

The native verifier runs each built binary three times in independent empty
initial-flash directories, retaining raw output before filtering. It checks real
process exits, fatal assertions and every linked checked-init result before
comparing unchanged owner snapshots. A SYS_INIT error cannot be hidden by a
later matching key trace. Native scheduling and controlled APIs still do not
establish physical transport/retention. Test-only core/tool dependencies are
pinned; update pins only after reviewing the new sources and rerunning this
owner matrix. No network updates are implicit in the owner test command.

`tests/rpc-lock/run.py` compiles the real RPC handler with the real nanopb
codec, optimized and with ASan/UBSan. While Studio is locked every change
request (set, name, delete, timeout, slow release, save, discard, prior idle,
reset) is refused with "Unlock required" before it reaches the store, and the
three reads still answer. Once unlocked no request is refused for the lock.
A slot that cannot be read (its behavior left the keymap) is left out of
ListCombos instead of failing the whole list. A build with 17 combo slots must
fail: one ListCombos response holds 16.
The runtime combo store, custom settings and Studio lock are controlled.

The real production [DTS defaults boundary entry](dt-defaults/README.md) compiles
23 cases through the selected DYA application and actual generated headers.
Every raw position and layer cell is checked before narrowing or bit shifts;
timer overrides must fit uint16. First/middle/last invalid cells, UINT32_MAX,
first-invalid/upper-valid values, omitted properties and zero inheritance retain
exact compiler diagnostics and outputs. Before the repair sixteen invalid
position/layer/timer cases compiled and failed these same owner assertions.
The ordinary native/board/RPC matrix remains unchanged and required.

## Reproducible isolated checks

Use this owner's `west/west-test-isolated.yml` and its pinned test dependency
manifest. Test ZMK is `e5c9b6915b56801193e359dd9bad4a167ce0d1b8`;
Zephyr is `10ba6d0cb38bc3d258775d27982f707599320085`. Resolve the
manifest and verify actual checkout identities before running. Do not inherit
global CMake module overrides or moving production dependency references into
these isolated fixtures. The CI SDK image is pinned by digest in
`.github/workflows/zmk-module.yml`; production dependency manifests are separate.

Use a new `OWNER_TEST_OUTPUT` directory for every complete owner run. Native
repeats reuse one compiled binary with independent initial-flash state; keep
the first failure and raw combined stdout/stderr order. DYA-only compatibility
still means no stock-ZMK test requirement.

The test/tool static CI job installs pre-commit 4.7.0 and uses the pinned
Ruff/clang-format/formatting hooks. Its SKIP list avoids repeating the firmware
and Web jobs. Local full pre-commit execution may run those functional hooks;
prepare Web dependencies with the normal package lifecycle first where present.
A successful hook exit must also leave source inputs unchanged. Snapshot and
event-pattern bytes are excluded from whitespace rewriting.

## 32-bit timeout arithmetic

Variable millisecond reservations use a private owner header and preserve the
full uint32 input range, exact ceiling and configured kernel tick ABI. Fractional
products and divisions use uint32; only the required tick result is widened.
Zero/forever handling, callback ownership, cancellation and output policy remain
at their existing call sites. This introduces no new limit on timer settings.

`test_timeout_width.py` runs in the canonical host/owner entry. It compares every
0..65535 interval, full-width boundaries and 65536 deterministic full-width
samples at 14 rates against an independent wide oracle, for both tick forms.
Full retains optimized, ASan/UBSan, measured nonzero complete header line/branch
coverage and m32 object/numeric execution. Four identified arithmetic faults
must produce three qualified assertion failures per tick form. A numerically
correct wide-divider control must fail the object-width guard. Existing output,
state, timing and supported-core assertions remain enabled. Kernel/physical
acceptance remains separate from this numerical and object-code check.

Original ZMK event/behavior timestamps remain in their kernel ABI. Only the
bounded pending timeout is narrowed for conversion; the integrating boot
contract is below one uint32 millisecond clock period.

The event filter scripts and key snapshots use LF checkout rules. CRLF makes
GNU sed reject the pattern file before a behavioral check. The repair restores
exact committed Git blob bytes; no expression, expected event or snapshot value
is regenerated or accepted. Formatters still exclude these fixtures.

The owner command boundary limits `zmk-build` to one target at a time by
default. `OWNER_TEST_BUILD_PARALLELISM` selects an explicit positive target
count; account for compiler jobs and concurrent owners in the global budget.
`tests/test_build_budget.py` checks default/explicit forwarding and rejects
invalid budgets before launching west. The canonical owner entry includes
these controls without dropping any native or board scenario.

Native result admission requires every `native_sim.keymap` fixture to have a
matching binary. `required_hooks.json` names the expected checked initialization
hooks and required feature/role configuration. Fixture-bound declarations also
reject missing configuration when both the request and build omit it; excluded
roles cannot replace the requested role. The verifier retains all three raw
runs before comparing unchanged ordered snapshots and rejects logged failures,
fatal output and missing/nonzero completions. `tests/test_native_verifier.py`
owns eleven real-ELF admission controls, including a deliberately missing
native scenario, wrong-role and unrelated-fixture controls.

`bash tests/run-web.sh` is the canonical schema-generation/Web test entry.
It runs all configured Jest scenarios in one worker process. The pre-commit
Jest hook dispatches this same owner entry so nested workers do not multiply
the global container budget. Build and lint retain their separate package entries.

The five snapshot cases use explicit one-case verifier partitions, and the
canonical entry checks the complete fixture census including the separately
executed active-update kernel suite. Each selected case still requires all
three identical-binary runs. An omitted selected case, an unknown partition
or a newly unassigned fixture fails; partial discovery never implies coverage.

## Web request and core catalog lifetime

`tests/web/lifecycle.spec.tsx` and `coreLifetime.spec.tsx` retain provider/hook
regressions for old replies, follow-up mutation retirement, concurrent startup
loading and stale layout/behavior IDs. Connection or subsystem replacement
creates an independent editor; retired saves cannot dispatch a name write or
refresh. Core behavior lists stop before detail dispatch when retired. Original
scenarios remain selected. These controlled boundaries do not cancel a device
operation already sent or establish physical browser transport acceptance.
