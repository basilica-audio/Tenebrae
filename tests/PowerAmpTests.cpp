#include "dsp/PowerAmp.h"
#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

// Measurable assertions for the power-amp block (brief section 6,
// T-P1..T-P5). T-P5 in particular is the CI gate that turns the stability
// bound from a design intention into something the build refuses to ship
// without.

namespace
{
    constexpr double oversampledRate = 192000.0;

    // Small-signal magnitude of the block at `frequencyHz`, measured by
    // quadrature demodulation at a level far below the transformer's knee so
    // the reading is a linear response and not a distortion product.
    double measureMagnitude (PowerAmp& amp, double frequencyHz, double amplitude = 0.001)
    {
        amp.reset();

        const auto settle = static_cast<int> (oversampledRate * 0.5);
        const auto cycles = std::max (20, static_cast<int> (frequencyHz * 0.05));
        const auto measure = static_cast<int> (std::round (cycles * oversampledRate / frequencyHz));

        double real = 0.0;
        double imaginary = 0.0;

        for (int i = 0; i < settle + measure; ++i)
        {
            const auto phase = juce::MathConstants<double>::twoPi * frequencyHz * i / oversampledRate;
            const auto y = amp.processSample (amplitude * std::sin (phase), 0);

            if (i >= settle)
            {
                real += y * std::sin (phase);
                imaginary += y * std::cos (phase);
            }
        }

        return 2.0 * std::hypot (real, imaginary) / (measure * amplitude);
    }

    void setParam (TenebraeAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }
}

//==============================================================================
// T-P5 first: everything else about this block is only meaningful if the loop
// is stable, and stability here is a property that can be computed rather
// than hoped for.
TEST_CASE ("T-P5: the small-signal loop gain stays at or below 0.5 over the whole shelf grid",
           "[poweramp][stability]")
{
    PowerAmp amp;

    SECTION ("worst-case loop gain over resonance x presence x every quality mode")
    {
        // Every quality mode means every oversampled rate the block can be
        // asked to run at. Stability of a unit-delay loop is independent of
        // the oversampling rate - which is exactly why it must be checked at
        // all of them rather than assumed away by "we oversample".
        for (const double rate : { 44100.0 * 2.0, 44100.0 * 4.0, 44100.0 * 8.0,
                                   48000.0 * 2.0, 48000.0 * 4.0, 48000.0 * 8.0,
                                   96000.0 * 2.0, 96000.0 * 4.0, 96000.0 * 8.0 })
        {
            amp.prepare (rate, 1);

            for (float resonance = 0.0f; resonance <= 12.0f; resonance += 1.0f)
            {
                for (float presence = -12.0f; presence <= 12.0f; presence += 1.0f)
                {
                    amp.setResonanceDb (resonance);
                    amp.setPresenceDb (presence);

                    const auto worst = amp.getWorstCaseLoopGain();

                    INFO ("rate " << rate << ", resonance " << resonance << " dB, presence " << presence << " dB");
                    CHECK (worst <= PowerAmp::maximumLoopGain + 1.0e-9);
                }
            }
        }
    }

    SECTION ("both return shelves are cut-only, so they can never raise the loop gain")
    {
        amp.prepare (oversampledRate, 1);

        // Neutral shelves are the worst case by construction. If a shelf ever
        // became a boost this would fail before the sweep above even ran.
        amp.setResonanceDb (0.0f);
        amp.setPresenceDb (-12.0f);
        const auto neutralWorst = amp.getWorstCaseLoopGain();

        CHECK (neutralWorst == Catch::Approx (PowerAmp::forwardGain * PowerAmp::feedbackFraction).epsilon (1.0e-6));

        for (float resonance = 0.0f; resonance <= 12.0f; resonance += 0.5f)
        {
            amp.setResonanceDb (resonance);
            CHECK (amp.getWorstCaseLoopGain() <= neutralWorst + 1.0e-12);
        }

        CHECK (amp.getResonanceShelfCutDb() <= 0.0);
        CHECK (amp.getPresenceShelfCutDb() <= 0.0);
        CHECK (amp.getPresenceShelfCutDb() >= -12.0);
    }

    SECTION ("the linearised loop's impulse response decays below -100 dB within 20 ms")
    {
        amp.prepare (oversampledRate, 1);
        amp.setResonanceDb (12.0f);  // deepest feedback shaping
        amp.setPresenceDb (12.0f);
        amp.reset();

        const auto samples = static_cast<int> (oversampledRate * 0.02);

        // Driven at a level far below the transformer's knee, the block is
        // its own linearisation.
        auto response = amp.processSample (1.0e-6, 0);

        double peakAfterFirst = 0.0;
        double previousWindowPeak = std::abs (response);
        double currentWindowPeak = 0.0;

        const auto windowSamples = static_cast<int> (oversampledRate * 0.001);
        bool envelopeIsMonotonic = true;

        for (int i = 1; i < samples; ++i)
        {
            response = amp.processSample (0.0, 0);
            REQUIRE (std::isfinite (response));

            currentWindowPeak = std::max (currentWindowPeak, std::abs (response));

            if (i % windowSamples == 0)
            {
                if (currentWindowPeak > previousWindowPeak)
                    envelopeIsMonotonic = false;

                previousWindowPeak = currentWindowPeak;
                currentWindowPeak = 0.0;
            }

            // The brief's gate is "decays below -100 dB within 20 ms", so the
            // measurement is the tail of that window. The loop's own pole is
            // at -L (0.5, i.e. gone within a handful of samples); what
            // actually sets the decay here is the 120 Hz return shelf's pole,
            // whose time constant is ~1.3 ms - hence measuring at 20 ms
            // rather than at 1 ms.
            if (i > samples - windowSamples)
                peakAfterFirst = std::max (peakAfterFirst, std::abs (response));
        }

        INFO ("peak in the final millisecond of the 20 ms window = "
              << TestHelpers::toDecibels (peakAfterFirst / 1.0e-6) << " dB");
        CHECK (envelopeIsMonotonic);
        CHECK (TestHelpers::toDecibels (peakAfterFirst / 1.0e-6) < -100.0);
    }
}

