#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat 1>&2 <<'EOF'
Usage: tools/wayland-screenshots.sh [--out DIR] [--upload]

Generates a Wayland screenshot gallery for the wlroots-based compositor scaffold (`wayland/aswlcomp`)
by running it nested under Xvfb (WLR_BACKENDS=x11) and capturing key screens.

Outputs:
  DIR/01-desktop.png
  DIR/02-menu.png
  DIR/03-clients.png
  DIR/04-clients-menu.png
  DIR/05-workspaces.png
  DIR/06-dockapp.png
  DIR/index.html
  DIR/aswlcomp.log
  DIR/aswlpanel-top.conf
  DIR/aswlpanel-right-pager.conf
  DIR/aswlpanel-right-dock.conf
  DIR/aswlmenu.conf

Options:
  --out DIR   Output directory (default: screenshots/YYYY-MM-DD-wayland)
  --upload    Upload a hosted gallery via `wtf-upload` (prints the index.html URL)
  --no-upload Skip upload (local files only) (default)
EOF
}

require_cmd() {
  local cmd="$1"
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "Error: '$cmd' is required." >&2
    exit 2
  fi
}

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd -- "${script_dir}/.." && pwd -P)"
host_home="${HOME:-}"

out_dir=""
do_upload=0
as_pid=""
upload_index=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --out)
      out_dir="${2:-}"; shift 2
      ;;
    --upload)
      do_upload=1; shift
      ;;
    --no-upload)
      do_upload=0; shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Error: unknown argument: $1" >&2
      usage
      exit 2
      ;;
  esac
done

if [[ -z "${out_dir}" ]]; then
  out_dir="${repo_root}/screenshots/$(date +%Y-%m-%d)-wayland"
fi

mkdir -p -- "${out_dir}"

# Re-exec under Xvfb so we can drive wlroots' x11 backend in CI/headless environments.
if [[ "${ASWL_IN_XVFB:-}" != "1" ]]; then
  require_cmd xvfb-run

  # Some environments have NVIDIA EGL installed but no usable EGL/GBM backend,
  # which can make Xvfb crash at startup when GLX is enabled. If we detect that
  # setup, force Mesa's EGL vendor to keep Xvfb usable.
  if [[ -z "${__EGL_VENDOR_LIBRARY_FILENAMES:-}" ]] \
      && [[ -r /usr/share/glvnd/egl_vendor.d/10_nvidia.json ]] \
      && [[ -r /usr/share/glvnd/egl_vendor.d/50_mesa.json ]]; then
    export __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json
  fi

  exec xvfb-run -a -s "-screen 0 1600x900x24" env \
    ASWL_IN_XVFB=1 \
    "${0}" --out "${out_dir}" $( ((do_upload)) && printf '%s' --upload || printf '%s' --no-upload )
fi

require_cmd xwininfo
require_cmd import
require_cmd identify
require_cmd make
require_cmd cc
require_cmd pkg-config
require_cmd pgrep
require_cmd xdotool

cd -- "${repo_root}"

make -C wayland aswlcomp aswlpanel aswlmenu aswlctl aswlbg aswlbanner

tmp_home="$(mktemp -d)"
runtime_dir="$(mktemp -d)"
chmod 700 "${runtime_dir}"

top_panel_cfg="${out_dir}/aswlpanel-top.conf"
top_winlist_cfg="${out_dir}/aswlpanel-top-winlist.conf"
right_pager_cfg="${out_dir}/aswlpanel-right-pager.conf"
right_dock_cfg="${out_dir}/aswlpanel-right-dock.conf"
right_dock_theme_cfg="${out_dir}/aswltheme-right-dock.conf"
menu_cfg="${out_dir}/aswlmenu.conf"
log_file="${out_dir}/aswlcomp.log"

