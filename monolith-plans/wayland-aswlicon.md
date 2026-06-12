# Plan: split `wayland/aswlicon.c`

Snapshot (2026-02-17): split (remaining: `aswlicon.c` 67 LOC, `aswlicon_resolve.c` 526 LOC, `aswlicon_png.c` 156 LOC,
`aswlicon_xml.c` 599 LOC, `aswlicon_afterimage.c` 789 LOC, `aswlicon_internal.h` 55 LOC).

`aswlicon.c` loads ARGB icons from an AfterStep-style icon spec, XDG icon names, or explicit paths. It also handles
png decoding, icon theme lookups, and composite/icon-XML parsing.

## Why it’s a monolith

- One TU mixes:
  - XDG theme discovery + search-path resolution
  - file kind detection + tilde/env expansion + “try suffix variants”
  - png decoding (libpng) + optional libAfterImage fallback
  - compositing/blending/scaling helpers and recursive “spec” evaluation

## Split targets (phase 1: mechanical extraction)

Introduce:

- `wayland/aswlicon_internal.h`: internal helpers shared by the new `.c` files.

Then split (each targeted **< 1000 LOC**):

- `wayland/aswlicon_resolve.c`
  - spec/path resolution (`~` expansion, AfterStep icon roots, suffix variants)
  - XDG theme lookup (`ASWL_ICON_THEME`, gtk settings.ini parsing, icon root search)
  - `aswl_detect_icon_kind`
- `wayland/aswlicon_png.c`
  - `#ifdef HAVE_LIBPNG` png decode to ARGB (`aswl_load_png_argb`) + libpng error/warn hooks
- `wayland/aswlicon_xml.c`
  - small XML-ish parsing helpers (`aswl_xml_attr_*`, tag scanning)
  - blend/composite helpers (`aswl_blend_over`, composite list parsing, layer application, scaling/cropping)
- `wayland/aswlicon_afterimage.c` (optional, gated by `HAVE_AFTERIMAGE`)
  - file/XML load through libAfterImage when available
- `wayland/aswlicon.c` (or `wayland/aswlicon_api.c`)
  - keep `aswl_icon_load_argb()` and the high-level dispatch thin

## Refactor steps (suggested order)

- [x] Extract `aswlicon_png.c` first (self-contained, `#ifdef HAVE_LIBPNG`).
- [x] Extract resolution + XDG logic (`aswlicon_resolve.c`).
- [x] Extract XML/compositing (`aswlicon_xml.c`).
- [x] Extract AfterImage integration (`aswlicon_afterimage.c`).
- [x] Keep `aswlicon.c` thin and update `wayland/Makefile`.

## Validation

- `make -C wayland`
- `tools/wayland-smoke.sh`
