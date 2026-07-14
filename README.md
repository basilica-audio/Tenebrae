# Tenebrae

*A liturgy of shadows — cascaded high-gain distortion for the heaviest rhythm tone.*

[![CI](https://github.com/metal-up-your-ass/tenebrae/actions/workflows/ci.yml/badge.svg)](https://github.com/metal-up-your-ass/tenebrae/actions/workflows/ci.yml)
[![License: AGPL v3](https://img.shields.io/badge/License-AGPL%20v3-blue.svg)](https://www.gnu.org/licenses/agpl-3.0)

> **Work in progress.** Tenebrae is pre-1.0 and under active development. There are no built binaries or releases yet — building from source is currently the only way to run it. Expect breaking changes until v1.0.0 ships (see [Roadmap](#roadmap)).

<!-- ==BEGIN BODY== (plugin engineer: replace this block with What it is / Features / Signal flow / Roadmap) -->
## What it is

Tenebrae is a high-gain rhythm-guitar distortion built on JUCE 8, aimed squarely at the core "chug" tone of symphonic metal: a tightening high-pass ahead of a cascade of three oversampled waveshaper stages, each progressively tighter and darker than the last, followed by a passive-style 3-band tone stack for shaping the result. It has no cabinet simulation of its own - pair it with a cab sim/IR loader for the final voicing.

## Features

- **Tight** - high-pass pre-emphasis, 20 Hz - 300 Hz (default 90 Hz), removes low end before the gain cascade so palm mutes stay percussive instead of farting out
- **Gain** - 0 - 40 dB of pre-gain into the 3-stage waveshaper cascade
- **Voicing** - Tight / Loose switch between two complete cascade voicings (asymmetry + interstage filtering): Tight is the tighter, more modern-leaning default; Loose is a softer-driven, wider-band, more vintage-leaning alternative
- **Bright** - fixed pre-cascade high-shelf switch, modelled on a high-gain amp channel's bright switch/a brighter cabinet's presence peak
- **3-stage cascade** - each stage is gain -> asymmetric tanh clip -> a fixed interstage high/low-pass pair; the three stages use progressively tighter, more asymmetric voicing so the cascade converges onto a focused chug band instead of an ever-fizzier mess. Runs inside 8x oversampling to keep aliasing from all three stages out of the audible band
- **Bass / Mid / Treble** - passive-style tone stack (low shelf @ 150 Hz / peak @ 650 Hz / high shelf @ 3.5 kHz), +/-15 dB per band
- **Tone Voice** - Flat / Scoop / Boost one-switch dB tilt added on top of the live Bass/Mid/Treble bands, for quickly auditioning a canned high-gain-rhythm tone shape
- **Level** - output trim, -24 dB to +24 dB
- **Mix** - dry/wet, with the dry path delay-compensated against the oversampling latency so Mix at 0% is a sample-accurate passthrough
- Full state save/recall via `AudioProcessorValueTreeState`

See [`docs/manual.md`](docs/manual.md) for the full user manual, including a musical description of every parameter and usage tips.

## Signal flow

```
Input --> Tight (HPF, 20-300 Hz) --> Bright (switch) --> Gain (0-40 dB) --> [8x oversampled]
              Cascade stage 1 -> Cascade stage 2 -> Cascade stage 3   (Voicing: Tight/Loose)
                                                              |
   Output <-- Mix <-- Level <-- Treble <-- Mid <-- Bass <----+   (tilted by Tone Voice)
     ^
     |
delay-compensated dry path
```

See [`docs/architecture.md`](docs/architecture.md) for the full breakdown, including the per-stage cascade voicing (both Tight and Loose tables), oversampling/latency-compensation strategy, and parameter smoothing.

## Roadmap

| Milestone | Description | Status |
|---|---|---|
| M1 | DSP completion - Voicing/Bright/Tone Voice, refined tone stack, broadened test coverage (sample-rate sweeps, bus configs, long-run stability) | Done |
| M2 | Preset system + factory presets | Planned |
| M3 | Custom GUI + accessibility pass | Planned |
| M4 | Release engineering - signing, notarization, installers, v1.0.0 | Planned |
<!-- ==END BODY== -->

## Installation

No pre-built binaries are published yet (see the work-in-progress notice above). Once releases begin, installation will follow the standard plugin locations:

**macOS**

| Format | Path |
|---|---|
| AU (Component) | `~/Library/Audio/Plug-Ins/Components/` |
| VST3 | `~/Library/Audio/Plug-Ins/VST3/` |

If Logic Pro doesn't pick up the plugin after installing, force a rescan by resetting the AU cache:

```sh
killall -9 AudioComponentRegistrar
auval -a
```

**Windows**

| Format | Path |
|---|---|
| VST3 | `C:\Program Files\Common Files\VST3\` |

## Building from source

Requires JUCE 8.0.14, C++20, and CMake ≥ 3.24. See [`docs/building.md`](docs/building.md) for full prerequisites and step-by-step build/test commands for macOS and Windows.

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## License

Tenebrae is licensed under the [GNU Affero General Public License v3.0](LICENSE) (AGPLv3).

This project uses [JUCE](https://juce.com) 8, whose open-source tier is licensed under AGPLv3 (as of JUCE 8; JUCE 7 and earlier used GPLv3), which is why this project is AGPLv3 rather than GPLv3. See [`docs/adr/0002-agplv3-licensing.md`](docs/adr/0002-agplv3-licensing.md) for the full reasoning.

VST is a registered trademark of Steinberg Media Technologies GmbH.

Tenebrae is an independent open-source project and is not affiliated with, endorsed by, or sponsored by any plugin manufacturer.
