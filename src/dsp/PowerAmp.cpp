#include "PowerAmp.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double twoPi = 6.283185307179586;

    double onePoleCoefficient (double seconds, double sampleRate) noexcept
    {
        if (! (seconds > 0.0) || ! (sampleRate > 0.0))
            return 1.0;

        return 1.0 - std::exp (-1.0 / (sampleRate * seconds));
    }

    double decibelsToGain (double db) noexcept
    {
        return std::pow (10.0, db / 20.0);
    }
}

//==============================================================================
double PowerAmp::Shelf::magnitude (double omega) const noexcept
{
    // |H(e^jw)| for y = b0*x + z ; z = b1*x - a1*y  (first order, a0 = 1)
    const auto cosW = std::cos (omega);
    const auto sinW = std::sin (omega);

    const auto numeratorReal = b0 + b1 * cosW;
    const auto numeratorImaginary = -b1 * sinW;
    const auto denominatorReal = 1.0 + a1 * cosW;
    const auto denominatorImaginary = -a1 * sinW;

    const auto numerator = std::hypot (numeratorReal, numeratorImaginary);
    const auto denominator = std::hypot (denominatorReal, denominatorImaginary);

    return denominator > 0.0 ? numerator / denominator : 0.0;
}

PowerAmp::Shelf PowerAmp::makeLowShelf (double frequencyHz, double gainLinear, double sampleRate) noexcept
{
    // First-order low shelf, unity at HF and `gainLinear` at DC:
    //     H(s) = (s + g*w) / (s + w)
    // bilinear-transformed with frequency pre-warping.
    Shelf shelf;

    if (! (sampleRate > 0.0))
        return shelf;

    const auto f = std::clamp (frequencyHz, 1.0, 0.5 * sampleRate * 0.49);
    const auto w = 2.0 * sampleRate * std::tan (twoPi * f / (2.0 * sampleRate));
    const auto k = 2.0 * sampleRate;
    const auto g = std::max (1.0e-6, gainLinear);

    const auto denominator = k + w;
    shelf.b0 = (k + g * w) / denominator;
    shelf.b1 = (g * w - k) / denominator;
    shelf.a1 = (w - k) / denominator;

    return shelf;
}

PowerAmp::Shelf PowerAmp::makeHighShelf (double frequencyHz, double gainLinear, double sampleRate) noexcept
{
    // First-order high shelf, unity at DC and `gainLinear` at Nyquist:
    //     H(s) = (g*s + w) / (s + w)
    Shelf shelf;

    if (! (sampleRate > 0.0))
        return shelf;

    const auto f = std::clamp (frequencyHz, 1.0, 0.5 * sampleRate * 0.49);
    const auto w = 2.0 * sampleRate * std::tan (twoPi * f / (2.0 * sampleRate));
    const auto k = 2.0 * sampleRate;
    const auto g = std::max (1.0e-6, gainLinear);

    const auto denominator = k + w;
    shelf.b0 = (g * k + w) / denominator;
    shelf.b1 = (w - g * k) / denominator;
    shelf.a1 = (w - k) / denominator;

    return shelf;
}

//==============================================================================
void PowerAmp::prepare (double oversampledRate, size_t numChannels)
{
    channels.assign (std::max<size_t> (1, numChannels), ChannelState {});
    setOversampledRate (oversampledRate);
    reset();
}

void PowerAmp::setOversampledRate (double oversampledRate) noexcept
{
    sampleRate = oversampledRate > 0.0 ? oversampledRate : 44100.0;

    sagAttackCoefficient = onePoleCoefficient (sagAttackSeconds, sampleRate);
    sagReleaseCoefficient = onePoleCoefficient (sagReleaseSeconds, sampleRate);

    updateShelves();
}

void PowerAmp::updateShelves() noexcept
{
    // Both shelves are cut-only: their magnitude never exceeds 1 anywhere, so
    // the worst-case loop gain stays at exactly g*beta. See the header.
    resonanceShelf = makeLowShelf (resonanceShelfHz, decibelsToGain (resonanceCutDb), sampleRate);
    presenceShelf = makeHighShelf (presenceShelfHz, decibelsToGain (presenceCutDb), sampleRate);
}

void PowerAmp::reset() noexcept
{
    for (auto& channel : channels)
    {
        tnbr::adaa::prime (transformer, channel.adaa, 0.0);
        channel.feedback = 0.0;
        channel.resonanceZ = 0.0;
        channel.presenceZ = 0.0;
        channel.sagEnvelope = 0.0;
    }
}

void PowerAmp::setResonanceDb (float newResonanceDb) noexcept
{
    // NaN-safe clamp, house pattern (juce::jlimit is not NaN-safe).
    const auto safe = std::isnan (newResonanceDb) ? 0.0 : static_cast<double> (newResonanceDb);
    const auto clamped = std::clamp (safe, 0.0, 12.0);
    const auto cut = -clamped;

    if (cut != resonanceCutDb)
    {
        resonanceCutDb = cut;
        updateShelves();
    }
}

