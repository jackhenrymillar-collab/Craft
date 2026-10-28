# Foundry: physical-modelling percussion plugin (JUCE)

> Working name: **Foundry** (placeholder, safe to rename later).
> This file is the project brief. Read all of it before writing code, and keep it up to date as decisions are made.

## 1. What we're building

A percussion-only synthesizer plugin where every drum sound comes from a **physically simulated object** the user builds from parts. No samples.

There are two ways into the same instrument:

- **Kit view** (default, for playing): 8 drum pads, a recipe panel for the selected pad, a "shared stand" coupling matrix and a 16-step sequencer. Fast and musical.
- **Bench view** (detail view, one pad at a time): a top-down workbench on a green cutting mat. The user drags parts (membranes, plates, bars, springs…) together, joins them, picks materials, places strikers and microphones, and tap-tests anywhere on a part.

Every pad in the Kit view *is* a bench instrument. The Kit view's recipe panel is a simplified editor over the same data the Bench view edits in full.

Percussion only, so **inharmonic overtones are a feature**. Pads aren't tuned to a scale by default. They have a *Size/Tune* control that scales the whole object.

### Out of scope (for now)
- Melodic/keyboard-tracked play
- Finite-element meshing of arbitrary drawn shapes (analytic modes first; FEM may come later)
- Effects beyond a simple room/air-mic model

## 2. Tech stack and project setup

- **JUCE**, the latest stable release, pulled with CMake `FetchContent` and pinned to a release tag. Check the current tag before pinning; don't guess.
- **CMake** (not Projucer), **C++20**.
- Plugin formats: **VST3, AU (macOS), Standalone**. Add CLAP later via `clap-juce-extensions` if wanted.
- Tests: **Catch2** (via FetchContent).
- Validation: **pluginval** run against the built VST3/AU.
- Fonts: **IBM Plex Sans** and **IBM Plex Mono** (SIL OFL), embedded with `juce_add_binary_data`.
- Licensing: JUCE is dual-licensed (open-source copyleft or commercial). Flag this to the user before any distribution decisions; don't decide it.

### Suggested layout
```
CMakeLists.txt
cmake/                     # FetchContent helpers, warnings, sanitizer options
source/
  plugin/                  # PluginProcessor, PluginEditor, parameter layout, state I/O
  model/                   # Instrument graph data (parts, joints, exciters, mics, materials)
  compile/                 # Graph -> CompiledInstrument (modes, coupling), runs off the audio thread
  dsp/                     # Modal banks, exciters, contacts, particles, mics, voice/pad engine, sequencer
  ui/
    theme/                 # Colours, fonts, LookAndFeel
    kit/                   # Kit view components
    bench/                 # Bench view components
    common/                # Knobs, sliders, chips, meters
tools/
  render_cli/              # Headless offline renderer: preset + hit pattern -> WAV + spectrum PNG/CSV
tests/
presets/                   # Factory kits (JSON or XML)
docs/                      # Design notes, decisions log
```

## 3. Architecture

### 3.1 Data model (message thread)
Each pad owns an **instrument graph**, stored in a `juce::ValueTree` so undo/redo, state saving and UI binding come for free.

- **Part** (resonator): `type` (Membrane, Plate, Bar, Shell, Tube, Spring, Box), geometry (size, thickness, tension where relevant), `material` (or a 2-material blend), position/rotation on the bench.
- **Joint**: connects point *a* on part X to point *b* on part Y. Types:
  - `Bolt`: stiff linear spring
  - `Rubber`: soft, lossy spring
  - `Contact`: one-sided nonlinear spring with a **gap**. Gives rattles and buzz.
  - `Wire`: a group of contact points across an area (snare wires)
- **Loose objects**: Beads, Rice, Chain, Sand. Particle count, mass and bounciness; they rest on a part's surface.
- **Exciter**: Mallet (hardness, mass), Brush, Drop (falling ball), Scrape, Rub, Slap, Pluck. Each has a strike position (with random spread).
- **Mics**: `Contact` (attached to a point on a part) or `Air` (bench position, with distance). Each has a level and pan.
- **Pad settings**: level, pan, choke group, MIDI note, Size/Tune.

**Rule:** structural edits (adding a part, changing a joint) are **not** automatable parameters. Continuous controls are exposed as automatable `AudioProcessorValueTreeState` parameters per pad: Force, Hardness, Spread, Choke, Rattle, Room, Size/Tune, Level, Pan, plus two assignable macros.

