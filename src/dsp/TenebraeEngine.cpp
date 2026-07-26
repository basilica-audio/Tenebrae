#include "TenebraeEngine.h"
#include "RealtimeGain.h"

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

    // Presence shelf (v0.2.0, docs/design-brief.md section 3.3): fixed
    // 2.4 kHz corner, sourced to a reference high-gain amp's documented
    // Presence-control pivot (docs/research-notes.md section 2, citation
    // only - see that file's own note on why brand names live there and
    // nowhere else) - the more "modern high-gain" of the two documented
    // pivots,
    // consistent with Tenebrae's own modern-leaning default (Tight)
    // Voicing. Placed post-cascade/post-tone-stack (unlike Bright, which is
    // deliberately pre-cascade) since the reference class's Presence
    // control is a power-amp feedback stage acting on the already-driven
    // signal - see setPresenceDb()'s docs.
    constexpr float presenceShelfFrequencyHz = 2400.0f;
    constexpr float presenceShelfQ = juce::MathConstants<float>::sqrt2 / 2.0f;

    // NaN-safe clamp for Presence's dB value - same rationale/pattern as
    // ToneStack::clampCombinedGainDb() (see that function's doc comment):
    // juce::jlimit() is not NaN-safe, so NaN is replaced with 0 dB (unity/
    // no-op) before clamping.
    float clampPresenceDb (float gainDb) noexcept
    {
        if (std::isnan (gainDb))
            gainDb = 0.0f;

        return juce::jlimit (-12.0f, 12.0f, gainDb);
    }
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

    // See process()'s doc comment and GitHub issue #13: bounds the chunk
    // size process() ever hands to processChunk(), so an oversized incoming
    // block never reaches the oversampler/scratch buffers below at more
    // than the size they were actually allocated for.
    maxPreparedBlockSamples = juce::jmax (static_cast<size_t> (1), static_cast<size_t> (spec.maximumBlockSize));

    tightHighPass.prepare (spec);

    brightShelf.prepare (spec);

    preGain.setRampDurationSeconds (smoothingTimeSeconds);
    preGain.prepare (spec);
    preGain.setGainDecibels (lastGainDb);

    // Sized once here (not on the audio thread) to the host's maximum block
    // size, shared by preGain and outputLevel below - see the member's doc
    // comment in TenebraeEngine.h and RealtimeGain.h for why this replaces
    // juce::dsp::Gain::process()'s own per-call stack allocation.
    hostRateGainScratch.resize (static_cast<size_t> (spec.maximumBlockSize));

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

    // v0.3.0: the three Triode-engine oversampling chains (brief 3.3). All
    // three are built here and kept resident for the lifetime of the prepare,
    // so switching Quality on the audio thread is a pointer swap plus a
    // coefficient rebuild - never an allocation. useIntegerLatency = true on
    // all of them (house rule), so every reported latency is an exact integer
    // sample count that setLatencySamples() can be trusted with.
    //
    // Eco (2x) and Standard (4x) use the polyphase IIR half-band family for
    // near-zero low-frequency latency; HQ (8x) uses the equiripple FIR family
    // for linear phase, which is the right trade for a mixdown pass and the
    // wrong one for tracking.
    triodeEcoOversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        spec.numChannels,
        1,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true,
        true);
    triodeEcoOversampler->initProcessing (static_cast<size_t> (spec.maximumBlockSize));

    triodeStandardOversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        spec.numChannels,
        2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true,
        true);
    triodeStandardOversampler->initProcessing (static_cast<size_t> (spec.maximumBlockSize));

    triodeHqOversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        spec.numChannels,
        3,
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
        true,
        true);
    triodeHqOversampler->initProcessing (static_cast<size_t> (spec.maximumBlockSize));

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

    // v0.3.0 Triode engine. The stage LUTs (both voicings, six stages) are
    // solved from the Dempwolf equations here, on the calling thread, and are
    // read-only afterwards - see TriodeStage::prepare(). Only the coefficient
    // set depends on the oversampled rate, and applyChainSelection() below
    // re-derives that in place whenever Quality changes.
    triodeCascade.prepare (oversampledSpec.sampleRate, spec.numChannels);
    triodeCascade.setVoicing (currentVoicing);
    triodeCascade.setBiasScale (lastStageBiasScale);

    powerAmp.prepare (oversampledSpec.sampleRate, spec.numChannels);
    powerAmp.setResonanceDb (lastResonanceDb);
    powerAmp.setPresenceDb (lastPresenceDb);
    powerAmp.setSagAmount (lastSagAmount);

    // Prepare-time stability assert for the power-amp feedback loop (brief
    // 3.2): the unit-delay loop is only stable while the small-signal loop
    // gain stays below 1 at every frequency, and the design bound is 0.5.
    // Mirrored as a CI gate by T-P5 in tests/PowerAmpTests.cpp.
    jassert (powerAmp.getWorstCaseLoopGain() <= PowerAmp::maximumLoopGain + 1.0e-9);

    // Pre-distortion key tap buffer for the gate (Gate.h). Sized to the
    // host's maximum block size; process() chunks anything larger.
    gateKeyBuffer.setSize (static_cast<int> (spec.numChannels),
                           juce::jmax (1, static_cast<int> (spec.maximumBlockSize)),
                           false, true, true);
    gateKeyPointers.assign (static_cast<size_t> (juce::jmax (1u, spec.numChannels)), nullptr);

    chainSwapFadeSamples = juce::jmax (16, static_cast<int> (sampleRate * chainSwapFadeSeconds));
    chainSwapFadeCounter = 0;

    stageBiasSmoothed.reset (sampleRate, smoothingTimeSeconds);
    stageBiasSmoothed.setCurrentAndTargetValue (lastStageBiasScale);
    resonanceSmoothed.reset (sampleRate, smoothingTimeSeconds);
    resonanceSmoothed.setCurrentAndTargetValue (lastResonanceDb);
    sagSmoothed.reset (sampleRate, smoothingTimeSeconds);
    sagSmoothed.setCurrentAndTargetValue (lastSagAmount);

    toneStack.prepare (spec);

    presenceShelf.prepare (spec);
    presenceDbSmoothed.reset (sampleRate, smoothingTimeSeconds);
    presenceDbSmoothed.setCurrentAndTargetValue (lastPresenceDb);
    // Primed even though process() may skip the filter entirely at the
    // default 0 dB (see process()'s bypass check) - if the user restores a
    // non-default Presence value from a saved session, the very first block
    // must already reflect it rather than starting from an identity/
    // uninitialised coefficient set.
    *presenceShelf.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        sampleRate, presenceShelfFrequencyHz, presenceShelfQ,
        juce::Decibels::decibelsToGain (clampPresenceDb (lastPresenceDb)));

    gate.prepare (spec);

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

    // Reports the latency of whichever chain the current engine/quality
    // selection runs on, and aligns the dry path against it.
    applyChainSelection (false);

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

    if (triodeEcoOversampler != nullptr)
        triodeEcoOversampler->reset();

    if (triodeStandardOversampler != nullptr)
        triodeStandardOversampler->reset();

    if (triodeHqOversampler != nullptr)
        triodeHqOversampler->reset();

    triodeCascade.reset();
    powerAmp.reset();

    cascadeStage1.reset();
    cascadeStage2.reset();
    cascadeStage3.reset();

    cascadeStage1Loose.reset();
    cascadeStage2Loose.reset();
    cascadeStage3Loose.reset();

    toneStack.reset();
    presenceShelf.reset();
    gate.reset();
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

