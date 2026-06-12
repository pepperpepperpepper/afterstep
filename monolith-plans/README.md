# Monolith plans

This folder is an index + set of per-file breakup plans for the biggest “single file does everything” hotspots in this
tree.

Snapshot date: **2026-02-14** (LOC counts are approximate; measured by newline counting on `git ls-files`).

## Taxonomy (largest files)

### First-party monoliths (highest priority)

These are *project-owned* files (excluding bundled third-party libraries and generated lexer/parser outputs).

| LOC | File | Subsystem | Notes | Plan |
| ---: | --- | --- | --- | --- |
| 8524 | `wayland/aswlcomp.c` | Wayland compositor | wlroots compositor + policy + protocols + rendering in one TU (IME already extracted) | `wayland-aswlcomp.md` |
| 4228 | `wayland/aswlpanel.c` | Wayland suite UI | panel + pager + dock + rendering + config + shm in one TU | `wayland-aswlpanel.md` |
| 4097 | `wayland/aswlmenu.c` | Wayland suite UI | menu UI + parsing + desktop entries + input + rendering in one TU | `wayland-aswlmenu.md` |
| 3116 | `src/Wharf/Wharf.c` | X11 module | Wharf: model + render + events + swallow + animations in one TU | `src-wharf.md` |
| 2714 | `libAfterImage/asstorage.c` | libAfterImage | storage + compression + zlib glue in one TU | `libAfterImage-asstorage.md` |
| 2683 | `src/afterstep/configure.c` | core WM (X11) | config load/apply + theme wiring in one TU | `src-afterstep-configure.md` |
| 2679 | `libAfterConf/afterconf.h` | config system | “everything config” umbrella header; lots of unrelated externs/types | `libAfterConf-afterconf-h.md` |
| 2486 | `libAfterImage/asimagexml.c` | libAfterImage | XML image composition engine (large, mixed responsibilities) | `libAfterImage-asimagexml.md` |
| 2121 | `libAfterImage/afterbase.c` | libAfterImage | base utilities + hash + XML + math in one TU | `libAfterImage-afterbase.md` |
| 2120 | `wayland/aswlicon.c` | Wayland suite UI | icon spec resolution + XDG theme search + decode/composite | `wayland-aswlicon.md` |
| 2044 | `src/Form/Form.c` | X11 module | single-file module (UI + parsing + X11 glue) | `src-form.md` |
| 2004 | `libAfterStep/decor.c` | libAfterStep | buttons + tiles + render + interaction in one TU | `libAfterStep-decor.md` |
| 2004 | `libAfterImage/draw.c` | libAfterImage | draw context + primitives + fill in one TU | `libAfterImage-draw.md` |
| 1951 | `src/afterstep/functions.c` | core WM (X11) | function execution + many handler families | `src-afterstep-functions.md` |
| 1911 | `src/afterstep/placement.c` | core WM (X11) | placement policy + move/resize grid + avoid-cover policy | `src-afterstep-placement.md` |
| 1879 | `libAfterStep/freestor.c` | config system | FreeStorage parsing/ops + conversions in one TU | `libAfterStep-freestor.md` |
| 1843 | `wayland/aswltheme.c` | Wayland suite UI | AfterStep theme reader + palette + gradients in one TU | `wayland-aswltheme.md` |
| 1800 | `src/afterstep/menus.c` | core WM (X11) | menu model + render + events + execution in one TU | `src-afterstep-menus.md` |
| 1764 | `libAfterStep/hints_common.c` | libAfterStep | hints merge + name/flag utilities in one TU | `libAfterStep-hints_common.md` |
| 1690 | `libAfterStep/asapp.c` | libAfterStep | global app init + dirs + function term registry | `libAfterStep-asapp.md` |
| 1661 | `src/WinList2/WinList.c` | X11 module | module main + model + UI/event loop in one TU | `src-winlist2.md` |
| 1573 | `libAfterImage/asimage.c` | libAfterImage | ASImage core operations concentrated in one TU | `libAfterImage-asimage.md` |
| 1552 | `libAfterImage/imencdec.c` | libAfterImage | mixed encode/decode paths + registry helpers | `libAfterImage-imencdec.md` |
| 1550 | `libAfterStep/clientprops.c` | libAfterStep | client property read/write helpers in one TU | `libAfterStep-clientprops.md` |
| 1549 | `libAfterStep/mystyle.c` | libAfterStep | style model + parsing + render glue in one TU | `libAfterStep-mystyle.md` |
| 1497 | `src/afterstep/decorations.c` | core WM (X11) | decoration model + render glue in one TU | `src-afterstep-decorations.md` |
| 1496 | `wayland/aswlfont.c` | Wayland suite UI | font load/layout/render in one TU | `wayland-aswlfont.md` |
| 1487 | `libAfterImage/asfont.c` | libAfterImage | font backend + layout helpers in one TU | `libAfterImage-asfont.md` |
| 1479 | `src/afterstep/pager.c` | core WM (X11) | pager model + policy + rendering glue in one TU | `src-afterstep-pager.md` |
| 1475 | `src/ASMount/main.c` | X11 module/tool | ASMount main + UI + ops in one TU | `src-asmount.md` |
| 1461 | `wayland/aswlbg.c` | Wayland suite UI | background helper (wl + render + config) in one TU | `wayland-aswlbg.md` |
| 1452 | `src/afterstep/window_frame.c` | core WM (X11) | frame layout + sizing + render glue in one TU | `src-afterstep-window_frame.md` |
| 1435 | `libAfterStep/wmprops.c` | libAfterStep | EWMH/WM properties helpers in one TU | `libAfterStep-wmprops.md` |
| 1419 | `libAfterBase/parse.c` | libAfterBase | parsing/token helpers concentrated in one TU | `libAfterBase-parse.md` |
| 1407 | `libAfterImage/import.c` | libAfterImage | import paths + dispatch + format glue in one TU | `libAfterImage-import.md` |
| 1399 | `src/Script/Instructions.c` | scripting | instruction set + exec loop in one TU | `src-script-instructions.md` |
| 1326 | `src/Gnome/Gnome.c` | X11 module | module main + protocol glue + policy in one TU | `src-gnome.md` |
| 1306 | `src/afterstep/dbus.c` | core WM (X11) | dbus connection + methods + signals in one TU | `src-afterstep-dbus.md` |
| 1289 | `libAfterImage/export.c` | libAfterImage | export paths + dispatch + format glue in one TU | `libAfterImage-export.md` |
| 1279 | `src/ASConfig/ASConfig.c` | tool/module | config tool main + model + IO in one TU | `src-asconfig.md` |
| 1236 | `src/afterstep/module.c` | core WM (X11) | module spawn/control + IPC helpers in one TU | `src-afterstep-module.md` |
| 1215 | `libAfterImage/apps/ascompose.c` | tool | ascompose main + parser + renderer in one TU | `libAfterImage-ascompose.md` |
| 1165 | `libAfterBase/audit.c` | libAfterBase | audit/logging helpers in one TU | `libAfterBase-audit.md` |
| 1150 | `libAfterStep/session.c` | libAfterStep | session discovery + save/restore helpers in one TU | `libAfterStep-session.md` |
| 1147 | `libAfterBase/layout.c` | libAfterBase | layout/geometry helpers in one TU | `libAfterBase-layout.md` |
| 1132 | `libAfterStep/canvas.c` | libAfterStep | ASCanvas implementation concentrated in one TU | `libAfterStep-canvas.md` |
| 1128 | `libAfterStep/desktop_category.c` | libAfterStep | category parsing + tree ops in one TU | `libAfterStep-desktop_category.md` |
| 1122 | `libAfterImage/asimage.h` | libAfterImage | umbrella header; mixed types, API, and format bits | `libAfterImage-asimage-h.md` |
| 1109 | `libAfterConf/AfterStep.c` | config system | core AfterStep config load/apply in one TU | `libAfterConf-AfterStep.md` |
| 1108 | `libASGTK/asgtkxmleditor.c` | GTK library | widget UI + model/IO glue in one TU | `libASGTK-asgtkxmleditor.md` |
| 1106 | `libAfterStep/moveresize.c` | libAfterStep | move/resize interaction + math in one TU | `libAfterStep-moveresize.md` |
| 1092 | `src/ASDocGen/xmlproc.c` | tool | XML processing helpers in one TU | `src-asdocgen-xmlproc.md` |
| 1083 | `libAfterStep/functions.c` | libAfterStep | function terms/registry helpers concentrated in one TU | `libAfterStep-functions.md` |
| 1070 | `src/asetroot/asetroot.c` | tool | asetroot main + IO + render glue in one TU | `src-asetroot.md` |
| 1063 | `src/afterstep/afterstep.c` | core WM (X11) | main init + event loop + wiring in one TU | `src-afterstep-afterstep.md` |
| 1050 | `src/ASDocGen/ASDocGen.c` | tool | doc generator main + output drivers in one TU | `src-asdocgen-main.md` |
| 1034 | `wayland/aswllock.c` | Wayland suite UI | lock UI + protocol glue in one TU | `wayland-aswllock.md` |
| 1027 | `src/Ident/Ident.c` | X11 module | ident module main + UI/event loop in one TU | `src-ident.md` |
| 1022 | `libAfterBase/regexp.c` | libAfterBase | regexp wrappers + helpers in one TU | `libAfterBase-regexp.md` |
| 1020 | `libAfterBase/xml.c` | libAfterBase | xml parse helpers + IO glue in one TU | `libAfterBase-xml.md` |
| 1001 | `libAfterStep/parser.c` | libAfterStep | parser helpers concentrated in one TU | `libAfterStep-parser.md` |

