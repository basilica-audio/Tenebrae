#include "dsp/TriodeStage.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

// Measurable assertions for the single stateful triode stage (brief
// section 6, T-C1..T-C4). Every claim the stage makes about itself - the
// equations it solves, the harmonic recipe it produces, the bias shift it
// recovers from, the frequency-dependent drive its cathode network imposes -
// is pinned here as a number.

namespace
{
    constexpr double oversampledRate = 192000.0;

    // The "Tight" stage-1 voicing from TriodeCascade.cpp's table. Duplicated
    // rather than exported so that a deliberate voicing change shows up here
    // as a failing test to be re-blessed, not as a silently moving target.
    TriodeStage::Voicing tightStageOne()
    {
        TriodeStage::Voicing voicing;
        voicing.cathodeResistorOhms = 1500.0;
        voicing.cathodeCapacitorF = 0.68e-6;
        voicing.gridStopperOhms = 47.0e3;
        voicing.gridScaleVolts = 3.0;
        voicing.couplingHighPassHz = 80.0;
        voicing.millerLowPassHz = 5000.0;
        voicing.biasDepth = 0.60;
        voicing.bloomDepth = 0.020;
        return voicing;
    }

    TriodeStage::Voicing looseStageOne()
    {
        TriodeStage::Voicing voicing;
        voicing.cathodeResistorOhms = 820.0;
        voicing.cathodeCapacitorF = 25.0e-6;
        voicing.gridStopperOhms = 68.0e3;
        voicing.gridScaleVolts = 2.5;
        voicing.couplingHighPassHz = 60.0;
        voicing.millerLowPassHz = 8000.0;
        voicing.biasDepth = 0.45;
        voicing.bloomDepth = 0.015;
        return voicing;
    }
}

//==============================================================================
// T-C1: the equations themselves, independent of any stage wiring.
TEST_CASE ("T-C1: Dempwolf 12AX7 equations reproduce their published form", "[triode][equations]")
{
    const TriodeStage::Dempwolf12AX7 tube;

    SECTION ("Table 1 RSD-1 parameters are the ones the model is built on")
    {
        CHECK (tube.G == Catch::Approx (2.242e-3));
        CHECK (tube.mu == Catch::Approx (103.2));
        CHECK (tube.gamma == Catch::Approx (1.26));
        CHECK (tube.C == Catch::Approx (3.40));
        CHECK (tube.Gg == Catch::Approx (6.177e-4));
        CHECK (tube.xi == Catch::Approx (1.314));
        CHECK (tube.Cg == Catch::Approx (9.901));
        CHECK (tube.Ig0 == Catch::Approx (8.025e-8));
    }

    SECTION ("ia() equals ik() - ig() to within 1e-6 relative over the working grid")
    {
        // Grid Vg in -4..+2 V against plate Va in {100, 200, 300} V - the
        // region a cascaded preamp stage actually traverses.
        for (const double va : { 100.0, 200.0, 300.0 })
        {
            for (double vg = -4.0; vg <= 2.0001; vg += 0.25)
            {
                const auto ik = tube.ik (vg, va);
                const auto ig = tube.ig (vg);
                const auto ia = tube.ia (vg, va);

                const auto expected = ik - ig;
                const auto scale = std::max (1.0e-12, std::abs (expected));

                CHECK (std::abs (ia - expected) / scale < 1.0e-6);
                CHECK (std::isfinite (ia));
            }
        }
    }

    SECTION ("softplus is smooth, positive and asymptotically correct")
    {
        // The softplus guard is what keeps pow() away from a non-positive
        // base and Newton away from a discontinuous derivative.
        for (double x = -50.0; x <= 50.0; x += 0.5)
        {
            const auto value = TriodeStage::Dempwolf12AX7::softplus (x, 3.4);
            CHECK (value > 0.0);
            CHECK (std::isfinite (value));

            if (x > 12.0)
                CHECK (value == Catch::Approx (x).epsilon (1.0e-6)); // -> x
        }

        CHECK (TriodeStage::Dempwolf12AX7::softplus (-40.0, 3.4) < 1.0e-50);
    }

    SECTION ("grid current is negligible below cutoff and dominant above conduction")
    {
        CHECK (tube.ig (-3.0) < 1.0e-6);   // grid diode closed
        CHECK (tube.ig (1.0) > 1.0e-4);    // grid conducting hard
        CHECK (tube.ig (1.0) > tube.ig (0.0));
        CHECK (tube.ig (0.0) > tube.ig (-1.0));
    }

    SECTION ("the solved DC operating point lands where a 12AX7 stage should")
    {
        // Vb = 300 V, Ra = 100k, Rk per voicing, grid at 0 V through the grid
        // leak, cathode cap open at DC.
        TriodeStage tight;
        tight.prepare (tightStageOne(), oversampledRate, 1);

        CHECK (tight.getQuiescentCathodeVolts() > 0.9);
        CHECK (tight.getQuiescentCathodeVolts() < 1.9);
        CHECK (tight.getQuiescentPlateVolts() > 140.0);
        CHECK (tight.getQuiescentPlateVolts() < 210.0);

        TriodeStage loose;
        loose.prepare (looseStageOne(), oversampledRate, 1);

        CHECK (loose.getQuiescentCathodeVolts() > 0.9);
        CHECK (loose.getQuiescentCathodeVolts() < 1.9);
        CHECK (loose.getQuiescentPlateVolts() > 140.0);
        CHECK (loose.getQuiescentPlateVolts() < 210.0);

        // The smaller cathode resistor draws more current, so it must bias
        // colder at the cathode and lower at the plate.
        CHECK (loose.getQuiescentCathodeVolts() < tight.getQuiescentCathodeVolts());
        CHECK (loose.getQuiescentPlateVolts() < tight.getQuiescentPlateVolts());
    }
}

