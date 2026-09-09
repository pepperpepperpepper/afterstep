#!/usr/bin/env bash
# minimize-verify: drive the minimize feature end-to-end under Xvfb.
#   1. xterm (Xwayland) iconifies itself via WM_CHANGE_STATE -> view_set_minimized
#      (ICCCM path)
#   2. list_windows shows minimized; aswlctl focus_window restores
#   3. an EWMH _NET_WM_STATE client message (the pager/wmctrl path) minimizes
#      too — wlroots pre-flips xsurface->minimized before the request, so this
#      exercises the one-field guard in view_set_minimized
#   4. the window-list menu's ROW is clicked: the @focus_window dispatch
#      restores the hidden window through the menu (not via aswlctl)
#   5. the window-list popup's own shade-bar button (xdg set_minimized) hides it
# Usage: minimize-verify.sh <repo-root> <workdir>
set -u
ROOT="${1:?repo root}"
WD="${2:?workdir}"
cd "$ROOT" || exit 1

SOCKET=aswlcomp-minimize-$$
RUNTIME="$WD/runtime"
HOME_DIR="$WD/home"
LOG="$WD/aswlcomp.log"
SHOT="$WD/shot.png"
mkdir -p "$RUNTIME" "$HOME_DIR"
rm -f "$LOG"

# Re-exec under xvfb-run (the smoke script's pattern), pinning Mesa's EGL
# vendor when NVIDIA EGL is present — bare Xvfb core-dumps in that setup.
if [[ "${MV_IN_XVFB:-}" != "1" ]]; then
  if [[ -z "${__EGL_VENDOR_LIBRARY_FILENAMES:-}" ]] \
      && [[ -r /usr/share/glvnd/egl_vendor.d/10_nvidia.json ]] \
      && [[ -r /usr/share/glvnd/egl_vendor.d/50_mesa.json ]]; then
    export __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json
  fi
  exec xvfb-run -a -s "-screen 0 1024x768x24" env MV_IN_XVFB=1 "$0" "$ROOT" "$WD"
fi
XVFB_PID=""

export WAYLAND_DISPLAY="$SOCKET"
export XDG_RUNTIME_DIR="$RUNTIME"
export HOME="$HOME_DIR"
export WLR_BACKENDS=x11 WLR_RENDERER=pixman
export XDG_CURRENT_DESKTOP=AfterStep:wlroots

./wayland/aswlcomp --socket "$SOCKET" --state "$WD/state.bin" >"$LOG" 2>&1 &
COMP_PID=$!

cleanup() {
  kill "$COMP_PID" 2>/dev/null
  wait "$COMP_PID" 2>/dev/null
}
trap cleanup EXIT

for _ in $(seq 1 100); do
  [ -S "$RUNTIME/$SOCKET" ] && break
  kill -0 "$COMP_PID" 2>/dev/null || { echo "FAIL: compositor died"; tail -30 "$LOG"; exit 1; }
  sleep 0.05
done
[ -S "$RUNTIME/$SOCKET" ] || { echo "FAIL: no socket"; exit 1; }

CTL="./wayland/aswlctl"
FAIL=0

list_windows() { "$CTL" list_windows 2>/dev/null; }

win_state() { # win_state ID -> the non-geom row's state column
  list_windows | awk -F'\t' -v id="$1" '$1==id && $2!="geom" {print $4}'
}

# ── 1. Xwayland ICCCM path: xterm iconifies itself ──────────────────────────
# xterm occasionally dies at startup under load ("fatal IO error 11" on the
# Xwayland socket) — retry the spawn rather than flaking the whole rig.
XTERM_ID=""
for ATTEMPT in 1 2 3; do
  "$CTL" exec "xterm -geometry 80x24+100+100" || { echo "FAIL: exec xterm"; exit 1; }
  for _ in $(seq 1 200); do
    XTERM_ID=$(list_windows | awk -F'\t' '$4 ~ /xwayland/ && $4 ~ /^mapped/ {print $1; exit}')
    [ -n "$XTERM_ID" ] && break
    sleep 0.05
  done
  [ -n "$XTERM_ID" ] && break
  echo "xterm attempt $ATTEMPT never mapped; respawning"
  sleep 1
done
[ -n "$XTERM_ID" ] || { echo "FAIL: xterm never mapped"; list_windows; echo "-- log tail --"; tail -25 "$LOG"; exit 1; }
echo "xterm id=$XTERM_ID"

