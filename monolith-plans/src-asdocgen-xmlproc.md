# Plan: split `src/ASDocGen/xmlproc.c`

Snapshot (2026-02-14): ~1092 LOC.

`xmlproc.c` contains XML processing helpers that have grown beyond a single-file module.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `src/ASDocGen/xmlproc_internal.h`: internal prototypes/shared state.

Then split into **2** translation units (each targeted **< 1000 LOC**):

- `src/ASDocGen/xmlproc_parse.c`
  - XML parsing/tokenization helpers
- `src/ASDocGen/xmlproc.c` (or `xmlproc_emit.c`)
  - output/emission helpers and public wrappers

## Refactor steps

- [ ] Extract parsing first.
- [ ] Update `src/ASDocGen/Makefile.in` and `src/ASDocGen/Makefile`.

## Validation

- `make -C src/ASDocGen`

