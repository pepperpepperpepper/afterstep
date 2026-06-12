# Plan: split `wayland/aswllock.c`

Snapshot (2026-02-14): ~1034 LOC.

`aswllock.c` mixes protocol binding, state, and UI/render glue in one TU.

Implemented (2026-02-18):

- ✅ Split into `aswllock_wl.c` (776 LOC) and `aswllock_internal.h` (26 LOC).
- ✅ Kept `aswllock.c` as the UI/render code (258 LOC).

## Split targets (phase 1: mechanical extraction)

Introduce:

- `wayland/aswllock_internal.h`: internal prototypes/shared state.

Then split into **2** translation units (each targeted **< 1000 LOC**):

- `wayland/aswllock_wl.c`
  - registry/protocol glue and lifecycle
- `wayland/aswllock.c` (or `aswllock_ui.c`)
  - UI/render logic and orchestration

## Refactor steps

- [x] Extract protocol glue first.
- [x] Update `wayland/Makefile`.

## Validation

- `make -C wayland`
- End-to-end: `tools/wayland-smoke.sh`
