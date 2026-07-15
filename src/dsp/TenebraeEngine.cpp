#include "TenebraeEngine.h"

#include <cmath>

namespace
{
    // Keeps a requested filter frequency safely below Nyquist regardless of
    // host sample rate, so juce::dsp::IIR::Coefficients::makeHighPass never
    // receives an out-of-range value (which would produce invalid/NaN
    // coefficients).
    //
    // juce::jlimit() is NOT NaN-safe (see GitHub issue #14): both of its
    // internal comparisons (`value < lowerLimit`, `upperLimit < value`)
    // evaluate false for a NaN `value`, so NaN falls through unchanged
    // rather than being clamped. A NaN Tight frequency can reach here
    // directly from host automation (juce::AudioParameterFloat::setValue()
    // does not itself guard against a NaN normalised value), and an
    // unclamped NaN passed to makeHighPass() produces NaN filter
    // coefficients that poison tightHighPass's delay-line state for at
    // least the block the NaN coefficients are applied on every
    // architecture - indefinitely on arm64, where JUCE's snap-to-zero
    // denormal cleanup (JUCE_SNAP_TO_ZERO) is a no-op rather than the
    // NaN-clearing comparison it is on x86_64. Replacing NaN with a safe
    // in-range default *before* the jlimit() call below closes that gap.
    float clampBelowNyquist (float frequencyHz, double sampleRate) noexcept
    {
        if (std::isnan (frequencyHz))
            frequencyHz = 10.0f;

        const auto nyquist = static_cast<float> (sampleRate) * 0.5f;
        return juce::jlimit (10.0f, nyquist * 0.9f, frequencyHz);
    }
}

namespace
{
    // Bright pre-emphasis shelf: fixed frequency/Q/gain, toggled wholesale
    // by setBright() between this boost and unity. 3.5 kHz sits at a typical
    // amp "bright switch"/presence corner - just above the tone stack's own
    // Treble shelf corner (see ToneStack.h), so Bright and Treble stack
    // rather than fight over the same band.
    constexpr float brightShelfFrequencyHz = 3500.0f;
    constexpr float brightShelfGainDb = 5.0f;
    constexpr float brightShelfQ = juce::MathConstants<float>::sqrt2 / 2.0f;
}

// Fixed per-stage cascade voicing: each successive stage is driven a little
// harder, clips a little more asymmetrically, and is filtered a little
// tighter/darker than the last. This is what turns three identical clippers
// into a cascade that converges onto a focused "chug" band rather than an
// ever-fizzier, ever-boomier mess - the same idea CascadeStage.h documents,
// concretised here with actual numbers. Only the single pre-cascade Gain
// parameter is user-automatable; these per-stage values are fixed voicing,
// selected in bulk by the Voicing switch (see setVoicing()).
//
// "Loose" is a deliberately softer counterpart to the "Tight" v0.1 cascade:
// less asymmetry (less even-harmonic bite per stage), lower fixed drive, and
// wider interstage HP/LP corners (more low end let through, more top-end
// air kept) - a vintage-leaning alternative to the tighter, more modern
// default voicing, rather than a simple louder/quieter variant of it.
TenebraeEngine::TenebraeEngine()
    : cascadeStage1 (0.15f, 80.0f, 9000.0f),
      cascadeStage2 (0.25f, 120.0f, 6500.0f),
      cascadeStage3 (0.35f, 150.0f, 5000.0f),
      cascadeStage1Loose (0.10f, 60.0f, 10000.0f),
      cascadeStage2Loose (0.18f, 90.0f, 8000.0f),
      cascadeStage3Loose (0.25f, 110.0f, 6500.0f)
{
}

void TenebraeEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    tightHighPass.prepare (spec);

    brightShelf.prepare (spec);

    preGain.setRampDurationSeconds (smoothingTimeSeconds);
    preGain.prepare (spec);
    preGain.setGainDecibels (lastGainDb);

    // 8x oversampling (2^3), half-band polyphase IIR: three cascaded
    // nonlinearities generate substantially more high-frequency content than
    // a single clipper, so the higher factor (vs. e.g. a simple boost/OD)
    // keeps aliasing from every stage - not just the first - out of the
    // audible band. useIntegerLatency=true so the reported latency (and
    // therefore setLatencySamples()) is an exact integer sample count.
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        spec.numChannels,
        oversamplingFactorPow2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true,
        true);
    oversampler->initProcessing (static_cast<size_t> (spec.maximumBlockSize));

    // The cascade stages run entirely inside the oversampled block, so they
    // must be prepared with the oversampled rate/block size, not the host's.
    const auto oversamplingMultiplier = static_cast<juce::uint32> (1u << static_cast<unsigned> (oversamplingFactorPow2));

    juce::dsp::ProcessSpec oversampledSpec;
    oversampledSpec.sampleRate = spec.sampleRate * static_cast<double> (oversamplingMultiplier);
    oversampledSpec.maximumBlockSize = spec.maximumBlockSize * oversamplingMultiplier;
    oversampledSpec.numChannels = spec.numChannels;

    cascadeStage1.prepare (oversampledSpec);
    cascadeStage2.prepare (oversampledSpec);
    cascadeStage3.prepare (oversampledSpec);

    cascadeStage1Loose.prepare (oversampledSpec);
    cascadeStage2Loose.prepare (oversampledSpec);
    cascadeStage3Loose.prepare (oversampledSpec);

    // Fixed per-stage drive (dB) into each stage's nonlinearity - part of
    // the cascade's voicing (see the constructor above), not exposed as a
    // separate user parameter. Loose is driven a little softer than Tight
    // at every stage, matching its more vintage-leaning character.
    cascadeStage1.setDriveDb (6.0f);
    cascadeStage2.setDriveDb (8.0f);
    cascadeStage3.setDriveDb (10.0f);

    cascadeStage1Loose.setDriveDb (4.0f);
    cascadeStage2Loose.setDriveDb (6.0f);
    cascadeStage3Loose.setDriveDb (8.0f);

    toneStack.prepare (spec);
    outputLevel.setRampDurationSeconds (smoothingTimeSeconds);
    outputLevel.prepare (spec);
    // M1 fix: juce::dsp::Gain's internal SmoothedValue default-constructs to
    // *linear 0* (silence), not unity, so outputLevel must be primed from
    // lastLevelDb here exactly like preGain is primed from lastGainDb a few
    // lines above - previously it was only ever set via setLevelDb(), so any
    // prepare() call not immediately preceded by one left the wet path
    // permanently silent (harmless in the shipped plugin, since
    // PluginProcessor::prepareToPlay() always calls setLevelDb() first, but
    // a real trap for anything exercising TenebraeEngine directly).
    outputLevel.setGainDecibels (lastLevelDb);

    dryWetMixer.prepare (spec);

    latencySamples = static_cast<int> (std::round (oversampler->getLatencyInSamples()));
    dryWetMixer.setWetLatency (static_cast<float> (latencySamples));

    // juce::dsp::DryWetMixer defaults its internal mix to fully wet (1.0)
    // until told otherwise, and its own reset() (called from our reset()
    // below) snaps its internal dry/wet gain smoothers' *current* value to
    // whatever their *target* happens to be at that moment - it does not
    // know about lastMixProportion. Priming the real target here, before
    // reset() runs, means the mixer is already sitting at the correct dry/
    // wet balance for the very first process() call instead of ramping up
    // from "fully wet" over its internal 50ms default ramp.
    dryWetMixer.setWetMixProportion (lastMixProportion);

    // Re-seed the Tight/Mix smoothers at the new sample rate, but pin
    // current == target to whatever was last requested (defaulting to the
    // ParameterLayout defaults on first prepare) - otherwise the ramp would
    // sweep up from a default-constructed 0 Hz/0.0 on the very first block.
    tightFrequencySmoothed.reset (sampleRate, smoothingTimeSeconds);
    tightFrequencySmoothed.setCurrentAndTargetValue (lastTightHz);
    mixSmoothed.reset (sampleRate, smoothingTimeSeconds);
    mixSmoothed.setCurrentAndTargetValue (lastMixProportion);

    reset();

    // Prime the Tight HPF coefficients immediately so the very first
    // process() call runs with correct, non-default coefficients rather
    // than an identity/uninitialised state.
    *tightHighPass.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (
        sampleRate, clampBelowNyquist (lastTightHz, sampleRate), filterQ);

    // Prime the Bright shelf coefficients to whatever brightEnabled was last
    // set to (defaulting to off), same rationale as the Tight HPF above -
    // otherwise the very first block would run with default/identity
    // coefficients regardless of the actual Bright switch state.
    *brightShelf.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        sampleRate, brightShelfFrequencyHz, brightShelfQ,
        juce::Decibels::decibelsToGain (brightEnabled ? brightShelfGainDb : 0.0f));
}

