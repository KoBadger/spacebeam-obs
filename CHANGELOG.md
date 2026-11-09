# Changelog

## 1.2.0

### Packaging

- **New install layout**: binaries now live in
  `plugins\spacebeam\bin\64bit\`, the layout OBS's own plugin template uses,
  with `data\` alongside. Existing installs should delete the old
  `plugins\spacebeam` folder first (or use `install.ps1`, which cleans it), or
  OBS will find the plugin twice and list the filter twice.
- Releases are now built and drafted by CI on a version tag instead of by hand,
  and the zip ships with a `.sha256` alongside it.

### Fixed

- The dock wired its default audio routings with hardcoded parameter indices
  (`21` for kaleidoscope zoom, `12` for hue). Those now resolve by name against
  a table shared with the filter, so reordering parameters cannot silently
  repoint the defaults.
- Audio analysis read only the left channel. It now averages the first two
  channels, so material panned hard to one side still drives the effect.

### Added

- `tests/smoke.js` — a post-install check over obs-websocket that verifies the
  filter exists, is enabled, has presets, and that music reaction has both a
  source and at least one routing.

## 1.1.1

### Music actually does something now

The Music toggle raised and lowered the band values but the image never moved,
because nothing was routed to a parameter and the routings live in the
properties dialog. Turning Music on now wires a default pair the first time:

- bass → kaleidoscope zoom
- treble → hue

Existing routings are never overwritten. Turning Music on with no source
selected also picks the first audio source instead of silently doing nothing.

Verified by measuring the render: with bass pointed at brightness, mean
luminance sits at 110.2 in silence, rises to 142-159 during playback, and
returns to exactly 110.2 afterwards.

### Master on/off

The dock now has an **On / Bypass** button in the top row. It flips the
filter's own enabled flag - the same switch as the checkbox in the Filters
dialog - so one click bypasses every stage.

### Fixed

- The dock's toggles kept their constructed state because the first sync ran
  before the filter list existed. It now syncs once more after the controls
  are built, so the panel always matches the filter.

## 1.1.0

### Kaleidoscope on/off, with a smooth unfold

`kaleido_on` switches the kaleidoscope stage, easing toward identity over
**Morph time** (default 0.6 s) instead of snapping. No extra passes: the shader
already lerps to identity as the amount reaches zero, so the gate simply scales
the amount. Available in the properties dialog and in the dock.

### Dock: the live controls are now on top

Reordered around what gets touched while performing: Filter, Kaleidoscope,
Music (with an audio-source picker), Master zoom, Master spin, then the presets,
save row, transition and tempo.

- The **Music** toggle shows the same `audio_on` state as the properties dialog
  and picks a source if none is set.
- **From:** lists every source carrying audio and remembers the choice.
- The dock now always reappears floating on startup. OBS does not persist a
  floating dock, and showing it in the dock area can silently land it on a
  hidden tab, which looked like the panel had vanished.

### Fixed

- The dock could write settings while it was still filling its controls at
  startup, so merely loading a scene could switch the music toggle on. Widget
  syncing is now inert until the constructor finishes.
- The kaleidoscope button started checked regardless of the filter's state,
  because the first sync ran before the filter list existed.

### Packaging

- The CI Qt build now uses OBS's own dependencies archive
  (`obs-deps 2026-10-23`), which carries the exact Qt 6.11.1 that OBS 33 ships;
  `fetch-deps.ps1` downloads it into `deps/qt`. aqt could not supply it.

## 1.0.1

### The dock can no longer break the filter

The control panel is now built into its own `spacebeam_dock.dll`, and the
filter DLL imports no Qt at all. OBS ships its own Qt, so if OBS is updated to
a different Qt minor version, only the dock fails to load — the filter keeps
working and OBS logs a warning saying why. Previously a Qt mismatch prevented
the whole plugin from loading.

`build.ps1` also finds Qt automatically now (from `QTDIR`, `Qt6_DIR`,
`SB_QT_ROOT`, `C:\Qt\6.11.1\msvc2022_64` or `~/QtSDK`) instead of pointing at
one hardcoded path.

### Notes

- If you install by hand, `spacebeam_dock.dll` must sit next to
  `spacebeam.dll`. Copying only the filter DLL still works; you just get no
  dock.

## 1.0.0

First release of the OBS port.

### Visual chain

Nine stages ported from the SpaceBeam Android app, each one skippable so an
unused stage costs nothing:

Camera Transform → Color → Edge Detect → Kaleidoscope → Droste spiral →
Master Transform → 3D Tunnel → Swirl → feedback.

### Control

- **Preset slots** — 9 slots, saved into the scene collection, with smooth
  morphing between looks and an adjustable transition time.
- **Preset bank export/import** — move all 9 slots between machines as JSON.
- **Dock panel** — floating control surface: filter picker, preset pads with
  names, save-to-slot, transition and tempo sliders. Right-click a pad to
  clear it.
- **Hotkeys** — 9 source-scoped hotkeys (one per pad) plus save/load current.

### Modulation

- **4 LFOs** with sine / triangle / saw / square / sample-and-hold shapes,
  targeting any of ~50 parameters, free-running or synced to tempo.
- **Audio reactivity** — 4 routing slots fed by a 4-band analysis of any audio
  source in the scene, with sensitivity and attack/release smoothing.

### Mixing and performance

- **4-layer source mixer** with per-layer opacity, blend mode, position,
  scale and rotation.
- **Render scale** (0.25–1.0) and **swirl step count** (8–60) for slower GPUs.
- **Auto-cycling** — step through a chosen list of slots every N seconds or
  every N beats.

### Notes

- Requires OBS Studio 30 or newer on Windows x64.
- The dock requires Qt 6.11.x headers at build time and is skipped if they are
  not found; everything else builds without Qt.
