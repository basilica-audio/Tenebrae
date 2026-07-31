#include "PluginProcessor.h"
#include "dsp/TenebraeEngine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

TEST_CASE ("getLatencySamples() reports the oversampling latency after prepareToPlay", "[latency]")
{
    TenebraeAudioProcessor processor;

    // Before prepareToPlay, no engine has been prepared yet - JUCE's default
    // AudioProcessor latency is 0.
    CHECK (processor.getLatencySamples() == 0);

    processor.prepareToPlay (48000.0, 512);

    // Cross-check against a standalone engine prepared identically: the
    // processor must report exactly what the engine (i.e. the 8x
    // oversampler) computes, not an approximation of it.
    TenebraeEngine referenceEngine;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 48000.0;
    spec.maximumBlockSize = 512;
    spec.numChannels = 2;
    referenceEngine.prepare (spec);

    CHECK (processor.getLatencySamples() == referenceEngine.getLatencySamples());
    CHECK (processor.getLatencySamples() > 0); // 8x oversampling always has some latency
}

TEST_CASE ("Latency is stable across repeated prepareToPlay calls at the same sample rate", "[latency]")
{
    TenebraeAudioProcessor processor;

    processor.prepareToPlay (44100.0, 256);
    const auto firstLatency = processor.getLatencySamples();

    processor.prepareToPlay (44100.0, 256);
    const auto secondLatency = processor.getLatencySamples();

    CHECK (firstLatency == secondLatency);
}

TEST_CASE ("Latency updates correctly when the sample rate changes", "[latency]")
{
    TenebraeAudioProcessor processor;

    processor.prepareToPlay (44100.0, 512);
    const auto latencyAt44k = processor.getLatencySamples();

    processor.prepareToPlay (96000.0, 512);
    const auto latencyAt96k = processor.getLatencySamples();

    CHECK (latencyAt44k > 0);
    CHECK (latencyAt96k > 0);
    // Not asserting a specific ratio (that depends on JUCE's internal
    // half-band filter design), just that both are well-defined positive
    // latencies reported consistently.
}

//==============================================================================
// v0.3.0 latency assertions (brief section 6, T-L1/T-L2).

#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <vector>

