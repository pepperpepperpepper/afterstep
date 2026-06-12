# Plan: split `src/Script/Instructions.c`

Snapshot (2026-02-14): ~1399 LOC.

`Instructions.c` appears to contain both the instruction definitions and the execution/dispatch loop for AfterStep’s
scripting support.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `src/Script/instructions_internal.h`: internal prototypes shared by instruction implementation files.

Then split into **2** translation units (each targeted **< 1000 LOC**):

- `src/Script/instructions_core.c`
  - interpreter/dispatch loop and shared state helpers
- `src/Script/Instructions.c` (or `instructions_ops.c`)
  - instruction implementations (large switch tables and per-op helpers)

## Refactor steps

- [ ] Extract the instruction implementations first (mechanical move).
- [ ] Leave `instructions_core.c` as the execution loop + shared helpers.
- [ ] Update `src/Script/Makefile.in` and `src/Script/Makefile`.

## Validation

- `make -C src/Script`
- End-to-end: `tools/xvfb-smoke.sh`

