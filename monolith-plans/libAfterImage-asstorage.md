# Plan: split `libAfterImage/asstorage.c`

Snapshot (2026-02-13): ~2714 LOC.

`asstorage.c` implements memory/storage management for image data, including custom compression and zlib integration.

## Why it’s a monolith

- One file contains multiple distinct concerns:
  - storage IDs/blocks/slots bookkeeping
  - compression implementations (custom RLE-diff + zlib)
  - stats/debug counters and instrumentation
- The compression code is difficult to audit/change without reading unrelated storage bookkeeping.

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterImage/asstorage_internal.h`: internal structs + helpers shared across the new `.c` files.

Then split:

- `libAfterImage/asstorage_core.c`: storage object lifecycle, block/slot management, ID mapping helpers.
- `libAfterImage/asstorage_rlediff.c`: `rlediff_*` bitmap compression helpers.
- `libAfterImage/asstorage_zlib.c`: zlib compress/decompress glue + stream helpers.
- `libAfterImage/asstorage_stats.c`: debug/stats counters (optional; can stay in core if small).

Keep `libAfterImage/asstorage.c` as the public entry point wrapper (or replace it with `asstorage_core.c` in Makefile).

## Refactor steps (suggested order)

- [ ] Extract compression helpers first (RLE-diff and zlib), keeping APIs private.
- [ ] Extract stats/debug code if it meaningfully reduces the core file.
- [ ] Keep `asstorage.h` unchanged as much as possible (avoid cascading rebuilds).
- [ ] Update `libAfterImage/Makefile.in` and `libAfterImage/Makefile`.

## Validation

- `make -C libAfterImage`
- End-to-end: `tools/xvfb-smoke.sh` (covers libAfterImage usage in normal WM startup)

