#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat 1>&2 <<'EOF'
Usage: tools/wayland-smoke.sh

Builds the Wayland suite (`wayland/`) and runs a minimal compositor smoke test by
starting `wayland/aswlcomp` under Xvfb (wlroots x11 backend), exercising the
control protocol via `wayland/aswlctl`, and shutting down cleanly.
EOF
}

require_cmd() {
  local cmd="$1"
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "Error: '$cmd' is required." >&2
    exit 2
  fi
}

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"

log_file="${AS_SMOKE_LOG:-}"
if [[ -z "${log_file}" ]]; then
  if [[ -w "${root_dir}" ]]; then
    log_file="${root_dir}/afterstep-wayland-smoke.log"
  else
    log_file="$(mktemp "${TMPDIR:-/tmp}/afterstep-wayland-smoke.XXXXXX.log")"
  fi
fi

case "${1:-}" in
  --help|-h)
    usage
    exit 0
    ;;
  "")
    ;;
  *)
    echo "Error: unknown argument: $1" >&2
    usage
    exit 2
    ;;
esac

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

  exec xvfb-run -a -s "-screen 0 1280x720x24" env \
    ASWL_IN_XVFB=1 \
    AS_SMOKE_LOG="${log_file}" \
    "${0}" "$@"
fi

require_cmd make

cd -- "${root_dir}"

make -C wayland aswlcomp aswlctl aswlmenu aswlpanel aswlbg aswlidle aswlprobe
make -C wayland aswllock

tmp_home="$(mktemp -d)"
runtime_dir="$(mktemp -d)"
chmod 700 "${runtime_dir}"
state_file="$(mktemp -t afterstep-aswlcomp.state.XXXXXX)"
autostart_file="$(mktemp -t afterstep-aswlcomp.autostart.XXXXXX)"

as_pid=""
socket="aswlcomp-smoke-$$"

cleanup() {
  if [[ -n "${as_pid}" ]]; then
    kill "${as_pid}" 2>/dev/null || true
    wait "${as_pid}" 2>/dev/null || true
    as_pid=""
  fi
  rm -f -- "${state_file}"
  rm -f -- "${autostart_file}"
  rm -rf -- "${tmp_home}" "${runtime_dir}"
}
trap cleanup EXIT

rm -f -- "${log_file}"

cat >"${autostart_file}" <<'EOF'
# wayland-smoke: exercise config parsing
set repeat_rate 30
set repeat_delay 500
EOF

WLR_BACKENDS=x11 \
WLR_RENDERER="${WLR_RENDERER:-pixman}" \
XDG_CURRENT_DESKTOP=AfterStep:wlroots \
XDG_SESSION_DESKTOP=AfterStep \
XDG_SESSION_TYPE=wayland \
HOME="${tmp_home}" \
XDG_RUNTIME_DIR="${runtime_dir}" \
./wayland/aswlcomp --socket "${socket}" --state "${state_file}" --autostart "${autostart_file}" >"${log_file}" 2>&1 &
as_pid=$!

require_comp_alive() {
  if [[ -z "${as_pid}" ]]; then
    echo "aswlcomp PID missing" >&2
    exit 1
  fi
  if ! kill -0 "${as_pid}" 2>/dev/null; then
    echo "aswlcomp died unexpectedly. Log tail:" >&2
    tail -n 200 "${log_file}" >&2 || true
    exit 1
  fi
}

wait_for_socket() {
  local sock_path="${runtime_dir}/${socket}"
  for _ in $(seq 1 200); do
    require_comp_alive
    if [[ -S "${sock_path}" ]]; then
      return 0
    fi
    sleep 0.05
  done
  return 1
}

if ! wait_for_socket; then
  echo "Wayland socket did not appear (aswlcomp crashed?). Log tail:" >&2
  tail -n 200 "${log_file}" >&2 || true
  exit 1
fi

found_config=0
for _ in $(seq 1 200); do
  require_comp_alive
  if grep -q "aswlcomp: config: repeat_rate=30" "${log_file}"; then
    found_config=1
    break
  fi
  sleep 0.05
done
if [[ "${found_config}" -ne 1 ]]; then
  echo "Config parsing did not run (repeat_rate missing). Log tail:" >&2
  tail -n 200 "${log_file}" >&2 || true
  exit 1
