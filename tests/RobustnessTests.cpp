#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <random>

namespace
{
    void setParam (TenebraeAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }
}

TEST_CASE ("Silence produces silence (and no NaN/Inf)", "[robustness]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::gain, 40.0f);
    setParam (processor, ParamIDs::mix, 100.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Full-scale input at maximum gain produces no NaN/Inf", "[robustness]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::gain, 40.0f);
    setParam (processor, ParamIDs::tight, 300.0f);
    setParam (processor, ParamIDs::bass, 15.0f);
    setParam (processor, ParamIDs::mid, 15.0f);
    setParam (processor, ParamIDs::treble, 15.0f);
    setParam (processor, ParamIDs::level, 24.0f);
    setParam (processor, ParamIDs::mix, 100.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 1.0f);

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
    CHECK (TestHelpers::peakAbsolute (buffer) < 1000.0f); // sane bound, not just "finite"
}

TEST_CASE ("Denormal-range input produces no NaN/Inf output", "[robustness]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::gain, 20.0f);
    setParam (processor, ParamIDs::mix, 100.0f);

    constexpr int numSamples = 512;
    juce::AudioBuffer<float> buffer (2, numSamples);

    const auto denormalValue = std::numeric_limits<float>::denorm_min() * 4.0f;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        for (int sample = 0; sample < numSamples; ++sample)
            data[sample] = (sample % 2 == 0) ? denormalValue : -denormalValue;
    }

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Zero-sample buffer does not crash processBlock", "[robustness]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 0);
    juce::MidiBuffer midi;

    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (buffer.getNumSamples() == 0);
}

TEST_CASE ("Extreme parameter values at both range edges produce no NaN/Inf", "[robustness]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (44100.0, 256);

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;

    for (bool useMinimum : { true, false })
    {
        setParam (processor, ParamIDs::tight, useMinimum ? 20.0f : 300.0f);
        setParam (processor, ParamIDs::gain, useMinimum ? 0.0f : 40.0f);
        setParam (processor, ParamIDs::bass, useMinimum ? -15.0f : 15.0f);
        setParam (processor, ParamIDs::mid, useMinimum ? -15.0f : 15.0f);
        setParam (processor, ParamIDs::treble, useMinimum ? -15.0f : 15.0f);
        setParam (processor, ParamIDs::level, useMinimum ? -24.0f : 24.0f);
        setParam (processor, ParamIDs::mix, useMinimum ? 0.0f : 100.0f);
        setParam (processor, ParamIDs::voicing, useMinimum ? 0.0f : 1.0f);
        setParam (processor, ParamIDs::bright, useMinimum ? 0.0f : 1.0f);
        setParam (processor, ParamIDs::toneVoice, useMinimum ? 0.0f : 2.0f);

        TestHelpers::fillWithSine (buffer, 44100.0, 440.0, 0.8f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Every combination of Voicing/Bright/Tone Voice at max gain produces no NaN/Inf", "[robustness]")
{
    // The M1 additions (Voicing, Bright, Tone Voice) branch/switch fixed DSP
    // state rather than being continuous controls, so it's worth exhaustively
    // covering their combinations - unlike a continuous knob, a bug in one
    // specific combination wouldn't necessarily show up when the others are
    // swept independently.
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::gain, 40.0f);
    setParam (processor, ParamIDs::tight, 300.0f);
    setParam (processor, ParamIDs::bass, 15.0f);
    setParam (processor, ParamIDs::mid, 15.0f);
    setParam (processor, ParamIDs::treble, 15.0f);
    setParam (processor, ParamIDs::level, 24.0f);
    setParam (processor, ParamIDs::mix, 100.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    for (float voicing : { 0.0f, 1.0f })
    {
        for (float bright : { 0.0f, 1.0f })
        {
            for (float toneVoice : { 0.0f, 1.0f, 2.0f })
            {
                setParam (processor, ParamIDs::voicing, voicing);
                setParam (processor, ParamIDs::bright, bright);
                setParam (processor, ParamIDs::toneVoice, toneVoice);

                TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 1.0f);

                CHECK_NOTHROW (processor.processBlock (buffer, midi));
                CHECK (TestHelpers::allSamplesFinite (buffer));
                CHECK (TestHelpers::peakAbsolute (buffer) < 1000.0f);
            }
        }
    }
}

TEST_CASE ("Rapid parameter automation across many blocks produces no NaN/Inf", "[robustness]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);

    juce::MidiBuffer midi;

    for (int block = 0; block < 100; ++block)
    {
        setParam (processor, ParamIDs::tight, 20.0f + unit (rng) * 280.0f);
        setParam (processor, ParamIDs::gain, unit (rng) * 40.0f);
        setParam (processor, ParamIDs::bass, -15.0f + unit (rng) * 30.0f);
        setParam (processor, ParamIDs::mid, -15.0f + unit (rng) * 30.0f);
        setParam (processor, ParamIDs::treble, -15.0f + unit (rng) * 30.0f);
        setParam (processor, ParamIDs::level, -24.0f + unit (rng) * 48.0f);
        setParam (processor, ParamIDs::mix, unit (rng) * 100.0f);
        // Flip the discrete switches on roughly every other block, so the
        // sweep also exercises mid-stream Voicing/Bright/Tone Voice changes
        // (a step-response case the continuous-parameter sweep above can't
        // reach), not just their values at prepareToPlay() time.
        setParam (processor, ParamIDs::voicing, unit (rng) < 0.5f ? 0.0f : 1.0f);
        setParam (processor, ParamIDs::bright, unit (rng) < 0.5f ? 0.0f : 1.0f);
        setParam (processor, ParamIDs::toneVoice, std::floor (unit (rng) * 3.0f));

        juce::AudioBuffer<float> buffer (2, 256);
        TestHelpers::fillWithSine (buffer, 48000.0, 200.0 + unit (rng) * 4000.0, 0.7f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("reset() followed by processBlock does not crash", "[robustness]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::gain, 30.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.6f);
    juce::MidiBuffer midi;

    processor.processBlock (buffer, midi);

    CHECK_NOTHROW (processor.reset());

    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.6f);
    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (TestHelpers::allSamplesFinite (buffer));
}
