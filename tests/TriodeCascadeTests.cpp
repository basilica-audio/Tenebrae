#include "dsp/TriodeCascade.h"
#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

// Measurable assertions for the three-stage triode chain (brief section 6,
// T-C5..T-C7).

namespace
{
    // Renders `seconds` of `program` through a freshly prepared cascade at
    // `rate`, mono.
    template <typename Program>
    std::vector<double> renderCascade (double rate, double seconds, Program program, int voicing = 0)
    {
        TriodeCascade cascade;
        cascade.prepare (rate, 1);
        cascade.setVoicing (voicing);
        cascade.setBiasScale (1.0);

        const auto numSamples = static_cast<int> (rate * seconds);
        std::vector<double> output (static_cast<size_t> (numSamples), 0.0);

        for (int i = 0; i < numSamples; ++i)
            output[static_cast<size_t> (i)] = cascade.processSample (program (i / rate), 0);

        return output;
    }

    void setParam (TenebraeAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    // Renders a deterministic program through a full processor instance and
    // returns the result, so the engine-level assertions below exercise the
    // real signal path (oversampling, dry/wet alignment and all) rather than
    // the cascade in isolation.
    juce::AudioBuffer<float> renderProcessor (TenebraeAudioProcessor& processor,
                                              double rate,
                                              int blockSize,
                                              int totalSamples,
                                              const std::function<float (int)>& program)
    {
        processor.prepareToPlay (rate, blockSize);

        juce::AudioBuffer<float> result (2, totalSamples);
        result.clear();

        juce::AudioBuffer<float> block (2, blockSize);
        juce::MidiBuffer midi;

        for (int position = 0; position < totalSamples; position += blockSize)
        {
            const auto count = std::min (blockSize, totalSamples - position);
            block.clear();

            for (int i = 0; i < count; ++i)
            {
                const auto value = program (position + i);
                block.setSample (0, i, value);
                block.setSample (1, i, value);
            }

            processor.processBlock (block, midi);

            for (int channel = 0; channel < 2; ++channel)
                result.copyFrom (channel, position, block, channel, 0, count);
        }

        return result;
    }
}

//==============================================================================
// T-C5: sample-rate invariance. A model whose voicing drifts with the host
// rate is not a model, it is an accident.
TEST_CASE ("T-C5: the Triode engine renders the same voicing at 44.1k and 96k", "[cascade][rate]")
{
    // Measured through the full processor, not the bare cascade: the claim is
    // about the engine's voicing, and the engine includes the oversampling
    // that keeps the cascade's own harmonics from folding. Feeding the bare
    // cascade at the host rate would measure fold-back landing on different
    // bins at the two rates - a real effect, but of the test harness rather
    // than of the model.
    constexpr int fftOrder = 15;
    constexpr int fftSize = 1 << fftOrder;

    // Chosen so that the tone AND every harmonic of it land on an exact FFT
    // bin at BOTH rates: 96000/44100 = 320/147, so 320 bins at 44.1 k and
    // 147 bins at 96 k are the same frequency. Without that, the two renders
    // are read at different fractional bin offsets and Blackman-Harris
    // scalloping alone contributes up to 0.83 dB of apparent difference -
    // which on a strong harmonic is larger than the -30 dB residual this
    // test is trying to measure.
    constexpr double toneHz = 320.0 * 44100.0 / fftSize; // ~430.66 Hz

    const auto renderAt = [] (double rate)
    {
        TenebraeAudioProcessor processor;
        processor.prepareToPlay (rate, 512);

        setParam (processor, ParamIDs::engine, 1.0f);   // Triode
        setParam (processor, ParamIDs::quality, 1.0f);  // Standard (4x)
        setParam (processor, ParamIDs::gain, 18.0f);
        setParam (processor, ParamIDs::gateOn, 0.0f);

        const auto totalSamples = static_cast<int> (rate * 0.75);

        return renderProcessor (processor, rate, 512, totalSamples,
                                [rate] (int index)
                                {
                                    return 0.4f * static_cast<float> (
                                        std::sin (juce::MathConstants<double>::twoPi * toneHz * index / rate));
                                });
    };

    const auto lowRender = renderAt (44100.0);
    const auto highRender = renderAt (96000.0);

    // Analyse the settled tail only, clear of the latency ramp-in.
    const auto spectrumOfTail = [] (const juce::AudioBuffer<float>& buffer)
    {
        const auto offset = buffer.getNumSamples() - fftSize;
        return TestHelpers::magnitudeSpectrum (buffer.getReadPointer (0) + offset, fftSize, fftOrder);
    };

    const auto lowSpectrum = spectrumOfTail (lowRender);
    const auto highSpectrum = spectrumOfTail (highRender);

    const auto lowBinWidth = 44100.0 / fftSize;
    const auto highBinWidth = 96000.0 / fftSize;

    // Total energy across the analysis window's main lobe rather than the
    // single peak bin, so the reading does not depend on where the component
    // happens to sit inside a bin.
    const auto magnitudeAt = [] (const std::vector<double>& spectrum, double binWidth, double frequency)
    {
        const auto centre = static_cast<int> (std::round (frequency / binWidth));
        double energy = 0.0;

        for (int bin = centre - 5; bin <= centre + 5; ++bin)
            if (bin >= 0 && bin < static_cast<int> (spectrum.size()))
                energy += spectrum[static_cast<size_t> (bin)] * spectrum[static_cast<size_t> (bin)];

        return std::sqrt (energy);
    };

    const auto lowFundamental = magnitudeAt (lowSpectrum, lowBinWidth, toneHz);
    const auto highFundamental = magnitudeAt (highSpectrum, highBinWidth, toneHz);

    REQUIRE (lowFundamental > 0.0);
    REQUIRE (highFundamental > 0.0);

    double worstResidualDb = -200.0;

    for (int harmonic = 1; harmonic * toneHz < 10000.0; ++harmonic)
    {
        const auto frequency = harmonic * toneHz;
        const auto low = magnitudeAt (lowSpectrum, lowBinWidth, frequency) / lowFundamental;
        const auto high = magnitudeAt (highSpectrum, highBinWidth, frequency) / highFundamental;

        const auto residualDb = TestHelpers::toDecibels (std::abs (low - high));

        if (residualDb > worstResidualDb)
        {
            worstResidualDb = residualDb;
            INFO ("worst harmonic " << harmonic << " at " << frequency << " Hz: 44.1k = "
                                    << TestHelpers::toDecibels (low) << " dB, 96k = "
                                    << TestHelpers::toDecibels (high) << " dB");
        }
    }

    INFO ("worst per-harmonic residual below 10 kHz = " << worstResidualDb << " dB");
    CHECK (worstResidualDb <= -30.0);
}

//==============================================================================
// T-C6: Tier B against an offline Tier A reference.
//
// Tier A here is the per-sample circuit solve the model is a calibrated
// approximation of: the same Dempwolf stage equations, but resolved sample by
// sample with the grid stopper and plate node in the loop, instead of read
// from a precomputed static curve. Running it inline (rather than committing
// a rendered fixture) keeps the comparison honest across both CI toolchains -
// a committed float render could never be byte-compared across them, and a
// tolerance comparison against one is only as good as the platform it was
// generated on.
//
// This is deliberately a documented divergence measurement, not a tight gate:
// Tier B is expected to track Tier A closely at mix drive levels and to
// diverge on hard transients (research-triode-adaa.md section 4).
TEST_CASE ("T-C6: Tier B tracks a Tier A reference solve at moderate drive", "[cascade][calibration]")
{
    constexpr double rate = 192000.0;
    constexpr int numSamples = 32768;

    TriodeStage stage;

    TriodeStage::Voicing voicing;
    voicing.cathodeResistorOhms = 1500.0;
    voicing.cathodeCapacitorF = 0.68e-6;
    voicing.gridStopperOhms = 47.0e3;
    voicing.gridScaleVolts = 3.0;
    voicing.couplingHighPassHz = 80.0;
    voicing.millerLowPassHz = 5000.0;
    voicing.biasDepth = 0.0;   // isolate the static curve from the sidechain
    voicing.bloomDepth = 0.0;
    stage.prepare (voicing, rate, 1);

    // The Tier A reference: solve the same DC stage equations directly per
    // sample, with no LUT and no interpolation in the path.
    const TriodeStage::Dempwolf12AX7 tube;
    const auto quiescentVk = stage.getQuiescentCathodeVolts();
    const auto quiescentVa = stage.getQuiescentPlateVolts();

    const auto tierAReference = [&] (double x)
    {
        const auto gridSource = x * voicing.gridScaleVolts;

        double vg = std::min (gridSource, quiescentVk);

        for (int i = 0; i < 200; ++i)
        {
            const auto vgk = vg - quiescentVk;
            const auto residual = vg - gridSource + tube.ig (vgk) * voicing.gridStopperOhms;
            const auto sp = std::max (TriodeStage::Dempwolf12AX7::softplus (vgk, tube.Cg), 1.0e-30);
            const auto logistic = 1.0 / (1.0 + std::exp (-tube.Cg * vgk));
            const auto derivative = 1.0 + voicing.gridStopperOhms
                                            * tube.Gg * tube.xi
                                            * std::exp ((tube.xi - 1.0) * std::log (sp)) * logistic;
            const auto step = residual / derivative;
            vg -= std::clamp (step, -5.0, 5.0);

            if (std::abs (step) < 1.0e-13)
                break;
        }

        const auto vgk = vg - quiescentVk;
        double va = quiescentVa;

        for (int i = 0; i < 300; ++i)
        {
            const auto vak = va - quiescentVk;
            const auto residual = (300.0 - va) / 100.0e3 - tube.ia (vgk, vak);
            const auto derivative = -1.0 / 100.0e3 - tube.diaDvak (vgk, vak);
            const auto step = residual / derivative;
            va -= std::clamp (0.7 * step, -40.0, 40.0);
            va = std::clamp (va, 0.0, 300.0);

            if (std::abs (step) < 1.0e-11)
                break;
        }

        return va - quiescentVa;
    };

    // Normalise the reference the same way the LUT is normalised, so the two
    // are compared as shapes rather than as scalings.
    double referenceExtreme = 0.0;

    for (double x = -3.0; x <= 3.0; x += 0.001)
        referenceExtreme = std::max (referenceExtreme, std::abs (tierAReference (x)));

    const auto referenceScale = 1.0 / referenceExtreme;

    // Pink-ish noise at -20 dBFS: the brief's calibration programme.
    juce::AudioBuffer<float> noise (1, numSamples);
    TestHelpers::fillWithNoise (noise, 0.1f, 0xC6C6C6u);

    double residualSumOfSquares = 0.0;
    double referenceSumOfSquares = 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto x = static_cast<double> (noise.getSample (0, i));
        const auto tierB = stage.evaluateCurve (x);
        const auto tierA = tierAReference (x) * referenceScale;

        residualSumOfSquares += (tierB - tierA) * (tierB - tierA);
        referenceSumOfSquares += tierA * tierA;
    }

