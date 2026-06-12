# Plan: split `libAfterStep/hints_common.c`

Snapshot (2026-02-14): ~1764 LOC.

`hints_common.c` is a core libAfterStep hotspot for hint/name/flag merging and cleanup. It’s used widely by the window
manager and modules, so improving locality and compile-time is worthwhile.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterStep/hints_common_internal.h`: internal helpers shared across new `.c` files.

Then split into **3** translation units (each targeted **< 1000 LOC**):

- `libAfterStep/hints_common_names.c`
  - name list manipulation (`add_name_to_list`, name compare helpers, string utilities)
  - name-related parts of `destroy_hints` / `compare_names`
- `libAfterStep/hints_common_flags.c`
  - flag encode/decode utilities (`encode_flags`, `decode_flags`) and related tables/helpers
- `libAfterStep/hints_common.c` (keep filename for build stability)
  - the remaining “merge and apply” routines that orchestrate hints from multiple sources

## Refactor steps

- [ ] Add `hints_common_internal.h`.
- [ ] Extract the pure helpers first (`*_flags.c`, `*_names.c`).
- [ ] Leave `hints_common.c` as the orchestration/merge layer.
- [ ] Update `libAfterStep/Makefile.in` and `libAfterStep/Makefile`.

## Validation

- `make -C libAfterStep`
- End-to-end: `tools/xvfb-smoke.sh`

