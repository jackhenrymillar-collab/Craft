# Building Foundry

Requirements: CMake ≥ 3.24, a C++20 compiler, Ninja (recommended), git (FetchContent pulls JUCE 9.0.4 and Catch2 v3.16.1).

## macOS (primary target: AU + VST3 + Standalone)

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # or -G Xcode
cmake --build build
ctest --test-dir build --output-on-failure
scripts/pluginval.sh build          # pluginval (strictness 10) on VST3 + AU, plus auval
scripts/render_demos.sh build renders
```

Builds are universal (arm64 + x86_64) by default, minimum macOS 11. Override with
`-DCMAKE_OSX_ARCHITECTURES=arm64` for faster local builds.

Artefacts: `build/source/plugin/Foundry_artefacts/Release/{AU,VST3,Standalone}`.
They aren't copied into the system plug-in folders automatically; `scripts/pluginval.sh`
copies the AU into `~/Library/Audio/Plug-Ins/Components` because AU hosts need it there.
To try the VST3 in a DAW, copy `Foundry.vst3` into `~/Library/Audio/Plug-Ins/VST3`.

## Linux (development / CI)

```sh
sudo apt install libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev \
     libxcomposite-dev libxext-dev libxi-dev libfreetype-dev libfontconfig1-dev libgl1-mesa-dev xvfb
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
xvfb-run -a scripts/pluginval.sh build
```

## Options

| Option | Default | |
|---|---|---|
| `FOUNDRY_BUILD_PLUGIN` | ON | VST3/AU/Standalone |
| `FOUNDRY_BUILD_TOOLS` | ON | `render_cli` |
| `FOUNDRY_BUILD_TESTS` | ON | Catch2 suite |
| `FOUNDRY_SANITIZERS` | OFF | ASan + UBSan on the core library and tests |

Core-only build (fast, no JUCE): `cmake -S . -B build-core -DFOUNDRY_BUILD_PLUGIN=OFF -DFOUNDRY_BUILD_TOOLS=OFF`.

Release builds use LTO and are slow to link; for day-to-day work use
`-DCMAKE_BUILD_TYPE=RelWithDebInfo`.

## render_cli

```sh
render_cli --out hit.wav --hardness 0.9 --at 0.8,0.4 --hits 0:0.8 --length 3 [treatment flags]
```

Writes `hit.wav` (24-bit), `hit.png` (spectrum with predicted modes marked + waveform),
`hit.spectrum.csv` and `hit.json` (peak level, clipping, finiteness, whether energy grew while
ringing freely, measured vs predicted T60, measured peaks with their distance in cents from the
nearest predicted mode). Exit code 2 means a stability or clipping check failed. `--help` lists
every flag.

## CI

`.github/workflows/ci.yml` builds and tests on macOS (AU + VST3 + pluginval + auval, renders
the demos as an artefact) and on Linux (VST3 + pluginval under xvfb).
