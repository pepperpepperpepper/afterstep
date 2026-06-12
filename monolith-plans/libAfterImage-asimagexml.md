# Plan: split `libAfterImage/asimagexml.c`

Snapshot (2026-02-14): ~2486 LOC.

`asimagexml.c` implements the XML-driven image composition engine used by `ascompose`-style workflows: parsing tags,
building intermediate `ASImage`s, storing results in an `ASImageManager`, and optional display/save helpers.

## Why it’s a monolith

- One TU mixes:
  - public API (`compose_asimage_xml*`, `build_image_from_xml`, `save_asimage_to_file`, `show_asimage`)
  - the recursive “build image from XML” dispatcher
  - ~20 tag handlers (`handle_asxml_tag_*`) spanning unrelated domains (I/O, state/vars, render, transforms)

## Split targets (phase 1: mechanical extraction)

Introduce:

- `libAfterImage/asimagexml_internal.h`: `ASImageXMLState` + shared helpers + cross-TU tag prototypes.

Then split into (each targeted **< 1000 LOC**):

- `libAfterImage/asimagexml_api.c`
  - `set_xml_image_manager`, `set_xml_font_manager`
  - `create_generic_imageman`, `create_generic_fontman`
  - `compose_asimage_xml`, `compose_asimage_xml_at_size`, `compose_asimage_xml_from_doc`
  - `save_asimage_to_file`, `show_asimage`
- `libAfterImage/asimagexml_build.c`
  - `build_image_from_xml` and the tag dispatch logic
  - `commit_xml_image_built`, `translate_tag_size`
- `libAfterImage/asimagexml_tags_source.c`
  - tags that primarily resolve/load/store images: `img`, `recall`, `save`, `background`
- `libAfterImage/asimagexml_tags_state.c`
  - state/vars/mgmt tags: `release`, `color`, `printf`, `set`, `if`/`unless`
- `libAfterImage/asimagexml_tags_render.c`
  - “generate pixels” tags: `text`, `composite`, `gradient`, `solid`
- `libAfterImage/asimagexml_tags_transform.c`
  - transforms: `blur`, `bevel`, `mirror`, `rotate`, `scale`, `slice`, `crop`, `tile`, `hsv`, `pad`,
    `pixelize`, `color2alpha`

Keep `libAfterImage/asimagexml.h` stable (avoid propagating header churn).

## Refactor steps (suggested order)

- [ ] Add `asimagexml_internal.h` and move `ASImageXMLState` there.
- [ ] Extract the tag handler groups first (they are mostly self-contained).
- [ ] Extract the dispatcher + shared helpers (`build_image_from_xml`, `translate_tag_size`, commit).
- [ ] Update `libAfterImage/Makefile.in` and `libAfterImage/Makefile`.

## Validation

- `make -C libAfterImage`
- End-to-end: `tools/xvfb-smoke.sh` (covers `ascompose`-style paths via normal WM startup)

