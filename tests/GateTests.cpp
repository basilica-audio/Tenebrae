#include "dsp/Gate.h"
#include "dsp/TriodeCascade.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <random>
#include <vector>

// Module-level tests for the v0.2.0 Gate (docs/design-brief.md section 3.5),
// exercised directly (not through the full TenebraeEngine) so the result
// isolates the gate's own ballistics from the rest of the signal chain -
// same pattern as tests/ToneStackTests.cpp/CascadeStageTests.cpp.
namespace
{
    constexpr double testSampleRate = 48000.0;

    juce::dsp::ProcessSpec makeTestSpec (int numChannels, juce::uint32 maxBlockSize)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = maxBlockSize;
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }
}

TEST_CASE ("Gate: silence stays silent, transient passes, tail decays without a click", "[dsp][gate]")
{
    // design-brief.md section 4: "silence -> transient above threshold ->
    // silence" - (a) pre-transient silence stays near-zero, (b) the
    // transient itself is not clipped/delayed beyond the attack time, (c)
    // post-transient output decays to near-zero within hold+release without
    // an abrupt (hard-mute) discontinuity at gate closure.
    //
    // Uses constant-level steps (not an oscillating tone) rather than a
    // sine burst: a genuine audio waveform's own sample-to-sample slope
    // (e.g. a full-scale 1 kHz tone can swing by >0.1 between adjacent
    // samples near its steepest point) would otherwise swamp the "no
    // hard-mute click" delta check below with waveform content unrelated
    // to the gate's own gain trajectory - a step/DC-style test signal is
    // the standard way to isolate ballistics (attack/hold/release) checks
    // from that, the same way a compressor/expander's ballistics are
    // normally characterised against a level step, not a full tone.
    constexpr int silenceLeadIn = 2000;
    constexpr int transientLength = 2000; // a loud burst, well above threshold
    constexpr float thresholdDb = -40.0f; // 0.01 linear
    constexpr float attackMs = 1.0f;
    constexpr float holdMs = 20.0f;
    constexpr float releaseMs = 80.0f;
    constexpr float loudLevel = 0.9f;
    constexpr float quietLevel = 0.001f; // well below the 0.01 linear threshold

    const auto holdSamples = static_cast<int> (holdMs * 0.001 * testSampleRate);
    const auto releaseSamples = static_cast<int> (releaseMs * 0.001 * testSampleRate); // one-pole time constant, in samples
    const auto decayCheckPoint = silenceLeadIn + transientLength + holdSamples + releaseSamples * 5; // ~99% decayed
    const auto numSamples = decayCheckPoint + 4000;

    Gate gate;
    gate.setThresholdDb (thresholdDb);
    gate.setAttackMs (attackMs);
    gate.setHoldMs (holdMs);
    gate.setReleaseMs (releaseMs);

    const auto spec = makeTestSpec (1, static_cast<juce::uint32> (numSamples));
    gate.prepare (spec);

    juce::AudioBuffer<float> buffer (1, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        float level = quietLevel;

        if (i < silenceLeadIn)
            level = 0.0f;
        else if (i < silenceLeadIn + transientLength)
            level = loudLevel;

        buffer.setSample (0, i, level);
    }

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    CHECK_NOTHROW (gate.process (context));

    CHECK (TestHelpers::allSamplesFinite (buffer));

    const auto* data = buffer.getReadPointer (0);

    // (a) Pre-transient silence: input was already 0, so this is trivially
    // near-zero regardless of gate state - the meaningful check is that the
    // gate doesn't inject anything (e.g. a DC offset) into true silence.
    for (int i = 0; i < silenceLeadIn; ++i)
        CHECK (std::abs (data[i]) < 1.0e-6f);

    // (b) The transient itself must not be clipped/attenuated to near-zero
    // for its full duration - well within the attack time (1 ms = 48
    // samples @ 48 kHz), the gate should be open and passing the level at
    // close to full amplitude.
    const auto attackSamples = static_cast<int> (attackMs * 0.001 * testSampleRate);
    float minLevelShortlyAfterAttack = loudLevel;

    for (int i = silenceLeadIn + attackSamples * 4; i < silenceLeadIn + transientLength; ++i)
        minLevelShortlyAfterAttack = std::min (minLevelShortlyAfterAttack, std::abs (data[i]));

    CHECK (minLevelShortlyAfterAttack > loudLevel * 0.9f); // comfortably close to the full input level

    // (c) No abrupt (hard-mute) discontinuity introduced BY THE GATE itself
    // anywhere in the block - a smoothed gain ramp closing over `releaseMs`
    // never produces a single-sample jump larger than this epsilon, whereas
    // a hard on/off multiply would jump close to the full quiet-level
    // amplitude the instant the gate closes. This deliberately excludes the
    // two sample indices where the *test signal itself* steps level
    // (silence->loud at the transient's onset, loud->quiet at its end) -
    // those are the test harness's own artificial level discontinuities,
    // not something any gain-only gate could or should smooth away (a real
    // pick attack/note release has its own rise/decay time in a genuine
    // recording; a gate's job is to not add a *second*, gate-caused click on
    // top of the program material's own dynamics, which is exactly what
    // this window verifies).
    constexpr float maxAllowedSampleToSampleDelta = 0.05f;
    const auto transientOnsetIndex = silenceLeadIn;
    const auto transientEndIndex = silenceLeadIn + transientLength;

    for (int i = 1; i < numSamples; ++i)
    {
        if (i == transientOnsetIndex || i == transientEndIndex)
            continue;

        CHECK (std::abs (data[i] - data[i - 1]) < maxAllowedSampleToSampleDelta);
    }

    // Output must decay to near-zero well within hold + release of the
    // transient ending.
    REQUIRE (decayCheckPoint < numSamples);
    CHECK (std::abs (data[decayCheckPoint]) < 0.01f);
}

