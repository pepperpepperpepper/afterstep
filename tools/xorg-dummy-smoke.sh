#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
prefix="${1:-"$root_dir/_install"}"

afterstep_bin="$prefix/bin/afterstep"
config_dir="$prefix/share/afterstep"
log_file="$root_dir/afterstep-xorg-dummy-smoke.log"

if [[ ! -x "$afterstep_bin" ]]; then
  echo "afterstep binary not found: $afterstep_bin" >&2
  echo "Build + install first:" >&2
  echo "  ./configure --prefix=\"$prefix\" && make -j\"\\$(nproc)\" && make install install.data" >&2
  exit 2
fi

if [[ ! -d "$config_dir" ]]; then
  echo "afterstep config dir not found: $config_dir" >&2
  echo "Install data first:" >&2
  echo "  make install install.data" >&2
  exit 2
fi

if ! command -v sudo >/dev/null 2>&1; then
  echo "sudo not found in PATH (needed to start Xorg headlessly)" >&2
  exit 2
fi

if ! sudo -n true >/dev/null 2>&1; then
  echo "sudo needs a password (this script needs non-interactive sudo)." >&2
  echo "Try: sudo -v" >&2
  exit 2
fi

if ! command -v /usr/lib/Xorg >/dev/null 2>&1; then
  echo "Xorg not found at /usr/lib/Xorg. Install xorg-server first." >&2
  exit 2
fi

if ! command -v xdpyinfo >/dev/null 2>&1; then
  echo "xdpyinfo not found in PATH" >&2
  exit 2
fi

if ! command -v xrandr >/dev/null 2>&1; then
  echo "xrandr not found in PATH" >&2
  exit 2
fi

if ! command -v xprop >/dev/null 2>&1; then
  echo "xprop not found in PATH" >&2
  exit 2
fi

find_free_display() {
  local n
  for n in $(seq 100 200); do
    if [[ ! -e "/tmp/.X${n}-lock" ]]; then
      echo ":$n"
      return 0
    fi
  done
  return 1
}

xorg_display="$(find_free_display || true)"
if [[ -z "$xorg_display" ]]; then
  echo "Failed to find a free X display for Xorg (tried :100..:200)." >&2
  exit 1
fi

tmp_dir="$(mktemp -d)"
tmp_home="$(mktemp -d)"
xorg_conf="$tmp_dir/xorg-dummy.conf"
xorg_log="$tmp_dir/xorg-dummy.log"

xorg_sudo_pid=""
xorg_pid=""
as_pid=""

cleanup() {
  if [[ -n "${as_pid:-}" ]]; then
    kill "$as_pid" 2>/dev/null || true
    for _ in $(seq 1 80); do
      if ! kill -0 "$as_pid" 2>/dev/null; then
        break
      fi
      sleep 0.1
    done
    if kill -0 "$as_pid" 2>/dev/null; then
      kill -KILL "$as_pid" 2>/dev/null || true
    fi
    wait "$as_pid" 2>/dev/null || true
  fi

  if [[ -n "${xorg_pid:-}" ]]; then
    sudo kill "$xorg_pid" 2>/dev/null || true
    for _ in $(seq 1 80); do
      if ! sudo kill -0 "$xorg_pid" 2>/dev/null; then
        break
      fi
      sleep 0.1
    done
    if sudo kill -0 "$xorg_pid" 2>/dev/null; then
      sudo kill -KILL "$xorg_pid" 2>/dev/null || true
    fi
  fi

  if [[ -n "${xorg_sudo_pid:-}" ]]; then
    wait "$xorg_sudo_pid" 2>/dev/null || true
  fi

  rm -rf "$tmp_dir"
  rm -rf "$tmp_home"
}
trap cleanup EXIT

cat >"$xorg_conf" <<'EOF'
Section "Device"
    Identifier  "DummyDevice"
    Driver      "dummy"
    VideoRam    256000
EndSection

Section "Monitor"
    Identifier  "DummyMonitor"
    HorizSync   28.0-80.0
    VertRefresh 48.0-75.0
EndSection