fi

# Verify compositor advertises core IME-related globals.
require_comp_alive
if ! WAYLAND_DISPLAY="${socket}" XDG_RUNTIME_DIR="${runtime_dir}" ./wayland/aswlprobe \
    zwp_text_input_manager_v3 \
    zwp_input_method_manager_v2 \
    zwp_virtual_keyboard_manager_v1 \
    zwp_pointer_constraints_v1 \
    zwp_relative_pointer_manager_v1 \
    zwp_primary_selection_device_manager_v1 \
    xdg_activation_v1 \
    >>"${log_file}" 2>&1; then
  echo "Wayland protocol probe failed. Log tail:" >&2
  tail -n 200 "${log_file}" >&2 || true
  exit 1
fi

WAYLAND_DISPLAY="${socket}" XDG_RUNTIME_DIR="${runtime_dir}" ./wayland/aswlctl list_windows >/dev/null

# Exercise the control protocol's exec path (layer-shell + xdg-shell clients).
require_comp_alive
WAYLAND_DISPLAY="${socket}" XDG_RUNTIME_DIR="${runtime_dir}" ./wayland/aswlctl exec "./wayland/aswlpanel" || true
sleep 0.1
require_comp_alive
WAYLAND_DISPLAY="${socket}" XDG_RUNTIME_DIR="${runtime_dir}" ./wayland/aswlctl exec "./wayland/aswlmenu" || true

found_menu=0
for _ in $(seq 1 200); do
  require_comp_alive
  if WAYLAND_DISPLAY="${socket}" XDG_RUNTIME_DIR="${runtime_dir}" ./wayland/aswlctl list_windows 2>/dev/null \
      | grep -q "app_id=afterstep\\.aswlmenu"; then
    found_menu=1
    break
  fi
  sleep 0.05
done
if [[ "${found_menu}" -ne 1 ]]; then
  echo "aswlmenu did not appear in the window list. Log tail:" >&2
  tail -n 200 "${log_file}" >&2 || true
  exit 1
fi

require_comp_alive
WAYLAND_DISPLAY="${socket}" XDG_RUNTIME_DIR="${runtime_dir}" ./wayland/aswlctl close_focused || true
sleep 0.2

# Exercise session-lock (ext-session-lock-v1) with the built-in lock client.
require_comp_alive
WAYLAND_DISPLAY="${socket}" \
XDG_RUNTIME_DIR="${runtime_dir}" \
HOME="${tmp_home}" \
./wayland/aswllock --auto-unlock 1 >>"${log_file}" 2>&1 &
lock_pid=$!

for _ in $(seq 1 400); do
  require_comp_alive
  if ! kill -0 "${lock_pid}" 2>/dev/null; then
    break
  fi
  sleep 0.05
done

if kill -0 "${lock_pid}" 2>/dev/null; then
  echo "aswllock did not exit (session-lock stuck?). Log tail:" >&2
  tail -n 200 "${log_file}" >&2 || true
  kill "${lock_pid}" 2>/dev/null || true
  wait "${lock_pid}" 2>/dev/null || true
  exit 1
fi

lock_rc=0
wait "${lock_pid}" 2>/dev/null || lock_rc=$?
if [[ "${lock_rc}" -ne 0 ]]; then
  echo "aswllock exited with status ${lock_rc}. Log tail:" >&2
  tail -n 200 "${log_file}" >&2 || true
  exit 1
fi

require_comp_alive
WAYLAND_DISPLAY="${socket}" XDG_RUNTIME_DIR="${runtime_dir}" ./wayland/aswlctl quit || true

for _ in $(seq 1 200); do
  if ! kill -0 "${as_pid}" 2>/dev/null; then
    break
  fi
  sleep 0.05
done

if kill -0 "${as_pid}" 2>/dev/null; then
  echo "aswlcomp did not exit after quit." >&2
  tail -n 200 "${log_file}" >&2 || true
  exit 1
fi

wait_rc=0
wait "${as_pid}" 2>/dev/null || wait_rc=$?
as_pid=""
if [[ "${wait_rc}" -ne 0 ]]; then
  echo "aswlcomp exited with status ${wait_rc}. Log tail:" >&2
  tail -n 200 "${log_file}" >&2 || true
  exit 1