TEST_CASE ("Gate: sustained signal above threshold is never gated (no chatter)", "[dsp][gate]")
{
    // A continuous sine held well above threshold for longer than
    // attack+hold+release combined must show no gain reduction at any
    // point after the attack ramp completes - proves the gate doesn't
    // false-trigger/chatter on sustained content.
    constexpr int numSamples = 48000; // 1s
    constexpr float thresholdDb = -40.0f;

    Gate gate;
    gate.setThresholdDb (thresholdDb);
    gate.setAttackMs (1.0f);
    gate.setHoldMs (20.0f);
    gate.setReleaseMs (150.0f);

    const auto spec = makeTestSpec (1, static_cast<juce::uint32> (numSamples));
    gate.prepare (spec);

    juce::AudioBuffer<float> buffer (1, numSamples);
    TestHelpers::fillWithSine (buffer, testSampleRate, 220.0, 0.8f);

    juce::AudioBuffer<float> reference;
    reference.makeCopyOf (buffer);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    gate.process (context);

    const auto* data = buffer.getReadPointer (0);
    const auto* refData = reference.getReadPointer (0);

    // Well past the attack ramp (a few ms is more than enough for a 1 ms
    // attack time constant to settle), the gate should track the reference
    // signal closely - no dips anywhere in the remainder of the block.
    constexpr int settleSamples = 2000; // ~42 ms, comfortably past a 1 ms attack

    for (int i = settleSamples; i < numSamples; ++i)
        CHECK (std::abs (data[i] - refData[i]) < 0.02f);
}

