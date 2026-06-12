# Plan: split `libAfterImage/apps/ascompose.c`

Snapshot (2026-02-14): ~1215 LOC.

`ascompose.c` is an app-level monolith: CLI handling, script parsing, composition execution, and output plumbing in one
file.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterImage/apps/ascompose_internal.h`: internal prototypes and shared state.

Then split into **2** translation units (each targeted **< 1000 LOC**):

- `libAfterImage/apps/ascompose_parse.c`
  - parse/config/script ingestion and validation
- `libAfterImage/apps/ascompose.c` (or `ascompose_run.c`)
  - execution/render pipeline + CLI/main

## Refactor steps

- [ ] Extract parse layer first.
- [ ] Update the build rules for `ascompose` (Makefile.in/Makefile as applicable).

## Validation

- `make -C libAfterImage`

