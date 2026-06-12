# Plan: split `libAfterStep/freestor.c`

Snapshot (2026-02-14): ~1879 LOC.

`libAfterStep/freestor.c` is the “kitchen sink” implementation of FreeStorage: element allocation, cleanup, printing,
parsing helpers, and a large family of conversions to/from other config/runtime types.

## Why it’s a monolith

- One TU mixes:
  - FreeStorage element lifecycle (create/dup/destroy, order, cleanup)
  - parser helpers (`parse_context`, modifier/context parsing)
  - `ReadConfigItem` and “interpret argv tokens” logic
  - many conversion families (`free_storage2*` and `*2FreeStorage`)
- Changes to one conversion often require rebuilding/understanding the entire file.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterStep/freestor_internal.h`: internal prototypes + small shared helpers.

Then split into these **4** translation units (each targeted **< 1000 LOC**):

- `libAfterStep/freestor_core.c`
  - `FindTerm`
  - FreeStorage element lifecycle: `CreateFreeStorageElem`, `Add*`, `DupFreeStorageElem`, `CopyFreeStorage`,
    `ReverseFreeStorageOrder`, `StorageCleanUp`, `DestroyFreeStorage`
  - debugging/diagnostics: `freestorage_print`
- `libAfterStep/freestor_parse.c`
  - string-array helpers (`CreateStringArray`, `DupStringArray`, `CompressStringArray`, etc.)
  - context/modifier parsing (`parse_context`, `parse_modifier*`, `parse_win_context*`)
  - `ReadConfigItem` and argument availability checks
- `libAfterStep/freestor_from.c`
  - conversions from storage: `free_storage2func`, `free_storage2geometry`, `free_storage2button`,
    `free_storage2cursor`, `free_storage2binding`, `free_storage2bitlist`, etc.
- `libAfterStep/freestor_to.c`
  - conversions to storage: `Integer2FreeStorage`, `Flags2FreeStorage`, `Strings2FreeStorage`, `Geometry2FreeStorage`,
    `Binding2FreeStorage`, `ASCursor2FreeStorage`, `Bitlist2FreeStorage`
  - struct-driven conversions: `CompositeFlags2FreeStorage`, `StructFlags2FreeStorage`, `StructToFreeStorage`

## Refactor steps (suggested order)

- [ ] Add `freestor_internal.h` and include it from `freestor.c`.
- [ ] Extract `freestor_from.c` and `freestor_to.c` (conversion families are cohesive).
- [ ] Extract `freestor_parse.c`.
- [ ] Leave `freestor_core.c` as the remaining “element API” implementation.
- [ ] Update `libAfterStep/Makefile.in` and `libAfterStep/Makefile`.

## Validation

- `make -C libAfterStep`
- End-to-end: `tools/xvfb-smoke.sh`

