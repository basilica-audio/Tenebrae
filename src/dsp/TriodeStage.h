#pragma once

#include "ADAAShaper.h"

#include <array>
#include <cstddef>
#include <vector>

// One stateful triode gain stage of the v0.3.0 Triode engine - Tier B of
// research-triode-adaa.md section 4, i.e. a *calibrated static stage curve*
// (solved from the Dempwolf-Zoelzer 12AX7 equations, not hand-drawn) wrapped
// in the three time-variant bolt-ons that a memoryless waveshaper can never
// produce:
//
//   1. Dynamic bias sidechain  - grid conduction charges the coupling cap,
//      biasing the stage more negative after an overload; recovers over
//      tens of ms. This is bias shift / blocking distortion, the effect
//      responsible for per-note "sag and breathe" and for palm mutes feeling
//      compressed (research section 1.2).
//   2. Cathode bypass shelf    - Rk||Ck only bypasses the cathode resistor
//      above its corner, so bass sees local feedback (less gain, less
//      distortion) while treble sees the full stage gain. Frequency-dependent
//      distortion, the core "British" fingerprint (research section 1.1).
//      Plus a slow bloom follower adding a small extra bias offset.
//   3. Miller interstage LPF   - Cin = Cgk + Cag*(1+|A|) against the source
//      impedance is a one-pole low-pass between every pair of stages
//      (research section 1.3).
//
// Signal order per sample (brief section 3.1):
//     in -> coupling HPF (DC block) -> cathode shelf -> minus bias/bloom
//        -> ADAA1 through the stage LUT -> Miller LPF -> out
//
// The stage is inverting, exactly like the real cell (a positive grid swing
// pulls the plate down). TriodeCascade owns the single net polarity
// normalisation for the whole chain - see there.
//
// All state is double; the LUT is built in prepare() (never on the audio
// thread) and is read-only afterwards. process() allocates nothing.
class TriodeStage
{
public:
    // Everything that distinguishes one voiced stage from another. Values
    // are real circuit quantities wherever the circuit has one, so the
    // voicing tables in TriodeCascade.cpp read as a schematic rather than as
    // magic numbers.
    struct Voicing
    {
        double cathodeResistorOhms = 1500.0;   // Rk
        double cathodeCapacitorF = 0.68e-6;    // Ck
        double gridStopperOhms = 47.0e3;       // R_stop (sets the grid-clamp softness)
        double gridScaleVolts = 3.0;           // volts at the grid per 1.0 of plugin signal
        double couplingHighPassHz = 80.0;      // Cout*Rg DC block ahead of the shaper
        double millerLowPassHz = 5000.0;       // Cin against the source impedance
        double biasDepth = 0.6;                // dynamic-bias sidechain depth (normalised)
        double bloomDepth = 0.02;              // cathode bloom offset (<= ~0.5 dB equivalent)
    };

    TriodeStage() = default;

    // Builds the stage LUT for `voicing` and sizes every coefficient for
    // `oversampledRate`. Allocates - call from prepare(), never from the
    // audio thread.
    void prepare (const Voicing& newVoicing, double oversampledRate, size_t numChannels);

    // Re-derives every rate-dependent coefficient without touching the LUT
    // or allocating - this is what a Quality (oversampling factor) switch
    // calls on the audio thread.
    void setOversampledRate (double oversampledRate) noexcept;

    void reset() noexcept;

    // Scales all three bias-sidechain depths, 0..2 (the user Bias Shift
    // parameter, 0-200 %). 1.0 is the neutral default.
    void setBiasScale (double newScale) noexcept { biasScale = newScale; }

    // Processes one sample on `channel`. `channel` must be < the numChannels
    // passed to prepare().
    double processSample (double x, size_t channel) noexcept;

    //==========================================================================
    // Analysis / test accessors. All const, none used on the audio path.

    // Quiescent DC operating point solved at prepare() time, in volts.
    double getQuiescentCathodeVolts() const noexcept { return quiescentVk; }
    double getQuiescentPlateVolts() const noexcept { return quiescentVa; }

    // The static stage curve S: normalised grid signal -> normalised plate
    // signal, i.e. exactly what the ADAA1 shaper integrates.
    double evaluateCurve (double x) const noexcept { return curve.value (x); }
    const tnbr::adaa::LutShaper& getCurve() const noexcept { return curve; }

