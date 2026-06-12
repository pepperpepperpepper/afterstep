# Plan: split `src/ASConfig/ASConfig.c`

Snapshot (2026-02-14): ~1279 LOC.

`ASConfig.c` mixes CLI/tool entry points with config model logic and IO in one TU.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `src/ASConfig/asconfig_internal.h`: internal prototypes and shared state.

Then split into **3** translation units (each targeted **< 1000 LOC**):

- `src/ASConfig/asconfig_model.c`
  - in-memory model and transformation helpers
- `src/ASConfig/asconfig_io.c`
  - file IO / serialization / parsing glue
- `src/ASConfig/ASConfig.c` (or `asconfig_main.c`)
  - CLI and orchestration

## Refactor steps

- [ ] Extract IO helpers first (tends to be cohesive).
- [ ] Extract model next.
- [ ] Update `src/ASConfig/Makefile.in` and `src/ASConfig/Makefile`.

## Validation

- `make -C src/ASConfig`
- End-to-end: `tools/xvfb-smoke.sh`