### Third-party (do not refactor)

These are bundled libraries (or imported portability headers). Keep them as-is; treat as vendor code.

| LOC | File | Notes |
| ---: | --- | --- |
| 5137 | `libAfterImage/libjpeg/jidctint.c` | libjpeg |
| 4951 | `perl-AfterImage/ppport.h` | Devel::PPPort output |
| 4348 | `libAfterImage/libjpeg/jfdctint.c` | libjpeg |
| 4284 | `libAfterImage/libpng/pngrtran.c` | libpng |
| 3548 | `libAfterImage/libpng/png.h` | libpng |
| 1675 | `libAfterImage/zlib/deflate.c` | zlib |

### Generated (do not refactor)

These are generated parser/lexer outputs. Refactor the `.l/.y` sources instead if needed.

| LOC | File |
| ---: | --- |
| 2676 | `src/Script/lex.yy.c` |
| 2334 | `src/Script/y.tab.c` |

## Conventions for breakup work

- Prefer **mechanical extraction first**: move a coherent group of functions + related types into a new translation unit
  without behavior changes; then do cleanup in follow-up patches.
- Introduce a small `*_internal.h` only when needed to share internal structs/prototypes across the new `.c` files.
- Update checked-in build files in the same patch (many subdirs have both `Makefile.in` and `Makefile`; `wayland/` has
  only `Makefile`).
- Validate with the narrowest build + smoke target for the touched subsystem:
  - Wayland: `make -C wayland` and `tools/wayland-smoke.sh`
  - X11 core/modules: `make -C src/afterstep` / `make -C src/<Module>` and `tools/xvfb-smoke.sh`