void TenebraeEngine::reset()
{
    tightHighPass.reset();
    brightShelf.reset();
    preGain.reset();

    if (oversampler != nullptr)
        oversampler->reset();

    cascadeStage1.reset();
    cascadeStage2.reset();
    cascadeStage3.reset();

    cascadeStage1Loose.reset();
    cascadeStage2Loose.reset();
    cascadeStage3Loose.reset();

    toneStack.reset();
    outputLevel.reset();
    dryWetMixer.reset();
}

void TenebraeEngine::setTightFrequencyHz (float newFrequencyHz)
{
    lastTightHz = newFrequencyHz;
    tightFrequencySmoothed.setTargetValue (newFrequencyHz);
}

void TenebraeEngine::setGainDb (float newGainDb)
{
    lastGainDb = newGainDb;
    preGain.setGainDecibels (newGainDb);
}

void TenebraeEngine::setBassDb (float newBassDb)
{
    toneStack.setBassDb (newBassDb);
}

void TenebraeEngine::setMidDb (float newMidDb)
{
    toneStack.setMidDb (newMidDb);
}

void TenebraeEngine::setTrebleDb (float newTrebleDb)
{
    toneStack.setTrebleDb (newTrebleDb);
}

void TenebraeEngine::setLevelDb (float newLevelDb)
{
    lastLevelDb = newLevelDb;
    outputLevel.setGainDecibels (newLevelDb);
}

void TenebraeEngine::setMixProportion (float newProportion01)
{
    lastMixProportion = newProportion01;
    mixSmoothed.setTargetValue (newProportion01);
}

void TenebraeEngine::setVoicing (int newVoicingIndex)
{
    currentVoicing = juce::jlimit (0, 1, newVoicingIndex);
}

void TenebraeEngine::setBright (bool isBrightOn)
{
    // Recompute the shelf coefficients only on an actual state change, both
    // to avoid needless trig calls every block for a switch that is
    // typically left alone for long stretches, and so process() never sees
    // a torn/partially-updated coefficient set. This is a discrete switch,
    // not a continuous control, so it is intentionally not smoothed - like a
    // physical amp's bright switch, engaging it can produce a small audible
    // step rather than a ramp.
    if (isBrightOn == brightEnabled)
        return;

    brightEnabled = isBrightOn;

    *brightShelf.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        sampleRate, brightShelfFrequencyHz, brightShelfQ,
        juce::Decibels::decibelsToGain (brightEnabled ? brightShelfGainDb : 0.0f));
}

void TenebraeEngine::setToneVoice (int newToneVoiceIndex)
{
    toneStack.setToneVoice (newToneVoiceIndex);
}

void TenebraeEngine::process (juce::dsp::AudioBlock<float>& block)
{
    const auto numSamples = block.getNumSamples();

    if (numSamples == 0)
        return;

    // Coefficient recomputation involves trig calls, so Tight is smoothed
    // and re-derived once per block rather than per sample - a standard
    // real-time-safe compromise for IIR filters, whose coefficients aren't
    // cheap to interpolate directly. Gain/Level still ramp sample-accurately
    // via juce::dsp::Gain's internal SmoothedValue, and Mix/tone-stack band
    // gains are re-applied once per block below.
    const auto tightHz = clampBelowNyquist (tightFrequencySmoothed.skip (static_cast<int> (numSamples)), sampleRate);
    const auto wetMix = mixSmoothed.skip (static_cast<int> (numSamples));

    *tightHighPass.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, tightHz, filterQ);
    dryWetMixer.setWetMixProportion (wetMix);
    toneStack.updateCoefficients (static_cast<int> (numSamples));

    juce::dsp::ProcessContextReplacing<float> context (block);

    // Capture the pre-processing signal as "dry" before any wet-path
    // filtering touches `block`. DryWetMixer internally delays this by
    // getLatencySamples() (set via setWetLatency in prepare()) so it stays
    // time-aligned with the oversampled wet path below.
    dryWetMixer.pushDrySamples (block);

    tightHighPass.process (context);
    brightShelf.process (context);
    preGain.process (context);

    auto oversampledBlock = oversampler->processSamplesUp (block);
    juce::dsp::ProcessContextReplacing<float> oversampledContext (oversampledBlock);

    if (currentVoicing == 0)
    {
        cascadeStage1.process (oversampledContext);
        cascadeStage2.process (oversampledContext);
        cascadeStage3.process (oversampledContext);
    }
    else
    {
        cascadeStage1Loose.process (oversampledContext);
        cascadeStage2Loose.process (oversampledContext);
        cascadeStage3Loose.process (oversampledContext);
    }

    oversampler->processSamplesDown (block);

    toneStack.process (context);
    outputLevel.process (context);

    dryWetMixer.mixWetSamples (block);
}