cleanup() {
  if [[ -n "${as_pid:-}" ]]; then
    kill "${as_pid}" 2>/dev/null || true
    wait "${as_pid}" 2>/dev/null || true
    as_pid=""
  fi

  if [[ -n "${upload_index:-}" ]]; then
    rm -f -- "${upload_index}" || true
    upload_index=""
  fi

  rm -rf -- "${tmp_home}" "${runtime_dir}"
}
trap cleanup EXIT

# Dock/panel geometry (match classic AfterStep Wharf/WinList/Pager positioning).
top_dock_h=64
top_dock_btns=15
top_dock_cross=$((top_dock_h))
top_dock_w=$((top_dock_btns * top_dock_cross))

# X11 baseline geometry (1600x900):
# - Right pager: 102px wide at x=1429..1530, starting at y=47.
# - Right dock strip: 64px wide at x=1531..1594 with a 5px right margin, starting at y=4.
# - WinList strip: 462x32+964+46 (gap between top dock and right sidebar).
right_pager_w=102
right_pager_h=313
right_pager_margin_top=47

right_dock_w=64
right_dock_margin=5
right_reserved=$((right_dock_w + right_dock_margin))
right_dock_margin_top=4

right_gap=3
top_winlist_h=32
top_winlist_margin_top=46
top_winlist_margin_right=$((right_pager_w + right_reserved + right_gap))

terminal_app_id="ASWLShotXTerm"
default_terminal_cmd="xterm -class ${terminal_app_id} -geometry 80x24 -fa Monospace -fs 12 -T aswlshot-xterm"
terminal_cmd="${TERMINAL:-${default_terminal_cmd}}"
wayland_demo_cmd="weston-simple-shm"

{
  echo "# Screenshot run top dock (Wharf-ish) config."
  echo "@edge top"
  echo "@anchor left top"
  echo "@margin left 1"
  echo "@margin top 1"
  echo "@width ${top_dock_w}"
  echo "@dock"
  cat <<'EOF'
Wharf extras|normal/Info=:
term|normal/Terminal=${TERMINAL:-xterm -fa Monospace -fs 12}
MidnightCommander|normal/MCInMonitorShadow=:
GUIFileManager|normal/Filecabinet2=:
Konqueror|logos/konqueror=:
WebBrowser|normal/WWW=:
Mail|normal/MailBox2=:
ImageEditor|logos/gimp=:
TextEditor|Text.xpm=:
WordProcessor|normal/Document=:
Spreadsheet|normal/Table=:
IRC|normal/IRCTransparent=:
Games|normal/GameController=:
AudioPlayer|normal/MusicalNote=:
mixer|normal/Speaker=:
EOF
} >"${top_panel_cfg}"

# A simple WinList-ish strip: show the focused window title in the gap between
# the top dock and the right sidebar, similar to the X11 screenshot.
cat >"${top_winlist_cfg}" <<EOF
# Screenshot run top "WinList" strip (focused window title).
@edge top
@nodock
@height ${top_winlist_h}
@margin top ${top_winlist_margin_top}
@margin left $((top_dock_w + 4))
@margin right ${top_winlist_margin_right}
EOF

cat >"${right_pager_cfg}" <<EOF
# Screenshot run right-side pager-ish config (sits left of the dock strip).
@edge right
@anchor right top
@width ${right_pager_w}
@height ${right_pager_h}
@pager_columns 2
@pager_rows 2
@margin top ${right_pager_margin_top}
@margin right ${right_reserved}
@nodock
Work|normal/Desktop=@workspace 1
WWW|normal/WWW=@workspace 2
Mail|normal/Mail=@workspace 3
Games|normal/Cardgames=@workspace 4
EOF

