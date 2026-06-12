# Plan: split `wayland/aswlpanel.c`

Snapshot (2026-02-16): `wayland/aswlpanel_main.c` = 168 LOC (+ `aswlpanel_wl.c` 662 LOC, `aswlpanel_buffers.c` 156 LOC,
`aswlpanel_model.c` 743 LOC, `aswlpanel_config.c` 768 LOC, `aswlpanel_render.c` 790 LOC, `aswlpanel_paint.c` 836 LOC,
`aswlpanel_internal.h` 272 LOC; `wayland/aswlpanel.c` removed).

`aswlpanel` is a Wayland client used as the AfterStep-themed panel/pager/dock UI (xdg-toplevel and/or layer-shell).

## Why it’s a monolith

- One file owns the Wayland client lifecycle (registry, globals, surface, seat/pointer), shm buffer management, UI model
  (buttons + window list + pager), config parsing, and rendering.
- Multiple “modes” (dock vs pager vs window list) are interleaved with shared state in one `struct as_state`.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `wayland/aswlpanel_internal.h`:
  - `struct as_state`, buffer structs, and shared helpers

Then split (phase 1 goal: **7** translation units, each **< 1000 LOC**):

- `wayland/aswlpanel_main.c`: argument parsing + top-level setup + main loop.
- `wayland/aswlpanel_wl.c`: Wayland glue (registry binding, globals, surface setup, frame callbacks, seat/pointer events).
- `wayland/aswlpanel_buffers.c`: shm buffer allocation/swap/destroy (`struct as_buffer`).
- `wayland/aswlpanel_config.c`: config parsing + button model/directives (env + config file).
- `wayland/aswlpanel_model.c`: window list/workspace state + layout inputs + hit testing + command dispatch.
- `wayland/aswlpanel_render.c`: top-level draw + commit pipeline.
- `wayland/aswlpanel_paint.c`: paint primitives, caches, gradients, bg snapshot sampling (split from render to keep files < 1000 LOC).

If a phase 1 file threatens to exceed 1000 LOC, split further in phase 2:

- `aswlpanel_bg_snapshot.c`: background snapshot mapping/sampling.
- `aswlpanel_input.c`: click/command dispatch separate from generic wl pointer plumbing.

## Refactor steps (suggested order)

- [x] Add `aswlpanel_internal.h` and relocate `struct as_state` + shared declarations.
- [x] Extract shm buffer code first (`struct as_buffer` helpers) into `aswlpanel_buffers.c`.
- [x] Extract model into `aswlpanel_model.c` and config parsing into `aswlpanel_config.c`.
- [x] Extract rendering into `aswlpanel_render.c` + `aswlpanel_paint.c` (split to keep files < 1000 LOC).
- [x] Extract Wayland registry/globals glue into `aswlpanel_wl.c`.
- [x] Leave `aswlpanel_main.c` as the orchestrator and update `wayland/Makefile`.

## Validation

- `make -C wayland`
- `tools/wayland-smoke.sh` (covers basic suite startup in the nested harness)