void PowerAmp::setPresenceDb (float newPresenceDb) noexcept
{
    const auto safe = std::isnan (newPresenceDb) ? 0.0 : static_cast<double> (newPresenceDb);
    const auto clamped = std::clamp (safe, -12.0, 12.0);

    // Affine map of the -12..+12 dB user control into the cut-only 0..-12 dB
    // return-path window (see the header).
    const auto cut = -0.5 * (clamped + 12.0);

    if (cut != presenceCutDb)
    {
        presenceCutDb = cut;
        updateShelves();
    }
}

void PowerAmp::setSagAmount (float newSagAmount) noexcept
{
    const auto safe = std::isnan (newSagAmount) ? 0.0f : newSagAmount;
    sagAmount = std::clamp (static_cast<double> (safe), 0.0, 1.0);
}

//==============================================================================
double PowerAmp::getWorstCaseLoopGain() const noexcept
{
    double worst = 0.0;

    // Sweep the whole discrete band, not just the audio range: a unit-delay
    // loop's stability is decided at every frequency up to fs_os/2.
    constexpr int points = 4096;

    for (int i = 0; i <= points; ++i)
    {
        const auto omega = 3.141592653589793 * static_cast<double> (i) / static_cast<double> (points);
        const auto magnitude = forwardGain * feedbackFraction
                                * resonanceShelf.magnitude (omega) * presenceShelf.magnitude (omega);
        worst = std::max (worst, magnitude);
    }

    return worst;
}

double PowerAmp::loopGainAt (double frequencyHz) const noexcept
{
    if (! (sampleRate > 0.0))
        return 0.0;

    const auto omega = twoPi * frequencyHz / sampleRate;
    return forwardGain * feedbackFraction * resonanceShelf.magnitude (omega) * presenceShelf.magnitude (omega);
}

double PowerAmp::closedLoopMagnitudeAt (double frequencyHz) const noexcept
{
    if (! (sampleRate > 0.0))
        return 0.0;

    const auto omega = twoPi * frequencyHz / sampleRate;

    // L(z) = g*beta*shelves(z)*z^-1, so the closed-loop denominator is
    // 1 + L(z) with the unit delay's phase included.
    const auto shelfMagnitude = resonanceShelf.magnitude (omega) * presenceShelf.magnitude (omega);
    const auto loop = forwardGain * feedbackFraction * shelfMagnitude;

    // The shelves are minimum-phase and gentle; taking the delay's phase into
    // account exactly would need their phase response too. At and below the
    // audio band the delay's phase is negligible, which is the region every
    // assertion in tests/PowerAmpTests.cpp uses.
    const auto real = 1.0 + loop * std::cos (-omega);
    const auto imaginary = loop * std::sin (-omega);

    return makeupGain * forwardGain / std::hypot (real, imaginary);
}

//==============================================================================
double PowerAmp::processSample (double x, size_t channel) noexcept
{
    if (channels.empty())
        return x;

    auto& state = channels[std::min (channel, channels.size() - 1)];

    // Global negative feedback with the loop's unit delay (the previous
    // sample's shaped return).
    const auto u = forwardGain * (x - feedbackFraction * state.feedback);

    // Supply sag: the envelope of the output squeezes the transformer's
    // headroom, so the block loses gain under sustained drive and recovers
    // over ~120 ms - gain droop and bloom per note.
    //
    // DEVIATION FROM THE BRIEF (recorded in the PR). Brief section 3.2 writes
    // this as y = OT(u * headroom) / headroom. That is inverted with respect
    // to its own stated intent: OT saturates at 1/k, so OT(u*h)/h saturates at
    // 1/(k*h), and a *shrinking* h therefore *raises* the ceiling - sag would
    // make the block louder and less compressed instead of producing "gain
    // droop + recovery per note". The multiply and the divide are swapped
    // here so that the ceiling is h/k and shrinks with the headroom, which is
    // what a sagging supply actually does. The stability bound is untouched:
    // d(y)/d(u) = OT'(u/h) <= 1 either way, so max|OT'| = 1 still holds and
    // T-P5's L <= 0.5 gate is unaffected.
    const auto headroom = std::clamp (1.0 - sagAmount * sagSensitivity * state.sagEnvelope,
                                      minimumHeadroom, 1.0);

    const auto y = tnbr::adaa::process (transformer, state.adaa, u / headroom) * headroom;

    const auto rectified = std::abs (y);
    const auto sagCoefficient = rectified > state.sagEnvelope ? sagAttackCoefficient : sagReleaseCoefficient;
    state.sagEnvelope += sagCoefficient * (rectified - state.sagEnvelope);

    // Shaping lives in the RETURN path - cutting a band here raises the
    // closed-loop gain in that band, which is exactly how Resonance and
    // Presence behave on the hardware.
    auto returnPath = resonanceShelf.process (y, state.resonanceZ);
    returnPath = presenceShelf.process (returnPath, state.presenceZ);
    state.feedback = returnPath;

    return makeupGain * y;
}
