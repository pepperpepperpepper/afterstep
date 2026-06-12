# Plan: split `libAfterStep/session.c`

Snapshot (2026-02-14): ~1150 LOC.

`session.c` mixes session discovery, path management, and save/restore helpers in one TU.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterStep/session_internal.h`: internal prototypes/shared state.

Then split into **2** translation units (each targeted **< 1000 LOC**):

- `libAfterStep/session_paths.c`
  - filesystem path helpers and discovery logic
- `libAfterStep/session.c` (or `session_state.c`)
  - session state save/restore and public API glue

## Refactor steps

- [ ] Extract path/discovery helpers first.
- [ ] Update `libAfterStep/Makefile.in` and `libAfterStep/Makefile`.

## Validation

- `make -C libAfterStep`
- End-to-end: `tools/xvfb-smoke.sh`

