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
  make -C wayland aswlcomp aswlpanel
fi

if [[ ! -x wayland/aswlcomp ]]; then
  echo "run-nested: missing wayland/aswlcomp (build with: make -C wayland aswlcomp)" 1>&2
  exit 1
fi

if [[ ! -x wayland/aswlpanel ]]; then
  echo "run-nested: missing wayland/aswlpanel (build with: make -C wayland)" 1>&2
  exit 1
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
panel_cfg="$(mktemp -t afterstep-aswlpanel.conf.XXXXXX)"
cleanup() { rm -f -- "${autostart}" "${panel_cfg}"; }
trap cleanup EXIT

cat >"${panel_cfg}" <<'EOF'
# Panel config for nested development runs.
# Lines are: LABEL[|/path/to/icon.png]=COMMAND
# Special directives:
#   @workspaces        (auto-generate workspace buttons)
#   @workspaces N      (generate 1..N)
@workspaces
Prev=@workspace_prev
Next=@workspace_next
Terminal=${TERMINAL:-foot}
Menu=fuzzel || bemenu-run || wofi --show drun
Close=@close
EOF

{
  echo "# Autostart file for nested development runs."
  echo "exec ASWLPANEL_CONFIG=\"${panel_cfg}\" ./wayland/aswlpanel"
  cat <<'EOF'

# Common bindings (optional):
bind Alt+Return exec "${TERMINAL:-foot}"
bind Alt+d exec "fuzzel || bemenu-run || wofi --show drun"
bind Alt+Tab focus_next
bind Alt+Shift+Tab focus_prev
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
  WLR_BACKENDS="${backend}" ./wayland/aswlcomp --socket "${socket}" --autostart "${autostart}" "${extra_args[@]}"
else
  ./wayland/aswlcomp --socket "${socket}" --autostart "${autostart}" "${extra_args[@]}"
fi
