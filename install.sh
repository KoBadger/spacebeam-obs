#!/usr/bin/env bash
# Install the built SpaceBeam plugin — POSIX counterpart to install.ps1.
#
#   ./install.sh --user              # per-user OBS plugin dir (no root, recommended)
#   sudo ./install.sh                # system install into /usr
#   sudo ./install.sh --prefix /usr/local
#
# Run ./build.sh first (or pass --build to build here).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
MODE="system"
PREFIX="${PREFIX:-/usr}"
DO_BUILD=0

while [ $# -gt 0 ]; do
  case "$1" in
    --user) MODE="user" ;;
    --prefix) PREFIX="$2"; shift ;;
    --build) DO_BUILD=1 ;;
    -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

[ "$DO_BUILD" = 1 ] && "$ROOT/build.sh"

MOD="$BUILD_DIR/spacebeam.so"
DOCK="$BUILD_DIR/spacebeam_dock.so"
if [ ! -f "$MOD" ]; then
  echo "missing $MOD — run ./build.sh first" >&2
  exit 1
fi

if [ "$MODE" = user ]; then
  OS="$(uname -s)"
  if [ "$OS" = Darwin ]; then
    BASE="$HOME/Library/Application Support/obs-studio/plugins/spacebeam"
    BIN_DIR="$BASE/bin"
  else
    BASE="${XDG_CONFIG_HOME:-$HOME/.config}/obs-studio/plugins/spacebeam"
    BIN_DIR="$BASE/bin/64bit"
  fi
  echo "== per-user install -> $BASE"
  mkdir -p "$BIN_DIR" "$BASE/data"
  install -m 0755 "$MOD" "$BIN_DIR/spacebeam.so"
  [ -f "$DOCK" ] && install -m 0755 "$DOCK" "$BIN_DIR/spacebeam_dock.so"
  cp -R "$ROOT/data/locale" "$BASE/data/"
  cp -R "$ROOT/data/shaders" "$BASE/data/"
  echo
  echo "installed. Restart OBS to load SpaceBeam."
  echo "  module: $BIN_DIR/spacebeam.so"
  echo "  data:   $BASE/data/"
else
  echo "== system install -> $PREFIX"
  cmake --install "$BUILD_DIR" --prefix "$PREFIX"
  echo
  echo "installed under $PREFIX. Restart OBS to load SpaceBeam."
fi