//==============================================================================
// T-P1: Resonance.
TEST_CASE ("T-P1: Resonance raises the closed-loop LF gain and leaves the top alone",
           "[poweramp][resonance]")
{
    PowerAmp amp;
    amp.prepare (oversampledRate, 1);
    amp.setSagAmount (0.0f);
    amp.setPresenceDb (0.0f);

    amp.setResonanceDb (0.0f);
    const auto neutralAt100 = measureMagnitude (amp, 100.0);
    const auto neutralAt1k = measureMagnitude (amp, 1000.0);
    const auto neutralAt5k = measureMagnitude (amp, 5000.0);

    amp.setResonanceDb (12.0f);
    const auto boostedAt100 = measureMagnitude (amp, 100.0);
    const auto boostedAt1k = measureMagnitude (amp, 1000.0);
    const auto boostedAt5k = measureMagnitude (amp, 5000.0);

    // The expected delta is not a magic number: it falls straight out of the
    // closed-loop factor 1/(1 + L) with L bounded at 0.5, computed by the
    // block itself from the very coefficients it is running.
    amp.setResonanceDb (0.0f);
    const auto expectedNeutral = amp.closedLoopMagnitudeAt (100.0);
    amp.setResonanceDb (12.0f);
    const auto expectedBoosted = amp.closedLoopMagnitudeAt (100.0);

    const auto expectedDeltaDb = TestHelpers::toDecibels (expectedBoosted / expectedNeutral);
    const auto measuredDeltaDb = TestHelpers::toDecibels (boostedAt100 / neutralAt100);

    INFO ("expected LF delta = " << expectedDeltaDb << " dB, measured = " << measuredDeltaDb << " dB");
    CHECK (std::abs (measuredDeltaDb - expectedDeltaDb) < 1.5);

    // It must be a real, audible move rather than a gesture. Measured at
    // 40 Hz as well as 100 Hz: the return shelf's corner is 120 Hz, and a
    // first-order shelf only reaches its full cut well below its corner, so
    // 100 Hz sees about half the available depth by construction.
    amp.setResonanceDb (0.0f);
    const auto neutralAt40 = measureMagnitude (amp, 40.0);
    amp.setResonanceDb (12.0f);
    const auto boostedAt40 = measureMagnitude (amp, 40.0);

    const auto deltaAt40 = TestHelpers::toDecibels (boostedAt40 / neutralAt40);
    INFO ("delta at 40 Hz = " << deltaAt40 << " dB");
    CHECK (deltaAt40 > 1.5);
    CHECK (deltaAt40 > measuredDeltaDb); // deeper the further below the corner

    // And it must stay out of the way above 1 kHz.
    const auto deltaAt1k = TestHelpers::toDecibels (boostedAt1k / neutralAt1k);
    const auto deltaAt5k = TestHelpers::toDecibels (boostedAt5k / neutralAt5k);

    INFO ("delta at 1 kHz = " << deltaAt1k << " dB, at 5 kHz = " << deltaAt5k << " dB");
    CHECK (std::abs (deltaAt1k) < 0.5);
    CHECK (std::abs (deltaAt5k) < 0.5);
}

