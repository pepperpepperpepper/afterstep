# Plan: split `libAfterStep/wmprops.c`

Snapshot (2026-02-14): ~1435 LOC.

`wmprops.c` concentrates WM/EWMH property tracking, decoding/encoding, and per-screen bookkeeping in one TU.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterStep/wmprops_internal.h`: internal prototypes and tables.

Then split into **3** translation units (each targeted **< 1000 LOC**):

- `libAfterStep/wmprops_atoms.c`
  - atoms and per-display initialization/cleanup
- `libAfterStep/wmprops_read.c`
  - property read/decode and state update helpers
- `libAfterStep/wmprops.c` (or `wmprops_write.c`)
  - property write/broadcast helpers and remaining glue

## Refactor steps

- [ ] Extract atoms first.
- [ ] Extract read/decode next.
- [ ] Update `libAfterStep/Makefile.in` and `libAfterStep/Makefile`.

## Validation

- `make -C libAfterStep`
- End-to-end: `tools/xvfb-smoke.sh`

