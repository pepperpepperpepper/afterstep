# Monolith plan (≤ 1000 LOC per file)

Date: **2026-02-14**

Last updated: **2026-02-18**

Current priority (finish core WM remaining monoliths):

- `src/afterstep/window_frame.c` (~1452 LOC) → plan: `monolith-plans/src-afterstep-window_frame.md`
- `src/afterstep/module.c` (~1236 LOC) → plan: `monolith-plans/src-afterstep-module.md`
- `src/afterstep/dbus.c` (~1306 LOC) → plan: `monolith-plans/src-afterstep-dbus.md`
- `src/afterstep/afterstep.c` (~1063 LOC) → plan: `monolith-plans/src-afterstep-afterstep.md`

This plan started by focusing on the worst Wayland monoliths first:

- `wayland/aswlcomp.c` (~8500 LOC) → **13–14 files** (+ 1 internal header), each targeted **< 1000 LOC**
- `wayland/aswlpanel.c` (~4200 LOC) → **6 files** (+ 1 internal header), each targeted **< 1000 LOC**
- `wayland/aswlmenu.c` (~4100 LOC) → **6–7 files** (+ 1 internal header), each targeted **< 1000 LOC**

Principles:

- **Mechanical extraction first** (move code without changing behavior), then cleanup in follow-up patches.
- Keep “public” surfaces stable: binaries stay `aswlcomp` / `aswlpanel`; keep command-line semantics unchanged.
- Prefer **domain ownership**: output code lives with outputs, view code lives with views, etc.
- Avoid include sprawl: introduce one `*_internal.h` and keep everything else `static` unless a cross-file call is needed.

## Workflow (split playbook)

When splitting a monolith, the goal is to keep changes *mechanical*:

1. Add `*_internal.h` to hold shared structs + cross-file prototypes (keep most functions `static`).
2. Extract the most self-contained leaf area first (usually a protocol, renderer, or parser “island”).
3. After each extraction:
   - update the directory `Makefile` / `Makefile.in` to build the new `*.o`
   - build + run the smallest relevant smoke test
4. Only after the file is under control (<1000 LOC per `*.c`) do readability cleanups (naming, deduping helpers).

### Quick LOC check (first-party only)

We use physical line count (`wc -l`) for the monolith threshold.

```sh
# List first-party monoliths >=1000 LOC (exclude vendor + generated).
git ls-files '*.c' '*.h' \
  | rg -v '^(libAfterImage/(libpng|libjpeg|zlib)/|perl-AfterImage/)' \
  | rg -v '^src/Script/(lex\\.yy\\.c|y\\.tab\\.c)$' \
  | xargs wc -l \
  | awk '$1 >= 1000 {print}' \
  | sort -nr
```

## Taxonomy (largest first-party monoliths)

These are the biggest *project-owned* single-file hotspots (excluding bundled third-party libraries and generated
lexer/parser outputs). The detailed per-file breakup notes live under `monolith-plans/`.
For the longer list (including “do not refactor” vendor/generated buckets), see `monolith-plans/README.md`.

| LOC | File | Subsystem | Planned split (each <1000 LOC) | Plan |
| ---: | --- | --- | --- | --- |
| 8524 | `wayland/aswlcomp.c` | Wayland compositor | **10 `.c`** + internal header | `monolith-plans/wayland-aswlcomp.md` |
| 4228 | `wayland/aswlpanel.c` | Wayland suite UI | **6 `.c`** + internal header | `monolith-plans/wayland-aswlpanel.md` |
| 4097 | `wayland/aswlmenu.c` | Wayland suite UI | **6–7 `.c`** + internal header | `monolith-plans/wayland-aswlmenu.md` |
| 3116 | `src/Wharf/Wharf.c` | X11 module | **~8 `.c`** + internal header | `monolith-plans/src-wharf.md` |
| 2714 | `libAfterImage/asstorage.c` | libAfterImage | **3–4 `.c`** + internal header | `monolith-plans/libAfterImage-asstorage.md` |
| 2683 | `src/afterstep/configure.c` | core WM (X11) | **5 `.c`** + internal header | `monolith-plans/src-afterstep-configure.md` |
| 2679 | `libAfterConf/afterconf.h` | config system | umbrella → **focused headers** (keep umbrella) | `monolith-plans/libAfterConf-afterconf-h.md` |
| 2486 | `libAfterImage/asimagexml.c` | libAfterImage | **6 `.c`** + internal header | `monolith-plans/libAfterImage-asimagexml.md` |
| 2121 | `libAfterImage/afterbase.c` | libAfterImage | **4 `.c`** + internal header | `monolith-plans/libAfterImage-afterbase.md` |
| 2120 | `wayland/aswlicon.c` | Wayland suite UI | **4 `.c`** + internal header | `monolith-plans/wayland-aswlicon.md` |
| 2044 | `src/Form/Form.c` | X11 module | **4 `.c`** + internal header | `monolith-plans/src-form.md` |
| 2004 | `libAfterStep/decor.c` | libAfterStep | **4 `.c`** + internal header | `monolith-plans/libAfterStep-decor.md` |
| 2004 | `libAfterImage/draw.c` | libAfterImage | **4 `.c`** + internal header | `monolith-plans/libAfterImage-draw.md` |
| 1951 | `src/afterstep/functions.c` | core WM (X11) | **7 `.c`** + internal header | `monolith-plans/src-afterstep-functions.md` |
| 1911 | `src/afterstep/placement.c` | core WM (X11) | **4 `.c`** + internal header | `monolith-plans/src-afterstep-placement.md` |
| 1879 | `libAfterStep/freestor.c` | config system | **4 `.c`** + internal header | `monolith-plans/libAfterStep-freestor.md` |
| 1843 | `wayland/aswltheme.c` | Wayland suite UI | **4 `.c`** + internal header | `monolith-plans/wayland-aswltheme.md` |
| 1800 | `src/afterstep/menus.c` | core WM (X11) | **4 `.c`** + internal header | `monolith-plans/src-afterstep-menus.md` |
| 1764 | `libAfterStep/hints_common.c` | libAfterStep | **3 `.c`** + internal header | `monolith-plans/libAfterStep-hints_common.md` |
| 1690 | `libAfterStep/asapp.c` | libAfterStep | **3 `.c`** + internal header | `monolith-plans/libAfterStep-asapp.md` |
| 1661 | `src/WinList2/WinList.c` | X11 module | **3 `.c`** + internal header | `monolith-plans/src-winlist2.md` |
| 1573 | `libAfterImage/asimage.c` | libAfterImage | **3 `.c`** + internal header | `monolith-plans/libAfterImage-asimage.md` |
| 1552 | `libAfterImage/imencdec.c` | libAfterImage | **3 `.c`** + internal header | `monolith-plans/libAfterImage-imencdec.md` |
| 1550 | `libAfterStep/clientprops.c` | libAfterStep | **3 `.c`** + internal header | `monolith-plans/libAfterStep-clientprops.md` |
| 1549 | `libAfterStep/mystyle.c` | libAfterStep | **3 `.c`** + internal header | `monolith-plans/libAfterStep-mystyle.md` |
| 1497 | `src/afterstep/decorations.c` | core WM (X11) | **3 `.c`** + internal header | `monolith-plans/src-afterstep-decorations.md` |
| 1496 | `wayland/aswlfont.c` | Wayland suite UI | **3 `.c`** + internal header | `monolith-plans/wayland-aswlfont.md` |
| 1487 | `libAfterImage/asfont.c` | libAfterImage | **3 `.c`** + internal header | `monolith-plans/libAfterImage-asfont.md` |
| 1479 | `src/afterstep/pager.c` | core WM (X11) | **3 `.c`** | `monolith-plans/src-afterstep-pager.md` |
| 1475 | `src/ASMount/main.c` | X11 module/tool | **2–3 `.c`** + internal header | `monolith-plans/src-asmount.md` |
| 1461 | `wayland/aswlbg.c` | Wayland suite UI | **2–3 `.c`** + internal header | `monolith-plans/wayland-aswlbg.md` |
| 1452 | `src/afterstep/window_frame.c` | core WM (X11) | **2–3 `.c`** + internal header | `monolith-plans/src-afterstep-window_frame.md` |
| 1435 | `libAfterStep/wmprops.c` | libAfterStep | **2–3 `.c`** + internal header | `monolith-plans/libAfterStep-wmprops.md` |
| 1419 | `libAfterBase/parse.c` | libAfterBase | **2–3 `.c`** + internal header | `monolith-plans/libAfterBase-parse.md` |
| 1407 | `libAfterImage/import.c` | libAfterImage | **2–3 `.c`** + internal header | `monolith-plans/libAfterImage-import.md` |
| 1399 | `src/Script/Instructions.c` | scripting | **2 `.c`** + internal header | `monolith-plans/src-script-instructions.md` |
| 1326 | `src/Gnome/Gnome.c` | X11 module | **2 `.c`** + internal header | `monolith-plans/src-gnome.md` |
| 1306 | `src/afterstep/dbus.c` | core WM (X11) | **2 `.c`** + internal header | `monolith-plans/src-afterstep-dbus.md` |
| 1289 | `libAfterImage/export.c` | libAfterImage | **2–3 `.c`** + internal header | `monolith-plans/libAfterImage-export.md` |
| 1279 | `src/ASConfig/ASConfig.c` | tool/module | **2–3 `.c`** + internal header | `monolith-plans/src-asconfig.md` |
| 1236 | `src/afterstep/module.c` | core WM (X11) | **2 `.c`** + internal header | `monolith-plans/src-afterstep-module.md` |
| 1215 | `libAfterImage/apps/ascompose.c` | tool | **2 `.c`** + internal header | `monolith-plans/libAfterImage-ascompose.md` |
| 1165 | `libAfterBase/audit.c` | libAfterBase | **2 `.c`** + internal header | `monolith-plans/libAfterBase-audit.md` |
| 1150 | `libAfterStep/session.c` | libAfterStep | **2 `.c`** + internal header | `monolith-plans/libAfterStep-session.md` |
| 1147 | `libAfterBase/layout.c` | libAfterBase | **2 `.c`** + internal header | `monolith-plans/libAfterBase-layout.md` |
| 1132 | `libAfterStep/canvas.c` | libAfterStep | **2 `.c`** + internal header | `monolith-plans/libAfterStep-canvas.md` |
| 1128 | `libAfterStep/desktop_category.c` | libAfterStep | **2 `.c`** + internal header | `monolith-plans/libAfterStep-desktop_category.md` |
| 1122 | `libAfterImage/asimage.h` | libAfterImage | umbrella → **focused headers** (keep umbrella) | `monolith-plans/libAfterImage-asimage-h.md` |
| 1109 | `libAfterConf/AfterStep.c` | config system | **2 `.c`** + internal header | `monolith-plans/libAfterConf-AfterStep.md` |
| 1108 | `libASGTK/asgtkxmleditor.c` | GTK library | **2 `.c`** + internal header | `monolith-plans/libASGTK-asgtkxmleditor.md` |
| 1106 | `libAfterStep/moveresize.c` | libAfterStep | **2 `.c`** + internal header | `monolith-plans/libAfterStep-moveresize.md` |
| 1092 | `src/ASDocGen/xmlproc.c` | tool | **2 `.c`** + internal header | `monolith-plans/src-asdocgen-xmlproc.md` |
| 1083 | `libAfterStep/functions.c` | libAfterStep | **2 `.c`** + internal header | `monolith-plans/libAfterStep-functions.md` |
| 1070 | `src/asetroot/asetroot.c` | tool | **2 `.c`** + internal header | `monolith-plans/src-asetroot.md` |
| 1063 | `src/afterstep/afterstep.c` | core WM (X11) | **2–3 `.c`** + internal header | `monolith-plans/src-afterstep-afterstep.md` |
| 1050 | `src/ASDocGen/ASDocGen.c` | tool | **2 `.c`** + internal header | `monolith-plans/src-asdocgen-main.md` |
| 1034 | `wayland/aswllock.c` | Wayland suite UI | **2 `.c`** + internal header | `monolith-plans/wayland-aswllock.md` |
| 1027 | `src/Ident/Ident.c` | X11 module | **2 `.c`** + internal header | `monolith-plans/src-ident.md` |
| 1022 | `libAfterBase/regexp.c` | libAfterBase | **2 `.c`** + internal header | `monolith-plans/libAfterBase-regexp.md` |
| 1020 | `libAfterBase/xml.c` | libAfterBase | **2 `.c`** + internal header | `monolith-plans/libAfterBase-xml.md` |
| 1001 | `libAfterStep/parser.c` | libAfterStep | **2 `.c`** + internal header | `monolith-plans/libAfterStep-parser.md` |

