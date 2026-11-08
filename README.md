# SpaceBeam for OBS

Native OBS Studio filter plugin ported from the [SpaceBeam](https://github.com/CalmyJane/SpaceBeam)
Android app by Calmy Jane.

> **License: GPL-3.0.** This is a derivative work of SpaceBeam's shaders, which are
> GPL-3.0. Any binary or fork you distribute must remain GPL-3.0.

## What it does

Adds a **SpaceBeam** video filter that runs SpaceBeam's effect chain on any source:

```
Mixer (+feedback)
  -> Camera Transform
  -> Color
  -> Edge Detect
  -> Kaleidoscope
  -> Droste (Escher spiral)
  -> Master Transform
  -> 3D Tunnel
  -> Swirl (raymarched gyroid)
```

Each stage has its own set of sliders in the filter properties. Stages that are
visually inert (amount 0, or defaults) are skipped and cost no GPU time.

## What was ported, what was not

| SpaceBeam feature | Status in this port |
| --- | --- |
| Kaleidoscope, Droste, 3D Tunnel, Swirl, Color, Edge Detect | Ported (GLSL ES -> OBS HLSL effect) |
| Camera Transform / Master Transform | Ported (incl. RGB shift, wobble, bend) |
| Frame feedback loop with tap points + blend modes | Ported, internal to the filter |
| Presets, smooth transitions | **Ported** — 9 slots, animated morphing |
| LFOs, BPM sync | **Ported** — 4 LFOs, 5 waveforms, tempo-locked or free-run |
| MIDI mapping | **Not yet** — bind a MIDI-to-hotkey bridge to the preset hotkeys |
| Hand / face tracking (MediaPipe) | **Not ported** (Android-specific; no MediaPipe in the plugin) |
| Gyroscope / accelerometer modulation | **Not ported** (no phone sensors on desktop) |
| Multi-source mixer UI (8 sources) | **Not yet** — currently one input source plus feedback |
| Camera capture, recording, RTSP | Not applicable — OBS does all of this |

## Requirements

- OBS Studio 33.x (Windows x64)
- Visual Studio 2022/2026 with the C++ toolset, and Windows SDK
- CMake + Ninja (both ship with Visual Studio)

## How the build works (no multi-GB checkout)

OBS ships `obs.dll` but **no import library**. Rather than building all of libobs,
this project:

1. Takes `libobs` headers from the matching obs-studio tag (`33.0.1`).
2. Generates `deps/obs.def` from `obs.dll`'s export table with `dumpbin`.
3. Runs `lib /def:` to produce `deps/lib/obs.lib`.
4. Links the plugin against that.

Result: a ~38 KB DLL that depends only on `obs.dll` + the CRT.

Regenerate the import library after an OBS update:

```powershell
.\build.ps1 -RegenerateImportLib
```

## Build

```powershell
.\build.ps1
```

Then install (copies to `C:\ProgramData\obs-studio\plugins\spacebeam`):

```powershell
.\install.ps1
```

Restart OBS. The filter appears as **SpaceBeam** in the filter list.

## Adding it to a scene

1. Right-click a source -> **Filters** -> **+** -> **SpaceBeam**.
2. Start with **Kaleidoscope** (Axis 6, Amount 1.0) for the classic look.
3. Turn up **Droste -> Strength** for the Escher spiral.
4. Turn up **3D Tunnel -> Strength** (with **Speed**) for tunnel flight.
5. Turn up **Swirl -> Strength** for the raymarched gyroid (heaviest stage).

### Feedback

Enable **Feedback -> Enable feedback**. Frames are stored in a ring buffer of the
configured **Frame delay**, then blended back in with the chosen **Blend mode**.
**Tap point** decides what gets recorded: the raw input, or the output of any stage.

Defaults mirror SpaceBeam: tap after Edge Detect, Screen blend, 85% amount.

### Presets and smooth transitions

The **Presets** group stores up to **9** complete parameter snapshots per filter.

1. Dial in a look, choose a **Preset slot**, press **Save to slot**.
2. Set **Transition time** (0 = instant, up to 20 s).
3. Change anything you like, then press **Load slot**.

Loading interpolates *every* numeric parameter from the current values to the
stored ones with a smoothstep curve, so the look morphs rather than snapping.
Booleans (e.g. feedback on/off) switch at the end of the transition. When the
morph finishes the values are committed to the filter settings, so the sliders
in the properties panel land on the preset.

Every slot also has a **hotkey** (Settings -> Hotkeys -> filter name) so you can
switch looks live from a Stream Deck or MIDI-to-hotkey bridge.

For automation, the filter accepts a scriptable command setting:

| `preset_cmd` | Action |
| --- | --- |
| `0` | none |
| `1` | save to the current `preset_slot` |
| `2` | load the current `preset_slot` |
| `3` | clear the current `preset_slot` |

It resets itself to `0` after firing, so it behaves like a button press. Example
with obs-websocket:

```js
await obs.call('SetSourceFilterSettings', {
  sourceName: 'My Source',
  filterName: 'SpaceBeam',
  filterSettings: { preset_slot: 3, preset_time: 2.0, preset_cmd: 2 },
});
```

### Modulation (LFOs + tempo sync)

The **Modulation** group gives each filter **4 independent LFOs**, applied on top
of the slider values (so presets and manual settings still work as a baseline).

| Control | Meaning |
| --- | --- |
| **Tempo (BPM)** | Clock speed used when **Lock LFOs to tempo** is on |
| **Rate** | Cycles per **beat** when locked, cycles per **second** when free |
| **Waveform** | Sine, triangle, saw, square, or random (sample-and-hold) |
| **Target parameter** | Any of ~50 animatable parameters |
| **Depth** | 0-1, as a fraction of the target's full slider range |
| **Phase offset** | 0-1, in cycles — offsets two LFOs on the same target |

Depths are range-relative, so a depth of 0.5 on **Hue** sweeps half the colour
wheel, while 0.5 on **Scale** moves half of the 1.5-12 range. The result is
always clamped to the slider's own limits, so LFOs cannot push a parameter out
of range.

Typical uses: slow sine on **Hue** for colour drift, tempo-locked saw on
**Spiral** for a 4-bar morph, random-hold on **Look X/Y** for jump-cut framing.

## Performance notes

- Swirl is a 60-step raymarch per pixel. On integrated GPUs keep Strength at 0
  unless you actually want it, and consider 720p output.
- Every skipped stage is genuinely free: the technique is not submitted at all.
- The filter renders at the target source's base resolution.

## Layout

```
src/spacebeam.c              plugin + chain engine + properties
data/shaders/spacebeam.effect  all shader stages (one technique each)
data/locale/en-US.ini          UI strings
CMakeLists.txt               build against installed OBS
build.ps1 / install.ps1      convenience wrappers
deps/obs.def, deps/lib/obs.lib  generated import library
```

## Credits

- **SpaceBeam** — Calmy Jane, GPL-3.0. Shader math, parameter ranges and the
  effect chain order come from the Android app.
- This OBS port is not affiliated with or endorsed by the SpaceBeam author.
