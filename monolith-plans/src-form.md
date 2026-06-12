# Plan: split `src/Form/Form.c`

Snapshot (2026-02-14): ~2044 LOC.

`Form.c` is the X11 “Form” module: it parses a form definition, builds UI state (items/widgets), handles the X11 event
loop, and renders widgets.

## Why it’s a monolith

- One TU mixes:
  - parsing and model construction (items, choices, button directives)
  - layout calculations (positions/sizes per line/item)
  - X11 window creation, event loop, and input handling
  - drawing and widget rendering for multiple item types

## Split targets (phase 1: mechanical extraction)

Introduce:

- `src/Form/form_internal.h`: shared structs/constants and internal prototypes (keep it module-private).

Then split (each targeted **< 1000 LOC**):

- `src/Form/form_main.c`
  - `main()` / module init + argument parsing
  - high-level init/shutdown and “run loop” orchestration
- `src/Form/form_parse.c`
  - parsing input/config into the in-memory model (`item` arrays, choices, directives)
- `src/Form/form_layout.c`
  - layout/geometry computations and hit-testing helpers
- `src/Form/form_x11.c`
  - X11 window creation, event loop, event handlers
  - rendering/drawing helpers for widgets (or split further into `form_draw.c` if needed)

Keep the module semantics identical (protocol messages, exit codes, config format).

## Refactor steps (suggested order)

- [ ] Extract parsing first (gives a stable “model” surface).
- [ ] Extract layout next (pure-ish computations).
- [ ] Extract X11/event loop/rendering last (most coupled).
- [ ] Update `src/Form/Makefile.in` and `src/Form/Makefile`.

## Validation

- `make -C src/Form`
- End-to-end: `tools/xvfb-smoke.sh`

