#include "CascadeStage.h"
#include "AsymmetricClipper.h"

#include <cmath>

namespace
{
    // Keeps a fixed interstage filter frequency safely below Nyquist
    // regardless of host sample rate/oversampling factor, so
    // juce::dsp::IIR::Coefficients::makeHighPass/makeLowPass never receives
    // an out-of-range value (which would produce invalid/NaN coefficients).
    //
    // juce::jlimit() is NOT NaN-safe (see GitHub issue #14 - this is the
    // CascadeStage.cpp duplicate of the same helper/gap fixed in
    // TenebraeEngine.cpp): both of its internal comparisons evaluate false
    // for NaN, so NaN falls through unchanged instead of being clamped.
    // highPassFrequencyHz/lowPassFrequencyHz are fixed per-stage voicing
    // constants (not user-automatable), so in practice this can currently
    // only be reached with a NaN via a constructor argument - but the guard
    // is applied here too, both for defence in depth and so the two
    // clampBelowNyquist() copies do not silently diverge in behaviour.
    float clampBelowNyquist (float frequencyHz, double sampleRate) noexcept
    {
        if (std::isnan (frequencyHz))
            frequencyHz = 10.0f;

        const auto nyquist = static_cast<float> (sampleRate) * 0.5f;
        return juce::jlimit (10.0f, nyquist * 0.9f, frequencyHz);
    }
}

CascadeStage::CascadeStage (float asymmetryToUse, float highPassFrequencyHzToUse, float lowPassFrequencyHzToUse)
    : asymmetry (asymmetryToUse),
      highPassFrequencyHz (highPassFrequencyHzToUse),
      lowPassFrequencyHz (lowPassFrequencyHzToUse)
{
}

void CascadeStage::prepare (const juce::dsp::ProcessSpec& spec)
{
    oversampledSampleRate = spec.sampleRate;

    driveGain.setRampDurationSeconds (smoothingTimeSeconds);
    driveGain.prepare (spec);

    interstageHighPass.prepare (spec);
    interstageLowPass.prepare (spec);

    reset();

    // Fixed filters - computed once here, never recomputed per block, since
    // neither corner frequency is user-automatable.
    *interstageHighPass.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (
        oversampledSampleRate, clampBelowNyquist (highPassFrequencyHz, oversampledSampleRate), filterQ);
    *interstageLowPass.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass (
        oversampledSampleRate, clampBelowNyquist (lowPassFrequencyHz, oversampledSampleRate), filterQ);
}

void CascadeStage::reset()
{
    driveGain.reset();
    interstageHighPass.reset();
    interstageLowPass.reset();
}

void CascadeStage::setDriveDb (float newDriveDb)
{
    driveGain.setGainDecibels (newDriveDb);
}

void CascadeStage::process (juce::dsp::ProcessContextReplacing<float>& context)
{
    driveGain.process (context);

    auto block = context.getOutputBlock();

    for (size_t channel = 0; channel < block.getNumChannels(); ++channel)
    {
        auto* channelData = block.getChannelPointer (channel);

        for (size_t sample = 0; sample < block.getNumSamples(); ++sample)
            channelData[sample] = AsymmetricClipper::processSample (channelData[sample], asymmetry);
    }

    interstageHighPass.process (context);
    interstageLowPass.process (context);
}
