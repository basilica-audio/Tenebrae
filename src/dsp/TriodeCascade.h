#pragma once

#include "TriodeStage.h"

#include <array>
#include <cstddef>
#include <vector>

// The three-stage stateful triode chain of the v0.3.0 Triode engine
// (brief section 3.1). Runs entirely inside the oversampled region, exactly
// like the Classic engine's CascadeStage triplet, and is selected by the
// same Voicing switch (Tight/Loose) - both voicing tables are built at
// prepare() time and kept resident, so switching Voicing is a branch, never
// an allocation or a LUT rebuild.
//
// POLARITY (binding, brief section 3.1 item 5 / revision note 3)
// -------------------------------------------------------------
// Each stage inverts, exactly like the real cell, and that alternation is
// what reproduces the duty-cycle flip seen on scope shots of cascaded amps
// (research-triode-adaa.md section 1.4) - so it is deliberately preserved
// *between* stages. But three inverting stages leave the chain output net
// inverted, and neither the power amp nor the output transformer adds an
// inversion of its own. Left uncorrected that would mean:
//   - any Mix below 100 % combs/cancels at LF against the delay-compensated
//     dry path instead of blending, and
//   - A/B-ing Classic against Triode flips absolute polarity.
// So exactly one x(-1) normalisation is applied here, at the cascade output,
// before the power-amp block. tests/TriodeCascadeTests.cpp (T-C7) guards it.
class TriodeCascade
{
public:
    static constexpr int numStages = 3;
    static constexpr int numVoicings = 2; // 0 = Tight, 1 = Loose

    TriodeCascade() = default;

    // Builds both voicing tables (six stage LUTs). Allocates - prepare()
    // only, never the audio thread.
    void prepare (double oversampledRate, size_t numChannels);

    // Re-derives every rate-dependent coefficient in place. Allocation-free,
    // safe to call from the audio thread when the Quality switch changes the
    // oversampling factor.
    void setOversampledRate (double oversampledRate) noexcept;

    void reset() noexcept;

    // 0 = Tight, 1 = Loose. Selects which prepared triplet processSample()
    // runs; the unselected triplet keeps its state untouched.
    void setVoicing (int newVoicing) noexcept;

    // User Bias Shift parameter as a 0..2 scalar over the per-stage depths
    // (1.0 = the voicing's own calibrated depth = neutral default).
    void setBiasScale (double newScale) noexcept;

    double processSample (double x, size_t channel) noexcept;

    // Test/analysis access to an individual prepared stage.
    TriodeStage& getStage (int voicing, int stage) noexcept;
    const TriodeStage& getStage (int voicing, int stage) const noexcept;

private:
    // Final DC blocker. Stage 1..3's own coupling caps sit *ahead* of each
    // shaper, so the third stage's asymmetric-clipping DC offset would
    // otherwise walk straight into the power amp (and, at Mix < 100 %, into
    // the dry/wet sum). One-pole at 10 Hz, well below anything the Tight HPF
    // upstream leaves behind.
    static constexpr double outputBlockerHz = 10.0;

    std::array<std::array<TriodeStage, numStages>, numVoicings> stages;

    int currentVoicing = 0;
    double outputBlockerCoefficient = 0.0;
    std::vector<double> outputBlockerZ;
};