void TenebraeEngine::setPresenceDb (float newPresenceDb)
{
    lastPresenceDb = newPresenceDb;
    presenceDbSmoothed.setTargetValue (newPresenceDb);
}

void TenebraeEngine::setGateThresholdDb (float newThresholdDb)
{
    gate.setThresholdDb (newThresholdDb);
}

void TenebraeEngine::setGateAttackMs (float newAttackMs)
{
    gate.setAttackMs (newAttackMs);
}

void TenebraeEngine::setGateHoldMs (float newHoldMs)
{
    gate.setHoldMs (newHoldMs);
}

void TenebraeEngine::setGateReleaseMs (float newReleaseMs)
{
    gate.setReleaseMs (newReleaseMs);
}

void TenebraeEngine::setGateOn (bool shouldBeOn)
{
    gate.setEnabled (shouldBeOn);
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
    triodeCascade.setVoicing (currentVoicing);
}

//==============================================================================
// v0.3.0 parameters.

juce::dsp::Oversampling<float>* TenebraeEngine::getActiveOversampler() const noexcept
{
    if (engineMode == EngineMode::classic)
        return oversampler.get();

    switch (quality)
    {
        case Quality::eco:      return triodeEcoOversampler.get();
        case Quality::hq:       return triodeHqOversampler.get();
        case Quality::standard: break;
    }

    return triodeStandardOversampler.get();
}