## Status

### Wayland top-3 progress

| Monolith | Target | Done | Next extraction |
| --- | --- | --- | --- |
| `wayland/aswlcomp.c` | 13–14 `.c` + internal header | `aswlcomp_internal.h`, `aswlcomp_ime.c`, `aswlcomp_decor.c`, `aswlcomp_layer.c`, `aswlcomp_output.c`, `aswlcomp_state.c`, `aswlcomp_control_lock.c`, `aswlcomp_input.c`, `aswlcomp_input_config.c`, `aswlcomp_view.c`, `aswlcomp_dock.c`, `aswlcomp_toplevel.c`, `aswlcomp_toplevel_protocols.c`, `aswlcomp_server.c`, `aswlcomp_main.c` | — |
| `wayland/aswlpanel.c` | 7 `.c` + internal header | `aswlpanel_internal.h`, `aswlpanel_buffers.c`, `aswlpanel_model.c`, `aswlpanel_config.c`, `aswlpanel_render.c`, `aswlpanel_paint.c`, `aswlpanel_wl.c`, `aswlpanel_main.c` | — |
| `wayland/aswlmenu.c` | 6–7 `.c` + internal header | `aswlmenu_internal.h`, `aswlmenu_model.c`, `aswlmenu_config.c`, `aswlmenu_desktop.c`, `aswlmenu_render.c`, `aswlmenu_input.c`, `aswlmenu_wl.c`, `aswlmenu_main.c` | — |

### Wayland top-3 checklists (update as patches land)

#### `wayland/aswlcomp.c` checklist

- [x] `wayland/aswlcomp_internal.h`
- [x] `wayland/aswlcomp_ime.c`
- [x] `wayland/aswlcomp_decor.c`
- [x] `wayland/aswlcomp_layer.c`
- [x] `wayland/aswlcomp_output.c`
- [x] `wayland/aswlcomp_state.c`
- [x] `wayland/aswlcomp_control_lock.c`
- [x] `wayland/aswlcomp_input.c`
- [x] `wayland/aswlcomp_input_config.c`
- [x] `wayland/aswlcomp_view.c`
- [x] `wayland/aswlcomp_dock.c` (split from view to keep files < 1000 LOC)
- [x] `wayland/aswlcomp_toplevel.c`
- [x] `wayland/aswlcomp_toplevel_protocols.c` (split from toplevel to keep files < 1000 LOC)
- [x] `wayland/aswlcomp_server.c`
- [x] `wayland/aswlcomp_main.c`
- [x] `wayland/aswlcomp.c` shrunk to glue-only (or removed from build)

#### `wayland/aswlpanel.c` checklist

- [x] `wayland/aswlpanel_internal.h`
- [x] `wayland/aswlpanel_buffers.c`
- [x] `wayland/aswlpanel_model.c`
- [x] `wayland/aswlpanel_config.c` (split from model to keep files < 1000 LOC)
- [x] `wayland/aswlpanel_render.c`
- [x] `wayland/aswlpanel_paint.c` (split from render to keep files < 1000 LOC)
- [x] `wayland/aswlpanel_wl.c`
- [x] `wayland/aswlpanel_main.c`
- [x] `wayland/aswlpanel.c` shrunk to glue-only (or removed from build)

#### `wayland/aswlmenu.c` checklist

- [x] `wayland/aswlmenu_internal.h`
- [x] `wayland/aswlmenu_model.c`
- [x] `wayland/aswlmenu_config.c`
- [x] `wayland/aswlmenu_desktop.c` (optional; only if it helps keep files < 1000 LOC)
- [x] `wayland/aswlmenu_render.c`
- [x] `wayland/aswlmenu_input.c`
- [x] `wayland/aswlmenu_wl.c`
- [x] `wayland/aswlmenu_main.c`
- [x] `wayland/aswlmenu.c` shrunk to glue-only (or removed from build)

Implemented (2026-02-13):

- ✅ Added `wayland/aswlcomp_internal.h`
- ✅ Extracted IME/text-input code into `wayland/aswlcomp_ime.c` and wired `aswl_ime_init()`
- ✅ Updated `wayland/Makefile` to build/link `aswlcomp_ime.o`
- ✅ Extracted decoration rendering/assets into `wayland/aswlcomp_decor.c` and wired `aswlcomp_decor.o`
- ✅ `make -C wayland aswlcomp` + `tools/wayland-smoke.sh` pass
- Snapshot sizes after decor extraction:
  - `wayland/aswlcomp.c`: 7532 LOC
  - `wayland/aswlcomp_decor.c`: 999 LOC
  - `wayland/aswlcomp_ime.c`: 585 LOC
  - `wayland/aswlcomp_internal.h`: 558 LOC

Implemented (2026-02-15):

- ✅ Extracted layer-shell layout/arrangement into `wayland/aswlcomp_layer.c` and wired `aswlcomp_layer.o`
- ✅ `make -C wayland aswlcomp` + `tools/wayland-smoke.sh` pass
- Snapshot sizes after layer extraction:
  - `wayland/aswlcomp.c`: 7300 LOC
  - `wayland/aswlcomp_layer.c`: 234 LOC

- ✅ Extracted output lifecycle + output-manager v1 apply/test into `wayland/aswlcomp_output.c`, and state/persistence into `wayland/aswlcomp_state.c`
- ✅ `make -C wayland aswlcomp` + `tools/wayland-smoke.sh` pass
- Snapshot sizes after output extraction:
  - `wayland/aswlcomp.c`: 6248 LOC
  - `wayland/aswlcomp_output.c`: 307 LOC
  - `wayland/aswlcomp_state.c`: 837 LOC

- ✅ Extracted suite control protocol (`afterstep-control-v1`) + session-lock glue into `wayland/aswlcomp_control_lock.c`
- ✅ `make -C wayland aswlcomp` + `tools/wayland-smoke.sh` pass
- Snapshot sizes after control/lock extraction:
  - `wayland/aswlcomp.c`: 5370 LOC
  - `wayland/aswlcomp_control_lock.c`: 898 LOC

- ✅ Extracted seat/cursor/bindings + interactive move/resize into `wayland/aswlcomp_input.c` + `wayland/aswlcomp_input_config.c`
- ✅ `make -C wayland aswlcomp` + `tools/wayland-smoke.sh` pass
- Snapshot sizes after input extraction:
  - `wayland/aswlcomp.c`: 3720 LOC
  - `wayland/aswlcomp_input.c`: 950 LOC
  - `wayland/aswlcomp_input_config.c`: 773 LOC
  - `wayland/aswlcomp_internal.h`: 646 LOC

- ✅ Extracted view focus/placement/workspaces into `wayland/aswlcomp_view.c` + dock policy/arrangement into `wayland/aswlcomp_dock.c`
- ✅ `make -C wayland aswlcomp` + `tools/wayland-smoke.sh` pass
- Snapshot sizes after view extraction:
  - `wayland/aswlcomp.c`: 2661 LOC
  - `wayland/aswlcomp_view.c`: 639 LOC
  - `wayland/aswlcomp_dock.c`: 469 LOC
  - `wayland/aswlcomp_internal.h`: 652 LOC

- ✅ Extracted xdg/xwayland toplevel wiring + view lifecycle listeners into `wayland/aswlcomp_toplevel.c` and split foreign/ext-foreign toplevel protocol handles into `wayland/aswlcomp_toplevel_protocols.c`
- ✅ `make -C wayland aswlcomp` + `tools/wayland-smoke.sh` pass
- Snapshot sizes after toplevel extraction:
  - `wayland/aswlcomp.c`: 1583 LOC
  - `wayland/aswlcomp_toplevel.c`: 906 LOC
  - `wayland/aswlcomp_toplevel_protocols.c`: 202 LOC
  - `wayland/aswlcomp_internal.h`: 657 LOC

- ✅ Extracted server setup/teardown + spawn/flush + xdg-activation glue into `wayland/aswlcomp_server.c`
- ✅ `make -C wayland aswlcomp` + `tools/wayland-smoke.sh` pass
- Snapshot sizes after server extraction:
  - `wayland/aswlcomp.c`: 932 LOC
  - `wayland/aswlcomp_server.c`: 711 LOC
  - `wayland/aswlcomp_internal.h`: 660 LOC

- ✅ Extracted CLI parsing + `main()` into `wayland/aswlcomp_main.c` and removed `wayland/aswlcomp.c` from the build
- ✅ `make -C wayland aswlcomp` + `tools/wayland-smoke.sh` pass
- Snapshot sizes after main extraction:
  - `wayland/aswlcomp_main.c`: 425 LOC
  - `wayland/aswlcomp_server.c`: 711 LOC
  - `wayland/aswlcomp_internal.h`: 660 LOC

Implemented (2026-02-16):

- ✅ Added `wayland/aswlpanel_internal.h`
- ✅ Extracted shm buffer lifecycle into `wayland/aswlpanel_buffers.c`
- ✅ Extracted model/config/layout into `wayland/aswlpanel_model.c` and split config parsing into `wayland/aswlpanel_config.c`
  (keeps extracted files < 1000 LOC)
- ✅ Updated `wayland/Makefile` to build/link `aswlpanel_buffers.o`, `aswlpanel_model.o`, `aswlpanel_config.o`
- ✅ `make -C wayland aswlpanel` + `tools/wayland-smoke.sh` pass
- Snapshot sizes after model/config extraction:
  - `wayland/aswlpanel.c`: 2428 LOC
  - `wayland/aswlpanel_buffers.c`: 156 LOC
  - `wayland/aswlpanel_model.c`: 743 LOC
  - `wayland/aswlpanel_config.c`: 768 LOC
  - `wayland/aswlpanel_internal.h`: 235 LOC

- ✅ Extracted rendering into `wayland/aswlpanel_render.c` and `wayland/aswlpanel_paint.c` (split to keep files < 1000 LOC)
- ✅ Updated `wayland/Makefile` to build/link `aswlpanel_render.o`, `aswlpanel_paint.o`
- ✅ `make -C wayland aswlpanel` + `tools/wayland-smoke.sh` pass
- Snapshot sizes after render extraction:
  - `wayland/aswlpanel.c`: 825 LOC
  - `wayland/aswlpanel_buffers.c`: 156 LOC
  - `wayland/aswlpanel_model.c`: 743 LOC
  - `wayland/aswlpanel_config.c`: 768 LOC
  - `wayland/aswlpanel_render.c`: 790 LOC
  - `wayland/aswlpanel_paint.c`: 836 LOC
  - `wayland/aswlpanel_internal.h`: 269 LOC

- ✅ Extracted Wayland glue/event handling into `wayland/aswlpanel_wl.c`
- ✅ Updated `wayland/Makefile` to build/link `aswlpanel_wl.o`
- ✅ `make -C wayland aswlpanel` + `tools/wayland-smoke.sh` pass
- Snapshot sizes after Wayland glue extraction:
  - `wayland/aswlpanel.c`: 168 LOC
  - `wayland/aswlpanel_wl.c`: 662 LOC
  - `wayland/aswlpanel_buffers.c`: 156 LOC
  - `wayland/aswlpanel_model.c`: 743 LOC
  - `wayland/aswlpanel_config.c`: 768 LOC
  - `wayland/aswlpanel_render.c`: 790 LOC
  - `wayland/aswlpanel_paint.c`: 836 LOC
  - `wayland/aswlpanel_internal.h`: 272 LOC

- ✅ Extracted main/orchestration into `wayland/aswlpanel_main.c` and removed `wayland/aswlpanel.c` from the build
- ✅ Updated `wayland/Makefile` to build/link `aswlpanel_main.o`
- ✅ `make -C wayland aswlpanel` + `tools/wayland-smoke.sh` pass
- Snapshot sizes after main extraction:
  - `wayland/aswlpanel_main.c`: 168 LOC
  - `wayland/aswlpanel_wl.c`: 662 LOC
  - `wayland/aswlpanel_buffers.c`: 156 LOC
  - `wayland/aswlpanel_model.c`: 743 LOC
  - `wayland/aswlpanel_config.c`: 768 LOC
  - `wayland/aswlpanel_render.c`: 790 LOC
  - `wayland/aswlpanel_paint.c`: 836 LOC
  - `wayland/aswlpanel_internal.h`: 272 LOC