# The xterm is an XWAYLAND client: xdotool must talk to the compositor's
# embedded X server, not the outer Xvfb. Learn it from an exec'd child's env.
"$CTL" exec "sh -c 'echo \\$DISPLAY >$WD/xdisplay'" || true
sleep 0.4
XDISP=$(cat "$WD/xdisplay" 2>/dev/null || true)
echo "outer DISPLAY=$DISPLAY (xvfb-run); exec-children DISPLAY=${XDISP:-unknown}"
XD="DISPLAY=${XDISP:-:0}"
XWIN=$(env "$XD" xdotool search --onlyvisible --class XTerm 2>/dev/null | head -1)
[ -n "$XWIN" ] || XWIN=$(env "$XD" xdotool search --onlyvisible --name "arch@" 2>/dev/null | head -1)
if [ -z "$XWIN" ]; then
  echo "-- hunting the xterm across candidate displays --"
  for d in "$XDISP" :0 :1 :2 :98 :100 "$DISPLAY"; do
    [ -n "$d" ] || continue
    ids=$(DISPLAY=$d xdotool search --onlyvisible --class XTerm 2>/dev/null | head -1)
    echo "  $d -> ${ids:-none}"
    if [ -n "$ids" ]; then XWIN=$ids; XD="DISPLAY=$d"; XDISP=$d; break; fi
  done
fi
[ -n "$XWIN" ] || { echo "FAIL: no X window for xterm"; exit 1; }

# The EWMH helper: sends the _NET_WM_STATE(add, hidden) client message a
# pager or wmctrl sends. Built here because wmctrl is not assumed installed.
cat >"$WD/xewmh-minimize.c" <<'EOF'
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
	if (argc != 2) { fprintf(stderr, "usage: %s WINDOWID\n", argv[0]); return 2; }
	Display *d = XOpenDisplay(NULL);
	if (d == NULL) { fprintf(stderr, "no display\n"); return 1; }
	Window w = (Window)strtoul(argv[1], NULL, 0);
	Atom state = XInternAtom(d, "_NET_WM_STATE", False);
	Atom hidden = XInternAtom(d, "_NET_WM_STATE_HIDDEN", False);
	XEvent ev;
	memset(&ev, 0, sizeof(ev));
	ev.xclient.type = ClientMessage;
	ev.xclient.window = w;
	ev.xclient.message_type = state;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = 1; /* _NET_WM_STATE_ADD */
	ev.xclient.data.l[1] = hidden;
	ev.xclient.data.l[2] = 0;
	ev.xclient.data.l[3] = 1; /* source: application */
	ev.xclient.data.l[4] = 0;
	XSendEvent(d, DefaultRootWindow(d), False,
	           SubstructureRedirectMask | SubstructureNotifyMask, &ev);
	XFlush(d);
	XCloseDisplay(d);
	return 0;
}
EOF
cc -O2 -o "$WD/xewmh-minimize" "$WD/xewmh-minimize.c" -lX11 \
  || { echo "FAIL: cannot build EWMH helper (libX11 dev missing?)"; exit 1; }

env "$XD" xdotool windowminimize "$XWIN"
sleep 0.8

grep -q "aswlcomp: xwayland request_minimize=1" "$LOG" || { echo "FAIL: no request_minimize log"; FAIL=1; }
grep -q "aswlcomp: minimize view $XTERM_ID -> 1" "$LOG" || { echo "FAIL: no minimize-verb log"; FAIL=1; }
STATE=$(win_state "$XTERM_ID")
echo "after icccm minimize: state=$STATE"
[[ "$STATE" == *minimized* && "$STATE" == *mapped* ]] || { echo "FAIL: not listed as minimized"; FAIL=1; }

# ── 2. Restore via focus_window (the control-protocol verb) ─────────────────
RC=0; "$CTL" focus_window "$XTERM_ID" 2>"$WD/focus.err" || RC=$?
echo "focus_window rc=$RC stderr:[$(cat "$WD/focus.err" 2>/dev/null | tr '\n' ';')]"
sleep 0.8
grep -q "aswlcomp: minimize view $XTERM_ID -> 0" "$LOG" || { echo "FAIL: no restore log"; FAIL=1; }
STATE=$(win_state "$XTERM_ID")
echo "after ctl restore: state=$STATE"
[[ "$STATE" != *minimized* ]] || { echo "FAIL: still minimized"; FAIL=1; }
[[ "$STATE" == *focused* ]] || { echo "FAIL: not focused after restore"; FAIL=1; }

