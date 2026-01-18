#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: tools/kms-randr-smoke.sh [--apply] [--display DISPLAY] [--output1 OUT] [--output2 OUT]

Smoke-test RandR hotplug/layout handling on a real (KMS-backed) Xorg session by
toggling a secondary output off/on and checking that AfterStep keeps managing
windows and updates EWMH geometry.

Defaults to a dry run. Pass --apply to actually run xrandr reconfiguration.

Exit codes:
  0  OK (or dry run)
  1  Test failed
  2  Prereqs missing / test not applicable (e.g. only 1 active output)
EOF
}

display="${DISPLAY:-}"
apply=0
out1=""
out2=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --apply) apply=1; shift ;;
    --display) display="${2:-}"; shift 2 ;;
    --output1) out1="${2:-}"; shift 2 ;;
    --output2) out2="${2:-}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *)
      echo "Unknown arg: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "$1 not found in PATH" >&2
    exit 2
  fi
}

need_cmd xrandr
need_cmd xprop
need_cmd xset

if [[ -z "$display" ]]; then
  echo "DISPLAY not set; use --display or export DISPLAY." >&2
  exit 2
fi

if ! DISPLAY="$display" xset q >/dev/null 2>&1; then
  echo "Cannot query display $display (xset q failed)." >&2
  exit 2
fi

# Verify that AfterStep looks like the active WM (best-effort).
wm_check="$(DISPLAY="$display" xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | awk '{print $5}' || true)"
if [[ -z "$wm_check" || "$wm_check" == "not" ]]; then
  echo "No _NET_SUPPORTING_WM_CHECK on root; is a WM running on $display?" >&2
  exit 1
fi

wm_name="$(DISPLAY="$display" xprop -id "$wm_check" _NET_WM_NAME 2>/dev/null | sed -n 's/.*= \"\\(.*\\)\".*/\\1/p' || true)"
if [[ -z "$wm_name" ]]; then
  wm_name="$(DISPLAY="$display" xprop -id "$wm_check" WM_NAME 2>/dev/null | sed -n 's/.*= \"\\(.*\\)\".*/\\1/p' || true)"
fi
if [[ "$wm_name" != *AfterStep* ]]; then
  echo "Warning: WM name does not look like AfterStep (WM name: ${wm_name:-unknown})." >&2
  echo "If AfterStep is not the active WM on $display, this test won't be meaningful." >&2
fi

declare -A out_modepos out_rotation out_primary out_active
active_outputs=()
primary_out=""

while IFS= read -r line; do
  [[ "$line" =~ ^([A-Za-z0-9._-]+)[[:space:]]+connected ]] || continue
  name="${BASH_REMATCH[1]}"

  is_primary=0
  if [[ "$line" == *" connected primary "* ]]; then
    is_primary=1
    primary_out="$name"
  fi

  modepos="$(awk '{for(i=1;i<=NF;i++){if($i ~ /^[0-9]+x[0-9]+[+-][0-9]+[+-][0-9]+$/){print $i; exit}}}' <<<"$line")"
  [[ -n "$modepos" ]] || continue
  rotation="$(awk '{for(i=1;i<=NF;i++){if($i ~ /^[0-9]+x[0-9]+[+-][0-9]+[+-][0-9]+$/){print $(i+1); exit}}}' <<<"$line")"
  rotation="${rotation:-normal}"

  out_modepos["$name"]="$modepos"
  out_rotation["$name"]="$rotation"
  out_primary["$name"]="$is_primary"
  out_active["$name"]=1
  active_outputs+=("$name")
done < <(DISPLAY="$display" xrandr --query)

