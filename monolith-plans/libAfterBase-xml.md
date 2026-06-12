# Plan: split `libAfterBase/xml.c`

Snapshot (2026-02-14): ~1020 LOC.

`xml.c` is a libAfterBase aggregation point for XML-ish parsing and IO glue.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterBase/xml_internal.h`: internal prototypes/shared state.

Then split into **2** translation units (each targeted **< 1000 LOC**):

- `libAfterBase/xml_parse.c`
  - parsing/tokenization helpers
- `libAfterBase/xml.c` (or `xml_io.c`)
  - IO helpers and public wrappers

## Refactor steps

- [ ] Extract parsing first.
- [ ] Update `libAfterBase/Makefile.in` and `libAfterBase/Makefile`.

## Validation

- `make -C libAfterBase`

