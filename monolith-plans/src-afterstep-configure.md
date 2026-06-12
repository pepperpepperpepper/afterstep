# Plan: split `src/afterstep/configure.c`

Snapshot (2026-02-13): ~2683 LOC.

`src/afterstep/configure.c` is the core WM configuration load/apply path. It contains parsing, merge/apply logic, and
feature-specific wiring that makes it hard to change/extend configuration behavior without touching unrelated areas.

## Why it’s a monolith

- “Read config from disk”, “merge”, and “apply to running WM state” are interleaved.
- Multiple distinct config families live together: feel/look, categories, desktop props, theme integration, restart hooks.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `src/afterstep/configure_internal.h`: internal shared declarations between the new `.c` files.

Then split:

- `src/afterstep/configure_load.c`: disk IO + parser invocation + building intermediate config objects.
- `src/afterstep/configure_merge.c`: merge logic for config structures (pure-ish code where possible).
- `src/afterstep/configure_apply.c`: “apply to live WM” code (side-effects concentrated here).
- `src/afterstep/configure_restart.c`: restart/quickrestart wiring + reload hooks (currently spread across config + functions).

Keep `src/afterstep/configure.c` as the high-level orchestrator (public entry points stay here, calling into the new units).

## Refactor steps (suggested order)

- [x] Create `configure_internal.h` and move internal-only prototypes out of `configure.c`.
- [x] Extract merge helpers first (least risky; easiest to keep behavior identical).
- [x] Extract “load/build config object” paths next.
- [x] Extract “apply to live state” paths last (most side-effects).
- [x] Update `src/afterstep/Makefile.in` and `src/afterstep/Makefile`.

## Validation

- `make -C src/afterstep`
- End-to-end: `tools/xvfb-smoke.sh`

## Implemented (2026-02-18)

- ✅ Split into:
  - `src/afterstep/configure.c` (orchestrator; keeps `LoadASConfig()`).
  - `src/afterstep/configure_load.c` (parser table + file IO + per-line handlers).
  - `src/afterstep/configure_apply.c` (legacy look state + look fixups + misc helpers).
  - `src/afterstep/configure_merge.c` (merge helpers).
  - `src/afterstep/configure_restart.c` (`QuickRestart()`).
  - `src/afterstep/configure_internal.h` (shared prototypes + shared legacy parser state).
- ✅ Snapshot sizes: `configure.c` 678 LOC, `configure_load.c` 992 LOC, `configure_apply.c` 937 LOC,
  `configure_merge.c` 75 LOC, `configure_restart.c` 44 LOC, `configure_internal.h` 61 LOC.
- ✅ `make -C src/afterstep` and `tools/xvfb-smoke.sh` pass.