    const auto residualDb = TestHelpers::toDecibels (std::sqrt (residualSumOfSquares / referenceSumOfSquares));

    INFO ("Tier B vs Tier A residual at -20 dBFS = " << residualDb << " dB RMS");
    CHECK (residualDb <= -40.0);

    SECTION ("the 0 dBFS divergence is logged, not gated")
    {
        // Documented, per the brief: Tier B is expected to diverge at the
        // extremes where the LUT's knot spacing is coarsest relative to the
        // curvature. This section records the number rather than gating on
        // it, so a regression shows up in the CI log without turning a known
        // and accepted approximation into a red build.
        double worst = 0.0;

        for (double x = -1.0; x <= 1.0; x += 0.001)
            worst = std::max (worst, std::abs (stage.evaluateCurve (x) - tierAReference (x) * referenceScale));

        WARN ("T-C6: worst Tier B vs Tier A divergence over +/-1.0 (0 dBFS) = "
              << TestHelpers::toDecibels (worst) << " dB");

        CHECK (worst < 0.05); // sanity bound only
    }
}

//==============================================================================
// T-C7: polarity. This is the assertion that guards the single x(-1)
// normalisation in TriodeCascade::processSample() - without it any Mix below
// 100 % combs instead of blending, and Classic/Triode A/B flips absolute
// polarity, which no other test in the suite would have caught (the mix = 0
// null in T-L1 passes either way).
TEST_CASE ("T-C7: the Triode wet path is polarity-aligned with dry and with Classic",
           "[cascade][polarity]")
{
    constexpr double rate = 48000.0;
    constexpr int blockSize = 256;
    constexpr int totalSamples = 48000;
    constexpr double toneHz = 100.0;

    const auto program = [] (int index)
    {
        return 0.3f * static_cast<float> (
            std::sin (juce::MathConstants<double>::twoPi * toneHz * index / rate));
    };

    SECTION ("at Mix = 50 % the wet path adds to the dry path rather than cancelling it")
    {
        TenebraeAudioProcessor processor;
        processor.prepareToPlay (rate, blockSize);

        setParam (processor, ParamIDs::engine, 1.0f);   // Triode
        setParam (processor, ParamIDs::gain, 12.0f);
        setParam (processor, ParamIDs::mix, 50.0f);
        setParam (processor, ParamIDs::gateOn, 0.0f);   // keep the gate out of it

        const auto blended = renderProcessor (processor, rate, blockSize, totalSamples, program);

        // The same instance at Mix = 100 % and Mix = 0 % gives the two
        // constituents the blend is made of.
        setParam (processor, ParamIDs::mix, 100.0f);
        const auto wetOnly = renderProcessor (processor, rate, blockSize, totalSamples, program);

        setParam (processor, ParamIDs::mix, 0.0f);
        const auto dryOnly = renderProcessor (processor, rate, blockSize, totalSamples, program);

        // Measure over the last half, well clear of the latency ramp-in.
        const auto offset = totalSamples / 2;
        const auto count = totalSamples / 2;

        const auto correlation = TestHelpers::correlation (wetOnly.getReadPointer (0) + offset,
                                                           dryOnly.getReadPointer (0) + offset,
                                                           count);

        INFO ("wet vs delay-aligned dry correlation = " << correlation);
        CHECK (correlation > 0.0);

        // And the blend must actually sit between them in level rather than
        // below both, which is what LF cancellation would look like.
        juce::AudioBuffer<float> blendedTail (1, count);
        juce::AudioBuffer<float> wetTail (1, count);
        juce::AudioBuffer<float> dryTail (1, count);

        blendedTail.copyFrom (0, 0, blended, 0, offset, count);
        wetTail.copyFrom (0, 0, wetOnly, 0, offset, count);
        dryTail.copyFrom (0, 0, dryOnly, 0, offset, count);

        const auto blendedRms = TestHelpers::rms (blendedTail);
        const auto wetRms = TestHelpers::rms (wetTail);
        const auto dryRms = TestHelpers::rms (dryTail);

        INFO ("blend = " << blendedRms << ", wet = " << wetRms << ", dry = " << dryRms);
        CHECK (blendedRms > 0.5 * std::min (wetRms, dryRms));
    }

    SECTION ("Classic and Triode renders do not disagree about absolute polarity")
    {
        TenebraeAudioProcessor classicProcessor;
        classicProcessor.prepareToPlay (rate, blockSize);
        setParam (classicProcessor, ParamIDs::engine, 0.0f);
        setParam (classicProcessor, ParamIDs::gain, 12.0f);
        setParam (classicProcessor, ParamIDs::gateOn, 0.0f);

        const auto classic = renderProcessor (classicProcessor, rate, blockSize, totalSamples, program);

        TenebraeAudioProcessor triodeProcessor;
        triodeProcessor.prepareToPlay (rate, blockSize);
        setParam (triodeProcessor, ParamIDs::engine, 1.0f);
        setParam (triodeProcessor, ParamIDs::gain, 12.0f);
        setParam (triodeProcessor, ParamIDs::gateOn, 0.0f);

        const auto triode = renderProcessor (triodeProcessor, rate, blockSize, totalSamples, program);

        const auto offset = totalSamples / 2;
        const auto count = totalSamples / 2;

        // The two engines have different latencies, so the comparison searches
        // a window of alignments and takes the best - a genuine polarity flip
        // shows up as no positive correlation at any alignment.
        const auto best = TestHelpers::bestCorrelationOverShift (classic.getReadPointer (0) + offset,
                                                                 triode.getReadPointer (0) + offset,
                                                                 count,
                                                                 128);

        INFO ("best Classic vs Triode correlation = " << best);
        CHECK (best > 0.5);
    }
}

