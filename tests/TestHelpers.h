#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <numeric>
#include <random>
#include <vector>

// Small shared helpers used across the Tests target. Kept dependency-free
// (just juce_audio_basics) so it can be included from any test file.
namespace TestHelpers
{
    // Fills every channel of the buffer with a sine wave of the given
    // frequency. `startSampleIndex` offsets the phase calculation, so
    // calling this for consecutive blocks with startSampleIndex incremented
    // by each block's length produces a phase-continuous sine across block
    // boundaries (needed whenever a test processes a "warm-up" block before
    // the measured one). Defaults to 0 (phase continuity across separate,
    // unrelated calls is not needed for the RMS-based checks that most
    // callers use this for).
    inline void fillWithSine (juce::AudioBuffer<float>& buffer,
                              double sampleRate,
                              double frequencyHz,
                              float amplitude = 0.5f,
                              juce::int64 startSampleIndex = 0)
    {
        const auto numChannels = buffer.getNumChannels();
        const auto numSamples = buffer.getNumSamples();

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* data = buffer.getWritePointer (channel);

            for (int sample = 0; sample < numSamples; ++sample)
            {
                const auto phase = juce::MathConstants<double>::twoPi * frequencyHz
                                    * static_cast<double> (startSampleIndex + sample) / sampleRate;
                data[sample] = amplitude * static_cast<float> (std::sin (phase));
            }
        }
    }

    // Root-mean-square level across all channels/samples in the buffer.
    inline double rms (const juce::AudioBuffer<float>& buffer)
    {
        double sumOfSquares = 0.0;
        juce::int64 numValues = 0;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const auto value = static_cast<double> (data[sample]);
                sumOfSquares += value * value;
                ++numValues;
            }
        }

        return numValues > 0 ? std::sqrt (sumOfSquares / static_cast<double> (numValues)) : 0.0;
    }

    // Largest absolute sample value across all channels/samples.
    inline float peakAbsolute (const juce::AudioBuffer<float>& buffer)
    {
        float peak = 0.0f;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                peak = std::max (peak, std::abs (data[sample]));
        }

        return peak;
    }

    // Returns true if every sample in the buffer is finite (no NaN/Inf).
    inline bool allSamplesFinite (const juce::AudioBuffer<float>& buffer)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                if (! std::isfinite (data[sample]))
                    return false;
        }

        return true;
    }

    // Pearson correlation coefficient between two equally-sized single
    // channel signals - a shape-similarity measure that is insensitive to
    // constant gain and DC offset, making it a good proxy for "near-linear
    // processing" even when the stage under test also introduces some fixed
    // gain loss and/or DC bias. 1.0 means the two signals are perfectly
    // linearly related.
    inline double correlation (const float* a, const float* b, int numSamples)
    {
        if (numSamples <= 0)
            return 0.0;

        double meanA = 0.0;
        double meanB = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            meanA += a[i];
            meanB += b[i];
        }

        meanA /= numSamples;
        meanB /= numSamples;

        double covariance = 0.0;
        double varianceA = 0.0;
        double varianceB = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto da = static_cast<double> (a[i]) - meanA;
            const auto db = static_cast<double> (b[i]) - meanB;

            covariance += da * db;
            varianceA += da * da;
            varianceB += db * db;
        }

        const auto denominator = std::sqrt (varianceA * varianceB);
        return denominator > 0.0 ? covariance / denominator : 0.0;
    }

    //==========================================================================
    // v0.3.0 measurement helpers (brief section 6). These exist so that every
    // new DSP claim is asserted as a *number* - an alias-to-signal ratio, a
    // gain-reduction slope, a loop-gain margin - rather than as "sounds
    // plausible".

    inline double toDecibels (double linear, double floorDb = -200.0)
    {
        return linear > 0.0 ? std::max (floorDb, 20.0 * std::log10 (linear)) : floorDb;
    }

    // Deterministic white noise, seeded explicitly so every assertion built on
    // it is reproducible across runs and platforms.
    inline void fillWithNoise (juce::AudioBuffer<float>& buffer, float amplitude, unsigned int seed)
    {
        std::mt19937 engine (seed);
        std::uniform_real_distribution<float> distribution (-amplitude, amplitude);

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* data = buffer.getWritePointer (channel);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                data[sample] = distribution (engine);
        }
    }

    // Blackman-Harris windowed magnitude spectrum of `signal`, zero-padded up
    // to the next power of two of `fftSize`. Returns fftSize/2+1 magnitudes,
    // normalised by the window's coherent gain so a full-scale sine reads 1.0
    // in its own bin.
    inline std::vector<double> magnitudeSpectrum (const float* signal, int numSamples, int fftOrder)
    {
        const auto fftSize = 1 << fftOrder;
        juce::dsp::FFT fft (fftOrder);

        std::vector<float> data (static_cast<size_t> (fftSize) * 2, 0.0f);

        const auto usable = std::min (numSamples, fftSize);
        double windowSum = 0.0;

        // Blackman-Harris (4-term): -92 dB sidelobes, which is what makes an
        // alias floor below -90 dBFS measurable at all.
        for (int i = 0; i < usable; ++i)
        {
            const auto t = juce::MathConstants<double>::twoPi * i / (usable - 1);
            const auto w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2.0 * t)
                            - 0.01168 * std::cos (3.0 * t);
            data[static_cast<size_t> (i)] = static_cast<float> (signal[i] * w);
            windowSum += w;
        }

        fft.performFrequencyOnlyForwardTransform (data.data(), true);

        const auto scale = windowSum > 0.0 ? 2.0 / windowSum : 1.0;

        std::vector<double> magnitudes (static_cast<size_t> (fftSize / 2 + 1), 0.0);

        for (size_t bin = 0; bin < magnitudes.size(); ++bin)
            magnitudes[bin] = static_cast<double> (data[bin]) * scale;

        return magnitudes;
    }

    // Alias-to-signal ratio in dB for a pure input tone at `fundamentalHz`:
    // total energy in every bin that is NOT within `harmonicToleranceBins` of
    // an integer multiple of the fundamental, relative to the fundamental's
    // own magnitude. This is the standard stepped-sine ASR measurement
    // (research-oversampling-architecture.md section 5, test T1).
    inline double aliasToSignalRatioDb (const std::vector<double>& magnitudes,
                                        double sampleRate,
                                        double fundamentalHz,
                                        int fftSize,
                                        double lowestHz = 20.0,
                                        double highestHz = 20000.0,
                                        int harmonicToleranceBins = 4)
    {
        const auto binWidth = sampleRate / fftSize;
        const auto nyquist = 0.5 * sampleRate;

        double aliasEnergy = 0.0;
        double fundamentalMagnitude = 0.0;

        for (size_t bin = 0; bin < magnitudes.size(); ++bin)
        {
            const auto frequency = bin * binWidth;

            if (frequency < lowestHz || frequency > std::min (highestHz, nyquist))
                continue;

            // Distance, in bins, to the closest harmonic of the fundamental
            // (including the fundamental itself).
            const auto harmonicIndex = std::max (1.0, std::round (frequency / fundamentalHz));
            const auto harmonicHz = harmonicIndex * fundamentalHz;
            const auto distanceBins = std::abs (frequency - harmonicHz) / binWidth;

            if (harmonicIndex == 1.0 && distanceBins <= harmonicToleranceBins)
                fundamentalMagnitude = std::max (fundamentalMagnitude, magnitudes[bin]);

            if (distanceBins <= harmonicToleranceBins)
                continue; // harmonic distortion, not aliasing

            aliasEnergy += magnitudes[bin] * magnitudes[bin];
        }

        if (fundamentalMagnitude <= 0.0)
            return 0.0;

        return toDecibels (std::sqrt (aliasEnergy) / fundamentalMagnitude);
    }

    // Least-squares fit of y = slope*x + intercept, plus the coefficient of
    // determination. Used to assert that a dB-linear gate release really is
    // dB-linear, and at the commanded slope.
    struct LinearFit
    {
        double slope = 0.0;
        double intercept = 0.0;
        double rSquared = 0.0;
    };

    inline LinearFit fitLine (const std::vector<double>& x, const std::vector<double>& y)
    {
        LinearFit fit;
        const auto n = static_cast<double> (std::min (x.size(), y.size()));

        if (n < 2.0)
            return fit;

        const auto count = static_cast<size_t> (n);
        double sumX = 0.0, sumY = 0.0;

        for (size_t i = 0; i < count; ++i)
        {
            sumX += x[i];
            sumY += y[i];
        }

        const auto meanX = sumX / n;
        const auto meanY = sumY / n;

        double covariance = 0.0, varianceX = 0.0;

        for (size_t i = 0; i < count; ++i)
        {
            covariance += (x[i] - meanX) * (y[i] - meanY);
            varianceX += (x[i] - meanX) * (x[i] - meanX);
        }

        if (varianceX <= 0.0)
            return fit;

        fit.slope = covariance / varianceX;
        fit.intercept = meanY - fit.slope * meanX;

        double residual = 0.0, total = 0.0;

        for (size_t i = 0; i < count; ++i)
        {
            const auto predicted = fit.slope * x[i] + fit.intercept;
            residual += (y[i] - predicted) * (y[i] - predicted);
            total += (y[i] - meanY) * (y[i] - meanY);
        }

        fit.rSquared = total > 0.0 ? 1.0 - residual / total : 1.0;
        return fit;
    }

    // True when every sample of the two buffers is bit-for-bit equal.
    //
    // Only ever used to compare two renders produced inside the SAME test
    // process (brief section 6's platform note): libm, FP contraction and
    // vectorisation differ between the AppleClang/arm64 and MSVC/x64 CI legs,
    // so a float render produced on one is never byte-equal on the other, and
    // comparing against a committed golden render would fail permanently on
    // one leg.
    inline bool buffersAreByteIdentical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
            return false;

        for (int channel = 0; channel < a.getNumChannels(); ++channel)
        {
            const auto* dataA = a.getReadPointer (channel);
            const auto* dataB = b.getReadPointer (channel);

            if (std::memcmp (dataA, dataB, sizeof (float) * static_cast<size_t> (a.getNumSamples())) != 0)
                return false;
        }

        return true;
    }

    // Largest absolute difference between two equally-sized buffers, in dB
    // relative to full scale. The tolerance-based counterpart of
    // buffersAreByteIdentical(), and the only comparison ever made against a
    // committed fixture.
    inline double maximumDifferenceDb (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        double worst = 0.0;

        for (int channel = 0; channel < std::min (a.getNumChannels(), b.getNumChannels()); ++channel)
        {
            const auto* dataA = a.getReadPointer (channel);
            const auto* dataB = b.getReadPointer (channel);

            for (int sample = 0; sample < std::min (a.getNumSamples(), b.getNumSamples()); ++sample)
                worst = std::max (worst, std::abs (static_cast<double> (dataA[sample] - dataB[sample])));
        }

        return toDecibels (worst);
    }
}

