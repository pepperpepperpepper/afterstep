# Plan: split `libAfterStep/asapp.c`

Snapshot (2026-02-14): ~1690 LOC.

`asapp.c` mixes global application state, directory defaults, X11/screen globals, argument plumbing, and a large function
term table in one TU.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterStep/asapp_internal.h`: internal prototypes and shared globals.

Then split into **3** translation units (each targeted **< 1000 LOC**):

- `libAfterStep/asapp_args.c`
  - `ASProgArgs` handling and command-line parsing helpers
  - `MyName`/`MyClass` setup helpers and usage/version hooks
- `libAfterStep/asapp_paths.c`
  - default directory names (`as_*_dir_name`) and environment/path utilities
  - session/env discovery helpers (where possible)
- `libAfterStep/asapp.c` (keep filename for build stability)
  - X11/screen globals (`dpy`, `ASDefaultScr`, fd bookkeeping)
  - function term registry tables (keep as pure data where possible)
  - minimal glue that exposes the public `asapp.h` API

## Refactor steps

- [ ] Add `asapp_internal.h`.
- [ ] Extract `asapp_args.c` (self-contained).
- [ ] Extract directory/path logic (`asapp_paths.c`).
- [ ] Update `libAfterStep/Makefile.in` and `libAfterStep/Makefile`.

## Validation

- `make -C libAfterStep`
- End-to-end: `tools/xvfb-smoke.sh`