- ✅ Added `wayland/aswlmenu_internal.h`
- ✅ Extracted model/filter/layout/hit-testing into `wayland/aswlmenu_model.c`
- ✅ Updated `wayland/Makefile` to build/link `aswlmenu_model.o`
- ✅ `make -C wayland aswlmenu` + `tools/wayland-smoke.sh` pass
- Snapshot sizes after model extraction:
  - `wayland/aswlmenu.c`: 3334 LOC
  - `wayland/aswlmenu_model.c`: 608 LOC
  - `wayland/aswlmenu_internal.h`: 211 LOC

- ✅ Extracted menu parsing/reload into `wayland/aswlmenu_config.c`
- ✅ Updated `wayland/Makefile` to build/link `aswlmenu_config.o`
- ✅ `make -C wayland aswlmenu` + `tools/wayland-smoke.sh` pass
- Snapshot sizes after config extraction:
  - `wayland/aswlmenu.c`: 3012 LOC
  - `wayland/aswlmenu_model.c`: 608 LOC
  - `wayland/aswlmenu_config.c`: 315 LOC
  - `wayland/aswlmenu_internal.h`: 237 LOC

- ✅ Extracted optional desktop entry scanning into `wayland/aswlmenu_desktop.c`
- ✅ Updated `wayland/Makefile` to build/link `aswlmenu_desktop.o`
- ✅ `make -C wayland aswlmenu` + `tools/wayland-smoke.sh` pass
- Snapshot sizes after desktop extraction:
  - `wayland/aswlmenu.c`: 2743 LOC
  - `wayland/aswlmenu_model.c`: 608 LOC
  - `wayland/aswlmenu_config.c`: 315 LOC
  - `wayland/aswlmenu_desktop.c`: 281 LOC
  - `wayland/aswlmenu_internal.h`: 237 LOC

- ✅ Extracted rendering into `wayland/aswlmenu_render.c`
- ✅ Updated `wayland/Makefile` to build/link `aswlmenu_render.o`
- ✅ `make -C wayland aswlmenu` + `tools/wayland-smoke.sh` pass
- Snapshot sizes after render extraction:
  - `wayland/aswlmenu.c`: 1811 LOC
  - `wayland/aswlmenu_model.c`: 608 LOC
  - `wayland/aswlmenu_config.c`: 315 LOC
  - `wayland/aswlmenu_desktop.c`: 281 LOC
  - `wayland/aswlmenu_render.c`: 943 LOC
  - `wayland/aswlmenu_internal.h`: 241 LOC

- ✅ Extracted pointer/keyboard input handling into `wayland/aswlmenu_input.c`
- ✅ Updated `wayland/Makefile` to build/link `aswlmenu_input.o`
- ✅ `make -C wayland aswlmenu` + `tools/wayland-smoke.sh` pass
- Snapshot sizes after input extraction:
  - `wayland/aswlmenu.c`: 1421 LOC
  - `wayland/aswlmenu_model.c`: 608 LOC
  - `wayland/aswlmenu_config.c`: 315 LOC
  - `wayland/aswlmenu_desktop.c`: 281 LOC
  - `wayland/aswlmenu_render.c`: 943 LOC
  - `wayland/aswlmenu_input.c`: 411 LOC
  - `wayland/aswlmenu_internal.h`: 247 LOC

- ✅ Extracted Wayland glue + buffers into `wayland/aswlmenu_wl.c`
- ✅ Updated `wayland/Makefile` to build/link `aswlmenu_wl.o`
- ✅ `make -C wayland aswlmenu` + `tools/wayland-smoke.sh` pass
- Snapshot sizes after wl extraction:
  - `wayland/aswlmenu.c`: 837 LOC
  - `wayland/aswlmenu_wl.c`: 617 LOC
  - `wayland/aswlmenu_model.c`: 608 LOC
  - `wayland/aswlmenu_config.c`: 315 LOC
  - `wayland/aswlmenu_desktop.c`: 281 LOC
  - `wayland/aswlmenu_render.c`: 943 LOC
  - `wayland/aswlmenu_input.c`: 411 LOC
  - `wayland/aswlmenu_internal.h`: 252 LOC

- ✅ Extracted `main()` + argument parsing into `wayland/aswlmenu_main.c`
- ✅ Updated `wayland/Makefile` to build/link `aswlmenu_main.o`
- ✅ `make -C wayland aswlmenu` + `tools/wayland-smoke.sh` pass
- Snapshot sizes after main extraction:
  - `wayland/aswlmenu.c`: 717 LOC
  - `wayland/aswlmenu_main.c`: 130 LOC
  - `wayland/aswlmenu_wl.c`: 617 LOC
  - `wayland/aswlmenu_model.c`: 608 LOC
  - `wayland/aswlmenu_config.c`: 315 LOC
  - `wayland/aswlmenu_desktop.c`: 281 LOC
  - `wayland/aswlmenu_render.c`: 943 LOC
  - `wayland/aswlmenu_input.c`: 411 LOC
  - `wayland/aswlmenu_internal.h`: 252 LOC

- ✅ Moved remaining activation/navigation helpers out of `wayland/aswlmenu.c`
- ✅ Updated `wayland/Makefile` to stop building/linking `aswlmenu.o`
- ✅ `make -C wayland aswlmenu` + `tools/wayland-smoke.sh` pass
- Snapshot sizes after removing `aswlmenu.c`:
  - `wayland/aswlmenu.c`: removed
  - `wayland/aswlmenu_main.c`: 130 LOC
  - `wayland/aswlmenu_wl.c`: 617 LOC
  - `wayland/aswlmenu_model.c`: 883 LOC
  - `wayland/aswlmenu_config.c`: 315 LOC
  - `wayland/aswlmenu_desktop.c`: 281 LOC
  - `wayland/aswlmenu_render.c`: 943 LOC
  - `wayland/aswlmenu_input.c`: 411 LOC
  - `wayland/aswlmenu_internal.h`: 252 LOC

---

## 1) `wayland/aswlcomp.c` → 13–14 files

### Target file layout

Create `wayland/aswlcomp_internal.h` (not counted toward the `*.c` split count; still keep it < 1000 LOC) containing:

- Internal structs/enums currently defined in `aswlcomp.c`:
  - `struct aswl_server`, `struct aswl_view`, `struct aswl_output`, `struct aswl_keyboard`, etc.
- Shared prototypes used cross-file (keep it small; most functions remain `static` in their `.c` file).
- Shared includes (wlroots/wayland/xkb headers) so individual `.c` files can be thinner.

Then split into these **13–14** translation units:

1) `wayland/aswlcomp_main.c` (**~200–400 LOC**)
   - `usage()`
   - `main(int argc, char **argv)` argument parsing (`--socket`, `--autostart`, `--state`, `--spawn`)
   - `#if HAVE_WLROOTS` / fallback stub `main()` when wlroots is unavailable

2) `wayland/aswlcomp_server.c` (**~700–900 LOC**)
   - `struct aswl_server` lifecycle: init defaults, create wlroots backend/renderer/allocator/compositor/scene
   - global protocol manager creation that doesn’t belong elsewhere
   - flush timer plumbing (`aswl_schedule_flush`, `aswl_flush_timer_cb`)
   - spawn helpers that are genuinely “server-level” (optional; otherwise keep in input/view)

3) Output + state/persistence (`wayland/aswlcomp_output.c` + `wayland/aswlcomp_state.c`) (**each < 1000 LOC**)
   - output discovery + lifecycle:
     - `handle_new_output`, `handle_output_destroy`, `handle_output_frame`
   - output manager v1:
     - `output_manager_update_current_config`, `output_manager_apply_or_test`,
       `handle_output_manager_apply`, `handle_output_manager_test`
   - output persistence/state file:
     - `aswl_state_path_*`, `aswl_state_load`, `aswl_state_save`
     - output persist helpers (`aswl_output_persist_*`, `find_output_mode`, apply logic)
     - strict parsers used *only* for state (`parse_*_strict`, transform/mode parsing)
   - NOTE: `send_output_state` / `broadcast_output_state` currently remain in `wayland/aswlcomp.c` (exported); move later if it helps keep the control TU thin.

4) `wayland/aswlcomp_layer.c` (**~500–800 LOC**)
   - layer-shell surfaces:
     - `handle_new_layer_surface`, `handle_layer_surface_*`
   - “usable box” / strut application:
     - `apply_layer_struts_top` (+ any similar helpers)
   - layer arrangement entry points:
     - `arrange_layers_for_output`, `arrange_layers`, `layer_tree_for`

5) `wayland/aswlcomp_view.c` (**~600–900 LOC**)
   - view lookup + focus + workspace policy:
     - `surface_at`, `view_from_wlr_surface`, `focus_view`, `focus_next/prev/topmost`
     - `normalize_workspace`, `set_workspace`, `workspace_next/prev`
   - view placement + geometry:
      - `place_view`, geometry save/restore, target box selection, maximize/fullscreen toggles
   - dock policy + arrangement: `wayland/aswlcomp_dock.c`
     - `view_maybe_mark_dock`, `arrange_dock_views`, rule matching helpers
   - suite control window-state helpers (today: in `wayland/aswlcomp_control_lock.c`; move later if helpful):
      - `send_window_state`, `send_window_geometry`, `send_window_list_snapshot`, `broadcast_window_state`

6) `wayland/aswlcomp_toplevel.c` (**~800–950 LOC**) + `wayland/aswlcomp_toplevel_protocols.c` (**~150–300 LOC**)
   - xdg-toplevel wiring:
     - `handle_new_xdg_toplevel`, request handlers (`handle_request_*`), title/app-id listeners
   - foreign/ext-foreign toplevel protocols:
     - `view_toplevel_protocols_*`, `handle_foreign_request_*`, `view_update_toplevel_protocols`
   - xwayland wiring:
     - `handle_new_xwayland_surface`, associate/dissociate, map/configure requests, xwayland attach/detach
   - generic view lifetime listeners if they’re mostly “toplevel protocol glue”:
     - `handle_view_map/unmap/commit/destroy/*` (move whichever side keeps this file < 1000)

7) `wayland/aswlcomp_decor.c` (**~600–900 LOC**)
   - decoration assets:
     - icon load/ensure/destroy (`aswl_deco_icon_*`, `aswl_deco_assets_*`)
   - titlebar rendering pipeline:
     - pixbuf-backed `wlr_buffer` implementation (`aswl_pixbuf_buffer_*`)
     - ARGB premul/unpremul + blend + gradient helpers
     - `view_render_titlebar_buffer`
   - decoration application:
     - `view_update_decorations`, frame/content size helpers (`view_get_frame_size`, `view_get_content_offset`)

8) `wayland/aswlcomp_input.c` + `wayland/aswlcomp_input_config.c` (**each < 1000 LOC**)
   - device discovery + runtime input:
     - `handle_new_input`, keyboard/pointer wiring, cursor event handlers, interactive move/resize
   - keybindings + config:
     - parse modifiers, binding list mgmt, config file load for bindings/actions
   - pointer constraints + relative pointer:
     - `handle_new_pointer_constraint`, region confinement helpers, delta application
   - idle inhibitors + activity bookkeeping:
     - `handle_new_idle_inhibitor`, `aswl_idle_*` activity and timer refresh

9) `wayland/aswlcomp_ime.c` (**~500–700 LOC**)
   - text-input-v3:
     - `handle_new_text_input`, enable/commit/disable/destroy
   - input-method-v2 + popups:
     - `handle_new_input_method`, commit/new-popup/grab-keyboard/destroy
   - (optional) virtual-keyboard-v1 wiring can live here later, but may stay with input if it shares keyboard helpers
   - IME focus + popup placement helpers (`aswl_ime_*`)

10) `wayland/aswlcomp_control_lock.c` (**~700–950 LOC**)
   - AfterStep suite protocol (`afterstep-control-v1`):
     - `aswl_control_*` request handlers (`exec`, `quit`, focus/close/workspace/window ops)
     - `aswl_control_bind` + resource destroy
   - session-lock:
     - lock surface lifecycle + arrangement (`handle_new_session_lock`, `handle_session_lock_*`,
       `handle_lock_surface_*`, `arrange_lock_surfaces`, focus helpers)
- IMPORTANT sizing rule: keep broadcast/snapshot helpers in their domain modules:
  - output snapshot/broadcast helpers live in `aswlcomp_output.c` (`send_output_state`, `broadcast_output_state`, etc.)
  - window snapshot/broadcast helpers currently live in `aswlcomp_control_lock.c` (`send_window_state`, `broadcast_window_state`, etc.)
  - `aswlcomp_control_lock.c` should mostly marshal protocol requests and call those helpers

