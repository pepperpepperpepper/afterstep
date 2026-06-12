# Plan: split `libAfterImage/asimage.c`

Snapshot (2026-02-14): ~1573 LOC.

`asimage.c` is a core libAfterImage implementation file. It tends to accumulate “ASImage lifecycle + helpers + misc ops”
because many components depend on it.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterImage/asimage_internal.h`: internal prototypes and shared structs/macros not meant to be public API.

Then split into **3** translation units (each targeted **< 1000 LOC**):

- `libAfterImage/asimage_lifecycle.c`
  - create/destroy/clone/dup and reference/lifetime helpers
- `libAfterImage/asimage_access.c`
  - scanline/channel accessors, storage/encoding helpers, low-level utilities
- `libAfterImage/asimage.c` (keep filename for build stability)
  - public API glue and any remaining “misc” helpers that don’t belong elsewhere (shrinks over time)

## Refactor steps

- [ ] Add `asimage_internal.h`.
- [ ] Extract lifecycle first (stable boundaries).
- [ ] Extract access/storage helpers next.
- [ ] Update `libAfterImage/Makefile.in` and `libAfterImage/Makefile`.

## Validation

- `make -C libAfterImage`
- End-to-end: `tools/xvfb-smoke.sh`

