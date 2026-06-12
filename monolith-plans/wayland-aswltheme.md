# Plan: split `wayland/aswltheme.c`

Snapshot (2026-02-17): split (`aswltheme_core.c` 187 LOC, `aswltheme_colorscheme.c` 241 LOC, `aswltheme_styles.c` 934 LOC,
`aswltheme_load.c` 491 LOC, `aswltheme_internal.h` 77 LOC).

`aswltheme` provides:

- a public theme struct (`struct aswl_theme`) with defaults
- color/gradient helpers
- a non-trivial AfterStep theme reader (colorscheme + Look/MyStyle parsing + inheritance)
- config discovery and file loading logic

All of that currently lives in one TU.

## Why it’s a monolith

- Hard to reason about boundaries: “theme model” vs “color math” vs “config parsing” vs “filesystem search”.
- The parsing code is large enough that small tweaks tend to create big diffs and increase review time.

## Split targets (phase 1: mechanical extraction)

Keep the public API in `wayland/aswltheme.h` stable.

Introduce:

- `wayland/aswltheme_internal.h`: internal structs/prototypes shared across the new `.c` files (color tables, style
  parse structures, cfg loader helpers).

Then split into these **4** translation units (each targeted **< 1000 LOC**):

- `wayland/aswltheme_core.c`
  - `aswl_theme_init_default`, `aswl_theme_destroy`
  - gradient lifecycle: `aswl_gradient_destroy`, `aswl_gradient_is_valid`
  - color math helpers: `aswl_color_*` (blend/lighten/darken/hilite/shadow/average)
- `wayland/aswltheme_colorscheme.c`
  - hex color parsing + token trimming/unquoting helpers
  - colorscheme loader (`aswl_load_colorscheme`) and `aswl_color_entry` table management
- `wayland/aswltheme_styles.c`
  - style structures (`aswl_style`, `aswl_look_directives`)
  - parsing Look/MyStyle-ish files into in-memory styles (including gradients and inheritance resolution)
  - applying selected styles into `struct aswl_theme` fields
- `wayland/aswltheme_load.c`
  - config discovery/search paths (XDG + AfterStep roots)
  - `struct aswl_theme_cfg` load helpers
  - `aswl_theme_load()` orchestration (load cfg → load colorscheme → load look/styles → apply)

## Refactor steps (suggested order)

- [x] Extract `aswltheme_core.c` first (pure helpers, low risk).
- [x] Extract `aswltheme_colorscheme.c` next (file parser, still self-contained).
- [x] Extract `aswltheme_styles.c` (largest chunk; keep it mechanical).
- [x] Extract `aswltheme_load.c` last and wire the pieces together.
- [x] Update `wayland/Makefile`.

## Validation

- `make -C wayland`
- End-to-end: `tools/wayland-smoke.sh`
