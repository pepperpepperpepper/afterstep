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

cd -- "${repo_root}"

make -C wayland aswlcomp aswlpanel aswlmenu aswlctl aswlbg aswlbanner

tmp_home="$(mktemp -d)"
runtime_dir="$(mktemp -d)"
chmod 700 "${runtime_dir}"

top_panel_cfg="${out_dir}/aswlpanel-top.conf"
top_winlist_cfg="${out_dir}/aswlpanel-top-winlist.conf"
right_pager_cfg="${out_dir}/aswlpanel-right-pager.conf"
right_dock_cfg="${out_dir}/aswlpanel-right-dock.conf"
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
right_pager_w=192
right_dock_w=64
right_dock_margin=5
right_reserved=$((right_dock_w + right_dock_margin))

default_terminal_cmd="xterm -fa Monospace -fs 12"
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
@margin left $((top_dock_w + 4))
@margin right ${right_reserved}
EOF

cat >"${right_pager_cfg}" <<EOF
# Screenshot run right-side pager-ish config (sits left of the dock strip).
@edge right
@anchor right top
@width ${right_pager_w}
@height 240
@pager_columns 2
@pager_rows 1
@margin top 27
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
@anchor right bottom
@margin right ${right_dock_margin}
@margin bottom ${right_dock_margin}
@dock
afterstepdoc|large/AfterStep3=:
WharfExtras|normal/Info=:
Tools|normal/Desktop=:
XEyes|normal/EyeInMonitorShadow=xeyes
QuitFolder|normal/RedLight=:
asfsm|normal/Harddrive=:
loadmonitor|normal/Monitor1=:
netmonitor|normal/Ethernet=:
asmail|normal/MailBox2=:
Clock=@clock
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
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "./wayland/aswlbanner" || true
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

focus_window_by_app_id() {
  local app_id="$1"
  local wid=""
  wid="$(WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl list_windows 2>/dev/null | awk -F'\t' -v app="app_id=${app_id}" '$5 == app {print $1; exit}')"
  if [[ -n "${wid}" ]]; then
    WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl focus_window "${wid}" >/dev/null 2>&1 || true
  fi
}

open_menu_and_wait() {
  require_comp_alive
  WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "./wayland/aswlmenu" || true
  for _ in $(seq 1 120); do
    require_comp_alive
    if WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl list_windows 2>/dev/null | grep -q "app_id=afterstep\\.aswlmenu"; then
      break
    fi
    sleep 0.05
  done
  sleep 0.2
}

open_window_list_menu_and_wait() {
  require_comp_alive
  WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "./wayland/aswlmenu --windows" || true
  for _ in $(seq 1 120); do
    require_comp_alive
    if WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl list_windows 2>/dev/null | grep -q "app_id=afterstep\\.aswlmenu"; then
      break
    fi
    sleep 0.05
  done
  sleep 0.2
}

close_menu_if_open() {
  focus_window_by_app_id "afterstep.aswlmenu"
  require_comp_alive
  WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl close_focused || true
  sleep 0.2
}

# Give aswlbg (background XML compositor via libAfterImage) time to paint before
# the first capture. This can take a bit longer on some systems.
sleep 2
snap "${out_dir}/01-desktop.png"

require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "ASWLPANEL_HEIGHT=${top_dock_h} ASWLPANEL_CONFIG='${top_panel_cfg}' ./wayland/aswlpanel" || true
require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "ASWLPANEL_HEIGHT=24 ASWLPANEL_WINDOW_LIST=focused ASWLPANEL_EXCLUSIVE_ZONE=0 ASWLPANEL_CONFIG='${top_winlist_cfg}' ./wayland/aswlpanel" || true
	require_comp_alive
	WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "ASWLPANEL_CLOCK_OVERRIDE=05:45 ASWLPANEL_CONFIG='${right_dock_cfg}' ./wayland/aswlpanel" || true
	require_comp_alive
	WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "ASWLPANEL_MODE=pager ASWLPANEL_CONFIG='${right_pager_cfg}' ./wayland/aswlpanel" || true

	sleep 0.3
open_menu_and_wait
snap "${out_dir}/02-menu.png"
close_menu_if_open

require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl workspace 1 || true
require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "${wayland_demo_cmd}" || true
require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "${terminal_cmd}" || true
require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "xeyes" || true
for _ in $(seq 1 200); do
  require_comp_alive
  if WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl list_windows 2>/dev/null | grep -q "mapped.*app_id=XEyes"; then
    break
  fi
  sleep 0.05
done
sleep 0.3
snap "${out_dir}/03-clients.png"

open_window_list_menu_and_wait
snap "${out_dir}/04-clients-menu.png"
close_menu_if_open

require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl workspace 2 || true
require_comp_alive
WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl exec "${wayland_demo_cmd}" || true
for _ in $(seq 1 200); do
  require_comp_alive
  if WAYLAND_DISPLAY="${socket}" ./wayland/aswlctl list_windows 2>/dev/null | grep -q "ws=2.*mapped"; then
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
