# Plan: split `libAfterBase/layout.c`

Snapshot (2026-02-14): ~1147 LOC.

`layout.c` contains a large amount of layout/geometry helpers in one TU.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterBase/layout_internal.h`: internal prototypes and shared helpers.

Then split into **2** translation units (each targeted **< 1000 LOC**):

- `libAfterBase/layout_geom.c`
  - geometry primitives and low-level helpers
- `libAfterBase/layout.c` (or `layout_algo.c`)
  - higher-level layout algorithms and public glue

## Refactor steps

- [ ] Extract low-level geometry helpers first.
- [ ] Update `libAfterBase/Makefile.in` and `libAfterBase/Makefile`.

## Validation

- `make -C libAfterBase`

