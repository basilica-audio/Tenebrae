#include "Gate.h"

#include <algorithm>
#include <cmath>

namespace
{
    // Denormal bias for the log-domain envelopes (brief section 3.4 item 5).
    constexpr float envelopeDenormalBias = 1.0e-30f;

    constexpr float butterworthQ = 0.7071067811865476f;

    // ln(10)/20 - converts a dB-per-sample step into a multiplicative gain
    // factor via a single exp().
    constexpr double dbToNeperScale = 0.11512925464970229;
}

Gate::Gate() = default;

float Gate::computeRampCoefficient (float timeMs, double sr) noexcept
{
    const auto timeSeconds = juce::jmax (0.0001f, timeMs * 0.001f);
    return 1.0f - std::exp (-1.0f / (static_cast<float> (sr) * timeSeconds));
}

void Gate::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    // Re-derive every coefficient from the last commanded values (see the
    // header's doc comment on lastThresholdDb/lastAttackMs/lastHoldMs/
    // lastReleaseMs) rather than resetting to built-in defaults, so a
    // re-prepare (sample-rate change, etc.) never silently discards a live
    // parameter value.
    setThresholdDb (lastThresholdDb);
    setAttackMs (lastAttackMs);
    setHoldMs (lastHoldMs);
    setReleaseMs (lastReleaseMs);

    tvpFastAttackCoefficient = computeRampCoefficient (tvpFastAttackMs, sampleRate);
    tvpFastReleaseCoefficient = computeRampCoefficient (tvpFastReleaseMs, sampleRate);
    tvpSlopeSmoothingCoefficient = computeRampCoefficient (tvpSlopeSmoothingMs, sampleRate);

    keyMixIncrement = 1.0f / juce::jmax (1.0f, static_cast<float> (sampleRate) * keyCrossfadeMs * 0.001f);

    // One detector band-pass per key channel, sized here (never on the audio
    // thread). Sized generously so a later mono/stereo renegotiation cannot
    // outgrow it.
    const auto channelCount = static_cast<size_t> (juce::jmax (2u, spec.numChannels));
    keyHighPassFilters.resize (channelCount);
    keyLowPassFilters.resize (channelCount);
    updateKeyFilters();

    reset();
}

void Gate::updateKeyFilters()
{
    const auto nyquist = static_cast<float> (sampleRate) * 0.5f;
    const auto highPassHz = juce::jlimit (10.0f, nyquist * 0.9f, keyHighPassHz);
    const auto lowPassHz = juce::jlimit (highPassHz * 1.1f, nyquist * 0.9f, keyLowPassHz);

    const auto highPass = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, highPassHz, butterworthQ);
    const auto lowPass = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, lowPassHz, butterworthQ);

    for (auto& filter : keyHighPassFilters)
        filter.coefficients = highPass;

    for (auto& filter : keyLowPassFilters)
        filter.coefficients = lowPass;
}

void Gate::reset()
{
    currentGain = 1.0f;
    holdCounter = 0;
    gateLatched = false;

    keyMix = (keySource == KeySource::pre) ? 1.0f : 0.0f;

    fastEnvelopeDb = envelopeFloorDb;
    slowEnvelopeDb = envelopeFloorDb;
    trackedSlopeDbPerSecond = 0.0f;

    for (auto& filter : keyHighPassFilters)
        filter.reset();

    for (auto& filter : keyLowPassFilters)
        filter.reset();

    keyChannelPointers = nullptr;
    numKeyChannels = 0;
    numKeySamples = 0;
}

//==============================================================================
void Gate::updateThresholds() noexcept
{
    // At H = 0 the two thresholds must be bit-identical, not merely equal to
    // within a dB conversion round trip - that identity is what makes the
    // superset reduce to the v0.2 expressions exactly (T-G1).
    if (hysteresisDb <= 0.0f)
    {
        closeThresholdLinear = thresholdLinear;
        return;
    }

    const auto safeDb = std::isnan (lastThresholdDb) ? -48.0f : lastThresholdDb;
    const auto openDb = juce::jlimit (minThresholdDb, maxThresholdDb, safeDb);
    closeThresholdLinear = juce::Decibels::decibelsToGain (openDb - hysteresisDb, -400.0f);
}

void Gate::setThresholdDb (float newThresholdDb)
{
    lastThresholdDb = newThresholdDb;
    const auto safeDb = std::isnan (newThresholdDb) ? -48.0f : newThresholdDb;
    thresholdLinear = juce::Decibels::decibelsToGain (juce::jlimit (minThresholdDb, maxThresholdDb, safeDb));
    updateThresholds();
}

