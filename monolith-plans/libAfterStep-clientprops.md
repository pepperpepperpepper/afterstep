# Plan: split `libAfterStep/clientprops.c`

Snapshot (2026-02-14): ~1550 LOC.

`clientprops.c` is a large aggregation point for X11 property (ICCCM/EWMH and AfterStep-specific) read/write helpers.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterStep/clientprops_internal.h`: internal prototypes/tables shared by the new `.c` files.

Then split into **3** translation units (each targeted **< 1000 LOC**):

- `libAfterStep/clientprops_atoms.c`
  - atom intern/lookup tables and per-display initialization/teardown
- `libAfterStep/clientprops_read.c`
  - property fetch + decode helpers (Window → structs)
- `libAfterStep/clientprops.c` (or `clientprops_write.c`)
  - property set/update helpers (structs → Window properties) and remaining glue

## Refactor steps

- [ ] Extract atom/table code first (low behavioral risk).
- [ ] Extract read/decode helpers.
- [ ] Leave the write/update surface in the main file.
- [ ] Update `libAfterStep/Makefile.in` and `libAfterStep/Makefile`.

## Validation

- `make -C libAfterStep`
- End-to-end: `tools/xvfb-smoke.sh`

