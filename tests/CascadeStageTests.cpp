#include "dsp/CascadeStage.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int testBlockSize = 4096;

    juce::dsp::ProcessSpec makeTestSpec()
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (testBlockSize);
        spec.numChannels = 2;
        return spec;
    }
}

TEST_CASE ("CascadeStage: NaN interstage corner frequency does not poison the output with NaN", "[dsp][cascadestage][nan]")
{
    // Regression test for GitHub issue #14: CascadeStage.cpp carries its own
    // duplicate of clampBelowNyquist() (see TenebraeEngine.cpp's version and
    // its EngineTests.cpp NaN test for the fuller writeup of the same gap),
    // which also relied solely on juce::jlimit() and was therefore not
    // NaN-safe. highPassFrequencyHz/lowPassFrequencyHz are fixed per-stage
    // voicing constants supplied at construction (not user-automatable), so
    // unlike TenebraeEngine's Tight frequency this can't currently be
    // reached via live host automation - but a NaN constructor argument
    // would previously have produced entirely-NaN interstage filter
    // coefficients at prepare() time, and therefore an entirely-NaN output
    // block from the very first sample.
    CascadeStage stage (0.2f, std::numeric_limits<float>::quiet_NaN(), 8000.0f);

    const auto spec = makeTestSpec();
    stage.prepare (spec);
    stage.setDriveDb (6.0f);

    juce::AudioBuffer<float> buffer (2, testBlockSize);
    TestHelpers::fillWithSine (buffer, testSampleRate, 1000.0, 0.5f);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    CHECK_NOTHROW (stage.process (context));

    CHECK (TestHelpers::allSamplesFinite (buffer));
}
