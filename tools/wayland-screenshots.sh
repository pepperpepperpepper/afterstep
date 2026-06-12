#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat 1>&2 <<'EOF'
Usage: tools/wayland-screenshots.sh [--out DIR] [--no-upload] [--upload]

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
  --upload    Upload a hosted gallery via `wtf-upload` (prints the index.html URL) (default)
  --no-upload Skip upload (local files only)
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
do_upload=1
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
require_cmd xprop
require_cmd xdotool

cd -- "${repo_root}"

make -C wayland aswlcomp aswlpanel aswlmenu aswlctl aswlbg aswlbanner aswlwait
make -C src/WinTabs WinTabs

# TermTabs/WinTabs relies on the X root pixmap for its transparent empty-state
# background (ParentRelative). Under Xwayland the root pixmap is often unset,
# yielding a black rectangle instead of the AfterStep background. Pre-populate
# the Xwayland root pixmap using libAfterImage's `ascompose -r`.
make -C libAfterImage/apps ascompose

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

  if [[ -n "${tmp_home:-}" && -d "${tmp_home}" ]]; then
    rm -r -- "${tmp_home}" 2>/dev/null || true
  fi
  if [[ -n "${runtime_dir:-}" && -d "${runtime_dir}" ]]; then
    rm -r -- "${runtime_dir}" 2>/dev/null || true
  fi
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

# Xwayland demo terminal used for the "clients" screenshot. We do not start it
# until after 02-menu.png so TermTabs remains empty for the empty-state capture.
terminal_app_id="XTerm"
# Match the X11 baseline window placement: with AfterStep's decoration metrics,
# the xterm client content begins at y=161 (not 164).
# Xterm's Xft face metrics differ by 1px between Xvfb (baseline) and Xwayland on
# some setups (e.g. 20px vs 19px row height for the same `-fs 12` request).
# Use `-fs 13` under Xwayland so the resulting pixel size matches the X11/Xvfb
# baseline captures.
default_terminal_cmd="xterm -geometry 63x24+52+161 -fa Monospace -fs 13 -T 'arch@sandbox-server:/home/arch/afterstep'"
terminal_cmd="${TERMINAL:-${default_terminal_cmd}}"
wayland_demo_cmd="weston-simple-shm"

# Xwayland client geometry is specified in terms of the client surface (content)
# while aswlcomp reports and positions "outer" (decorated) geometry. Keep the
# screenshot layout stable by deriving client geometry from the decoration
# metrics. These defaults must match aswlcomp's compiled-in defaults.
deco_border="${ASWLCOMP_DECOR_BORDER:-2}"
deco_title="${ASWLCOMP_DECOR_TITLE:-28}"

# Expected outer geometry for the TermTabs/WinTabs window in the reference shots.
termtabs_outer_w=640
termtabs_outer_h=510
termtabs_outer_x=48
termtabs_outer_y=100
termtabs_client_w=$((termtabs_outer_w - 2 * deco_border))
termtabs_client_h=$((termtabs_outer_h - deco_title - deco_border))
termtabs_client_x=$((termtabs_outer_x + deco_border))
termtabs_client_y=$((termtabs_outer_y + deco_title))

# Expected outer geometry for xeyes in the reference shots.
xeyes_outer_w=152
xeyes_outer_h=138
xeyes_outer_x=200
xeyes_outer_y=200
xeyes_client_w=$((xeyes_outer_w - 2 * deco_border))
xeyes_client_h=$((xeyes_outer_h - deco_title - deco_border))
xeyes_client_x=$((xeyes_outer_x + deco_border))
xeyes_client_y=$((xeyes_outer_y + deco_title))

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

mkdir -p -- "${HOME}/.afterstep/non-configurable"

# Many legacy AfterStep modules (e.g. WinTabs) expect a populated AfterStep config
# tree even when running "standalone" under the Wayland harness. Populate the
# temporary HOME with the in-repo defaults so style/fonts/pixmaps resolve and the
# module can render (and therefore map) consistently.
cp -a -- "${repo_root}/afterstep/." "${HOME}/.afterstep/"

