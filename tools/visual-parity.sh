#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat 1>&2 <<'EOF'
Usage: tools/visual-parity.sh [--x11 DIR] [--wayland DIR] [--out DIR] [--fuzz PERCENT]

Compares the X11 screenshot baseline against the Wayland/aswlcomp baseline and writes
diff artifacts (mask + absdiff) plus a small numeric summary.

Defaults:
  --x11     screenshots/2026-01-18-xvfb
  --wayland screenshots/2026-02-09-wayland
  --out     screenshots/wayland-test-diff-YYYY-MM-DD
  --fuzz    0%

Notes:
  - `--fuzz` is passed to ImageMagick `compare` for the AE metric and the mask.
  - This tool is intended for quick iteration (it does not try to be "pixel perfect").
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

x11_dir="${repo_root}/screenshots/2026-01-18-xvfb"
wl_dir="${repo_root}/screenshots/2026-02-09-wayland"
out_dir="${repo_root}/screenshots/wayland-test-diff-$(date +%Y-%m-%d)"
fuzz="0%"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --x11)
      x11_dir="${2:-}"; shift 2
      ;;
    --wayland|--wl)
      wl_dir="${2:-}"; shift 2
      ;;
    --out)
      out_dir="${2:-}"; shift 2
      ;;
    --fuzz)
      fuzz="${2:-}"; shift 2
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

require_cmd magick
require_cmd compare
require_cmd awk

mkdir -p -- "${out_dir}"

# Map "same-scene" screenshots between the X11 and Wayland galleries.
pairs=(
  "01-desktop.png:01-desktop.png"
  "02-root-menu.png:02-menu.png"
  "03-clients.png:03-clients.png"
  "04-clients-menu.png:04-clients-menu.png"
)

echo "X11:     ${x11_dir}"
echo "Wayland: ${wl_dir}"
echo "Out:     ${out_dir}"
echo "Fuzz:    ${fuzz}"
echo

for pair in "${pairs[@]}"; do
  x11="${x11_dir}/${pair%%:*}"
  wl="${wl_dir}/${pair##*:}"
  base="${pair%%:*}"
  stem="${base%.png}"

  if [[ ! -f "${x11}" ]]; then
    echo "Skip ${base}: missing ${x11}" >&2
    continue
  fi
  if [[ ! -f "${wl}" ]]; then
    echo "Skip ${base}: missing ${wl}" >&2
    continue
  fi

  rmse="$(compare -metric RMSE "${x11}" "${wl}" null: 2>&1 || true)"
  mae="$(compare -metric MAE "${x11}" "${wl}" null: 2>&1 || true)"
  ae="$(compare -metric AE -fuzz "${fuzz}" "${x11}" "${wl}" null: 2>&1 || true)"

  mask="${out_dir}/${stem}.mask.png"
  absdiff="${out_dir}/${stem}.absdiff.png"

  magick compare -fuzz "${fuzz}" -highlight-color white -lowlight-color black \
    "${x11}" "${wl}" "${mask}" >/dev/null 2>&1 || true

  magick "${x11}" "${wl}" -compose difference -composite -auto-level "${absdiff}"

  geom="$(magick "${mask}" -trim -format '%@' info: 2>/dev/null || true)"

  {
    echo "== ${base}"
    echo "RMSE: ${rmse}"
    echo "MAE:  ${mae}"
    echo "AE:   ${ae} (fuzz=${fuzz})"
    echo "mask_trim: ${geom}"
    echo "mask:   ${mask}"
    echo "absdiff:${absdiff}"
    echo
  }
done

echo "Done."