void Gate::setAttackMs (float newAttackMs)
{
    lastAttackMs = newAttackMs;
    const auto safeMs = std::isnan (newAttackMs) ? 1.0f : juce::jlimit (minAttackMs, maxAttackMs, newAttackMs);
    attackCoefficient = computeRampCoefficient (safeMs, sampleRate);
}

void Gate::setHoldMs (float newHoldMs)
{
    lastHoldMs = newHoldMs;
    const auto safeMs = std::isnan (newHoldMs) ? 20.0f : juce::jlimit (minHoldMs, maxHoldMs, newHoldMs);
    holdSamples = static_cast<int> (std::round (static_cast<double> (safeMs) * 0.001 * sampleRate));
}

void Gate::setReleaseMs (float newReleaseMs)
{
    lastReleaseMs = newReleaseMs;
    const auto safeMs = std::isnan (newReleaseMs) ? 150.0f : juce::jlimit (minReleaseMs, maxReleaseMs, newReleaseMs);
    releaseCoefficient = computeRampCoefficient (safeMs, sampleRate);
}

void Gate::setEnabled (bool shouldBeEnabled)
{
    enabled = shouldBeEnabled;
}

void Gate::setKeySource (KeySource newKeySource) noexcept
{
    keySource = newKeySource;
}

void Gate::setHysteresisDb (float newHysteresisDb) noexcept
{
    const auto safeDb = std::isnan (newHysteresisDb) ? 0.0f : newHysteresisDb;
    const auto clamped = juce::jlimit (minHysteresisDb, maxHysteresisDb, safeDb);

    if (clamped == hysteresisDb)
        return;

    hysteresisDb = clamped;
    updateThresholds();
}

void Gate::setRange (float newRangeDb, bool isMute) noexcept
{
    rangeIsMute = isMute;

    if (isMute)
    {
        // Exactly the v0.2 target. Not "-90 dB", not "very small" - zero.
        closedTargetGain = 0.0f;
        return;
    }

    const auto safeDb = std::isnan (newRangeDb) ? maxRangeDb : newRangeDb;
    const auto clamped = juce::jlimit (minRangeDb, maxRangeDb, safeDb);
    closedTargetGain = juce::Decibels::decibelsToGain (-clamped, -400.0f);
}

void Gate::setReleaseMode (ReleaseMode newReleaseMode) noexcept
{
    releaseMode = newReleaseMode;
}

void Gate::setKeyBuffer (const float* const* keyChannels, size_t channelCount, size_t sampleCount) noexcept
{
    keyChannelPointers = keyChannels;
    numKeyChannels = keyChannels != nullptr ? channelCount : 0;
    numKeySamples = keyChannels != nullptr ? sampleCount : 0;
}

//==============================================================================
float Gate::processKeySample (size_t sample) noexcept
{
    if (keyChannelPointers == nullptr || numKeyChannels == 0 || sample >= numKeySamples)
        return 0.0f;

    const auto usableChannels = std::min (numKeyChannels, keyHighPassFilters.size());

    float peak = 0.0f;

    for (size_t channel = 0; channel < usableChannels; ++channel)
    {
        const auto* source = keyChannelPointers[channel];

        if (source == nullptr)
            continue;

        auto value = source[sample];

        // Keep a NaN/Inf key sample out of the detector's IIR state: the
        // band-pass would latch it permanently (same rationale as
        // TenebraeEngine's clampBelowNyquist).
        if (! std::isfinite (value))
            value = 0.0f;

        value = keyHighPassFilters[channel].processSample (value);
        value = keyLowPassFilters[channel].processSample (value);

        peak = std::max (peak, std::abs (value));
    }

    return peak;
}