//==============================================================================
// T-C1b: the ADAA consistency rule (brief revision note 4). This is the
// assertion that stops the LUT flavour from ever regressing to an
// independently tabulated F1.
TEST_CASE ("T-C1b: the stage LUT's antiderivative differentiates back to the LUT exactly",
           "[triode][adaa]")
{
    TriodeStage stage;
    stage.prepare (tightStageOne(), oversampledRate, 1);

    const auto& curve = stage.getCurve();
    REQUIRE (curve.isBuilt());

    // A central difference of F1 must reproduce S to within the difference
    // scheme's own truncation error and nothing more. If F1 were tabulated
    // independently (composite Simpson and a separate interpolation, which
    // the brief prohibits) this would fail by orders of magnitude, and the
    // 1/dx amplification in the ADAA divided difference would push that error
    // straight into the -60..-90 dB range the alias gates live in.
    constexpr double h = 1.0e-5;
    double worstError = 0.0;

    for (double x = -2.5; x <= 2.5; x += 0.0037)
    {
        const auto numerical = (curve.antiderivative (x + h) - curve.antiderivative (x - h)) / (2.0 * h);
        worstError = std::max (worstError, std::abs (numerical - curve.value (x)));
    }

    CHECK (worstError < 1.0e-6);

    SECTION ("F1 is continuous across every knot")
    {
        // A cumulative-constant bug would show up as a sawtooth in F1 at the
        // knot spacing, which the divided difference would turn into a
        // broadband spray.
        double previous = curve.antiderivative (curve.getMinX());

        for (double x = curve.getMinX(); x <= curve.getMaxX(); x += 0.001)
        {
            const auto value = curve.antiderivative (x);
            CHECK (std::abs (value - previous) < 0.05);
            previous = value;
        }
    }

    SECTION ("ADAA1 agrees with direct waveshaping on a slowly varying signal")
    {
        // With a signal that moves far less than a LUT segment per sample,
        // the divided difference must collapse onto f itself.
        tnbr::adaa::State state;
        tnbr::adaa::prime (curve, state, 0.0);

        double worst = 0.0;

        for (int i = 1; i < 4000; ++i)
        {
            const auto x = 0.8 * std::sin (juce::MathConstants<double>::twoPi * i / 3000.0);
            const auto adaaValue = tnbr::adaa::process (curve, state, x);
            worst = std::max (worst, std::abs (adaaValue - curve.value (x)));
        }

        CHECK (worst < 5.0e-3);
    }
}