void TenebraeEngine::applyChainSelection (bool resetChainState)
{
    auto* active = getActiveOversampler();

    if (active == nullptr)
        return;

    latencySamples = static_cast<int> (std::round (active->getLatencyInSamples()));

    // Safe on the audio thread: juce::dsp::DryWetMixer::setWetLatency() only
    // writes a delay length into an already-allocated delay line (sized
    // generously at construction - see the member's declaration).
    dryWetMixer.setWetLatency (static_cast<float> (latencySamples));

    // The triode cascade and power amp run inside the oversampled region, so
    // every one of their time constants and filter corners has to be
    // re-derived for the newly selected factor. This is a bounded, purely
    // arithmetic rebuild of already-allocated coefficient sets - the stage
    // LUTs themselves are rate-independent and are never rebuilt here.
    const auto oversampledRate = sampleRate * static_cast<double> (active->getOversamplingFactor());
    triodeCascade.setOversampledRate (oversampledRate);
    powerAmp.setOversampledRate (oversampledRate);

    if (resetChainState)
    {
        // Clear every nonlinear chain, not just the one being switched to.
        // A chain that is switched away from and later switched back to would
        // otherwise resume from state that is however many seconds stale, and
        // splicing that onto the live signal rings - measurably: switching
        // back to Classic a second time produced a peak 2.3x the settled
        // level even with the fade in place, because the fade covers the new
        // chain's ramp-in but not a stale chain's discontinuity.
        //
        // This runs only on a deliberate Engine/Quality change, so it cannot
        // affect a migrated v0.2 session, which never changes either.
        active->reset();
        triodeCascade.reset();
        powerAmp.reset();

        cascadeStage1.reset();
        cascadeStage2.reset();
        cascadeStage3.reset();
        cascadeStage1Loose.reset();
        cascadeStage2Loose.reset();
        cascadeStage3Loose.reset();

        // The new chain starts from zero state, so ramp its first samples in
        // rather than splicing them onto the previous chain's tail.
        chainSwapFadeCounter = chainSwapFadeSamples;
    }
}

void TenebraeEngine::setEngineMode (EngineMode newEngineMode)
{
    if (newEngineMode == engineMode)
        return;

    engineMode = newEngineMode;
    chainSelectionDirty = true;
}

void TenebraeEngine::setQuality (Quality newQuality)
{
    if (newQuality == quality)
        return;

    quality = newQuality;

    // Inert while Classic is selected: Classic has its own fixed 8x chain, so
    // there is nothing to re-derive and nothing to fade.
    if (engineMode == EngineMode::triode)
        chainSelectionDirty = true;
}

void TenebraeEngine::setStageBiasPercent (float newBiasPercent)
{
    const auto safe = std::isnan (newBiasPercent) ? 100.0f : juce::jlimit (0.0f, 200.0f, newBiasPercent);
    lastStageBiasScale = safe * 0.01f;
    stageBiasSmoothed.setTargetValue (lastStageBiasScale);
}

