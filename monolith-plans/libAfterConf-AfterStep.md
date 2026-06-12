# Plan: split `libAfterConf/AfterStep.c`

Snapshot (2026-02-14): ~1109 LOC.

`libAfterConf/AfterStep.c` is a config-system monolith where “load/parse” and “apply/convert” responsibilities are mixed.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterConf/afterstep_conf_internal.h`: internal prototypes and shared structs.

Then split into **2** translation units (each targeted **< 1000 LOC**):

- `libAfterConf/afterstep_conf_load.c`
  - file discovery, parsing, and FreeStorage ingestion
- `libAfterConf/AfterStep.c` (or `afterstep_conf_apply.c`)
  - apply/convert and public API wrappers

## Refactor steps

- [ ] Extract load/parse first.
- [ ] Update `libAfterConf/Makefile.in` and `libAfterConf/Makefile`.

## Validation

- `make -C libAfterConf`

