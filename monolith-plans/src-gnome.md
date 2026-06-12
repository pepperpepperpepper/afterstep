# Plan: split `src/Gnome/Gnome.c`

Snapshot (2026-02-14): ~1326 LOC.

`Gnome.c` is a module-style implementation that mixes entry wiring with protocol glue/policy in one TU.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `src/Gnome/gnome_internal.h`: internal shared prototypes/state.

Then split into **2** translation units (each targeted **< 1000 LOC**):

- `src/Gnome/gnome_protocol.c`
  - GNOME protocol/property glue and decode/encode helpers
- `src/Gnome/Gnome.c` (or `gnome_main.c`)
  - module init + event loop orchestration

## Refactor steps

- [ ] Extract protocol glue first.
- [ ] Update `src/Gnome/Makefile.in` and `src/Gnome/Makefile`.

## Validation

- `make -C src/Gnome`
- End-to-end: `tools/xvfb-smoke.sh`

