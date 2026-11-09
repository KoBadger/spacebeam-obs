# Changelog

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
