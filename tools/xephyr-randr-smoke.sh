#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
prefix="${1:-"$root_dir/_install"}"

afterstep_bin="$prefix/bin/afterstep"
config_dir="$prefix/share/afterstep"
log_file="$root_dir/afterstep-xephyr-randr-smoke.log"

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

if ! command -v xvfb-run >/dev/null 2>&1; then
  echo "xvfb-run not found in PATH" >&2
  exit 2
fi

if ! command -v Xephyr >/dev/null 2>&1; then
  echo "Xephyr not found in PATH" >&2
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

if ! command -v xset >/dev/null 2>&1; then
  echo "xset not found in PATH" >&2
  exit 2
fi

inner_script="$(mktemp)"
tmp_home="$(mktemp -d)"
cleanup() { rm -f "$inner_script"; rm -rf "$tmp_home"; }
trap cleanup EXIT

cat >"$inner_script" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

: "${AS_SMOKE_LOG:?}"
: "${AFTERSTEP_BIN:?}"
: "${AFTERSTEP_CONFIG_DIR:?}"

rm -f "$AS_SMOKE_LOG"

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

nested_display="$(find_free_display || true)"
if [[ -z "$nested_display" ]]; then
  echo "Failed to find a free X display for Xephyr (tried :100..:200)." >&2
  exit 1
fi

xephyr_log="$(mktemp)"
xephyr_pid=""
as_pid=""

shutdown_all() {
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

  if [[ -n "${xephyr_pid:-}" ]]; then
    kill "$xephyr_pid" 2>/dev/null || true
    wait "$xephyr_pid" 2>/dev/null || true
  fi

  rm -f "$xephyr_log"
}
trap shutdown_all EXIT

# Run Xephyr inside the (headless) host Xvfb. Disable Xinerama so we exercise
# AfterStep's RandR path even when Xinerama is available elsewhere.
Xephyr "$nested_display" -ac -noreset \
  -screen 1024x768 -resizeable -dumb \
  -xinerama >"$xephyr_log" 2>&1 &
xephyr_pid=$!

for _ in $(seq 1 200); do
  if DISPLAY="$nested_display" xset q >/dev/null 2>&1; then
    break
  fi
  if ! kill -0 "$xephyr_pid" 2>/dev/null; then
    echo "Xephyr exited before becoming ready. Log:" >&2
    tail -n 200 "$xephyr_log" >&2 || true
    exit 1
  fi
  sleep 0.05
done

if ! DISPLAY="$nested_display" xset q >/dev/null 2>&1; then
  echo "Xephyr did not become ready in time. Log:" >&2
  tail -n 200 "$xephyr_log" >&2 || true
  exit 1
fi

mkdir -p "$HOME/.afterstep/non-configurable"
cat >"$HOME/.afterstep/non-configurable/send_postcard.sh" <<'POSTCARD_EOF'
#!/usr/bin/env sh
exit 0
POSTCARD_EOF
chmod +x "$HOME/.afterstep/non-configurable/send_postcard.sh"

DISPLAY="$nested_display" "$AFTERSTEP_BIN" --bypass-autoexec \
  -p "$HOME/.afterstep" -g "$AFTERSTEP_CONFIG_DIR" \
  -l "$AS_SMOKE_LOG" -V 6 2>>"$AS_SMOKE_LOG" &
as_pid=$!

root_has_wm_check() {
  DISPLAY="$nested_display" xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'
}

for _ in $(seq 1 200); do
  if root_has_wm_check; then
    break
  fi
  if ! kill -0 "$as_pid" 2>/dev/null; then
    echo "AfterStep exited before becoming the active WM. Log:" >&2
    tail -n 200 "$AS_SMOKE_LOG" >&2 || true
    exit 1
  fi
  sleep 0.05
done

if ! root_has_wm_check; then
  echo "AfterStep did not set _NET_SUPPORTING_WM_CHECK on the root window in time. Log:" >&2
  tail -n 200 "$AS_SMOKE_LOG" >&2 || true
  exit 1
fi

# Optionally create a RandR 1.5 monitor layout (LEFT+RIGHT halves) so we can
# verify AfterStep consumes XRRGetMonitors() data.
monitor_layout_set=0
out="$(DISPLAY="$nested_display" xrandr --query | awk '/ connected/{print $1; exit}')"
if [[ -n "$out" ]]; then
  if DISPLAY="$nested_display" xrandr --setmonitor LEFT 512/512x768/768+0+0 "$out" >/dev/null 2>&1 \
      && DISPLAY="$nested_display" xrandr --setmonitor RIGHT 512/512x768/768+512+0 "$out" >/dev/null 2>&1; then
    monitor_layout_set=1
  else
    echo "Skipping RandR monitor layout test (xrandr --setmonitor not supported by server)." >&2
  fi
fi

