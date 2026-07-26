#pragma once

#include <juce_dsp/juce_dsp.h>

#include <vector>

// Tenebrae's expander/gate, inserted after the tone stack/Presence and
// before Level.
//
// v0.3.0 turns the v0.2 fixed-ballistics gate into a STRICT SUPERSET
// (brief section 3.4). Every new capability is a no-op at its neutral
// default, and with all of them neutral the code path reduces to the exact
// v0.2 expressions - same raw cross-channel peak detector, same one-pole
// ballistics, same hold counter, same 0/1 targets - so a migrated session
// renders sample-for-sample as it did before. tests/GateTests.cpp (T-G1)
// pins that against a literal transcription of the v0.2 algorithm.
//
// The five additions, each neutral by default:
//
//  1. KEY TAP (Post -> Pre). A cascade running at 40 dB of gain squashes the
//     40+ dB difference between "noise floor" and "playing" down to a few dB
//     by the time the signal reaches the gate, which is why a post-distortion
//     detector cannot tell them apart. Keying from a pre-distortion copy of
//     the plugin input (through a fixed 80 Hz - 8 kHz detector band-pass,
//     research-gate-expander.md section 2.7) restores the full dynamic range
//     to the detector. The gain reduction is still applied at the same place
//     in the chain; only the detector moves.
//  2. HYSTERESIS. Separate open (T) and close (T-H) thresholds, so a signal
//     hovering at the threshold cannot chatter the gate. H = 0 collapses to
//     the single v0.2 threshold.
//  3. RANGE. A finite closed-state floor of -M dB instead of a hard mute.
//     The parameter's top position is "Mute" (target exactly 0.0), which is
//     the legacy behaviour and the default.
//  4. PROGRAM-DEPENDENT RELEASE (Manual -> Auto). A two-envelope race
//     (research-gate-expander.md section 2.5): a fast peak envelope against
//     a slowly self-releasing one. When they diverge by more than the window
//     W the note has actually stopped -> fast dB-linear release; otherwise
//     the gate tracks the note's own measured decay slope plus a small
//     margin, so it fades just ahead of the note and never lets the noise
//     floor surface. Manual keeps the v0.2 one-pole release.
//  5. All control decisions stay per-sample (never block-quantised, which
//     would chatter), with a denormal bias in the log-domain envelopes.
//
// Allocation-free after prepare(); every setter and process() is safe to call
// from the audio thread.
class Gate
{
public:
    enum class KeySource
    {
        post = 0, // v0.2 behaviour: detector reads the gated signal itself
        pre = 1   // detector reads the pre-distortion key buffer
    };

    enum class ReleaseMode
    {
        manual = 0, // v0.2 behaviour: one-pole release driven by Release
        automatic = 1
    };

    Gate();

    void prepare (const juce::dsp::ProcessSpec& spec);

    // Resets the gate to fully open (current gain = 1, hold countdown
    // cleared) without deallocating - a freshly (re)prepared/reset engine
    // must not gate the very first block shut before any signal has been
    // seen, matching the rest of TenebraeEngine's reset() semantics.
    void reset();

    // Points the detector at this block's pre-distortion key signal. The
    // pointers must stay valid for the duration of the following process()
    // call and cover at least as many samples as the block being processed.
    // Passing a null/empty key makes a Pre-keyed gate fall back to Post
    // keying for that block rather than gating on silence.
    void setKeyBuffer (const float* const* keyChannels, size_t numKeyChannels, size_t numKeySamples) noexcept;

    // Processes `context` in place. A no-op (true bypass, `context` left
    // byte-for-byte untouched, no internal state read or written) whenever
    // the gate is disabled - see setEnabled().
    void process (juce::dsp::ProcessContextReplacing<float>& context);

    void setThresholdDb (float newThresholdDb);
    void setAttackMs (float newAttackMs);
    void setHoldMs (float newHoldMs);
    void setReleaseMs (float newReleaseMs);

    // v0.3.0 additions - all neutral at their defaults (see the class docs).
    void setKeySource (KeySource newKeySource) noexcept;
    void setHysteresisDb (float newHysteresisDb) noexcept;

    // Closed-state floor. `isMute` selects the legacy hard 0.0 target and is
    // the default; otherwise the floor is -rangeDb.
    void setRange (float newRangeDb, bool isMute) noexcept;
    void setReleaseMode (ReleaseMode newReleaseMode) noexcept;

    // Gate on/off. When off, process() returns immediately without touching
    // `context` or any internal state (envelope/hold/gain) at all - a true
    // structural bypass.
    void setEnabled (bool shouldBeEnabled);

