# Decisions log

Short log of choices and why. Newest at the bottom.

## 2026-10-28 · M0/M1

- **Platform: macOS first** (Jack). AU + VST3 + Standalone are the priority targets; universal
  binaries, macOS 11+. Linux builds are kept working for CI and headless rendering.
- **JUCE 9.0.4, Catch2 v3.16.1, pluginval v1.0.4**, pinned to release tags (checked upstream on
  2026-10-28). No other third-party dependencies.
- **Licence: not decided.** JUCE is dual-licensed (AGPLv3 or commercial). This has to be Jack's
  call before anything is distributed, so the repo has no LICENSE file yet. The IBM Plex fonts
  are SIL OFL; their licence is in `resources/fonts/OFL.txt`.
- **Core is JUCE-free.** `foundry_core` (model, compile, dsp) is plain C++20 so physics tests and
  `render_cli` analysis don't need the plugin stack.
- **Bessel functions are our own** (periodic trapezoid integral, exponentially accurate) because
  libc++ on macOS has no `std::cyl_bessel_j`. Zeros and radial tables are computed once and cached.
- **Modes are mass-normalised**, so modal stiffness is diag(ω²); every part type implements the
  same `ModalBasis` interface (shapes, gradients, quadrature, stiffness kernel).
- **Exact low modes + statistical high band.** A membrane's mode density grows with frequency
  (Weyl), so 96 exact modes only reach ~1.6 kHz on the default tom. Above that, 96 log-spaced
  "band" modes each stand in for the real modes in their band: √N coupling at the strike point
  (correct local compliance and band energy) and typical coupling at the mic (incoherent sum).
  Treatments reach the band statistically (mean/RMS detune and loss ratio of the upper exact modes).
- **Damping:** σ = π·η·f·(1 + h·f/1 kHz) (loss factor η from the material table, h = extra
  high-frequency loss). The fundamental's T60 matches 6.91/σ to within 0.1 % in renders.
- **Discretisation:** each mode uses the exact zero-order-hold solution of the damped oscillator
  (unconditionally stable, exact decay and frequency). The Hertz/Hunt–Crossley mallet force is
  solved implicitly against the modes each sample (safeguarded Newton), so very stiff tips are stable.
- **Mallet tip is a patch, not a point:** shapes are averaged over the tip radius (20 mm felt →
  3 mm hard tip). On a membrane the head's own give dominates contact time (~5–7 ms on the
  default tom, consistent with the 2πT/ln(a/b) local stiffness), so Hardness mostly shapes the
  force onset and brightness rather than contact time.
- **Defaults:** 14" Mylar head, T = 2000 N/m (f₀ ≈ 189 Hz without air loading), 12 g effective
  stick mass. Contact mic at r = 0.82, velocity pickup.
- **Pulled forward from M2:** the background compile thread, lock-free swap and 20 ms crossfade,
  because the treatment knobs reshape the modes and need it. Ringing state is carried across a
  recompile (projected through the untreated basis), so turning a knob while a hit rings doesn't
  restart or click.
- **Treatments (Jack's request): Age, Deform, Twist, Puncture.** Implemented as local changes of
  stiffness/tension, mass and loss, projected onto the modes and re-solved as a generalised
  eigenproblem (K + δK) x = λ (M + δM) x. This splits paired modes, shifts pitch, mixes mode
  shapes and adds loss in a physically consistent way and works for every future part type.
  See `docs/TREATMENTS.md`. Random layouts come from a per-pad seed so turning a knob changes
  intensity, never where things are.
- **Age is per material** with a dry↔damp "Climate" control, because materials age differently
  in different conditions (steel rusts when damp, patinates when dry; hide tightens when dry,
  goes slack when damp…). Rules live in `source/model/Materials.cpp`.
- **Grit** (rust flakes, torn hole edges, wrinkles) is an output-side crackle driven by surface
  motion for now; it becomes real contact rattle with the M3 contact joints.
- **UI:** workbench theme tokens exactly as the brief; one 1280×800 layout scaled as a whole
  (fixed aspect, 50–200 %). The M1 Kit view has the 4×2 grid (pad 1 live), recipe panel with
  head material chips, a top-down head view (draws dents, holes, cracks, pits, twist; click to
  tap-test), a Treatments strip and the macro knobs. Bench view is a mat placeholder with the
  same tap-testable head.
- **Parameters** are prefixed `p1_` so pads 2–8 slot in at M4 without breaking saved sessions.
  Head material is a state-tree property (structural, not automatable). Pad 1 plays on MIDI note 36.
- **Output safety:** per-pad non-finite/runaway detection resets the pad and flags it in the
  status chip; the plugin output has a soft clip above −1 dBFS.

## Open questions (defaults taken, see brief §8)

1. OS/DAW: macOS first (answered). Which DAWs to test first is still open; default: Logic (AU)
   and Ableton/Reaper (VST3).
2. Product name: "Foundry" kept as the working name.
3. Pad count: 8, as in the brief.
4. Sequencer in v1: assumed yes (M4).
5. Impossible materials: Hyperglass is in the table; Reverse is deferred until the limiter exists.
