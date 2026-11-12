# Building SpaceBeam on Linux and macOS

Windows builds against the *installed* OBS plus import libraries generated from
`obs.dll` (see the main README). Linux and macOS cannot do that — a Unix build
links against `libobs` at link time — so the CMake project detects the platform
and uses OBS's own CMake package config instead:

```cmake
find_package(libobs REQUIRED)
find_package(obs-frontend-api)        # only needed for the dock
find_package(Qt6 COMPONENTS Widgets)  # only needed for the dock
```

The vendored `libobs/` headers and `deps/` import libraries are **Windows-only**
and are not used on Unix. The shader (`data/shaders/spacebeam.effect`) is the
same file on every platform: OBS transpiles the HLSL-style effect to GLSL/SPIR-V
on Linux and macOS at load time, so there is no per-platform shader variant.

## Linux

### 1. Install the prerequisites

OBS ships `libobs-dev` and the frontend API headers in most distributions:

| Distro | Command |
| --- | --- |
| Debian / Ubuntu | `sudo apt install libobs-dev qt6-base-dev` |
| Fedora | `sudo dnf install obs-studio-devel qt6-qtbase-devel` |
| Arch / Manjaro | `sudo pacman -S obs-studio qt6-base` |
| openSUSE | `sudo zypper install obs-studio-devel qt6-base-devel` |

`qt6-base-dev` is only needed for the optional dock panel. Without it the filter
still builds — CMake prints *"dock panel disabled"* and `SB_WITH_DOCK` is not
defined.

Check what is present:

```bash
./fetch-deps.sh
```

### 2. Build

```bash
./build.sh                 # -> build/spacebeam.so (+ build/spacebeam_dock.so)
NO_DOCK=1 ./build.sh       # filter only, no Qt
BUILD_TYPE=Debug ./build.sh
./build.sh --clean
```

### 3. Install

```bash
./install.sh --user        # ~/.config/obs-studio/plugins/spacebeam (no root)
sudo ./install.sh          # system-wide, into /usr
sudo ./install.sh --prefix /usr/local
```

`--user` writes the OBS 28+ per-user layout:

```
~/.config/obs-studio/plugins/spacebeam/
├── bin/64bit/spacebeam.so
├── bin/64bit/spacebeam_dock.so     (if the dock was built)
└── data/{locale,shaders}/…
```

### 4. Package

```bash
./package.sh               # dist/spacebeam-<version>.zip (or .tar.gz without zip)
```

## macOS

There is **no Homebrew formula for libobs development files** — Homebrew's
`obs-studio` is an app cask with no headers or libraries. To build a plugin you
need `libobs` and `obs-frontend-api` from an `obs-studio` source build.

### 1. Build libobs once

```bash
brew install cmake ninja qt@6
git clone --recursive https://github.com/obsproject/obs-studio
cd obs-studio
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)" \
  -DENABLE_SCRIPTING=OFF
cmake --build build --parallel
cmake --install build --prefix "$HOME/obs-studio/dist"
```

This is the slow part (ffmpeg, x264, etc.). The official OBS CI uses the
`obsproject/obs-deps` bundle to speed it up; the plain source build is fine for
local use.

### 2. Build SpaceBeam against it

```bash
cd /path/to/spacebeam-obs
CMAKE_PREFIX_PATH="$HOME/obs-studio/dist" ./build.sh
CMAKE_PREFIX_PATH="$HOME/obs-studio/dist" ./install.sh --user
```

`install.sh --user` writes:

```
~/Library/Application Support/obs-studio/plugins/spacebeam/
├── bin/spacebeam.so
├── bin/spacebeam_dock.so           (if the dock was built)
└── data/{locale,shaders}/…
```

## How the dock loads (both platforms)

The dock is a **separate module** so the filter itself never imports Qt — if OBS
is built against a different Qt minor, only the dock stops loading.

On Windows the filter finds it with `GetModuleFileNameW` + `LoadLibraryExW`. On
Unix it asks the dynamic loader where *this* module lives (`dladdr`) and
`dlopen`s `spacebeam_dock.so` from the same directory. If it is missing or its
Qt mismatches, the OBS log says so and the filter keeps working:

```
[spacebeam] dock library not loaded (/usr/lib/…/libQt6Widgets.so.6: …) from …, running without the dock panel
```

## File dialogs (bank import / export)

Windows uses the Win32 common dialogs. On Linux and macOS the filter shells out
to a **native dialog helper** so it does not have to link Qt:

- Linux: `zenity` (GNOME) then `kdialog` (KDE)
- macOS: `osascript` (`choose file` / `choose file name`)

Install one of them, e.g. `sudo apt install zenity`. If neither is present, the
filter logs a warning and you can still import/export by setting **Bank file
path** in the filter properties — that path always skips the dialog.

## Notes

- macOS plugins are `.so` (CMake `MODULE` libraries), matching what OBS loads on
  macOS — not `.dylib`.
- `SB_NO_DOCK=ON` (or `NO_DOCK=1` for the shell scripts) builds the filter only;
  CI uses this to keep the Linux job dependency-light.
