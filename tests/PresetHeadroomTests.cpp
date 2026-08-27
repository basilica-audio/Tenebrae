#include "PluginProcessor.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

// The factory-preset headroom gate (issue #45).
//
// Nothing in this repo used to check what a factory preset does to the LEVEL of
// the signal it is handed. tests/PresetManagerTests.cpp asserts that every
// factory preset parses, loads, and lands its parameters in range - not that the
// result stays inside full scale. A preset that clips is a preset the user has
// to fix before they can even audition it, which is a defect and not
// gain-staging taste.
//
// This file is the gate for that. It renders every shipped factory preset
// through the real AudioProcessor at a documented reference input level and
// asserts the output peak stays below 0 dBFS. A preset added later that clips
// this reference fails here.
//
// WHAT THIS DELIBERATELY DOES NOT DO is level-match the presets to each other.
// The gate is a ceiling, not a target: a preset sitting well below the line is
// left exactly where its author put it. Relative loudness between presets is a
// voicing/taste question and is not decided here.
namespace
{
    constexpr double referenceSampleRate = 48000.0;
    constexpr int referenceBlockSize = 512;

    // The suite-wide reference input level. -12 dBFS peak is the level a track
    // is conventionally recorded at, leaving 12 dB of headroom, and therefore
    // the level a factory preset's author must be assumed to have voiced for.
    constexpr float referencePeakDbfs = -12.0f;

    // The line this gate asserts: a factory preset must not push the reference
    // programme past full scale.
    constexpr double clippingCeiling = 1.0; // 0 dBFS, linear

    // What the shipped trims in presets/factory/*.json AIM at, which is 0.3 dB
    // below the asserted line. The gap is deliberate: it is the difference
    // between "an upstream voicing tweak moved the peak a little" and "this gate
    // goes red". A trim is derived as (measured overshoot + 0.3 dB), rounded to
    // the level parameter's 0.01 dB step - never tasted, and never applied to a
    // preset that was already under the line.
    constexpr double headroomTargetDbfs = -0.3;

    // Twelve factory presets ship - CMakeLists.txt's juce_add_binary_data list
    // and PluginProcessor.cpp's makeFactoryPresetAssets() must agree with this.
    // Asserted below so that "the preset library stopped loading" is
    // distinguishable from "every preset passed".
    constexpr int shippedFactoryPresetCount = 12;

    //==========================================================================
    // The fleet reference programme signal.
    //
    // Four plucked notes, each a harmonic series with 1/n partial amplitudes, a
    // 3 ms pick attack and a 900 ms decay, with the upper partials decaying
    // faster than the fundamental as a real string does. Deterministic and
    // repeatable, which a recording could not be inside a test suite.
    //
    // The four fundamentals span E1 (41.203 Hz) to A5 (880.000 Hz): the bottom
    // is below a drop-tuned seven-string's lowest note, the top is a lead
    // register, and twelve harmonics per note carry content up to 10.6 kHz. That
    // makes it a harsher test than a bass DI for anything with high-band drive
    // and a fair one for everything else, and - the point of using the same
    // shape everywhere - it makes every plugin in the suite measurable on one
    // scale.
    //
    // Peak-normalised to referencePeakDbfs after synthesis, so the level the
    // gate talks about is exact regardless of how the partials happen to add.
    juce::AudioBuffer<float> makeReferenceProgramme (double sampleRate,
                                                    double seconds = 2.0,
                                                    float peakDbfs = referencePeakDbfs)
    {
        const auto numSamples = static_cast<int> (seconds * sampleRate);
        juce::AudioBuffer<float> buffer (2, numSamples);
        buffer.clear();

        const double notes[] = { 41.203, 110.000, 293.665, 880.000 }; // E1, A2, D4, A5
        const auto noteCount = static_cast<int> (std::size (notes));
        const auto noteSamples = numSamples / noteCount;

        for (int noteIndex = 0; noteIndex < noteCount; ++noteIndex)
        {
            const auto fundamental = notes[noteIndex];
            const auto start = noteIndex * noteSamples;

            for (int sample = 0; sample < noteSamples; ++sample)
            {
                const auto t = static_cast<double> (sample) / sampleRate;
                const auto envelope = (1.0 - std::exp (-t / 0.003)) * std::exp (-t / 0.9);

                double value = 0.0;

                for (int harmonic = 1; harmonic <= 12; ++harmonic)
                {
                    const auto frequency = fundamental * static_cast<double> (harmonic);

                    if (frequency >= 0.45 * sampleRate)
                        break;

                    const auto harmonicDecay = std::exp (-t * static_cast<double> (harmonic) / 2.5);
                    value += (1.0 / static_cast<double> (harmonic)) * harmonicDecay
                                 * std::sin (juce::MathConstants<double>::twoPi * frequency * t);
                }

                const auto out = static_cast<float> (envelope * value);

                for (int channel = 0; channel < 2; ++channel)
                    buffer.setSample (channel, start + sample, out);
            }
        }

        const auto peak = buffer.getMagnitude (0, numSamples);

        if (peak > 0.0f)
            buffer.applyGain (juce::Decibels::decibelsToGain (peakDbfs) / peak);

        return buffer;
    }

