# Plan: split `libAfterStep/canvas.c`

Snapshot (2026-02-14): ~1132 LOC.

`canvas.c` concentrates ASCanvas implementation details (lifecycle, X11 integration, render helpers) in one TU.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterStep/canvas_internal.h`: internal prototypes/shared state.

Then split into **2** translation units (each targeted **< 1000 LOC**):

- `libAfterStep/canvas_core.c`
  - lifecycle, geometry tracking, basic operations
- `libAfterStep/canvas.c` (or `canvas_x11.c`)
  - X11 integration, event glue, and remaining helpers

## Refactor steps

- [ ] Extract core/lifecycle first.
- [ ] Update `libAfterStep/Makefile.in` and `libAfterStep/Makefile`.

## Validation

- `make -C libAfterStep`
- End-to-end: `tools/xvfb-smoke.sh`

