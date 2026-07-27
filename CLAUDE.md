# Tenebrae — high-gain rhythm distortion (guitar)

Per-repo working memory for Claude Code sessions on this plugin. Part of the **Basilica Audio** plugin suite — sacred-architecture DSP for heavy music (`github.com/basilica-audio`).

## What this is
Tenebrae is the "high-gain rhythm distortion (guitar)" member of the suite. AU / VST3 / Standalone, JUCE 8.

## Status (v0.3.0 — stateful triode engine, power amp, gate v2)
Core DSP complete, **107 Catch2 tests green** locally (up from 73 in v0.2.0). v0.3.0 added a second, selectable engine (`Engine`: Classic/Triode) built from solved Dempwolf-Zoelzer 12AX7 stage equations with dynamic bias shift, cathode-bypass frequency-dependent drive and Miller interstage filtering; a negative-feedback `PowerAmp` block (Resonance/Sag, Presence moved into the feedback return path when engaged, loop gain bounded at 0.5); ADAA1 on every new shaper with a `Quality` switch (Eco 2x / Standard 4x / HQ 8x); and a strict-superset gate v2 (pre-distortion key tap, hysteresis, range floor, program-dependent release). All ten new parameters default neutral and a v0.2.0 session renders **byte-identically** — asserted in-process by T-S1/T-PR2, since a committed float render can never be byte-compared across the macOS and Windows CI legs. GUI is a functional v0.1/v0.2 slider/combo-box/toggle editor covering every parameter plus a preset bar (custom LookAndFeel is still roadmap M3). No signing yet (roadmap M4). v0.2.0 shipped a research-derived deep-dive rework against the high-gain cascaded rhythm-guitar reference class (see `docs/design-brief.md`/`docs/research-notes.md` for the sourcing - the only two files in this repo that name specific reference products, as citations): a new Presence control, a new Gate (on by default), and the tone stack's Treble corner raised from 3.5 kHz to 5 kHz. Also shipped: this repo's implementation of the suite-wide M2 preset system (`src/presets/`, pattern piloted in `basilica-audio/nave` - see `docs/preset-system-notes.md`), 8 factory presets (`docs/presets.md`), and a German frame-string localisation. Open work is tracked in this repo's GitHub **milestones/issues**.

## DSP
Signal chain: input -> Tight HPF (20-300 Hz, default 90 Hz, 2nd-order Butterworth IIR) -> Bright (switch: fixed +5 dB high-shelf @3.5 kHz pre-cascade, modelled on an amp bright switch) -> Gain (0-40 dB pre-gain) -> 8x oversampled 3-stage waveshaper cascade, selectable via Voicing (Tight/Loose, two complete preallocated per-stage tables of fixed drive/asymmetry/interstage HP-LP corners - see docs/architecture.md) -> passive-style 3-band ToneStack (Bass low-shelf @150 Hz, Mid peak @650 Hz, Treble high-shelf @5 kHz [v0.2.0: raised from 3.5 kHz], all +/-15 dB) with a Tone Voice switch (Flat/Scoop/Boost - fixed dB tilt added on top of the live band gains) -> Presence (v0.2.0: post-cascade high-shelf @2.4 kHz, +/-12 dB, skipped entirely at its 0 dB default for a true passthrough) -> Gate (v0.2.0: fixed attack/hold/release expander/gate, Threshold/Attack/Hold/Release + on/off, **on by default**, structural bypass when off) -> Level (-24..+24 dB) -> DryWetMixer (0-100%, delay-compensated). Oversampling is 8x (2^3) half-band polyphase IIR with useIntegerLatency=true; TenebraeEngine::getLatencySamples() is reported via setLatencySamples() in prepareToPlay(), and the dry path is time-aligned via DryWetMixer::setWetLatency(), primed with setWetMixProportion() before reset() per the documented JUCE 8.0.14 DryWetMixer gotcha.

M1 also fixed a real pre-existing bug found via broadened test coverage: `TenebraeEngine::outputLevel` (a `juce::dsp::Gain`) was never primed with a starting gain in `prepare()`, and JUCE default-constructs a `Gain`'s internal `SmoothedValue` to *linear zero* (silence), not unity - any `prepare()` call not immediately preceded by `setLevelDb()` produced a permanently silent wet path. Harmless in the shipped plugin (`PluginProcessor::prepareToPlay()` always seeds Level from APVTS first) but a real trap for anything exercising `TenebraeEngine` directly; fixed with a `lastLevelDb` re-priming member matching the existing `lastGainDb`/`lastTightHz` pattern.

## Build & test
```sh
export CPM_SOURCE_CACHE="$HOME/.cache/CPM"      # shared JUCE 8.0.14 + Catch2 cache
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target Tests Tenebrae_Standalone --parallel 4
ctest --test-dir build --output-on-failure
```
Release/universal + pluginval + auval run in CI, not locally.

**Known org-level issue (2026-07-16):** the macOS release job fails at "Import signing certificate" (broken secret, fix pending) - Windows release assets still succeed; do not retry-loop the macOS job until the secret is fixed.

## Conventions & guardrails
- JUCE 8.0.14 via CPM · C++20 · AGPLv3 · Pamplejuce `SharedCode` pattern · manufacturer `Yvsv`, plugin code `Tnbr`, `com.yvesvogl.tenebrae`.
- **Real-time safety:** no alloc/lock/file-IO/logging on the audio thread; allocate in `prepareToPlay`; `reset()` clears all state; `ScopedNoDenormals`; smoothed params; report latency via `setLatencySamples` where the chain adds any.
- **DryWetMixer gotcha (JUCE 8.0.14):** prime `setWetMixProportion(mix)` before `reset()` in `prepare()` (else it ramps from 100% wet). See sibling `overture`.
- **`main` is protected** — no direct commits; feature branch + PR, green CI required (Conventional Commits). New DSP needs tests (null/reference, NaN/Inf sweep, state round-trip, latency).

## Roadmap
GitHub milestones (M1 DSP & tests · M2 presets/state · M3 GUI & a11y · M4 release/signing/v1.0.0) + issues. Read with `gh issue list --repo basilica-audio/tenebrae`.

## Suite context
Style references: sibling `basilica-audio/overture` and `basilica-audio/crypta`. The suite: overture, tenebrae, nave, silentium, requiem, seraph, aureate, firmament, triptych, apotheosis, crypta.