    void renderThrough (juce::AudioProcessor& processor, juce::AudioBuffer<float>& buffer, int blockSize)
    {
        juce::MidiBuffer midi;

        for (int position = 0; position < buffer.getNumSamples(); position += blockSize)
        {
            const auto thisBlock = juce::jmin (blockSize, buffer.getNumSamples() - position);
            juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), position, thisBlock);
            processor.processBlock (block, midi);
        }
    }

    // One preset's measured output peak, in dBFS, on the reference programme.
    double renderFactoryPresetPeakDb (const juce::String& presetName, const juce::AudioBuffer<float>& input)
    {
        TenebraeAudioProcessor processor;

        // Load the preset BEFORE prepareToPlay(), which is the order a host
        // uses on session load (setStateInformation() then prepareToPlay()) and
        // the only order that measures the preset rather than the ramp INTO it:
        // prepareToPlay() primes every smoothed gain stage from the live
        // parameter values, so the very first sample is already at the preset's
        // staging. Loading afterwards would leave the output level smoothing
        // down from the previous value across the first block - which is
        // precisely where the reference programme's first pick transient sits,
        // so the measured peak would be an artefact of the ramp.
        REQUIRE (processor.presetManager.loadPreset (presetName));

        processor.setPlayConfigDetails (2, 2, referenceSampleRate, referenceBlockSize);
        processor.prepareToPlay (referenceSampleRate, referenceBlockSize);
        processor.reset();

        juce::AudioBuffer<float> rendered (2, input.getNumSamples());
        rendered.makeCopyOf (input);
        renderThrough (processor, rendered, referenceBlockSize);

        REQUIRE (TestHelpers::allSamplesFinite (rendered));

        return juce::Decibels::gainToDecibels (
            static_cast<double> (rendered.getMagnitude (0, rendered.getNumSamples())));
    }
} // namespace

TEST_CASE ("Factory presets: none of them push the reference programme past 0 dBFS", "[presets][headroom]")
{
    const auto input = makeReferenceProgramme (referenceSampleRate);

    // The input really is at the level this gate claims it is.
    const auto inputPeakDb = juce::Decibels::gainToDecibels (
        static_cast<double> (input.getMagnitude (0, input.getNumSamples())));
    REQUIRE (std::abs (inputPeakDb - static_cast<double> (referencePeakDbfs)) < 0.01);

    TenebraeAudioProcessor probe;
    const auto presets = probe.presetManager.getAllPresets();

    auto factoryCount = 0;
    auto overFullScale = 0;

    for (const auto& entry : presets)
    {
        if (! entry.isFactory)
            continue;

        ++factoryCount;
        INFO ("preset: " << entry.name);

        const auto peakDb = renderFactoryPresetPeakDb (entry.name, input);

        INFO ("output peak " << peakDb << " dBFS at a " << referencePeakDbfs << " dBFS input peak");

        if (peakDb >= 0.0)
            ++overFullScale;

        CHECK (juce::Decibels::decibelsToGain (peakDb) < clippingCeiling);
    }

    // If this collapses, the preset library stopped being loaded rather than
    // every preset passing - the failure mode this gate must not have.
    INFO ("factory presets exercised: " << factoryCount
          << ", of which at or over 0 dBFS: " << overFullScale);
    CHECK (factoryCount == shippedFactoryPresetCount);
    CHECK (overFullScale == 0);
}

// Hidden reporting case: `./Tests "[.headroom-table]"` prints the measured peak
// of every factory preset, which is how the shipped trims were derived and how
// the next one would be.
TEST_CASE ("Factory preset headroom table", "[.headroom-table]")
{
    const auto input = makeReferenceProgramme (referenceSampleRate);

    TenebraeAudioProcessor probe;

    for (const auto& entry : probe.presetManager.getAllPresets())
    {
        if (! entry.isFactory)
            continue;

        const auto peakDb = renderFactoryPresetPeakDb (entry.name, input);
        const auto trim = peakDb - headroomTargetDbfs;

        WARN (entry.name << " | peak " << peakDb << " dBFS | trim to hit "
              << headroomTargetDbfs << " dBFS: " << -trim << " dB");
    }
}
