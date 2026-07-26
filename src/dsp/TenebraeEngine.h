#pragma once

#include <juce_dsp/juce_dsp.h>

#include "CascadeStage.h"
#include "Gate.h"
#include "PowerAmp.h"
#include "ToneStack.h"
#include "TriodeCascade.h"

#include <vector>

// The complete Tenebrae signal path, independent of juce::AudioProcessor so
// it can be exercised directly by unit tests without instantiating a full
// plugin (see tests/EngineTests.cpp). Owns all DSP state; every buffer/
// filter/oversampler is allocated in prepare() and never reallocated on the
// audio thread.
//
// Signal flow (see docs/architecture.md for the full diagram and the
// latency-compensation rationale):
//
//   input -> Tight HPF -> Bright (switch) -> pre-Gain
//         -> [8x oversampled] 3-stage cascade (Voicing: Tight/Loose)
//         -> 3-band tone stack (Bass/Mid/Treble, tilted by Tone Voice)
//         -> Presence (post-cascade high-shelf) -> Gate -> Level -> Dry/Wet mix
//
// Each cascade stage is gain -> asymmetric tanh clip -> fixed interstage
// HP/LP filter (see CascadeStage); the three stages use progressively
// tighter/darker fixed voicing so the cascade converges onto the "chug"
// band rather than piling up an ever-fizzier mess (see TenebraeEngine.cpp).
// Voicing selects between two such fixed-voicing triplets ("Tight"/"Loose").
//
// v0.2.0 (docs/design-brief.md) added two post-tone-stack modules: Presence
// (a fixed-2.4kHz high-shelf, modelled on the reference class's power-amp
// Presence feedback control's functional position, not its circuit - see
// setPresenceDb()) and Gate (a conventional fixed attack/hold/release
// expander/gate - see Gate.h), both inserted before Level.
//
// The dry path is delay-compensated against the oversampler's reported
// latency via juce::dsp::DryWetMixer, so Mix at 0% is a sample-accurate
// (once shifted by getLatencySamples()) passthrough of the input - this is
// what the plugin's null test (tests/EngineTests.cpp) exercises.
class TenebraeEngine
{
public:
    // v0.3.0: the engine branch. "Classic" runs the v0.2 signal path with
    // byte-identical code (AsymmetricClipper/CascadeStage/8x IIR oversampler,
    // all untouched by this release) - which is what makes migration of an
    // existing session a provable no-op rather than a hopeful one. "Triode"
    // runs the new stateful triode cascade plus the optional power-amp block.
    enum class EngineMode
    {
        classic = 0,
        triode = 1
    };

    // Oversampling quality for the Triode engine only. Classic keeps its own
    // fixed 8x IIR path regardless of this setting.
    enum class Quality
    {
        eco = 0,      // 2x polyphase IIR half-band  - near-zero latency, live
        standard = 1, // 4x polyphase IIR half-band  - the default
        hq = 2        // 8x equiripple FIR half-band - linear phase, mixdown
    };

    TenebraeEngine();

    // Allocates all DSP state. Must be called (and completed) before the
    // first process() call, and again whenever sample rate/block size/
    // channel count change.
    void prepare (const juce::dsp::ProcessSpec& spec);

    // Clears all filter/oversampler/delay-line state without deallocating.
    // Safe to call from the audio thread (e.g. on playback stop/loop).
    void reset();

    // Processes `block` in place. A zero-sample block is a safe no-op. No
    // allocation occurs here.
    //
    // `block` may contain MORE samples than the maximumBlockSize declared to
    // prepare() (some hosts occasionally hand over an oversized block -
    // offline bounce/render, buffer-size renegotiation - see GitHub issue
    // #13): the oversampler's internal buffer, driveGainScratch/
    // hostRateGainScratch (see RealtimeGain.h), and every other
    // prepare()-sized buffer in this engine are all sized to exactly that
    // declared maximum, and juce::dsp::Oversampling's own bounds check on an
    // oversized block is a debug-only jassert (compiles to nothing in
    // Release). process() detects this and transparently loops over
    // prepare()-sized chunks via processChunk() instead of passing the
    // oversized block straight through, so the "never process more than
    // maximumBlockSize samples in one internal call" invariant holds
    // regardless of what the caller hands over.
    void process (juce::dsp::AudioBlock<float>& block);

