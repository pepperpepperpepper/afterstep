#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
prefix="${1:-"$root_dir/_install"}"
duration_sec="${2:-${AS_SOAK_DURATION_SEC:-60}}"

afterstep_bin="$prefix/bin/afterstep"
config_dir="$prefix/share/afterstep"

if [[ -n "${AS_SOAK_LOG:-}" ]]; then
  log_file="$AS_SOAK_LOG"
elif [[ -w "$root_dir" ]]; then
  log_file="$root_dir/afterstep-xvfb-soak.log"
else
  log_file="$(mktemp "${TMPDIR:-/tmp}/afterstep-xvfb-soak.XXXXXX.log")"
fi

if [[ ! -w "$(dirname "$log_file")" ]]; then
  echo "Log directory not writable: $(dirname "$log_file")" >&2
  echo "Set AS_SOAK_LOG to a writable path to override." >&2
  exit 2
fi

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

if ! command -v xprop >/dev/null 2>&1; then
  echo "xprop not found in PATH" >&2
  exit 2
fi

inner_script="$(mktemp)"
tmp_home="$(mktemp -d)"
cleanup() { rm -f "$inner_script"; rm -rf "$tmp_home"; }
trap cleanup EXIT

cat >"$inner_script" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

: "${AS_SOAK_LOG:?}"
: "${AS_SOAK_DURATION_SEC:?}"
: "${AFTERSTEP_BIN:?}"
: "${AFTERSTEP_CONFIG_DIR:?}"

rm -f "$AS_SOAK_LOG"

mkdir -p "$HOME/.afterstep/non-configurable"
cat >"$HOME/.afterstep/non-configurable/send_postcard.sh" <<'POSTCARD_EOF'
#!/usr/bin/env sh
exit 0
POSTCARD_EOF
chmod +x "$HOME/.afterstep/non-configurable/send_postcard.sh"

root_has_wm_check() {
  xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'
}

wait_for_root_wm() {
  for _ in $(seq 1 200); do
    if root_has_wm_check; then
      return 0
    fi
    if ! kill -0 "$as_pid" 2>/dev/null; then
      return 1
    fi
    sleep 0.05
  done
  root_has_wm_check
}

wait_for_client_wm_state() {
  local name="$1"
  for _ in $(seq 1 200); do
    if xprop -name "$name" WM_STATE >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.05
  done
  xprop -name "$name" WM_STATE >/dev/null
}

spawn_managed_client() {
  local title="$1"
  local pid=""

  if command -v xeyes >/dev/null 2>&1; then
    # Prefer a small client that doesn't depend on font availability.
    xeyes &
    pid=$!
    title=xeyes
  elif command -v xterm >/dev/null 2>&1; then
    xterm -T "$title" -geometry 40x6+10+10 -e sh -c 'sleep 30' &
    pid=$!
  else
    echo "No X11 client tools found (need xterm or xeyes)." >&2
    return 1
  fi

  wait_for_client_wm_state "$title"

  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
}

"$AFTERSTEP_BIN" -p "$HOME/.afterstep" -g "$AFTERSTEP_CONFIG_DIR" -l "$AS_SOAK_LOG" -V 5 &
as_pid=$!

shutdown() {
  kill "$as_pid" 2>/dev/null || true
  for _ in $(seq 1 120); do
    if ! kill -0 "$as_pid" 2>/dev/null; then
      return 0
    fi
    sleep 0.1
  done
  kill -KILL "$as_pid" 2>/dev/null || true
  wait "$as_pid" 2>/dev/null || true
}
trap shutdown EXIT

wait_for_root_wm

deadline=$((SECONDS + AS_SOAK_DURATION_SEC))
i=0
while (( SECONDS < deadline )); do
  i=$((i + 1))
  title="as-soak-xterm-$i"
  spawn_managed_client "$title"
  xprop -root _NET_SUPPORTING_WM_CHECK >/dev/null
  sleep 0.2
done

if grep -Eq 'Segmentation Fault trapped|Non-critical Signal' "$AS_SOAK_LOG"; then
  echo "AfterStep crashed while running under Xvfb:" >&2
  grep -nE 'Segmentation Fault trapped|Non-critical Signal' "$AS_SOAK_LOG" >&2 || true
  exit 1
fi

if grep -Eq 'ERROR: AddressSanitizer|LeakSanitizer|runtime error:' "$AS_SOAK_LOG"; then
  echo "Sanitizers reported an error while AfterStep was running under Xvfb:" >&2
  grep -nE 'ERROR: AddressSanitizer|LeakSanitizer|runtime error:' "$AS_SOAK_LOG" >&2 || true
  exit 1
fi

if grep -Eq 'failed to locate icon file|failed to load X11 font' "$AS_SOAK_LOG"; then
  echo "AfterStep started, but missing assets/fonts:" >&2
  grep -nE 'failed to locate icon file|failed to load X11 font' "$AS_SOAK_LOG" >&2 || true
  exit 1
fi
EOF

chmod +x "$inner_script"

timeout_sec=$((duration_sec + 45))
# AfterStep's session integration expects a DBus session in many environments.
run_cmd=("$inner_script")
if command -v dbus-run-session >/dev/null 2>&1; then
  run_cmd=(dbus-run-session -- "$inner_script")
fi

# Some environments have NVIDIA EGL installed but no usable EGL/GBM backend,
# which can make Xvfb crash at startup when GLX is enabled. If we detect that
# setup, force Mesa's EGL vendor to keep Xvfb usable.
if [[ -z "${__EGL_VENDOR_LIBRARY_FILENAMES:-}" ]] \
    && [[ -r /usr/share/glvnd/egl_vendor.d/10_nvidia.json ]] \
    && [[ -r /usr/share/glvnd/egl_vendor.d/50_mesa.json ]]; then
  export __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json
fi

timeout "${timeout_sec}s" xvfb-run -a -s "-screen 0 1024x768x24" env \
  HOME="$tmp_home" \
  PATH="$prefix/bin:$PATH" \
  AFTERSTEP_BIN="$afterstep_bin" \
  AFTERSTEP_CONFIG_DIR="$config_dir" \
  AS_SOAK_LOG="$log_file" \
  AS_SOAK_DURATION_SEC="$duration_sec" \
  "${run_cmd[@]}"

echo "OK (log: $log_file)"
