# Plan: split `wayland/aswlcomp.c`

Snapshot (2026-02-15): entrypoint in `wayland/aswlcomp_main.c` 425 LOC (`wayland/aswlcomp_decor.c`: 999 LOC, `wayland/aswlcomp_input.c`: 950 LOC, `wayland/aswlcomp_toplevel.c`: 906 LOC, `wayland/aswlcomp_control_lock.c`: 898 LOC, `wayland/aswlcomp_state.c`: 825 LOC, `wayland/aswlcomp_input_config.c`: 773 LOC, `wayland/aswlcomp_server.c`: 711 LOC, `wayland/aswlcomp_view.c`: 639 LOC, `wayland/aswlcomp_ime.c`: 585 LOC, `wayland/aswlcomp_dock.c`: 469 LOC, `wayland/aswlcomp_output.c`: 307 LOC, `wayland/aswlcomp_layer.c`: 234 LOC, `wayland/aswlcomp_toplevel_protocols.c`: 202 LOC).

`aswlcomp` is the wlroots-based Wayland compositor for the AfterStep Wayland track. Today it concentrates compositor
policy/state, protocol wiring, rendering/scene setup, and suite integration into one translation unit.

## Why it’s a monolith

- `struct aswl_server` owns **almost everything**: outputs/layout, views, seat/input, protocol globals, IME, idle/lock,
  screencopy/dmabuf, decoration assets, docking rules, state persistence, and suite control protocol.
- Large numbers of file-scope statics create implicit coupling (hard to reason about ownership + invariants).
- Workstreams that should be independently maintainable (e.g., IME vs output-management vs decorations) are interleaved.

## Split targets (phase 1: mechanical extraction)

Goal: break the file into focused `.c` units while preserving all behavior and public surfaces.

Introduce:

- `wayland/aswlcomp_internal.h`:
  - `struct aswl_server`, `struct aswl_view`, `struct aswl_output`, and other internal structs currently defined in
    `aswlcomp.c`
  - internal helpers/prototypes shared between the new units

Then split by subsystem (phase 1 goal: **13–14** translation units, each **< 1000 LOC**):

- `wayland/aswlcomp_main.c`: argument parsing (`--socket`, `--autostart`, `--state`, `--spawn`), env parsing, init + run.
- `wayland/aswlcomp_server.c`: server init/destroy, wl_display/wl_event_loop glue, scene/renderer/allocator, global protocol creation.
- `wayland/aswlcomp_output.c` + `wayland/aswlcomp_state.c`: outputs + layout, output-manager v1 apply/test, persistence/state file (split as needed to keep each file < 1000 LOC).
- `wayland/aswlcomp_layer.c`: layer-shell surfaces, “usable box” computation, `arrange_layers*()`.
- `wayland/aswlcomp_view.c`: view lookup + focus + placement + workspace policy.
- `wayland/aswlcomp_dock.c`: dockapp detection + arrangement.
- `wayland/aswlcomp_toplevel.c`: xdg-toplevel/xwayland wiring + request handlers + view lifecycle listeners.
- `wayland/aswlcomp_toplevel_protocols.c`: foreign/ext-foreign toplevel protocols (create/update/destroy toplevel handles).
- `wayland/aswlcomp_decor.c`: decoration assets + titlebar rendering + hit-testing for close.
- `wayland/aswlcomp_input.c` + `wayland/aswlcomp_input_config.c`: seat/cursor, keyboard/pointer config, bindings, interactive move/resize, idle/lock activity plumbing.
- `wayland/aswlcomp_ime.c`: text-input-v3 + input-method-v2 + popups (virtual keyboard can stay in input if it shares helpers).
- `wayland/aswlcomp_control_lock.c`: `afterstep-control-v1` protocol marshalling + session-lock glue (split later if it exceeds 1000 LOC).

Notes:

- screencopy/export-dmabuf can live in `aswlcomp_server.c` or `aswlcomp_output.c` (whichever keeps the files < 1000 LOC).
- idle-inhibit plumbing can live in `aswlcomp_input_config.c` or `aswlcomp_control_lock.c` (same rule).

Entrypoint now lives in `wayland/aswlcomp_main.c`. The legacy monolith file `wayland/aswlcomp.c` has been removed from
the build (and deleted).

## Refactor steps (suggested order)

- [x] Add `aswlcomp_internal.h` and adjust includes to use it.
- [x] Move the IME block (`aswl_ime_*`, text-input/input-method listeners) into `aswlcomp_ime.c` first (usually the most
  self-contained).
- [x] Move decoration rendering + assets into `aswlcomp_decor.c`.
- [x] Move layer-shell layout/arrangement into `aswlcomp_layer.c`.
- [x] Move output persistence + output-management apply/test handlers into `aswlcomp_output.c` (+ `aswlcomp_state.c` to keep each file < 1000 LOC).
- [x] Move suite control protocol + session-lock glue into `aswlcomp_control_lock.c`.
- [x] Move input/cursor/bindings + interactive move/resize into `aswlcomp_input.c` (+ `aswlcomp_input_config.c` to keep each file < 1000 LOC).
- [x] Move view focus/placement/workspaces into `aswlcomp_view.c` (+ `aswlcomp_dock.c` to keep each file < 1000 LOC).
- [x] Move xdg/xwayland toplevel protocol glue into `aswlcomp_toplevel.c` (+ `aswlcomp_toplevel_protocols.c` to keep files < 1000 LOC).
- [x] Split server init/destroy + protocol global creation into `aswlcomp_server.c`.
- [x] Split CLI parsing + `main()` into `aswlcomp_main.c` (and drop the legacy `aswlcomp.c` from the build).
- [x] Update `wayland/Makefile` to compile/link the new objects (incrementally per extraction).
- [x] Delete transitional `#if 0` blocks once each moved chunk is validated (legacy `aswlcomp.c` removed).

## Phase 2 (optional): reduce coupling further

- Split `struct aswl_server` into sub-structs (`aswl_outputs`, `aswl_input`, `aswl_protocols`) so most modules can include
  only what they need.
- Promote shared suite helpers into a small `wayland/aswlutil.[ch]` used by `aswlmenu`, `aswlpanel`, etc.

## Validation

- `make -C wayland`
- `tools/wayland-smoke.sh`
- Visual sanity (optional but useful for regressions): `tools/wayland-screenshots.sh`
