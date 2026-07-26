#pragma once

#include "ADAAShaper.h"

#include <cstddef>
#include <vector>

// The v0.3.0 power-amp block (brief section 3.2): a global negative-feedback
// loop around an ADAA1 output-transformer saturator, with Resonance and
// Presence acting as shelving cuts *in the return path* - which is where a
// real amp's Presence/Depth controls live - and an envelope-driven supply-sag
// term modulating the transformer's headroom.
//
//   u[n]  = g * (x[n] - beta * fb[n-1])                   // unit delay in the loop
//   y[n]  = OT(u[n] * headroom[n]) / headroom[n]          // ADAA1 tanh
//   fb[n] = lowShelfCut(resonance) . highShelfCut(presence) (y[n])
//
// STABILITY (binding, brief section 3.2 / revision note 1)
// -------------------------------------------------------
// A unit-delay feedback loop places its small-signal pole at -L, where
// L = g * beta * |OT'| * |shelves|. It is therefore stable if and only if
// L < 1 at *every* frequency up to the oversampled Nyquist - a property of
// the discrete loop that oversampling does not improve. Sizing beta for
// "about 6 dB of loop gain" (L ~ 2) does not give a 6 dB-deep feedback
// voicing; it diverges until the tanh clamps it into a sustained fs/2 limit
// cycle.
//
// So the binding design value here is L_max <= 0.5, i.e. at least 6 dB of
// gain margin at every frequency including fs_os/2. max|OT'| = 1 (tanh slope
// at the origin) and both return shelves are cut-only (0..-12 dB), so they
// can only ever *lower* L: the worst case is exactly g*beta at neutral shelf
// settings. The audible feedback depth is voiced through the closed-loop
// factor 1/(1+L) instead of through a large L. getWorstCaseLoopGain() below
// is asserted at prepare() time and gated in CI by T-P5.
//
// If voicing ever demands more feedback than L = 0.5 permits, the sanctioned
// escalation is an implicit zero-delay-feedback solve (a per-sample scalar
// Newton/bisection on the loop equation, LUT-compatible) - an explicit
// orchestrator decision, never a silent beta increase.
class PowerAmp
{
public:
    // Forward gain and feedback fraction. Their product is the small-signal
    // loop gain at neutral shelf settings, and it is the number the L <= 0.5
    // bound applies to.
    static constexpr double forwardGain = 1.0;
    static constexpr double feedbackFraction = 0.5;
    static constexpr double maximumLoopGain = 0.5;

    // Output-transformer knee. OT(x) = tanh(k*x)/k.
    static constexpr double transformerKnee = 1.2;

    // Return-path shelf corners. The Presence corner deliberately matches the
    // v0.2 post-EQ Presence shelf (2.4 kHz) so that turning the power amp on
    // moves the same band, and Resonance sits at the classic Depth corner.
    static constexpr double resonanceShelfHz = 120.0;
    static constexpr double presenceShelfHz = 2400.0;

    static constexpr double sagAttackSeconds = 0.005;
    static constexpr double sagReleaseSeconds = 0.120;
    static constexpr double minimumHeadroom = 0.5;

    PowerAmp() = default;

    void prepare (double oversampledRate, size_t numChannels);
    void setOversampledRate (double oversampledRate) noexcept;
    void reset() noexcept;

    // Resonance: 0..12 dB of user "depth". Realised as a 0..-12 dB low-shelf
    // *cut in the return path*, which lowers the loop gain at LF and so
    // raises the closed-loop LF gain (and the LF drive into the transformer).
    // Cut-only by construction, so it can never violate the L bound.
    void setResonanceDb (float newResonanceDb) noexcept;

    // Presence: the existing -12..+12 dB user parameter. With the power amp
    // engaged it maps onto the return-path high shelf instead of the post-EQ
    // shelf (which TenebraeEngine bypasses structurally in that case). The
    // map is affine into the cut-only window: -12 dB -> 0 dB of cut (maximum
    // HF feedback, darkest), +12 dB -> -12 dB of cut (least HF feedback,
    // brightest).
    void setPresenceDb (float newPresenceDb) noexcept;

    // Sag: 0..1. Drives the transformer headroom term.
    void setSagAmount (float newSagAmount) noexcept;

    double processSample (double x, size_t channel) noexcept;

    //==========================================================================
    // Analysis accessors (const, never on the audio path).

    // Worst-case small-signal loop-gain magnitude over all frequencies, for
    // the current shelf settings. |z^-1| = 1 and max|OT'| = 1, so this is
    // g*beta times the largest shelf magnitude, which is 1 (both shelves are
    // cut-only, unity outside their cut band).
    double getWorstCaseLoopGain() const noexcept;

    // Small-signal loop-gain magnitude at `frequencyHz` for the current
    // shelf settings - the quantity T-P5 sweeps.
    double loopGainAt (double frequencyHz) const noexcept;

    // Closed-loop small-signal magnitude 1/(1 + L(w)) times the makeup gain,
    // i.e. the response the Resonance/Presence assertions in
    // tests/PowerAmpTests.cpp compare against.
    double closedLoopMagnitudeAt (double frequencyHz) const noexcept;

    double getResonanceShelfCutDb() const noexcept { return resonanceCutDb; }
    double getPresenceShelfCutDb() const noexcept { return presenceCutDb; }

private:
    struct ChannelState
    {
        tnbr::adaa::State adaa;
        double feedback = 0.0;
        double resonanceZ = 0.0;
        double presenceZ = 0.0;
        double sagEnvelope = 0.0;
    };

    // First-order shelf coefficients (bilinear, unity outside the shelf band).
    struct Shelf
    {
        double b0 = 1.0, b1 = 0.0, a1 = 0.0;

        double process (double x, double& z) const noexcept
        {
            const auto y = b0 * x + z;
            z = b1 * x - a1 * y;
            return y;
        }

        // Magnitude response at a normalised angular frequency.
        double magnitude (double omega) const noexcept;
    };

    void updateShelves() noexcept;

    static Shelf makeLowShelf (double frequencyHz, double gainLinear, double sampleRate) noexcept;
    static Shelf makeHighShelf (double frequencyHz, double gainLinear, double sampleRate) noexcept;

    tnbr::adaa::TanhShaper transformer { transformerKnee };

    double sampleRate = 192000.0;
    double resonanceCutDb = 0.0;
    double presenceCutDb = -6.0;
    double sagAmount = 0.0;

    // Compensates the nominal mid-band closed-loop attenuation 1/(1+g*beta)
    // so that engaging the power amp at neutral settings is a level move of
    // roughly nothing rather than a 3.5 dB drop.
    static constexpr double makeupGain = 1.0 + forwardGain * feedbackFraction;

    double sagAttackCoefficient = 0.0;
    double sagReleaseCoefficient = 0.0;

    Shelf resonanceShelf;
    Shelf presenceShelf;

    std::vector<ChannelState> channels;
};