//==============================================================================
void Gate::process (juce::dsp::ProcessContextReplacing<float>& context)
{
    if (! enabled || context.isBypassed)
        return; // true structural bypass - see setEnabled()'s docs.

    auto block = context.getOutputBlock();
    const auto numChannels = block.getNumChannels();
    const auto numSamples = block.getNumSamples();

    const auto keyTarget = (keySource == KeySource::pre && keyChannelPointers != nullptr) ? 1.0f : 0.0f;
    const auto usingTvp = (releaseMode == ReleaseMode::automatic);

    for (size_t sample = 0; sample < numSamples; ++sample)
    {
        float peak = 0.0f;

        for (size_t channel = 0; channel < numChannels; ++channel)
            peak = std::max (peak, std::abs (block.getChannelPointer (channel)[sample]));

        // ---- Key selection -----------------------------------------------
        // The Post branch below is a plain assignment, never an arithmetic
        // blend, so at the default key source the detector value is
        // bit-identical to v0.2 regardless of what the key buffer holds.
        if (keyMix < keyTarget)
            keyMix = std::min (keyTarget, keyMix + keyMixIncrement);
        else if (keyMix > keyTarget)
            keyMix = std::max (keyTarget, keyMix - keyMixIncrement);

        if (keyMix > 0.0f)
        {
            const auto keyPeak = processKeySample (sample);
            peak = (keyMix >= 1.0f) ? keyPeak : peak + keyMix * (keyPeak - peak);
        }

        // ---- State machine ------------------------------------------------
        // At H = 0 closeThresholdLinear IS thresholdLinear (bit-identical, see
        // updateThresholds()), so gateLatched cannot change the outcome and
        // these three lines are the v0.2 expressions verbatim.
        const auto armThreshold = gateLatched ? closeThresholdLinear : thresholdLinear;
        const auto above = peak >= armThreshold;

        if (above)
        {
            holdCounter = holdSamples;
            gateLatched = true;
        }
        else if (holdCounter > 0)
        {
            --holdCounter;
        }

        const auto gateShouldBeOpen = above || (holdCounter > 0);

        if (! gateShouldBeOpen)
            gateLatched = false;

        const auto targetGain = gateShouldBeOpen ? 1.0f : closedTargetGain;

        // ---- Ballistics ---------------------------------------------------
        if (targetGain > currentGain || ! usingTvp)
        {
            // v0.2 path, unchanged: one-pole toward the target. Also the
            // opening path in Auto mode, since TVP only governs release.
            const auto coefficient = (targetGain > currentGain) ? attackCoefficient : releaseCoefficient;
            currentGain += (targetGain - currentGain) * coefficient;
        }
        else
        {
            // ---- Program-dependent (TVP) release --------------------------
            // Two envelopes race off the same detector. While they agree, the
            // note is still ringing and the gate fades at the note's own
            // measured decay rate plus a small margin - just ahead of the
            // note, so the fade hides under its masking. When they diverge by
            // more than the window the note has actually stopped, and the
            // gate dumps to a fast dB-linear release.
            const auto peakDb = juce::jmax (envelopeFloorDb,
                                            20.0f * std::log10 (peak + envelopeDenormalBias));

            const auto fastCoefficient = (peakDb > fastEnvelopeDb) ? tvpFastAttackCoefficient
                                                                   : tvpFastReleaseCoefficient;
            fastEnvelopeDb += (peakDb - fastEnvelopeDb) * fastCoefficient;

            const auto previousSlowDb = slowEnvelopeDb;
            const auto selfReleased = slowEnvelopeDb
                                       - tvpSlowReleaseDbPerSecond / static_cast<float> (sampleRate);
            slowEnvelopeDb = juce::jmax (fastEnvelopeDb, selfReleased);

            float slopeDbPerSecond;

            if ((slowEnvelopeDb - fastEnvelopeDb) > tvpWindowDb)
            {
                slowEnvelopeDb = fastEnvelopeDb; // dump
                slopeDbPerSecond = tvpDumpReleaseDbPerSecond;
            }
            else
            {
                // Decay-tracking: estimate how fast the programme itself is
                // falling, then fade slightly faster than that.
                const auto instantaneousSlope = juce::jlimit (
                    0.0f,
                    tvpMaxTrackedSlopeDbPerSecond,
                    (previousSlowDb - slowEnvelopeDb) * static_cast<float> (sampleRate));

                trackedSlopeDbPerSecond += (instantaneousSlope - trackedSlopeDbPerSecond)
                                            * tvpSlopeSmoothingCoefficient;
                slopeDbPerSecond = trackedSlopeDbPerSecond + tvpTrackMarginDbPerSecond;
            }

            // dB-linear ramp: one exp() per sample rather than a log/pow pair.
            const auto decay = static_cast<float> (
                std::exp (-static_cast<double> (slopeDbPerSecond) * dbToNeperScale / sampleRate));

            currentGain *= decay;

            if (currentGain < targetGain)
                currentGain = targetGain;
        }

        for (size_t channel = 0; channel < numChannels; ++channel)
        {
            auto* data = block.getChannelPointer (channel);
            data[sample] *= currentGain;
        }
    }

    // The key buffer only ever describes the block that was just processed.
    keyChannelPointers = nullptr;
    numKeyChannels = 0;
    numKeySamples = 0;
}
