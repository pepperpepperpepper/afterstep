# Plan: split `src/afterstep/placement.c`

Snapshot (2026-02-14): ~1911 LOC.

`src/afterstep/placement.c` mixes initial placement policy, multiple placement strategies, avoid-cover enforcement, and
interactive move/resize grid helpers in one translation unit.

## Why it’s a monolith

- One TU contains:
  - “free space rectangles” construction for smart placement
  - placement strategies (`smart`, `random`, `tile`, `cascade`, `pointer`, `closest`, `manual`)
  - avoid-cover enforcement + timers
  - moveresize plumbing (desktop grid + apply/finish handlers)
- The strategy code and the moveresize/grid code share helpers, making the file hard to navigate and hard to test.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `src/afterstep/placement_internal.h`: internal prototypes shared across the new `.c` files.

Then split into these **4** translation units (each targeted **< 1000 LOC**):

- `src/afterstep/placement_space.c`
  - free-rectangle list construction (`build_free_space_list`, iterators, rectangle subtraction glue)
  - small geometry helpers used by both grid and strategies
- `src/afterstep/placement_strategies.c`
  - `do_smart_placement`, `do_random_placement`, `do_tile_placement`, `do_cascade_placement`
  - `do_manual_placement`, `do_pointer_placement`, `do_closest_placement`
  - helper movement nudges (`move_placement_left/right/up/down`)
- `src/afterstep/placement_moveresize.c`
  - desktop grid construction (`make_desktop_grid`, `get_aswindow_grid_iter_func`)
  - interactive move/resize setup + apply/finish handlers (`setup_aswindow_moveresize`, `apply_aswindow_*`, etc.)
- `src/afterstep/placement.c` (keep filename for build stability)
  - public entry points: initial placement orchestration + avoid-cover enforcement (`enforce_avoid_cover`, `obey_avoid_cover`)
  - minimal dispatcher that calls strategy/grid helpers

## Refactor steps (suggested order)

- [x] Add `placement_internal.h`, include it from `placement.c`.
- [x] Extract `placement_moveresize.c` (mostly self-contained; clear boundaries).
- [x] Extract `placement_strategies.c` next (pure policy; easy to diff).
- [x] Extract shared rectangle/free-space helpers into `placement_space.c`.
- [x] Update `src/afterstep/Makefile.in` and `src/afterstep/Makefile`.

## Validation

- `make -C src/afterstep`
- End-to-end: `tools/xvfb-smoke.sh`

## Implemented (2026-02-18)

- ✅ Split into:
  - `src/afterstep/placement.c` (public entry points; initial placement orchestration + avoid-cover enforcement).
  - `src/afterstep/placement_space.c` (free-rectangle list construction for placement).
  - `src/afterstep/placement_strategies.c` (placement strategies + policy helpers).
  - `src/afterstep/placement_moveresize.c` (desktop grid + interactive move/resize handlers).
  - `src/afterstep/placement_internal.h` (shared internal prototypes).
- ✅ Snapshot sizes: `placement.c` 580 LOC, `placement_space.c` 133 LOC, `placement_strategies.c` 978 LOC,
  `placement_moveresize.c` 316 LOC, `placement_internal.h` 27 LOC.
- ✅ `make -C src/afterstep` and `tools/xvfb-smoke.sh` pass.