### 3.2 Compile step (background thread)
When the graph changes, a background job turns it into an immutable **`CompiledInstrument`**:
- the modes of each part: frequency, decay (T60), and mode shape sampled at every point that matters (strike points, joint points, mic points, particle contact points)
- the coupling matrices for linear joints
- the setup for contacts, particles and mics

The compiled instrument is handed to the audio thread through a **lock-free swap**: an atomic pointer plus deferred deletion on the message thread. Crossfade old→new over ~20 ms so editing while playing doesn't click. Show "modes solved · N ms" in the UI.

### 3.3 Modal solving (analytic first)
Implement closed-form or table-based modes per part type, scaled by material and geometry:
- **Membrane** (circular, tensioned): Bessel-function zeros. Mode shapes are J_m(k r)·cos(m θ).
- **Bar**: Euler–Bernoulli free–free or clamped–free.
- **Plate** (rectangular, thin): Kirchhoff approximation with simply-supported or free edges (use the standard published approximations for free edges).
- **Shell/Bowl**: a parametric approximation tuned by ear (bell-like ratios). Document the approximation.
- **Tube**: air column (waveguide or modal).
- **Spring**: dispersive "chirpy" mode series.
- **Box**: low cavity (Helmholtz) mode coupled to plate modes on its faces.

Damping per mode: `1/T60 ∝ material loss × (1 + highFreqDamping × f)` so high modes die faster. Cap the mode count per part (e.g. 64–128, below Nyquist) and expose a quality setting.

### 3.4 DSP (audio thread)
- **Modal bank:** one resonator per mode (stable 2-pole, or a complex one-pole rotation). SIMD-friendly structure-of-arrays. This is where most of the CPU goes.
- **Excitation:**
  - Mallet: a **Hertzian contact** force (force ∝ compression^α, with α and stiffness from Hardness) between a small mass and the surface, so contact time depends on velocity and hardness like a real stick
  - Brush/Scrape: filtered noise force with a moving position
  - Drop: a ball under gravity that bounces on the surface using the same contact model
  - Rub: a stick-slip friction model
- **Linear joints:** spring and damper forces between the displacements of two parts' points, computed from the modal state each sample.
- **Contact joints:** a one-sided penalty spring, active only when the displacement difference exceeds the gap. This makes the sim nonlinear: use energy-stable discretisation and offer 2× oversampling for pads with contacts.
- **Particles:** small masses with gravity that collide with surface displacement at their positions. Their positions drift slowly; cap the count.
- **Mics:** contact mic = surface velocity at its point. Air mic = weighted sum of parts' radiating velocity, with distance delay and gentle high-frequency loss. Stereo comes from mic pans.
- **Retriggering:** hitting a ringing pad **adds energy to the running state**; it does not reset it. This matters for realism. Choke groups damp the target pad quickly.
- **Shared stand (kit coupling):** an 8×8 matrix of sympathetic coupling. A pad's low-mode motion feeds a small force into the linked pads. Keep it weak and stable (clamp total gain).
- **Real-time rules:** no allocation, locks, file I/O or logging in `processBlock`. Preallocate everything. Flush denormals (`juce::ScopedNoDenormals`). Clamp anything that can explode, and recover cleanly if a sim goes non-finite (reset that pad and flag it in the UI).

### 3.5 Sequencer and MIDI
- 16 steps × 8 pads, per-step velocity, swing, synced to host transport (free-running in Standalone).
- MIDI input triggers pads. Default note map: GM-style starting at note 36. Velocity → Force.
- Sequencer state is plugin state; step on/off isn't automatable.

## 4. UI

Use the **workbench visual theme** everywhere (concept "A"), including the Kit view. Don't use the periodic-table styling from the earlier concept C; the Kit view keeps C's *layout and features*, not its look.

### 4.1 Theme tokens
| Token | Value | Use |
|---|---|---|
| `bg` | `#1B1C1A` | window background |
| `panel` | `#20211E` | side panels, bars |
| `control` | `#2A2B27` | buttons, chips, inputs |
| `border` | `#34352F` / `#3A3B35` | dividers / control outlines |
| `text` | `#ECEBE6` | primary text |
| `textMuted` | `#A6A59D` | labels (4.5:1 on panel) |
| `accent` | `#FF7A1A` | selection, active state, knob arcs, strike point |
| `mat` | `#23503D` | bench cutting mat, 20 px fine + 100 px major grid lines in white at 4% / 10% |
| `ok` | `#6FBF73` | "modes solved", meters |
| `modeBar` | `#D4B06A` | overtone bars, waveform |

