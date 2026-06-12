# Plan: split `src/Ident/Ident.c`

Snapshot (2026-02-14): ~1027 LOC.

`Ident.c` is a module-style implementation that mixes entry wiring, UI, and event handling in one TU.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `src/Ident/ident_internal.h`: internal prototypes/shared state.

Then split into **2** translation units (each targeted **< 1000 LOC**):

- `src/Ident/ident_ui.c`
  - UI rendering/layout and input handling
- `src/Ident/Ident.c` (or `ident_main.c`)
  - init/IPC/main loop orchestration

## Refactor steps

- [ ] Extract UI first (cohesive chunk).
- [ ] Update `src/Ident/Makefile.in` and `src/Ident/Makefile`.

## Validation

- `make -C src/Ident`
- End-to-end: `tools/xvfb-smoke.sh`