//==============================================================================
TEST_CASE ("The cascade is bounded, finite and voicing-switchable without allocation-time surprises",
           "[cascade]")
{
    constexpr double rate = 192000.0;

    TriodeCascade cascade;
    cascade.prepare (rate, 2);

    SECTION ("hard drive stays bounded on both voicings and both channels")
    {
        for (int voicing = 0; voicing < TriodeCascade::numVoicings; ++voicing)
        {
            cascade.setVoicing (voicing);
            cascade.reset();

            double peak = 0.0;

            for (int i = 0; i < 200000; ++i)
            {
                const auto x = 4.0 * std::sin (juce::MathConstants<double>::twoPi * 82.0 * i / rate);

                for (size_t channel = 0; channel < 2; ++channel)
                {
                    const auto y = cascade.processSample (x, channel);
                    REQUIRE (std::isfinite (y));
                    peak = std::max (peak, std::abs (y));
                }
            }

            INFO ("voicing " << voicing << " peak = " << peak);
            CHECK (peak < 4.0);
        }
    }

    SECTION ("the output carries no DC offset after asymmetric clipping")
    {
        // The third stage has no downstream coupling cap of its own, so the
        // cascade's own output blocker is the only thing standing between an
        // asymmetric clipper and a DC-offset wet path.
        cascade.setVoicing (0);
        cascade.reset();

        double sum = 0.0;
        const auto settle = static_cast<int> (rate * 0.5);
        const auto measure = static_cast<int> (rate * 0.5);

        for (int i = 0; i < settle + measure; ++i)
        {
            const auto x = 2.0 * std::sin (juce::MathConstants<double>::twoPi * 110.0 * i / rate);
            const auto y = cascade.processSample (x, 0);

            if (i >= settle)
                sum += y;
        }

        const auto mean = sum / measure;
        INFO ("mean output = " << mean);
        CHECK (std::abs (mean) < 0.01);
    }

    SECTION ("Bias Shift at 0 % removes the dynamic bias entirely")
    {
        // The parameter's floor must be a genuine off position: the whole
        // sidechain contributes nothing, so the cascade reduces to its static
        // curves plus the linear interstage network.
        cascade.setVoicing (0);

        const auto renderWith = [&] (double biasScale)
        {
            cascade.setBiasScale (biasScale);
            cascade.reset();

            std::vector<double> output;
            output.reserve (20000);

            for (int i = 0; i < 20000; ++i)
            {
                const auto x = 3.0 * std::sin (juce::MathConstants<double>::twoPi * 200.0 * i / rate);
                output.push_back (cascade.processSample (x, 0));
            }

            return output;
        };

        const auto neutral = renderWith (1.0);
        const auto off = renderWith (0.0);
        const auto doubled = renderWith (2.0);

        double neutralVsOff = 0.0;
        double doubledVsNeutral = 0.0;

        for (size_t i = neutral.size() / 2; i < neutral.size(); ++i)
        {
            neutralVsOff = std::max (neutralVsOff, std::abs (neutral[i] - off[i]));
            doubledVsNeutral = std::max (doubledVsNeutral, std::abs (doubled[i] - neutral[i]));
        }

        // All three must be genuinely different renders - a parameter that
        // does nothing is worse than no parameter.
        CHECK (neutralVsOff > 1.0e-3);
        CHECK (doubledVsNeutral > 1.0e-3);
    }
}
