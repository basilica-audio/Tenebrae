#include "dsp/ToneStack.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

// Verifies the ToneStack's Bass/Treble bands measurably shift low/high
// energy in the expected direction - tested against ToneStack directly
// (rather than through the full oversampled cascade) so the result isolates
// the tone stack's own shelving filters from the cascade's fixed interstage
// filtering.
namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int testBlockSize = 4096;

    juce::dsp::ProcessSpec makeTestSpec()
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (testBlockSize);
        spec.numChannels = 1;
        return spec;
    }

    // Builds a ToneStack with the requested band gain (dB) already seeded
    // as the starting value (setter called before prepare(), matching
    // TenebraeEngine::prepare()'s seed-then-prepare pattern), processes one
    // full block of a test tone through it, and returns the RMS of the
    // result.
    double measureBandRms (void (ToneStack::*setter) (float), float gainDb, double toneFrequencyHz)
    {
        ToneStack toneStack;
        (toneStack.*setter) (gainDb);

        const auto spec = makeTestSpec();
        toneStack.prepare (spec);
        toneStack.updateCoefficients (testBlockSize);

        juce::AudioBuffer<float> buffer (1, testBlockSize);
        TestHelpers::fillWithSine (buffer, testSampleRate, toneFrequencyHz, 0.5f);

        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> context (block);
        toneStack.process (context);

        return TestHelpers::rms (buffer);
    }
}

TEST_CASE ("ToneStack: Bass boost/cut measurably shifts low-frequency energy", "[dsp][tonestack]")
{
    // 100 Hz sits below the 150 Hz low-shelf corner, squarely in the band
    // the Bass control shapes.
    const auto rmsBoost = measureBandRms (&ToneStack::setBassDb, 15.0f, 100.0);
    const auto rmsCut = measureBandRms (&ToneStack::setBassDb, -15.0f, 100.0);

    REQUIRE (rmsCut > 0.0);
    CHECK (rmsBoost > rmsCut * 1.5); // comfortably more than a rounding-error difference
}

TEST_CASE ("ToneStack: Treble boost/cut measurably shifts high-frequency energy", "[dsp][tonestack]")
{
    // 6 kHz sits above the M1-refined 3.5 kHz high-shelf corner, squarely in the band
    // the Treble control shapes.
    const auto rmsBoost = measureBandRms (&ToneStack::setTrebleDb, 15.0f, 6000.0);
    const auto rmsCut = measureBandRms (&ToneStack::setTrebleDb, -15.0f, 6000.0);

    REQUIRE (rmsCut > 0.0);
    CHECK (rmsBoost > rmsCut * 1.5); // comfortably more than a rounding-error difference
}

TEST_CASE ("ToneStack: Mid boost/cut measurably shifts energy at the peak's centre frequency", "[dsp][tonestack]")
{
    // 650 Hz is the M1-refined Mid peak's own centre frequency, squarely in
    // the band the Mid control shapes.
    const auto rmsBoost = measureBandRms (&ToneStack::setMidDb, 15.0f, 650.0);
    const auto rmsCut = measureBandRms (&ToneStack::setMidDb, -15.0f, 650.0);

    REQUIRE (rmsCut > 0.0);
    CHECK (rmsBoost > rmsCut * 1.5); // comfortably more than a rounding-error difference
}

TEST_CASE ("ToneStack: Bass and Treble at unity (0 dB) leave a mid-band tone effectively unchanged", "[dsp][tonestack]")
{
    ToneStack toneStack;

    const auto spec = makeTestSpec();
    toneStack.prepare (spec);
    toneStack.updateCoefficients (testBlockSize);

    juce::AudioBuffer<float> buffer (1, testBlockSize);
    // 2 kHz sits well away from every fixed corner (150 Hz Bass shelf,
    // 650 Hz Mid peak, 3.5 kHz Treble shelf), so with every band at its
    // 0 dB default (Tone Voice also at its Flat/no-tilt default) the whole
    // stack should be close to unity gain here.
    TestHelpers::fillWithSine (buffer, testSampleRate, 2000.0, 0.5f);
    const auto inputRms = TestHelpers::rms (buffer);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    toneStack.process (context);

    const auto outputRms = TestHelpers::rms (buffer);

    CHECK (outputRms > inputRms * 0.9);
    CHECK (outputRms < inputRms * 1.1);
}

