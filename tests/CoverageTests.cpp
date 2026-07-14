#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <random>

// M1 "broaden test coverage" additions: sample-rate sweeps across the full
// range hosts realistically request (44.1-192 kHz), explicit mono/stereo bus
// configuration coverage, and a longer-run NaN/Inf stability soak test on
// top of the existing 8-block robustness checks. Kept well under CI's
// per-test time budget even in a Debug build (a few hundred small blocks at
// most, no oversampling-heavy sample-rate exceeds 192 kHz).
namespace
{
    void setParam (TenebraeAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }
}

TEST_CASE ("Sample-rate sweep: 44.1-192 kHz all produce finite, latency-reporting output", "[coverage][samplerate]")
{
    // Covers the full range of sample rates a real host is likely to
    // request, including both 44.1 kHz-family and 48 kHz-family rates.
    static constexpr std::array<double, 6> sampleRates {
        44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0
    };

    for (const auto sampleRate : sampleRates)
    {
        TenebraeAudioProcessor processor;
        processor.prepareToPlay (sampleRate, 256);

        setParam (processor, ParamIDs::gain, 28.0f);
        setParam (processor, ParamIDs::mix, 100.0f);
        setParam (processor, ParamIDs::bright, 1.0f);
        setParam (processor, ParamIDs::voicing, 1.0f);
        setParam (processor, ParamIDs::toneVoice, 1.0f);

        // 8x oversampling always introduces some latency, at every sample
        // rate - a regression here would silently break host PDC.
        CHECK (processor.getLatencySamples() > 0);

        juce::AudioBuffer<float> buffer (2, 256);
        TestHelpers::fillWithSine (buffer, sampleRate, 220.0, 0.6f);

        juce::MidiBuffer midi;

        for (int block = 0; block < 4; ++block)
            CHECK_NOTHROW (processor.processBlock (buffer, midi));

        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Bus layouts: mono and stereo in/out are supported; mismatched/unsupported layouts are rejected", "[coverage][buses]")
{
    TenebraeAudioProcessor processor;

    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();
    const auto quad = juce::AudioChannelSet::quadraphonic();

    auto makeLayout = [] (juce::AudioChannelSet in, juce::AudioChannelSet out)
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (in);
        layout.outputBuses.add (out);
        return layout;
    };

    CHECK (processor.isBusesLayoutSupported (makeLayout (mono, mono)));
    CHECK (processor.isBusesLayoutSupported (makeLayout (stereo, stereo)));
    CHECK_FALSE (processor.isBusesLayoutSupported (makeLayout (mono, stereo)));
    CHECK_FALSE (processor.isBusesLayoutSupported (makeLayout (stereo, mono)));
    CHECK_FALSE (processor.isBusesLayoutSupported (makeLayout (quad, quad)));
}

TEST_CASE ("Mono bus configuration processes audio without crashing or producing NaN/Inf", "[coverage][buses]")
{
    TenebraeAudioProcessor processor;

    juce::AudioProcessor::BusesLayout monoLayout;
    monoLayout.inputBuses.add (juce::AudioChannelSet::mono());
    monoLayout.outputBuses.add (juce::AudioChannelSet::mono());

    REQUIRE (processor.isBusesLayoutSupported (monoLayout));
    REQUIRE (processor.setBusesLayout (monoLayout));

    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::gain, 30.0f);
    setParam (processor, ParamIDs::mix, 100.0f);

    juce::AudioBuffer<float> buffer (1, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 220.0, 0.7f);

    juce::MidiBuffer midi;

    for (int block = 0; block < 4; ++block)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
    // Regression guard for a real M1 bug: TenebraeEngine::outputLevel (a
    // juce::dsp::Gain) was never primed with a starting gain in prepare(),
    // and JUCE defaults a fresh Gain's smoothed value to *linear 0*, not
    // unity - so any prepare() not immediately preceded by setLevelDb()
    // produced permanent silence. The real plugin always seeds Level from
    // APVTS before prepare() (see PluginProcessor::prepareToPlay()), so this
    // never reached a shipped build, but it's exactly the kind of gap a
    // fresh bus configuration (like this mono test) could have exposed -
    // see the lastLevelDb fix in TenebraeEngine.h/.cpp.
    CHECK (TestHelpers::rms (buffer) > 0.01);
}

TEST_CASE ("Long-run automation soak: hundreds of blocks with continuous parameter movement stay finite", "[coverage][robustness][soak]")
{
    // A longer-running complement to RobustnessTests.cpp's 100-block sweep:
    // 400 blocks (~1.7s of audio at 48 kHz/256 samples per block) with every
    // parameter - including the M1 Voicing/Bright/Tone Voice switches -
    // moving continuously, checking finiteness throughout rather than only
    // at the end. Still comfortably under a minute even in a Debug build.
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    std::mt19937 rng (99);
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer (2, 256);

    for (int block = 0; block < 400; ++block)
    {
        setParam (processor, ParamIDs::tight, 20.0f + unit (rng) * 280.0f);
        setParam (processor, ParamIDs::gain, unit (rng) * 40.0f);
        setParam (processor, ParamIDs::bass, -15.0f + unit (rng) * 30.0f);
        setParam (processor, ParamIDs::mid, -15.0f + unit (rng) * 30.0f);
        setParam (processor, ParamIDs::treble, -15.0f + unit (rng) * 30.0f);
        setParam (processor, ParamIDs::level, -24.0f + unit (rng) * 48.0f);
        setParam (processor, ParamIDs::mix, unit (rng) * 100.0f);
        setParam (processor, ParamIDs::voicing, unit (rng) < 0.5f ? 0.0f : 1.0f);
        setParam (processor, ParamIDs::bright, unit (rng) < 0.5f ? 0.0f : 1.0f);
        setParam (processor, ParamIDs::toneVoice, (block % 3 == 0) ? 0.0f : (block % 3 == 1 ? 1.0f : 2.0f));

        TestHelpers::fillWithSine (buffer, 48000.0, 80.0 + unit (rng) * 6000.0, 0.8f);

        processor.processBlock (buffer, midi);

        if (! TestHelpers::allSamplesFinite (buffer))
        {
            FAIL ("Non-finite sample produced at block " << block);
        }
    }
}
