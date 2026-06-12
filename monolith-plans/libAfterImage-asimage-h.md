# Plan: split `libAfterImage/asimage.h`

Snapshot (2026-02-14): ~1122 LOC.

`asimage.h` is a large public umbrella header that mixes types, core API, helper macros, and assorted format-related
details. This increases include churn and makes it hard to find the “right” declaration.

## Split targets (phase 1: mechanical extraction)

Keep `libAfterImage/asimage.h` as an umbrella header for backward compatibility, but move content into focused headers
and include them from the umbrella.

Suggested new headers:

- `libAfterImage/asimage_types.h` (structs/enums/typedefs)
- `libAfterImage/asimage_api.h` (public lifecycle + core API)
- `libAfterImage/asimage_scanline.h` (scanline/channel access)
- `libAfterImage/asimage_format.h` (format constants/encoding helpers)

## Refactor steps

- [ ] Create focused headers and move declarations mechanically.
- [ ] Keep the umbrella header including the new headers in a stable order.
- [ ] Fix include users incrementally (optional; not required initially).

## Validation

- Full rebuild recommended: `make -j\"$(nproc)\"`

