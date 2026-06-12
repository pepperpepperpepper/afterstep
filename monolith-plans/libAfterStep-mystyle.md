# Plan: split `libAfterStep/mystyle.c`

Snapshot (2026-02-14): ~1549 LOC.

`mystyle.c` is a classic AfterStep hotspot: style model, parsing/config apply, and render-ish helpers end up in one file.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterStep/mystyle_internal.h`: internal prototypes shared across new `.c` files.

Then split into **3** translation units (each targeted **< 1000 LOC**):

- `libAfterStep/mystyle_core.c`
  - MyStyle lifecycle, caches, lookups, and “pure model” helpers
- `libAfterStep/mystyle_parse.c`
  - config parsing/apply logic (FreeStorage → MyStyle updates)
- `libAfterStep/mystyle.c` (or `mystyle_render.c`)
  - any remaining draw/gradient/texture helpers + glue

## Refactor steps

- [ ] Add `mystyle_internal.h`.
- [ ] Extract parse/apply helpers (clear boundary).
- [ ] Extract “model/core” helpers next.
- [ ] Update `libAfterStep/Makefile.in` and `libAfterStep/Makefile`.

## Validation

- `make -C libAfterStep`
- End-to-end: `tools/xvfb-smoke.sh`