//==============================================================================
// T-C2: the harmonic recipe.
TEST_CASE ("T-C2: the stage curve produces an even-dominant, asymmetric harmonic profile",
           "[triode][harmonics]")
{
    TriodeStage stage;
    stage.prepare (tightStageOne(), oversampledRate, 1);

    SECTION ("moderate drive: H2 leads H3 by at least 6 dB")
    {
        // The plate-current law is a smooth ~x^1.4 curvature, so at drive
        // levels short of grid conduction the second harmonic must dominate -
        // that is the whole difference between a triode stage and a symmetric
        // clipper.
        constexpr int fftOrder = 15;
        constexpr int fftSize = 1 << fftOrder;
        constexpr double toneHz = 220.0;

        std::vector<float> output (fftSize, 0.0f);

        for (int i = 0; i < fftSize; ++i)
        {
            const auto x = 0.25 * std::sin (juce::MathConstants<double>::twoPi * toneHz * i / oversampledRate);
            output[static_cast<size_t> (i)] = static_cast<float> (stage.evaluateCurve (x));
        }

        const auto spectrum = TestHelpers::magnitudeSpectrum (output.data(), fftSize, fftOrder);
        const auto binWidth = oversampledRate / fftSize;

        const auto magnitudeAt = [&] (double frequency)
        {
            const auto centre = static_cast<int> (std::round (frequency / binWidth));
            double peak = 0.0;

            for (int bin = centre - 3; bin <= centre + 3; ++bin)
                if (bin >= 0 && bin < static_cast<int> (spectrum.size()))
                    peak = std::max (peak, spectrum[static_cast<size_t> (bin)]);

            return peak;
        };

        const auto h2 = TestHelpers::toDecibels (magnitudeAt (2.0 * toneHz));
        const auto h3 = TestHelpers::toDecibels (magnitudeAt (3.0 * toneHz));

        INFO ("H2 = " << h2 << " dB, H3 = " << h3 << " dB");
        CHECK (h2 - h3 >= 6.0);
    }

    SECTION ("hard drive: the two halves of the transfer curve are unequal")
    {
        // Asymmetry ratio at full overdrive. The two limits are set by
        // genuinely different mechanisms - grid conduction against the grid
        // stopper on one side, the supply rail at cutoff on the other - so
        // they cannot coincide.
        //
        // NOTE (deviation, recorded in the PR): the brief's parenthetical
        // reads "grid-clamp side flatter". In this solved model the *cutoff*
        // side is the flatter one, because cutoff is a hard wall (plate
        // current reaches zero and Va can go no higher than Vb) while grid
        // conduction through R_stop is a soft, logarithmic compression that
        // is still moving at the edge of the LUT domain. The binding numeric
        // assertion - asymmetry ratio > 1.15 - is what is checked here, and
        // the flatness relationship is asserted in its physically correct
        // direction below.
        double mostNegative = 0.0;
        double mostPositive = 0.0;

        for (double x = -3.0; x <= 3.0; x += 0.001)
        {
            const auto y = stage.evaluateCurve (x);
            mostNegative = std::min (mostNegative, y);
            mostPositive = std::max (mostPositive, y);
        }

        const auto ratio = std::max (std::abs (mostNegative), mostPositive)
                            / std::max (1.0e-9, std::min (std::abs (mostNegative), mostPositive));

        INFO ("swing- = " << mostNegative << ", swing+ = " << mostPositive << ", ratio = " << ratio);
        CHECK (ratio > 1.15);

        // The stage inverts, so a positive input (grid driven positive) must
        // move the output negative.
        CHECK (stage.evaluateCurve (1.0) < 0.0);
        CHECK (stage.evaluateCurve (-1.0) > 0.0);

        // Cutoff is the harder wall: the incremental slope out at the cutoff
        // extreme is smaller than at the grid-conduction extreme.
        constexpr double h = 1.0e-3;
        const auto cutoffSlope = std::abs (stage.evaluateCurve (-2.5 + h) - stage.evaluateCurve (-2.5 - h));
        const auto gridSlope = std::abs (stage.evaluateCurve (2.5 + h) - stage.evaluateCurve (2.5 - h));

        INFO ("cutoff-side slope = " << cutoffSlope << ", grid-side slope = " << gridSlope);
        CHECK (cutoffSlope < gridSlope);
    }

    SECTION ("the curve is monotonic and bounded across the whole LUT domain")
    {
        double previous = stage.evaluateCurve (-3.0);

        for (double x = -3.0; x <= 3.0; x += 0.002)
        {
            const auto y = stage.evaluateCurve (x);
            CHECK (std::isfinite (y));
            CHECK (std::abs (y) <= 1.0001);
            // Inverting => non-increasing. The tolerance absorbs the cubic
            // Hermite interpolant's sub-ULP wiggle in the saturated plateaus,
            // where consecutive tabulated values differ by less than 1e-8.
            CHECK (y <= previous + 1.0e-6);
            previous = y;
        }
    }
}