//==============================================================================
// Global allocation counter for the real-time-safety assertions (T-X1).
//
// The audio thread must not allocate, and "must not" is only worth anything
// if it is measured. These operators are the process-wide new/delete for the
// Tests binary; they are a plain passthrough with an atomic counter that is
// only armed inside an AllocationGuard scope, so they cost nothing anywhere
// else.
namespace TestHelpers
{
    namespace detail
    {
        inline std::atomic<int>& allocationCount()
        {
            static std::atomic<int> count { 0 };
            return count;
        }

        inline std::atomic<bool>& allocationCountingArmed()
        {
            static std::atomic<bool> armed { false };
            return armed;
        }
    }

    // RAII: arms allocation counting for its scope. Not reentrant and not
    // thread-safe by design - the tests using it are single-threaded.
    class AllocationGuard
    {
    public:
        AllocationGuard()
        {
            detail::allocationCount().store (0, std::memory_order_relaxed);
            detail::allocationCountingArmed().store (true, std::memory_order_relaxed);
        }

        ~AllocationGuard()
        {
            detail::allocationCountingArmed().store (false, std::memory_order_relaxed);
        }

        int getAllocationCount() const
        {
            return detail::allocationCount().load (std::memory_order_relaxed);
        }

        JUCE_DECLARE_NON_COPYABLE (AllocationGuard)
    };

    // Best Pearson correlation across a small range of sample-alignment
    // offsets. Real IIR filters (even ones nominally "out of the way" of a
    // test tone) have some residual group delay of their own on top of
    // whatever's reported via getLatencySamples() - typically a fraction of
    // a sample to a couple of samples, not something a plugin reports/
    // compensates as "latency". Searching a small window of offsets isolates
    // genuine shape (non)linearity from that legitimate, unreported
    // sub-block delay.
    inline double bestCorrelationOverShift (const float* a, const float* b, int numSamples, int maxShiftSamples)
    {
        double best = -1.0;

        for (int shift = -maxShiftSamples; shift <= maxShiftSamples; ++shift)
        {
            const auto length = numSamples - std::abs (shift);
            if (length <= 0)
                continue;

            const auto* aStart = a + std::max (0, shift);
            const auto* bStart = b + std::max (0, -shift);

            best = std::max (best, correlation (aStart, bStart, length));
        }

        return best;
    }
}
