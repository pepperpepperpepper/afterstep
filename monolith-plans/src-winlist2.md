# Plan: split `src/WinList2/WinList.c`

Snapshot (2026-02-14): ~1661 LOC.

`WinList.c` is a single-file module implementation that mixes: module entry/IPC glue, window list model logic, and UI
render/event handling.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `src/WinList2/winlist_internal.h`: internal prototypes and shared state for the module.

Then split into **3** translation units (each targeted **< 1000 LOC**):

- `src/WinList2/winlist_model.c`
  - model/state: window list representation, sorting/filtering, update helpers
- `src/WinList2/winlist_ui.c`
  - rendering/layout and input/event handling (X11 events → model actions)
- `src/WinList2/WinList.c` (or `winlist_main.c`)
  - CLI/module init, AfterStep IPC wiring, main loop orchestration

## Refactor steps

- [ ] Add `winlist_internal.h`.
- [ ] Extract UI/render helpers first (often cohesive and easier to validate visually).
- [ ] Extract model/state next.
- [ ] Leave `WinList.c` as the module entry + wiring.
- [ ] Update `src/WinList2/Makefile.in` and `src/WinList2/Makefile`.

## Validation

- `make -C src/WinList2`
- End-to-end: `tools/xvfb-smoke.sh`