wait_for_log_substr() {
  local needle="$1"
  local desc="$2"

  for _ in $(seq 1 200); do
    if grep -Fq "$needle" "$AS_SMOKE_LOG"; then
      return 0
    fi
    if ! kill -0 "$as_pid" 2>/dev/null; then
      echo "AfterStep exited while waiting for $desc. Log:" >&2
      tail -n 200 "$AS_SMOKE_LOG" >&2 || true
      exit 1
    fi
    sleep 0.05
  done

  echo "AfterStep did not log $desc in time. Log:" >&2
  tail -n 200 "$AS_SMOKE_LOG" >&2 || true
  exit 1
}

# Trigger a few dynamic screen size changes.
DISPLAY="$nested_display" xrandr -s 800x600
sleep 0.2
DISPLAY="$nested_display" xrandr -s 1024x768
sleep 0.2

if [[ "$monitor_layout_set" -eq 1 ]]; then
  wait_for_log_substr "{0, 0, 512," "RandR LEFT monitor rectangle"
  wait_for_log_substr "{512, 0, 512," "RandR RIGHT monitor rectangle"

  # Change monitor layout (TOP/BOTTOM), then force another geometry refresh.
  DISPLAY="$nested_display" xrandr --delmonitor LEFT >/dev/null 2>&1 || true
  DISPLAY="$nested_display" xrandr --delmonitor RIGHT >/dev/null 2>&1 || true

  if DISPLAY="$nested_display" xrandr --setmonitor TOP 1024/1024x384/384+0+0 "$out" >/dev/null 2>&1 \
      && DISPLAY="$nested_display" xrandr --setmonitor BOTTOM 1024/1024x384/384+0+384 "$out" >/dev/null 2>&1; then
    DISPLAY="$nested_display" xrandr -s 800x600
    sleep 0.2
    DISPLAY="$nested_display" xrandr -s 1024x768
    sleep 0.2

    wait_for_log_substr "{0, 0, 1024, 384}" "RandR TOP monitor rectangle"
    wait_for_log_substr "{0, 384, 1024," "RandR BOTTOM monitor rectangle"
  else
    echo "Skipping RandR monitor relayout test (xrandr --setmonitor failed unexpectedly)." >&2
  fi

  DISPLAY="$nested_display" xrandr --delmonitor TOP >/dev/null 2>&1 || true
  DISPLAY="$nested_display" xrandr --delmonitor BOTTOM >/dev/null 2>&1 || true
fi

if command -v xeyes >/dev/null 2>&1; then
  DISPLAY="$nested_display" xeyes &
  client_pid=$!

  for _ in $(seq 1 200); do
    if DISPLAY="$nested_display" xprop -name xeyes WM_STATE >/dev/null 2>&1; then
      break
    fi
    sleep 0.05
  done

  if ! DISPLAY="$nested_display" xprop -name xeyes WM_STATE >/dev/null 2>&1; then
    echo "AfterStep did not manage the test client window (xeyes) after RandR resizes." >&2
    exit 1
  fi

  kill "$client_pid" 2>/dev/null || true
  wait "$client_pid" 2>/dev/null || true
fi
EOF

chmod +x "$inner_script"

# Some environments have NVIDIA EGL installed but no usable EGL/GBM backend,
# which can make Xvfb crash at startup when GLX is enabled. If we detect that
# setup, force Mesa's EGL vendor to keep Xvfb usable.
if [[ -z "${__EGL_VENDOR_LIBRARY_FILENAMES:-}" ]] \
    && [[ -r /usr/share/glvnd/egl_vendor.d/10_nvidia.json ]] \
    && [[ -r /usr/share/glvnd/egl_vendor.d/50_mesa.json ]]; then
  export __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json
fi

timeout 50s xvfb-run -a -s "-screen 0 1600x1200x24" env \
  HOME="$tmp_home" \
  PATH="$prefix/bin:$PATH" \
  AFTERSTEP_BIN="$afterstep_bin" \
  AFTERSTEP_CONFIG_DIR="$config_dir" \
  AS_SMOKE_LOG="$log_file" \
  "$inner_script"

if grep -Eq 'Segmentation Fault trapped|Non-critical Signal' "$log_file"; then
  echo "AfterStep crashed while running under Xephyr+Xvfb:" >&2
  grep -nE 'Segmentation Fault trapped|Non-critical Signal' "$log_file" >&2 || true
  exit 1
fi

if grep -Eq 'ERROR: AddressSanitizer|LeakSanitizer|runtime error:' "$log_file"; then
  echo "Sanitizers reported an error while AfterStep was running under Xephyr+Xvfb:" >&2
  grep -nE 'ERROR: AddressSanitizer|LeakSanitizer|runtime error:' "$log_file" >&2 || true
  exit 1
fi

echo "OK (log: $log_file)"
