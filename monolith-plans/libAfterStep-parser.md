# Plan: split `libAfterStep/parser.c`

Snapshot (2026-02-14): ~1001 LOC.

`parser.c` sits just over the 1000 LOC threshold and concentrates parsing/token logic that is used by multiple config
consumers.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterStep/parser_internal.h`: internal prototypes/shared state.

Then split into **2** translation units (each targeted **< 1000 LOC**):

- `libAfterStep/parser_tokenize.c`
  - tokenization, quoting/comment handling, low-level scanning helpers
- `libAfterStep/parser.c` (or `parser_api.c`)
  - public API wrappers and remaining glue

## Refactor steps

- [ ] Extract tokenization first.
- [ ] Update `libAfterStep/Makefile.in` and `libAfterStep/Makefile`.

## Validation

- `make -C libAfterStep`
- End-to-end: `tools/xvfb-smoke.sh`

