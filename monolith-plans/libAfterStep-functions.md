# Plan: split `libAfterStep/functions.c`

Snapshot (2026-02-14): ~1083 LOC.

`libAfterStep/functions.c` is distinct from `src/afterstep/functions.c`: this is the library-side function registry /
parsing utility implementation. It’s still large enough to merit splitting for reviewability and compile times.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterStep/functions_internal.h`: internal prototypes/shared tables.

Then split into **2** translation units (each targeted **< 1000 LOC**):

- `libAfterStep/functions_terms.c`
  - large term tables/registries and pure data helpers
- `libAfterStep/functions.c` (or `functions_parse.c`)
  - parsing/lookup helpers and public API glue

## Refactor steps

- [ ] Extract the term/registry tables first (pure move).
- [ ] Update `libAfterStep/Makefile.in` and `libAfterStep/Makefile`.

## Validation

- `make -C libAfterStep`
- End-to-end: `tools/xvfb-smoke.sh`

