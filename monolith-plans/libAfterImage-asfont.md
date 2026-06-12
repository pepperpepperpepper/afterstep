# Plan: split `libAfterImage/asfont.c`

Snapshot (2026-02-14): ~1487 LOC.

`asfont.c` is libAfterImage’s font backend and layout/measurement helper implementation. It tends to couple backend
loading with high-level text drawing.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterImage/asfont_internal.h`: internal prototypes shared across new `.c` files.

Then split into **3** translation units (each targeted **< 1000 LOC**):

- `libAfterImage/asfont_backend.c`
  - backend/font loading and caching (freetype/fontconfig glue)
- `libAfterImage/asfont_layout.c`
  - glyph metrics and string measurement/layout helpers
- `libAfterImage/asfont.c` (or `asfont_render.c`)
  - rasterization/drawing helpers + public API glue

## Refactor steps

- [ ] Extract backend/load/cache first.
- [ ] Extract layout helpers next.
- [ ] Update `libAfterImage/Makefile.in` and `libAfterImage/Makefile`.

## Validation

- `make -C libAfterImage`
- End-to-end: `tools/xvfb-smoke.sh`

