# Plan: split `src/afterstep/window_frame.c`

Snapshot (2026-02-14): ~1452 LOC.

`window_frame.c` is a core WM file that mixes frame geometry/layout logic with drawing/apply-to-window glue in one TU.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `src/afterstep/window_frame_internal.h`: internal prototypes and shared structs.

Then split into **3** translation units (each targeted **< 1000 LOC**):

- `src/afterstep/window_frame_layout.c`
  - geometry calculations, sizing, layout constraints
- `src/afterstep/window_frame_render.c`
  - rendering glue (mystyles/buttons/tiles) and “redraw frame” helpers
- `src/afterstep/window_frame.c` (or `window_frame_apply.c`)
  - apply-to-window plumbing and remaining glue

## Refactor steps

- [ ] Extract layout calculations first (pure-ish helpers).
- [ ] Extract render glue next.
- [ ] Update `src/afterstep/Makefile.in` and `src/afterstep/Makefile`.

## Validation

- `make -C src/afterstep`
- End-to-end: `tools/xvfb-smoke.sh`

