# Plan: split `src/ASDocGen/ASDocGen.c`

Snapshot (2026-02-14): ~1050 LOC.

`ASDocGen.c` is the entry point for the documentation generator tool, mixing CLI, driver selection, and generation logic.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `src/ASDocGen/asdocgen_internal.h`: internal prototypes/shared state.

Then split into **2** translation units (each targeted **< 1000 LOC**):

- `src/ASDocGen/asdocgen_drivers.c`
  - output driver implementations/dispatch
- `src/ASDocGen/ASDocGen.c` (or `asdocgen_main.c`)
  - CLI and orchestration

## Refactor steps

- [ ] Extract drivers/dispatch first.
- [ ] Update `src/ASDocGen/Makefile.in` and `src/ASDocGen/Makefile`.

## Validation

- `make -C src/ASDocGen`

