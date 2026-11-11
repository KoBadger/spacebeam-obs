#!/usr/bin/env bash
# Check the Linux/macOS build prerequisites — POSIX counterpart to fetch-deps.ps1.
#
# On Windows this script downloads OBS's Qt bundle into deps/qt. On Linux and
# macOS there is nothing to download: libobs, obs-frontend-api and Qt come from
# the system (or, on macOS, from an obs-studio source build). This script only
# reports what is present and what to install.
set -uo pipefail

OS="$(uname -s)"
echo "== SpaceBeam prerequisites ($OS)"
echo

have() { command -v "$1" >/dev/null 2>&1; }
status() { if have "$1"; then echo "  ok    $2"; else echo "  MISS  $2"; fi; }

status cmake    "cmake >= 3.20"
status pkg-config "pkg-config"

missing=0
# libobs: check the CMake package config that find_package(libobs) needs.
LIBOBS_CFG=""
for d in /usr/lib/cmake/libobs /usr/lib/*/cmake/libobs /usr/local/lib/cmake/libobs \
         /opt/homebrew/lib/cmake/libobs "$HOME/obs-studio/build/libobs"; do
  [ -d "$d" ] && LIBOBS_CFG="$d" && break
done
if [ -n "$LIBOBS_CFG" ]; then
  echo "  ok    libobs CMake package ($LIBOBS_CFG)"
else
  echo "  MISS  libobs CMake package (find_package(libobs) will fail)"
  missing=1
fi

# Qt6 is only needed for the dock panel.
qt_ok=0
have qmake6 && qt_ok=1
[ -n "${Qt6_DIR:-}" ] && qt_ok=1
for d in /usr/lib/*/cmake/Qt6 /usr/lib/cmake/Qt6 /usr/local/lib/cmake/Qt6 \
         /opt/homebrew/lib/cmake/Qt6; do
  [ -d "$d" ] && qt_ok=1
done
if [ "$qt_ok" = 1 ]; then
  echo "  ok    Qt6 (dock panel)"
else
  echo "  warn  Qt6 not found — the filter still builds, just without the dock"
fi

echo
if [ "$missing" = 1 ]; then
  cat <<'HINT'
Install libobs:
  Debian/Ubuntu   sudo apt install libobs-dev qt6-base-dev
  Fedora          sudo dnf install obs-studio-devel qt6-qtbase-devel
  Arch            sudo pacman -S obs-studio qt6-base
  macOS           no Homebrew package exists; build obs-studio from source.
                  See docs/BUILDING-UNIX.md.
HINT
  exit 1
fi
echo "All required prerequisites are present. Next: ./build.sh"