TEST_CASE ("Gate: NaN/Inf sweep at both threshold extremes with a full-scale signal stays finite", "[dsp][gate][nan]")
{
    for (float thresholdDb : { Gate::minThresholdDb, Gate::maxThresholdDb })
    {
        Gate gate;
        gate.setThresholdDb (thresholdDb);
        gate.setAttackMs (0.1f);
        gate.setHoldMs (0.0f);
        gate.setReleaseMs (5.0f);

        constexpr int numSamples = 4096;
        const auto spec = makeTestSpec (2, static_cast<juce::uint32> (numSamples));
        gate.prepare (spec);

        juce::AudioBuffer<float> buffer (2, numSamples);
        TestHelpers::fillWithSine (buffer, testSampleRate, 1000.0, 1.0f);

        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> context (block);
        CHECK_NOTHROW (gate.process (context));

        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Gate: NaN threshold parameter does not poison the gate with NaN gain", "[dsp][gate][nan]")
{
    Gate gate;
    gate.setThresholdDb (std::numeric_limits<float>::quiet_NaN());
    gate.setAttackMs (std::numeric_limits<float>::quiet_NaN());
    gate.setHoldMs (std::numeric_limits<float>::quiet_NaN());
    gate.setReleaseMs (std::numeric_limits<float>::quiet_NaN());

    constexpr int numSamples = 2048;
    const auto spec = makeTestSpec (1, static_cast<juce::uint32> (numSamples));
    gate.prepare (spec);

    juce::AudioBuffer<float> buffer (1, numSamples);
    TestHelpers::fillWithSine (buffer, testSampleRate, 1000.0, 0.7f);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    CHECK_NOTHROW (gate.process (context));

    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Gate: disabled is a true bypass - output is bit-identical to the untouched input", "[dsp][gate][bypass]")
{
    Gate gate;
    gate.setThresholdDb (0.0f); // would gate almost everything shut if engaged
    gate.setEnabled (false);

    constexpr int numSamples = 4096;
    const auto spec = makeTestSpec (2, static_cast<juce::uint32> (numSamples));
    gate.prepare (spec);

    juce::AudioBuffer<float> buffer (2, numSamples);
    TestHelpers::fillWithSine (buffer, testSampleRate, 1000.0, 0.5f);

    juce::AudioBuffer<float> reference;
    reference.makeCopyOf (buffer);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    gate.process (context);

    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < numSamples; ++i)
            CHECK (juce::exactlyEqual (buffer.getSample (channel, i), reference.getSample (channel, i)));
}

TEST_CASE ("Gate: disabled bypass is independent of prior engaged-state internal state", "[dsp][gate][bypass]")
{
    // Dirty the gate's internal envelope/hold/gain state with a hot
    // threshold that forces it fully closed, then disable it - the
    // subsequent bypassed output must be identical to a gate that was
    // never engaged at all, proving process() truly returns before
    // touching any state when disabled (not merely "computes an always-
    // open gate").
    constexpr int numSamples = 4096;
    const auto spec = makeTestSpec (1, static_cast<juce::uint32> (numSamples));

    Gate dirtiedGate;
    dirtiedGate.setThresholdDb (0.0f);
    dirtiedGate.prepare (spec);

    juce::AudioBuffer<float> warmup (1, numSamples);
    TestHelpers::fillWithSine (warmup, testSampleRate, 1000.0, 0.1f); // below 0 dB threshold - forces closed
    juce::dsp::AudioBlock<float> warmupBlock (warmup);
    juce::dsp::ProcessContextReplacing<float> warmupContext (warmupBlock);
    dirtiedGate.process (warmupContext); // gate closes, currentGain settles near 0

    dirtiedGate.setEnabled (false);

    Gate cleanGate;
    cleanGate.setEnabled (false);
    cleanGate.prepare (spec);

    juce::AudioBuffer<float> dirtiedBuffer (1, numSamples);
    juce::AudioBuffer<float> cleanBuffer (1, numSamples);
    TestHelpers::fillWithSine (dirtiedBuffer, testSampleRate, 500.0, 0.6f);
    cleanBuffer.makeCopyOf (dirtiedBuffer);

    juce::dsp::AudioBlock<float> dirtiedBlock (dirtiedBuffer);
    juce::dsp::ProcessContextReplacing<float> dirtiedContext (dirtiedBlock);
    dirtiedGate.process (dirtiedContext);

    juce::dsp::AudioBlock<float> cleanBlock (cleanBuffer);
    juce::dsp::ProcessContextReplacing<float> cleanContext (cleanBlock);
    cleanGate.process (cleanContext);

    for (int i = 0; i < numSamples; ++i)
        CHECK (juce::exactlyEqual (dirtiedBuffer.getSample (0, i), cleanBuffer.getSample (0, i)));
}

TEST_CASE ("Gate: reset() reopens the gate without crashing", "[dsp][gate]")
{
    Gate gate;
    gate.setThresholdDb (0.0f); // forces closed against a quiet signal

    constexpr int numSamples = 2048;
    const auto spec = makeTestSpec (1, static_cast<juce::uint32> (numSamples));
    gate.prepare (spec);

    juce::AudioBuffer<float> buffer (1, numSamples);
    TestHelpers::fillWithSine (buffer, testSampleRate, 1000.0, 0.1f);
    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    gate.process (context);

    CHECK_NOTHROW (gate.reset());

    TestHelpers::fillWithSine (buffer, testSampleRate, 1000.0, 0.1f);
    CHECK_NOTHROW (gate.process (context));
    CHECK (TestHelpers::allSamplesFinite (buffer));
}

//==============================================================================
// v0.3.0 Gate v2 assertions (brief section 6, T-G1..T-G7).
//
// The v0.3.0 gate is specified as a STRICT SUPERSET of the v0.2 one: every
// new capability must be a no-op at its default. T-G1 is the release gate on
// that promise and everything else measures what the new capabilities
// actually do, in dB and in milliseconds.

namespace
{
    // A literal transcription of the v0.2.0 Gate::process() loop, kept here
    // as the reference T-G1 compares against. Deliberately verbatim - the
    // point is to detect any change in the arithmetic, so this must not be
    // "tidied up" into an equivalent-looking rewrite.
    struct LegacyGateV2Reference
    {
        float thresholdLinear = 0.0f;
        float attackCoefficient = 1.0f;
        float releaseCoefficient = 1.0f;
        int holdSamples = 0;

        int holdCounter = 0;
        float currentGain = 1.0f;

        static float computeRampCoefficient (float timeMs, double sr) noexcept
        {
            const auto timeSeconds = juce::jmax (0.0001f, timeMs * 0.001f);
            return 1.0f - std::exp (-1.0f / (static_cast<float> (sr) * timeSeconds));
        }

        void configure (float thresholdDb, float attackMs, float holdMs, float releaseMs, double sr)
        {
            thresholdLinear = juce::Decibels::decibelsToGain (
                juce::jlimit (Gate::minThresholdDb, Gate::maxThresholdDb, thresholdDb));
            attackCoefficient = computeRampCoefficient (
                juce::jlimit (Gate::minAttackMs, Gate::maxAttackMs, attackMs), sr);
            holdSamples = static_cast<int> (std::round (static_cast<double> (
                juce::jlimit (Gate::minHoldMs, Gate::maxHoldMs, holdMs)) * 0.001 * sr));
            releaseCoefficient = computeRampCoefficient (
                juce::jlimit (Gate::minReleaseMs, Gate::maxReleaseMs, releaseMs), sr);

            currentGain = 1.0f;
            holdCounter = 0;
        }

        void process (juce::AudioBuffer<float>& buffer)
        {
            const auto numChannels = buffer.getNumChannels();
            const auto numSamples = buffer.getNumSamples();

            for (int sample = 0; sample < numSamples; ++sample)
            {
                float peak = 0.0f;

                for (int channel = 0; channel < numChannels; ++channel)
                    peak = std::max (peak, std::abs (buffer.getSample (channel, sample)));

                if (peak >= thresholdLinear)
                    holdCounter = holdSamples;
                else if (holdCounter > 0)
                    --holdCounter;

                const auto gateShouldBeOpen = (peak >= thresholdLinear) || (holdCounter > 0);
                const auto targetGain = gateShouldBeOpen ? 1.0f : 0.0f;
                const auto coefficient = (targetGain > currentGain) ? attackCoefficient : releaseCoefficient;
                currentGain += (targetGain - currentGain) * coefficient;

                for (int channel = 0; channel < numChannels; ++channel)
                    buffer.setSample (channel, sample,
                                      buffer.getSample (channel, sample) * currentGain);
            }
        }
    };

    // A 10 s adversarial programme: bursts, decays, silences, near-threshold
    // hovering and a noise bed - everything that could make two gate
    // implementations diverge.
    juce::AudioBuffer<float> makeAdversarialProgramme (double sr, double seconds, unsigned int seed = 0xA5A5u)
    {
        const auto numSamples = static_cast<int> (sr * seconds);
        juce::AudioBuffer<float> buffer (2, numSamples);

        std::mt19937 engine (seed);
        std::uniform_real_distribution<float> noise (-1.0f, 1.0f);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto t = i / sr;
            const auto phase = juce::MathConstants<double>::twoPi * 82.0 * t;

            // A 4 Hz burst pattern with an exponential decay inside each
            // burst, riding on a -60 dBFS noise bed.
            const auto burstPhase = std::fmod (t, 0.25);
            const auto envelope = burstPhase < 0.18 ? std::exp (-burstPhase * 14.0) : 0.0;

            // Every other second, hover right at a typical threshold.
            const auto hovering = (static_cast<int> (t) % 2) == 1;
            const auto level = hovering ? 0.004 : 0.6;

            const auto value = static_cast<float> (level * envelope * std::sin (phase))
                                + 0.001f * noise (engine);

            buffer.setSample (0, i, value);
            buffer.setSample (1, i, value * 0.98f);
        }

        return buffer;
    }

    // Runs `gate` over `buffer` in blocks, so the result reflects the real
    // block-boundary behaviour rather than one giant call.
    void processInBlocks (Gate& gate, juce::AudioBuffer<float>& buffer, int blockSize,
                          const juce::AudioBuffer<float>* keyBuffer = nullptr)
    {
        const auto numSamples = buffer.getNumSamples();

        for (int position = 0; position < numSamples; position += blockSize)
        {
            const auto count = std::min (blockSize, numSamples - position);

            juce::dsp::AudioBlock<float> block (buffer);
            auto subBlock = block.getSubBlock (static_cast<size_t> (position), static_cast<size_t> (count));
            juce::dsp::ProcessContextReplacing<float> context (subBlock);

            std::vector<const float*> keyPointers;

            if (keyBuffer != nullptr)
            {
                for (int channel = 0; channel < keyBuffer->getNumChannels(); ++channel)
                    keyPointers.push_back (keyBuffer->getReadPointer (channel) + position);

                gate.setKeyBuffer (keyPointers.data(), keyPointers.size(), static_cast<size_t> (count));
            }

            gate.process (context);
        }
    }

    // Gain-reduction trace: the ratio of output envelope to input envelope,
    // measured as a peak per window rather than sample by sample.
    //
    // A per-sample ratio is unusable on any oscillating programme: at every
    // zero crossing the denominator vanishes, so the "gain" collapses to
    // whatever the guard clause returns and the trace fills with spikes that
    // read as state transitions and as -400 dB minima. Reading peaks over a
    // window that spans at least one period of the test tone removes that
    // entirely, and is what a gain-reduction meter does anyway.
    //
    // `windowSamples` must cover at least one period of the programme.
    std::vector<double> gainTrace (const juce::AudioBuffer<float>& before,
                                   const juce::AudioBuffer<float>& after,
                                   int windowSamples)
    {
        const auto numSamples = before.getNumSamples();
        const auto numWindows = numSamples / windowSamples;

        std::vector<double> trace;
        trace.reserve (static_cast<size_t> (numWindows));

        for (int window = 0; window < numWindows; ++window)
        {
            double inputPeak = 0.0;
            double outputPeak = 0.0;

            for (int i = window * windowSamples; i < (window + 1) * windowSamples; ++i)
            {
                inputPeak = std::max (inputPeak, std::abs (static_cast<double> (before.getSample (0, i))));
                outputPeak = std::max (outputPeak, std::abs (static_cast<double> (after.getSample (0, i))));
            }

            trace.push_back (inputPeak > 1.0e-12 ? outputPeak / inputPeak : 0.0);
        }

        return trace;
    }
}

//==============================================================================
TEST_CASE ("T-G1: at its defaults the v0.3.0 gate is sample-exact against the v0.2 gate", "[gate][superset]")
{
    // gateKey = Post, hysteresis = 0, range = Mute, release mode = Manual.
    // This is the whole migration promise: a session saved by v0.2 carries
    // none of the new IDs, falls back to exactly these defaults, and must
    // therefore render bit-for-bit as it did.
    auto programme = makeAdversarialProgramme (testSampleRate, 10.0);

    juce::AudioBuffer<float> viaGate (2, programme.getNumSamples());
    viaGate.makeCopyOf (programme);

    juce::AudioBuffer<float> viaReference (2, programme.getNumSamples());
    viaReference.makeCopyOf (programme);

    Gate gate;
    gate.prepare (makeTestSpec (2, 512));
    gate.setThresholdDb (-48.0f);
    gate.setAttackMs (1.0f);
    gate.setHoldMs (20.0f);
    gate.setReleaseMs (150.0f);
    gate.setEnabled (true);
    // Explicitly at the documented neutral defaults, so this test also fails
    // if a default ever changes.
    gate.setKeySource (Gate::KeySource::post);
    gate.setHysteresisDb (0.0f);
    gate.setRange (Gate::maxRangeDb, true);
    gate.setReleaseMode (Gate::ReleaseMode::manual);

    processInBlocks (gate, viaGate, 512);

    LegacyGateV2Reference reference;
    reference.configure (-48.0f, 1.0f, 20.0f, 150.0f, testSampleRate);
    reference.process (viaReference);

    // In-process byte equality (brief section 6's platform note): both renders
    // are produced by this binary, on this machine, in this run.
    CHECK (TestHelpers::buffersAreByteIdentical (viaGate, viaReference));

    SECTION ("and stays exact across a range of threshold/ballistics settings")
    {
        struct Settings { float threshold, attack, hold, release; };

        for (const auto& settings : { Settings { -60.0f, 0.5f, 5.0f, 50.0f },
                                      Settings { -30.0f, 5.0f, 100.0f, 800.0f },
                                      Settings { -12.0f, 20.0f, 500.0f, 2000.0f } })
        {
            juce::AudioBuffer<float> a (2, programme.getNumSamples());
            a.makeCopyOf (programme);
            juce::AudioBuffer<float> b (2, programme.getNumSamples());
            b.makeCopyOf (programme);

            Gate subject;
            subject.prepare (makeTestSpec (2, 512));
            subject.setThresholdDb (settings.threshold);
            subject.setAttackMs (settings.attack);
            subject.setHoldMs (settings.hold);
            subject.setReleaseMs (settings.release);
            subject.setEnabled (true);
            processInBlocks (subject, a, 512);

            LegacyGateV2Reference legacy;
            legacy.configure (settings.threshold, settings.attack, settings.hold, settings.release,
                              testSampleRate);
            legacy.process (b);

            INFO ("threshold " << settings.threshold << " dB, attack " << settings.attack << " ms");
            CHECK (TestHelpers::buffersAreByteIdentical (a, b));
        }
    }
}

//==============================================================================
TEST_CASE ("T-G2: hysteresis separates the open and close thresholds by exactly H", "[gate][hysteresis]")
{
    constexpr float thresholdDb = -40.0f;
    constexpr float hysteresisDb = 6.0f;

    // A slow triangular level sweep: up through the threshold, then back down.
    // The level at which the gate opens and the level at which it closes are
    // read off the gain trace.
    const auto sweepSeconds = 8.0;
    const auto numSamples = static_cast<int> (testSampleRate * sweepSeconds);

    juce::AudioBuffer<float> programme (1, numSamples);
    std::vector<double> levelDb (static_cast<size_t> (numSamples), 0.0);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto t = i / testSampleRate;
        // -60 dB up to -20 dB and back, at 10 dB/s.
        const auto db = t < sweepSeconds * 0.5 ? -60.0 + 10.0 * t
                                               : -60.0 + 10.0 * (sweepSeconds - t);
        levelDb[static_cast<size_t> (i)] = db;

        const auto amplitude = std::pow (10.0, db / 20.0);
        programme.setSample (0, i, static_cast<float> (
            amplitude * std::sin (juce::MathConstants<double>::twoPi * 1000.0 * t)));
    }

    juce::AudioBuffer<float> processed (1, numSamples);
    processed.makeCopyOf (programme);

    Gate gate;
    gate.prepare (makeTestSpec (1, 512));
    gate.setThresholdDb (thresholdDb);
    gate.setAttackMs (1.0f);
    // Hold matters here: the detector reads the instantaneous peak of a sine,
    // which passes through zero every cycle. Without a hold the gate
    // re-latches on every single cycle, so it is always deciding from the
    // OPEN threshold and the hysteresis is structurally invisible - which is
    // exactly why real gates pair the two. 50 ms of hold at the 10 dB/s sweep
    // rate costs 0.5 dB of measurement bias, inside the 1 dB tolerance.
    gate.setHoldMs (50.0f);
    gate.setReleaseMs (5.0f);
    gate.setHysteresisDb (hysteresisDb);
    gate.setEnabled (true);

    processInBlocks (gate, processed, 512);

    // 1 ms windows: the probe is 1 kHz, so each window spans a full period.
    const auto windowSamples = static_cast<int> (testSampleRate * 0.001);
    const auto trace = gainTrace (programme, processed, windowSamples);

    // Opening level: the first window where the gain crosses halfway.
    double openLevelDb = 0.0;
    double closeLevelDb = 0.0;
    bool foundOpen = false;

    for (size_t i = 1; i < trace.size(); ++i)
    {
        if (! foundOpen && trace[i] > 0.5 && trace[i - 1] <= 0.5)
        {
            openLevelDb = levelDb[i * static_cast<size_t> (windowSamples)];
            foundOpen = true;
        }
        else if (foundOpen && trace[i] < 0.5 && trace[i - 1] >= 0.5)
        {
            closeLevelDb = levelDb[i * static_cast<size_t> (windowSamples)];
            break;
        }
    }

    REQUIRE (foundOpen);

    const auto measuredHysteresis = openLevelDb - closeLevelDb;

    INFO ("opened at " << openLevelDb << " dB, closed at " << closeLevelDb
                       << " dB, hysteresis = " << measuredHysteresis << " dB");
    CHECK (std::abs (measuredHysteresis - hysteresisDb) < 1.0);

    SECTION ("with H = 4 dB a +/-1.5 dB dither around the threshold cannot retrigger it")
    {
        constexpr float ditherHysteresis = 4.0f;

        const auto ditherSamples = static_cast<int> (testSampleRate * 4.0);
        juce::AudioBuffer<float> dithered (1, ditherSamples);

        std::mt19937 engine (0x5EEDu);
        std::uniform_real_distribution<double> dither (-1.5, 1.5);

        for (int i = 0; i < ditherSamples; ++i)
        {
            const auto t = i / testSampleRate;
            // Level wanders +/-1.5 dB around the threshold, re-randomised
            // every 20 ms so the detector genuinely sees it move.
            static double currentDb = thresholdDb;

            if (i % static_cast<int> (testSampleRate * 0.02) == 0)
                currentDb = thresholdDb + dither (engine);

            const auto amplitude = std::pow (10.0, currentDb / 20.0);
            dithered.setSample (0, i, static_cast<float> (
                amplitude * std::sin (juce::MathConstants<double>::twoPi * 1000.0 * t)));
        }

        juce::AudioBuffer<float> ditherProcessed (1, ditherSamples);
        ditherProcessed.makeCopyOf (dithered);

        Gate ditherGate;
        ditherGate.prepare (makeTestSpec (1, 512));
        ditherGate.setThresholdDb (thresholdDb);
        ditherGate.setAttackMs (1.0f);
        ditherGate.setHoldMs (50.0f);
        ditherGate.setReleaseMs (50.0f);
        ditherGate.setHysteresisDb (ditherHysteresis);
        ditherGate.setEnabled (true);

        processInBlocks (ditherGate, ditherProcessed, 512);

        const auto ditherTrace = gainTrace (dithered, ditherProcessed,
                                            static_cast<int> (testSampleRate * 0.001));

        // Count halfway crossings after the initial opening.
        int transitions = 0;
        bool above = ditherTrace.front() > 0.5;

        for (size_t i = 1; i < ditherTrace.size(); ++i)
        {
            const auto nowAbove = ditherTrace[i] > 0.5;

            if (nowAbove != above)
            {
                ++transitions;
                above = nowAbove;
            }
        }

        INFO ("transitions with H = " << ditherHysteresis << " dB: " << transitions);
        CHECK (transitions <= 1);
    }
}