void TenebraeEngine::setPowerAmpOn (bool shouldBeOn)
{
    if (shouldBeOn == powerAmpOn)
        return;

    powerAmpOn = shouldBeOn;

    // Same pattern as Presence's bypassed->engaged transition below: a block
    // that has been structurally bypassed for an unknown number of blocks
    // must start from clean state, not from whatever its filters were left
    // holding.
    if (powerAmpOn)
        powerAmp.reset();
}

void TenebraeEngine::setResonanceDb (float newResonanceDb)
{
    const auto safe = std::isnan (newResonanceDb) ? 0.0f : juce::jlimit (0.0f, 12.0f, newResonanceDb);
    lastResonanceDb = safe;
    resonanceSmoothed.setTargetValue (safe);
}

void TenebraeEngine::setSagPercent (float newSagPercent)
{
    const auto safe = std::isnan (newSagPercent) ? 0.0f : juce::jlimit (0.0f, 100.0f, newSagPercent);
    lastSagAmount = safe * 0.01f;
    sagSmoothed.setTargetValue (lastSagAmount);
}

void TenebraeEngine::setGateKeySource (Gate::KeySource newKeySource)
{
    gate.setKeySource (newKeySource);
}

void TenebraeEngine::setGateHysteresisDb (float newHysteresisDb)
{
    gate.setHysteresisDb (newHysteresisDb);
}

void TenebraeEngine::setGateRange (float newRangeDb, bool isMute)
{
    gate.setRange (newRangeDb, isMute);
}

void TenebraeEngine::setGateReleaseMode (Gate::ReleaseMode newReleaseMode)
{
    gate.setReleaseMode (newReleaseMode);
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

    if (numSamples <= maxPreparedBlockSamples)
    {
        processChunk (block);
        return;
    }

    // Oversized block (larger than the maximumBlockSize declared to
    // prepare()) - see process()'s doc comment and GitHub issue #13. Chunk
    // it down rather than truncating: truncating would leave the excess
    // samples completely unprocessed (raw input passed through with no
    // Tight/Gain/cascade/tone-stack/Level/Mix applied at all), which is a
    // worse and more surprising failure mode than the extra CPU cost of an
    // additional chunk or two.
    size_t position = 0;

    while (position < numSamples)
    {
        const auto chunkSize = juce::jmin (maxPreparedBlockSamples, numSamples - position);
        auto chunk = block.getSubBlock (position, chunkSize);
        processChunk (chunk);
        position += chunkSize;
    }
}

