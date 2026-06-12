# Plan: split `libASGTK/asgtkxmleditor.c`

Snapshot (2026-02-14): ~1108 LOC.

`asgtkxmleditor.c` appears to mix GTK widget UI code with model/IO glue in one TU.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libASGTK/asgtkxmleditor_internal.h`: internal prototypes/shared state.

Then split into **2** translation units (each targeted **< 1000 LOC**):

- `libASGTK/asgtkxmleditor_model.c`
  - model/state handling and XML load/save glue
- `libASGTK/asgtkxmleditor.c` (or `asgtkxmleditor_widget.c`)
  - GTK widget construction, signals, and UI code

## Refactor steps

- [ ] Extract model/IO first (clean dependency boundary).
- [ ] Update `libASGTK/Makefile.in` and `libASGTK/Makefile`.

## Validation

- `make -C libASGTK`