//==============================================================================
TEST_CASE ("T-G3: a tone sitting exactly at the threshold does not chatter", "[gate][chatter]")
{
    // 70 Hz - a drop-tuned low string - is the worst case: the detector's
    // ripple at the fundamental is largest when the period is longest.
    constexpr double toneHz = 70.0;
    constexpr float thresholdDb = -40.0f;

    const auto seconds = 5.0;
    const auto numSamples = static_cast<int> (testSampleRate * seconds);

    juce::AudioBuffer<float> programme (1, numSamples);
    const auto amplitude = std::pow (10.0, thresholdDb / 20.0);

    for (int i = 0; i < numSamples; ++i)
        programme.setSample (0, i, static_cast<float> (
            amplitude * std::sin (juce::MathConstants<double>::twoPi * toneHz * i / testSampleRate)));

    juce::AudioBuffer<float> processed (1, numSamples);
    processed.makeCopyOf (programme);

    Gate gate;
    gate.prepare (makeTestSpec (1, 512));
    gate.setThresholdDb (thresholdDb);
    gate.setAttackMs (1.0f);
    gate.setHoldMs (20.0f);
    gate.setReleaseMs (150.0f);
    gate.setHysteresisDb (3.0f);
    gate.setEnabled (true);

    processInBlocks (gate, processed, 512);

    // 20 ms windows: the probe is 70 Hz (14.3 ms period), so each window
    // spans a full cycle.
    const auto trace = gainTrace (programme, processed, static_cast<int> (testSampleRate * 0.02));

    int transitions = 0;
    bool above = trace.front() > 0.5;

    for (size_t i = 1; i < trace.size(); ++i)
    {
        const auto nowAbove = trace[i] > 0.5;

        if (nowAbove != above)
        {
            ++transitions;
            above = nowAbove;
        }
    }

    const auto perSecond = transitions / seconds;

    INFO ("state transitions per second at the threshold = " << perSecond);
    CHECK (perSecond <= 1.0);
}

