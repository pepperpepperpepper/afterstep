# Plan: split `libAfterImage/imencdec.c`

Snapshot (2026-02-14): ~1552 LOC.

`imencdec.c` mixes image encode/decode paths, dispatch/registry logic, and format glue in one TU.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterImage/imencdec_internal.h`: internal prototypes shared by new `.c` files.

Then split into **3** translation units (each targeted **< 1000 LOC**):

- `libAfterImage/imencdec_registry.c`
  - format registry/dispatch tables and “detect which codec to use” helpers
- `libAfterImage/imencdec_decode.c`
  - decode paths (file/stream → ASImage/ASImageDecoder)
- `libAfterImage/imencdec.c` (or `imencdec_encode.c`)
  - encode paths (ASImage → file/stream) and remaining glue

## Refactor steps

- [ ] Add `imencdec_internal.h`.
- [ ] Extract decode first (often easier to validate with fixtures).
- [ ] Extract registry/dispatch next.
- [ ] Update `libAfterImage/Makefile.in` and `libAfterImage/Makefile`.

## Validation

- `make -C libAfterImage`
- End-to-end: `tools/xvfb-smoke.sh`

