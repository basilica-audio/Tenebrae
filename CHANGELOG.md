# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.1] - 2026-07-16

### Changed

- Housekeeping: new icon motif with canonical squircle cutout embedded into the plugin binary (`ICON_BIG`) and README/manual, org link sweep, heavy-music copy reframe, README pointed at GitHub Releases, and the signed tag-triggered release CI workflow added.

### Fixed

- `clampBelowNyquist()` (`TenebraeEngine.cpp`, duplicated in `CascadeStage.cpp`) and `ToneStack::clampCombinedGainDb()` relied solely on `juce::jlimit()`, which is not NaN-safe (both of its internal comparisons evaluate false for NaN, so a NaN input previously fell through unclamped). A NaN Tight-frequency or Bass/Mid/Treble gain reaching these from host automation could produce NaN filter coefficients that poison a filter's delay-line state (persistently on arm64, where JUCE's snap-to-zero denormal cleanup is a no-op, vs. self-healing after one block on x86_64). Both helpers now replace a NaN input with a safe default before clamping. (#14)
- `CascadeStage::driveGain`, `TenebraeEngine::preGain`, and `TenebraeEngine::outputLevel` (all `juce::dsp::Gain<float>`) called JUCE's own `Gain::process()`, whose multichannel branch `alloca()`s a scratch buffer sized to the block on every call, with no upper bound or heap fallback - `driveGain` runs on the 8x-oversampled block, making it the single largest stack allocation in the whole signal chain. All three now go through a new `RealtimeGain::process()` helper (`src/dsp/RealtimeGain.h`) that replicates JUCE's own per-sample math into a caller-owned buffer sized once in `prepare()`, eliminating the audio-thread `alloca()`. (#12)
- `TenebraeEngine::process()` never compared the incoming block's sample count against the `maximumBlockSize` declared to `prepare()`. An oversized block (some hosts occasionally hand over one - offline bounce/render, buffer-size renegotiation) went straight into `juce::dsp::Oversampling`'s internal buffer, which is sized to exactly that declared maximum and only bounds-checks with a debug-only `jassert` - silent heap corruption with no exception to catch in a Release build. `process()` now chunks any oversized block down into `prepare()`-sized pieces via a new internal `processChunk()` rather than truncating it, so no more than the declared maximum ever reaches the oversampler (or any other `prepare()`-sized internal buffer) in one call. (#13)

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