    // Parameter setters, in real units (dB, Hz, 0-1 proportion). Safe to
    // call every block from the audio thread - no allocation/locks.
    void setTightFrequencyHz (float newFrequencyHz);
    void setGainDb (float newGainDb);
    void setBassDb (float newBassDb);
    void setMidDb (float newMidDb);
    void setTrebleDb (float newTrebleDb);

    // Presence: post-cascade/post-tone-stack high-shelf, fixed 2.4 kHz
    // corner (see the .cpp). At exactly 0 dB (the default) the shelf is
    // skipped entirely, not merely computed as a near-unity filter - see
    // the .cpp's process() for why this is required for a true passthrough
    // guarantee (docs/design-brief.md section 4's Presence test).
    void setPresenceDb (float newPresenceDb);

    // Gate: conventional fixed attack/hold/release expander/gate, applied to
    // the fully-voiced wet signal after Presence. Real units: dB, ms, ms, ms.
    void setGateThresholdDb (float newThresholdDb);
    void setGateAttackMs (float newAttackMs);
    void setGateHoldMs (float newHoldMs);
    void setGateReleaseMs (float newReleaseMs);
    void setGateOn (bool shouldBeOn);

    void setLevelDb (float newLevelDb);
    void setMixProportion (float newProportion01);

    // Voicing: 0 = Tight (v0.1 cascade), 1 = Loose (softer-driven, wider-
    // band alternative) - selects which of the two preallocated cascade
    // stage triplets process() runs. Both triplets are always kept prepared
    // so switching is a branch, not a reallocation.
    void setVoicing (int newVoicingIndex);

    // Bright: fixed pre-cascade high-shelf pre-emphasis (see .cpp for the
    // exact shelf), modelled on a high-gain amp channel's bright switch.
    // Discrete switch, not a continuous control - see setBright()'s .cpp
    // comment for why it is intentionally not smoothed.
    void setBright (bool isBrightOn);

    // Tone Voice: forwarded to ToneStack::setToneVoice() - see there.
    void setToneVoice (int newToneVoiceIndex);

    //==========================================================================
    // v0.3.0 parameters. All of them are no-ops at their neutral defaults
    // (Classic engine, power amp off, gate at v0.2 behaviour).

    // Engine/Quality switching is allocation-free: every oversampling chain
    // is built in prepare() and kept resident, so a switch swaps a branch and
    // re-derives the (already-allocated) triode/power-amp coefficients for
    // the new oversampled rate. Both change getLatencySamples(), which the
    // processor re-reports from the message thread.
    void setEngineMode (EngineMode newEngineMode);
    void setQuality (Quality newQuality);

    // Bias Shift, 0-200 % over the voicing's calibrated per-stage bias depths.
    void setStageBiasPercent (float newBiasPercent);

    void setPowerAmpOn (bool shouldBeOn);
    void setResonanceDb (float newResonanceDb);
    void setSagPercent (float newSagPercent);

    // Gate v2 - see Gate.h. Each of these is neutral at its default.
    void setGateKeySource (Gate::KeySource newKeySource);
    void setGateHysteresisDb (float newHysteresisDb);
    void setGateRange (float newRangeDb, bool isMute);
    void setGateReleaseMode (Gate::ReleaseMode newReleaseMode);

    // Oversampling latency in samples, valid after prepare() has run.
    int getLatencySamples() const noexcept { return latencySamples; }

private:
    // Processes a single chunk of at most maxPreparedBlockSamples samples -
    // the actual signal path, factored out of process() so the oversized-
    // block guard there (see process()'s doc comment and GitHub issue #13)
    // can loop over it. `chunk` must not exceed maxPreparedBlockSamples.
    void processChunk (juce::dsp::AudioBlock<float>& chunk);

    static constexpr int oversamplingFactorPow2 = 3; // 2^3 = 8x oversampling
    static constexpr double smoothingTimeSeconds = 0.05;
    // Butterworth (maximally-flat) Q for the Tight HPF.
    static constexpr float filterQ = juce::MathConstants<float>::sqrt2 / 2.0f;