void TenebraeEngine::processChunk (juce::dsp::AudioBlock<float>& block)
{
    const auto numSamples = block.getNumSamples();

    // Coefficient recomputation involves trig calls, so Tight is smoothed
    // and re-derived once per block rather than per sample - a standard
    // real-time-safe compromise for IIR filters, whose coefficients aren't
    // cheap to interpolate directly. Gain/Level still ramp sample-accurately
    // via juce::dsp::Gain's internal SmoothedValue, and Mix/tone-stack band
    // gains are re-applied once per block below.
    // An engine/quality change requested since the last block: re-derive the
    // newly selected chain's coefficients and latency at this block boundary,
    // never mid-block. Allocation-free (see applyChainSelection()).
    if (chainSelectionDirty)
    {
        chainSelectionDirty = false;
        applyChainSelection (true);
    }

    const auto tightHz = clampBelowNyquist (tightFrequencySmoothed.skip (static_cast<int> (numSamples)), sampleRate);
    const auto wetMix = mixSmoothed.skip (static_cast<int> (numSamples));
    const auto presenceDb = clampPresenceDb (presenceDbSmoothed.skip (static_cast<int> (numSamples)));

    // Triode-engine continuous controls. Like the tone stack's band gains
    // these are re-applied once per block from their smoothers rather than
    // recomputed per sample: bias depth is a plain scalar (no zipper risk),
    // and Resonance/Sag feed filter coefficients whose recomputation involves
    // trig, so the standard per-block compromise applies.
    const auto stageBiasScale = stageBiasSmoothed.skip (static_cast<int> (numSamples));
    const auto resonanceDb = resonanceSmoothed.skip (static_cast<int> (numSamples));
    const auto sagAmount = sagSmoothed.skip (static_cast<int> (numSamples));

    const auto runningTriode = (engineMode == EngineMode::triode);
    const auto runningPowerAmp = runningTriode && powerAmpOn;

    if (runningTriode)
    {
        triodeCascade.setBiasScale (static_cast<double> (stageBiasScale));

        if (runningPowerAmp)
        {
            powerAmp.setResonanceDb (resonanceDb);
            powerAmp.setSagAmount (sagAmount);
            // With the power amp engaged, Presence is a feedback-return
            // control rather than a post-EQ shelf (brief 3.2) - the post-EQ
            // shelf is structurally bypassed below.
            powerAmp.setPresenceDb (presenceDb);
        }
    }

    *tightHighPass.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, tightHz, filterQ);
    dryWetMixer.setWetMixProportion (wetMix);
    toneStack.updateCoefficients (static_cast<int> (numSamples));

    // Presence within presenceBypassEpsilonDb of exactly 0 dB is treated as
    // an explicit "off" position: the shelf's process() call is skipped
    // entirely below (not merely computed as a near-unity filter), so the
    // default state is a true bit-accurate passthrough rather than an
    // approximately-flat shelf - see docs/design-brief.md section 4's
    // Presence passthrough test guarantee and sibling plugin nave's
    // CabConvolutionEngine.cpp for the same established pattern (LoCut/
    // HiCut/Distance at their own "off" positions).
    // With the power amp engaged, Presence has already been handed to the
    // feedback return path above, so the post-EQ shelf is skipped outright
    // (not merely flattened) - the brief's "skipped when PowerAmp on".
    const bool presenceBypassed = runningPowerAmp || std::abs (presenceDb) <= presenceBypassEpsilonDb;

    if (! presenceBypassed && ! presenceEngagedPreviously)
        presenceShelf.reset();

    presenceEngagedPreviously = ! presenceBypassed;

    if (! presenceBypassed)
    {
        *presenceShelf.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
            sampleRate, presenceShelfFrequencyHz, presenceShelfQ, juce::Decibels::decibelsToGain (presenceDb));
    }

    // Non-finite input guard. A single Inf or NaN sample reaching any of the
    // one-pole/IIR states below latches there permanently (y += a*(x - y) with
    // x = Inf leaves y = Inf for good), and every subsequent block comes out
    // NaN even after the input goes clean again - which
    // tests/RobustnessTests.cpp (T-X2) measures directly. Replacing it at the
    // boundary protects every downstream filter in both engines at the cost of
    // one branch per sample.
    //
    // This cannot affect the bit-identity guarantee: for finite input the
    // sample is passed through untouched, and every render the migration gates
    // compare is finite by construction.
    for (size_t channel = 0; channel < block.getNumChannels(); ++channel)
    {
        auto* data = block.getChannelPointer (channel);

        for (size_t sample = 0; sample < numSamples; ++sample)
            if (! std::isfinite (data[sample]))
                data[sample] = 0.0f;
    }

    juce::dsp::ProcessContextReplacing<float> context (block);

    // Capture the pre-processing signal as "dry" before any wet-path
    // filtering touches `block`. DryWetMixer internally delays this by
    // getLatencySamples() (set via setWetLatency in prepare()) so it stays
    // time-aligned with the oversampled wet path below.
    dryWetMixer.pushDrySamples (block);

    // Gate v2 pre-distortion key tap (Gate.h): a copy of the plugin input
    // taken here, before the Tight HPF and the gain cascade have had any
    // chance to squash the 40+ dB difference between "noise floor" and
    // "playing" that the detector needs to see. Copied unconditionally
    // (a bounded memcpy per block) rather than branched on the key setting,
    // so switching the key source mid-stream crossfades from a detector that
    // is already running rather than from a cold one.
    const auto keyChannels = juce::jmin (static_cast<size_t> (gateKeyBuffer.getNumChannels()),
                                         block.getNumChannels());

    for (size_t channel = 0; channel < keyChannels; ++channel)
    {
        gateKeyBuffer.copyFrom (static_cast<int> (channel), 0,
                                block.getChannelPointer (channel),
                                static_cast<int> (numSamples));
        gateKeyPointers[channel] = gateKeyBuffer.getReadPointer (static_cast<int> (channel));
    }

    tightHighPass.process (context);
    brightShelf.process (context);
    // See GitHub issue #12/RealtimeGain.h: routes around
    // juce::dsp::Gain::process()'s multichannel-branch alloca().
    RealtimeGain::process (preGain, context, hostRateGainScratch.data(), hostRateGainScratch.size());

    auto* activeOversampler = runningTriode ? getActiveOversampler() : oversampler.get();

    auto oversampledBlock = activeOversampler->processSamplesUp (block);
    juce::dsp::ProcessContextReplacing<float> oversampledContext (oversampledBlock);

    if (! runningTriode)
    {
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
    }
    else
    {
        // The whole nonlinear chain runs inside ONE oversampled region
        // (research-triode-adaa.md section 3.3): oversampling each stage
        // separately would let each stage's above-Nyquist harmonics fold
        // before the next stage ever sees them, and ADAA cannot undo that.
        const auto oversampledChannels = oversampledBlock.getNumChannels();
        const auto oversampledSamples = oversampledBlock.getNumSamples();

        for (size_t channel = 0; channel < oversampledChannels; ++channel)
        {
            auto* data = oversampledBlock.getChannelPointer (channel);

            for (size_t sample = 0; sample < oversampledSamples; ++sample)
            {
                auto value = triodeCascade.processSample (static_cast<double> (data[sample]), channel);

                if (runningPowerAmp)
                    value = powerAmp.processSample (value, channel);

                data[sample] = static_cast<float> (value);
            }
        }
    }

    activeOversampler->processSamplesDown (block);

    // Click suppression after an engine/quality swap - see the member's
    // declaration. Applied to the freshly generated wet signal, before any
    // stateful host-rate module downstream sees it.
    if (chainSwapFadeCounter > 0)
    {
        const auto fadeSamples = juce::jmin (static_cast<size_t> (chainSwapFadeCounter), numSamples);

        for (size_t channel = 0; channel < block.getNumChannels(); ++channel)
        {
            auto* data = block.getChannelPointer (channel);

            for (size_t sample = 0; sample < fadeSamples; ++sample)
            {
                const auto remaining = chainSwapFadeCounter - static_cast<int> (sample);
                const auto ramp = 1.0f - static_cast<float> (remaining)
                                            / static_cast<float> (chainSwapFadeSamples + 1);
                data[sample] *= ramp;
            }
        }

        chainSwapFadeCounter -= static_cast<int> (fadeSamples);
    }

    toneStack.process (context);

    if (! presenceBypassed)
        presenceShelf.process (context);

    // Gate: gates the fully-voiced wet signal (post-Presence, pre-Level) -
    // see Gate.h. A true no-op/structural bypass when disabled
    // (Gate::process() returns immediately without touching `context` or any
    // internal state - see its docs).
    //
    // The key buffer is handed over per block and consumed by process(); with
    // the default Post key source the gate ignores it entirely and reads the
    // block itself, exactly as in v0.2.
    if (keyChannels > 0)
        gate.setKeyBuffer (gateKeyPointers.data(), keyChannels, numSamples);

    gate.process (context);

    // See GitHub issue #12/RealtimeGain.h: same rationale as preGain above.
    RealtimeGain::process (outputLevel, context, hostRateGainScratch.data(), hostRateGainScratch.size());

    dryWetMixer.mixWetSamples (block);
}
