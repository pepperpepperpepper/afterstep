#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
prefix="${1:-"$root_dir/_install"}"

afterstep_bin="$prefix/bin/afterstep"
config_dir="$prefix/share/afterstep"
log_file="$root_dir/afterstep-xvfb-smoke.log"

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

: "${AS_SMOKE_LOG:?}"
: "${AFTERSTEP_BIN:?}"
: "${AFTERSTEP_CONFIG_DIR:?}"

rm -f "$AS_SMOKE_LOG"

mkdir -p "$HOME/.afterstep/non-configurable"
cat >"$HOME/.afterstep/non-configurable/send_postcard.sh" <<'POSTCARD_EOF'
#!/usr/bin/env sh
exit 0
POSTCARD_EOF
chmod +x "$HOME/.afterstep/non-configurable/send_postcard.sh"

"$AFTERSTEP_BIN" --bypass-autoexec -p "$HOME/.afterstep" -g "$AFTERSTEP_CONFIG_DIR" -l "$AS_SMOKE_LOG" -V 5 &
as_pid=$!

root_has_wm_check() {
  xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'
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

# Sanity-check that AfterStep is actually managing windows by launching a small
# X11 client and waiting for the WM to set WM_STATE on it.
if command -v xeyes >/dev/null 2>&1; then
  xeyes &
  client_pid=$!

  for _ in $(seq 1 200); do
    if xprop -name xeyes WM_STATE >/dev/null 2>&1; then
      break
    fi
    sleep 0.05
  done

  if ! xprop -name xeyes WM_STATE >/dev/null 2>&1; then
    echo "AfterStep did not manage the test client window (xeyes) in time." >&2
    exit 1
  fi

  kill "$client_pid" 2>/dev/null || true
  wait "$client_pid" 2>/dev/null || true
fi

# Give AfterStep time to finish init (modules/screen/etc) before asking it to shutdown.
sleep 2

kill "$as_pid" 2>/dev/null || true
for _ in $(seq 1 50); do
  if ! kill -0 "$as_pid" 2>/dev/null; then
    break
  fi
  sleep 0.1
done
if kill -0 "$as_pid" 2>/dev/null; then
  kill -KILL "$as_pid" 2>/dev/null || true
fi
wait "$as_pid" 2>/dev/null || true

if grep -Eq 'Segmentation Fault trapped|Non-critical Signal' "$AS_SMOKE_LOG"; then
  echo "AfterStep crashed while running under Xvfb:" >&2
  grep -nE 'Segmentation Fault trapped|Non-critical Signal' "$AS_SMOKE_LOG" >&2 || true
  exit 1
fi

if grep -Eq 'ERROR: AddressSanitizer|LeakSanitizer|runtime error:' "$AS_SMOKE_LOG"; then
  echo "Sanitizers reported an error while AfterStep was running under Xvfb:" >&2
  grep -nE 'ERROR: AddressSanitizer|LeakSanitizer|runtime error:' "$AS_SMOKE_LOG" >&2 || true
  exit 1
fi

if grep -Eq 'failed to locate icon file|failed to load X11 font' "$AS_SMOKE_LOG"; then
  echo "AfterStep started, but missing assets/fonts:" >&2
  grep -nE 'failed to locate icon file|failed to load X11 font' "$AS_SMOKE_LOG" >&2 || true
  exit 1
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

timeout 20s xvfb-run -a -s "-screen 0 1024x768x24" env \
  HOME="$tmp_home" \
  PATH="$prefix/bin:$PATH" \
  AFTERSTEP_BIN="$afterstep_bin" \
  AFTERSTEP_CONFIG_DIR="$config_dir" \
  AS_SMOKE_LOG="$log_file" \
  "$inner_script"

echo "OK (log: $log_file)"
