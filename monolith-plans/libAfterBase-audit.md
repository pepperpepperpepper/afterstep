# Plan: split `libAfterBase/audit.c`

Snapshot (2026-02-14): ~1165 LOC.

`audit.c` collects a wide set of audit/logging/diagnostic helpers in one TU.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterBase/audit_internal.h`: internal prototypes/shared state.

Then split into **2** translation units (each targeted **< 1000 LOC**):

- `libAfterBase/audit_log.c`
  - logging/output formatting helpers
- `libAfterBase/audit.c` (or `audit_checks.c`)
  - checks/instrumentation helpers and remaining glue

## Refactor steps

- [ ] Extract the pure logging layer first.
- [ ] Update `libAfterBase/Makefile.in` and `libAfterBase/Makefile`.

## Validation

- `make -C libAfterBase`