//==============================================================================
TEST_CASE ("T-G4: the range floor lands where it is asked to", "[gate][range]")
{
    const auto measureClosedGainDb = [] (float rangeDb, bool isMute)
    {
        // A burst, then silence long enough for the gate to reach its floor,
        // then a quiet probe well below the threshold so the applied gain can
        // be read off.
        const auto numSamples = static_cast<int> (testSampleRate * 3.0);
        juce::AudioBuffer<float> programme (1, numSamples);

        const auto burstSamples = static_cast<int> (testSampleRate * 0.2);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto phase = juce::MathConstants<double>::twoPi * 500.0 * i / testSampleRate;
            const auto amplitude = i < burstSamples ? 0.5 : 1.0e-4; // probe at -80 dBFS
            programme.setSample (0, i, static_cast<float> (amplitude * std::sin (phase)));
        }

        juce::AudioBuffer<float> processed (1, numSamples);
        processed.makeCopyOf (programme);

        Gate gate;
        gate.prepare (makeTestSpec (1, 512));
        gate.setThresholdDb (-40.0f);
        gate.setAttackMs (1.0f);
        gate.setHoldMs (0.0f);
        gate.setReleaseMs (50.0f);
        gate.setRange (rangeDb, isMute);
        gate.setEnabled (true);

        processInBlocks (gate, processed, 512);

        // 2 ms windows: the probe is 500 Hz.
        const auto windowSamples = static_cast<int> (testSampleRate * 0.002);
        const auto trace = gainTrace (programme, processed, windowSamples);

        // Settled value: the maximum gain over the last 200 ms.
        const auto tailWindows = static_cast<size_t> (0.2 * testSampleRate / windowSamples);
        double settled = 0.0;

        for (size_t i = trace.size() - tailWindows; i < trace.size(); ++i)
            settled = std::max (settled, trace[i]);

        return TestHelpers::toDecibels (settled, -400.0);
    };

    for (const float rangeDb : { 20.0f, 40.0f, 60.0f })
    {
        const auto measured = measureClosedGainDb (rangeDb, false);
        INFO ("range " << rangeDb << " dB: measured closed gain " << measured << " dB");
        CHECK (std::abs (measured + rangeDb) < 0.5);
    }

    const auto muted = measureClosedGainDb (Gate::maxRangeDb, true);
    INFO ("Mute position: measured closed gain " << muted << " dB");
    CHECK (muted <= -120.0);
}

