# Architecture

## Signal flow

```mermaid
flowchart LR
    IN[Input] --> HPF[Tight<br/>HPF, 20-300 Hz]
    HPF --> BRIGHT[Bright<br/>fixed high-shelf switch]
    BRIGHT --> GAIN[Gain<br/>0-40 dB]
    GAIN --> UP[8x Oversample]
    UP --> S1[Cascade stage 1<br/>gain -> asym clip -> HP/LP]
    S1 --> S2[Cascade stage 2<br/>gain -> asym clip -> HP/LP]
    S2 --> S3[Cascade stage 3<br/>gain -> asym clip -> HP/LP]
    S3 --> DOWN[8x Downsample]
    DOWN --> BASS[Bass<br/>low shelf, 150 Hz]
    BASS --> MID[Mid<br/>peak, 650 Hz]
    MID --> TREBLE[Treble<br/>high shelf, 5 kHz]
    TREBLE --> PRESENCE[Presence<br/>high shelf, 2.4 kHz]
    PRESENCE --> GATE[Gate<br/>expander/gate]
    GATE --> LEVEL[Level<br/>output trim]
    LEVEL --> MIX[Dry/Wet Mix]
    IN -.->|delay-compensated dry path| MIX
    MIX --> OUT[Output]
```

Everything from the Tight HPF through Level is the "wet" path, owned by `TenebraeEngine` (`src/dsp/TenebraeEngine.{h,cpp}`). The dry path is the untouched input signal, delayed to stay time-aligned with the wet path (see [Latency and oversampling](#latency-and-oversampling) below), then blended in at the Mix stage via `juce::dsp::DryWetMixer`.

Two M1 additions sit outside this linear diagram since they are discrete switches rather than points in the signal chain: **Voicing** (Tight/Loose) selects which of two preallocated, independently-prepared sets of the three cascade stages actually runs each block (see [The waveshaper cascade](#the-waveshaper-cascade)), and **Tone Voice** (Flat/Scoop/Boost) adds a fixed dB tilt on top of the live Bass/Mid/Treble values before they reach the tone stack's filter coefficients (see [Parameter smoothing](#parameter-smoothing)).

v0.2.0 (`docs/design-brief.md`) added two modules to the linear diagram, both post-tone-stack and pre-Level: **Presence**, a fixed-2.4kHz post-cascade high-shelf (unlike Bright, which is pre-cascade), and **Gate**, a conventional fixed attack/hold/release expander/gate that gates the fully-voiced wet signal. Also in v0.2.0: the ToneStack's Treble corner moved from 3.5 kHz to 5 kHz (an internal filter-coefficient constant, not a parameter range change - see [The tone stack](#the-tone-stack) below and `docs/design-brief.md` section 3.4 for the full rationale).

### v0.3.0: the Triode engine branch

`Engine` selects between two complete tone-generating cores. Classic is the diagram above, with its
code untouched by this release - which is what makes the migration guarantee provable rather than
argued (`tests/StateTests.cpp`, T-S1). Triode replaces the three `CascadeStage` units with three
`TriodeStage` units and optionally inserts `PowerAmp`, both inside the oversampled region:

```mermaid
flowchart LR
    GAIN[Gain] --> UP[Oversample<br/>2x / 4x / 8x by Quality]
    UP --> T1[TriodeStage 1<br/>coupling HP -> cathode shelf<br/>-> minus bias -> ADAA LUT -> Miller LP]
    T1 --> T2[TriodeStage 2]
    T2 --> T3[TriodeStage 3]
    T3 --> POL[x -1<br/>polarity normalisation]
    POL --> PA[PowerAmp<br/>optional]
    PA --> DOWN[Downsample]
```

Each `TriodeStage` solves the Dempwolf-Zoelzer 12AX7 equations at `prepare()` - first for its own DC
operating point, then for a 2048-point static plate curve with the grid stopper in circuit - and
wraps that curve in the three time-variant effects a memoryless waveshaper cannot produce: a
grid-overshoot bias sidechain (blocking distortion, ~20 ms recovery), an analytic cathode-bypass
shelf plus bloom follower, and a Miller interstage low-pass. See `src/dsp/TriodeStage.h` for the
per-effect derivation and `src/dsp/TriodeCascade.cpp` for the two voicing tables.

Every shaper in this branch runs through first-order ADAA (`src/dsp/ADAAShaper.h`). The LUT flavour
stores its interpolant as cubic Hermite segments and evaluates the antiderivative as the exact
quartic antiderivative of those same segments, so `F1' == S` identically - a consistency requirement,
not an optimisation, because the divided difference amplifies any mismatch by `1/dx`.

`PowerAmp` closes a unit-delay negative-feedback loop around an ADAA `tanh` output transformer, with
Resonance and Presence as cut-only shelves in the *return* path and an envelope-driven sag term on
the transformer's headroom. The loop's small-signal gain is bounded at 0.5 - at least 6 dB of margin
at every frequency up to the oversampled Nyquist - asserted at `prepare()` and gated in CI by T-P5.
See the header for why that bound is a hard requirement of the unit-delay discretisation rather than
a taste decision.

All four oversampling chains (Classic's 8x plus Triode's 2x/4x/8x) are built in `prepare()` and stay
resident, so switching Engine or Quality is a pointer swap plus an in-place coefficient rebuild,
measured at zero additional allocations (T-X1). Because both parameters change the reported latency,
and `setLatencySamples()` is message-thread-only, the engine publishes its current latency into an
atomic and `PluginProcessor::LatencyReporter` re-reports it from a timer.

## Module map

| Directory | Responsibility |
|---|---|
| `src/dsp` | All audio-thread DSP: `AsymmetricClipper` (the stateless tanh nonlinearity), `CascadeStage` (one gain -> clip -> fixed interstage HP/LP unit), `ToneStack` (Bass/Mid/Treble shelving+peak bands, plus the Tone Voice tilt), `Gate` (v0.2.0: fixed attack/hold/release expander/gate - see [The Gate](#the-gate) below), `ADAAShaper` (v0.3.0: shared first-order antialiasing, LUT and closed-form flavours), `TriodeStage`/`TriodeCascade` (v0.3.0: the stateful triode engine), `PowerAmp` (v0.3.0: the negative-feedback power-amp block), and `TenebraeEngine` (the full signal chain wiring them together with the Tight HPF, Bright switch, pre-gain, 8x oversampling, Voicing-selected cascade, the v0.2.0 Presence shelf, the Gate, Level, and dry/wet mix). No allocation, locks, or I/O once `prepare()` has run. Independent of `juce::AudioProcessor`, so it is directly unit-testable (see `tests/EngineTests.cpp`, `tests/AsymmetricClipperTests.cpp`, `tests/ToneStackTests.cpp`, `tests/GateTests.cpp`). |
| `src/params` | Parameter layout and `AudioProcessorValueTreeState` definitions - parameter IDs, ranges, defaults. Single source of truth for what a preset captures. |
| `src/presets` | M2 preset system (`PresetManager`/`PresetBar`/`Localisation`) - suite-wide, copy-paste-portable across all 13 plugins (pilot: `basilica-audio/nave`, see `docs/preset-system-notes.md`). Owns factory/user preset discovery, load/save/import/export, default resolution, dirty-state tracking, and the German i18n frame. |
| `src/PluginProcessor.*` | Host plumbing: APVTS construction, `prepareToPlay`/`processBlock`/`reset`, latency reporting, state save/load, `PresetManager` construction/wiring. Reads APVTS values and pushes them into `TenebraeEngine` every block; does not implement any DSP itself. |
| `src/PluginEditor.*` | A simple, functional v0.1/v0.2 GUI: one rotary slider per continuous parameter (`SliderAttachment`), a combo box per choice parameter (`ComboBoxAttachment`, Voicing/Tone Voice), a toggle button for Bright/Gate on (`ButtonAttachment`), and (v0.2.0) a `PresetBar` strip docked at the top. A custom vector-drawn GUI is a later milestone (M3). |

Dependency direction is one-way: `PluginEditor` -> `params` + `presets` (via attachments/`PresetBar`) and `PluginProcessor` -> `params` + `dsp` + `presets`. `src/dsp` has no upward dependency on the processor or UI, which is what keeps `TenebraeEngine` (and its constituent `CascadeStage`/`ToneStack`/`Gate` units) testable in isolation. `src/presets` has no dependency on `src/dsp` at all - it only talks to the live `AudioProcessorValueTreeState`.

## The waveshaper cascade

`TenebraeEngine` drives three `CascadeStage` instances in series, each `gain -> AsymmetricClipper::processSample -> fixed interstage HP/LP filter pair`. Only the single pre-cascade **Gain** parameter is user-automatable; each stage's own internal drive, clipping asymmetry, and interstage filter corners are fixed voicing constants (set in `TenebraeEngine`'s constructor and `prepare()`). As of M1, the engine keeps **two** complete triplets of `CascadeStage` prepared at all times, selected by the **Voicing** parameter:

**Tight** (default, the original v0.1 cascade):

| Stage | Drive (fixed) | Asymmetry | Interstage HP | Interstage LP |
|---|---|---|---|---|
| 1 | 6 dB | 0.15 | 80 Hz | 9000 Hz |
| 2 | 8 dB | 0.25 | 120 Hz | 6500 Hz |
| 3 | 10 dB | 0.35 | 150 Hz | 5000 Hz |

**Loose** (M1 addition, a softer-driven, wider-band alternative):

| Stage | Drive (fixed) | Asymmetry | Interstage HP | Interstage LP |
|---|---|---|---|---|
| 1 | 4 dB | 0.10 | 60 Hz | 10000 Hz |
| 2 | 6 dB | 0.18 | 90 Hz | 8000 Hz |
| 3 | 8 dB | 0.25 | 110 Hz | 6500 Hz |

Within either voicing, each stage is driven a little harder, clips a little more asymmetrically, and is filtered a little tighter/darker than the last, so the cascade converges onto a focused "chug" band rather than piling up an ever-fizzier, ever-boomier mess as gain stacks. This is the same rationale `CascadeStage.h` documents at the single-stage level, concretised here with actual numbers. Loose applies the same idea with a gentler slope throughout (lower drive, less asymmetry, looser interstage corners), for a more vintage-leaning character rather than simply "less gain".

Both triplets are prepared in every `TenebraeEngine::prepare()` call and reset in every `reset()` call, so `TenebraeEngine::setVoicing()` is a cheap branch in `process()` (which triplet actually runs), never a reallocation - real-time-safe to call every block, same as any other parameter setter.

## The tone stack

`ToneStack` (`src/dsp/ToneStack.{h,cpp}`) applies three independent, non-interacting bands after the cascade has been downsampled: Bass (low shelf, 150 Hz), Mid (peak, 650 Hz, Q 1.1), Treble (high shelf). v0.2.0 (`docs/design-brief.md` section 3.4) raises the Treble corner from 3.5 kHz to **5 kHz** - an internal filter-coefficient constant (`ToneStack::trebleShelfFrequencyHz`), not a parameter ID/range change, so it does not break existing sessions/presets, only the *sound* of an existing non-zero Treble setting shifts slightly. The move separates Treble from both the pre-cascade Bright shelf (3.5 kHz, unchanged) and the new post-cascade Presence shelf (2.4 kHz, see below), each of which previously either shared or sat close to the old 3.5 kHz Treble corner. `combinedGainLimitDb` (21 dB) still bounds Bass/Mid/Treble's user-dB-plus-Tone-Voice-tilt combination exactly as before; Presence is a fully separate filter stage, not summed into any ToneStack band, so no new combined-clamp logic was needed for it.

## The Gate

`Gate` (`src/dsp/Gate.{h,cpp}`, v0.2.0) is a conventional fixed attack/hold/release expander/gate - explicitly *not* an attempt to replicate any proprietary adaptive/program-dependent algorithm (see `docs/design-brief.md`'s honesty section). It runs at the host rate, inserted after Presence and before Level, so it gates the *fully-voiced* wet signal (post-cascade, post-tone-stack, post-Presence) rather than the raw pre-cascade input - matching where reference plugins in this genre place their gates ("usually first, or right after your amp sim" - `docs/research-notes.md` section 7), with Tenebrae itself being the "amp sim".

Detection is a per-sample peak level across all channels (max `|x|`), compared directly against a linear threshold with no detector-side smoothing of its own - this is what lets the gate react to a transient within a single sample rather than waiting for a filtered envelope to catch up. Once the level crosses Threshold the gate's gain ramps towards 1 over the Attack time (a one-pole exponential ramp, `Gate::computeRampCoefficient()`); once the level drops back below Threshold, a Hold countdown runs before Release begins (gain ramps towards 0 over the Release time) - the standard technique that stops a held/sustained note's own natural amplitude ripple from re-triggering/chattering the gate.

Gate on/off (`ParamIDs::gateOn`) is a **true structural bypass**: `Gate::process()` returns immediately, touching neither the audio buffer nor any internal envelope/hold/gain state, whenever disabled - not merely "compute an always-open gate". This is what makes the bypass state provably independent of whatever the gate's internal state happened to be at the moment it was disabled (see `tests/GateTests.cpp`'s and `tests/EngineTests.cpp`'s bypass tests). Gate defaults to **ON** - a deliberate default-behaviour change from every other v0.1 module, flagged explicitly in `docs/design-brief.md` section 5's honesty note: the research is unanimous that a gate is a structural expectation of "tight chug" tone in this genre, not an optional add-on, so shipping it off by default would silently reproduce the exact gap the brief exists to close.

## Presence

`TenebraeEngine`'s Presence shelf (a `juce::dsp::IIR` high-shelf, fixed 2.4 kHz corner, sourced to a reference high-gain amp's documented Presence-control pivot - see `docs/research-notes.md` section 2, the citation-only source for this number) sits after the tone stack and before the Gate. Unlike Bright (deliberately pre-cascade, feeding a brighter signal into the clipper), Presence is deliberately **post-cascade** - the reference class's own Presence control is a power-amp negative-feedback stage acting on the already-driven signal, not a pre-distortion EQ; Presence mirrors that functional position without claiming to model the feedback-loop circuit itself.

At exactly 0 dB (the default), `TenebraeEngine::processChunk()` skips `presenceShelf.process()` entirely rather than merely computing a near-unity filter - the same "off position = skip the IIR processing, not just compute an extreme-but-active filter" pattern sibling plugin `nave`'s `CabConvolutionEngine` uses for its own LoCut/HiCut/Distance controls at their minimum/maximum positions. This is required for a genuinely *bit-accurate* passthrough at the default (not just "close to flat"), which is what a new control must be so it never changes the tone of an existing preset/session on migration (`docs/design-brief.md` section 6) - `tests/EngineTests.cpp` verifies this by dirtying the shelf's IIR state with an extreme Presence value and confirming the subsequent 0 dB output is byte-for-byte independent of that prior state, which only a true structural skip (not an active near-unity filter) can guarantee. The shelf's IIR state is also reset exactly on a bypassed-to-engaged transition (`presenceEngagedPreviously`), so re-engaging Presence after a period at 0 dB always starts from clean state rather than stale delay-line memory.

## Latency and oversampling

The three cascade stages run inside an 8x oversampled block (`juce::dsp::Oversampling<float>`, half-band polyphase IIR, `useIntegerLatency = true`). Three cascaded nonlinearities generate substantially more high-frequency content than a single clipper, so 8x (vs. e.g. 4x for a single-stage boost/OD) keeps aliasing generated by every stage - not just the first - out of the audible band.

This oversampling is the only source of the plugin's reported latency: `TenebraeEngine::getLatencySamples()` returns `oversampler.getLatencyInSamples()` (an exact integer, since `useIntegerLatency` is enabled), and `TenebraeAudioProcessor::prepareToPlay()` reports it to the host via `setLatencySamples()`, so host-side plugin delay compensation (PDC) accounts for the whole chain.

The dry path used by the Mix control has to stay time-aligned with this delayed wet path. Rather than a hand-rolled delay line, `TenebraeEngine` uses `juce::dsp::DryWetMixer`: the pre-processing signal is captured via `pushDrySamples()` before any wet-path filtering touches the buffer, and `setWetLatency(getLatencySamples())` configures the mixer's internal delay line to match. `mixWetSamples()` then blends the two back together, so at Mix = 0% the output is a sample-accurate passthrough of the input, once shifted by `getLatencySamples()` (this is exactly what `tests/EngineTests.cpp`'s null test verifies, to < -90 dBFS residual).

One JUCE 8.0.14 behaviour worth calling out because it cost real debugging time on a sibling plugin (see `tests/DryWetMixerContractTests.cpp`): `DryWetMixer`'s internal dry/wet gain smoothers default their *target* to fully wet (`mix == 1.0`) until `setWetMixProportion()` is called, and the mixer's own `reset()` (invoked from its `prepare()`) only snaps the smoothers' *current* value to whatever *target* is set at that moment - it has no idea what the "real" starting Mix parameter value should be. Skipping this would mean a freshly prepared engine audibly ramps in from 100% wet over the mixer's internal ~50ms default ramp on every `prepareToPlay()`, regardless of the actual Mix parameter. `TenebraeEngine::prepare()` works around this by calling `dryWetMixer.setWetMixProportion(lastMixProportion)` *before* its own `reset()` runs, so the mixer is already sitting at the correct dry/wet balance from the very first `process()` call.

The Tight HPF, the M1 Bright shelf, and the ToneStack's Bass/Mid/Treble bands are plain 2nd-order IIR filters (`juce::dsp::IIR::Coefficients::makeHighPass`/`makeHighShelf`/`makeLowShelf`/`makePeakFilter`) running at the host rate, outside the oversampled block - they add no new nonlinearity, so no extra anti-aliasing headroom is needed for them, and their own small, frequency-dependent group delay is treated as ordinary filter character rather than something to compensate.

**Bright** applies a fixed high-shelf (+5 dB @ 3.5 kHz) after Tight and before Gain - i.e. before the cascade's nonlinearity, not after it, so engaging Bright changes what feeds the clipper rather than just re-EQing an already-clipped signal. `TenebraeEngine::setBright()` recomputes the shelf's coefficients only on an actual state change (comparing against the last-known `brightEnabled`), avoiding redundant trig calls on every block for a control that is typically left alone for long stretches. As of v0.2.0, Bright's 3.5 kHz corner is shared with neither the tone stack's Treble shelf (moved to 5 kHz) nor the new Presence shelf (2.4 kHz) - see [The tone stack](#the-tone-stack) and [Presence](#presence) above.

**Tone Voice** (Flat/Scoop/Boost) does not touch any filter directly; it stores a fixed `(bassTiltDb, midTiltDb, trebleTiltDb)` triple in `ToneStack`, added to each band's already-smoothed dB value inside `ToneStack::updateCoefficients()` before the combined total (clamped to +/-21 dB) is handed to `makeLowShelf`/`makePeakFilter`/`makeHighShelf`. This means the individual Bass/Mid/Treble knobs stay fully live and independently automatable on top of whichever Tone Voice is selected, rather than Tone Voice overriding them.

Both Bright and Voicing are discrete switches (like a physical amp channel-select or bright-cap switch), so - unlike Tight/Gain/Bass/Mid/Treble/Mix - they are deliberately **not** smoothed: engaging them can produce a small audible step rather than a crossfade. This is a conscious trade-off documented at each call site (`TenebraeEngine::setBright()`, `TenebraeEngine::setVoicing()`), not an oversight.

## Parameter smoothing

- **Gain** and **Level** are plain gain stages (`juce::dsp::Gain<float>`), which ramp sample-accurately via their own internal `SmoothedValue` (`setRampDurationSeconds`). Each `CascadeStage`'s fixed internal drive uses the same mechanism.
- **Tight** is a filter cutoff frequency. Recomputing IIR coefficients involves trig calls, so it is not cheap to interpolate per sample; instead it is smoothed with a `juce::SmoothedValue<float, ValueSmoothingTypes::Multiplicative>` (multiplicative smoothing suits frequencies, which are perceived logarithmically) and the filter coefficients are recomputed once per block from the smoothed value.
- **Bass/Mid/Treble** use linear `SmoothedValue`s on the dB values themselves (`ToneStack`), re-applied to their respective filter coefficients once per block via `updateCoefficients()` (which also folds in the Tone Voice tilt - see above).
- **Presence** (v0.2.0) uses the same linear-`SmoothedValue`-on-dB pattern as Bass/Mid/Treble (`presenceDbSmoothed`, `TenebraeEngine`'s own 50 ms `smoothingTimeSeconds`), for consistency - it is a continuous control like Bass/Mid/Treble, not a discrete switch like Bright/Voicing/Tone Voice.
- **Gate** (v0.2.0) is a special case: Threshold/Attack/Hold/Release are not `juce::SmoothedValue`-ramped parameters themselves (they change the gate's own ballistics coefficients directly, recomputed in each setter - see `Gate.cpp`); what *is* smoothed is the gate's own output **gain**, via a per-sample one-pole ramp using a different coefficient depending on whether the gate is opening (Attack) or closing (Release) - see [The Gate](#the-gate) above.
- **Mix** is smoothed both by the engine's own `juce::SmoothedValue<float, ValueSmoothingTypes::Linear>` (feeding `DryWetMixer::setWetMixProportion()` once per block) and by `DryWetMixer`'s own internal ~50ms ramp on top of that.
- **Voicing**, **Bright**, **Tone Voice**, and **Gate on/off** are discrete switches and are intentionally not smoothed at the parameter level (see above) - Gate on/off's own *audible* transition is still click-free because disabling it is a structural bypass of an already-settled gain state, not a hard mute of live audio (see [The Gate](#the-gate)).
- All continuous-control smoothers are seeded to their real starting value in `TenebraeEngine::prepare()` (see `lastTightHz`/`lastGainDb`/`lastLevelDb`/`lastMixProportion`/`lastPresenceDb`), so re-preparing (sample-rate change, etc.) never resets a live parameter back to a built-in default or lets a smoother ramp from an invalid 0 Hz/0.0 starting point. `lastLevelDb` is an M1 addition: `juce::dsp::Gain`'s internal `SmoothedValue` default-constructs to *linear zero* (silence), not unity, so `outputLevel` - like `preGain` already did via `lastGainDb` - must be explicitly re-primed on every `prepare()`, or any prepare() call not immediately preceded by a fresh `setLevelDb()` call would produce a permanently silent wet path (see the M1 fix note in `TenebraeEngine.cpp`).

## Real-time safety

- `TenebraeAudioProcessor::processBlock()` starts with `juce::ScopedNoDenormals`.
- All DSP state (filters, the oversampler, the cascade stages, the dry/wet delay line) is allocated in `prepare()`/`prepareToPlay()` and never reallocated on the audio thread.
- `reset()` clears all filter/oversampler/cascade/delay-line state without deallocating (`TenebraeEngine::reset()`, called from both `AudioProcessor::reset()` and internally from `prepare()`).
- Parameter values are read via `apvts.getRawParameterValue()` atomics in `processBlock()`, never via `apvts.getParameter()->getValue()` (which is not guaranteed lock/allocation-free) and never via `String`-keyed lookups on the audio thread.
- `TenebraeEngine::process()` treats a zero-sample block as a safe no-op before touching any filter/oversampler/cascade state.
- Filter cutoff frequencies passed to `IIR::Coefficients::makeHighPass`/`makeLowPass` are clamped below Nyquist (`clampBelowNyquist`, in `TenebraeEngine.cpp` and `CascadeStage.cpp`) as defensive insurance against invalid coefficients if the plugin is ever prepared at an unusually low sample rate.
- **Voicing** switches between two fully preallocated cascade triplets (both prepared/reset alongside everything else) rather than reconstructing one on demand, so selecting a voicing is a plain integer comparison in `process()` - no allocation, matching the same real-time-safety bar as every other parameter.
- **Gate** (v0.2.0, `src/dsp/Gate.{h,cpp}`) allocates no state at all beyond its own fixed-size scalar members (envelope/hold-counter/current-gain); every setter recomputes coefficients directly (no containers, no trig-heavy path per sample beyond the fixed one-pole ramp update) - safe to call every block/sample from the audio thread. Gate on/off's structural bypass (`if (! enabled) return;`) means a disabled gate costs nothing beyond a single branch.
- **Presence** (v0.2.0) reuses the same `juce::dsp::ProcessorDuplicator<IIR::Filter, IIR::Coefficients>` pattern as every other filter in this engine, prepared/reset alongside them; its bypass-at-0dB skip (see [Presence](#presence) above) is itself a real-time-safe branch, not a reallocation.
- `src/presets/PresetManager`'s only audio-thread-adjacent code is its `AudioProcessorValueTreeState::Listener::parameterChanged()` override (dirty-flag tracking), implemented as a single lock-free `std::atomic<bool>` store - every other `PresetManager`/`PresetBar` method does file I/O/JSON parsing/`juce::String` allocation and is message-thread-only, never called from `processBlock()` (see `docs/preset-system-notes.md`'s real-time-safety section for the full writeup).
