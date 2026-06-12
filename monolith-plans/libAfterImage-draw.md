# Plan: split `libAfterImage/draw.c`

Snapshot (2026-02-14): ~2004 LOC.

`draw.c` implements the `ASDrawContext` drawing API: brushes/tools, path building, anti-aliased line drawing, ellipse/
circle primitives, and fill operations.

## Why it’s a monolith

- One TU contains multiple algorithm families:
  - context/tool/brush setup and pixel application backends
  - path state machine + line/bezier rendering
  - ellipse/circle primitives
  - flood fill and region fill helpers

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterImage/draw_internal.h`: private helpers shared by the algorithm files (keep `draw.h` public + stable).

Then split (each targeted **< 1000 LOC**):

- `libAfterImage/draw_context.c`
  - `create_asdraw_context`, `destroy_asdraw_context`, `apply_asdraw_context`
  - brush/tool selection (`asim_set_brush`, `asim_set_custom_brush*`)
  - core “apply tool / fill hline” backends
- `libAfterImage/draw_path.c`
  - path APIs (`asim_start_path`, `asim_apply_path`, `asim_move_to`, `asim_line_to`, `asim_line_to_aa`)
  - bezier (`asim_cube_bezier`) and line/path helpers
- `libAfterImage/draw_shapes.c`
  - `asim_rectangle`
  - ellipse/circle primitives (`asim_straight_ellips`, `asim_circle`, `asim_ellips*`)
- `libAfterImage/draw_fill.c`
  - `asim_flood_fill` and any scanline/queue helpers

## Refactor steps (suggested order)

- [ ] Extract fill + shape primitives first (few dependencies).
- [ ] Extract path/line algorithms next.
- [ ] Leave context/tool as the “hub” and extract it last (keeps intermediate builds simple).
- [ ] Update `libAfterImage/Makefile.in` and `libAfterImage/Makefile`.

## Validation

- `make -C libAfterImage`
- End-to-end: `tools/xvfb-smoke.sh`