    // Presence sits within this epsilon of exactly 0 dB is treated as the
    // explicit "off" position (filter skipped entirely, not merely computed
    // as a near-unity shelf) - see the .cpp's process(). Matches the
    // NormalisableRange step (0.01) ParameterLayout.cpp gives the Presence
    // parameter, so no in-range user-reachable value is ever misclassified
    // as "default".
    static constexpr float presenceBypassEpsilonDb = 0.005f;

    double sampleRate = 44100.0;

    // The maximumBlockSize declared to prepare() - i.e. the largest sample
    // count the oversampler's internal buffer (and every other
    // prepare()-sized buffer in this engine) was actually allocated for.
    // process() chunks any larger incoming block down to this size before
    // calling processChunk() - see process()'s doc comment and GitHub issue
    // #13. Always >= 1 once prepare() has run.
    size_t maxPreparedBlockSamples = 0;

    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> tightHighPass;

    // Bright pre-emphasis: a fixed high-shelf, toggled between unity (0 dB)
    // and a fixed boost by setBright() - see TenebraeEngine.cpp for the
    // shelf frequency/gain. Runs after Tight, before preGain, so engaging it
    // feeds a brighter signal into the cascade's nonlinearity rather than
    // just brightening the already-clipped output.
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> brightShelf;

    juce::dsp::Gain<float> preGain;

    // Recomputes latency/DryWetMixer alignment and re-derives the triode/
    // power-amp coefficients for the currently selected chain's oversampled
    // rate. Allocation-free: every chain below is built in prepare().
    void applyChainSelection (bool resetChainState);

    // The oversampler the current engine/quality selection runs on.
    juce::dsp::Oversampling<float>* getActiveOversampler() const noexcept;

    // Classic's fixed 8x half-band polyphase IIR chain - untouched from v0.2,
    // and deliberately kept as its own instance so the Classic path's filter
    // state can never be perturbed by anything the Triode path does.
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    // The three Triode-engine chains, all built in prepare() and kept
    // resident so that a Quality switch is a pointer swap (brief 3.3).
    std::unique_ptr<juce::dsp::Oversampling<float>> triodeEcoOversampler;
    std::unique_ptr<juce::dsp::Oversampling<float>> triodeStandardOversampler;
    std::unique_ptr<juce::dsp::Oversampling<float>> triodeHqOversampler;

    // Fixed per-stage voicing (asymmetry, interstage HP/LP corner
    // frequencies, internal drive) is set up in the constructor/prepare() -
    // see TenebraeEngine.cpp for the rationale behind each stage's numbers.
    // Only the pre-cascade Gain parameter is user-automatable. Both the
    // "Tight" (v0.1) and "Loose" (M1) triplets are always prepared; process()
    // branches on currentVoicing to pick which one actually runs, so
    // switching Voicing is allocation-free.
    CascadeStage cascadeStage1;
    CascadeStage cascadeStage2;
    CascadeStage cascadeStage3;

    CascadeStage cascadeStage1Loose;
    CascadeStage cascadeStage2Loose;
    CascadeStage cascadeStage3Loose;

    // v0.3.0 Triode engine: the stateful three-stage triode chain (both
    // voicings resident) and the optional power-amp block. Both live inside
    // the oversampled region, exactly like the Classic cascade.
    TriodeCascade triodeCascade;
    PowerAmp powerAmp;

    ToneStack toneStack;

    // Presence: v0.2.0 addition, a fixed-2.4kHz high-shelf running at the
    // host rate after the tone stack - see setPresenceDb() and the .cpp's
    // process() for the "skip entirely at 0 dB" bypass handling.
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> presenceShelf;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> presenceDbSmoothed;
    float lastPresenceDb = 0.0f;
    // Tracks whether Presence was engaged (non-bypassed) on the previous
    // block, so process() can reset the shelf's IIR state exactly on a
    // bypassed->engaged transition (clean start, not stale delay-line
    // memory from however many blocks ago it was last active) - same
    // pattern sibling plugin nave's CabConvolutionEngine uses for its own
    // "off position" filters.
    bool presenceEngagedPreviously = false;

