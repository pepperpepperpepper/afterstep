# Plan: split `libAfterImage/afterbase.c`

Snapshot (2026-02-14): ~2121 LOC.

`afterbase.c` is the “asim_” utility layer for libAfterImage: logging, file/path helpers, envvar expansion, string
helpers, ARGB parsing, math parsing (with `$var` lookup), hash table helpers, and an XML parser/pretty-printer.

## Why it’s a monolith

- “Base utilities” grew to include several mini-subsystems:
  - filesystem/path/envvar utilities
  - string + ctrl-code interpretation
  - color + math parsing + `asxml_var_*`
  - hash table + memory pool helpers
  - XML parsing + printing
- Small changes require re-reviewing unrelated logic (e.g., XML parser vs hash tables).

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterImage/afterbase_internal.h`: small internal prototypes shared across the new `.c` files.

Then split (each targeted **< 1000 LOC**):

- `libAfterImage/afterbase_util.c`
  - `asim_set_application_name`, `asim_get_application_name`
  - `asim_show_*` logging helpers
  - filesystem/path helpers (`asim_check_file_mode`, `asim_put_file_home`, `asim_find_file`, `asim_load_*`)
  - envvar expansion (`asim_copy_replace_envvar` + helpers)
  - string helpers (`asim_mystrdup`, `asim_mystrcasecmp`, etc.)
- `libAfterImage/afterbase_color_math.c`
  - `asim_parse_argb_color`
  - `asim_parse_math`
  - `asim_asxml_var_{init,insert,get,cleanup}` and the `$var` lookup helpers used by the math parser
- `libAfterImage/afterbase_hash.c`
  - hash funcs + memory pool helpers (`asim_default_hash_func`, pointer hash, pool flush, etc.)
- `libAfterImage/afterbase_xml.c`
  - XML parser + helpers (`asim_xml_parse_*`, `asim_xml_elem_delete`, `asim_xml_print`, XML buffer helpers)

Keep `libAfterImage/afterbase.h` stable where possible; avoid moving unrelated public declarations between headers.

## Refactor steps (suggested order)

- [ ] Extract XML code first (biggest cohesive block; has clear function prefixing).
- [ ] Extract hash table helpers next (shared dependency for XML vocabulary tables).
- [ ] Extract color/math/vars next (used by XML-driven tools).
- [ ] Leave `afterbase_util.c` as the “rest” bucket; split further only if it still grows past ~900 LOC.
- [ ] Update `libAfterImage/Makefile.in` and `libAfterImage/Makefile`.

## Validation

- `make -C libAfterImage`
- End-to-end: `tools/xvfb-smoke.sh`