# Prefer the installed "active" theme selectors so X11 modules (TermTabs/WinTabs)
# pick the same look/colorscheme/background as the X11 screenshot baseline.
if [[ -d "${repo_root}/_install/share/afterstep/non-configurable" ]]; then
  cp -a -- "${repo_root}/_install/share/afterstep/non-configurable/0_background" "${HOME}/.afterstep/non-configurable/" 2>/dev/null || true
  cp -a -- "${repo_root}/_install/share/afterstep/non-configurable/0_colorscheme" "${HOME}/.afterstep/non-configurable/" 2>/dev/null || true
  cp -a -- "${repo_root}/_install/share/afterstep/non-configurable/0_look" "${HOME}/.afterstep/non-configurable/" 2>/dev/null || true
  cp -a -- "${repo_root}/_install/share/afterstep/non-configurable/0_feel" "${HOME}/.afterstep/non-configurable/" 2>/dev/null || true
elif [[ -d "/usr/share/afterstep/non-configurable" ]]; then
  cp -a -- "/usr/share/afterstep/non-configurable/0_background" "${HOME}/.afterstep/non-configurable/" 2>/dev/null || true
  cp -a -- "/usr/share/afterstep/non-configurable/0_colorscheme" "${HOME}/.afterstep/non-configurable/" 2>/dev/null || true
  cp -a -- "/usr/share/afterstep/non-configurable/0_look" "${HOME}/.afterstep/non-configurable/" 2>/dev/null || true
  cp -a -- "/usr/share/afterstep/non-configurable/0_feel" "${HOME}/.afterstep/non-configurable/" 2>/dev/null || true
fi

export WLR_BACKENDS=x11
export WLR_X11_FULLSCREEN=1
export WLR_RENDERER=pixman
export ASWLCOMP_OUTPUT_WIDTH=1600
export ASWLCOMP_OUTPUT_HEIGHT=900

export ASWLCOMP_WORKSPACES=4
# The X11 reference screenshots are click-to-focus and (critically) appear to
# have client windows unfocused for the "clients" shots. Avoid focus-stealing
# on map so titlebars render in the unfocused style for parity.
#
# The WinList strip stays populated by listing visible windows (not
# focused-only) rather than by forcing focus.
export ASWLCOMP_FOCUS_ON_MAP="${ASWLCOMP_FOCUS_ON_MAP:-0}"
# The menu/launcher requests a Wayland keyboard. If a headless wlroots/x11 + Xvfb
# environment misbehaves with a keyboard present, rerun with:
#   ASWLCOMP_DISABLE_KEYBOARD=1 tools/wayland-screenshots.sh ...
export ASWLMENU_CONFIG="${menu_cfg}"

socket="aswlcomp-shot-$$"

demo_bin="${tmp_home}/aswlx11dockapp-demo"
cc -O2 -g -std=c11 -Wall -Wextra -Wformat=2 -Wshadow -Wpointer-arith \
  -o "${demo_bin}" wayland/aswlx11dockapp-demo.c $(pkg-config --cflags --libs x11)

clock_bin="${tmp_home}/aswlx11clock-overlay"
cc -O2 -g -std=c11 -Wall -Wextra -Wformat=2 -Wshadow -Wpointer-arith \
  -o "${clock_bin}" wayland/aswlx11clock-overlay.c $(pkg-config --cflags --libs x11 xft)

fill_bin="${tmp_home}/aswlx11fill-window"
cc -O2 -g -std=c11 -Wall -Wextra -Wformat=2 -Wshadow -Wpointer-arith \
  -o "${fill_bin}" wayland/aswlx11fill-window.c $(pkg-config --cflags --libs x11)

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