    static constexpr float minThresholdDb = -80.0f;
    static constexpr float maxThresholdDb = 0.0f;
    static constexpr float minAttackMs = 0.1f;
    static constexpr float maxAttackMs = 20.0f;
    static constexpr float minHoldMs = 0.0f;
    static constexpr float maxHoldMs = 500.0f;
    static constexpr float minReleaseMs = 5.0f;
    static constexpr float maxReleaseMs = 2000.0f;

    static constexpr float minHysteresisDb = 0.0f;
    static constexpr float maxHysteresisDb = 12.0f;
    static constexpr float minRangeDb = 20.0f;
    static constexpr float maxRangeDb = 90.0f;

    // Detector band-pass corners for the pre-distortion key tap
    // (research-gate-expander.md section 2.7 "keyed" defaults: the key is a
    // clean DI-like signal, so the band is opened up rather than tuned as
    // tightly as a post-distortion detector would need).
    static constexpr float keyHighPassHz = 80.0f;
    static constexpr float keyLowPassHz = 8000.0f;

    // Program-dependent release constants (research section 2.5).
    static constexpr float tvpFastAttackMs = 0.5f;
    static constexpr float tvpFastReleaseMs = 10.0f;
    static constexpr float tvpSlowReleaseDbPerSecond = 40.0f;
    static constexpr float tvpWindowDb = 5.0f;
    static constexpr float tvpDumpReleaseDbPerSecond = 800.0f;
    static constexpr float tvpTrackMarginDbPerSecond = 15.0f;
    static constexpr float tvpMaxTrackedSlopeDbPerSecond = 400.0f;
    static constexpr float tvpSlopeSmoothingMs = 50.0f;

    // Floor for the log-domain envelopes, and the crossfade applied when the
    // key source is switched mid-stream.
    static constexpr float envelopeFloorDb = -160.0f;
    static constexpr float keyCrossfadeMs = 10.0f;

private:
    // One-pole ramp coefficient for reaching ~63% of a gain step in
    // `timeMs` milliseconds - the standard exponential-ramp ballistics
    // technique used by every fixed-attack/release gate/compressor design.
    static float computeRampCoefficient (float timeMs, double sampleRate) noexcept;

    void updateThresholds() noexcept;
    void updateKeyFilters();

    // Peak of the filtered key signal at `sample`, advancing the detector
    // band-pass state. Returns 0 when no key buffer is available.
    float processKeySample (size_t sample) noexcept;

    double sampleRate = 44100.0;

    float thresholdLinear = 0.0f;
    float closeThresholdLinear = 0.0f;
    float attackCoefficient = 1.0f;
    float releaseCoefficient = 1.0f;
    int holdSamples = 0;

    int holdCounter = 0;
    float currentGain = 1.0f;
    bool enabled = true;

    // True once the gate has opened and until it fully closes; selects which
    // of the two thresholds re-arms the hold counter. Inert at H = 0, where
    // both thresholds are identical.
    bool gateLatched = false;

    // Last commanded values, re-applied on every prepare() so a re-prepare
    // (sample-rate change, etc.) never resets a live parameter back to a
    // built-in default - the same pattern TenebraeEngine/ToneStack/
    // CascadeStage already use for their own "last*" members.
    float lastThresholdDb = -48.0f;
    float lastAttackMs = 1.0f;
    float lastHoldMs = 20.0f;
    float lastReleaseMs = 150.0f;

    //==========================================================================
    // v0.3.0 state. Every member below is inert at the neutral defaults.

    KeySource keySource = KeySource::post;
    ReleaseMode releaseMode = ReleaseMode::manual;

    float hysteresisDb = 0.0f;
    float closedTargetGain = 0.0f; // "Mute" - the legacy hard target
    bool rangeIsMute = true;

    // 0 = fully Post, 1 = fully Pre. Ramped over keyCrossfadeMs so switching
    // the key source mid-performance does not step the detector.
    float keyMix = 0.0f;
    float keyMixIncrement = 1.0f;

    const float* const* keyChannelPointers = nullptr;
    size_t numKeyChannels = 0;
    size_t numKeySamples = 0;

    // Detector band-pass, one 2nd-order Butterworth high-pass and low-pass
    // per key channel.
    std::vector<juce::dsp::IIR::Filter<float>> keyHighPassFilters;
    std::vector<juce::dsp::IIR::Filter<float>> keyLowPassFilters;

    // TVP (program-dependent release) state.
    float fastEnvelopeDb = envelopeFloorDb;
    float slowEnvelopeDb = envelopeFloorDb;
    float trackedSlopeDbPerSecond = 0.0f;
    float tvpFastAttackCoefficient = 1.0f;
    float tvpFastReleaseCoefficient = 1.0f;
    float tvpSlopeSmoothingCoefficient = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Gate)
};