Fonts: IBM Plex Sans for UI text; IBM Plex Mono for small caps-style section labels (letter-spaced ~0.12 em), values and readouts. Corner radius 4–8 px. Minimum hit target 44 px. The UI should be **resizable** (scale everything from a base size of 1280×800).

### 4.2 Kit view (default)
- **Top bar:** logo, kit preset selector, a **Kit / Bench** view switch, transport (play, BPM, swing), CPU and voice readout, output meter.
- **Pad grid (4×2):** each pad shows its number, a small line glyph of its main object, its recipe in mono text (e.g. `Bronze Shell + Rice × Drop`) and a name. The selected pad gets an accent fill or ring. Clicking a pad selects and plays it. **Double-click opens it in the Bench view.**
- **Recipe panel** (selected pad): object chips, material picker (swatches in the bench style, with a two-material blend slider), interaction verb chips (Strike, Drop, Scrape, Rub, Roll, Shake, Brush, Bounce), 4 key sliders that change with the object (e.g. Size, Wall, Rice amount, Drop height), and an "Open on bench" button.
- **Shared stand matrix:** 8×8 dots whose size shows coupling strength. Drag a dot to change it.
- **Sequencer strip:** 8 rows × 16 steps with velocity shown as step opacity or height, rows labelled by pad.
- **Macros strip:** the selected pad's knobs (Force, Hardness, Spread, Choke, Rattle, Room).

### 4.3 Bench view (detail)
- **Left: parts bin.** Sections are Resonators, Joints, Loose, Exciters, as two-column draggable tiles.
- **Centre: cutting-mat canvas.** Pan/zoom. Parts drawn top-down in a tactile illustrative style (skin membrane with lugs, bronze plate with rubber mounts, spring coil). Selection shows a dashed accent outline with corner handles. Labels sit in small dark tags in mono text.
  - Tool rail: Select, Join, Damping finger, Place mic, Tap test
  - **Tap test:** click any point on a part to strike it there
  - **X-ray toggle:** draws nodal lines and a displacement heat-map of the instrument as it rings (animated at UI frame rate from a decimated snapshot of the modal state, passed lock-free)
  - Strike point shown as an accent dot with ripple rings; air mics shown with pickup cones
  - Status chip: "modes solved · N ms"
- **Right: inspector** for the selected item. For parts: material swatches, shape and damping sliders, temperature, and an overtone list (frequency, ratio, relative level bar). For joints: stiffness, gap and friction. For exciters: hardness, mass and spread. For mics: type, level and pan.
- **Bottom strip:** last-hit waveform, then the macro knobs (same as the Kit view).
- A **Build / Play / X-ray** segmented control in the top bar.

### 4.4 UI engineering
- Bind controls to `ValueTree`/APVTS; never read audio-thread state directly. Pass meters and snapshots via lock-free FIFOs.
- Repaint the bench canvas efficiently (cache static layers; redraw only animated overlays at ~30–60 fps).
- Build all visuals in JUCE `Graphics` (paths, gradients); no bitmaps required for v1.

## 5. Materials (starting table)

Approximate real values. Tune them by ear, and keep them in one data file so they're easy to edit.

| Material | Density kg/m³ | Young's modulus GPa | Loss factor η | Notes |
|---|---|---|---|---|
| Steel | 7850 | 200 | 0.0002 | |
| Bronze (bell) | 8800 | 100 | 0.0003 | long ring |
| Aluminium | 2700 | 69 | 0.0003 | |
| Brass | 8500 | 100 | 0.0005 | |
| Glass | 2500 | 70 | 0.001 | |
| Ceramic | 2400 | 100 | 0.002 | |
| Slate | 2800 | 50 | 0.006 | dull, short |
| Ice | 917 | 9 | 0.002 | temperature-sensitive |
| Oak | 750 | 12 | 0.012 | |
| Spruce | 450 | 11 | 0.008 | |
| Bamboo | 600 | 18 | 0.010 | |
| Rubber | 1200 | 0.05 | 0.15 | heavy damping |
| Hide / Mylar | (membranes use tension + areal density) | | 0.02 / 0.01 | |
| Paper | 800 | 4 | 0.05 | |
| **Hyperglass** (impossible) | 100 | 150 | 0.000002 | rings ~a minute |
| **Reverse** (impossible) | — | — | negative, ramped | swells instead of decays; must have a limiter and max length |