    // Normalised input level at which grid conduction begins (the bias
    // sidechain's rectifier corner).
    double getGridClampPoint() const noexcept { return gridClampPoint; }

    // Analytic cathode-bypass shelf response magnitude at `frequencyHz`,
    // for the T-C4 assertion in tests/TriodeStageTests.cpp.
    double cathodeShelfMagnitude (double frequencyHz) const noexcept;

    const Voicing& getVoicing() const noexcept { return voicing; }

    //==========================================================================
    // The Dempwolf-Zoelzer 12AX7 model itself (DAFx-11 equations 10-12,
    // Table 1 "RSD-1" fit). Exposed as a free-standing struct so
    // tests/TriodeStageTests.cpp can regression-test the equations directly
    // against published values, independently of any stage wiring.
    struct Dempwolf12AX7
    {
        double G = 2.242e-3;
        double mu = 103.2;
        double gamma = 1.26;
        double C = 3.40;
        double Gg = 6.177e-4;
        double xi = 1.314;
        double Cg = 9.901;
        double Ig0 = 8.025e-8;

        // log(1 + exp(c*x)) / c, with the overflow guards from
        // research-triode-adaa.md section 3.1. Strictly positive, C-infinity,
        // so pow() below never sees a non-positive base.
        static double softplus (double x, double c) noexcept;

        double ik (double vgk, double vak) const noexcept;   // cathode current, eq. 10
        double ig (double vgk) const noexcept;               // grid current,    eq. 11
        double ia (double vgk, double vak) const noexcept;   // plate current,   eq. 12

        // d(ia)/d(vak), used by the Newton solve below.
        double diaDvak (double vgk, double vak) const noexcept;
    };

private:
    // Number of LUT knots. 2048 per the brief; the curve is smooth on the
    // scale of the knot spacing so cubic Hermite interpolation error is far
    // below the alias floor the shaper is graded against.
    static constexpr size_t lutPoints = 2048;

    // LUT domain in normalised plugin units. Wider than +/-1 so that
    // interstage overshoot stays inside the tabulated (rather than
    // constant-extrapolated) region.
    static constexpr double lutHalfWidth = 3.0;

    // Common-cathode cell constants shared by every voicing.
    static constexpr double supplyVolts = 300.0;      // Vb
    static constexpr double plateLoadOhms = 100.0e3;  // Ra

    // Bias sidechain time constants (research section 1.2 / brief 3.1):
    // attack ~ R_stop*Cout, release ~ Cout*Rg = 22n * 1M.
    static constexpr double biasAttackSeconds = 0.0005;
    static constexpr double biasReleaseSeconds = 0.020;
    // Cathode bloom follower.
    static constexpr double bloomSeconds = 0.010;

    struct ChannelState
    {
        tnbr::adaa::State adaa;
        double couplingHighPassZ = 0.0;
        double cathodeShelfZ = 0.0;
        double millerZ = 0.0;
        double bias = 0.0;
        double bloom = 0.0;
    };

    // Solves the quiescent operating point (grid at 0 V through the grid
    // leak, cathode cap open at DC) and then tabulates S over the LUT domain.
    void buildCurve();

    // Plate voltage for a given grid-source voltage, with the cathode held
    // at its quiescent value (the cap shorts Rk at audio rates) and the grid
    // stopper in circuit so grid conduction clamps the positive swing.
    double solvePlateVolts (double gridSourceVolts) const;

    Dempwolf12AX7 tube;
    Voicing voicing;

    tnbr::adaa::LutShaper curve;

    double sampleRate = 352800.0;
    double quiescentVk = 1.5;
    double quiescentVa = 200.0;
    double outputScale = 1.0;
    double gridClampPoint = 0.5;
    double biasScale = 1.0;

    // Coefficients, all one-pole/first-order, recomputed by
    // setOversampledRate().
    double couplingHighPassCoefficient = 0.0;
    double millerCoefficient = 0.0;
    double cathodeShelfB0 = 1.0, cathodeShelfB1 = 0.0, cathodeShelfA1 = 0.0;
    double biasAttackCoefficient = 0.0;
    double biasReleaseCoefficient = 0.0;
    double bloomCoefficient = 0.0;

    // Analytic cathode-shelf poles/zeros in rad/s, kept for
    // cathodeShelfMagnitude() and for the docs.
    double cathodeZeroRadians = 1.0;
    double cathodePoleRadians = 1.0;

    std::vector<ChannelState> channels;
};
