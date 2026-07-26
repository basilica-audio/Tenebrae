#include "TriodeStage.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double twoPi = 6.283185307179586;

    // One-pole coefficient for an exponential ramp reaching ~63 % of a step
    // in `seconds`.
    double onePoleCoefficient (double seconds, double sampleRate) noexcept
    {
        if (! (seconds > 0.0) || ! (sampleRate > 0.0))
            return 1.0;

        return 1.0 - std::exp (-1.0 / (sampleRate * seconds));
    }

    // Pre-warped bilinear pole coefficient for a first-order low-pass at
    // `frequencyHz`. Returns the "a" in y += a*(x - y).
    double lowPassCoefficient (double frequencyHz, double sampleRate) noexcept
    {
        if (! (sampleRate > 0.0))
            return 1.0;

        const auto nyquist = 0.5 * sampleRate;
        const auto f = std::clamp (frequencyHz, 1.0, nyquist * 0.49);
        const auto omega = twoPi * f / sampleRate;
        return 1.0 - std::exp (-omega);
    }
}

//==============================================================================
double TriodeStage::Dempwolf12AX7::softplus (double x, double c) noexcept
{
    const auto cx = c * x;

    if (cx > 30.0)
        return x;                       // log1p(exp(cx))/c -> x

    if (cx < -30.0)
        return std::exp (cx) / c;       // -> 0+, derivative still smooth

    return std::log1p (std::exp (cx)) / c;
}

double TriodeStage::Dempwolf12AX7::ik (double vgk, double vak) const noexcept
{
    const auto veff = vgk + vak / mu;
    const auto base = std::max (softplus (veff, C), 1.0e-30);
    return G * std::exp (gamma * std::log (base));
}

double TriodeStage::Dempwolf12AX7::ig (double vgk) const noexcept
{
    const auto base = std::max (softplus (vgk, Cg), 1.0e-30);
    return Gg * std::exp (xi * std::log (base)) + Ig0;
}

double TriodeStage::Dempwolf12AX7::ia (double vgk, double vak) const noexcept
{
    return ik (vgk, vak) - ig (vgk);
}

double TriodeStage::Dempwolf12AX7::diaDvak (double vgk, double vak) const noexcept
{
    // d/d(vak) of G * softplus(vgk + vak/mu, C)^gamma. The grid term does not
    // depend on vak (Dempwolf's measured Ig is a function of Vg alone), so it
    // drops out.
    const auto veff = vgk + vak / mu;
    const auto sp = std::max (softplus (veff, C), 1.0e-30);
    const auto logistic = 1.0 / (1.0 + std::exp (-C * veff)); // d(softplus)/d(veff)
    return G * gamma * std::exp ((gamma - 1.0) * std::log (sp)) * logistic / mu;
}

//==============================================================================
void TriodeStage::prepare (const Voicing& newVoicing, double oversampledRate, size_t numChannels)
{
    voicing = newVoicing;
    channels.assign (std::max<size_t> (1, numChannels), ChannelState {});

    buildCurve();
    setOversampledRate (oversampledRate);
    reset();
}

void TriodeStage::setOversampledRate (double oversampledRate) noexcept
{
    sampleRate = oversampledRate > 0.0 ? oversampledRate : 44100.0;

    couplingHighPassCoefficient = lowPassCoefficient (voicing.couplingHighPassHz, sampleRate);
    millerCoefficient = lowPassCoefficient (voicing.millerLowPassHz, sampleRate);

    biasAttackCoefficient = onePoleCoefficient (biasAttackSeconds, sampleRate);
    biasReleaseCoefficient = onePoleCoefficient (biasReleaseSeconds, sampleRate);
    bloomCoefficient = onePoleCoefficient (bloomSeconds, sampleRate);

    // Cathode-bypass shelf. Rk||Ck is bypassed above the pole; below it the
    // stage sees local (degenerative) feedback through Rk and loses gain.
    //
    //   zero at  1 / (Rk * Ck)
    //   pole at  1 / ((Rk || rk) * Ck),   rk = (ra + Ra) / (mu + 1)
    //
    // Normalised so the *high* side is unity (the shaper's own scaling
    // already carries the stage's mid-band gain), which makes the shelf a
    // pure bass cut of Rk_parallel/Rk - the frequency-dependent drive the
    // brief calls the "British" fingerprint.
    constexpr double plateResistanceOhms = 62.5e3; // ra, 12AX7 datasheet class
    const auto rk = (plateResistanceOhms + plateLoadOhms) / (tube.mu + 1.0);
    const auto rkParallel = (voicing.cathodeResistorOhms * rk) / (voicing.cathodeResistorOhms + rk);

    cathodeZeroRadians = 1.0 / (voicing.cathodeResistorOhms * voicing.cathodeCapacitorF);
    cathodePoleRadians = 1.0 / (rkParallel * voicing.cathodeCapacitorF);

    // Bilinear transform of H(s) = (wz/wp) * (1 + s/wz) / (1 + s/wp), with
    // frequency pre-warping on both singularities.
    const auto warp = [this] (double omega)
    {
        const auto nyquist = 0.5 * sampleRate;
        const auto f = std::clamp (omega / twoPi, 0.01, nyquist * 0.49);
        return 2.0 * sampleRate * std::tan (twoPi * f / (2.0 * sampleRate));
    };

    const auto wz = warp (cathodeZeroRadians);
    const auto wp = warp (cathodePoleRadians);
    const auto k = 2.0 * sampleRate;
    const auto gain = wz / wp;
    const auto denominator = wp + k;

    cathodeShelfB0 = gain * (wz + k) / denominator;
    cathodeShelfB1 = gain * (wz - k) / denominator;
    cathodeShelfA1 = (wp - k) / denominator;
}