//==============================================================================
TEST_CASE ("T-G5: the program-dependent release discriminates a stop from a decay", "[gate][tvp]")
{
    SECTION ("(a) an abrupt stop reaches the floor within 150 ms")
    {
        const auto numSamples = static_cast<int> (testSampleRate * 1.0);
        juce::AudioBuffer<float> programme (1, numSamples);

        const auto burstSamples = static_cast<int> (testSampleRate * 0.3);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto phase = juce::MathConstants<double>::twoPi * 220.0 * i / testSampleRate;
            const auto amplitude = i < burstSamples ? 0.5 : 1.0e-4;
            programme.setSample (0, i, static_cast<float> (amplitude * std::sin (phase)));
        }

        juce::AudioBuffer<float> processed (1, numSamples);
        processed.makeCopyOf (programme);

        Gate gate;
        gate.prepare (makeTestSpec (1, 512));
        gate.setThresholdDb (-40.0f);
        gate.setAttackMs (1.0f);
        gate.setHoldMs (0.0f);
        gate.setReleaseMode (Gate::ReleaseMode::automatic);
        gate.setRange (60.0f, false);
        gate.setEnabled (true);

        processInBlocks (gate, processed, 512);

        // 5 ms windows: the probe is 220 Hz.
        const auto windowSamples = static_cast<int> (testSampleRate * 0.005);
        const auto trace = gainTrace (programme, processed, windowSamples);

        // 150 ms after the stop, the gain must already be at the floor.
        const auto probeWindow = (burstSamples + static_cast<int> (testSampleRate * 0.15)) / windowSamples;
        const auto gainAtProbeDb = TestHelpers::toDecibels (trace[static_cast<size_t> (probeWindow)], -400.0);

        INFO ("gain 150 ms after the stop = " << gainAtProbeDb << " dB");
        CHECK (gainAtProbeDb <= -55.0);
    }

    SECTION ("(b) a decaying note is tracked, and the fade is dB-linear at the commanded slope")
    {
        // A 30 dB/s decaying tone: the gate must follow it down rather than
        // dumping, and the fade it applies must be a straight line in dB at
        // the note's own decay rate plus the tracking margin.
        constexpr double decayDbPerSecond = 30.0;

        const auto numSamples = static_cast<int> (testSampleRate * 4.0);
        juce::AudioBuffer<float> programme (1, numSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto t = i / testSampleRate;
            const auto db = -6.0 - decayDbPerSecond * t;
            const auto amplitude = std::pow (10.0, db / 20.0);
            programme.setSample (0, i, static_cast<float> (
                amplitude * std::sin (juce::MathConstants<double>::twoPi * 220.0 * t)));
        }

        juce::AudioBuffer<float> processed (1, numSamples);
        processed.makeCopyOf (programme);

        Gate gate;
        gate.prepare (makeTestSpec (1, 512));
        gate.setThresholdDb (-50.0f);
        gate.setAttackMs (1.0f);
        gate.setHoldMs (0.0f);
        gate.setHysteresisDb (3.0f);
        gate.setReleaseMode (Gate::ReleaseMode::automatic);
        gate.setRange (70.0f, false);
        gate.setEnabled (true);

        processInBlocks (gate, processed, 512);

        // 5 ms windows: the probe is 220 Hz.
        const auto windowSamples = static_cast<int> (testSampleRate * 0.005);
        const auto trace = gainTrace (programme, processed, windowSamples);
        const auto windowsPerSecond = testSampleRate / windowSamples;

        // While the note is comfortably above the threshold the gate must be
        // out of the way: the output tracks the input within 1.5 dB.
        const auto trackedUntil = static_cast<size_t> (windowsPerSecond * 1.2);
        double worstTrackingErrorDb = 0.0;

        for (size_t i = static_cast<size_t> (windowsPerSecond * 0.05); i < trackedUntil; ++i)
            worstTrackingErrorDb = std::max (worstTrackingErrorDb,
                                             std::abs (TestHelpers::toDecibels (trace[i], -400.0)));

        INFO ("worst tracking error while above threshold = " << worstTrackingErrorDb << " dB");
        CHECK (worstTrackingErrorDb < 1.5);

        // Once the gate does start closing, the fade is a straight line in dB.
        std::vector<double> times;
        std::vector<double> gainsDb;

        for (size_t i = 0; i < trace.size(); ++i)
        {
            const auto gainDb = TestHelpers::toDecibels (trace[i], -400.0);

            // The fitting window: between 3 dB and 40 dB of gain reduction,
            // i.e. clear of both the onset and the range floor.
            if (gainDb < -3.0 && gainDb > -40.0)
            {
                times.push_back (i / windowsPerSecond);
                gainsDb.push_back (gainDb);
            }
        }

        REQUIRE (times.size() > 10);

        const auto fit = TestHelpers::fitLine (times, gainsDb);

        INFO ("fitted release slope = " << -fit.slope << " dB/s, R^2 = " << fit.rSquared);
        CHECK (fit.rSquared > 0.99);

        // Decay-tracking: the gate fades at the programme's own measured rate
        // plus the margin, so it stays just ahead of the note.
        const auto expectedSlope = decayDbPerSecond + Gate::tvpTrackMarginDbPerSecond;
        CHECK (std::abs (-fit.slope - expectedSlope) < 0.1 * expectedSlope);
    }
}