### Next extraction notes: `wayland/aswlcomp_decor.c`

As of **2026-02-14** (`wayland/aswlcomp.c` = **8524 LOC**), the compositor decoration code is largely concentrated in
these symbols (grep-friendly cut list):

- Types:
  - `struct aswl_pixbuf_buffer`
  - `struct aswl_deco_icon`, `struct aswl_deco_assets`
- Helpers/pixels:
  - `aswl_argb_to_premul_f` (NOTE: also used by session-lock rendering)
  - `aswl_premul_argb`, `aswl_unpremul_argb`
  - `aswl_blend_pixel_argb`, `aswl_blend_image_bilinear_argb`, `aswl_sample_image_bilinear_unpremul`
  - `aswl_gradient_t`, `aswl_gradient_sample`
  - `aswl_pixbuf_buffer_*` + `aswl_pixbuf_buffer_impl`
- Assets:
  - `aswl_deco_icon_destroy`, `aswl_deco_icon_try_load`
  - `aswl_deco_assets_ensure`, `aswl_deco_assets_destroy`
- View-facing API (decoration glue):
  - `view_get_deco_metrics`
  - `view_get_content_offset` (currently non-`static`)
  - `view_get_frame_size`
  - `view_create_frame_scene`
  - `view_render_titlebar_buffer`
  - `view_update_decorations` (called from focus changes + view lifecycle handlers)

Dependencies to expect:

- Icon loading: `aswl_icon_load_argb()` (from `wayland/aswlicon.c`)
- Gradients/colors: `aswl_gradient_is_valid()`, `aswl_color_*()` (theme/util)
- Text rendering: `aswl_font*` and/or `aswl_font5x7_*` helpers (keep as-is for mechanical move)

Cross-file exposure suggestion (mechanical, can revisit later):

- Promote `view_update_decorations()` + `view_get_frame_size()` to `aswlcomp_internal.h` prototypes after the move, since
  multiple non-decor domains call them.
- Decide what to do with `aswl_argb_to_premul_f()`:
  - either export it via `aswlcomp_internal.h`, or
  - keep it in `aswlcomp.c` until `aswlcomp_control_lock.c` extraction and move the lock-side call site together.

### Extraction (done): `wayland/aswlcomp_layer.c`

Layer-shell is a good next cut because most of it is “wlroots protocol glue + arrangement math” with relatively small
surface area into the rest of the compositor. Grep-friendly cut list:

- Types:
  - `struct aswl_layer_surface` (and any per-layer state like `surface_listeners_added` flags)
- Arrangement helpers:
  - `layer_tree_for`
  - `apply_layer_struts_top` (and any other `apply_layer_struts_*` helpers, if present)
  - `arrange_layers_for_output`
  - `arrange_layers` (NOTE: called from output manager apply/new output paths)
- Layer-surface lifecycle listeners:
  - `handle_new_layer_surface`
  - `handle_layer_surface_destroy`
  - `handle_layer_surface_surface_destroy`
  - `handle_layer_surface_map`
  - `handle_layer_surface_unmap`
  - `handle_layer_surface_commit`

Dependencies to expect:

- wlroots layer-shell + scene: `wlr_layer_surface_v1`, `wlr_scene_layer_surface_v1_create`, `struct wlr_scene_tree`
- Output geometry: `struct aswl_output`, `struct wlr_output`, `struct wlr_box`
- Dock integration: comments reference `arrange_dock_views()`, so keep the arrangement call ordering the same during the
  mechanical move.

Cross-file exposure suggestion:

- Export `arrange_layers(struct aswl_server *)` via `aswlcomp_internal.h` so output-related code can keep calling it
  without knowing where it lives.
- Keep `handle_new_layer_surface()` declared in `aswlcomp_internal.h` (server init owns the `wl_listener` hookup, even if
  the implementation moves to `aswlcomp_layer.c`).

### Completed extraction: `wayland/aswlcomp_output.c` + `wayland/aswlcomp_state.c`

Output lifecycle/output-manager and persistence/state were extracted after the layer split. The code ended up split
across two files to keep each unit < 1000 LOC (output-state broadcast helpers still live in `wayland/aswlcomp.c` for now).

Grep-friendly cut list:

- Types:
  - `struct aswl_output_persist` (+ `aswl_output_persist_find/get/apply` helpers)
- State persistence + strict parsing:
  - `aswl_state_load`, `aswl_state_save`
  - `aswl_state_path_*` helpers (if present)
  - `parse_u32_strict`, `parse_i32_strict`, `parse_float_strict`, `parse_bool_strict`
  - `parse_output_transform_strict`, `parse_output_mode_strict`
- Output lifecycle + frame:
  - `handle_new_output`, `handle_output_destroy`, `handle_output_frame`
  - `output_from_wlr_output`
  - `find_output_mode`
- Output manager v1:
  - `output_manager_update_current_config`
  - `output_manager_apply_or_test`
  - `handle_output_manager_apply`, `handle_output_manager_test`
- Control “output state” helpers (recommended to move here to reduce cross-module deps):
  - `server_primary_output`
  - `send_output_state`, `broadcast_output_state`

Dependencies to expect:

- wlroots output stack: `wlr_output_layout`, `wlr_output_manager_v1`, `wlr_xdg_output_manager_v1`
- backend state/test/commit: `wlr_backend_test`, `wlr_backend_commit`, `wlr_output_configuration_v1_build_state`
- scene output commit path: `wlr_scene_output_*`

Cross-file exposure suggestion:

- Export `aswl_state_load()` / `aswl_state_save()` and `output_from_wlr_output()` via `aswlcomp_internal.h` (they’re used
  from workspace/view paths).
- Keep `handle_new_output()` / `handle_output_manager_*()` declared in `aswlcomp_internal.h` (server init owns the
  `wl_listener` hookups, even if implementations move here).

### Next extraction after output: `wayland/aswlcomp_control_lock.c`

This cut groups the compositor’s “external control surfaces”:

- AfterStep suite protocol (`afterstep-control-v1`) server-side implementation (request handlers + bind)
- session-lock (`wlr_session_lock_v1`) lifecycle + lock-surface arrangement

Grep-friendly cut list:

- Types:
  - `struct aswl_control_client`
  - `struct aswl_lock_surface` (already in `aswlcomp_internal.h` today; OK to keep there for the first cut)
- Control protocol plumbing:
  - `aswl_control_resource_destroy`
  - `aswl_control_bind`
  - `aswl_control_impl`
- Control request handlers (from `aswl_control_impl`):
  - `aswl_control_destroy`
  - `aswl_control_exec`
  - `aswl_control_quit`
  - `aswl_control_close_focused`
  - `aswl_control_focus_next`
  - `aswl_control_focus_prev`
  - `aswl_control_set_workspace`
  - `aswl_control_workspace_next`
  - `aswl_control_workspace_prev`
  - `aswl_control_list_windows`
  - `aswl_control_focus_window`
  - `aswl_control_close_window`
  - `aswl_control_move_window_to_workspace`
  - `aswl_control_toggle_fullscreen`
  - `aswl_control_toggle_maximized`
  - `broadcast_workspace_state` (keep here: it is protocol emission + version gating)
- Lock spawning (optional but recommended so “lock policy” stays in one place):
  - `spawn_lock` (export via `aswlcomp_internal.h` if called from bindings/idle timer)
- Session-lock lifecycle:
  - `focus_lock_surface`, `focus_any_lock_surface`
  - `lock_surfaces_destroy`
  - `session_lock_apply_scene`, `session_lock_enter`, `session_lock_exit`
  - `handle_lock_surface_surface_destroy`
  - `handle_lock_surface_map`, `handle_lock_surface_unmap`
  - `handle_lock_surface_destroy`
  - `arrange_lock_surfaces`
  - `handle_session_lock_new_surface`
  - `session_lock_detach`
  - `handle_session_lock_unlock`, `handle_session_lock_destroy`
  - `handle_new_session_lock`

Coupling note (output frames):

- `handle_output_frame()` currently contains the “send `locked` once all enabled outputs have presented at least one lock
  frame” gating logic.
  - Option A (mechanical): leave the gating block in `aswlcomp_output.c` (still references `server->session_*` flags).
  - Option B (cleaner): move the gating block into an exported helper here and call it from the output frame handler.

Dependencies to expect:

- Wayland server core: `wl_global_create`, `wl_resource_*`
- `afterstep-control-v1-protocol.h`
- wlroots session-lock: `wlr_session_lock_manager_v1`, `wlr_session_lock_v1`, `wlr_session_lock_surface_v1`
- scene toggles/stacking: `wlr_scene_node_*`, `wlr_scene_rect_*`

Cross-file exposure suggestion (keep control TU thin):

- Keep output/window snapshot helpers in their domain TUs:
  - `send_output_state` / `broadcast_output_state` → `aswlcomp_output.c`
  - `send_window_state` / `send_window_geometry` / `send_window_list_snapshot` /
    `broadcast_window_state` / `broadcast_window_closed` → `aswlcomp_control_lock.c` (today)
- `aswlcomp_control_lock.c` should mostly marshal protocol requests and call these helpers (plus view/workspace helpers).

### Input extraction notes: `wayland/aswlcomp_input.c` + `wayland/aswlcomp_input_config.c`

Input is big, but it’s mostly “one-directional”: it consumes wlroots events and calls into view/workspace policy.
In practice it may need a second TU for config parsing to keep each file under 1000 LOC.

Grep-friendly cut list:

- Types (if not already in `aswlcomp_internal.h`):
  - `struct aswl_keyboard`
  - `struct aswl_pointer_device`
  - `struct aswl_binding`
  - `struct aswl_idle_inhibitor`
  - `struct aswl_pointer_constraint`
- Keyboard config + binding model:
  - `parse_modifiers`
  - `aswl_set_opt_string`
  - `aswl_apply_keyboard_device_config`, `aswl_apply_keyboard_config`
  - `add_binding_exec`, `add_binding_action`, `add_binding_workspace_set`
  - `binding_action_name`
- Pointer/libinput config:
  - `aswl_parse_bool`
  - `aswl_apply_pointer_device_config`, `aswl_apply_pointer_config`
  - `handle_pointer_device_destroy`
- Config/autostart loader (keeps keyboard/pointer/binds in one place):
  - `default_autostart_path`
  - `load_config_file`
- Keyboard event listeners:
  - `handle_new_virtual_keyboard`
  - `handle_keyboard_key`
  - `handle_keyboard_modifiers`
  - `handle_keyboard_destroy`
- Cursor + interactive move/resize:
  - `begin_interactive`, `end_interactive`
  - `process_cursor_motion`
  - `handle_cursor_motion`, `handle_cursor_motion_absolute`
  - `handle_cursor_button`, `handle_cursor_axis`, `handle_cursor_frame`
- Selection/cursor requests from clients:
  - `handle_request_cursor`
  - `handle_request_set_selection`
  - `handle_request_set_primary_selection`
- Pointer constraints + relative pointer:
  - `handle_pointer_constraint_destroy`, `handle_pointer_constraint_set_region`
  - `aswl_pointer_constraints_update`
  - `handle_new_pointer_constraint`
  - `aswl_relative_pointer_send_motion`
  - `aswl_cursor_apply_pointer_constraints_delta`
- Idle + inhibitors:
  - `aswl_idle_inhibit_is_active`
  - `aswl_idle_lock_timer_cb`
  - `aswl_idle_lock_note_activity`
  - `aswl_idle_note_activity`
  - `aswl_idle_inhibit_refresh`
  - `handle_new_idle_inhibitor`
  - `handle_idle_inhibitor_destroy`
  - `handle_idle_inhibitor_surface_map`
  - `handle_idle_inhibitor_surface_unmap`
  - `idle_inhibitors_destroy`

Dependencies to expect:

- wlroots input stack: `wlr_seat`, `wlr_cursor`, `wlr_xcursor_manager`
- xkbcommon
- libinput (via wlroots helpers) for pointer accel/tap-to-click
- pointer constraints + relative pointer protocols

Cross-file exposure suggestion:

- Keep “policy” in view/workspace code; input should call helpers like
  `focus_view()` / `focus_next_view()` / `set_workspace()` / `place_view()` rather than re-implementing them.
- If `spawn_lock()` is moved into `aswlcomp_control_lock.c`, export a thin `spawn_lock(server)` prototype so bindings and
  idle timers can trigger it without reintroducing lock internals here.

### Toplevel extraction notes: `wayland/aswlcomp_toplevel.c`