Section "Screen"
    Identifier   "DummyScreen"
    Device       "DummyDevice"
    Monitor      "DummyMonitor"
    DefaultDepth 24
    SubSection "Display"
        Depth   24
        Modes   "1280x800" "1024x768" "800x600"
    EndSubSection
EndSection

Section "ServerLayout"
    Identifier "DummyLayout"
    Screen     "DummyScreen"
EndSection
EOF

rm -f "$log_file"

# Some environments have NVIDIA EGL installed but no usable EGL/GBM backend,
# which can make Xorg crash at startup when GLX is enabled. If we detect that
# setup, force Mesa's EGL vendor to keep Xorg usable.
xorg_env=()
if [[ -n "${__EGL_VENDOR_LIBRARY_FILENAMES:-}" ]]; then
  xorg_env+=( "__EGL_VENDOR_LIBRARY_FILENAMES=$__EGL_VENDOR_LIBRARY_FILENAMES" )
elif [[ -r /usr/share/glvnd/egl_vendor.d/10_nvidia.json ]] \
    && [[ -r /usr/share/glvnd/egl_vendor.d/50_mesa.json ]]; then
  xorg_env+=( "__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json" )
fi

sudo env "${xorg_env[@]}" /usr/lib/Xorg "$xorg_display" -config "$xorg_conf" \
  -noreset -nolisten tcp -logfile "$xorg_log" -ac -novtswitch -sharevts \
  -verbose 1 >/dev/null 2>&1 &
xorg_sudo_pid=$!

display_num="${xorg_display#:}"
xorg_lock="/tmp/.X${display_num}-lock"
for _ in $(seq 1 200); do
  if [[ -s "$xorg_lock" ]]; then
    break
  fi
  if ! kill -0 "$xorg_sudo_pid" 2>/dev/null; then
    break
  fi
  sleep 0.05
done
if [[ -s "$xorg_lock" ]]; then
  xorg_pid="$(head -n 1 "$xorg_lock" | tr -d '[:space:]' || true)"
fi
if [[ -z "${xorg_pid:-}" ]]; then
  xorg_pid="$xorg_sudo_pid"
fi

for _ in $(seq 1 200); do
  if xdpyinfo -display "$xorg_display" >/dev/null 2>&1; then
    break
  fi
  if ! kill -0 "$xorg_sudo_pid" 2>/dev/null; then
    echo "Xorg exited before becoming ready. Log:" >&2
    tail -n 200 "$xorg_log" >&2 || true
    exit 1
  fi
  sleep 0.05
done

if ! xdpyinfo -display "$xorg_display" >/dev/null 2>&1; then
  echo "Xorg did not become ready in time. Log:" >&2
  tail -n 200 "$xorg_log" >&2 || true
  exit 1
fi

mkdir -p "$tmp_home/.afterstep/non-configurable"
cat >"$tmp_home/.afterstep/non-configurable/send_postcard.sh" <<'POSTCARD_EOF'
#!/usr/bin/env sh
exit 0
POSTCARD_EOF
chmod +x "$tmp_home/.afterstep/non-configurable/send_postcard.sh"

DISPLAY="$xorg_display" HOME="$tmp_home" PATH="$prefix/bin:$PATH" \
  "$afterstep_bin" --bypass-autoexec \
  -p "$tmp_home/.afterstep" -g "$config_dir" \
  -l "$log_file" -V 6 2>>"$log_file" &
as_pid=$!

root_has_wm_check() {
  DISPLAY="$xorg_display" xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'
}

for _ in $(seq 1 200); do
  if root_has_wm_check; then
    break
  fi
  if ! kill -0 "$as_pid" 2>/dev/null; then
    echo "AfterStep exited before becoming the active WM. Log:" >&2
    tail -n 200 "$log_file" >&2 || true
    exit 1
  fi
  sleep 0.05
done

if ! root_has_wm_check; then
  echo "AfterStep did not set _NET_SUPPORTING_WM_CHECK on the root window in time. Log:" >&2
  tail -n 200 "$log_file" >&2 || true
  exit 1
fi

