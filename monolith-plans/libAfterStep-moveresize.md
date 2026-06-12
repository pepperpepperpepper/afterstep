# Plan: split `libAfterStep/moveresize.c`

Snapshot (2026-02-14): ~1106 LOC.

`moveresize.c` mixes interactive move/resize mechanics with geometry constraints/math in one TU.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterStep/moveresize_internal.h`: internal prototypes/shared state.

Then split into **2** translation units (each targeted **< 1000 LOC**):

- `libAfterStep/moveresize_constraints.c`
  - geometry constraints, snapping/grid math, pure-ish helpers
- `libAfterStep/moveresize.c` (or `moveresize_interaction.c`)
  - interactive loop and X11 integration

## Refactor steps

- [ ] Extract constraints/math first (easy to diff).
- [ ] Update `libAfterStep/Makefile.in` and `libAfterStep/Makefile`.

## Validation

- `make -C libAfterStep`
- End-to-end: `tools/xvfb-smoke.sh`

