# Plan: split `src/ASMount/main.c`

Snapshot (2026-02-14): ~1475 LOC.

`ASMount` is implemented largely in `main.c`, mixing UI, command handling, and mount/unmount operations in one TU.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `src/ASMount/asmount_internal.h`: internal shared state/prototypes.

Then split into **3** translation units (each targeted **< 1000 LOC**):

- `src/ASMount/asmount_ops.c`
  - mount/unmount/refresh operations and command execution helpers
- `src/ASMount/asmount_ui.c`
  - UI layout/render and event handling
- `src/ASMount/main.c` (or `asmount_main.c`)
  - CLI/module init and main loop wiring

## Refactor steps

- [ ] Extract `asmount_ops.c` first (clear boundaries).
- [ ] Extract UI next.
- [ ] Update `src/ASMount/Makefile.in` and `src/ASMount/Makefile`.

## Validation

- `make -C src/ASMount`
- End-to-end: `tools/xvfb-smoke.sh`

