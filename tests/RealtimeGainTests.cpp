#include "dsp/RealtimeGain.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <vector>

// Tests for GitHub issue #12: juce::dsp::Gain<float>::process()'s
// multichannel branch alloca()s a scratch buffer proportional to the block
// size on every call (see RealtimeGain.h for the full writeup and the
// verified JUCE 8.0.14 source citation) - CascadeStage::driveGain runs on
// the 8x-oversampled block, making it the single largest stack allocation in
// the whole signal chain. RealtimeGain::process() replicates JUCE's own math
// exactly but with a caller-owned (prepare()-time-allocated) scratch buffer
// instead.
//
// A literal "fails before, passes after" reproduction of the underlying
// defect is not practically achievable in a portable, deterministic Catch2
// unit test: the failure mode is a stack overflow, whose threshold depends
// on the calling thread's stack size (audio-callback stacks vary by host/
// platform/plugin format and are not something this test binary's own
// thread reproduces), not on anything a CHECK() can observe short of an
// actual crash - which would be flaky/unsafe to depend on in CI. These
// tests instead pin down the two things that *are* deterministically
// testable: that RealtimeGain::process() is a numerically faithful
// drop-in replacement for the alloca() path it replaces (at exactly the
// block size the issue cites), and that it honours its documented
// scratch-capacity contract. CascadeStage's/TenebraeEngine's own existing
// test suite (EngineTests.cpp's null test and full-scale-gain test in
// particular, both already run at this same 8192-host/65536-oversampled
// stereo scale) continues to pass unchanged with RealtimeGain wired in,
// which is the practical regression guard for the actual fix.
namespace
{
    constexpr double testSampleRate = 48000.0;

    juce::dsp::ProcessSpec makeSpec (int numChannels, juce::uint32 maxBlockSize)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = maxBlockSize;
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }
}

TEST_CASE ("RealtimeGain::process() matches juce::dsp::Gain::process() at CascadeStage's own oversampled scale", "[dsp][realtimegain]")
{
    // 8192 (EngineTests.cpp's own host test block size) * 8 (oversampling
    // factor) = 65536 samples, stereo - the exact worst-case size issue #12
    // cites for CascadeStage::driveGain. Runs two identically configured
    // juce::dsp::Gain<float> instances - one through JUCE's own
    // Gain::process() (the alloca() path being replaced), one through
    // RealtimeGain::process() - over an identical input and checks the
    // outputs are bit-identical.
    constexpr int numSamples = 65536;
    constexpr int numChannels = 2;

    juce::dsp::Gain<float> referenceGain;
    juce::dsp::Gain<float> realtimeGain;

    const auto spec = makeSpec (numChannels, static_cast<juce::uint32> (numSamples));
    referenceGain.prepare (spec);
    realtimeGain.prepare (spec);
    referenceGain.setGainDecibels (6.0f);
    realtimeGain.setGainDecibels (6.0f);

    juce::AudioBuffer<float> referenceBuffer (numChannels, numSamples);
    TestHelpers::fillWithSine (referenceBuffer, testSampleRate, 1000.0, 0.5f);

    juce::AudioBuffer<float> realtimeBuffer;
    realtimeBuffer.makeCopyOf (referenceBuffer);

    juce::dsp::AudioBlock<float> referenceBlock (referenceBuffer);
    juce::dsp::ProcessContextReplacing<float> referenceContext (referenceBlock);
    referenceGain.process (referenceContext); // JUCE's own alloca() path

    juce::dsp::AudioBlock<float> realtimeBlock (realtimeBuffer);
    juce::dsp::ProcessContextReplacing<float> realtimeContext (realtimeBlock);
    std::vector<float> scratch (static_cast<size_t> (numSamples));
    RealtimeGain::process (realtimeGain, realtimeContext, scratch.data(), scratch.size());

    float maxAbsoluteDifference = 0.0f;

    for (int channel = 0; channel < numChannels; ++channel)
    {
        const auto* refData = referenceBuffer.getReadPointer (channel);
        const auto* rtData = realtimeBuffer.getReadPointer (channel);

        for (int i = 0; i < numSamples; ++i)
            maxAbsoluteDifference = std::max (maxAbsoluteDifference, std::abs (refData[i] - rtData[i]));
    }

    CHECK (juce::exactlyEqual (maxAbsoluteDifference, 0.0f));
}

TEST_CASE ("RealtimeGain::process() leaves samples beyond scratch capacity unprocessed rather than overrunning it", "[dsp][realtimegain]")
{
    // Documented contract (see RealtimeGain.h): if the caller-supplied
    // scratch buffer is smaller than the block - which should never happen
    // given CascadeStage/TenebraeEngine size it to their own prepared
    // maximum in prepare() (see issues #12/#13) - process() must clamp to
    // scratchCapacity rather than read/write past it.
    constexpr int numSamples = 64;
    constexpr int numChannels = 2;
    constexpr size_t scratchCapacity = 16;

    juce::dsp::Gain<float> gain;
    const auto spec = makeSpec (numChannels, static_cast<juce::uint32> (numSamples));
    gain.prepare (spec);
    gain.setGainDecibels (12.0f); // a non-unity gain, so "processed" is observable

    juce::AudioBuffer<float> buffer (numChannels, numSamples);
    TestHelpers::fillWithSine (buffer, testSampleRate, 1000.0, 0.5f);

    juce::AudioBuffer<float> reference;
    reference.makeCopyOf (buffer);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);

    std::array<float, scratchCapacity> scratch {};
    CHECK_NOTHROW (RealtimeGain::process (gain, context, scratch.data(), scratch.size()));

    CHECK (TestHelpers::allSamplesFinite (buffer));

    for (int channel = 0; channel < numChannels; ++channel)
    {
        const auto* data = buffer.getReadPointer (channel);
        const auto* refData = reference.getReadPointer (channel);

        // Samples beyond scratchCapacity must be untouched (still exactly
        // the unprocessed reference value).
        for (int i = static_cast<int> (scratchCapacity); i < numSamples; ++i)
            CHECK (juce::exactlyEqual (data[i], refData[i]));
    }
}
