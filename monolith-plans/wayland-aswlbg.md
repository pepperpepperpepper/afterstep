# Plan: split `wayland/aswlbg.c`

Snapshot (2026-02-14): ~1461 LOC.

`aswlbg.c` appears to mix wl registry/surface glue, background configuration, and rendering into one TU.

Implemented (2026-02-18):

- ✅ Split into `aswlbg_wl.c` (529 LOC), `aswlbg_render.c` (947 LOC), and `aswlbg_internal.h` (53 LOC).
- ✅ Kept `aswlbg.c` as the thin `main()` wrapper (12 LOC).

## Split targets (phase 1: mechanical extraction)

Introduce:

- `wayland/aswlbg_internal.h`: internal prototypes and shared state.

Then split into **3** translation units (each targeted **< 1000 LOC**):

- `wayland/aswlbg_wl.c`
  - registry binding, surface/layer-shell wiring, frame callbacks
- `wayland/aswlbg_render.c`
  - background render path (color/image/scale) and buffer management glue
- `wayland/aswlbg.c` (or `aswlbg_main.c`)
  - CLI + orchestration and “apply config” glue

## Refactor steps

- [x] Extract wl glue first.
- [x] Extract render/buffers next.
- [x] Update `wayland/Makefile`.

## Validation

- `make -C wayland`
- End-to-end: `tools/wayland-smoke.sh`