fi

# Exercise idle-inhibit (idle-inhibit-unstable-v1) by verifying it blocks the compositor's idle-lock timer.
echo "ASWL_IDLE_INHIBIT_TEST_BEGIN" >>"${log_file}"

socket="aswlcomp-idle-smoke-$$"
as_pid=""

WLR_BACKENDS=x11 \
WLR_RENDERER="${WLR_RENDERER:-pixman}" \
ASWLCOMP_IDLE_LOCK_SECONDS=3 \
ASWLCOMP_LOCK_CMD="echo ASWLCOMP_IDLE_TEST_LOCK_SPAWNED" \
XDG_CURRENT_DESKTOP=AfterStep:wlroots \
XDG_SESSION_DESKTOP=AfterStep \
XDG_SESSION_TYPE=wayland \
HOME="${tmp_home}" \
XDG_RUNTIME_DIR="${runtime_dir}" \
./wayland/aswlcomp --socket "${socket}" --state "${state_file}" >>"${log_file}" 2>&1 &
as_pid=$!

if ! wait_for_socket; then
  echo "Wayland socket did not appear for idle test (aswlcomp crashed?). Log tail:" >&2
  tail -n 200 "${log_file}" >&2 || true
  exit 1
fi

require_comp_alive
WAYLAND_DISPLAY="${socket}" XDG_RUNTIME_DIR="${runtime_dir}" ./wayland/aswlidle --hold-ms 3500 >>"${log_file}" 2>&1
echo "ASWL_IDLE_INHIBIT_TEST_AFTER_INHIBIT" >>"${log_file}"

if sed -n '/ASWL_IDLE_INHIBIT_TEST_BEGIN/,/ASWL_IDLE_INHIBIT_TEST_AFTER_INHIBIT/p' "${log_file}" \
    | grep -q "ASWLCOMP_IDLE_TEST_LOCK_SPAWNED"; then
  echo "Idle lock fired while inhibited. Log excerpt:" >&2
  sed -n '/ASWL_IDLE_INHIBIT_TEST_BEGIN/,/ASWL_IDLE_INHIBIT_TEST_AFTER_INHIBIT/p' "${log_file}" >&2 || true
  exit 1
fi

found_lock=0
for _ in $(seq 1 400); do
  require_comp_alive
  if grep -q "ASWLCOMP_IDLE_TEST_LOCK_SPAWNED" "${log_file}"; then
    found_lock=1
    break
  fi
  sleep 0.05
done
if [[ "${found_lock}" -ne 1 ]]; then
  echo "Idle lock did not fire after inhibitor ended. Log tail:" >&2
  tail -n 200 "${log_file}" >&2 || true
  exit 1
fi

require_comp_alive
WAYLAND_DISPLAY="${socket}" XDG_RUNTIME_DIR="${runtime_dir}" ./wayland/aswlctl quit || true

for _ in $(seq 1 200); do
  if ! kill -0 "${as_pid}" 2>/dev/null; then
    break
  fi
  sleep 0.05
done

if kill -0 "${as_pid}" 2>/dev/null; then
  echo "aswlcomp did not exit after quit (idle test)." >&2
  tail -n 200 "${log_file}" >&2 || true
  exit 1
fi

wait_rc=0
wait "${as_pid}" 2>/dev/null || wait_rc=$?
as_pid=""
if [[ "${wait_rc}" -ne 0 ]]; then
  echo "aswlcomp exited with status ${wait_rc} (idle test). Log tail:" >&2
  tail -n 200 "${log_file}" >&2 || true
  exit 1
fi

echo "ASWL_IDLE_INHIBIT_TEST_END" >>"${log_file}"

if grep -Eq 'Segmentation fault|double free or corruption|ERROR: AddressSanitizer|LeakSanitizer|runtime error:' "${log_file}"; then
  echo "Wayland smoke run reported a crash/sanitizer error. Log excerpt:" >&2
  grep -nE 'Segmentation fault|double free or corruption|ERROR: AddressSanitizer|LeakSanitizer|runtime error:' "${log_file}" >&2 || true
  exit 1
fi

echo "OK (log: ${log_file})"