cat >"${right_dock_cfg}" <<EOF
# Screenshot run right-side dock/wharf-ish strip.
@edge right
@anchor right top bottom
@width ${right_dock_w}
@margin right ${right_dock_margin}
@margin top ${right_dock_margin_top}
@dock
AudioPlayer|normal/MusicalNote=:
afterstepdoc|large/AfterStep3=:
WharfExtras|normal/Info=:
ToolsFolder|normal/Desktop,normal/HammerBrown,normal/HammerRed,dots/3_dots=:
XEyes=@xeyes
QuitFolder|normal/RedLight,dots/3_dots=:
asfsm|normal/Harddrive=:
loadmonitor|normal/Monitor1=:
loadinstantmonitor|normal/Monitor1=:
asmon|normal/Monitor1=:
wmtop|normal/Monitor1=:
netmonitor|normal/Ethernet=:
asmail|normal/MailBox2=:
clock=@clock
EOF

cat >"${right_dock_theme_cfg}" <<'EOF'
# Match classic X11 MonitorWharf styling (uses MyStyle BackPixmap 149).
PanelStyle=*MonitorWharfTile
EOF

cat >"${menu_cfg}" <<'EOF'
# Screenshot run menu config.
@title AfterStep 2.2.12
@no_desktop_entries
Applications|Folder-Gear2=@submenu
Find...|Folder-MagnifyingGlass=@submenu
Desktop Setup|Folder-Desktop2=@submenu
Modules|Folder-Puzzle=@submenu
Run ...|normal/Run=:
Screen Savers|Folder-MoonInMonitor=@submenu
System Settings|Folder-Gear2=@submenu
Windows|Folder-Windows=@submenu
XTerminal|mini-app.xpm=${TERMINAL:-xterm -fa Monospace -fs 12}
Quit|Folder-Stopsign=@submenu
About AfterStep|Folder-Info=@submenu
EOF

export HOME="${tmp_home}"
export XDG_RUNTIME_DIR="${runtime_dir}"
export XDG_CURRENT_DESKTOP=AfterStep:wlroots
export XDG_SESSION_DESKTOP=AfterStep
export XDG_SESSION_TYPE=wayland

export WLR_BACKENDS=x11
export WLR_X11_FULLSCREEN=1
export WLR_RENDERER=pixman
export ASWLCOMP_OUTPUT_WIDTH=1600
export ASWLCOMP_OUTPUT_HEIGHT=900

export ASWLCOMP_WORKSPACES=4
# The menu/launcher requests a Wayland keyboard. If a headless wlroots/x11 + Xvfb
# environment misbehaves with a keyboard present, rerun with:
#   ASWLCOMP_DISABLE_KEYBOARD=1 tools/wayland-screenshots.sh ...
export ASWLMENU_CONFIG="${menu_cfg}"

socket="aswlcomp-shot-$$"

demo_bin="${tmp_home}/aswlx11dockapp-demo"
cc -O2 -g -std=c11 -Wall -Wextra -Wformat=2 -Wshadow -Wpointer-arith \
  -o "${demo_bin}" wayland/aswlx11dockapp-demo.c $(pkg-config --cflags --libs x11)

rm -f -- "${log_file}"
./wayland/aswlcomp --socket "${socket}" --autostart /dev/null >"${log_file}" 2>&1 &
as_pid=$!

wait_for_socket() {
  for _ in $(seq 1 400); do
    if [[ -S "${XDG_RUNTIME_DIR}/${socket}" ]]; then
      return 0
    fi
    if ! kill -0 "${as_pid}" 2>/dev/null; then
      return 1
    fi
    sleep 0.05
  done
  return 1
}

require_comp_alive() {
  if [[ -z "${as_pid:-}" ]]; then
    echo "aswlcomp PID missing" >&2
    exit 1
  fi
  if ! kill -0 "${as_pid}" 2>/dev/null; then
    echo "aswlcomp died unexpectedly. Log tail:" >&2
    tail -n 200 "${log_file}" >&2 || true
    exit 1
  fi
}

if ! wait_for_socket; then
  echo "Wayland socket did not appear (aswlcomp crashed?). Log tail:" >&2
  tail -n 200 "${log_file}" >&2 || true
  exit 1
