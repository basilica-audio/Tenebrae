#include "TriodeCascade.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double twoPi = 6.283185307179586;

    double onePoleLowPass (double frequencyHz, double sampleRate) noexcept
    {
        if (! (sampleRate > 0.0))
            return 1.0;

        const auto f = std::clamp (frequencyHz, 1.0, 0.5 * sampleRate * 0.49);
        return 1.0 - std::exp (-twoPi * f / sampleRate);
    }

    // ==========================================================================
    // Voicing tables. These are the Triode-engine counterpart of the Classic
    // engine's fixed (asymmetry, HP, LP) triples in TenebraeEngine.cpp, and
    // they follow the same philosophy: each successive stage is driven a
    // little harder, clamps a little sooner, and is filtered a little
    // tighter/darker than the last, so the chain converges onto the chug band
    // instead of piling up fizz.
    //
    // What is different is that every number here is a circuit quantity that
    // the model actually uses, not a shaping constant:
    //
    //  - Rk / Ck    set the DC operating point *and* the cathode-bypass
    //               corner. "Tight" is the 1k5 / 0.68 uF British pairing:
    //               the cathode is only bypassed above a few hundred Hz, so
    //               bass sees local feedback and stays clean while the mids
    //               and treble get the full stage gain. "Loose" is the 820R /
    //               25 uF full-band-bypass pairing - no frequency-dependent
    //               drive, the vintage-leaning voice.
    //  - R_stop     sets how hard grid conduction clamps the positive swing.
    //               Larger = softer, more compressed clipping, so it grows
    //               down the chain.
    //  - gridScale  volts at the grid per 1.0 of plugin signal, i.e. the
    //               per-stage drive. Chosen so the resulting small-signal
    //               stage gains (~4 / ~6.6 / ~8.7 dB) land on the Classic
    //               cascade's 6 / 8 / 10 dB drive feel.
    //  - coupling / miller are the interstage HP and LP corners, carried over
    //               in spirit from the Classic voicing tables (Tight
    //               80/120/150 Hz HP) with the Miller poles retuned to the
    //               one-pole (6 dB/oct) slope the physical Cin/R_source pair
    //               actually has, rather than the Classic 2nd-order table.
    // ==========================================================================

    using Voicing = TriodeStage::Voicing;

    constexpr Voicing tightVoicing[TriodeCascade::numStages] = {
        //  Rk      Ck        R_stop    gridScale  couplingHP  millerLP  bias  bloom
        { 1500.0, 0.68e-6,  47.0e3,    3.0,        80.0,      5000.0,   0.60, 0.020 },
        { 1500.0, 0.68e-6,  68.0e3,    4.0,       120.0,      3500.0,   0.70, 0.020 },
        { 1500.0, 0.68e-6, 100.0e3,    5.0,       150.0,      2100.0,   0.80, 0.020 },
    };

    constexpr Voicing looseVoicing[TriodeCascade::numStages] = {
        {  820.0, 25.0e-6,  68.0e3,    2.5,        60.0,      8000.0,   0.45, 0.015 },
        {  820.0, 25.0e-6, 100.0e3,    3.3,        90.0,      5500.0,   0.55, 0.015 },
        {  820.0, 25.0e-6, 150.0e3,    4.2,       110.0,      3500.0,   0.65, 0.015 },
    };
}

//==============================================================================
void TriodeCascade::prepare (double oversampledRate, size_t numChannels)
{
    const auto channelCount = std::max<size_t> (1, numChannels);

    for (int stage = 0; stage < numStages; ++stage)
    {
        stages[0][static_cast<size_t> (stage)].prepare (tightVoicing[stage], oversampledRate, channelCount);
        stages[1][static_cast<size_t> (stage)].prepare (looseVoicing[stage], oversampledRate, channelCount);
    }

    outputBlockerZ.assign (channelCount, 0.0);
    setOversampledRate (oversampledRate);
    reset();
}

void TriodeCascade::setOversampledRate (double oversampledRate) noexcept
{
    for (auto& voicing : stages)
        for (auto& stage : voicing)
            stage.setOversampledRate (oversampledRate);

    outputBlockerCoefficient = onePoleLowPass (outputBlockerHz, oversampledRate);
}

void TriodeCascade::reset() noexcept
{
    for (auto& voicing : stages)
        for (auto& stage : voicing)
            stage.reset();

    std::fill (outputBlockerZ.begin(), outputBlockerZ.end(), 0.0);
}

void TriodeCascade::setVoicing (int newVoicing) noexcept
{
    currentVoicing = std::clamp (newVoicing, 0, numVoicings - 1);
}

void TriodeCascade::setBiasScale (double newScale) noexcept
{
    const auto scale = std::isnan (newScale) ? 1.0 : std::clamp (newScale, 0.0, 2.0);

    for (auto& voicing : stages)
        for (auto& stage : voicing)
            stage.setBiasScale (scale);
}

TriodeStage& TriodeCascade::getStage (int voicing, int stage) noexcept
{
    const auto v = static_cast<size_t> (std::clamp (voicing, 0, numVoicings - 1));
    const auto s = static_cast<size_t> (std::clamp (stage, 0, numStages - 1));
    return stages[v][s];
}

const TriodeStage& TriodeCascade::getStage (int voicing, int stage) const noexcept
{
    const auto v = static_cast<size_t> (std::clamp (voicing, 0, numVoicings - 1));
    const auto s = static_cast<size_t> (std::clamp (stage, 0, numStages - 1));
    return stages[v][s];
}

double TriodeCascade::processSample (double x, size_t channel) noexcept
{
    auto& triplet = stages[static_cast<size_t> (currentVoicing)];

    auto v = x;

    for (auto& stage : triplet)
        v = stage.processSample (v, channel);

    // Final DC block (see the header): the third stage has no downstream
    // coupling cap of its own.
    if (! outputBlockerZ.empty())
    {
        auto& z = outputBlockerZ[std::min (channel, outputBlockerZ.size() - 1)];
        z += outputBlockerCoefficient * (v - z);
        v -= z;
    }

    // The single, deliberate polarity normalisation for the whole chain -
    // see the header's POLARITY note and T-C7.
    return -v;
}
