# Plan: split `libAfterStep/decor.c`

Snapshot (2026-02-14): ~2004 LOC.

`decor.c` implements a large slice of titlebar/decorations infrastructure: button data, tile layout, bar rendering,
balloon/pointer interactions, and assorted style/image integration.

## Why it’s a monolith

- One TU mixes:
  - button and button-block lifecycle (`ASTBtnData`, `ASBtnBlock`)
  - tile/layout bookkeeping (`ASTile`, padding/resize rules, add/remove tile helpers)
  - rendering (`render_astbar*`, cached backgrounds, transparency/shaping)
  - interaction state (focused bar tracking, pointer actions, balloons)

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterStep/decor_internal.h`: small internal prototypes shared across the new `.c` files.

Then split (each targeted **< 1000 LOC**):

- `libAfterStep/decor_buttons.c`
  - `create_astbtn`, `destroy_astbtn`, `set_tbtn_images`, `make_tbtn`
  - button-block helpers (`free_asbtn_block`, `add_astbar_btnblock` wiring where appropriate)
- `libAfterStep/decor_tiles.c`
  - `ASTile` lifecycle + add/remove helpers (`add_astbar_*`, `delete_astbar_tile`)
  - size calculations (`calculate_astbar_*`, tile pad/resize helpers)
- `libAfterStep/decor_render.c`
  - `render_astbar*`, cache helpers, transparency/shaping helpers
- `libAfterStep/decor_interact.c`
  - focus/pointer/balloon state (`on_astbar_pointer_action`, `set_astbar_balloon*`, focused-bar tracking)

Keep `libAfterStep/decor.h` API stable; internal header should carry only cross-file helpers.

## Refactor steps (suggested order)

- [ ] Extract button helpers first (low coupling).
- [ ] Extract rendering next (big cohesive block; makes the remaining TU easier to reason about).
- [ ] Extract tile/layout helpers.
- [ ] Extract interaction/balloon glue.
- [ ] Update `libAfterStep/Makefile.in` and `libAfterStep/Makefile`.

## Validation

- `make -C libAfterStep`
- End-to-end: `tools/xvfb-smoke.sh`

