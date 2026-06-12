# Plan: split `libAfterStep/desktop_category.c`

Snapshot (2026-02-14): ~1128 LOC.

`desktop_category.c` mixes category parsing, tree/model operations, and lookup helpers in one TU.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterStep/desktop_category_internal.h`: internal prototypes/shared state.

Then split into **2** translation units (each targeted **< 1000 LOC**):

- `libAfterStep/desktop_category_parse.c`
  - parsing/ingestion helpers (desktop files/categories)
- `libAfterStep/desktop_category.c` (or `desktop_category_tree.c`)
  - tree/model operations and public API glue

## Refactor steps

- [ ] Extract parsing first.
- [ ] Update `libAfterStep/Makefile.in` and `libAfterStep/Makefile`.

## Validation

- `make -C libAfterStep`
- End-to-end: `tools/xvfb-smoke.sh`

