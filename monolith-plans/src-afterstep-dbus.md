# Plan: split `src/afterstep/dbus.c`

Snapshot (2026-02-14): ~1306 LOC.

`dbus.c` mixes dbus connection setup, method handlers, and signal/notification glue inside the core WM.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `src/afterstep/dbus_internal.h`: internal prototypes and shared state.

Then split into **2** translation units (each targeted **< 1000 LOC**):

- `src/afterstep/dbus_core.c`
  - connection setup/teardown and main-loop integration
- `src/afterstep/dbus.c` (or `dbus_api.c`)
  - exported API, method handlers, and signal emission helpers

## Refactor steps

- [ ] Extract core connection/lifecycle first.
- [ ] Update `src/afterstep/Makefile.in` and `src/afterstep/Makefile`.

## Validation

- `make -C src/afterstep`
- End-to-end: `tools/xvfb-smoke.sh`

