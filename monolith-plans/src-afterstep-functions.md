# Plan: split `src/afterstep/functions.c`

Snapshot (2026-02-13): ~1951 LOC.

`src/afterstep/functions.c` contains a large portion of the core “execute AfterStep FunctionData” path, plus many builtin
handlers. Some config-related handlers were already extracted to `function_handlers_config.c`, but the remaining file is
still a multi-domain hotspot.

## Why it’s a monolith

- Handler families (focus/raise, window ops, desk/workspace ops, module spawning, misc utilities) are implemented together.
- Shared state and helper functions make it hard to audit which handlers depend on which subsystems.

## Split targets (phase 1: mechanical extraction)

Keep `ExecuteFunction()` and the public execution entry points stable, and extract handler families into focused modules:

- `src/afterstep/function_handlers_window.c`: window lifecycle/state ops (move/resize, close, shade, maximize, etc.).
- `src/afterstep/function_handlers_focus.c`: focus/raise/warp and related policy.
- `src/afterstep/function_handlers_workspace.c`: desk/workspace switching, viewport, pager-related actions.
- `src/afterstep/function_handlers_modules.c`: module spawn/control, pipes, IPC helpers.
- `src/afterstep/function_handlers_misc.c`: “everything else” (gradually shrinks over time).

Introduce:

- `src/afterstep/functions_internal.h`: internal shared prototypes and small helper declarations.

## Refactor steps (suggested order)

- [x] Create `functions_internal.h` and include it from `functions.c` + new handler files.
- [x] Extract the most self-contained handler family first (often workspace switching / simple wrappers).
- [x] Extract window-op handlers next (usually a large chunk, but cohesive).
- [x] Extract module/IPC-related handlers.
- [x] Extract focus/warp policy handlers.
- [x] Leave `functions.c` as the dispatcher + shared helpers.
- [x] Update `src/afterstep/Makefile.in` and `src/afterstep/Makefile`.

## Validation

- `make -C src/afterstep`
- End-to-end: `tools/xvfb-smoke.sh`

## Implemented (2026-02-18)

- ✅ Split into:
  - `src/afterstep/functions.c` (dispatcher/queue + complex-function runner; keeps public entry points stable).
  - `src/afterstep/function_handlers_config.c` (config/theme/background handlers; already extracted).
  - `src/afterstep/function_handlers_window.c` (window lifecycle/state ops).
  - `src/afterstep/function_handlers_focus.c` (focus/warp/bookmarks policy).
  - `src/afterstep/function_handlers_workspace.c` (desk/workspace/viewport/paging actions).
  - `src/afterstep/function_handlers_modules.c` (module spawn/control + module list helpers).
  - `src/afterstep/function_handlers_misc.c` (misc/utility handlers; gradually shrinks over time).
  - `src/afterstep/functions_internal.h` (shared handler prototypes + shared helper declaration).
- ✅ Snapshot sizes: `functions.c` 732 LOC, `function_handlers_config.c` 526 LOC, `function_handlers_window.c` 236 LOC,
  `function_handlers_focus.c` 50 LOC, `function_handlers_workspace.c` 174 LOC, `function_handlers_modules.c` 119 LOC,
  `function_handlers_misc.c` 600 LOC, `functions_internal.h` 106 LOC.
- ✅ `make -C src/afterstep` and `tools/xvfb-smoke.sh` pass.
