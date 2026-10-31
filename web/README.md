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

## Console privacy

The local Vite plugin replaces the two locked Studio SDKs' exception log
arguments with fixed error categories. Messages, stacks, causes, device/profile
names and arbitrary error names are not SDK console arguments. New SDK console
sites stop the build for privacy review. Excluding the SDKs from development
prebundling applies the same transform in development and production. Transport,
exception propagation and visible UI messages are preserved. Browser/framework
diagnostics and unrelated browser extensions remain separate domains.

## Dependency security

`package.json` overrides the Studio client's transitive `protobufjs` dependency
to `7.6.5`, including the patched UTF-8 helper. Use `npm ci` with this lockfile.
After changing dependencies, run `npm audit --omit=dev`, the Web tests and the
production build.
