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

## 2026-10-29 · M2: objects, strikers, air mic

- **Seven part types** behind one `ModalBasis` interface (mass-normalised modes, shapes and
  gradients, quadrature, placement helpers, radiation). Each has three shape controls and one
  or two mounts (`source/model/PartCatalogue.cpp` holds names, ranges and defaults):
  - **Membrane** (unchanged from M1; Diameter, Tension, Muffle).
  - **Bar**: Euler–Bernoulli beam functions, Ritz-solved with a parabolic undercut so the
    2nd mode moves from 2.76× toward 4× (marimba tuning) as Undercut goes 0 → 1. Torsional
    modes too. Mounts: on cords at the nodes (0.224 L) or clamped at one end.
  - **Plate**: thin Kirchhoff plate. Hung = free edges (Warburton's approximation, within
    ~7 % of published values); Framed = simply supported (exact).
  - **Shell**: a bowl that morphs into a bell (Profile). Ring-mode ratios for the bowl, the
    classic hum/prime/tierce/quint/nominal family for the bell; each mode is a cos/sin pair
    with a slight split. This is a tuned approximation, not a shell solver.
  - **Tube**: a hollow free–free beam (chime) plus the air column inside it (open or capped,
    end corrections included), coupled by the Air control.
  - **Spring**: a dispersive mode series f ∝ k·√(1 + χk²) (Chirp sets χ) with coil friction.
  - **Box**: a simply-supported face coupled to the Helmholtz mode of the cavity through its
    port (cajón-like). Port = 0 closes it.
- **Material choice follows the part:** membranes take Mylar / Hide / Paper; every other part
  takes the solids (Paper included). Changing the part resets material, mount and shape knobs
  to that part's defaults.
- **Damping for solid parts** adds two things the membrane didn't need: sound radiation
  (σ = ρω²g²/8πc, from each mode's net volume and dipole moment) and a mount loss (cords,
  clamps, frames, a cushion under a bowl). Without them a hung steel plate rang for minutes.
  Impossible materials skip the mount loss so Hyperglass still rings for ages.
- **Strikers:** Mallet (as M1), **Hammer** (heavy, 350 g, rubber → steel face), **Hand**
  (palm → fingertip: mass and contact area shrink with Hardness) and **Drop** (a 20 g ball
  under gravity that bounces until it settles; Force/velocity set the drop height). All use the
  same implicit Hertz/Hunt–Crossley contact. Heavy strikers are slowed (speed ∝ mass^-0.45) so
  a hammer isn't 20 dB louder than a stick; a heavy hammer on a small bell still rides the
  hum mode (1.5 ms contact) and sounds dark, which is physically right.
- **Air mic** (Air mix, Mic distance): the radiated sound from each mode's volume and dipole
  terms, delayed by d/c, with gentle air loss (low-pass falling with distance) and 1/d level.
  The membrane's radiation isn't added to its damping (its M1 tuning already includes it).
- **Level calibration:** every compiled part is played once with a reference mallet hit and its
  contact and air gains are set so a medium hit has the same loudness on every part, with a
  ceiling from a hard hit (the air mic may only be pulled down 6 dB by that ceiling, otherwise
  it came out 7–14 dB quieter than the contact mic). Default membrane level is unchanged from M1.
- **Treatments on solid parts:** dents stiffen a solid (shallow-shell effect) instead of
  slackening it; holes and features are placed with each part's own geometry.
- **Brush and Scrape are deferred to M3** with the other continuous exciters.

## Open questions (defaults taken, see brief §8)

1. OS/DAW: macOS first (answered). Which DAWs to test first is still open; default: Logic (AU)
   and Ableton/Reaper (VST3).
2. Product name: "Foundry" kept as the working name.
3. Pad count: 8, as in the brief.
4. Sequencer in v1: assumed yes (M4).
5. Impossible materials: Hyperglass is in the table; Reverse is deferred until the limiter exists.