fi

# Start the background + banner early, but delay panels/docks until after the
# first capture so 01-desktop matches the X11 gallery ("early init" without the
# module UI visible yet).
require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "./wayland/aswlbg" || true
sleep 0.1
require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "ASWLBANNER_TINT='#0e7f7f7f' ./wayland/aswlbanner" || true
sleep 0.1

get_comp_window_id() {
  xwininfo -root -tree | awk '/^[[:space:]]*0x[0-9a-f]+/ {print $1; exit}'
}

wait_for_comp_window() {
  for _ in $(seq 1 200); do
    require_comp_alive
    local wid
    wid="$(get_comp_window_id)"
    if [[ -n "${wid}" && "${wid}" =~ ^0x[0-9a-f]+$ ]]; then
      printf '%s' "${wid}"
      return 0
    fi
    sleep 0.05
  done
  return 1
}

win_id="$(wait_for_comp_window)" || {
  echo "Failed to locate compositor X11 window under Xvfb." >&2
  tail -n 200 "${log_file}" >&2 || true
  exit 1
}

snap() {
  local file="$1"
  require_comp_alive
  import -window "${win_id}" "${file}"
}

move_pointer() {
  local x="${1:?x required}"
  local y="${2:?y required}"
  xdotool mousemove --sync "${x}" "${y}" >/dev/null 2>&1 || true
}

focus_window_by_app_id() {
  local app_id="${1:-}"
  focus_window_by_match "${app_id}" ""
}

focus_window_by_match() {
  local app_id="${1:-}"
  local title_substr="${2:-}"
  local wid=""
  wid="$(WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl list_windows 2>/dev/null | awk -F'\t' -v want_app="${app_id}" -v want_title="${title_substr}" '
    BEGIN { id = "" }
    {
      got_app = $5
      sub(/^app_id=/, "", got_app)
      got_app = tolower(got_app)

      got_title = $6
      sub(/^title=/, "", got_title)
      got_title = tolower(got_title)

      want_app = tolower(want_app)
      want_title = tolower(want_title)

      match_app = (want_app != "" && got_app == want_app)
      match_title = (want_title != "" && index(got_title, want_title) > 0)
      if ((match_app || match_title) && id == "") id = $1
    }
    END { print id }
  ' || true)"
  if [[ -n "${wid}" ]]; then
    WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl focus_window "${wid}" >/dev/null 2>&1 || true
  fi
}

window_ids_by_app_id() {
  local app_id="$1"
  WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl list_windows 2>/dev/null | awk -F'\t' -v want="${app_id}" '
    {
      got = $5
      sub(/^app_id=/, "", got)
      if (tolower(got) == tolower(want)) {
        print $1
      }
    }
  ' || true
}

close_windows_by_app_id() {
  local app_id="$1"
  local ids=()
  mapfile -t ids < <(window_ids_by_app_id "${app_id}") || true
  if [[ "${#ids[@]}" -eq 0 ]]; then
    return 0
  fi

  for id in "${ids[@]}"; do
    require_comp_alive
    WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl close_window "${id}" >/dev/null 2>&1 || true
  done
}

wait_until_app_id_gone() {
  local app_id="$1"
  local ids=()
  for _ in $(seq 1 200); do
    require_comp_alive
    ids=()
    mapfile -t ids < <(window_ids_by_app_id "${app_id}") || true
    if [[ "${#ids[@]}" -eq 0 ]]; then
      return 0
    fi
    sleep 0.05
  done
  return 1
}

