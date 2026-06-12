# Plan: split `src/afterstep/pager.c`

Snapshot (2026-02-14): ~1479 LOC.

`pager.c` is a large core WM file that mixes pager state/model, update/policy, and drawing/event glue in one TU.

## Split targets (phase 1: mechanical extraction)

Split into **3** translation units (each targeted **< 1000 LOC**):

- `src/afterstep/pager_model.c`
  - viewport/desk model + update helpers (`MoveViewport`, `ChangeDeskAndViewport`, `ChangeDesks`)
- `src/afterstep/pager_render.c`
  - root background generation/animation + background client requests
- `src/afterstep/pager.c`
  - paging/edge-scroll entry point (`HandlePaging`)

## Refactor steps

- [x] Extract background/render code (`pager_render.c`).
- [x] Extract viewport/desk model (`pager_model.c`).
- [x] Update `src/afterstep/Makefile.in` and `src/afterstep/Makefile`.

## Validation

- `make -C src/afterstep`
- End-to-end: `tools/xvfb-smoke.sh`

## Implemented (2026-02-18)

- ✅ Split into:
  - `src/afterstep/pager.c` (`HandlePaging`)
  - `src/afterstep/pager_model.c` (viewport/desk model + helpers)
  - `src/afterstep/pager_render.c` (root background + client background requests)
- ✅ Snapshot sizes: `pager.c` 198 LOC, `pager_model.c` 355 LOC, `pager_render.c` 998 LOC.
- ✅ `make -C src/afterstep` and `tools/xvfb-smoke.sh` pass.
