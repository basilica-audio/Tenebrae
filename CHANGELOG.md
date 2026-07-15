# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- `clampBelowNyquist()` (`TenebraeEngine.cpp`, duplicated in `CascadeStage.cpp`) and `ToneStack::clampCombinedGainDb()` relied solely on `juce::jlimit()`, which is not NaN-safe (both of its internal comparisons evaluate false for NaN, so a NaN input previously fell through unclamped). A NaN Tight-frequency or Bass/Mid/Treble gain reaching these from host automation could produce NaN filter coefficients that poison a filter's delay-line state (persistently on arm64, where JUCE's snap-to-zero denormal cleanup is a no-op, vs. self-healing after one block on x86_64). Both helpers now replace a NaN input with a safe default before clamping. (#14)

## [0.1.0] - 2026-07-14

### Added

- Project bootstrap: README, license, contributing guide, architecture and build docs, ADRs, and CI workflow.
- DSP core: initial working Tenebrae signal path (Tight HPF, Gain, 3-stage oversampled waveshaper cascade, 3-band tone stack, Level, delay-compensated dry/wet Mix) with unit tests.
- **Voicing** parameter (Tight/Loose): switches the fixed cascade drive/asymmetry/interstage-filter constants between the original "Tight" cascade and a new, softer-driven, wider-band "Loose" alternative. Both cascade triplets are fully preallocated so switching is allocation-free.
- **Bright** parameter: fixed pre-cascade high-shelf switch (+5 dB @ 3.5 kHz), modelled on a high-gain amp channel's bright switch, feeding a brighter signal into the cascade's nonlinearity rather than just re-EQing the already-clipped output.
- **Tone Voice** parameter (Flat/Scoop/Boost): a fixed dB tilt added on top of the live Bass/Mid/Treble tone-stack bands, for one-switch access to canned high-gain-rhythm tone shapes (a "smiley" scoop curve and a mid-forward boost curve) without overriding the individual band knobs.
- `docs/manual.md`: full user manual (what the plugin is, where it sits in a symphonic-metal chain, signal flow, complete parameter reference, usage tips).

### Changed

- Tone stack corner frequencies refined against typical high-gain rhythm voicings: Mid peak moved from 800 Hz to 650 Hz with a narrower Q (0.8 -> 1.1) for a more surgical scoop, and the Treble shelf moved from 3000 Hz to 3500 Hz to sit at a more typical amp "presence" corner.
- Broadened the Catch2 suite from 26 to 36 test cases: sample-rate sweeps (44.1-192 kHz), mono/stereo bus-layout coverage, a longer-running (400-block) NaN/Inf automation soak test, exhaustive Voicing/Bright/Tone Voice combination coverage, and dedicated tests for every new parameter (state round-trip, defaults/ranges, DSP-level effect).

### Fixed

- `TenebraeEngine::outputLevel` (the Level gain stage) was never primed with a starting gain in `prepare()`; since `juce::dsp::Gain` default-constructs its internal smoothed value to linear zero (silence) rather than unity, any `prepare()` call not immediately preceded by a fresh `setLevelDb()` call produced a permanently silent wet path. This never reached the shipped plugin (`PluginProcessor::prepareToPlay()` always seeds Level from the host/session state first) but was a real trap for anything exercising `TenebraeEngine` directly, found via the broadened mono-bus test coverage above. Fixed by re-priming `outputLevel` from a new `lastLevelDb` member on every `prepare()`, matching the existing `lastGainDb`/`lastTightHz` pattern.