void TriodeStage::reset() noexcept
{
    for (auto& channel : channels)
    {
        channel.adaa.reset (0.0);
        channel.couplingHighPassZ = 0.0;
        channel.cathodeShelfZ = 0.0;
        channel.millerZ = 0.0;
        channel.bias = 0.0;
        channel.bloom = 0.0;
    }
}

//==============================================================================
double TriodeStage::solvePlateVolts (double gridSourceVolts) const
{
    // Two coupled scalar equations, solved by nested damped Newton:
    //
    //   grid node:  Vg   = Vg_src - Ig(Vg - Vk_q) * R_stop      (grid stopper)
    //   plate node: (Vb - Va) / Ra = Ia(Vg - Vk_q, Va - Vk_q)
    //
    // The cathode is held at its quiescent value because Ck shorts Rk at
    // audio rates; the *frequency-dependent* part of that bypass is modelled
    // separately by the analytic cathode shelf (see setOversampledRate()).
    //
    // Grid conduction through R_stop is what produces the flat, compressed
    // positive-grid side of the curve - the asymmetry a plain tanh cannot
    // manufacture.
    double vg = std::min (gridSourceVolts, quiescentVk);

    for (int iteration = 0; iteration < 60; ++iteration)
    {
        const auto vgk = vg - quiescentVk;
        const auto residual = vg - gridSourceVolts + tube.ig (vgk) * voicing.gridStopperOhms;

        // d(residual)/d(vg) = 1 + R_stop * d(Ig)/d(vgk), always >= 1, so this
        // Newton step is unconditionally well-conditioned.
        const auto sp = std::max (Dempwolf12AX7::softplus (vgk, tube.Cg), 1.0e-30);
        const auto logistic = 1.0 / (1.0 + std::exp (-tube.Cg * vgk));
        const auto digDvgk = tube.Gg * tube.xi * std::exp ((tube.xi - 1.0) * std::log (sp)) * logistic;
        const auto derivative = 1.0 + voicing.gridStopperOhms * digDvgk;

        const auto step = residual / derivative;
        vg -= std::clamp (step, -5.0, 5.0);

        if (std::abs (step) < 1.0e-12)
            break;
    }

    const auto vgk = vg - quiescentVk;

    double va = quiescentVa;

    for (int iteration = 0; iteration < 80; ++iteration)
    {
        const auto vak = va - quiescentVk;
        const auto residual = (supplyVolts - va) / plateLoadOhms - tube.ia (vgk, vak);
        const auto derivative = -1.0 / plateLoadOhms - tube.diaDvak (vgk, vak);

        const auto step = residual / derivative;
        // Damped and bounded: the plate curve is stiff near saturation, and
        // an undamped step can otherwise jump straight past the solution
        // into the (unphysical) negative-plate-voltage region.
        va -= std::clamp (0.7 * step, -40.0, 40.0);
        va = std::clamp (va, 0.0, supplyVolts);

        if (std::abs (step) < 1.0e-9)
            break;
    }

    return va;
}

