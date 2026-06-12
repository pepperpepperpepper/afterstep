# Plan: split `src/Wharf/Wharf.c`

Snapshot (2026-02-13): ~3116 LOC.

`Wharf` is a core AfterStep module, and (even after extracting `wharf_config.c`/`wharf_loop.c`) `Wharf.c` still contains a
large mix of responsibilities: model, rendering, event handling, and swallow/animation logic.

## Why it’s a monolith

- The “UI model” (folders/buttons), rendering, and X11 event handlers are in the same file and share globals heavily.
- Multiple higher-level concepts are intertwined: folder lifecycle, button lifecycle, swallowed windows, style selection,
  and animation timing.

## Split targets (phase 1: mechanical extraction)

This follows the same pattern used elsewhere in the tree (e.g., `src/Pager/` now has a small `Pager.c` plus focused
`pager_*.c` files).

Introduce:

- `src/Wharf/wharf_internal.h`: shared internal structs/prototypes (folders, buttons, module globals).

Then split out of `Wharf.c`:

- `src/Wharf/wharf_main.c`: `main()`, init/teardown, config load orchestration.
- `src/Wharf/wharf_style.c`: style helpers + `SetWharfLook()` + style selection.
- `src/Wharf/wharf_folder.c`: folder lifecycle (build/destroy/withdraw, open/close).
- `src/Wharf/wharf_button.c`: button lifecycle and state transitions (focus, press/release, xref helpers).
- `src/Wharf/wharf_swallow.c`: swallow target tracking + swallow exec + window association.
- `src/Wharf/wharf_render.c`: rendering functions (`render_wharf_button`, clipping, pixmaps).
- `src/Wharf/wharf_events.c`: event handlers (`on_wharf_*`), ConfigureRequest, Enter/Leave, button handling.
- `src/Wharf/wharf_animate.c`: animation timers/iterators.

Keep `src/Wharf/Wharf.c` as either:

- a compatibility wrapper that includes the new headers and contains only glue, or
- replaced by `wharf_main.c` with `Makefile*` updates (preferred).

## Refactor steps (suggested order)

- [ ] Add `wharf_internal.h` and relocate internal structs (buttons/folders) from `Wharf.c`.
- [ ] Move rendering code first (easiest to validate visually and tends to have clear call boundaries).
- [ ] Move swallow logic into `wharf_swallow.c` (keeps the complex window association in one place).
- [ ] Move folder/button lifecycles next (`wharf_folder.c`, `wharf_button.c`).
- [ ] Move event handlers into `wharf_events.c`.
- [ ] Move style selection into `wharf_style.c`.
- [ ] Update `src/Wharf/Makefile.in` and `src/Wharf/Makefile` to compile/link the new objects.

## Validation

- `make -C src/Wharf`
- End-to-end: `tools/xvfb-smoke.sh` (exercises module startup in a controlled X server)