run_in_comp_sync() {
  local desc="$1"
  local cmd="$2"
  local max_tries="${3:-200}"

  local marker
  marker="${XDG_RUNTIME_DIR}/aswl-sync-$$-${RANDOM}.rc"
  rm -f -- "${marker}" 2>/dev/null || true

  require_comp_alive
  # Run the command in the compositor (inherits WAYLAND_DISPLAY + DISPLAY for Xwayland),
  # then write its exit code to a marker file so the harness can wait deterministically.
  WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "${cmd}; rc=\$?; printf '%s\n' \"\${rc}\" > '${marker}'" || true

  for _ in $(seq 1 "${max_tries}"); do
    require_comp_alive
    if [[ -f "${marker}" ]]; then
      local rc
      rc="$(cat -- "${marker}" 2>/dev/null || true)"
      rm -f -- "${marker}" 2>/dev/null || true
      if [[ "${rc}" == "0" ]]; then
        return 0
      fi
      echo "Error: ${desc} failed (exit ${rc})." >&2
      return 1
    fi
    sleep 0.05
  done

  echo "Error: timed out waiting for ${desc} to finish." >&2
  return 1
}

aswl_default_file() {
  local primary="$1"
  local fallback="$2"

  if [[ -r "${primary}" ]]; then
    printf '%s' "${primary}"
    return 0
  fi
  if [[ -r "${fallback}" ]]; then
    printf '%s' "${fallback}"
    return 0
  fi
  return 1
}

aswl_colorscheme_value() {
  local file="$1"
  local key="$2"

  awk -v want="${key}" '
    BEGIN { disabled = "#~~DISABLED~~#" }
    {
      line = $0
      sub(/\r$/, "", line)
      sub(/^[[:space:]]+/, "", line)
      if (line == "") next
      if (index(line, disabled) == 1) {
        line = substr(line, length(disabled) + 1)
        sub(/^[[:space:]]+/, "", line)
      } else if (substr(line, 1, 1) == "#") {
        next
      }

      if (line ~ ("^" want "[[:space:]]+#[0-9A-Fa-f]{6,8}([[:space:]]|$)")) {
        # Split: KEY VALUE ...
        n = split(line, parts, /[[:space:]]+/)
        if (n >= 2) {
          print parts[2]
          exit 0
        }
      }
    }
  ' "${file}" 2>/dev/null || true
}

resolve_stormy_skies_path() {
  local share_root="$1"

  local candidates=(
    "${share_root}/backgrounds/.StormySkies"
    "${share_root}/backgrounds/jpg/.StormySkies"
    "${repo_root}/afterstep/backgrounds/jpg/.StormySkies"
    "${repo_root}/_install/share/afterstep/backgrounds/.StormySkies"
    "/usr/share/afterstep/backgrounds/.StormySkies"
  )
  for p in "${candidates[@]}"; do
    if [[ -r "${p}" ]]; then
      printf '%s' "${p}"
      return 0
    fi
  done
  return 1
}

resolve_simple_texture_path() {
  local share_root="$1"

  local candidates=(
    "${share_root}/desktop/tiles/SimpleTexture"
    "${share_root}/desktop/tiles/png/SimpleTexture"
    "${share_root}/desktop/tiles/jpg/SimpleTexture"
    "${repo_root}/afterstep/desktop/tiles/png/SimpleTexture"
    "${repo_root}/_install/share/afterstep/desktop/tiles/SimpleTexture"
    "/usr/share/afterstep/desktop/tiles/SimpleTexture"
  )
  for p in "${candidates[@]}"; do
    if [[ -r "${p}" ]]; then
      printf '%s' "${p}"
      return 0
    fi
  done
  return 1
}

