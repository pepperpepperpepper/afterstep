# Plan: split `src/afterstep/afterstep.c`

Snapshot (2026-02-14): ~1063 LOC.

`afterstep.c` is the core WM’s “main TU” and tends to accumulate initialization and event-loop wiring.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `src/afterstep/afterstep_internal.h`: internal prototypes/shared state.

Then split into **2–3** translation units (each targeted **< 1000 LOC**):

- `src/afterstep/afterstep_init.c`
  - subsystem initialization/teardown wiring
- `src/afterstep/afterstep_loop.c`
  - main loop/event dispatch glue
- `src/afterstep/afterstep.c` (or `afterstep_main.c`)
  - `main()` / top-level orchestration and minimal glue

## Refactor steps

- [ ] Extract init/teardown first (mechanical).
- [ ] Extract event-loop wiring next.
- [ ] Update `src/afterstep/Makefile.in` and `src/afterstep/Makefile`.

## Validation

- `make -C src/afterstep`
- End-to-end: `tools/xvfb-smoke.sh`