This cut should own xdg/xwayland toplevel wiring + request handlers + view lifecycle listeners, calling into policy in
`wayland/aswlcomp_view.c` and dock helpers in `wayland/aswlcomp_dock.c`.

Grep-friendly cut list:

- XDG toplevel wiring:
  - `handle_new_xdg_toplevel`
  - request handlers (`handle_request_*`)
  - title/app-id listeners (`handle_view_set_*`)
- Xwayland wiring:
  - `handle_new_xwayland_surface`
  - associate/dissociate + attach/detach
  - map_request + request_configure handlers
  - xwayland request handlers (`handle_xwayland_request_*`)
- View lifecycle listeners (if they’re mostly “toplevel protocol glue”):
  - `handle_view_scene_destroy`, `handle_view_surface_destroy`, `handle_view_commit`
  - `handle_view_map`, `handle_view_unmap`, `handle_view_destroy`
- Foreign toplevel protocols (split into `wayland/aswlcomp_toplevel_protocols.c` to keep files < 1000 LOC):
  - `view_toplevel_protocols_*`, `handle_foreign_request_*`, `view_update_toplevel_protocols`

Coupling note:

- This module will call into:
  - view/workspace policy: `focus_view`, `set_workspace`, `place_view`
  - decorations: `view_update_decorations`
  - dock policy: `view_maybe_mark_dock`, `arrange_dock_views`
  - control protocol (today): `broadcast_window_state`, `broadcast_window_closed` (in `aswlcomp_control_lock.c`)

### Toplevel extraction notes: `wayland/aswlcomp_toplevel.c`

This cut should own the “toplevel protocol glue” for new windows/surfaces and their lifecycle listeners.

Grep-friendly cut list:

- Title/app-id helpers:
  - `view_title`, `view_app_id` (and `view_is_suite_popup` if present)
- View lifecycle listeners:
  - `handle_view_scene_destroy`
  - `handle_view_surface_destroy`
  - `handle_view_commit`
  - `handle_view_map`
  - `handle_view_unmap`
  - `handle_view_destroy`
  - `handle_view_set_title`
  - `handle_view_set_app_id`
  - `handle_view_set_class`
- XDG toplevel:
  - `handle_request_move`
  - `handle_request_resize`
  - `handle_request_fullscreen`
  - `handle_request_maximize`
  - `handle_request_minimize`
  - `handle_new_xdg_toplevel`
- Xwayland:
  - `handle_xwayland_associate`
  - `handle_xwayland_dissociate`
  - `handle_xwayland_map_request`
  - `handle_xwayland_request_configure`
  - `handle_xwayland_request_move`
  - `handle_xwayland_request_resize`
  - `handle_xwayland_request_fullscreen`
  - `handle_xwayland_request_maximize`
  - `handle_xwayland_request_minimize`
  - `handle_new_xwayland_surface`
- Foreign toplevel management:
  - `handle_foreign_request_maximize`
  - `handle_foreign_request_minimize`
  - `handle_foreign_request_activate`
  - `handle_foreign_request_fullscreen`
  - `handle_foreign_request_close`
  - `view_update_toplevel_protocols` (create/update/destroy ext+foreign toplevel handles and listeners)

Dependencies to expect:

- wlroots xdg-shell, xwayland, foreign toplevel management, ext foreign toplevel list
- this module calls back into view policy for placement/workspace and into decor for decoration refresh

Cross-file exposure suggestion:

- Export these internal helpers in `aswlcomp_internal.h`:
  - `view_update_toplevel_protocols(view)`
  - `view_title(view)` / `view_app_id(view)` (or equivalent getters)
- Keep protocol-specific structs (`struct aswl_xdg_deco`) in this TU to avoid bloating the internal header.

### Late extraction: `wayland/aswlcomp_server.c` + `wayland/aswlcomp_main.c`

Once the behavioral islands above are split, whatever “glue” remains in the bottom half of `aswlcomp.c` can be split as:

- `aswlcomp_main.c`: `usage()`, `main(int argc, char **argv)` argument parsing + default paths, and the wlroots-missing
  fallback stub `main()` when `HAVE_WLROOTS` is false.
- `aswlcomp_server.c`: server bootstrap/teardown + global manager creation and **listener hookups** (implementations live
  in the domain TUs above).

Grep-friendly cut list (server/glue):

- Timers:
  - `aswl_flush_timer_cb`
  - `aswl_schedule_flush`
- XDG activation + spawning helpers (if not already moved elsewhere):
  - `aswl_now_msec`
  - `aswl_user_event_is_recent`
  - `aswl_surface_is_focused_for_activation`
  - `aswl_activation_token_for_spawn`
  - `spawn_command`
  - `spawn_command_with_activation`
  - `handle_xdg_activation_new_token`
  - `handle_xdg_activation_request_activate`
- wlroots/Wayland init + manager creation (in/near `main()` today):
  - `wl_display_create`, `wl_display_get_event_loop`, `wl_display_add_socket*`, `wl_display_run`, `wl_display_destroy`
  - `wlr_backend_autocreate`, `wlr_renderer_autocreate`, `wlr_allocator_autocreate`
  - scene + layout: `wlr_scene_create`, `wlr_output_layout_create`, `wlr_scene_output_layout_create`, etc.
  - globals: layer shell, xdg shell, output manager, pointer constraints, relative pointer manager, idle-notifier +
    idle-inhibit manager, session-lock manager, foreign toplevel managers, screencopy, dmabuf export, etc.
- Listener hookups (notify assignments + `wl_signal_add`):
  - `server.new_output` → `handle_new_output`
  - `server.output_manager_*` → output manager handlers
  - `server.new_layer_surface` → `handle_new_layer_surface`
  - `server.new_xdg_toplevel` / `server.new_xwayland_surface` / `server.new_xdg_decoration`
  - `server.new_session_lock` / `server.session_lock_*`
  - `server.new_input` / `server.new_virtual_keyboard` / `server.new_pointer_constraint` / `server.new_idle_inhibitor`
  - cursor listeners (`cursor_motion*`, `cursor_button`, `cursor_axis`, `cursor_frame`)
  - seat request listeners (`request_cursor`, `request_set_selection`, `request_set_primary_selection`)
  - xdg activation listeners

### Extraction order (lowest-risk first)

1. ✅ Add `wayland/aswlcomp_internal.h` and adjust `aswlcomp.c` to include it.
2. ✅ Extract `aswlcomp_ime.c` first (most self-contained; easiest to validate).
3. ✅ Extract `aswlcomp_decor.c` (mostly pure rendering helpers + assets).
4. ✅ Extract `aswlcomp_layer.c` (layer-shell lifecycle + arrangement).
5. ✅ Extract `aswlcomp_output.c` (output mgmt + persistence); validate with `wayland/aswlrandr` harness if available.
6. ✅ Extract `aswlcomp_control_lock.c` (suite protocol + session lock).
7. ✅ Extract `aswlcomp_input.c` + `aswlcomp_input_config.c` (cursor/keybindings/pointer constraints/idle inhibitors).
8. ✅ Extract `aswlcomp_view.c` + `aswlcomp_dock.c` (focus/workspace/placement + dock policy).
9. ✅ Extract `aswlcomp_toplevel.c` + `aswlcomp_toplevel_protocols.c` (xdg/xwayland wiring + view lifecycle listeners + foreign toplevel protocols).
10. Extract `aswlcomp_main.c` / `aswlcomp_server.c` last to split whatever “glue” remains in `aswlcomp.c`.
11. Update `wayland/Makefile` to compile/link the new objects (this will usually happen incrementally per extraction).
12. Remove transitional `#if 0` blocks from `aswlcomp.c` once each moved chunk is validated (they still count toward LOC).

### Definition of done

- No single `wayland/aswlcomp_*.c` exceeds **1000** lines.
- `make -C wayland` succeeds.
- `tools/wayland-smoke.sh` succeeds.
- Optional: `tools/wayland-screenshots.sh` still produces deterministic galleries.

---

## 2) `wayland/aswlpanel.c` → 6–7 files

Goal: **7** translation units, each **< 1000 LOC**, by separating:

- protocol glue vs UI model
- “paint primitives” vs “render the UI”

### Target file layout

Create `wayland/aswlpanel_internal.h` (keep it < 1000 LOC; split further only if it grows) containing:

- `struct as_state`, `struct as_buffer`, `struct as_window`, `struct as_button`
- shared constants/enums and internal prototypes

Then split into these **7** files:

1) `wayland/aswlpanel_main.c` (**~300–600 LOC**)
   - `main()`, argument parsing (dock/pager/window-list mode, geometry overrides, config path)
   - init + teardown orchestration (`setup_*`, `cleanup`)

2) `wayland/aswlpanel_wl.c` (**~700–950 LOC**)
   - registry binding + globals
   - xdg-shell + layer-shell setup and configure listeners
   - seat/pointer listeners + event translation into model actions
   - afterstep-control listener (workspace/window list updates from compositor)
   - frame callback wiring (`frame_done`) and “request redraw” triggers

3) `wayland/aswlpanel_buffers.c` (**~250–500 LOC**)
   - shm tmpfile creation, `wl_buffer` lifecycle callbacks
   - `as_buffer_create/destroy`, `as_state_ensure_buffers`, buffer acquire/swap logic

4) `wayland/aswlpanel_config.c` (**~600–850 LOC**)
   - config parsing for buttons and directives (`@edge`, `@margin`, `@workspaces`, etc.)
   - button list mgmt + icon loading
   - config file loading (`ASWLPANEL_CONFIG`, `~/.config/afterstep/aswlpanel.conf`)
   - env overrides for basic panel mode/edge/margins

5) `wayland/aswlpanel_model.c` (**~650–850 LOC**)
   - window list upsert/remove + geometry updates
   - layout computation + hit testing (hover/pressed index, exclusive zone, anchors/margins)
   - command parsing helpers (workspace targets, etc.) + command dispatch

6) `wayland/aswlpanel_render.c` (**~650–900 LOC**)
   - top-level draw: `as_state_draw()`
   - commit pipeline: `draw_and_commit()` + `schedule_redraw()` + frame callback

7) `wayland/aswlpanel_paint.c` (**~750–900 LOC**)
   - “paint primitives” + caches:
     - gradients, bevels, image blending, tinting
     - background snapshot mapping (`ASWLBG1`) and sampling

### Symbol map (current `wayland/aswlpanel.c`)

As of **2026-02-14** (`wayland/aswlpanel.c` = **4228 LOC**), here’s an approximate “what moves where” map, keyed by
existing function names (so extraction can be mostly cut/paste + include fixes):

- `aswlpanel_buffers.c` (shm + `wl_buffer` lifecycle)
  - `create_tmpfile` (memfd/tmpfile helper)
  - `buffer_release`
  - `as_buffer_create`, `as_buffer_destroy`
  - `as_state_destroy_buffers`, `as_state_ensure_buffers`, `as_state_acquire_buffer`
- `aswlpanel_paint.c` (paint primitives + caches)
  - ARGB helpers: `as_premul_argb`, `as_unpremul_argb`
  - background snapshot: `as_bg_snapshot_*`, `as_state_ensure_bg_snapshot`, `as_bg_snapshot_path`
  - gradients/cache: `as_afterimage_*`, `as_gradient_cache_*`, `as_gradient_cache_get`
  - draw primitives: `as_buffer_fill_*`, `as_buffer_draw_*`, `as_sample_image_bilinear_unpremul`
- `aswlpanel_render.c` (buffer → commit)
  - top-level drawing: `as_state_draw`, `draw_and_commit`, `schedule_redraw`, `frame_done`
- `aswlpanel_config.c` (config parsing + buttons)
  - parsing: `as_panel_edge_parse`, `as_anchor_parse`, `as_margins_parse_and_apply`, `as_parse_long_token`
  - button model + config parsing: `as_state_load_buttons`, `as_state_free_buttons`, `as_state_load_buttons_from_file`,
    `append_button`, `handle_button_directive`, `load_buttons_from_file`
- `aswlpanel_model.c` (state/layout/hit-testing + command dispatch)
  - layout math: `as_state_get_layout`, `as_state_compute_surface_origin`,
    `as_state_exclusive_zone`, `as_state_calc_dock_main_axis_size`
  - parsing: `as_command_parse_workspace_target`
  - window model: `as_state_*window*`, `as_window_label`, `as_state_visible_window_nth`, `as_state_hit_test`
  - command dispatch helpers: `spawn_command`, `as_state_launch_command`
