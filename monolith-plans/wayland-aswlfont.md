# Plan: split `wayland/aswlfont.c`

Snapshot (2026-02-18): split into multiple TUs to keep each file < 1000 LOC:

- `wayland/aswlfont.c` (685 LOC): public API + built-in rasterizer + FreeType render glue
- `wayland/aswlfont_backend.c` (574 LOC): font loading/backend bindings + cache/state
- `wayland/aswlfont_layout.c` (258 LOC): UTF-8 decode + measurement/fit helpers
- `wayland/aswlfont_internal.h` (55 LOC): shared internal structs/prototypes

`aswlfont.c` mixes font loading, text layout/measurement, and actual rasterization/rendering to ARGB buffers in one TU.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `wayland/aswlfont_internal.h`: internal prototypes/types shared by the new `.c` files.

Then split into **3** translation units (each targeted **< 1000 LOC**):

- `wayland/aswlfont_backend.c`
  - font loading/backend bindings (e.g., freetype/fontconfig) and font cache/state
- `wayland/aswlfont_layout.c`
  - glyph measurement, string layout, line breaking, baseline/metrics helpers
- `wayland/aswlfont.c` (or `aswlfont_render.c`)
  - glyph rasterization and render-to-ARGB helpers, plus public API surface

## Refactor steps

- [x] Extract backend/load/cache first.
- [x] Extract layout helpers next.
- [x] Keep `aswlfont.c` as public API + rendering glue.
- [x] Update `wayland/Makefile`.

## Validation

- [x] `make -C wayland`
- [x] End-to-end: `tools/wayland-smoke.sh`