//==============================================================================
// T-C3: bias shift / blocking distortion, measured as gain reduction over
// time rather than as a vibe.
TEST_CASE ("T-C3: an overdriven burst biases the stage toward cutoff and recovers over ~20 ms",
           "[triode][bias]")
{
    constexpr double rate = 192000.0;

    TriodeStage stage;
    stage.prepare (tightStageOne(), rate, 1);

    const auto probeGain = [&] (int probeSamples)
    {
        // A quiet probe tone, measured as peak-to-peak output. If the stage
        // has biased itself toward cutoff, the probe sits on a colder part of
        // the curve and comes out smaller.
        double maximum = -1.0e9;
        double minimum = 1.0e9;

        for (int i = 0; i < probeSamples; ++i)
        {
            const auto x = 0.03 * std::sin (juce::MathConstants<double>::twoPi * 500.0 * i / rate);
            const auto y = stage.processSample (x, 0);

            if (i > probeSamples / 2) // let the coupling HPF settle
            {
                maximum = std::max (maximum, y);
                minimum = std::min (minimum, y);
            }
        }

        return maximum - minimum;
    };

    // Reference: the probe through a stage that has never been overdriven.
    const auto referenceSwing = probeGain (static_cast<int> (rate * 0.05));
    REQUIRE (referenceSwing > 0.0);

    // 50 ms burst at 10x overdrive.
    stage.reset();

    const auto burstSamples = static_cast<int> (rate * 0.05);

    for (int i = 0; i < burstSamples; ++i)
    {
        const auto x = 10.0 * 0.3 * std::sin (juce::MathConstants<double>::twoPi * 500.0 * i / rate);
        const auto y = stage.processSample (x, 0);
        CHECK (std::isfinite (y));
    }

    // Immediately after the burst, the probe must be suppressed.
    const auto suppressedSwing = probeGain (static_cast<int> (rate * 0.004));
    const auto suppressionDb = TestHelpers::toDecibels (suppressedSwing / referenceSwing);

    INFO ("post-burst probe suppression = " << suppressionDb << " dB");
    CHECK (suppressionDb <= -3.0);

    SECTION ("recovery follows the coupling network's ~20 ms time constant")
    {
        // Re-run the burst, then sample the recovery envelope and fit an
        // exponential to it. The time constant is Cout*Rg (22n * 1M ~ 22 ms)
        // and the model targets 20 ms, so the fit must land within 30 %.
        stage.reset();

        for (int i = 0; i < burstSamples; ++i)
        {
            const auto x = 10.0 * 0.3 * std::sin (juce::MathConstants<double>::twoPi * 500.0 * i / rate);
            stage.processSample (x, 0);
        }

        // Silence, sampling the DC the biased stage settles toward. The bias
        // state itself is internal, so it is observed through its effect: the
        // stage's output offset decays with the bias.
        std::vector<double> times;
        std::vector<double> logOffsets;

        const auto recoverySamples = static_cast<int> (rate * 0.06);
        double firstOffset = 0.0;

        for (int i = 0; i < recoverySamples; ++i)
        {
            const auto y = stage.processSample (0.0, 0);

            if (i == 0)
                firstOffset = std::abs (y);

            // Sample every 0.5 ms across the first three time constants.
            if (i > 0 && i % static_cast<int> (rate * 0.0005) == 0 && i < static_cast<int> (rate * 0.045))
            {
                const auto offset = std::abs (y);

                if (offset > 1.0e-9 && firstOffset > 1.0e-9)
                {
                    times.push_back (i / rate);
                    logOffsets.push_back (std::log (offset));
                }
            }
        }

        REQUIRE (times.size() > 20);

        const auto fit = TestHelpers::fitLine (times, logOffsets);
        const auto tau = -1.0 / fit.slope;

        INFO ("fitted tau = " << tau * 1000.0 << " ms, R^2 = " << fit.rSquared);
        CHECK (fit.rSquared > 0.9);
        CHECK (tau > 0.020 * 0.7);
        CHECK (tau < 0.020 * 1.3);
    }
}

