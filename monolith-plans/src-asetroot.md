# Plan: split `src/asetroot/asetroot.c`

Snapshot (2026-02-14): ~1070 LOC.

`asetroot.c` is a classic single-file CLI tool implementation (arg parsing + image load + root apply).

## Split targets (phase 1: mechanical extraction)

Introduce:

- `src/asetroot/asetroot_internal.h`: internal prototypes/shared state.

Then split into **2** translation units (each targeted **< 1000 LOC**):

- `src/asetroot/asetroot_io.c`
  - image load/transform helpers and IO glue
- `src/asetroot/asetroot.c` (or `asetroot_main.c`)
  - CLI and orchestration

## Refactor steps

- [ ] Extract IO helpers first.
- [ ] Update `src/asetroot/Makefile.in` and `src/asetroot/Makefile`.

## Validation

- `make -C src/asetroot`

