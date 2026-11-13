#!/usr/bin/env bash
# Package the built plugin — POSIX counterpart to package.ps1.
#
#   ./package.sh              # build (if needed) and write
#                             #   dist/spacebeam-<version>-<os>-<arch>.zip
#   ./package.sh --no-build
#
# The archive name is platform-tagged (e.g. spacebeam-1.2.6-linux-x64.zip) so
# release assets are unambiguous; the Windows package built by package.ps1 is
# spacebeam-<version>-windows-x64.zip.
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

# Platform tag for the archive name (mirrors package.ps1's "-windows-x64").
case "$(uname -s)" in
  Darwin) PLATFORM="macos" ;;
  Linux) PLATFORM="linux" ;;
  *) PLATFORM="$(uname -s | tr '[:upper:]' '[:lower:]')" ;;
esac
case "$(uname -m)" in
  x86_64|amd64) ARCH="x64" ;;
  arm64|aarch64) ARCH="arm64" ;;
  *) ARCH="$(uname -m)" ;;
esac
TAG="$PLATFORM-$ARCH"

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

# Same metadata file the Windows package ships, so OBS's plugin list shows the
# same name/version/description on every platform. Kept in sync with package.ps1.
cat >"$STAGE/spacebeam/data/manifest.json" <<JSON
{
    "display_name": "SpaceBeam",
    "id": "3f0b1e52-9c74-4a6b-8f21-7d2c5e90b4a1",
    "version": "$VERSION",
    "authors": ["Calmy Jane (original app)", "OBS port"],
    "name": "spacebeam",
    "description": "Real-time kaleidoscope / Droste / tunnel / swirl visual synthesizer ported from SpaceBeam.",
    "long_description": "Ported from the SpaceBeam Android app. Camera Transform, Color, Edge Detect, Kaleidoscope, Droste spiral, Master Transform, 3D Tunnel, Swirl, frame feedback, LFOs with tempo sync, audio reactivity, a 4-layer source mixer, preset slots with morphing and a control dock.",
    "urls": {"repository": "https://github.com/CalmyJane/SpaceBeam"},
    "has_banner": false,
    "has_icon": false
}
JSON

mkdir -p "$OUT_DIR"
if command -v zip >/dev/null 2>&1; then
  ARCHIVE="$OUT_DIR/spacebeam-$VERSION-$TAG.zip"
  rm -f "$ARCHIVE"
  ( cd "$STAGE" && zip -r -q "$ARCHIVE" spacebeam )
else
  # zip is not installed everywhere (many Linux boxes); tar.gz is equally fine.
  ARCHIVE="$OUT_DIR/spacebeam-$VERSION-$TAG.tar.gz"
  rm -f "$ARCHIVE"
  tar -czf "$ARCHIVE" -C "$STAGE" spacebeam
fi

# Checksum beside the archive, in the same format package.ps1 writes
# ("<hash>  <name>"), so a download can be verified on either platform.
if command -v sha256sum >/dev/null 2>&1; then
  ( cd "$OUT_DIR" && sha256sum "$(basename "$ARCHIVE")" >"$(basename "$ARCHIVE").sha256" )
elif command -v shasum >/dev/null 2>&1; then
  ( cd "$OUT_DIR" && shasum -a 256 "$(basename "$ARCHIVE")" >"$(basename "$ARCHIVE").sha256" )
else
  echo "note: no sha256sum/shasum found - checksum not written" >&2
fi

echo "== package"
echo "  $ARCHIVE"
[ -f "$ARCHIVE.sha256" ] && echo "  $ARCHIVE.sha256"
if command -v zip >/dev/null 2>&1; then
  unzip -l "$ARCHIVE"
else
  tar -tzf "$ARCHIVE"
fi
