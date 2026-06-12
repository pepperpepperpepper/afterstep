# Plan: split `libAfterBase/regexp.c`

Snapshot (2026-02-14): ~1022 LOC.

`regexp.c` is a small-but-over-threshold libAfterBase file that concentrates regex compile/match helpers and wrappers.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterBase/regexp_internal.h`: internal prototypes/shared helpers.

Then split into **2** translation units (each targeted **< 1000 LOC**):

- `libAfterBase/regexp_compile.c`
  - compile/cache helpers
- `libAfterBase/regexp.c` (or `regexp_match.c`)
  - match/replace helpers and public wrappers

## Refactor steps

- [ ] Extract compile/cache first.
- [ ] Update `libAfterBase/Makefile.in` and `libAfterBase/Makefile`.

## Validation

- `make -C libAfterBase`

