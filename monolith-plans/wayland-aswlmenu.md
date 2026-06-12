# Plan: split `wayland/aswlmenu.c`

Snapshot (2026-02-17): legacy `wayland/aswlmenu.c` removed from build (remaining: `aswlmenu_main.c` 130 LOC, `aswlmenu_wl.c` 617 LOC, `aswlmenu_model.c` 883 LOC, `aswlmenu_config.c` 315 LOC, `aswlmenu_desktop.c` 281 LOC, `aswlmenu_render.c` 943 LOC, `aswlmenu_input.c` 411 LOC, `aswlmenu_internal.h` 252 LOC).

`aswlmenu` is the AfterStep-themed Wayland menu/launcher client, with support for:
- parsing AfterStep-ish menu configs (including nested menus)
- window list mode
- optional `.desktop` entry inclusion
- keyboard + pointer navigation with filtering

## Why it’s a monolith

- One file mixes: config parsing, desktop entry scanning, model (entries + stack + filter), Wayland glue (registry/surface),
  shm buffers, input, and rendering.
- UI state transitions (filtering, stack push/pop, selection/scroll) are tightly coupled to rendering and event handling.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `wayland/aswlmenu_internal.h`:
  - `struct as_state`, `struct as_menu_entry`, stack structs, and shared helpers

Then split:

- `wayland/aswlmenu_main.c`: argument parsing + startup + main loop.
- `wayland/aswlmenu_wl.c`: Wayland glue (registry binding, globals, shm buffers, surface/frame callbacks).
- `wayland/aswlmenu_model.c`: entry arrays, filtering, pinned handling, stack push/pop, selection/scroll state.
- `wayland/aswlmenu_config.c`: menu config parsing (`@menu`, `@title`, `@include`, pinning rules).
- `wayland/aswlmenu_desktop.c`: freedesktop `.desktop` discovery + entry conversion (optional compilation unit).
- `wayland/aswlmenu_render.c`: draw logic (header/titlebar, close button, entry rows, help overlay).
- `wayland/aswlmenu_input.c`: pointer/keyboard handling mapped onto model operations.

## Refactor steps (suggested order)

- [x] Add `aswlmenu_internal.h` and relocate shared structs.
- [x] Extract model/filter/layout logic into `aswlmenu_model.c` (keep its API minimal: “render inputs” + “actions”).
- [x] Extract config parsing into `aswlmenu_config.c`.
- [x] Extract optional desktop entry scanning into `aswlmenu_desktop.c`.
- [x] Extract rendering into `aswlmenu_render.c`.
- [x] Extract pointer/keyboard input handling into `aswlmenu_input.c`.
- [x] Extract Wayland glue into `aswlmenu_wl.c`.
- [x] Keep `aswlmenu_main.c` thin and update `wayland/Makefile`.
- [x] Move remaining activation/navigation helpers into `aswlmenu_model.c` and stop building/linking `aswlmenu.c`.

## Validation

- `make -C wayland`
- `tools/wayland-smoke.sh`
