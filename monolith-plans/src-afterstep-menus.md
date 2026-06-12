# Plan: split `src/afterstep/menus.c`

Snapshot (2026-02-14): ~1800 LOC.

`src/afterstep/menus.c` implements almost the entire X11 menu system:

- menu/window creation + lifecycle (`ASMenu`, items, scrollbars, balloons)
- layout + rendering (bars, selection/press states, size computations)
- event handlers (pointer/scroll/keyboard) via `ASInternalWindow`
- high-level entry points (`run_menu`, `run_submenu`, pinning, submenu stack)

## Why it’s a monolith

- UI rendering, input handling, and “menu model” logic are interleaved.
- Hard to change one area (e.g., keyboard navigation) without touching unrelated code (e.g., rendering caches).

## Split targets (phase 1: mechanical extraction)

Introduce:

- `src/afterstep/menus_internal.h`: internal shared prototypes and minimal shared state (`ASTopmostMenu`, helper
  forward-decls).

Then split into these **4** translation units (each targeted **< 1000 LOC**):

- `src/afterstep/menus_core.c`
  - menu/window creation and destruction (`create_asmenu`, `destroy_asmenu`, `close_asmenu`, submenu close helpers)
  - menu item wiring (`set_asmenu_item_data`, free helpers)
  - stack helpers (`find_asmenu`, `find_topmost_transient_menu`, `pin_asmenu`, `is_menu_pinnable`)
- `src/afterstep/menus_render.c`
  - layout + render helpers (`render_asmenu_bars`, size calculations, selection/press visuals)
  - “render triggers” used by event handlers
- `src/afterstep/menus_events.c`
  - `ASInternalWindow` callbacks (pointer/scroll/keyboard handlers, hilite/pressure hooks, moveresize hooks)
  - small event-to-model translation helpers (selection computation, scroll logic)
- `src/afterstep/menus_run.c`
  - hints/window integration (`make_menu_hints`, `show_asmenu`)
  - high-level execution: `run_submenu`, `run_menu_data`, `run_menu`

## Refactor steps (suggested order)

- [x] Add `menus_internal.h` and include it from the split `menus_*.c` files.
- [x] Extract `menus_render.c` first (large cohesive chunk; minimal external deps).
- [x] Extract `menus_events.c` next (keeps the callback surface explicit).
- [x] Extract `menus_run.c` and leave `menus_core.c` as the remaining base.
- [x] Update `src/afterstep/Makefile.in` and `src/afterstep/Makefile`.

## Validation

- `make -C src/afterstep`
- End-to-end: `tools/xvfb-smoke.sh`

## Implemented (2026-02-18)

- ✅ Split into:
  - `src/afterstep/menus_core.c` (menu/window lifecycle, menu data wiring, stack helpers).
  - `src/afterstep/menus_render.c` (layout + rendering).
  - `src/afterstep/menus_events.c` (ASInternalWindow callbacks, selection/scroll/keyboard logic).
  - `src/afterstep/menus_run.c` (hints/window integration + `run_menu*` entry points).
  - `src/afterstep/menus_internal.h` (shared internal prototypes/state).
- ✅ Snapshot sizes: `menus_core.c` 515 LOC, `menus_render.c` 339 LOC, `menus_events.c` 745 LOC, `menus_run.c` 319 LOC,
  `menus_internal.h` 42 LOC.
- ✅ `make -C src/afterstep` and `tools/xvfb-smoke.sh` pass.