void TriodeStage::buildCurve()
{
    // ---- 1. Quiescent operating point -------------------------------------
    // At DC the cathode cap is an open circuit, so the whole cathode current
    // flows through Rk and self-biases the grid negative:
    //     Vk = Ik * Rk,  Va = Vb - Ia * Ra,  Vg = 0 (through the grid leak)
    // Solved by damped fixed-point iteration on Ik, which converges from any
    // physically sane start for the 12AX7/100k/1k5-class values used here.
    double current = 1.0e-3;

    for (int iteration = 0; iteration < 400; ++iteration)
    {
        const auto vk = current * voicing.cathodeResistorOhms;
        const auto va = supplyVolts - current * plateLoadOhms;
        const auto updated = tube.ik (-vk, va - vk);
        const auto next = current + 0.15 * (updated - current);

        if (std::abs (next - current) < 1.0e-15)
        {
            current = next;
            break;
        }

        current = std::clamp (next, 1.0e-9, 5.0e-3);
    }

    quiescentVk = current * voicing.cathodeResistorOhms;
    quiescentVa = supplyVolts - current * plateLoadOhms;

    // Grid conduction starts once the grid reaches the cathode, i.e. once the
    // incoming signal has cancelled the self-bias. In normalised units:
    gridClampPoint = quiescentVk / voicing.gridScaleVolts;

    // ---- 2. Tabulate the static stage curve -------------------------------
    std::vector<double> samples (lutPoints, 0.0);

    const auto step = (2.0 * lutHalfWidth) / static_cast<double> (lutPoints - 1);
    double largestExcursion = 0.0;

    for (size_t i = 0; i < lutPoints; ++i)
    {
        const auto x = -lutHalfWidth + step * static_cast<double> (i);
        const auto va = solvePlateVolts (x * voicing.gridScaleVolts);
        samples[i] = va - quiescentVa;
        largestExcursion = std::max (largestExcursion, std::abs (samples[i]));
    }

    // Normalise the *largest* plate excursion to unity. The two sides of the
    // curve are deliberately left unequal - the grid-clamp side compresses
    // against the grid stopper while the cutoff side asymptotes at the supply
    // rail - which is precisely the duty-cycle asymmetry the cascade is after.
    outputScale = largestExcursion > 0.0 ? 1.0 / largestExcursion : 1.0;

    for (auto& sample : samples)
        sample *= outputScale;

    curve.build (samples, -lutHalfWidth, lutHalfWidth);
}

//==============================================================================
double TriodeStage::cathodeShelfMagnitude (double frequencyHz) const noexcept
{
    const auto omega = twoPi * frequencyHz;
    const auto numerator = std::hypot (1.0, omega / cathodeZeroRadians);
    const auto denominator = std::hypot (1.0, omega / cathodePoleRadians);
    return (cathodeZeroRadians / cathodePoleRadians) * numerator / denominator;
}

//==============================================================================
double TriodeStage::processSample (double x, size_t channel) noexcept
{
    if (channels.empty())
        return x;

    auto& state = channels[std::min (channel, channels.size() - 1)];

    // ---- Interstage coupling cap: DC block ahead of the shaper ------------
    // (brief section 3.1 item 4 - the coupling HPF belongs *before* the
    // nonlinearity, not after it, so each stage's own DC offset is stripped
    // before the next stage's bias point sees it.)
    state.couplingHighPassZ += couplingHighPassCoefficient * (x - state.couplingHighPassZ);
    auto v = x - state.couplingHighPassZ;

    // ---- Cathode bypass shelf --------------------------------------------
    const auto shelfOut = cathodeShelfB0 * v + state.cathodeShelfZ;
    state.cathodeShelfZ = cathodeShelfB1 * v - cathodeShelfA1 * shelfOut;
    v = shelfOut;

    // ---- Dynamic bias: grid-overshoot rectifier -> asymmetric one-pole ----
    // The rectified overshoot above the grid clamp point charges the coupling
    // cap fast (R_stop*Cout) and bleeds off slowly (Cout*Rg), and the result
    // is *subtracted* from the shaper input: the stage biases itself toward
    // cutoff after an overload and takes ~20 ms to come back.
    const auto overshoot = std::max (0.0, v - gridClampPoint);
    const auto biasCoefficient = overshoot > state.bias ? biasAttackCoefficient : biasReleaseCoefficient;
    state.bias += biasCoefficient * (overshoot - state.bias);

    const auto shaperInput = v - biasScale * voicing.biasDepth * state.bias - state.bloom;

    // ---- The stage curve, antialiased ------------------------------------
    const auto shaped = tnbr::adaa::process (curve, state.adaa, shaperInput);

    // ---- Cathode bloom ----------------------------------------------------
    // Slow follower on the stage's output-current proxy, adding a small extra
    // bias offset (kept well under half a dB equivalent) - the attack "bloom"
    // of a charging cathode cap.
    state.bloom += bloomCoefficient * (voicing.bloomDepth * std::abs (shaped) - state.bloom);

    // ---- Miller interstage low-pass --------------------------------------
    state.millerZ += millerCoefficient * (shaped - state.millerZ);

    return state.millerZ;
}