- `aswlpanel_wl.c` (Wayland glue + event handling)
  - xdg/layer callbacks: `xdg_*`, `layer_surface_*`
  - seat/pointer callbacks: `seat_*`, `pointer_*`
  - afterstep-control listeners: `control_*`
  - registry/global wiring: `registry_global`, `registry_global_remove`, `setup_xdg`, `setup_layer_shell`
  - shutdown: `cleanup`
- `aswlpanel_main.c`
  - `main` (and any small argument/config parsing kept out of the above)

### Next extraction notes: `wayland/aswlpanel_internal.h` + `wayland/aswlpanel_buffers.c`

The lowest-risk first cut for panel is shm buffers, but it benefits from an internal header first so the type graph
doesn’t sprawl across unrelated includes.

`wayland/aswlpanel_internal.h` (mechanical contents to move as-is):

- Feature macros: either keep `#define _GNU_SOURCE` / `#define _POSIX_C_SOURCE 200809L` at the top of each extracted `.c`,
  or move them into the internal header and ensure it is included first everywhere.
- Core types + constants from the top of `aswlpanel.c`:
  - `enum as_panel_edge`, anchor/window flag enums
  - `struct as_margins`
  - `struct as_button`, `struct as_window`, `struct as_buffer`, `struct as_state`
  - background snapshot structs: `struct aswl_bg_snapshot_header`, `struct as_bg_snapshot`
- Internal cross-file prototypes (keep this small; most helpers remain `static`):
  - buffer API: `as_state_destroy_buffers`, `as_state_ensure_buffers`, `as_state_acquire_buffer`
  - redraw/commit: `schedule_redraw`, `draw_and_commit`, `frame_done` (whatever the buffer release callback needs)

`wayland/aswlpanel_buffers.c` cut list (grep-friendly):

- `create_tmpfile`
- `buffer_release` (NOTE: calls `draw_and_commit()` today)
- `as_buffer_create`, `as_buffer_destroy`
- `as_state_destroy_buffers`, `as_state_ensure_buffers`, `as_state_acquire_buffer`

Coupling note (important for a clean split):

- `buffer_release()` currently calls `draw_and_commit()` when a buffer becomes free and a redraw is pending.
  - Option A (most mechanical): make `draw_and_commit()` non-`static`, declare it in `aswlpanel_internal.h`, and keep the
    callback behavior unchanged.
  - Option B (slightly cleaner): change the callback to call `schedule_redraw()` only (but verify this doesn’t affect
    latency/behavior).

### Next extraction after buffers: `wayland/aswlpanel_model.c`

This cut extracts “state and intent”: config parsing, buttons/windows model, layout, and hit-testing.

Grep-friendly cut list:

- Window list model:
  - `as_window_destroy`
  - `as_state_clear_windows`, `as_state_destroy_windows`
  - `as_state_find_window`
  - `as_state_upsert_window`, `as_state_upsert_window_geometry`
  - `as_state_remove_window`
  - `as_window_visible`, `as_window_label`
  - `as_state_visible_window_nth`
- Button model + config parsing:
  - `append_button`
  - `handle_button_directive`
  - `load_buttons_from_file`
  - `as_state_load_buttons`
- Layout + hit-testing:
  - `as_state_compute_surface_origin`
  - `as_state_exclusive_zone`
  - `as_state_calc_dock_main_axis_size`
  - `as_state_button_main_size`
  - `as_state_window_main_size`
  - `as_state_get_layout`
  - `as_state_hit_test`
- Command parsing + launching:
  - `as_parse_long_token`
  - `as_panel_edge_parse`
  - `as_anchor_parse`
  - `as_margins_parse_and_apply`
  - `as_command_parse_workspace_target`
  - `spawn_command`
  - `as_state_launch_command`

Cross-file exposure suggestion:

- `aswlpanel_wl.c` needs access to:
  - `as_state_hit_test`, `as_state_exclusive_zone`
  - `as_state_upsert_window*` and `as_state_remove_window` (from `control_*` listener callbacks)
- `aswlpanel_render.c` needs access to:
  - `as_state_get_layout`
  - `as_state_visible_window_nth`
  - `as_window_visible` / `as_window_label` (if render decides what to paint based on those)
- So: declare a minimal set of these in `aswlpanel_internal.h` and keep the rest `static`.

### Render extraction: `wayland/aswlpanel_render.c` + `wayland/aswlpanel_paint.c`

This cut extracts pixels: gradient cache, background snapshot sampling, draw primitives, and the “draw→commit” pipeline.
To keep files < 1000 LOC, the implementation is split between `wayland/aswlpanel_paint.c` (primitives/caches) and
`wayland/aswlpanel_render.c` (top-level draw + commit).

Grep-friendly cut list:

- ARGB helpers:
  - `as_premul_argb`, `as_unpremul_argb`
- Background snapshot:
  - `as_bg_snapshot_destroy`
  - `as_bg_snapshot_path`
  - `as_bg_snapshot_load`
  - `as_state_ensure_bg_snapshot`
  - `as_buffer_fill_backpixmap_tint`
- Gradient cache + AfterImage bridge:
  - `as_gradient_cache_destroy`
  - `as_afterimage_gradient_type`
  - `as_afterimage_make_gradient_argb`
  - `as_gradient_cache_get`
- “Paint primitives” / draw helpers:
  - the `as_buffer_*` family (fill rects, bevels, gradients, image sampling, icon glyph drawing)
- Top-level render + commit:
  - `as_state_draw`
  - `draw_and_commit`
  - `schedule_redraw`
  - `frame_done`

Coupling note (buffers):

- `draw_and_commit()` calls `as_state_acquire_buffer()` (buffers TU) and may be called from `buffer_release()`
  (buffers TU). Keep prototypes in `aswlpanel_internal.h` and pick one “owner” file for each helper.

### Next extraction after render: `wayland/aswlpanel_wl.c`

This cut extracts all Wayland protocol glue and event handling.

Grep-friendly cut list:

- Registry/global binding:
  - `registry_global`, `registry_global_remove`
  - registry listener struct(s)
- XDG-shell setup + listeners:
  - `setup_xdg`
  - `xdg_wm_base_ping` + `xdg_wm_base_listener`
  - `xdg_surface_configure` + `xdg_surface_listener`
  - `xdg_toplevel_configure`, `xdg_toplevel_close` + `xdg_toplevel_listener`
- Layer-shell setup + listeners:
  - `setup_layer_shell`
  - `layer_surface_configure`, `layer_surface_closed` + `layer_surface_listener`
- Seat/pointer listeners:
  - `seat_capabilities`, `seat_name` + `seat_listener`
  - `pointer_enter`, `pointer_leave`, `pointer_motion`
  - `pointer_button`, `pointer_axis`, `pointer_frame`
  - `pointer_axis_source`, `pointer_axis_stop`, `pointer_axis_discrete`
  - `pointer_listener`
- AfterStep control listener:
  - `control_workspace_state`
  - `control_output_state`
  - `control_window_list_begin`, `control_window`, `control_window_geometry`, `control_window_list_end`
  - `control_window_closed`
  - `control_listener`
- Teardown:
  - `cleanup`

Cross-file exposure suggestion:

- This TU should call:
  - model helpers (`as_state_hit_test`, window upserts/removals)
  - render helpers (`schedule_redraw`, `frame_done`)
  - and avoid owning any “paint primitives”.

### Late extraction: `wayland/aswlpanel_main.c`

Keep this file orchestration-only:

- parse CLI/config args, init state defaults, load theme/font
- call setup functions from `aswlpanel_wl.c`, then `wl_display_dispatch` loop
- on exit: call `cleanup()`

### Extraction order

1. Add `wayland/aswlpanel_internal.h`.
2. Extract `aswlpanel_buffers.c` first (very self-contained).
3. Extract `aswlpanel_config.c` + `aswlpanel_model.c` (parsing + state, no pixels).
4. Extract `aswlpanel_render.c` + `aswlpanel_paint.c` (drawing + bg snapshot + draw/commit).
5. Extract `aswlpanel_wl.c` (callbacks + glue) and keep `aswlpanel_main.c` thin.
6. Update `wayland/Makefile`.

### Definition of done

- No single `wayland/aswlpanel_*.c` exceeds **1000** lines.
- `make -C wayland` succeeds.
- `tools/wayland-smoke.sh` succeeds.

---

## 3) `wayland/aswlmenu.c` → 6–7 files

Goal: **6–7** translation units, each **< 1000 LOC**, by separating:

- config parsing vs model/state
- Wayland glue vs input handling
- rendering vs selection/filter state
- optional freedesktop `.desktop` scanning

### Target file layout

Create `wayland/aswlmenu_internal.h` (keep it < 1000 LOC; split further only if it grows) containing:

- `struct as_state` and the menu entry / stack types
- shared constants/enums and internal prototypes used cross-file

Then split into these units:

1) `wayland/aswlmenu_main.c` (**~200–500 LOC**)
   - `main()`, argument parsing, startup + teardown orchestration

2) `wayland/aswlmenu_wl.c` (**~700–950 LOC**)
   - registry binding + globals
   - xdg-shell/layer-shell setup, configure listeners
   - shm buffer + surface commit/frame callback wiring
   - “request redraw” triggers

3) `wayland/aswlmenu_model.c` (**~700–950 LOC**)
   - entry arrays + stack push/pop
   - filter state, selection/scroll state, pinned handling
   - “actions” API used by input + config (keep rendering out of here)

4) `wayland/aswlmenu_config.c` (**~500–900 LOC**)
   - AfterStep-ish menu parsing (`@menu`, nested includes, titles, pinning rules)
   - model population from config structures

5) `wayland/aswlmenu_input.c` (**~500–900 LOC**)
   - pointer + keyboard handling mapped onto model actions (next/prev, activate, back, filter typing)

6) `wayland/aswlmenu_render.c` (**~700–950 LOC**)
   - draw logic: header/titlebar, close button, entry rows, help overlay
   - keep “paint primitives” local to this TU unless shared with panel/theme

7) `wayland/aswlmenu_desktop.c` (**~300–800 LOC**, optional)
   - freedesktop `.desktop` discovery + conversion into menu entries
   - if compiled out, keep stubs so `aswlmenu_model.c` doesn’t `#ifdef` heavily

### Symbol map (current `wayland/aswlmenu.c`)

As of **2026-02-14** (`wayland/aswlmenu.c` = **4097 LOC**), here’s an approximate “what moves where” map keyed by the
existing functions:

- `aswlmenu_model.c` (state/actions/layout/hit-testing)
  - entry lists + filtered list: `as_state_append_entry`, `as_state_free_entries`,
    `as_state_free_filtered`, `as_state_ensure_filtered`, `as_state_rebuild_filtered`
  - filter editing: `as_state_filter_*`
  - menu stack: `as_state_menu_stack_*`, `as_state_go_back`, `as_state_open_submenu`
  - selection/hover/layout: `as_state_get_layout`, `as_state_visible_rows`, `as_state_select_delta`,
    `as_state_ensure_selection_visible`, `as_state_hit_test_layout`, `as_state_update_hover`,
    `as_state_update_close_button_metrics`, `as_state_point_in_close_button`, `as_state_autosize`
  - command launch helpers: `spawn_command`, `as_state_launch_command`, `as_state_activate_entry`
  - window list title updates: `update_window_list_title`
- `aswlmenu_config.c` (AfterStep-ish menu parsing)
  - `.menu` parsing: `load_menu_from_file_section`, `load_menu_from_file`, `as_state_load_menu`,
    `as_state_finalize_menu`, `as_state_reload_menu_from_config`
  - submenu helpers: `menu_command_is_submenu`, `menu_command_submenu_target`, `menu_config_section_exists`
  - string helpers used by parsing: `lstrip`, `rstrip`, `str_case_contains`, `parse_bool` (keep minimal)
- `aswlmenu_desktop.c` (optional freedesktop scanning)
  - `.desktop` discovery: `as_state_add_desktop_entries`, `as_state_scan_desktop_dir`, `as_state_add_desktop_file`
  - `Exec=` sanitizing + tmp parser: `desktop_exec_sanitize`, `desktop_tmp_*`
- `aswlmenu_render.c` (pixels → buffer)
  - ARGB/gradients/primitives: `as_premul_argb`, `as_unpremul_argb`, `as_gradient_*`,
    `as_buffer_*`, `as_sample_image_bilinear_unpremul`
  - tiny font: `as_font5x7_*`, `as_buffer_draw_text5x7`
  - icon handling: `as_menu_entry_try_load_icon`, `as_menu_entry_destroy_icon`, `as_state_try_load_icon`,
    `as_state_ensure_close_button_icons`
  - top-level draw: `as_state_draw`
  - commit helpers: `draw_and_commit`, `schedule_redraw` (frame callback in wl glue)
