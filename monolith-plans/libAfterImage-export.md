# Plan: split `libAfterImage/export.c`

Snapshot (2026-02-14): ~1289 LOC.

`export.c` is a libAfterImage hotspot where dispatch/format selection and encoding glue accumulates.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterImage/export_internal.h`: internal prototypes shared by new `.c` files.

Then split into **3** translation units (each targeted **< 1000 LOC**):

- `libAfterImage/export_dispatch.c`
  - “choose codec/format” helpers and tables
- `libAfterImage/export_encode.c`
  - encode implementations/wrappers
- `libAfterImage/export.c` (or `export_paths.c`)
  - path/IO helpers and remaining glue

## Refactor steps

- [ ] Extract dispatch tables first.
- [ ] Extract encode helpers next.
- [ ] Update `libAfterImage/Makefile.in` and `libAfterImage/Makefile`.

## Validation

- `make -C libAfterImage`
- End-to-end: `tools/xvfb-smoke.sh`

