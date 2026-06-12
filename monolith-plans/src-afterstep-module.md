# Plan: split `src/afterstep/module.c`

Snapshot (2026-02-14): ~1236 LOC.

`module.c` mixes module spawn/control, IPC/message handling, and bookkeeping in one TU.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `src/afterstep/module_internal.h`: internal prototypes and shared state.

Then split into **2** translation units (each targeted **< 1000 LOC**):

- `src/afterstep/module_ipc.c`
  - message decode/encode, pipe/FD helpers, dispatch callbacks
- `src/afterstep/module.c` (or `module_spawn.c`)
  - module spawn/control orchestration and public entry points

## Refactor steps

- [ ] Extract IPC helpers first.
- [ ] Update `src/afterstep/Makefile.in` and `src/afterstep/Makefile`.

## Validation

- `make -C src/afterstep`
- End-to-end: `tools/xvfb-smoke.sh`