# ── 3. EWMH path: pager-style _NET_WM_STATE message ────────────────────────
# wlroots pre-flips xsurface->minimized on this path BEFORE the request fires,
# so the guard in view_set_minimized must read the compositor's own flag only
# or this minimize is silently dropped (window stays on screen).
env "$XD" "$WD/xewmh-minimize" "$XWIN"
sleep 0.8
N=$(grep -c "aswlcomp: minimize view $XTERM_ID -> 1" "$LOG")
echo "ewmh minimize: verb fired ${N}x total (want 2)"
[ "$N" -ge 2 ] || { echo "FAIL: EWMH minimize dropped (guard read the pre-flipped flag?)"; FAIL=1; }
STATE=$(win_state "$XTERM_ID")
echo "after ewmh minimize: state=$STATE"
[[ "$STATE" == *minimized* ]] || { echo "FAIL: EWMH minimize not reflected in window list"; FAIL=1; }

# ── 4. Restore by CLICKING the window-list row (the menu's own path) ────────
"$CTL" exec "env ASWLMENU_GEOM_DEBUG=1 $ROOT/wayland/aswlmenu --windows" || { echo "FAIL: exec menu"; exit 1; }
MENU_ID=""
for _ in $(seq 1 100); do
  MENU_ID=$(list_windows | awk -F'\t' '$5 ~ /app_id=afterstep.aswlmenu/ {print $1; exit}')
  [ -n "$MENU_ID" ] && break
  sleep 0.05
done
[ -n "$MENU_ID" ] || { echo "FAIL: menu never listed"; list_windows; exit 1; }
sleep 0.8
echo "menu id=$MENU_ID"
MGEO=$(list_windows | awk -F'\t' -v id="$MENU_ID" '$1==id && $2=="geom" {print $0}')
MX=$(echo "$MGEO" | grep -o 'x=[0-9]*' | head -1 | cut -d= -f2)
MY=$(echo "$MGEO" | grep -o 'y=[0-9]*' | head -1 | cut -d= -f2)
MW=$(echo "$MGEO" | grep -o 'w=[0-9]*' | head -1 | cut -d= -f2)
BAND=$(grep -o 'aswlmenu: row band y=[0-9]* h=[0-9]* rows=[0-9]*' "$LOG" | tail -1)
echo "menu geom: ${MGEO:-?}; band: ${BAND:-MISSING}"
if [ -n "$BAND" ] && [ -n "$MX" ]; then
  BY=$(echo "$BAND" | sed -E 's/.*y=([0-9]+).*/\1/')
  BH=$(echo "$BAND" | sed -E 's/.*h=([0-9]+).*/\1/')
  ROWS=$(echo "$BAND" | sed -E 's/.*rows=([0-9]+).*/\1/')
  [ "$ROWS" -ge 1 ] 2>/dev/null || { echo "FAIL: window list has no rows"; FAIL=1; }
  CX=$((MX + MW / 2))
  CY=$((MY + BY + BH / 2))
  echo "clicking row 0 ($CX,$CY)"
  xdotool mousemove --sync "$CX" "$CY"
  sleep 0.3
  xdotool click 1
  sleep 0.8
  grep -q "aswlmenu: compositor focus_window=$XTERM_ID" "$LOG" \
    || { echo "FAIL: row click never dispatched focus_window (parser?)"; FAIL=1; }
  grep -q "aswlmenu: invalid focus_window id" "$LOG" \
    && { echo "FAIL: invalid focus_window id logged (off-by-one back?)"; FAIL=1; }
  N=$(grep -c "aswlcomp: minimize view $XTERM_ID -> 0" "$LOG")
  echo "row-click restore: restore verb fired ${N}x total (want 2)"
  [ "$N" -ge 2 ] || { echo "FAIL: row click did not restore"; FAIL=1; }
  STATE=$(win_state "$XTERM_ID")
  echo "after row-click restore: state=$STATE"
  [[ "$STATE" != *minimized* ]] || { echo "FAIL: still minimized after row click"; FAIL=1; }
else
  echo "FAIL: no row-band metrics or menu geometry"; FAIL=1