wait_for_mapped_app_id() {
  local app_id="${1:-}"
  local title_substr="${2:-}"
  local max_tries="${3:-200}"
  for _ in $(seq 1 200); do
    if [[ "${max_tries}" -le 0 ]]; then
      break
    fi
    require_comp_alive
    if WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl list_windows 2>/dev/null | awk -F'\t' -v want_app="${app_id}" -v want_title="${title_substr}" '
      BEGIN { found = 0 }
      {
        got_app = $5
        sub(/^app_id=/, "", got_app)
        got_app = tolower(got_app)

        got_title = $6
        sub(/^title=/, "", got_title)
        got_title = tolower(got_title)

        want_app = tolower(want_app)
        want_title = tolower(want_title)

        match_app = (want_app != "" && got_app == want_app)
        match_title = (want_title != "" && index(got_title, want_title) > 0)
        if ($4 ~ /mapped/ && (match_app || match_title)) found = 1
      }
      END { exit found ? 0 : 1 }
    ' >/dev/null; then
      return 0
    fi
    sleep 0.05
    max_tries=$((max_tries - 1))
  done
  return 1
}

ensure_mapped_window() {
  local desc="$1"
  local cmd="$2"
  local want_app_id="${3:-}"
  local want_title_substr="${4:-}"
  local attempts="${5:-3}"

  for attempt in $(seq 1 "${attempts}"); do
    require_comp_alive
    WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "${cmd}" || true
    if wait_for_mapped_app_id "${want_app_id}" "${want_title_substr}" 200; then
      return 0
    fi

    echo "Warning: ${desc} did not map in time (attempt ${attempt}/${attempts}); retrying..." >&2
    sleep 0.25
  done

  echo "Error: timed out waiting for ${desc} to map." >&2
  WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl list_windows 2>/dev/null || true
  echo "aswlcomp log tail:" >&2
  tail -n 200 "${log_file}" >&2 || true
  return 1
}

kill_children_matching() {
  local pattern="$1"
  local signal="${2:-TERM}"
  local pids=()

  if [[ -z "${as_pid:-}" ]]; then
    return 0
  fi

  mapfile -t pids < <(pgrep -P "${as_pid}" -f "${pattern}" 2>/dev/null || true) || true
  if [[ "${#pids[@]}" -eq 0 ]]; then
    return 0
  fi

  kill -s "${signal}" "${pids[@]}" 2>/dev/null || true
}

open_menu_and_wait() {
  require_comp_alive
  close_menu_if_open
  WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "./wayland/aswlmenu" || true
  if ! wait_for_mapped_app_id "afterstep.aswlmenu"; then
    echo "Error: aswlmenu did not appear in time." >&2
    WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl list_windows 2>/dev/null || true
    return 1
  fi
  sleep 0.2
}

open_window_list_menu_and_wait() {
  require_comp_alive
  close_menu_if_open
  WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "./wayland/aswlmenu --windows" || true
  if ! wait_for_mapped_app_id "afterstep.aswlmenu"; then
    echo "Error: aswlmenu (window list) did not appear in time." >&2
    WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl list_windows 2>/dev/null || true
    return 1
  fi
  sleep 0.2
}

close_menu_if_open() {
  close_windows_by_app_id "afterstep.aswlmenu"
  if ! wait_until_app_id_gone "afterstep.aswlmenu"; then
    close_windows_by_app_id "afterstep.aswlmenu"
    if ! wait_until_app_id_gone "afterstep.aswlmenu"; then
      echo "Warning: forcing aswlmenu termination (close request ignored)" >&2
      kill_children_matching "aswlmenu" TERM
      if ! wait_until_app_id_gone "afterstep.aswlmenu"; then
        kill_children_matching "aswlmenu" KILL
      fi
      if wait_until_app_id_gone "afterstep.aswlmenu"; then
        sleep 0.05
        return 0
      fi
      echo "Error: aswlmenu did not close in time." >&2
      WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl list_windows 2>/dev/null || true
      return 1
    fi
  fi
  sleep 0.05
}

# Give aswlbg (background XML compositor via libAfterImage) time to paint before
# the first capture. This can take a bit longer on some systems.
sleep 2
snap "${out_dir}/01-desktop.png"