- `aswlmenu_input.c` (pointer/keyboard)
  - `pointer_*`, `keyboard_*` → call into model actions (`as_state_select_delta`, `as_state_filter_*`,
    `as_state_activate_entry`, `as_state_go_back`, etc.)
- `aswlmenu_wl.c` (Wayland glue + buffers + suite control listener)
  - shm buffers: `create_tmpfile`, `buffer_release`, `as_buffer_create/destroy`,
    `as_state_destroy_buffers`, `as_state_ensure_buffers`, `as_state_acquire_buffer`
  - xdg callbacks: `xdg_*`
  - seat callbacks: `seat_*`
  - afterstep-control listeners: `control_*`
  - registry: `registry_*`
  - frame callback: `frame_done` (typically just resets callback + triggers redraw)
- `aswlmenu_main.c`
  - `usage`, `main`

If the combined `aswlmenu_wl.c` ends up too big (easy to do when it also owns shm buffers), split buffers out into a
dedicated `aswlmenu_buffers.c` mirroring the panel split; this still stays within the **6–7** file target if desktop
scanning is kept optional.

### Next extraction notes: `wayland/aswlmenu_internal.h` + `wayland/aswlmenu_model.c`

For menu, the highest leverage first step is carving out a “state/actions” module so input + config stop sharing
incidental helpers.

`wayland/aswlmenu_internal.h` (mechanical contents to move as-is):

- Feature macros: same `_GNU_SOURCE` / `_POSIX_C_SOURCE` note as panel.
- Shared types from the top of `aswlmenu.c`:
  - key/button fallback `#define`s (keep near the top so input files don’t pull linux headers)
  - `struct as_menu_entry`, `struct as_menu_stack_entry`, `struct as_buffer`, `struct as_state`
  - `struct as_menu_layout` (currently defined near `as_state_get_layout`; move into the header so render + model share it)
- Cross-file prototypes needed immediately:
  - model/actions exported from `aswlmenu_model.c` (see cut list below)
  - `schedule_redraw()` (model tends to request redraw after state changes)

`wayland/aswlmenu_model.c` cut list (grep-friendly; move these first):

- Entry + filtered list management:
  - `as_state_free_entries`, `as_state_append_entry`
  - `as_state_free_filtered`, `as_state_ensure_filtered`, `as_state_rebuild_filtered`
- Filter editing:
  - `as_state_filter_clear_silent`, `as_state_filter_set`, `as_state_filter_append_utf8`, `as_state_filter_backspace`
- Stack/navigation:
  - `as_state_menu_stack_clear`, `as_state_menu_stack_push`
  - `as_state_go_back`, `as_state_open_submenu`
- Selection/hover/layout/hit-testing:
  - `as_state_get_layout`
  - `as_state_visible_rows`, `as_state_autosize`
  - `as_state_select_delta`, `as_state_ensure_selection_visible`
  - `as_state_hit_test_layout`, `as_state_update_hover`
  - close-button metrics helpers: `as_state_update_close_button_metrics`, `as_state_point_in_close_button`
- Command launch (keep the action in model; platform glue stays elsewhere):
  - `spawn_command`, `as_state_launch_command`, `as_state_activate_entry`
  - `update_window_list_title` (if it’s logically part of “model tells compositor what the title should be”)

Keep out of `aswlmenu_model.c` for this first cut:

- shm/buffer lifecycle: `create_tmpfile`, `buffer_release`, `as_state_*buffers*` (goes with `aswlmenu_wl.c` or a later
  `aswlmenu_buffers.c`)
- rendering: `as_state_draw` and ARGB primitives (goes to `aswlmenu_render.c`)
- `.desktop` scanning: `as_state_add_desktop_*` (goes to `aswlmenu_desktop.c`)
- menu parsing/load: `as_state_load_menu`, `as_state_finalize_menu`, `as_state_reload_menu_from_config` (goes to
  `aswlmenu_config.c`)

### Next extraction after model: `wayland/aswlmenu_config.c`

This cut extracts menu parsing + reload logic (file IO, directives, submenu section resolution) and keeps it out of the
runtime model.

Grep-friendly cut list:

- Submenu/section helpers:
  - `menu_command_is_submenu`
  - `menu_command_submenu_target`
  - `menu_config_section_exists`
- Menu parsing/load:
  - `load_menu_from_file_section`
  - `load_menu_from_file`
  - `as_state_load_menu`
  - `as_state_finalize_menu`
  - `as_state_reload_menu_from_config`

Notes:

- `as_state_finalize_menu()` typically belongs here even if it calls model helpers (e.g. rebuild filtered list or
  autosize) so “reload” stays centralized.
- Keep `lstrip()` / `rstrip()` available to both config and desktop parsing:
  - easiest: make them `static inline` in `aswlmenu_internal.h` (tiny, no linkage headaches).

Cross-file exposure suggestion:

- Config should only “emit entries” via model APIs (e.g. `as_state_append_entry(...)`).
- After reload: request redraw via `schedule_redraw(state)` (prototype in internal header).

### Next extraction after config (optional): `wayland/aswlmenu_desktop.c`

This is optional, but it’s a clean cut and helps keep `aswlmenu.c` under control once config is extracted.

Grep-friendly cut list:

- `desktop_exec_sanitize`
- `struct desktop_tmp_entry`
- `desktop_tmp_finalize`, `desktop_tmp_reset`
- `as_state_add_desktop_file`
- `as_state_scan_desktop_dir`
- `as_state_add_desktop_entries`
- `parse_bool` (used for `Hidden=` / `NoDisplay=`)

Cross-file exposure suggestion:

- Desktop scanning should only add items via `as_state_append_entry(...)` and avoid touching render/input state directly.

### Next extraction after config/desktop: `wayland/aswlmenu_render.c`

This cut extracts pixels and the “draw→commit” pipeline.

Grep-friendly cut list:

- ARGB + gradients:
  - `as_premul_argb`, `as_unpremul_argb`
  - `as_gradient_t`, `as_gradient_sample`
  - `as_buffer_fill_style_rect`
- Draw primitives:
  - the `as_buffer_*` family (`as_buffer_paint_solid`, `as_buffer_fill_rect`, `as_buffer_draw_bevel_rect`, blending,
    bilinear sampling/draw)
  - `as_sample_image_bilinear_unpremul`
- Tiny font:
  - `as_font5x7_*` + `as_buffer_draw_text5x7`
- Icon handling:
  - `as_menu_entry_try_load_icon`, `as_menu_entry_destroy_icon`
  - `as_state_try_load_icon`, `as_state_ensure_close_button_icons`
- Top-level draw:
  - `as_state_draw`
- Commit pipeline:
  - `draw_and_commit`
  - `schedule_redraw`
  - `frame_done` (+ `frame_listener`)

Coupling note (buffers):

- `draw_and_commit()` uses `as_state_ensure_buffers()` / `as_state_acquire_buffer()`, and `buffer_release()` currently
  calls `draw_and_commit()`. Keep the prototypes in `aswlmenu_internal.h` and keep the behavior identical first.

### Next extraction after render: `wayland/aswlmenu_input.c`

This cut extracts pointer/keyboard handling (but keeps “what action happens” in the model).

Grep-friendly cut list:

- Pointer:
  - `pointer_enter`, `pointer_leave`, `pointer_motion`, `pointer_button`
  - `pointer_axis`, `pointer_frame`
  - `pointer_axis_source`, `pointer_axis_stop`, `pointer_axis_discrete`
  - `pointer_listener`
- Keyboard:
  - `keyboard_keymap` (both `HAVE_XKBCOMMON` and fallback branches)
  - `keyboard_enter`, `keyboard_leave`
  - `keyboard_key`, `keyboard_modifiers`, `keyboard_repeat_info`
  - `keyboard_listener`

Cross-file exposure suggestion:

- Input handlers should call into model actions:
  - hover/selection: `as_state_update_hover`, `as_state_select_delta`, `as_state_ensure_selection_visible`
  - filter: `as_state_filter_*`
  - activation/navigation: `as_state_activate_entry`, `as_state_go_back`
- Then request redraw via `schedule_redraw(state)`.

### Next extraction after input: `wayland/aswlmenu_wl.c`

This TU should own: Wayland connection/setup, registry binding, shm buffer wiring, seat + control listener glue, and
cleanup.

Grep-friendly cut list:

- shm buffers:
  - `create_tmpfile`, `buffer_release`
  - `as_buffer_create`, `as_buffer_destroy`
  - `as_state_destroy_buffers`, `as_state_ensure_buffers`, `as_state_acquire_buffer`
- XDG callbacks + setup:
  - `setup_xdg`
  - `xdg_wm_base_ping` + `xdg_wm_base_listener`
  - `xdg_surface_configure` + `xdg_surface_listener`
  - `xdg_toplevel_configure`, `xdg_toplevel_close` + `xdg_toplevel_listener`
- Seat glue:
  - `seat_capabilities`, `seat_name` + `seat_listener` (installs pointer/keyboard listeners from `aswlmenu_input.c`)
- Suite control listener:
  - `control_workspace_state`
  - `control_output_state`
  - `control_window_list_begin`, `control_window`, `control_window_geometry`, `control_window_list_end`
  - `control_window_closed`
  - `control_listener`
- Registry:
  - `registry_global`, `registry_global_remove`
  - `registry_listener`
- Teardown:
  - `cleanup`

Cross-file exposure suggestion:

- This TU should call:
  - model upserts/removals from `control_*` callbacks
  - render helpers (`schedule_redraw`, and possibly `draw_and_commit` at startup)
  - and avoid owning any “paint primitives”.

### Late extraction: `wayland/aswlmenu_main.c`

Keep this file orchestration-only:

- `usage`, `main` arg parsing
- theme/font init + config load (calls into `aswlmenu_config.c` / `aswlmenu_desktop.c`)
- Wayland setup (calls into `aswlmenu_wl.c`) and event loop
- teardown (`cleanup`)

### Extraction order

1. Add `wayland/aswlmenu_internal.h`.
2. Extract `aswlmenu_model.c` (gives the rest a stable “state/actions” surface).
3. Extract `aswlmenu_config.c`.
4. Extract `aswlmenu_desktop.c` (if enabled; otherwise postpone).
5. Extract `aswlmenu_render.c`.
6. Extract `aswlmenu_input.c`.
7. Extract `aswlmenu_wl.c` and keep `aswlmenu_main.c` thin.
8. Update `wayland/Makefile`.

### Definition of done

- No single `wayland/aswlmenu_*.c` exceeds **1000** lines.
- `make -C wayland` succeeds.
- `tools/wayland-smoke.sh` succeeds.

---

## 4) Next targets (after the top 3 Wayland monoliths)

Once the Wayland suite stops being the biggest pain point, the next highest-impact monoliths to tackle are
listed below, grouped by what to build/test.

### Wayland helpers (validate: `make -C wayland` + `tools/wayland-smoke.sh`)

Priority (next; still ≥ 1000 LOC):

- [x] `wayland/aswltheme.c` → plan: `monolith-plans/wayland-aswltheme.md`
- [x] `wayland/aswlfont.c` → plan: `monolith-plans/wayland-aswlfont.md`
- [x] `wayland/aswlbg.c` → plan: `monolith-plans/wayland-aswlbg.md`
- [x] `wayland/aswllock.c` → plan: `monolith-plans/wayland-aswllock.md`

Already done:

- [x] `wayland/aswlicon.c` → plan: `monolith-plans/wayland-aswlicon.md`

Implemented (2026-02-17):

- ✅ Split `wayland/aswlicon.c` into `aswlicon_resolve.c`, `aswlicon_png.c`, `aswlicon_xml.c`, `aswlicon_afterimage.c`,
  and `aswlicon_internal.h` (kept `aswlicon.c` as the thin dispatch wrapper).
- ✅ `make -C wayland` and `tools/wayland-smoke.sh` pass.
- Snapshot sizes: `aswlicon.c` 67 LOC, `aswlicon_resolve.c` 526 LOC, `aswlicon_png.c` 156 LOC, `aswlicon_xml.c` 599 LOC,
  `aswlicon_afterimage.c` 789 LOC, `aswlicon_internal.h` 55 LOC.

