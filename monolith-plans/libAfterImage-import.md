# Plan: split `libAfterImage/import.c`

Snapshot (2026-02-14): ~1407 LOC.

`import.c` is a large libAfterImage dispatch point that mixes path handling, format detection, and “call the right
decoder” glue in one TU.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterImage/import_internal.h`: internal prototypes shared by new `.c` files.

Then split into **3** translation units (each targeted **< 1000 LOC**):

- `libAfterImage/import_paths.c`
  - path expansion, search paths, file kind detection helpers
- `libAfterImage/import_dispatch.c`
  - format detection/dispatch tables and high-level load entry points
- `libAfterImage/import.c` (or `import_decode.c`)
  - remaining decode glue and public API wrappers

## Refactor steps

- [ ] Extract path helpers first (pure-ish utilities).
- [ ] Extract dispatch/registry next.
- [ ] Update `libAfterImage/Makefile.in` and `libAfterImage/Makefile`.

## Validation

- `make -C libAfterImage`
- End-to-end: `tools/xvfb-smoke.sh`