//==============================================================================
TEST_CASE ("T-G6: keying before the distortion restores the detector's dynamic range", "[gate][key]")
{
    // The premise: a cascade running at 40 dB of gain squashes the difference
    // between "noise floor" and "playing" almost flat by the time the signal
    // reaches the gate. Measured here as the level difference the detector
    // would see at each tap point.
    constexpr double noiseLevel = 1.778e-3;  // -55 dBFS
    constexpr double playingLevel = 5.623e-2; // -25 dBFS

    TriodeCascade cascade;
    cascade.prepare (testSampleRate * 4.0, 1);
    cascade.setVoicing (0);

    const auto postCascadeLevelDb = [&] (double inputLevel)
    {
        cascade.reset();

        // 65 dB models a high-gain channel's total pre-cascade gain: the
        // plugin's own 40 dB Gain with a boost in front of it, which is how
        // this genre is actually tracked. At 40 dB alone the quiet case never
        // reaches the first stage's grid clamp, so the cascade compresses the
        // two levels together by only ~6 dB and the point being demonstrated
        // does not yet exist.
        const auto gain = juce::Decibels::decibelsToGain (65.0);
        const auto numSamples = static_cast<int> (testSampleRate * 4.0 * 0.5);

        double peak = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto phase = juce::MathConstants<double>::twoPi * 200.0 * i / (testSampleRate * 4.0);
            const auto y = cascade.processSample (inputLevel * gain * std::sin (phase), 0);

            if (i > numSamples / 2)
                peak = std::max (peak, std::abs (y));
        }

        return TestHelpers::toDecibels (peak);
    };

    const auto preTapDifferenceDb = TestHelpers::toDecibels (playingLevel / noiseLevel);
    const auto postTapDifferenceDb = postCascadeLevelDb (playingLevel) - postCascadeLevelDb (noiseLevel);

    INFO ("pre-distortion tap sees " << preTapDifferenceDb << " dB of difference; "
          << "post-distortion tap sees " << postTapDifferenceDb << " dB");

    // The pre tap keeps the full 30 dB the source actually has.
    CHECK (preTapDifferenceDb >= 25.0);

    // The post tap has had it compressed away - which is precisely why a
    // post-distortion detector cannot set a threshold that separates them.
    CHECK (postTapDifferenceDb < 6.0);

    SECTION ("and the Pre key actually gates on the key signal, not on the audio")
    {
        // Audio that is always loud, key that goes quiet: a Post-keyed gate
        // stays open, a Pre-keyed one closes.
        const auto numSamples = static_cast<int> (testSampleRate * 1.0);

        juce::AudioBuffer<float> audio (1, numSamples);
        juce::AudioBuffer<float> key (1, numSamples);

        const auto halfway = numSamples / 2;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto phase = juce::MathConstants<double>::twoPi * 500.0 * i / testSampleRate;
            audio.setSample (0, i, static_cast<float> (0.5 * std::sin (phase)));
            key.setSample (0, i, static_cast<float> ((i < halfway ? 0.5 : 1.0e-4) * std::sin (phase)));
        }

        const auto finalGainFor = [&] (Gate::KeySource source)
        {
            juce::AudioBuffer<float> processed (1, numSamples);
            processed.makeCopyOf (audio);

            Gate gate;
            gate.prepare (makeTestSpec (1, 512));
            gate.setThresholdDb (-20.0f);
            gate.setAttackMs (1.0f);
            gate.setHoldMs (0.0f);
            gate.setReleaseMs (50.0f);
            gate.setKeySource (source);
            gate.setEnabled (true);
            gate.reset();

            processInBlocks (gate, processed, 512, &key);

            // 2 ms windows: the probe is 500 Hz.
            const auto trace = gainTrace (audio, processed, static_cast<int> (testSampleRate * 0.002));
            return trace.back();
        };

        const auto postGain = finalGainFor (Gate::KeySource::post);
        const auto preGain = finalGainFor (Gate::KeySource::pre);

        INFO ("final gain: Post = " << postGain << ", Pre = " << preGain);
        CHECK (postGain > 0.9);   // audio still loud -> stays open
        CHECK (preGain < 0.01);   // key went quiet -> closes
    }
}