namespace
{
    void setLatencyTestParam (TenebraeAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    // Measures the delay the plugin actually applies, by finding where an
    // impulse comes out. Run at Mix = 0 %, i.e. the delay-compensated dry
    // path, which is where the reported latency has to be exactly right.
    int measureImpulseDelay (TenebraeAudioProcessor& processor, double sampleRate, int blockSize)
    {
        processor.prepareToPlay (sampleRate, blockSize);

        constexpr int totalSamples = 8192;

        juce::AudioBuffer<float> block (2, blockSize);
        juce::MidiBuffer midi;

        std::vector<float> output;
        output.reserve (totalSamples);

        for (int position = 0; position < totalSamples; position += blockSize)
        {
            block.clear();

            if (position == 0)
            {
                block.setSample (0, 0, 1.0f);
                block.setSample (1, 0, 1.0f);
            }

            processor.processBlock (block, midi);

            for (int i = 0; i < blockSize; ++i)
                output.push_back (block.getSample (0, i));
        }

        int peakIndex = 0;
        float peak = 0.0f;

        for (int i = 0; i < static_cast<int> (output.size()); ++i)
        {
            if (std::abs (output[static_cast<size_t> (i)]) > peak)
            {
                peak = std::abs (output[static_cast<size_t> (i)]);
                peakIndex = i;
            }
        }

        return peakIndex;
    }
}

TEST_CASE ("T-L1: every engine and quality combination reports the latency it actually has", "[latency]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    struct Combination { float engine; float quality; const char* label; };

    for (const auto& combination : { Combination { 0.0f, 1.0f, "Classic" },
                                     Combination { 1.0f, 0.0f, "Triode/Eco" },
                                     Combination { 1.0f, 1.0f, "Triode/Standard" },
                                     Combination { 1.0f, 2.0f, "Triode/HQ" } })
    {
        TenebraeAudioProcessor processor;
        processor.prepareToPlay (sampleRate, blockSize);

        setLatencyTestParam (processor, ParamIDs::engine, combination.engine);
        setLatencyTestParam (processor, ParamIDs::quality, combination.quality);
        setLatencyTestParam (processor, ParamIDs::mix, 0.0f); // delay-compensated dry path
        setLatencyTestParam (processor, ParamIDs::gateOn, 0.0f);

        const auto measured = measureImpulseDelay (processor, sampleRate, blockSize);

        // One processBlock is needed before the engine has published the new
        // chain's latency (Engine/Quality take effect at the next block
        // boundary - see TenebraeEngine::processChunk).
        const auto reported = processor.getLatencySamples();

        INFO (combination.label << ": reported " << reported << ", measured " << measured);
        CHECK (reported > 0);
        CHECK (measured == reported);

        SECTION (juce::String (combination.label).toStdString() + ": Mix = 0 % is a clean passthrough")
        {
            // Everything but the impulse itself must be silent: at Mix = 0 %
            // the wet path contributes nothing, so the output is the input
            // shifted by exactly the reported latency.
            processor.prepareToPlay (sampleRate, blockSize);

            juce::AudioBuffer<float> block (2, blockSize);
            juce::MidiBuffer midi;

            double worstResidual = 0.0;

            for (int position = 0; position < 8192; position += blockSize)
            {
                TestHelpers::fillWithSine (block, sampleRate, 440.0, 0.5f, position);

                juce::AudioBuffer<float> input (2, blockSize);
                input.makeCopyOf (block);

                processor.processBlock (block, midi);

                // Compare against the input delayed by the reported latency,
                // once past the initial ramp-in.
                if (position >= 2048)
                {
                    for (int i = 0; i < blockSize; ++i)
                    {
                        const auto sourceIndex = position + i - reported;

                        if (sourceIndex < 0)
                            continue;

                        const auto expected = 0.5 * std::sin (
                            juce::MathConstants<double>::twoPi * 440.0 * sourceIndex / sampleRate);

                        worstResidual = std::max (worstResidual,
                                                  std::abs (block.getSample (0, i) - expected));
                    }
                }
            }

            INFO ("worst passthrough residual = " << TestHelpers::toDecibels (worstResidual) << " dB");
            CHECK (TestHelpers::toDecibels (worstResidual) < -100.0);
        }
    }
}

TEST_CASE ("T-L2: switching quality mid-stream stays finite, click-free and re-reports latency", "[latency]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;
    constexpr int blocksPerSegment = 10;
    constexpr int numSegments = 8;

    TenebraeAudioProcessor processor;
    processor.prepareToPlay (sampleRate, blockSize);

    setLatencyTestParam (processor, ParamIDs::engine, 1.0f);
    setLatencyTestParam (processor, ParamIDs::quality, 1.0f);
    setLatencyTestParam (processor, ParamIDs::gain, 32.0f);

    juce::AudioBuffer<float> block (2, blockSize);
    juce::MidiBuffer midi;

    std::vector<int> reportedLatencies;

    for (int segment = 0; segment < numSegments; ++segment)
    {
        // Cycle Eco -> Standard -> HQ -> Classic.
        const auto step = segment % 4;

        if (step == 3)
        {
            setLatencyTestParam (processor, ParamIDs::engine, 0.0f);
        }
        else
        {
            setLatencyTestParam (processor, ParamIDs::engine, 1.0f);
            setLatencyTestParam (processor, ParamIDs::quality, static_cast<float> (step));
        }

        std::vector<float> peaks;

        for (int i = 0; i < blocksPerSegment; ++i)
        {
            TestHelpers::fillWithSine (block, sampleRate, 110.0, 0.5f,
                                       (segment * blocksPerSegment + i) * blockSize);
            processor.processBlock (block, midi);

            REQUIRE (TestHelpers::allSamplesFinite (block));

            // The brief's bound for a mid-stream switch: nothing exceeds
            // |1.5|. Meeting it required two fixes that this assertion found:
            // a 2 ms swap fade instead of 16 samples (16 samples is shorter
            // than a half-band IIR's reset transient), and resetting every
            // nonlinear chain on a swap rather than only the one being
            // switched to (a chain switched back to was resuming from
            // seconds-old state). Before those, this read 1.65.
            REQUIRE (TestHelpers::peakAbsolute (block) <= 1.5f);

            peaks.push_back (TestHelpers::peakAbsolute (block));
        }

        // Recorded, not gated: how far above the settled level the switch
        // block transiently sits. Compared WITHIN the segment, because the
        // two engines legitimately run at different levels and comparing
        // across segments would only measure that difference.
        //
        // The residual overshoot is the tone stack ringing on the step from
        // the outgoing chain's steady output down to the incoming chain's
        // ramp-in - a true crossfade would need both chains running at once
        // for the fade. Measured at up to ~1.8x the settled level for a
        // single block when switching engines, which stays inside the brief's
        // |1.5| absolute bound and is one of the reasons Engine and Quality
        // are flagged non-automatable rather than being swept.
        double settledSum = 0.0;

        for (size_t i = 3; i < peaks.size(); ++i)
            settledSum += peaks[i];

        const auto settledPeak = settledSum / (peaks.size() - 3);

        if (segment > 0)
        {
            INFO ("segment " << segment << " step " << step
                             << ": switch-block peak " << peaks[0]
                             << " vs settled mean " << settledPeak);

            // Two blocks after the swap the transient is fully gone.
            CHECK (peaks[2] <= settledPeak * 1.35);
        }

        // Latency is re-reported from the MESSAGE thread (see
        // PluginProcessor::LatencyReporter) because
        // AudioProcessor::setLatencySamples() notifies host listeners and is
        // not audio-thread safe. In a host the message loop is running; here
        // it has to be pumped explicitly, or getLatencySamples() would never
        // observe the change and this assertion would be vacuous.
        juce::MessageManager::getInstance()->runDispatchLoopUntil (250);

        reportedLatencies.push_back (processor.getLatencySamples());
        INFO ("segment " << segment << " reported latency " << processor.getLatencySamples());
        CHECK (processor.getLatencySamples() > 0);
    }

    // Each of the four chains has its own latency, so the switch cycle must
    // have produced several distinct values.
    std::vector<int> distinct;

    for (const auto latency : reportedLatencies)
        if (std::find (distinct.begin(), distinct.end(), latency) == distinct.end())
            distinct.push_back (latency);

    INFO ("distinct reported latencies across the switch cycle: " << distinct.size());
    CHECK (distinct.size() >= 3);
}

//==============================================================================
// Suite-wide hardening wave: sample-rate matrix reprepare.
//
// Broader than "Latency updates correctly when the sample rate changes"
// above: this drives one processor instance through a full
// 44.1k -> 96k -> 192k reprepare matrix, crossing small AND large block
// sizes and mono/stereo bus layouts along the way, with automation-like
// parameter churn between reprepares. Engine/Quality are deliberately left
// untouched here (they are documented non-automatable, latency-affecting
// switches re-reported via the message-thread LatencyReporter timer - see
// T-L2 above and PluginProcessor.h) - this test's job is prepareToPlay()
// itself, which sets latency synchronously (PluginProcessor.cpp:
// `setLatencySamples (engine.getLatencySamples())`), so every reprepare
// below must observe the new latency immediately, with no dispatch-loop
// pump required. Deterministic and block counts kept small so this stays
// well under 30s even on Debug/CI.
TEST_CASE ("Sample-rate matrix reprepare: 44.1k -> 96k -> 192k across block sizes and bus "
           "layouts survives parameter automation and reports correct latency every time",
           "[latency][robustness][samplerate][reprepare]")
{
    TenebraeAudioProcessor processor;
    juce::MidiBuffer midi;

    setLatencyTestParam (processor, ParamIDs::gain, 22.0f);
    setLatencyTestParam (processor, ParamIDs::tight, 120.0f);
    setLatencyTestParam (processor, ParamIDs::bass, 4.0f);
    setLatencyTestParam (processor, ParamIDs::treble, -3.0f);
    setLatencyTestParam (processor, ParamIDs::level, -2.0f);
    setLatencyTestParam (processor, ParamIDs::mix, 80.0f);

    auto* gainParam = processor.apvts.getParameter (ParamIDs::gain);
    REQUIRE (gainParam != nullptr);

    // Tracks what Gain's value ought to be at the start of each iteration -
    // seeded from the setLatencyTestParam() above, then updated to the last
    // value the automation loop below left it at, so each reprepare's
    // "did the value survive" check is against ground truth rather than a
    // stale constant.
    auto expectedGainValue = gainParam->convertFrom0to1 (gainParam->getValue());

    struct Step
    {
        double sampleRate;
        int blockSize;
        int numChannels;
    };

    // Small AND large blocks at both 96k and 192k, plus a mono layout
    // change thrown in at 192k (Tenebrae supports mono -
    // isBusesLayoutSupported() accepts mono or stereo in == out) to make
    // sure a channel-count change riding along with a sample-rate reprepare
    // doesn't trip anything up.
    static constexpr Step steps[] = {
        { 44100.0,  32,   2 },
        { 96000.0,  32,   2 },
        { 96000.0,  2048, 2 },
        { 192000.0, 32,   1 },
        { 192000.0, 2048, 2 },
    };

    for (const auto& step : steps)
    {
        if (step.numChannels == 1)
        {
            juce::AudioProcessor::BusesLayout monoLayout;
            monoLayout.inputBuses.add (juce::AudioChannelSet::mono());
            monoLayout.outputBuses.add (juce::AudioChannelSet::mono());
            REQUIRE (processor.setBusesLayout (monoLayout));
        }
        else
        {
            juce::AudioProcessor::BusesLayout stereoLayout;
            stereoLayout.inputBuses.add (juce::AudioChannelSet::stereo());
            stereoLayout.outputBuses.add (juce::AudioChannelSet::stereo());
            REQUIRE (processor.setBusesLayout (stereoLayout));
        }

        processor.prepareToPlay (step.sampleRate, step.blockSize);

        // Latency must be reported (and positive - 8x oversampling always
        // adds some) after every single reprepare in the matrix, not just
        // the first one.
        CHECK (processor.getLatencySamples() > 0);

        // State survival: prepareToPlay() must never reset APVTS parameter
        // values, at any sample rate/block-size/layout combination.
        CHECK (gainParam->convertFrom0to1 (gainParam->getValue())
               == Catch::Approx (expectedGainValue).margin (0.01f));

        juce::AudioBuffer<float> buffer (step.numChannels, step.blockSize);

        for (int block = 0; block < 4; ++block)
        {
            // Automation-like parameter churn while processing, mimicking a
            // host sweeping controls mid-stream between reprepares.
            const auto sweep = static_cast<float> (block) / 4.0f;
            expectedGainValue = 5.0f + sweep * 30.0f;
            setLatencyTestParam (processor, ParamIDs::gain, expectedGainValue);
            setLatencyTestParam (processor, ParamIDs::bass, -12.0f + sweep * 24.0f);
            setLatencyTestParam (processor, ParamIDs::mid, -12.0f + sweep * 24.0f);
            setLatencyTestParam (processor, ParamIDs::treble, -12.0f + sweep * 24.0f);
            setLatencyTestParam (processor, ParamIDs::presence, -6.0f + sweep * 12.0f);

            TestHelpers::fillWithSine (buffer, step.sampleRate, 220.0, 0.6f,
                                       static_cast<juce::int64> (block) * step.blockSize);

            CHECK_NOTHROW (processor.processBlock (buffer, midi));
            CHECK (TestHelpers::allSamplesFinite (buffer));
        }
    }
}
