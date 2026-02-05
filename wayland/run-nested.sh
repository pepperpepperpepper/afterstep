#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat 1>&2 <<'EOF'
Usage: wayland/run-nested.sh [--build] [--backend NAME] [--socket NAME] [-- ARGS...]

Runs the wlroots-based AfterStep Wayland compositor scaffold ("aswlcomp") nested inside
your current desktop session and autostarts the panel ("aswlpanel").

Backend selection:
  - If --backend is provided, uses it.
  - Else if $WLR_BACKENDS is set, uses that.
  - Else uses "wayland" when $WAYLAND_DISPLAY is set, or "x11" when $DISPLAY is set.

Examples:
  make -C wayland aswlcomp aswlpanel
  wayland/run-nested.sh

  wayland/run-nested.sh --build
  wayland/run-nested.sh --backend x11
  wayland/run-nested.sh --socket aswlcomp-0 -- --spawn "foot"
EOF
}

build=0
backend=""
socket=""
extra_args=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build)
      build=1
      shift
      ;;
    --backend)
      backend="${2:-}"
      shift 2
      ;;
    --socket)
      socket="${2:-}"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    --)
      shift
      extra_args=("$@")
      break
      ;;
    *)
      echo "run-nested: unknown argument: $1" 1>&2
      usage
      exit 2
      ;;
  esac
done

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd -- "${script_dir}/.." && pwd -P)"

cd -- "$repo_root"

if [[ "${build}" -eq 1 ]]; then
  make -C wayland aswlcomp aswlpanel aswlmenu aswlbg
fi

if [[ ! -x wayland/aswlcomp ]]; then
  echo "run-nested: missing wayland/aswlcomp (build with: make -C wayland aswlcomp)" 1>&2
  exit 1
fi

if [[ ! -x wayland/aswlpanel ]]; then
  echo "run-nested: missing wayland/aswlpanel (build with: make -C wayland)" 1>&2
  exit 1
fi

if [[ ! -x wayland/aswlbg ]]; then
  echo "run-nested: missing wayland/aswlbg (background will be black; build with: make -C wayland aswlbg)" 1>&2
fi

if [[ ! -x wayland/aswlmenu ]]; then
  echo "run-nested: missing wayland/aswlmenu (menu will fall back to fuzzel/bemenu/wofi)" 1>&2
fi

if [[ -z "${socket}" ]]; then
  socket="aswlcomp-nested-$$"
fi

if [[ -z "${backend}" ]]; then
  backend="${WLR_BACKENDS:-}"
fi
if [[ -z "${backend}" ]]; then
  if [[ -n "${WAYLAND_DISPLAY:-}" ]]; then
    backend="wayland"
  elif [[ -n "${DISPLAY:-}" ]]; then
    backend="x11"
  fi
fi

autostart="$(mktemp -t afterstep-aswlcomp.autostart.XXXXXX)"
top_panel_cfg="$(mktemp -t afterstep-aswlpanel-top.conf.XXXXXX)"
right_pager_cfg="$(mktemp -t afterstep-aswlpanel-right-pager.conf.XXXXXX)"
right_dock_cfg="$(mktemp -t afterstep-aswlpanel-right-dock.conf.XXXXXX)"
state_file="$(mktemp -t afterstep-aswlcomp.state.XXXXXX)"
cleanup() { rm -f -- "${autostart}" "${top_panel_cfg}" "${right_pager_cfg}" "${right_dock_cfg}" "${state_file}"; }
trap cleanup EXIT

cat >"${top_panel_cfg}" <<'EOF'
# Top dock config for nested development runs.
# Lines are: LABEL[|/path/to/icon.png]=COMMAND
# Special directives:
#   @edge top|bottom|left|right  (layer-shell placement; defaults to top)
#   @dock              (icon-only "dock" layout)
#   @workspaces        (auto-generate workspace buttons)
#   @workspaces N      (generate 1..N)
@edge top
@margin left 1
@margin top 1
@dock
Info|normal/Info=:
Terminal|logos/Eterm=${TERMINAL:-foot}
WWW|normal/WWW=:
Mail|normal/Mail=:
Games|normal/Cardgames=:
Menu|dots/menu_medium=./wayland/aswlmenu || fuzzel || bemenu-run || wofi --show drun
Close|dots/abi-close=@close
Full|dots/window_thick=@fullscreen
Max|dots/abi-icon-max=@maximize
EOF

cat >"${right_pager_cfg}" <<'EOF'
# Right-side pager-ish config for nested development runs.
# Sits left of the dock strip (via the layer-shell margin).
@edge right
@margin right 48
@nodock
Work|normal/Desktop=@workspace 1
WWW|normal/WWW=@workspace 2
Mail|normal/Mail=@workspace 3
Games|normal/Cardgames=@workspace 4
Prev|dots/arrow_small=@workspace_prev
Next|dots/lined_arrow=@workspace_next
EOF

cat >"${right_dock_cfg}" <<'EOF'
# Right-side dock/wharf-ish strip.
@edge right
@dock
Info|normal/Info=:
WWW|normal/WWW=:
Mail|normal/Mail=:
Games|normal/Cardgames=:
Tools|normal/Wrench=:
XEyes|logos/Xmms=xeyes
EOF

{
  echo "# Autostart file for nested development runs."
  if [[ -x ./wayland/aswlbg ]]; then
    echo "exec ./wayland/aswlbg"
  fi
  echo "exec ASWLPANEL_CONFIG=\"${top_panel_cfg}\" ./wayland/aswlpanel"
  echo "exec ASWLPANEL_CONFIG=\"${right_dock_cfg}\" ./wayland/aswlpanel"
  echo "exec ASWLPANEL_CONFIG=\"${right_pager_cfg}\" ASWLPANEL_HEIGHT=48 ./wayland/aswlpanel"
  cat <<'EOF'

# Common bindings (optional):
bind Alt+Return exec "${TERMINAL:-foot}"
bind Alt+d exec "fuzzel || bemenu-run || wofi --show drun"
bind Alt+space exec "./wayland/aswlmenu"
bind Alt+Tab focus_next
bind Alt+Shift+Tab focus_prev
bind Alt+f fullscreen
bind Alt+m maximize
bind Alt+Left workspace_prev
bind Alt+Right workspace_next
bind Alt+1 workspace 1
bind Alt+2 workspace 2
bind Alt+3 workspace 3
bind Alt+4 workspace 4
bind Alt+q close_focused
EOF
} >"${autostart}"

echo "run-nested: socket=${socket} backend=${backend:-auto}" 1>&2
echo "run-nested: autostart=${autostart}" 1>&2

if [[ -n "${backend}" ]]; then
  XDG_CURRENT_DESKTOP=AfterStep:wlroots \
  XDG_SESSION_DESKTOP=AfterStep \
  XDG_SESSION_TYPE=wayland \
  WLR_BACKENDS="${backend}" \
    ./wayland/aswlcomp --socket "${socket}" --autostart "${autostart}" --state "${state_file}" "${extra_args[@]}"
else
  XDG_CURRENT_DESKTOP=AfterStep:wlroots \
  XDG_SESSION_DESKTOP=AfterStep \
  XDG_SESSION_TYPE=wayland \
    ./wayland/aswlcomp --socket "${socket}" --autostart "${autostart}" --state "${state_file}" "${extra_args[@]}"
fi