TEST_CASE ("ToneStack: Tone Voice tilt is added on top of live Bass/Mid/Treble gains", "[dsp][tonestack]")
{
    // Scoop (index 1) tilts mid down (see ToneStack.h); with every band
    // knob left at its own 0 dB default, selecting Scoop should measurably
    // cut a tone at the Mid peak's own centre frequency relative to Flat
    // (index 0), and Boost (index 2) should measurably boost it the other
    // way - proving the switch actually reaches the filter coefficients,
    // not just that ToneStack accepts the call.
    constexpr double toneFrequencyHz = 650.0; // Mid peak centre

    auto measureWithToneVoice = [] (int toneVoiceIndex)
    {
        ToneStack toneStack;
        toneStack.setToneVoice (toneVoiceIndex);

        const auto spec = makeTestSpec();
        toneStack.prepare (spec);
        toneStack.updateCoefficients (testBlockSize);

        juce::AudioBuffer<float> buffer (1, testBlockSize);
        TestHelpers::fillWithSine (buffer, testSampleRate, toneFrequencyHz, 0.5f);

        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> context (block);
        toneStack.process (context);

        return TestHelpers::rms (buffer);
    };

    const auto rmsFlat = measureWithToneVoice (0);
    const auto rmsScoop = measureWithToneVoice (1);
    const auto rmsBoost = measureWithToneVoice (2);

    REQUIRE (rmsFlat > 0.0);
    CHECK (rmsScoop < rmsFlat * 0.9);  // Scoop cuts the mid
    CHECK (rmsBoost > rmsFlat * 1.1);  // Boost lifts the mid
    CHECK (rmsBoost > rmsScoop * 1.5); // comfortably distinct from each other
}

TEST_CASE ("ToneStack: NaN Bass gain is treated as a 0 dB no-op, not a silent full cut", "[dsp][tonestack][nan]")
{
    // Regression test for GitHub issue #14: ToneStack::clampCombinedGainDb()
    // relied solely on juce::jlimit(), which is not NaN-safe (both of its
    // internal comparisons evaluate false for NaN, so NaN previously fell
    // through unchanged). Unlike clampBelowNyquist() (see
    // EngineTests.cpp/CascadeStageTests.cpp's NaN tests), an unclamped NaN
    // gain here does not itself produce NaN *output*: it reaches
    // juce::Decibels::decibelsToGain(), whose own `decibels > minusInfinityDb
    // ? pow(...) : 0` branch also evaluates false for NaN and happens to
    // return a linear gain of exactly 0 - i.e. pre-fix, a NaN Bass value
    // silently and severely attenuates the Bass band (a low-shelf gain of 0
    // linear nulls everything below the shelf corner) rather than NaN-ing
    // it. That is still a real correctness bug (a NaN parameter should be
    // treated as a safe no-op, not as an accidental "cut this band to
    // nothing"), and it is what this test pins down: post-fix, a NaN Bass
    // value clamps to 0 dB and behaves as a no-op, leaving a 100 Hz tone
    // (squarely inside the Bass band) close to its input level rather than
    // reduced to near-silence.
    ToneStack toneStack;
    toneStack.setBassDb (std::numeric_limits<float>::quiet_NaN());

    const auto spec = makeTestSpec();
    toneStack.prepare (spec);
    toneStack.updateCoefficients (testBlockSize);

    juce::AudioBuffer<float> buffer (1, testBlockSize);
    TestHelpers::fillWithSine (buffer, testSampleRate, 100.0, 0.5f);
    const auto inputRms = TestHelpers::rms (buffer);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    CHECK_NOTHROW (toneStack.process (context));

    const auto outputRms = TestHelpers::rms (buffer);

    CHECK (TestHelpers::allSamplesFinite (buffer));
    // Pre-fix this was ~0.00001x the input (a linear-gain-0 low shelf nulls
    // everything below its corner); post-fix it is a 0 dB no-op.
    CHECK (outputRms > inputRms * 0.9);
    CHECK (outputRms < inputRms * 1.1);
}