# The X11 baseline's Banner is only visible during early init. Stop it before we
# bring up the panels and menus so subsequent shots match the baseline.
kill_children_matching "aswlbanner" TERM
sleep 0.1

require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "ASWLPANEL_HEIGHT=${top_dock_h} ASWLPANEL_CONFIG='${top_panel_cfg}' ./wayland/aswlpanel" || true
require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "ASWLPANEL_EXCLUSIVE_ZONE=0 ASWLPANEL_WINDOW_LIST=focused ASWLPANEL_CONFIG='${top_winlist_cfg}' ./wayland/aswlpanel" || true
require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "ASWLPANEL_EXCLUSIVE_ZONE=0 ASWLPANEL_CLOCK_OVERRIDE=05:45 ASWLTHEME_CONFIG='${right_dock_theme_cfg}' ASWLPANEL_CONFIG='${right_dock_cfg}' ./wayland/aswlpanel" || true
require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "ASWLPANEL_EXCLUSIVE_ZONE=0 ASWLPANEL_MODE=pager ASWLPANEL_CONFIG='${right_pager_cfg}' ./wayland/aswlpanel" || true

sleep 0.3
move_pointer 48 100
open_menu_and_wait
snap "${out_dir}/02-menu.png"
close_menu_if_open

require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl workspace 1 || true
require_comp_alive
close_windows_by_app_id "${terminal_app_id}"
ensure_mapped_window "xterm" "${terminal_cmd}" "${terminal_app_id}" "aswlshot-xterm"
require_comp_alive
close_windows_by_app_id "XEyes"
ensure_mapped_window "xeyes" "xeyes" "XEyes" "xeyes"
focus_window_by_app_id "${terminal_app_id}"
for _ in $(seq 1 30); do
  focus_window_by_match "XEyes" "xeyes"
  if WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl list_windows 2>/dev/null | awk -F'\t' '
    BEGIN { found = 0 }
    BEGIN { want_app = tolower("XEyes"); want_title = tolower("xeyes"); }
    {
      got_app = $5
      sub(/^app_id=/, "", got_app)
      got_app = tolower(got_app)
      got_title = $6
      sub(/^title=/, "", got_title)
      got_title = tolower(got_title)
      if ($4 ~ /focused/ && ((got_app == want_app) || (index(got_title, want_title) > 0))) found = 1
    }
    END { exit found ? 0 : 1 }
  ' >/dev/null; then
    break
  fi
  sleep 0.05
done
sleep 0.3
snap "${out_dir}/03-clients.png"

move_pointer 707 402
open_window_list_menu_and_wait
snap "${out_dir}/04-clients-menu.png"
close_menu_if_open

require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl workspace 2 || true
require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "${wayland_demo_cmd}" || true
for _ in $(seq 1 200); do
  require_comp_alive
  if WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl list_windows 2>/dev/null | awk -F'\t' '
    BEGIN { found = 0 }
    $2 == "ws=2" && $4 ~ /mapped/ { found = 1 }
    END { exit found ? 0 : 1 }
  ' >/dev/null; then
    break
  fi
  sleep 0.05
done
sleep 0.3
snap "${out_dir}/05-workspaces.png"

require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl workspace 1 || true
require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "${demo_bin} --size 96 --title 'dockapp' --seconds 120" || true
sleep 0.6
snap "${out_dir}/06-dockapp.png"

require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl quit || true
for _ in $(seq 1 200); do
  if ! kill -0 "${as_pid}" 2>/dev/null; then
    break
  fi
  sleep 0.05
done
kill "${as_pid}" 2>/dev/null || true
wait "${as_pid}" 2>/dev/null || true

date_stamp="$(date +%Y-%m-%d)"
if [[ "$(basename -- "${out_dir}")" =~ ^([0-9]{4}-[0-9]{2}-[0-9]{2})-wayland$ ]]; then
  date_stamp="${BASH_REMATCH[1]}"
