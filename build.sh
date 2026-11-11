#!/usr/bin/env bash
# Build SpaceBeam for OBS — POSIX (Linux + macOS) counterpart to build.ps1.
#
#   ./build.sh                 # release build into ./build
#   BUILD_TYPE=Debug ./build.sh
#   NO_DOCK=1 ./build.sh       # skip the Qt dock panel
#   ./build.sh --clean
#
# Prerequisites (see docs/BUILDING-UNIX.md):
#   Linux  - libobs-dev + qt6-base-dev (dock optional)
#   macOS  - an obs-studio source build providing libobs + obs-frontend-api
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"

if [ "${1:-}" = "--clean" ]; then
  echo "removing $BUILD_DIR"
  rm -rf "$BUILD_DIR"
  shift
fi

CMAKE_ARGS=(-DCMAKE_BUILD_TYPE="$BUILD_TYPE")
if [ "${NO_DOCK:-0}" = "1" ]; then
  CMAKE_ARGS+=(-DSB_NO_DOCK=ON)
fi
# Allow overriding the install layout prefixes (useful for staging).
[ -n "${CMAKE_INSTALL_PREFIX_OVERRIDE:-}" ] && CMAKE_ARGS+=(-DCMAKE_INSTALL_PREFIX="$CMAKE_INSTALL_PREFIX_OVERRIDE")

echo "== configure ($(uname -s), $BUILD_TYPE)"
cmake -S "$ROOT" -B "$BUILD_DIR" "${CMAKE_ARGS[@]}"

echo "== build"
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel

echo
echo "== artifacts"
ls -l "$BUILD_DIR"/spacebeam*.so 2>/dev/null || {
  echo "no spacebeam*.so produced — see the configure output above (libobs missing?)" >&2
  exit 1
}
