#!/usr/bin/env bash
# Package the built plugin — POSIX counterpart to package.ps1.
#
#   ./package.sh              # build (if needed) and write dist/spacebeam-<version>.zip
#   ./package.sh --no-build
#
# The archive is laid out for OBS's per-user plugin dir, so it extracts straight
# into it:
#   Linux  ~/.config/obs-studio/plugins/spacebeam/{bin/64bit,data}
#   macOS  ~/Library/Application Support/obs-studio/plugins/spacebeam/{bin,data}
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
OUT_DIR="${OUT_DIR:-$ROOT/dist}"
VERSION="$(tr -d ' \r\n' < "$ROOT/VERSION")"
NO_BUILD=0

while [ $# -gt 0 ]; do
  case "$1" in
    --no-build) NO_BUILD=1 ;;
    -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

[ "$NO_BUILD" = 1 ] || "$ROOT/build.sh"

MOD="$BUILD_DIR/spacebeam.so"
DOCK="$BUILD_DIR/spacebeam_dock.so"
if [ ! -f "$MOD" ]; then
  echo "missing $MOD — run ./build.sh first" >&2
  exit 1
fi
[ -n "$VERSION" ] || { echo "VERSION file is empty" >&2; exit 1; }

STAGE="$OUT_DIR/stage"
rm -rf "$STAGE"
if [ "$(uname -s)" = Darwin ]; then
  BIN_REL="bin"
else
  BIN_REL="bin/64bit"
fi
mkdir -p "$STAGE/spacebeam/$BIN_REL" "$STAGE/spacebeam/data"
install -m 0755 "$MOD" "$STAGE/spacebeam/$BIN_REL/spacebeam.so"
[ -f "$DOCK" ] && install -m 0755 "$DOCK" "$STAGE/spacebeam/$BIN_REL/spacebeam_dock.so"
cp -R "$ROOT/data/locale" "$STAGE/spacebeam/data/"
cp -R "$ROOT/data/shaders" "$STAGE/spacebeam/data/"

mkdir -p "$OUT_DIR"
if command -v zip >/dev/null 2>&1; then
  ARCHIVE="$OUT_DIR/spacebeam-$VERSION.zip"
  rm -f "$ARCHIVE"
  ( cd "$STAGE" && zip -r -q "$ARCHIVE" spacebeam )
else
  # zip is not installed everywhere (many Linux boxes); tar.gz is equally fine.
  ARCHIVE="$OUT_DIR/spacebeam-$VERSION.tar.gz"
  rm -f "$ARCHIVE"
  tar -czf "$ARCHIVE" -C "$STAGE" spacebeam
fi

echo "== package"
echo "  $ARCHIVE"
if command -v zip >/dev/null 2>&1; then
  unzip -l "$ARCHIVE"
else
  tar -tzf "$ARCHIVE"
fi
