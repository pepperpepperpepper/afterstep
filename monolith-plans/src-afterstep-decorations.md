# Plan: split `src/afterstep/decorations.c`

Snapshot (2026-02-14): ~1497 LOC.

`decorations.c` is a core WM file that tends to accumulate: decoration model/state, rendering glue, and “apply to window”
policy in one TU.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `src/afterstep/decorations_internal.h`: internal prototypes and shared structs.

Then split into **3** translation units (each targeted **< 1000 LOC**):

- `src/afterstep/decorations_model.c`
  - orientation globals (`HorzOrientation`, `VertOrientation`) and policy helpers (allowed functions, titlebutton mask,
    input grabs).
- `src/afterstep/decorations_render.c`
  - canvas creation/destruction helpers + tbar creation helpers + mystyle invalidation.
- `src/afterstep/decorations.c`
  - main “apply decoration to ASWindow” logic (`hints2decorations`, `redecorate_window`) + shaping helpers.

## Refactor steps

- [x] Add `decorations_internal.h`.
- [x] Extract `decorations_render.c` (canvas/tbar helpers + invalidation).
- [x] Extract `decorations_model.c` (orientation + policy helpers).
- [x] Update `src/afterstep/Makefile.in` and `src/afterstep/Makefile`.

## Validation

- `make -C src/afterstep`
- End-to-end: `tools/xvfb-smoke.sh`

## Implemented (2026-02-18)

- ✅ Split into:
  - `src/afterstep/decorations.c` (main apply/orchestration).
  - `src/afterstep/decorations_model.c` (orientation + policy helpers).
  - `src/afterstep/decorations_render.c` (canvas/tbar helpers + invalidation).
  - `src/afterstep/decorations_internal.h` (shared internal prototypes).
- ✅ Snapshot sizes: `decorations.c` 790 LOC, `decorations_model.c` 340 LOC, `decorations_render.c` 433 LOC,
  `decorations_internal.h` 25 LOC.
- ✅ `make -C src/afterstep` and `tools/xvfb-smoke.sh` pass.