fi
cat >"${out_dir}/index.html" <<EOF
<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>AfterStep screenshots (${date_stamp}, Wayland/aswlcomp)</title>
    <style>
      :root {
        color-scheme: light dark;
      }
      body {
        font-family: system-ui, -apple-system, Segoe UI, Roboto, Ubuntu, Cantarell, Noto Sans,
          Helvetica, Arial, sans-serif;
        margin: 2rem;
        line-height: 1.4;
      }
      h1 {
        margin: 0 0 0.5rem;
        font-size: 1.5rem;
      }
      p {
        margin: 0 0 1.25rem;
        max-width: 75ch;
      }
      .grid {
        display: grid;
        grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
        gap: 1rem;
      }
      figure {
        margin: 0;
        padding: 0.75rem;
        border: 1px solid rgba(127, 127, 127, 0.35);
        border-radius: 12px;
        background: rgba(127, 127, 127, 0.06);
      }
      figcaption {
        margin-top: 0.5rem;
        font-size: 0.95rem;
      }
      img {
        width: 100%;
        height: auto;
        display: block;
        border-radius: 8px;
      }
      a {
        color: inherit;
        text-underline-offset: 2px;
      }
      code {
        font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono",
          "Courier New", monospace;
        font-size: 0.95em;
      }
    </style>
  </head>
  <body>
    <h1>AfterStep screenshots (${date_stamp}, Wayland/aswlcomp)</h1>
    <p>
      Captured under Xvfb using the wlroots-based compositor scaffold (<code>wayland/aswlcomp</code>)
      with the panel (<code>aswlpanel</code>), launcher menu (<code>aswlmenu</code>), and mixed
      Wayland/Xwayland clients. Click any image to open the full-size PNG.
    </p>
    <div class="grid">
      <figure>
        <a href="01-desktop.png">
          <img src="01-desktop.png" alt="aswlcomp desktop with panel" />
        </a>
        <figcaption><strong>Desktop</strong> — <a href="01-desktop.png">01-desktop.png</a></figcaption>
      </figure>
      <figure>
        <a href="02-menu.png">
          <img src="02-menu.png" alt="aswlmenu launcher open" />
        </a>
        <figcaption><strong>Menu</strong> — <a href="02-menu.png">02-menu.png</a></figcaption>
      </figure>
      <figure>
        <a href="03-clients.png">
          <img src="03-clients.png" alt="Wayland and Xwayland clients" />
        </a>
        <figcaption><strong>Clients</strong> — <a href="03-clients.png">03-clients.png</a></figcaption>
      </figure>
      <figure>
        <a href="04-clients-menu.png">
          <img src="04-clients-menu.png" alt="Wayland and Xwayland clients with the menu open" />
        </a>
        <figcaption><strong>Clients + menu</strong> — <a href="04-clients-menu.png">04-clients-menu.png</a></figcaption>
      </figure>
      <figure>
        <a href="05-workspaces.png">
          <img src="05-workspaces.png" alt="Workspace switching" />
        </a>
        <figcaption><strong>Workspaces</strong> — <a href="05-workspaces.png">05-workspaces.png</a></figcaption>
      </figure>
      <figure>
        <a href="06-dockapp.png">
          <img src="06-dockapp.png" alt="Xwayland dockapp-style window" />
        </a>
        <figcaption><strong>Dockapp</strong> — <a href="06-dockapp.png">06-dockapp.png</a></figcaption>
      </figure>
    </div>
  </body>
</html>
EOF

echo "Wrote gallery: ${out_dir}/index.html"

