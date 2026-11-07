# Runtime Combo Web Frontend

React + TypeScript frontend for editing `zmk-feature-runtime-combo` through the
custom ZMK Studio RPC subsystem.

## Quick Start

```bash
npm install
npm run generate
npm run dev
```

## Commands

```bash
npm run generate  # Generate TypeScript protobuf types from ../proto
npm run build     # Type-check and build
npm test          # Run Jest tests
npm run lint      # ESLint and Prettier check
```

## Protocol

The protobuf schema is defined at:

```text
../proto/cormoran/runtime_combo/runtime_combo.proto
```

The generated TypeScript file is:

```text
src/proto/cormoran/runtime_combo/runtime_combo.ts
```

The custom subsystem identifier is:

```text
cormoran__runtime_combo
```

Every custom reply must contain exactly one recognized response variant matching
the request, or one error. The global-settings response must include `settings`;
the combo read must include `combo`. A mutation requires `status` before the
editor reports success, dispatches a combo-name write or refreshes the device
state. Missing, empty, malformed, unknown, wrong-operation and mixed responses
are refused. Explicit empty lists and default-valued status messages are valid.
Blank error text is replaced with a visible fixed fallback.

The Timeout, Slow Release and Require Prior Idle buttons save their respective
field with the shared "Persist global settings" choice. Their follow-up read
updates that field and the read-only slot limit, retaining the other drafts and
the persistence choice. Edits made during the write or read belong to a later
save and remain visible. The initial read has the same protection for edits made
while connecting. An explicit Global Settings refresh replaces values that have
not changed since its read began. Save Pending and Discard Pending refresh all
global values that have not changed since the action began, including while its
mutation acknowledgement is pending. Those actions do not send unsent form
values as individual setting writes.

These response and draft boundaries are covered by the owner DOM/RPC tests.
They do not establish browser or physical serial/BLE delivery, or undo a device
write already sent before an error or disconnect.

## Console privacy

The app installs a runtime console guard before rendering. It keeps the first
string argument as a fixed label and redacts subsequent strings, objects and
errors according to `src/consolePrivacy.ts`; it does not rewrite the SDKs.
`tests/web/sdkConsole.spec.ts` checks every executable console use in the two
locked SDKs' shipped JavaScript against the exact reviewed sites. A new or changed
site fails that test for review. The Vite build itself has no SDK console-site
gate. Transport, exception propagation and visible UI messages are preserved.
Browser/framework diagnostics and unrelated browser extensions remain separate
domains.

## Dependency security

`package.json` overrides the Studio client's transitive `protobufjs` dependency
to `7.6.5`, including the patched UTF-8 helper. Use `npm ci` with this lockfile.
After changing dependencies, run both `npm audit` and `npm audit --omit=dev`,
the Web tests, lint and the production build. The reviewed lockfile updates only
compatible development transitive dependencies and retains the direct packages,
Studio SDK commits and production dependency graph. The 2026-11-07 full audit
still reports low/moderate development findings; the production-only audit has
no findings. A clean production-only audit does not make the full audit pass.