    // Gate: v0.2.0 addition - see Gate.h. Runs at the host rate, after
    // Presence and before Level, on the fully-voiced wet signal.
    Gate gate;

    juce::dsp::Gain<float> outputLevel;

    // Scratch buffer for RealtimeGain::process() (see TenebraeEngine.cpp and
    // RealtimeGain.h), sized in prepare() to the host's maximum block size.
    // Shared by preGain and outputLevel - both run at the host rate on the
    // same in-place `block`/`context`, never concurrently, so one buffer
    // covers both call sites. Routes around juce::dsp::Gain::process()'s
    // multichannel-branch alloca() (see GitHub issue #12); smaller in
    // magnitude here than CascadeStage::driveGainScratch since this is the
    // host-rate (not oversampled) block, but the same unbounded-stack-alloc
    // risk in principle.
    std::vector<float> hostRateGainScratch;

    // Sized generously above any realistic oversampling latency so
    // setWetLatency() never exceeds the mixer's internal delay-line
    // capacity regardless of sample rate.
    juce::dsp::DryWetMixer<float> dryWetMixer { 1024 };

    // Tight is a filter cutoff frequency (perceived logarithmically), so it
    // uses multiplicative smoothing; Mix uses linear smoothing and must be
    // able to reach exactly 0.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> tightFrequencySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;

    // Last commanded values (ParameterLayout defaults until a setter is
    // called), re-applied on every prepare() so re-prepare (sample-rate
    // change, etc.) never resets a live parameter back to a default or lets
    // a smoother start from an invalid 0 Hz.
    // lastLevelDb exists for the same reason as lastGainDb: juce::dsp::Gain
    // (used by both preGain and outputLevel) default-constructs its internal
    // SmoothedValue to *linear 0* (silence), not unity - unlike Tight/Bass/
    // Mid/Treble, which all have sane implicit defaults. Without re-applying
    // lastLevelDb here on every prepare(), any prepare() call not
    // immediately preceded by a fresh setLevelDb() (e.g. exercising the
    // engine directly, or a future re-prepare path that doesn't reseed every
    // parameter) would silently produce a permanently silent wet path - see
    // the M1 fix note in TenebraeEngine.cpp's prepare().
    float lastTightHz = 90.0f;
    float lastGainDb = 24.0f;
    float lastLevelDb = 0.0f;
    float lastMixProportion = 1.0f;

    // Voicing/Bright have no coefficient-priming subtlety like Tight/Mix
    // above (Voicing is a pure branch, Bright's shelf is fully recomputed by
    // setBright() itself whenever it changes - see the .cpp), but are still
    // tracked here so setVoicing()/setBright() can no-op on a repeated,
    // unchanged value from processBlock() every block.
    int currentVoicing = 0;
    bool brightEnabled = false;

    //==========================================================================
    // v0.3.0 state.

    EngineMode engineMode = EngineMode::classic;
    Quality quality = Quality::standard;
    bool powerAmpOn = false;

    // Set by setEngineMode()/setQuality() and consumed at the top of the next
    // processChunk(), so the (bounded, allocation-free) coefficient rebuild
    // happens at a block boundary rather than mid-block.
    bool chainSelectionDirty = false;

    // Pre-distortion copy of the incoming block, captured before the Tight
    // HPF, used by the gate's Pre key tap (Gate.h). Sized in prepare().
    juce::AudioBuffer<float> gateKeyBuffer;
    std::vector<const float*> gateKeyPointers;

    // Click suppression on an engine/quality swap: the freshly-reset chain
    // starts from zero state, so the first samples out of it are ramped in
    // rather than stitched onto whatever the previous chain left behind.
    static constexpr int chainSwapFadeSamples = 16;
    int chainSwapFadeCounter = 0;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> stageBiasSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> resonanceSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> sagSmoothed;

    float lastStageBiasScale = 1.0f;
    float lastResonanceDb = 0.0f;
    float lastSagAmount = 0.0f;

    int latencySamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TenebraeEngine)
};