if [[ ${#active_outputs[@]} -lt 2 ]]; then
  echo "Need at least two *active* outputs on $display (found: ${#active_outputs[@]})." >&2
  echo "Tip: connect a second monitor or create virtual heads (e.g. vkms/VirtualHeads) for hotplug testing." >&2
  exit 2
fi

if [[ -z "$out1" ]]; then
  out1="${primary_out:-${active_outputs[0]}}"
fi

if [[ -z "$out2" ]]; then
  for candidate in "${active_outputs[@]}"; do
    if [[ "$candidate" != "$out1" ]] && [[ "${out_primary[$candidate]:-0}" -ne 1 ]]; then
      out2="$candidate"
      break
    fi
  done
  if [[ -z "$out2" ]]; then
    for candidate in "${active_outputs[@]}"; do
      if [[ "$candidate" != "$out1" ]]; then
        out2="$candidate"
        break
      fi
    done
  fi
fi

if [[ -z "${out_active[$out1]:-}" || -z "${out_active[$out2]:-}" ]]; then
  echo "Selected outputs are not active: out1=$out1 out2=$out2" >&2
  exit 2
fi

if [[ "${out_primary[$out2]:-0}" -eq 1 ]]; then
  echo "Refusing to use the primary output as output2 (out2=$out2)." >&2
  echo "Pick outputs explicitly with: --output1 OUT --output2 OUT" >&2
  exit 2
fi

modepos2="${out_modepos[$out2]}"
rotation2="${out_rotation[$out2]}"

if [[ ! "$modepos2" =~ ^([0-9]+x[0-9]+)([+-][0-9]+)([+-][0-9]+)$ ]]; then
  echo "Failed to parse xrandr mode/pos for $out2: $modepos2" >&2
  exit 1
fi
mode2="${BASH_REMATCH[1]}"
xoff="${BASH_REMATCH[2]#+}"
yoff="${BASH_REMATCH[3]#+}"
pos2="${xoff}x${yoff}"

get_xrandr_current_wh() {
  DISPLAY="$display" xrandr --query | awk '
    /^Screen 0:/ {
      for (i = 1; i <= NF; i++) {
        if ($i == "current") {
          w = $(i+1);
          h = $(i+3);
          gsub(/,/, "", h);
          print w "x" h;
          exit;
        }
      }
    }
  '
}

get_net_desktop_geometry() {
  DISPLAY="$display" xprop -root _NET_DESKTOP_GEOMETRY 2>/dev/null | \
    sed -n 's/.*= \\([0-9][0-9]*\\), \\([0-9][0-9]*\\).*/\\1x\\2/p' | head -n 1
}

open_test_client() {
  if command -v xeyes >/dev/null 2>&1; then
    DISPLAY="$display" xeyes >/dev/null 2>&1 &
    echo $!
  fi
}

client_pid=""

cleanup() {
  if [[ "$apply" -eq 1 ]]; then
    DISPLAY="$display" xrandr --output "$out2" --mode "$mode2" --pos "$pos2" --rotate "$rotation2" >/dev/null 2>&1 || true
  fi

  if [[ -n "${client_pid:-}" ]]; then
    kill "$client_pid" 2>/dev/null || true
    wait "$client_pid" 2>/dev/null || true
  fi
}
trap cleanup EXIT

before_screen="$(get_xrandr_current_wh || true)"
before_geom="$(get_net_desktop_geometry || true)"

echo "Display: $display"
echo "Output1: $out1"
echo "Output2: $out2 (restore: mode=$mode2 pos=$pos2 rotate=$rotation2)"
echo "Screen size (before): ${before_screen:-unknown}"
echo "_NET_DESKTOP_GEOMETRY (before): ${before_geom:-missing}"

echo "Planned actions:"
echo "  xrandr --output \"$out2\" --off"
echo "  xrandr --output \"$out2\" --mode \"$mode2\" --pos \"$pos2\" --rotate \"$rotation2\""

if [[ "$apply" -ne 1 ]]; then
  echo "Dry run only. Re-run with --apply to perform the test." >&2
  exit 0
fi

client_pid="$(open_test_client || true)"
if [[ -n "$client_pid" ]]; then
  for _ in $(seq 1 200); do
    if DISPLAY="$display" xprop -name xeyes WM_STATE >/dev/null 2>&1; then
      break
    fi
    sleep 0.05
  done
  if ! DISPLAY="$display" xprop -name xeyes WM_STATE >/dev/null 2>&1; then
    echo "AfterStep did not manage the test client window (xeyes) before output changes." >&2
    exit 1
  fi
fi

DISPLAY="$display" xrandr --output "$out2" --off
sleep 0.5

after_off_screen="$(get_xrandr_current_wh || true)"
after_off_geom="$(get_net_desktop_geometry || true)"
echo "Screen size (after off): ${after_off_screen:-unknown}"
echo "_NET_DESKTOP_GEOMETRY (after off): ${after_off_geom:-missing}"

if [[ -n "$before_screen" && -n "$after_off_screen" && "$before_screen" == "$after_off_screen" ]]; then
  echo "Screen size did not change after disabling $out2; cannot validate RandR geometry refresh reliably." >&2
  echo "Try arranging outputs side-by-side (not mirrored), then re-run." >&2
  exit 2
fi

DISPLAY="$display" xrandr --output "$out2" --mode "$mode2" --pos "$pos2" --rotate "$rotation2"
sleep 0.5

after_restore_screen="$(get_xrandr_current_wh || true)"
after_restore_geom="$(get_net_desktop_geometry || true)"
echo "Screen size (after restore): ${after_restore_screen:-unknown}"
echo "_NET_DESKTOP_GEOMETRY (after restore): ${after_restore_geom:-missing}"

if [[ -n "$client_pid" ]]; then
  for _ in $(seq 1 200); do
    if DISPLAY="$display" xprop -name xeyes WM_STATE >/dev/null 2>&1; then
      break
    fi
    sleep 0.05
  done
  if ! DISPLAY="$display" xprop -name xeyes WM_STATE >/dev/null 2>&1; then
    echo "AfterStep did not manage the test client window (xeyes) after output changes." >&2
    exit 1
  fi
fi

if [[ -n "$before_geom" && -n "$after_off_geom" && "$before_geom" == "$after_off_geom" ]]; then
  echo "_NET_DESKTOP_GEOMETRY did not change after disabling $out2." >&2
  echo "This may indicate AfterStep did not refresh its screen geometry on RandR hotplug." >&2
  exit 1
fi

echo "OK"

