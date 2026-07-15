#include "dsp/TenebraeEngine.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int testBlockSize = 8192; // large single block: keeps the null-test
                                         // bookkeeping below simple by avoiding
                                         // multi-block accounting.
    constexpr double testFrequencyHz = 1000.0;

    juce::dsp::ProcessSpec makeTestSpec (int numChannels)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (testBlockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }
}

TEST_CASE ("Engine null test: 0% mix nulls against the input once shifted by latency", "[dsp][engine][null]")
{
    TenebraeEngine engine;

    // Parameters other than Mix are deliberately set to non-neutral values:
    // a true null test has to prove the *entire* wet chain (Tight HPF,
    // Gain, cascade, tone stack, Level) is bypassed, not just that it
    // happens to be quiet at default settings.
    engine.setMixProportion (0.0f);
    engine.setGainDb (30.0f);
    engine.setTightFrequencyHz (200.0f);
    engine.setBassDb (10.0f);
    engine.setMidDb (-8.0f);
    engine.setTrebleDb (6.0f);
    engine.setLevelDb (8.0f);

    const auto spec = makeTestSpec (2);
    engine.prepare (spec);

    const auto latency = engine.getLatencySamples();
    REQUIRE (latency >= 0);
    // Sanity bound: the oversampling latency must be well inside both the
    // DryWetMixer's fixed dry-delay capacity (1024, see TenebraeEngine.h)
    // and the test block size, or the overlap window below would be
    // meaningless.
    REQUIRE (latency < testBlockSize / 2);

    juce::AudioBuffer<float> reference (2, testBlockSize);
    TestHelpers::fillWithSine (reference, testSampleRate, testFrequencyHz, 0.5f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    engine.process (block);

    const auto overlapLength = testBlockSize - latency;
    REQUIRE (overlapLength > testBlockSize / 2);

    // < -90 dBFS residual, in linear amplitude.
    constexpr float tolerance = 3.1623e-5f; // 10^(-90/20)

    for (int channel = 0; channel < reference.getNumChannels(); ++channel)
    {
        const auto* refData = reference.getReadPointer (channel);
        const auto* outData = processed.getReadPointer (channel);

        float maxResidual = 0.0f;

        for (int i = 0; i < overlapLength; ++i)
            maxResidual = std::max (maxResidual, std::abs (outData[latency + i] - refData[i]));

        CHECK (maxResidual < tolerance);
    }
}

TEST_CASE ("Engine reset() clears filter/oversampler/delay/cascade state without crashing", "[dsp][engine]")
{
    TenebraeEngine engine;
    engine.setGainDb (30.0f);
    engine.setMixProportion (1.0f);

    const auto spec = makeTestSpec (2);
    engine.prepare (spec);

    juce::AudioBuffer<float> buffer (2, testBlockSize);
    TestHelpers::fillWithSine (buffer, testSampleRate, testFrequencyHz, 0.9f);

    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);

    CHECK_NOTHROW (engine.reset());
    CHECK (TestHelpers::allSamplesFinite (buffer));

    // Processing again straight after reset() must not crash or produce
    // non-finite output.
    TestHelpers::fillWithSine (buffer, testSampleRate, testFrequencyHz, 0.9f);
    CHECK_NOTHROW (engine.process (block));
    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Engine: full-scale, maximum-gain input produces bounded, finite output", "[dsp][engine]")
{
    TenebraeEngine engine;
    engine.setGainDb (40.0f);
    engine.setTightFrequencyHz (300.0f);
    engine.setBassDb (15.0f);
    engine.setMidDb (15.0f);
    engine.setTrebleDb (15.0f);
    engine.setLevelDb (24.0f);
    engine.setMixProportion (1.0f);

    const auto spec = makeTestSpec (2);
    engine.prepare (spec);

    juce::AudioBuffer<float> buffer (2, testBlockSize);
    TestHelpers::fillWithSine (buffer, testSampleRate, testFrequencyHz, 1.0f);

    juce::dsp::AudioBlock<float> block (buffer);
    CHECK_NOTHROW (engine.process (block));

    CHECK (TestHelpers::allSamplesFinite (buffer));
    CHECK (TestHelpers::peakAbsolute (buffer) < 1000.0f); // sane bound, not just "finite"
}

TEST_CASE ("Engine: Voicing switch measurably changes the cascade's output (not a no-op)", "[dsp][engine][voicing]")
{
    // Renders the identical input through the Tight (0) and Loose (1)
    // voicings and checks the results actually differ - proving setVoicing()
    // reaches the cascade rather than being silently ignored. Both voicings
    // must independently stay finite and bounded too.
    auto renderWithVoicing = [] (int voicingIndex)
    {
        TenebraeEngine engine;
        engine.setGainDb (30.0f);
        engine.setMixProportion (1.0f);
        engine.setVoicing (voicingIndex);

        const auto spec = makeTestSpec (1);
        engine.prepare (spec);

        juce::AudioBuffer<float> buffer (1, testBlockSize);
        TestHelpers::fillWithSine (buffer, testSampleRate, testFrequencyHz, 0.7f);

        juce::dsp::AudioBlock<float> block (buffer);
        engine.process (block);

        return buffer;
    };

    const auto tightOutput = renderWithVoicing (0);
    const auto looseOutput = renderWithVoicing (1);

    REQUIRE (TestHelpers::allSamplesFinite (tightOutput));
    REQUIRE (TestHelpers::allSamplesFinite (looseOutput));
    CHECK (TestHelpers::peakAbsolute (tightOutput) < 1000.0f);
    CHECK (TestHelpers::peakAbsolute (looseOutput) < 1000.0f);

    // The two voicings use different asymmetry/interstage filtering, so a
    // sample-by-sample comparison should show a clear difference somewhere
    // in the block rather than being bit-identical.
    const auto* tightData = tightOutput.getReadPointer (0);
    const auto* looseData = looseOutput.getReadPointer (0);

    float maxAbsoluteDifference = 0.0f;
    for (int i = 0; i < testBlockSize; ++i)
        maxAbsoluteDifference = std::max (maxAbsoluteDifference, std::abs (tightData[i] - looseData[i]));

    CHECK (maxAbsoluteDifference > 1.0e-4f);
}

TEST_CASE ("Engine: Bright switch measurably changes the output and stays finite", "[dsp][engine][bright]")
{
    // Bright boosts a fixed high-shelf *before* the 3-stage saturating
    // cascade (each stage adding its own fixed 6-10 dB internal drive on
    // top of the user Gain), so by the time the signal reaches the output
    // its overall RMS is dominated by the cascade's own saturation, not by
    // a modest pre-boost - a directional "RMS goes up with Bright on" claim
    // does not hold in general (and doesn't in this specific case; verified
    // empirically). What must hold is that the switch reaches the signal
    // chain at all: the two renders should differ, sample by sample, not be
    // near-identical - same rationale as the Voicing test above.
    auto renderWithBright = [] (bool brightOn)
    {
        TenebraeEngine engine;
        engine.setGainDb (24.0f);
        engine.setMixProportion (1.0f);
        engine.setBright (brightOn);

        const auto spec = makeTestSpec (1);
        engine.prepare (spec);

        juce::AudioBuffer<float> buffer (1, testBlockSize);
        TestHelpers::fillWithSine (buffer, testSampleRate, 4000.0, 0.5f); // above the bright shelf corner

        juce::dsp::AudioBlock<float> block (buffer);
        engine.process (block);

        return buffer;
    };

    const auto offOutput = renderWithBright (false);
    const auto onOutput = renderWithBright (true);

    REQUIRE (TestHelpers::allSamplesFinite (offOutput));
    REQUIRE (TestHelpers::allSamplesFinite (onOutput));
    CHECK (TestHelpers::peakAbsolute (onOutput) < 1000.0f);

    const auto* offData = offOutput.getReadPointer (0);
    const auto* onData = onOutput.getReadPointer (0);

    float maxAbsoluteDifference = 0.0f;
    for (int i = 0; i < testBlockSize; ++i)
        maxAbsoluteDifference = std::max (maxAbsoluteDifference, std::abs (offData[i] - onData[i]));

    CHECK (maxAbsoluteDifference > 1.0e-4f);
}

TEST_CASE ("Engine: Voicing does not change the reported oversampling latency", "[dsp][engine][voicing][latency]")
{
    // Voicing only selects which preallocated cascade stage triplet runs;
    // both triplets are driven by the same oversampler, so the reported
    // latency must be identical regardless of which voicing is selected.
    TenebraeEngine tightEngine;
    tightEngine.setVoicing (0);
    tightEngine.prepare (makeTestSpec (2));

    TenebraeEngine looseEngine;
    looseEngine.setVoicing (1);
    looseEngine.prepare (makeTestSpec (2));

    CHECK (tightEngine.getLatencySamples() == looseEngine.getLatencySamples());
}

TEST_CASE ("Engine: NaN Tight frequency does not poison the wet path with NaN output", "[dsp][engine][nan]")
{
    // Regression test for GitHub issue #14: clampBelowNyquist() relied
    // solely on juce::jlimit(), which is not NaN-safe (both of its internal
    // comparisons evaluate false for NaN, so NaN falls through unchanged).
    // A NaN Tight target - reachable from a host handing over a NaN
    // normalised automation value (see the issue for the
    // AudioParameterFloat::setValue() path) - previously reached
    // juce::dsp::IIR::Coefficients::makeHighPass() as-is, producing an
    // {b0,b1,b2,a1,a2} coefficient set that is entirely NaN. Every output
    // sample of a biquad computed with NaN coefficients is NaN from the
    // very first sample of the block (NaN multiplied/added with anything is
    // NaN), so this reproduces reliably in a single block, on every
    // architecture - unlike the "does the NaN *persist* in filter state"
    // question, which is architecture-dependent (see the issue).
    TenebraeEngine engine;
    engine.setGainDb (20.0f);
    engine.setMixProportion (1.0f);

    const auto spec = makeTestSpec (2);
    engine.prepare (spec);

    // Force the Tight HPF coefficients to be (re)computed from a NaN target
    // on the very next process() call - tightFrequencySmoothed's ramp
    // reaches its (NaN) target well within one 8192-sample block.
    engine.setTightFrequencyHz (std::numeric_limits<float>::quiet_NaN());

    juce::AudioBuffer<float> buffer (2, testBlockSize);
    TestHelpers::fillWithSine (buffer, testSampleRate, testFrequencyHz, 0.5f);

    juce::dsp::AudioBlock<float> block (buffer);
    CHECK_NOTHROW (engine.process (block));

    CHECK (TestHelpers::allSamplesFinite (buffer));
}