ensure_xwayland_root_pixmap() {
  local share_root=""
  if [[ -d "${repo_root}/_install/share/afterstep" ]]; then
    share_root="${repo_root}/_install/share/afterstep"
  elif [[ -d "/usr/share/afterstep" ]]; then
    share_root="/usr/share/afterstep"
  else
    share_root="${repo_root}/afterstep"
  fi

  local bg_path
  bg_path="$(aswl_default_file "${share_root}/non-configurable/0_background" "${repo_root}/afterstep/backgrounds/xml/Default" || true)"
  if [[ -z "${bg_path}" ]]; then
    echo "Warning: could not locate background XML for Xwayland root pixmap" >&2
    return 0
  fi

  local cs_path
  cs_path="$(aswl_default_file "${share_root}/non-configurable/0_colorscheme" "${repo_root}/afterstep/colorschemes/colorscheme.Stormy_Skies" || true)"

  local base_light="#FF7B97B3"
  local base_dark="#FF000000"
  local inactive1="#FF5C5B66"
  if [[ -n "${cs_path}" && -r "${cs_path}" ]]; then
    base_light="$(aswl_colorscheme_value "${cs_path}" BaseLight || true)"
    base_dark="$(aswl_colorscheme_value "${cs_path}" BaseDark || true)"
    inactive1="$(aswl_colorscheme_value "${cs_path}" Inactive1 || true)"

    [[ -n "${base_light}" ]] || base_light="#FF7B97B3"
    [[ -n "${base_dark}" ]] || base_dark="#FF000000"
    [[ -n "${inactive1}" ]] || inactive1="#FF5C5B66"
  fi

  local stormy_path tile_path
  stormy_path="$(resolve_stormy_skies_path "${share_root}" || true)"
  tile_path="$(resolve_simple_texture_path "${share_root}" || true)"

  # Build a small, self-contained XML string so `ascompose` does not depend on
  # repo-vs-installed asset layout, nor on AfterStep color names.
  local xml
  xml="$(cat -- "${bg_path}" 2>/dev/null || true)"
  if [[ -z "${xml}" ]]; then
    echo "Warning: failed to read ${bg_path}; skipping Xwayland root pixmap" >&2
    return 0
  fi

  # Replace known colorscheme tokens with hex so libAfterImage can parse them.
  xml="$(printf '%s' "${xml}" | sed \
    -e "s/\\<BaseLight\\>/${base_light}/g" \
    -e "s/\\<BaseDark\\>/${base_dark}/g" \
    -e "s/\\<Inactive1\\>/${inactive1}/g")"

  # Fix up asset locations for repo vs installed layouts.
  if [[ -n "${stormy_path}" ]]; then
    xml="$(printf '%s' "${xml}" | sed -e "s|src=\"\\.StormySkies\"|src=\"${stormy_path}\"|g")"
  fi
  if [[ -n "${tile_path}" ]]; then
    xml="$(printf '%s' "${xml}" | sed -e "s|src=\"tiles/SimpleTexture\"|src=\"${tile_path}\"|g")"
  fi

  # Ensure ascompose can locate fonts via FONT_PATH; IMAGE_PATH isn't required
  # after we rewrite `src=` to absolute paths.
  local fonts_dir="${share_root}/desktop/fonts"
  if [[ ! -d "${fonts_dir}" ]]; then
    fonts_dir="${repo_root}/afterstep/desktop/fonts"
  fi

  # Escape single quotes for /bin/sh -c '...'
  local xml_escaped
  xml_escaped="${xml//\'/\'\\\'\'}"

  # Try a few times: Xwayland may not be ready immediately at compositor start.
  for _ in $(seq 1 20); do
    if run_in_comp_sync "ascompose -r (set Xwayland root pixmap)" \
      "FONT_PATH='${fonts_dir}' libAfterImage/apps/ascompose -q -r -s '${xml_escaped}' >/dev/null 2>&1 && xprop -root _XROOTPMAP_ID >/dev/null 2>&1" \
      200; then
      if [[ "${ASWL_DEBUG_XWAYLAND_ROOT:-}" == "1" ]]; then
        run_in_comp_sync "debug: capture Xwayland root" \
          "xprop -root _XROOTPMAP_ID > '${out_dir}/xwayland-rootpixmap.txt' 2>&1 || true; xwininfo -root -all > '${out_dir}/xwayland-rootwininfo.txt' 2>&1 || true; import -window root '${out_dir}/xwayland-root.png' > '${out_dir}/xwayland-root-import.txt' 2>&1 || true; true" \
          200 || true
      fi
      return 0
    fi
    sleep 0.1
  done

  echo "Warning: failed to set Xwayland root pixmap (continuing)" >&2
  return 0
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