Temperature (optional per part): shifts stiffness slightly; Ice detunes noticeably. Off by default.

## 6. Milestones

Work in this order. Each milestone ends with a working build, passing tests, pluginval passing (from M1), and rendered audio demos from `render_cli`.

- **M0 – Skeleton:** CMake + FetchContent JUCE, plugin builds as VST3/AU/Standalone, empty themed editor, Catch2 wired up, pluginval script, CI-ready build commands documented in `docs/BUILD.md`.
- **M1 – One pad, one drum:** circular membrane with analytic modes + Hertzian mallet + one contact mic. MIDI note plays it; velocity → force; retrigger adds energy. Includes the `render_cli` tool.
- **M2 – Parts and materials:** Bar, Plate, Shell, Box, Spring; material table; air mics; Size/Tune; the background compile with lock-free swap and crossfade.
- **M3 – Joints and loose objects:** Bolt, Rubber, Contact (gap/buzz), snare Wire; Beads/Rice; Drop, Scrape, Brush exciters; oversampling option; stability safeguards.
- **M4 – Kit view:** 8 pads, recipe panel, macros, choke groups, sequencer, shared-stand matrix, preset save/load, 6–8 factory kits.
- **M5 – Bench view:** parts bin, canvas with drag/join/select/pan/zoom, inspector, tap test, undo/redo.
- **M6 – X-ray and polish:** nodal lines and heat-map overlay, performance (SIMD, mode-count quality setting, CPU targets), resizable UI, accessibility pass.

**CPU target:** a full 8-pad kit with typical recipes under ~15% of one core at 48 kHz / 128-sample buffer on a recent laptop. Measure and report it.

## 7. How to work on this project

- **You can't listen to audio**, so verify sound with the tools: render WAVs with `render_cli`, then check spectra (peak frequencies against expected mode ratios), decay times (T60 against the material table), peak level / clipping, and stability (no NaN/inf, energy doesn't grow without input). Write those checks as tests where possible. Hand renders to the user for listening at the end of each milestone.
- Unit-test the physics: membrane mode ratios match Bessel zeros, bar ratios match Euler–Bernoulli, a freely ringing object loses energy monotonically, contact joints stay bounded under extreme velocity.
- Keep the audio thread real-time safe (see 3.4). If you're unsure whether something allocates, assume it does and move it.
- Prefer small, reviewable commits per feature. Keep `docs/DECISIONS.md` as a short log of choices and why.
- Ask the user before: changing the theme, adding third-party dependencies beyond JUCE/Catch2/pluginval, choosing a licence, or changing the Kit/Bench split.

## 8. Open questions for the user

Don't block on these: make a sensible default, note it in `docs/DECISIONS.md`, and move on.

1. Which OS(es) and DAWs will you test in first? (Affects AU priority and CI.)
2. Final product name?
3. Is the 8-pad count fixed, or should kits allow 4–16 pads?
4. Should the sequencer be in v1, or should the first release be MIDI-only?
5. Which "impossible" materials and fatigue/cracking ideas matter most for v1?

## 9a. Treatments (added by Jack, 2026-10-28)

Four per-pad controls that change the object itself, implemented in the compile step as local
stiffness/mass/loss changes re-solved into new modes (works for every part type):

1. **Age**: realistic, somewhat exaggerated ageing per material (rust, patina, crazing, checking,
   stretch…), with a dry ↔ damp Climate control.
2. **Deform**: dents/bumps with Intensity, Size and Density.
3. **Twist**: Amount (±), Pivot, Focus, Wrinkle.
4. **Puncture**: Holes, Size, Placement, Scatter, Ragged.

Design and per-material rules: `docs/TREATMENTS.md`. Status and choices: `docs/DECISIONS.md`.
Target platform: macOS first (AU + VST3).

## 9. First task

Start with **M0**, then **M1**:
1. Set up the CMake project with a pinned JUCE release, the folder layout above, Catch2, and a pluginval script.
2. Create the themed empty editor (Kit/Bench switch in the top bar, both views as placeholders) using the tokens in 4.1.
3. Implement the membrane + mallet + contact mic voice and `render_cli`, with tests for mode ratios, decay and stability.
4. Render 3–4 short demo hits (soft/hard mallet, centre/edge strike) and report what you verified.