//==============================================================================
// T-P2: Presence in the feedback return path.
TEST_CASE ("T-P2: Presence moves the top monotonically when the power amp is on, and is inert when it is off",
           "[poweramp][presence]")
{
    SECTION ("monotonic HF delta, matching the design curve")
    {
        PowerAmp amp;
        amp.prepare (oversampledRate, 1);
        amp.setSagAmount (0.0f);
        amp.setResonanceDb (0.0f);

        double previousMagnitude = 0.0;

        for (const float presence : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
        {
            amp.setPresenceDb (presence);

            const auto measured = measureMagnitude (amp, 6000.0);
            const auto expected = amp.closedLoopMagnitudeAt (6000.0);

            INFO ("presence " << presence << " dB: measured " << TestHelpers::toDecibels (measured)
                              << " dB, design " << TestHelpers::toDecibels (expected) << " dB");

            CHECK (std::abs (TestHelpers::toDecibels (measured / expected)) < 1.5);
            CHECK (measured > previousMagnitude); // strictly monotonic

            previousMagnitude = measured;
        }
    }

    SECTION ("with the power amp off the response is the untouched v0.2 shelf")
    {
        // Structural, not approximate: with Power Amp off, nothing in the
        // v0.2 path changes at all, so two renders with the engine at Classic
        // and Power Amp respectively off and (pointlessly) on must be
        // byte-identical - the power-amp branch is simply never entered.
        constexpr double rate = 48000.0;
        constexpr int blockSize = 256;
        constexpr int totalSamples = 24000;

        const auto render = [&] (bool powerAmpOn)
        {
            TenebraeAudioProcessor processor;
            processor.prepareToPlay (rate, blockSize);

            setParam (processor, ParamIDs::engine, 0.0f); // Classic
            setParam (processor, ParamIDs::powerAmp, powerAmpOn ? 1.0f : 0.0f);
            setParam (processor, ParamIDs::presence, 6.0f);
            setParam (processor, ParamIDs::resonance, 12.0f);
            setParam (processor, ParamIDs::sag, 100.0f);

            // Cleared, and only whole blocks are rendered: an uncleared tail
            // would compare uninitialised memory and fail at random.
            juce::AudioBuffer<float> result (2, (totalSamples / blockSize) * blockSize);
            result.clear();

            juce::AudioBuffer<float> block (2, blockSize);
            juce::MidiBuffer midi;

            for (int position = 0; position + blockSize <= result.getNumSamples(); position += blockSize)
            {
                TestHelpers::fillWithSine (block, rate, 440.0, 0.3f, position);
                processor.processBlock (block, midi);

                for (int channel = 0; channel < 2; ++channel)
                    result.copyFrom (channel, position, block, channel, 0, blockSize);
            }

            return result;
        };

        const auto withPowerAmpOff = render (false);
        const auto withPowerAmpOn = render (true);

        // In-process byte equality only (brief section 6's platform note).
        CHECK (TestHelpers::buffersAreByteIdentical (withPowerAmpOff, withPowerAmpOn));
    }
}

//==============================================================================
// T-P3: sag, measured as gain reduction against time.
TEST_CASE ("T-P3: sag develops 1-3 dB of droop with the specified attack and recovery",
           "[poweramp][sag]")
{
    PowerAmp amp;
    amp.prepare (oversampledRate, 1);
    amp.setResonanceDb (0.0f);
    amp.setPresenceDb (0.0f);

    // Envelope of the block's output for a sustained -6 dBFS tone, sampled
    // per cycle, with and without sag engaged. The ratio is the sag-induced
    // gain reduction over time.
    const auto envelopeFor = [&] (float sagAmount)
    {
        amp.setSagAmount (sagAmount);
        amp.reset();

        constexpr double toneHz = 100.0;
        const auto samplesPerCycle = static_cast<int> (oversampledRate / toneHz);
        const auto totalSamples = static_cast<int> (oversampledRate * 0.5);

        std::vector<double> envelope;
        double cyclePeak = 0.0;

        for (int i = 0; i < totalSamples; ++i)
        {
            const auto phase = juce::MathConstants<double>::twoPi * toneHz * i / oversampledRate;
            const auto y = amp.processSample (0.5 * std::sin (phase), 0);

            cyclePeak = std::max (cyclePeak, std::abs (y));

            if ((i + 1) % samplesPerCycle == 0)
            {
                envelope.push_back (cyclePeak);
                cyclePeak = 0.0;
            }
        }

        return envelope;
    };

    const auto withoutSag = envelopeFor (0.0f);
    const auto withSag = envelopeFor (1.0f);

    REQUIRE (withSag.size() == withoutSag.size());
    REQUIRE (withSag.size() > 20);

    // Compression depth: the steady-state droop relative to the very first
    // cycle, once the sag envelope has settled.
    const auto initial = TestHelpers::toDecibels (withSag.front() / withoutSag.front());
    const auto settled = TestHelpers::toDecibels (withSag.back() / withoutSag.back());
    const auto depthDb = initial - settled;

    INFO ("sag depth = " << depthDb << " dB (initial " << initial << ", settled " << settled << ")");
    CHECK (depthDb >= 1.0);
    CHECK (depthDb <= 3.0);

    SECTION ("the sag envelope runs at the specified attack and release constants")
    {
        amp.setSagAmount (1.0f);
        amp.reset();

        constexpr double toneHz = 100.0;

        // ---- Attack: 5 ms +/- 50 % ---------------------------------------
        std::vector<double> attackTimes;
        std::vector<double> attackLogDeficits;

        const auto attackSamples = static_cast<int> (oversampledRate * 0.05);
        double finalEnvelope = 0.0;

        std::vector<double> attackEnvelope;
        attackEnvelope.reserve (static_cast<size_t> (attackSamples));

        for (int i = 0; i < attackSamples; ++i)
        {
            const auto phase = juce::MathConstants<double>::twoPi * toneHz * i / oversampledRate;
            amp.processSample (0.9 * std::sin (phase), 0);
            attackEnvelope.push_back (amp.getSagEnvelope (0));
        }

        finalEnvelope = attackEnvelope.back();

        // Sample every 0.2 ms across the first three time constants.
        const auto attackStride = static_cast<int> (oversampledRate * 0.0002);

        for (int i = attackStride; i < static_cast<int> (oversampledRate * 0.015); i += attackStride)
        {
            const auto deficit = finalEnvelope - attackEnvelope[static_cast<size_t> (i)];

            if (deficit > 1.0e-6)
            {
                attackTimes.push_back (i / oversampledRate);
                attackLogDeficits.push_back (std::log (deficit));
            }
        }

        REQUIRE (attackTimes.size() > 10);

        const auto attackFit = TestHelpers::fitLine (attackTimes, attackLogDeficits);
        const auto attackTau = -1.0 / attackFit.slope;

        INFO ("fitted sag attack tau = " << attackTau * 1000.0 << " ms, R^2 = " << attackFit.rSquared);
        CHECK (attackTau > PowerAmp::sagAttackSeconds * 0.5);
        CHECK (attackTau < PowerAmp::sagAttackSeconds * 1.5);

        // ---- Release: 120 ms +/- 30 % ------------------------------------
        std::vector<double> releaseTimes;
        std::vector<double> releaseLogEnvelopes;

        const auto releaseSamples = static_cast<int> (oversampledRate * 0.4);
        const auto releaseStride = static_cast<int> (oversampledRate * 0.002);

        for (int i = 0; i < releaseSamples; ++i)
        {
            const auto envelope = amp.processSample (0.0, 0), unusedOutput = envelope;
            juce::ignoreUnused (unusedOutput);

            if (i > 0 && i % releaseStride == 0 && i < static_cast<int> (oversampledRate * 0.3))
            {
                const auto value = amp.getSagEnvelope (0);

                if (value > 1.0e-9)
                {
                    releaseTimes.push_back (i / oversampledRate);
                    releaseLogEnvelopes.push_back (std::log (value));
                }
            }
        }

        REQUIRE (releaseTimes.size() > 20);

        const auto releaseFit = TestHelpers::fitLine (releaseTimes, releaseLogEnvelopes);
        const auto releaseTau = -1.0 / releaseFit.slope;

        INFO ("fitted sag release tau = " << releaseTau * 1000.0 << " ms, R^2 = " << releaseFit.rSquared);
        CHECK (releaseFit.rSquared > 0.99);
        CHECK (releaseTau > PowerAmp::sagReleaseSeconds * 0.7);
        CHECK (releaseTau < PowerAmp::sagReleaseSeconds * 1.3);
    }
}

//==============================================================================
// T-P4: the nonlinear soak. T-P5 proves the *linearised* loop is stable;
// this proves the real one does not find some other way to misbehave.
TEST_CASE ("T-P4: worst-case settings soak without NaN, divergence or limit cycles",
           "[poweramp][stability]")
{
    for (const double rate : { 44100.0 * 2.0, 48000.0 * 4.0, 96000.0 * 8.0 })
    {
        PowerAmp amp;
        amp.prepare (rate, 2);
        amp.setResonanceDb (12.0f);
        amp.setPresenceDb (12.0f);
        amp.setSagAmount (1.0f);

        double peak = 0.0;

        // 10 s of adversarial programme: full-scale square-ish drive with an
        // abrupt level change, which is the worst thing a feedback loop can
        // be handed.
        const auto totalSamples = static_cast<int> (rate * 10.0);

        for (int i = 0; i < totalSamples; ++i)
        {
            const auto phase = juce::MathConstants<double>::twoPi * 73.0 * i / rate;
            const auto drive = (i % static_cast<int> (rate) < static_cast<int> (rate / 2)) ? 8.0 : 0.02;
            const auto x = drive * std::sin (phase);

            for (size_t channel = 0; channel < 2; ++channel)
            {
                const auto y = amp.processSample (x, channel);
                REQUIRE (std::isfinite (y));
                peak = std::max (peak, std::abs (y));
            }
        }

        INFO ("rate " << rate << " peak = " << peak);
        CHECK (peak < 10.0);

        SECTION ("no limit cycle survives into silence")
        {
            double silencePeak = 0.0;

            const auto silenceSamples = static_cast<int> (rate);

            for (int i = 0; i < silenceSamples; ++i)
            {
                for (size_t channel = 0; channel < 2; ++channel)
                {
                    const auto y = amp.processSample (0.0, channel);
                    REQUIRE (std::isfinite (y));

                    // Ignore the first 100 ms - that is the decay itself, not
                    // a limit cycle.
                    if (i > static_cast<int> (rate * 0.1))
                        silencePeak = std::max (silencePeak, std::abs (y));
                }
            }

            INFO ("residual after 100 ms of silence = " << TestHelpers::toDecibels (silencePeak) << " dB");
            CHECK (TestHelpers::toDecibels (silencePeak) < -100.0);
        }
    }
}

//==============================================================================
TEST_CASE ("The power amp saturates rather than clipping, and is NaN-safe at every extreme",
           "[poweramp]")
{
    PowerAmp amp;
    amp.prepare (oversampledRate, 1);

    SECTION ("NaN parameter values are clamped to something sane")
    {
        const auto nan = std::numeric_limits<float>::quiet_NaN();

        amp.setResonanceDb (nan);
        amp.setPresenceDb (nan);
        amp.setSagAmount (nan);

        CHECK (std::isfinite (amp.getResonanceShelfCutDb()));
        CHECK (std::isfinite (amp.getPresenceShelfCutDb()));
        CHECK (amp.getWorstCaseLoopGain() <= PowerAmp::maximumLoopGain + 1.0e-9);

        amp.reset();

        for (int i = 0; i < 10000; ++i)
        {
            const auto y = amp.processSample (0.5 * std::sin (i * 0.01), 0);
            REQUIRE (std::isfinite (y));
        }
    }

    SECTION ("the transfer curve is monotonic and compressive")
    {
        amp.setResonanceDb (0.0f);
        amp.setPresenceDb (0.0f);
        amp.setSagAmount (0.0f);

        // Static sweep: hold each input long enough for the loop to settle,
        // then read the output.
        const auto settledOutputFor = [&] (double x)
        {
            amp.reset();
            double y = 0.0;

            for (int i = 0; i < 2000; ++i)
                y = amp.processSample (x, 0);

            return y;
        };

        double previous = settledOutputFor (0.0);
        double previousSlope = 1.0e9;

        for (double x = 0.05; x <= 4.0; x += 0.05)
        {
            const auto y = settledOutputFor (x);
            const auto slope = (y - previous) / 0.05;

            CHECK (y > previous);              // monotonic
            CHECK (slope <= previousSlope + 1.0e-6); // and compressive

            previous = y;
            previousSlope = slope;
        }

        CHECK (previous < 4.0); // saturates well below the input level
    }
}