if [[ "${do_upload}" -eq 1 ]]; then
  require_cmd wtf-upload

  upload_home="${HOME}"
  if [[ -n "${host_home}" && -d "${host_home}" ]]; then
    upload_home="${host_home}"
  fi

  img_urls_output="$(HOME="${upload_home}" wtf-upload \
    "${out_dir}/01-desktop.png" \
    "${out_dir}/02-menu.png" \
    "${out_dir}/03-clients.png" \
    "${out_dir}/04-clients-menu.png" \
    "${out_dir}/05-workspaces.png" \
    "${out_dir}/06-dockapp.png")"
  mapfile -t img_urls <<<"${img_urls_output}"
  if [[ "${#img_urls[@]}" -ne 6 ]]; then
    echo "Error: wtf-upload returned ${#img_urls[@]} URLs (expected 6)" >&2
    exit 1
  fi

  upload_index="$(mktemp -t afterstep-wayland-gallery.XXXXXX.html)"

  cat >"${upload_index}" <<EOF
<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>AfterStep screenshots (${date_stamp}, Wayland/aswlcomp)</title>
    <style>
      :root { color-scheme: light dark; }
      body { font-family: system-ui, -apple-system, Segoe UI, Roboto, Ubuntu, Cantarell, Noto Sans, Helvetica, Arial, sans-serif; margin: 2rem; line-height: 1.4; }
      h1 { margin: 0 0 0.5rem; font-size: 1.5rem; }
      p { margin: 0 0 1.25rem; max-width: 75ch; }
      .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 1rem; }
      figure { margin: 0; padding: 0.75rem; border: 1px solid rgba(127,127,127,0.35); border-radius: 12px; background: rgba(127,127,127,0.06); }
      figcaption { margin-top: 0.5rem; font-size: 0.95rem; }
      img { width: 100%; height: auto; display: block; border-radius: 8px; }
      a { color: inherit; text-underline-offset: 2px; }
      code { font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, \"Liberation Mono\", \"Courier New\", monospace; font-size: 0.95em; }
    </style>
  </head>
  <body>
    <h1>AfterStep screenshots (${date_stamp}, Wayland/aswlcomp)</h1>
    <p>
      Hosted gallery uploaded via <code>wtf-upload</code>. Click any image to open the full-size PNG.
    </p>
    <div class="grid">
      <figure>
        <a href="${img_urls[0]}">
          <img src="${img_urls[0]}" alt="aswlcomp desktop with panel" />
        </a>
        <figcaption><strong>Desktop</strong> — <a href="${img_urls[0]}">01-desktop.png</a></figcaption>
      </figure>
      <figure>
        <a href="${img_urls[1]}">
          <img src="${img_urls[1]}" alt="aswlmenu launcher open" />
        </a>
        <figcaption><strong>Menu</strong> — <a href="${img_urls[1]}">02-menu.png</a></figcaption>
      </figure>
      <figure>
        <a href="${img_urls[2]}">
          <img src="${img_urls[2]}" alt="Wayland and Xwayland clients" />
        </a>
        <figcaption><strong>Clients</strong> — <a href="${img_urls[2]}">03-clients.png</a></figcaption>
      </figure>
      <figure>
        <a href="${img_urls[3]}">
          <img src="${img_urls[3]}" alt="Wayland and Xwayland clients with the menu open" />
        </a>
        <figcaption><strong>Clients + menu</strong> — <a href="${img_urls[3]}">04-clients-menu.png</a></figcaption>
      </figure>
      <figure>
        <a href="${img_urls[4]}">
          <img src="${img_urls[4]}" alt="Workspace switching" />
        </a>
        <figcaption><strong>Workspaces</strong> — <a href="${img_urls[4]}">05-workspaces.png</a></figcaption>
      </figure>
      <figure>
        <a href="${img_urls[5]}">
          <img src="${img_urls[5]}" alt="Xwayland dockapp-style window" />
        </a>
        <figcaption><strong>Dockapp</strong> — <a href="${img_urls[5]}">06-dockapp.png</a></figcaption>
      </figure>
    </div>
  </body>
</html>
EOF

  index_url="$(HOME="${upload_home}" wtf-upload "${upload_index}")"
  echo "Hosted gallery: ${index_url}"
fi
