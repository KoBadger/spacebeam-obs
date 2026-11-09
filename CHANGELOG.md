# Changelog

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