fi

# ── 5. The popup's own shade-bar button (xdg set_minimized) ─────────────────
"$CTL" exec "env ASWLMENU_GEOM_DEBUG=1 $ROOT/wayland/aswlmenu --windows" || { echo "FAIL: exec menu (2)"; exit 1; }
MENU_ID=""
for _ in $(seq 1 100); do
  MENU_ID=$(list_windows | awk -F'\t' '$5 ~ /app_id=afterstep.aswlmenu/ {print $1; exit}')
  [ -n "$MENU_ID" ] && break
  sleep 0.05
done
[ -n "$MENU_ID" ] || { echo "FAIL: menu never listed (2)"; exit 1; }
sleep 0.8

BTN=$(grep -o 'aswlmenu: iconize button +[0-9]*+[0-9]* [0-9]*x[0-9]*' "$LOG" | tail -1)
echo "button metrics line: ${BTN:-MISSING}"
if [ -n "$BTN" ]; then
  BX=$(echo "$BTN" | sed -E 's/.*\+([0-9]+)\+[0-9]+ .*/\1/')
  BY=$(echo "$BTN" | sed -E 's/.*\+[0-9]+\+([0-9]+) .*/\1/')
  BW=$(echo "$BTN" | sed -E 's/.* ([0-9]+)x[0-9]*/\1/')
  BH=$(echo "$BTN" | sed -E 's/.* [0-9]+x([0-9]*)/\1/')
  MGEO=$(list_windows | awk -F'\t' -v id="$MENU_ID" '$1==id && $2=="geom" {print $0}')
  MX=$(echo "$MGEO" | grep -o 'x=[0-9]*' | head -1 | cut -d= -f2)
  MY=$(echo "$MGEO" | grep -o 'y=[0-9]*' | head -1 | cut -d= -f2)
  echo "menu geom: ${MGEO:-?}; button local +${BX}+${BY} ${BW}x${BH}"
  if [ -n "$MX" ] && [ -n "$BX" ] && [ -n "$BW" ]; then
    CX=$((MX + BX + BW / 2))
    CY=$((MY + BY + BH / 2))
    echo "clicking ($CX,$CY)"
    xdotool mousemove --sync "$CX" "$CY"
    sleep 0.3
    xdotool click 1
    sleep 0.8
    grep -q "aswlcomp: xdg request_minimize" "$LOG" || { echo "FAIL: no xdg request_minimize"; FAIL=1; }
    grep -q "aswlcomp: minimize view $MENU_ID -> 1" "$LOG" || { echo "FAIL: menu not minimized by verb"; FAIL=1; }
    MSTATE=$(win_state "$MENU_ID")
    echo "menu after button: state=$MSTATE"
    [[ "$MSTATE" == *minimized* ]] || { echo "FAIL: menu not listed minimized"; FAIL=1; }
    "$CTL" focus_window "$MENU_ID"
    sleep 0.8
    MSTATE=$(win_state "$MENU_ID")
    echo "menu after restore: state=$MSTATE"
    [[ "$MSTATE" != *minimized* && "$MSTATE" == *mapped* ]] || { echo "FAIL: menu not restored"; FAIL=1; }
    grep -q "aswlcomp: minimize view $MENU_ID -> 0" "$LOG" || { echo "FAIL: no menu restore log"; FAIL=1; }
  else
    echo "FAIL: missing geometry for click"; FAIL=1
  fi
else
  echo "FAIL: menu did not log its iconize metrics"; FAIL=1
fi

# Screenshot the Xvfb screen to locate the popup's title-bar buttons.
if command -v import >/dev/null 2>&1; then
  import -window root "$SHOT" 2>/dev/null ||SHOT=""
elif command -v xwd >/dev/null 2>&1; then
  xwd -root -silent | convert xwd:- "$SHOT" 2>/dev/null || SHOT=""
fi
[ -n "${SHOT:-}" ] && [ -s "$SHOT" ] && echo "screenshot: $SHOT" || echo "screenshot unavailable"

echo "---- compositor log (minimize lines) ----"
grep -n "minimize" "$LOG" | tail -10

if [ "$FAIL" -eq 0 ]; then
  echo "OK: icccm + ewmh minimize, ctl/row/button restore all verified"
else
  echo "VERIFICATION FAILURES PRESENT"
fi
exit $FAIL
