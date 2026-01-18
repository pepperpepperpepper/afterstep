#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
prefix="${1:-"$root_dir/_install"}"

include_dir="$prefix/include/libAfterImage"
lib_dir="$prefix/lib"

if [[ ! -r "$include_dir/afterimage.h" ]]; then
  echo "libAfterImage headers not found: $include_dir/afterimage.h" >&2
  echo "Build + install first:" >&2
  echo "  ./configure --prefix=\"$prefix\" && make -j\"\\$(nproc)\" && make install install.data" >&2
  exit 2
fi

if [[ ! -r "$lib_dir/libAfterImage.a" ]]; then
  echo "libAfterImage static lib not found: $lib_dir/libAfterImage.a" >&2
  exit 2
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "$1 not found in PATH" >&2
    exit 2
  fi
}

need_cmd cc
need_cmd pkg-config
need_cmd xvfb-run

if [[ -z "${__EGL_VENDOR_LIBRARY_FILENAMES:-}" ]] \
    && [[ -r /usr/share/glvnd/egl_vendor.d/10_nvidia.json ]] \
    && [[ -r /usr/share/glvnd/egl_vendor.d/50_mesa.json ]]; then
  export __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json
fi

tmp_dir="$(mktemp -d)"
bin="$tmp_dir/xrender-text-smoke"
cleanup() { rm -rf "$tmp_dir"; }
trap cleanup EXIT

cflags="$(pkg-config --cflags x11 xrender xext freetype2)"
libs="$(pkg-config --libs x11 xrender xext freetype2)"

cc -O2 -g -Wall -Wextra \
  -I"$include_dir" \
  $cflags \
  -o "$bin" \
  "$root_dir/tools/xrender-text-smoke.c" \
  "$lib_dir/libAfterImage.a" \
  "$lib_dir/libAfterBase.a" \
  $libs

font="$root_dir/libAfterImage/apps/test.ttf"
if [[ ! -r "$font" ]]; then
  echo "Test font not found: $font" >&2
  exit 2
fi

timeout 20s xvfb-run -a -s "-screen 0 1024x768x24" env \
  AS_XRENDER_TEST_FONT="$font" \
  "$bin"
