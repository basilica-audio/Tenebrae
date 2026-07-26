#include "dsp/ToneStack.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <limits>

TEST_CASE ("scratch: NaN bass rms probe", "[scratch]")
{
    ToneStack toneStack;
    toneStack.setBassDb (std::numeric_limits<float>::quiet_NaN());

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 48000.0;
    spec.maximumBlockSize = 4096;
    spec.numChannels = 1;
    toneStack.prepare (spec);
    toneStack.updateCoefficients (4096);

    juce::AudioBuffer<float> buffer (1, 4096);
    TestHelpers::fillWithSine (buffer, 48000.0, 100.0, 0.5f);
    const auto inputRms = TestHelpers::rms (buffer);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    toneStack.process (context);

    const auto outputRms = TestHelpers::rms (buffer);
    std::printf ("SCRATCH: inputRms=%f outputRms=%f finite=%d\n", inputRms, outputRms, TestHelpers::allSamplesFinite (buffer));
}