# Seed the Xwayland root pixmap so X11 modules that use ParentRelative
# transparency (WinTabs/TermTabs) match the X11 baseline instead of rendering
# against a black root.
ensure_xwayland_root_pixmap

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

wait_for_rendered_crop() {
  local geom="${1:?crop geometry required (WxH+X+Y)}"
  local min_std="${2:-0.12}"
  local max_tries="${3:-100}"

  local probe="${runtime_dir}/aswl-crop-probe-$$.png"
  rm -f -- "${probe}" 2>/dev/null || true

  for _ in $(seq 1 "${max_tries}"); do
    require_comp_alive
    import -window "${win_id}" -crop "${geom}" +repage "${probe}" >/dev/null 2>&1 || true
    if [[ -r "${probe}" ]]; then
      local std
      std="$(identify -format '%[fx:standard_deviation]' "${probe}" 2>/dev/null || printf '0')"
      if awk -v s="${std}" -v m="${min_std}" 'BEGIN { exit !(s > m) }'; then
        rm -f -- "${probe}" 2>/dev/null || true
        return 0
      fi
    fi
    sleep 0.05
  done

  rm -f -- "${probe}" 2>/dev/null || true
  echo "Warning: timed out waiting for rendered crop ${geom} (continuing)" >&2
  return 0
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
      if ($4 ~ /mapped/ && (match_app || match_title) && id == "") id = $1
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

wait_for_window_geometry() {
  local title_substr="${1:?title substring required}"
  local want_w="${2:?width required}"
  local want_h="${3:?height required}"
  local want_x="${4:-}"
  local want_y="${5:-}"
  local max_tries="${6:-200}"

  for _ in $(seq 1 "${max_tries}"); do
    require_comp_alive
	    if WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl list_windows 2>/dev/null | awk -F'\t' \
      -v want_title="${title_substr}" \
      -v want_w="${want_w}" \
      -v want_h="${want_h}" \
      -v want_x="${want_x}" \
      -v want_y="${want_y}" '
      function lcase(s) { return tolower(s) }
	      BEGIN { id = ""; found = 0 }
	      {
	        if ($2 ~ /^ws=/) {
	          got_title = $6
	          sub(/^title=/, "", got_title)
	          if (index(lcase(got_title), lcase(want_title)) > 0) {
	            id = $1
	          } else {
	            id = ""
	          }
	        } else if (id != "" && $1 == id && $2 == "geom") {
	          gx = $3; gy = $4; gw = $5; gh = $6
	          sub(/^x=/, "", gx); sub(/^y=/, "", gy); sub(/^w=/, "", gw); sub(/^h=/, "", gh)
	          ok = (gw == want_w && gh == want_h)
	          if (want_x != "") ok = ok && (gx == want_x)
	          if (want_y != "") ok = ok && (gy == want_y)
	          if (ok) {
	            found = 1
	            exit
	          }
	          # Title matched but geometry did not; keep scanning (handles retries
	          # that accidentally spawn duplicate windows).
	          id = ""
	        }
	      }
      END { exit found ? 0 : 1 }
    ' >/dev/null; then
      return 0
    fi
    sleep 0.05
  done
  return 1
}

ensure_mapped_window() {
  local desc="$1"
  local cmd="$2"
  local want_app_id="${3:-}"
  local want_title_substr="${4:-}"
  local attempts="${5:-3}"
  local max_tries="${6:-200}"

  for attempt in $(seq 1 "${attempts}"); do
    require_comp_alive
    WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "${cmd}" || true
    if wait_for_mapped_app_id "${want_app_id}" "${want_title_substr}" "${max_tries}"; then
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
require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "ASWLWAIT_X=810 ASWLWAIT_Y=460 ASWLWAIT_W=672 ASWLWAIT_H=30 ASWLWAIT_TEXT='Waiting for window matching \"WinList\" ... Press button to cancel.' ./wayland/aswlwait" || true
sleep 2
snap "${out_dir}/01-desktop.png"

# The X11 baseline's Banner is only visible during early init. Stop it before we
# bring up the panels and menus so subsequent shots match the baseline.
kill_children_matching "aswlwait" TERM
sleep 0.1
kill_children_matching "aswlbanner" TERM
sleep 0.1

require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "ASWLPANEL_HEIGHT=${top_dock_h} ASWLPANEL_CONFIG='${top_panel_cfg}' ./wayland/aswlpanel" || true
require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "ASWLPANEL_EXCLUSIVE_ZONE=0 ASWLPANEL_WINDOW_LIST=topmost ASWLPANEL_CONFIG='${top_winlist_cfg}' ./wayland/aswlpanel" || true
require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "ASWLPANEL_EXCLUSIVE_ZONE=0 ASWLPANEL_CLOCK_OVERRIDE=05:45 ASWLTHEME_CONFIG='${right_dock_theme_cfg}' ASWLPANEL_CONFIG='${right_dock_cfg}' ./wayland/aswlpanel" || true
require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "ASWLPANEL_EXCLUSIVE_ZONE=0 ASWLPANEL_MODE=pager ASWLPANEL_CONFIG='${right_pager_cfg}' ./wayland/aswlpanel" || true

ensure_mapped_window "TermTabs (WinTabs)" \
  "./src/WinTabs/WinTabs --myname TermTabs --pattern '*term*' --exclude-pattern 'mc*' --geometry ${termtabs_client_w}x${termtabs_client_h}+${termtabs_client_x}+${termtabs_client_y} --title 'term tabs' --standalone-scan" \
  "" "term tabs" 1 400

# Capture a window list snapshot for parity debugging (includes frame geometry).
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl list_windows >"${out_dir}/windows-02-termtabs.txt" 2>&1 || true

# Match the (mislabeled) X11 baseline `02-root-menu.png`: it shows TermTabs'
# empty state, not the menu.
	# WinTabs sleeps for 1s before entering its event loop; wait until its pixels
	# look "rendered" (stddev rises from ~0.07 for an uninitialized surface).
	wait_for_rendered_crop "240x90+48+100" 0.10 400
	# Give WinTabs a moment to finish painting the hint/banner area; otherwise we
	# can capture partially-rendered pixels and regress parity vs the X11 baseline.
	sleep 0.2
	snap "${out_dir}/02-menu.png"

require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl workspace 1 || true
require_comp_alive
close_windows_by_app_id "${terminal_app_id}"
require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "${terminal_cmd}" || true
# Wait for TermTabs to expand to its configured client size after swallowing
# the terminal. This is the key visual parity requirement for 03/04.
if ! wait_for_window_geometry "term tabs" 640 510 48 100 400; then
  echo "Error: TermTabs did not expand after starting xterm." >&2
  WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl list_windows 2>/dev/null || true
  tail -n 200 "${log_file}" >&2 || true
  exit 1
fi

if [[ "${ASWL_DEBUG_XTERM_INFO:-}" == "1" ]]; then
  run_in_comp_sync "debug: xterm xwininfo" \
    "xwininfo -name 'arch@sandbox-server:/home/arch/afterstep' -all > '${out_dir}/xterm-xwininfo.txt' 2>&1 || true; true" \
    200 || true
  run_in_comp_sync "debug: xterm xprop" \
    "xprop -name 'arch@sandbox-server:/home/arch/afterstep' > '${out_dir}/xterm-xprop.txt' 2>&1 || true; true" \
    200 || true
  run_in_comp_sync "debug: xterm WM_NORMAL_HINTS" \
    "xprop -name 'arch@sandbox-server:/home/arch/afterstep' WM_NORMAL_HINTS > '${out_dir}/xterm-wm-normal-hints.txt' 2>&1 || true; true" \
    200 || true
fi

require_comp_alive
close_windows_by_app_id "XEyes"
# Match the X11 baseline's xeyes appearance: use an unshaped (rectangular)
# window and explicitly paint its background white.
#
# xeyes intentionally sets its core background pixmap to None, so the window
# background isn't cleared automatically (even if you pass -bg). Without this,
# Xwayland often shows uninitialized pixels (typically black) behind the eyes.
# We fill the window (and its child widget) with #FFFFFF and then trigger an
# Expose so xeyes repaints on top of the new background.
#
# Xwayland may not have a named-color database (rgb.txt), so use hex colors to
# keep the demo deterministic across environments.
ensure_mapped_window "xeyes" "xeyes +shape -render +present -bg '#FFFFFF' -fg '#000000' -outline '#000000' -center '#FFFFFF' -geometry ${xeyes_client_w}x${xeyes_client_h}+${xeyes_client_x}+${xeyes_client_y}" "XEyes" "xeyes"

# Force a stable white background for the xeyes window.
xeyes_xwininfo_tmp="${XDG_RUNTIME_DIR}/xeyes-xwininfo-$$.txt"
rm -f -- "${xeyes_xwininfo_tmp}" 2>/dev/null || true
run_in_comp_sync "xeyes xwininfo" \
  "xwininfo -name xeyes -all > '${xeyes_xwininfo_tmp}' 2>&1 || true; true" \
  200 || true
wid="$(awk '/Window id:/{print $4; exit}' "${xeyes_xwininfo_tmp}" 2>/dev/null || true)"
if [[ -z "${wid}" ]]; then
  echo "Error: could not determine xeyes window id for background fill." >&2
  sed -n '1,120p' "${xeyes_xwininfo_tmp}" >&2 || true
  exit 1
fi
run_in_comp_sync "xeyes background fill" \
  "${fill_bin} --window '${wid}' --color '#FFFFFF' --subtree --expose" \
  200

if [[ "${ASWL_DEBUG_XEYES_INFO:-}" == "1" ]]; then
  cp -f -- "${xeyes_xwininfo_tmp}" "${out_dir}/xeyes-xwininfo.txt" 2>/dev/null || true
  run_in_comp_sync "debug: xeyes xwininfo/xprop" \
    "xprop -name xeyes > '${out_dir}/xeyes-xprop.txt' 2>&1 || true; true" \
    200 || true

  child="$(awk '/child:/{getline; print $1; exit}' "${xeyes_xwininfo_tmp}" 2>/dev/null || true)"

  if [[ -n "${wid}" ]]; then
    run_in_comp_sync "debug: xeyes import" \
      "import -window '${wid}' '${out_dir}/xeyes-import.png' 2>/dev/null || true; true" \
      200 || true
  fi

  if [[ -n "${child}" ]]; then
    run_in_comp_sync "debug: xeyes import child" \
      "import -window '${child}' '${out_dir}/xeyes-child-import.png' 2>/dev/null || true; true" \
      200 || true
  fi
fi

# Overlay a small "clock" window over xeyes (matches X11 baseline and should
# not appear in the window list menu).
require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "${clock_bin} --text '05:45' --geometry '68x37+261+290' --font 'Monospace-13' --seconds 120 --no-title --no-wm-class" || true
wait_for_rendered_crop "90x60+250+275" 0.10 400

# X11 baseline captures are click-to-focus. For parity, do not focus any client
# window here: we keep the pointer inside xeyes to make its pupils deterministic
# while leaving decorations in the unfocused style.

# Capture a window list snapshot for parity debugging (includes frame geometry).
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl list_windows >"${out_dir}/windows-03-clients.txt" 2>&1 || true

# Make `xeyes` deterministic: it tracks the pointer position, so place the
# cursor inside the window before capturing. This reduces visual diffs vs the
# X11 baseline.
move_pointer "${ASWL_SHOT_XEYES_POINTER_X:-315}" "${ASWL_SHOT_XEYES_POINTER_Y:-263}"
sleep 0.1
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
          <img src="02-menu.png" alt="AfterStep with TermTabs (WinTabs) empty state" />
        </a>
        <figcaption><strong>TermTabs</strong> — <a href="02-menu.png">02-menu.png</a></figcaption>
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
          <img src="${img_urls[1]}" alt="AfterStep with TermTabs (WinTabs) empty state" />
        </a>
        <figcaption><strong>TermTabs</strong> — <a href="${img_urls[1]}">02-menu.png</a></figcaption>
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
