# Plan: split `libAfterBase/parse.c`

Snapshot (2026-02-14): ~1419 LOC.

`parse.c` collects a broad set of parsing helpers (tokenization, numeric parsing, quoting, etc.) in one TU.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterBase/parse_internal.h`: internal prototypes and shared helpers.

Then split into **3** translation units (each targeted **< 1000 LOC**):

- `libAfterBase/parse_tokenize.c`
  - tokenization and whitespace/comment handling
- `libAfterBase/parse_numbers.c`
  - numeric parsing, geometry-ish parsing, small conversion helpers
- `libAfterBase/parse.c` (or `parse_api.c`)
  - public API surface and remaining glue

## Refactor steps

- [ ] Extract tokenizer helpers first.
- [ ] Extract numeric parsing next.
- [ ] Update `libAfterBase/Makefile.in` and `libAfterBase/Makefile`.

## Validation

- `make -C libAfterBase`