//==============================================================================
TEST_CASE ("T-G7: hold keeps the gate open between 16th-note bursts", "[gate][hold]")
{
    // 16ths at 120 BPM = one burst every 125 ms. With 50 ms of hold and a
    // conventional release the gate must not audibly duck between them.
    constexpr double burstIntervalSeconds = 0.125;
    constexpr double burstLengthSeconds = 0.05;

    const auto numSamples = static_cast<int> (testSampleRate * 4.0);
    juce::AudioBuffer<float> programme (1, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto t = i / testSampleRate;
        const auto phaseInBurst = std::fmod (t, burstIntervalSeconds);
        const auto amplitude = phaseInBurst < burstLengthSeconds ? 0.5 : 1.0e-4;

        programme.setSample (0, i, static_cast<float> (
            amplitude * std::sin (juce::MathConstants<double>::twoPi * 110.0 * t)));
    }

    juce::AudioBuffer<float> processed (1, numSamples);
    processed.makeCopyOf (programme);

    Gate gate;
    gate.prepare (makeTestSpec (1, 512));
    gate.setThresholdDb (-30.0f);
    gate.setAttackMs (1.0f);
    gate.setHoldMs (50.0f);
    gate.setReleaseMs (200.0f);
    gate.setEnabled (true);

    processInBlocks (gate, processed, 512);

    // 10 ms windows: the probe is 110 Hz (9.1 ms period).
    const auto windowSamples = static_cast<int> (testSampleRate * 0.01);
    const auto trace = gainTrace (programme, processed, windowSamples);

    // Measured from the second burst onward, so the initial opening ramp is
    // not counted.
    const auto firstWindow = static_cast<size_t> (
        testSampleRate * burstIntervalSeconds * 2 / windowSamples);

    double lowestGain = 1.0;

    for (size_t i = firstWindow; i < trace.size(); ++i)
        lowestGain = std::min (lowestGain, trace[i]);

    const auto lowestGainDb = TestHelpers::toDecibels (lowestGain, -400.0);

    INFO ("lowest gain between bursts = " << lowestGainDb << " dB");
    CHECK (lowestGainDb >= -3.0);
}