- ✅ Split `wayland/aswltheme.c` into `aswltheme_core.c`, `aswltheme_colorscheme.c`, `aswltheme_styles.c`,
  `aswltheme_load.c`, and `aswltheme_internal.h` (removed the monolith from the build).
- ✅ `make -C wayland` and `tools/wayland-smoke.sh` pass.
- Snapshot sizes: `aswltheme_core.c` 187 LOC, `aswltheme_colorscheme.c` 241 LOC, `aswltheme_styles.c` 934 LOC,
  `aswltheme_load.c` 491 LOC, `aswltheme_internal.h` 77 LOC.

Implemented (2026-02-18):

- ✅ Split `wayland/aswlfont.c` into `aswlfont_backend.c`, `aswlfont_layout.c`, and `aswlfont_internal.h` (kept
  `aswlfont.c` as the public API/render glue).
- ✅ `make -C wayland` and `tools/wayland-smoke.sh` pass.
- Snapshot sizes: `aswlfont.c` 685 LOC, `aswlfont_backend.c` 574 LOC, `aswlfont_layout.c` 258 LOC,
  `aswlfont_internal.h` 55 LOC.

- ✅ Split `wayland/aswlbg.c` into `aswlbg_wl.c`, `aswlbg_render.c`, and `aswlbg_internal.h` (kept `aswlbg.c` as the
  thin `main()` wrapper).
- ✅ `make -C wayland` and `tools/wayland-smoke.sh` pass.
- Snapshot sizes: `aswlbg.c` 12 LOC, `aswlbg_wl.c` 529 LOC, `aswlbg_render.c` 947 LOC, `aswlbg_internal.h` 53 LOC.

- ✅ Split `wayland/aswllock.c` into `aswllock_wl.c` and `aswllock_internal.h` (kept `aswllock.c` as UI/render code).
- ✅ `make -C wayland` and `tools/wayland-smoke.sh` pass.
- Snapshot sizes: `aswllock.c` 258 LOC, `aswllock_wl.c` 776 LOC, `aswllock_internal.h` 26 LOC.

#### Wayland helper quick split sketches

(Mirrors the per-file plans above; included here so you don’t have to jump files mid-refactor.)

- `wayland/aswlicon.c` (~2120 LOC) → `aswlicon_internal.h` + `aswlicon_resolve.c` + `aswlicon_png.c` + `aswlicon_xml.c`;
  keep `aswlicon.c` as thin API glue; optionally split `aswlicon_afterimage.c` if it helps keep files < 1000 LOC.
- `wayland/aswltheme.c` (~1843 LOC) → `aswltheme_internal.h` + `aswltheme_core.c` + `aswltheme_colorscheme.c` +
  `aswltheme_styles.c` + `aswltheme_load.c`
- `wayland/aswlfont.c` (~1496 LOC) → `aswlfont_internal.h` + `aswlfont_backend.c` + `aswlfont_layout.c`; keep
  `aswlfont.c` as API/render glue (or rename it to `aswlfont_render.c` and add a thin `aswlfont.c` wrapper).
- `wayland/aswlbg.c` (~1461 LOC) → `aswlbg_internal.h` + `aswlbg_wl.c` + `aswlbg_render.c`; keep `aswlbg.c` as
  CLI/orchestration (or rename it to `aswlbg_main.c` and add a thin `aswlbg.c` wrapper).
- `wayland/aswllock.c` (~1034 LOC) → `aswllock_internal.h` + `aswllock_wl.c`; keep `aswllock.c` as UI/render glue (or
  rename it to `aswllock_ui.c`).

### Core WM (X11) (validate: `make -C src/afterstep` + `tools/xvfb-smoke.sh`)

- [x] `src/afterstep/configure.c` → plan: `monolith-plans/src-afterstep-configure.md`
- [x] `src/afterstep/functions.c` → plan: `monolith-plans/src-afterstep-functions.md`
- [x] `src/afterstep/placement.c` → plan: `monolith-plans/src-afterstep-placement.md`
- [x] `src/afterstep/menus.c` → plan: `monolith-plans/src-afterstep-menus.md`
- [x] `src/afterstep/decorations.c` → plan: `monolith-plans/src-afterstep-decorations.md`
- [x] `src/afterstep/pager.c` → plan: `monolith-plans/src-afterstep-pager.md`
- [ ] `src/afterstep/window_frame.c` → plan: `monolith-plans/src-afterstep-window_frame.md`
- [ ] `src/afterstep/module.c` → plan: `monolith-plans/src-afterstep-module.md`
- [ ] `src/afterstep/dbus.c` → plan: `monolith-plans/src-afterstep-dbus.md`
- [ ] `src/afterstep/afterstep.c` → plan: `monolith-plans/src-afterstep-afterstep.md`

Implemented (2026-02-18):

- ✅ Split `src/afterstep/configure.c` into `configure_load.c`, `configure_apply.c`, `configure_merge.c`,
  `configure_restart.c`, and `configure_internal.h` (kept `configure.c` as the orchestrator).
- ✅ `make -C src/afterstep` and `tools/xvfb-smoke.sh` pass.
- Snapshot sizes: `configure.c` 678 LOC, `configure_load.c` 992 LOC, `configure_apply.c` 937 LOC,
  `configure_merge.c` 75 LOC, `configure_restart.c` 44 LOC, `configure_internal.h` 61 LOC.

- ✅ Split `src/afterstep/functions.c` into `function_handlers_window.c`, `function_handlers_focus.c`,
  `function_handlers_workspace.c`, `function_handlers_modules.c`, `function_handlers_misc.c`, and `functions_internal.h`
  (kept `functions.c` as the dispatcher/queue + complex-function runner; config handlers remain in
  `function_handlers_config.c`).
- ✅ `make -C src/afterstep` and `tools/xvfb-smoke.sh` pass.
- Snapshot sizes: `functions.c` 732 LOC, `function_handlers_config.c` 526 LOC, `function_handlers_window.c` 236 LOC,
  `function_handlers_focus.c` 50 LOC, `function_handlers_workspace.c` 174 LOC, `function_handlers_modules.c` 119 LOC,
  `function_handlers_misc.c` 600 LOC, `functions_internal.h` 106 LOC.

- ✅ Split `src/afterstep/placement.c` into `placement_space.c`, `placement_strategies.c`, `placement_moveresize.c`, and
  `placement_internal.h` (kept `placement.c` as initial placement orchestration + avoid-cover enforcement).
- ✅ `make -C src/afterstep` and `tools/xvfb-smoke.sh` pass.
- Snapshot sizes: `placement.c` 580 LOC, `placement_space.c` 133 LOC, `placement_strategies.c` 978 LOC,
  `placement_moveresize.c` 316 LOC, `placement_internal.h` 27 LOC.

- ✅ Split `src/afterstep/menus.c` into `menus_core.c`, `menus_render.c`, `menus_events.c`, `menus_run.c`, and
  `menus_internal.h`.
- ✅ `make -C src/afterstep` and `tools/xvfb-smoke.sh` pass.
- Snapshot sizes: `menus_core.c` 515 LOC, `menus_render.c` 339 LOC, `menus_events.c` 745 LOC, `menus_run.c` 319 LOC,
  `menus_internal.h` 42 LOC.

- ✅ Split `src/afterstep/decorations.c` into `decorations_model.c`, `decorations_render.c`, and
  `decorations_internal.h` (kept `decorations.c` as the main apply/orchestration TU).
- ✅ `make -C src/afterstep` and `tools/xvfb-smoke.sh` pass.
- Snapshot sizes: `decorations.c` 790 LOC, `decorations_model.c` 340 LOC, `decorations_render.c` 433 LOC,
  `decorations_internal.h` 25 LOC.

- ✅ Split `src/afterstep/pager.c` into `pager_model.c` and `pager_render.c` (kept `pager.c` as the paging/edge-scroll
  entry point).
- ✅ `make -C src/afterstep` and `tools/xvfb-smoke.sh` pass.
- Snapshot sizes: `pager.c` 198 LOC, `pager_model.c` 355 LOC, `pager_render.c` 998 LOC.

### X11 modules/tools (validate: `make -C <dir>` + `tools/xvfb-smoke.sh`)

- `src/Wharf/Wharf.c` → plan: `monolith-plans/src-wharf.md`
- `src/Form/Form.c` → plan: `monolith-plans/src-form.md`
- `src/WinList2/WinList.c` → plan: `monolith-plans/src-winlist2.md`
- `src/ASMount/main.c` → plan: `monolith-plans/src-asmount.md`
- `src/Gnome/Gnome.c` → plan: `monolith-plans/src-gnome.md`
- `src/Ident/Ident.c` → plan: `monolith-plans/src-ident.md`
- `src/ASConfig/ASConfig.c` → plan: `monolith-plans/src-asconfig.md`
- `src/asetroot/asetroot.c` → plan: `monolith-plans/src-asetroot.md`
- `src/ASDocGen/ASDocGen.c` → plan: `monolith-plans/src-asdocgen-main.md`
- `src/ASDocGen/xmlproc.c` → plan: `monolith-plans/src-asdocgen-xmlproc.md`
- `src/Script/Instructions.c` → plan: `monolith-plans/src-script-instructions.md`

### Libraries (validate: `make -C <lib>`; for confidence add `tools/xvfb-smoke.sh`)

- `libAfterImage/asstorage.c` → plan: `monolith-plans/libAfterImage-asstorage.md`
- `libAfterImage/asimagexml.c` → plan: `monolith-plans/libAfterImage-asimagexml.md`
- `libAfterImage/afterbase.c` → plan: `monolith-plans/libAfterImage-afterbase.md`
- `libAfterImage/draw.c` → plan: `monolith-plans/libAfterImage-draw.md`
- `libAfterImage/asimage.c` → plan: `monolith-plans/libAfterImage-asimage.md`
- `libAfterImage/imencdec.c` → plan: `monolith-plans/libAfterImage-imencdec.md`
- `libAfterImage/asfont.c` → plan: `monolith-plans/libAfterImage-asfont.md`
- `libAfterImage/import.c` → plan: `monolith-plans/libAfterImage-import.md`
- `libAfterImage/export.c` → plan: `monolith-plans/libAfterImage-export.md`
- `libAfterImage/apps/ascompose.c` → plan: `monolith-plans/libAfterImage-ascompose.md`
- `libAfterStep/decor.c` → plan: `monolith-plans/libAfterStep-decor.md`
- `libAfterStep/freestor.c` → plan: `monolith-plans/libAfterStep-freestor.md`
- `libAfterStep/hints_common.c` → plan: `monolith-plans/libAfterStep-hints_common.md`
- `libAfterStep/asapp.c` → plan: `monolith-plans/libAfterStep-asapp.md`
- `libAfterStep/clientprops.c` → plan: `monolith-plans/libAfterStep-clientprops.md`
- `libAfterStep/mystyle.c` → plan: `monolith-plans/libAfterStep-mystyle.md`
- `libAfterStep/wmprops.c` → plan: `monolith-plans/libAfterStep-wmprops.md`
- `libAfterStep/session.c` → plan: `monolith-plans/libAfterStep-session.md`
- `libAfterStep/canvas.c` → plan: `monolith-plans/libAfterStep-canvas.md`
- `libAfterStep/desktop_category.c` → plan: `monolith-plans/libAfterStep-desktop_category.md`
- `libAfterStep/moveresize.c` → plan: `monolith-plans/libAfterStep-moveresize.md`
- `libAfterStep/functions.c` → plan: `monolith-plans/libAfterStep-functions.md`
- `libAfterStep/parser.c` → plan: `monolith-plans/libAfterStep-parser.md`
- `libAfterBase/parse.c` → plan: `monolith-plans/libAfterBase-parse.md`
- `libAfterBase/audit.c` → plan: `monolith-plans/libAfterBase-audit.md`
- `libAfterBase/layout.c` → plan: `monolith-plans/libAfterBase-layout.md`
- `libAfterBase/regexp.c` → plan: `monolith-plans/libAfterBase-regexp.md`
- `libAfterBase/xml.c` → plan: `monolith-plans/libAfterBase-xml.md`
- `libAfterConf/AfterStep.c` → plan: `monolith-plans/libAfterConf-AfterStep.md`
- `libASGTK/asgtkxmleditor.c` → plan: `monolith-plans/libASGTK-asgtkxmleditor.md`

### Header umbrellas (validate: full build recommended)

- `libAfterConf/afterconf.h` → plan: `monolith-plans/libAfterConf-afterconf-h.md`
- `libAfterImage/asimage.h` → plan: `monolith-plans/libAfterImage-asimage-h.md`