wait_for_log_substr() {
  local needle="$1"
  local desc="$2"

  for _ in $(seq 1 200); do
    if grep -Fq "$needle" "$log_file"; then
      return 0
    fi
    if ! kill -0 "$as_pid" 2>/dev/null; then
      echo "AfterStep exited while waiting for $desc. Log:" >&2
      tail -n 200 "$log_file" >&2 || true
      exit 1
    fi
    sleep 0.05
  done

  echo "AfterStep did not log $desc in time. Log:" >&2
  tail -n 200 "$log_file" >&2 || true
  exit 1
}

# Trigger dynamic screen size changes.
DISPLAY="$xorg_display" xrandr -s 1024x768
sleep 0.2
DISPLAY="$xorg_display" xrandr -s 1280x800
sleep 0.2

# Create a RandR 1.5 monitor layout (LEFT/RIGHT), then force a geometry refresh via resize.
out="$(DISPLAY="$xorg_display" xrandr --query | awk '/ connected/{print $1; exit}')"
out="${out:-none}"
DISPLAY="$xorg_display" xrandr --setmonitor LEFT 640/160x800/200+0+0 "$out" >/dev/null 2>&1
DISPLAY="$xorg_display" xrandr --setmonitor RIGHT 640/160x800/200+640+0 "$out" >/dev/null 2>&1
DISPLAY="$xorg_display" xrandr -s 800x600
sleep 0.2
DISPLAY="$xorg_display" xrandr -s 1280x800
sleep 0.2

wait_for_log_substr "{0, 0, 640, 800}" "RandR LEFT monitor rectangle"
wait_for_log_substr "{640, 0, 640, 800}" "RandR RIGHT monitor rectangle"

# Re-layout to TOP/BOTTOM and force another refresh.
DISPLAY="$xorg_display" xrandr --delmonitor LEFT >/dev/null 2>&1 || true
DISPLAY="$xorg_display" xrandr --delmonitor RIGHT >/dev/null 2>&1 || true
DISPLAY="$xorg_display" xrandr --setmonitor TOP 1280/320x400/100+0+0 "$out" >/dev/null 2>&1
DISPLAY="$xorg_display" xrandr --setmonitor BOTTOM 1280/320x400/100+0+400 "$out" >/dev/null 2>&1
DISPLAY="$xorg_display" xrandr -s 1024x768
sleep 0.2
DISPLAY="$xorg_display" xrandr -s 1280x800
sleep 0.2

wait_for_log_substr "{0, 0, 1280, 400}" "RandR TOP monitor rectangle"
wait_for_log_substr "{0, 400, 1280, 400}" "RandR BOTTOM monitor rectangle"

DISPLAY="$xorg_display" xrandr --delmonitor TOP >/dev/null 2>&1 || true
DISPLAY="$xorg_display" xrandr --delmonitor BOTTOM >/dev/null 2>&1 || true

if command -v xeyes >/dev/null 2>&1; then
  DISPLAY="$xorg_display" xeyes &
  client_pid=$!

  for _ in $(seq 1 200); do
    if DISPLAY="$xorg_display" xprop -name xeyes WM_STATE >/dev/null 2>&1; then
      break
    fi
    sleep 0.05
  done

  if ! DISPLAY="$xorg_display" xprop -name xeyes WM_STATE >/dev/null 2>&1; then
    echo "AfterStep did not manage the test client window (xeyes) after RandR changes." >&2
    exit 1
  fi

  kill "$client_pid" 2>/dev/null || true
  wait "$client_pid" 2>/dev/null || true
fi

if grep -Eq 'Segmentation Fault trapped|Non-critical Signal' "$log_file"; then
  echo "AfterStep crashed while running under Xorg (dummy driver):" >&2
  grep -nE 'Segmentation Fault trapped|Non-critical Signal' "$log_file" >&2 || true
  exit 1
fi

if grep -Eq 'ERROR: AddressSanitizer|LeakSanitizer|runtime error:' "$log_file"; then
  echo "Sanitizers reported an error while AfterStep was running under Xorg (dummy driver):" >&2
  grep -nE 'ERROR: AddressSanitizer|LeakSanitizer|runtime error:' "$log_file" >&2 || true
  exit 1
fi

echo "OK (log: $log_file)"