//==============================================================================
// T-C4: the cathode-bypass shelf, against its own analytic response.
TEST_CASE ("T-C4: the cathode bypass imposes the voicing's frequency-dependent drive",
           "[triode][cathode]")
{
    constexpr double rate = 192000.0;

    // Measures the stage's small-signal magnitude at `frequencyHz` by driving
    // it well below the clipping region and reading back the peak swing, then
    // dividing out the Miller low-pass so only the cathode shelf remains.
    const auto measureShelfDeltaDb = [] (TriodeStage& stage, double lowHz, double highHz)
    {
        const auto amplitudeAt = [&] (double frequencyHz)
        {
            stage.reset();

            const auto settle = static_cast<int> (rate * 0.4);
            // A whole number of cycles, so the quadrature sums below are not
            // biased by a partial final cycle.
            const auto cycles = std::max (20, static_cast<int> (frequencyHz * 0.1));
            const auto measure = static_cast<int> (std::round (cycles * rate / frequencyHz));

            double real = 0.0;
            double imaginary = 0.0;

            for (int i = 0; i < settle + measure; ++i)
            {
                // Deliberately tiny: the stage curve's incremental slope falls
                // with |x|, so measuring the shelf at an amplitude the shaper
                // can still bend would read the shelf's own level difference
                // back as ~1 dB of gain difference and corrupt the very thing
                // under test. 6 mV at the grid is unambiguously linear.
                const auto phase = juce::MathConstants<double>::twoPi * frequencyHz * i / rate;
                const auto y = stage.processSample (0.002 * std::sin (phase), 0);

                // Quadrature demodulation at the drive frequency. Unlike a
                // peak reading this rejects the stage's DC bloom offset and
                // every harmonic outright, which matters here because the
                // signal is only a few mV and the offset is not.
                if (i >= settle)
                {
                    real += y * std::sin (phase);
                    imaginary += y * std::cos (phase);
                }
            }

            return 2.0 * std::hypot (real, imaginary) / measure;
        };

        const auto measuredLow = amplitudeAt (lowHz);
        const auto measuredHigh = amplitudeAt (highHz);

        // The Miller low-pass and the coupling high-pass are in series with
        // the shelf and are not what is under test here, so their magnitudes
        // are divided back out. They are computed as the *discrete* responses
        // of the exact one-poles TriodeStage implements
        // (y += a*(x - y), a = 1 - exp(-2*pi*f/fs)), not as their analog
        // prototypes: at 100 Hz against an 80 Hz corner the two differ by
        // enough to swamp the 1 dB tolerance this section asserts.
        const auto onePoleMagnitude = [] (double cornerHz, double frequencyHz)
        {
            const auto a = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * cornerHz / rate);
            const auto omega = juce::MathConstants<double>::twoPi * frequencyHz / rate;
            const auto denominator = std::hypot (1.0 - (1.0 - a) * std::cos (omega),
                                                 (1.0 - a) * std::sin (omega));
            return a / denominator;
        };

        const auto millerHz = stage.getVoicing().millerLowPassHz;
        const auto millerAt = [&] (double f) { return onePoleMagnitude (millerHz, f); };

        // The coupling high-pass is x - lowpass(x), so its magnitude is that
        // of the complementary one-pole.
        const auto couplingHz = stage.getVoicing().couplingHighPassHz;
        const auto couplingAt = [&] (double f)
        {
            const auto a = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * couplingHz / rate);
            const auto omega = juce::MathConstants<double>::twoPi * f / rate;
            const auto numerator = (1.0 - a) * std::hypot (1.0 - std::cos (omega), std::sin (omega));
            const auto denominator = std::hypot (1.0 - (1.0 - a) * std::cos (omega),
                                                 (1.0 - a) * std::sin (omega));
            return numerator / denominator;
        };

        const auto correctedLow = measuredLow / (millerAt (lowHz) * couplingAt (lowHz));
        const auto correctedHigh = measuredHigh / (millerAt (highHz) * couplingAt (highHz));

        return TestHelpers::toDecibels (correctedHigh / correctedLow);
    };

    SECTION ("Tight (1k5 / 0.68 uF): a real bass-versus-treble drive difference")
    {
        TriodeStage stage;
        stage.prepare (tightStageOne(), rate, 1);

        const auto analyticDelta = TestHelpers::toDecibels (stage.cathodeShelfMagnitude (2000.0)
                                                            / stage.cathodeShelfMagnitude (100.0));
        const auto measuredDelta = measureShelfDeltaDb (stage, 100.0, 2000.0);

        INFO ("analytic = " << analyticDelta << " dB, measured = " << measuredDelta << " dB");
        CHECK (std::abs (measuredDelta - analyticDelta) < 1.0);

        // The whole point of the 0.68 uF pairing: this is a substantial,
        // audible shelf, not a rounding error.
        CHECK (analyticDelta > 3.0);
    }

    SECTION ("Loose (820R / 25 uF): full-band bypass, flat from 50 Hz to 10 kHz")
    {
        TriodeStage stage;
        stage.prepare (looseStageOne(), rate, 1);

        double worstDelta = 0.0;

        for (const double f : { 50.0, 100.0, 300.0, 1000.0, 3000.0, 10000.0 })
        {
            const auto delta = TestHelpers::toDecibels (stage.cathodeShelfMagnitude (f)
                                                         / stage.cathodeShelfMagnitude (10000.0));
            worstDelta = std::max (worstDelta, std::abs (delta));
        }

        INFO ("worst Loose cathode-shelf deviation = " << worstDelta << " dB");
        CHECK (worstDelta < 0.5);
    }
}
