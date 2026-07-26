#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Shared first-order antiderivative-antialiasing (ADAA1) machinery for the
// v0.3.0 Triode engine (docs/architecture.md; brief section 3.3).
//
// ADAA1 (Parker/Zavalishin/Le Bivic, DAFx-16) replaces a memoryless shaper
// f(x) by the divided difference of its antiderivative F1:
//
//     y[n] = (F1(x[n]) - F1(x[n-1])) / (x[n] - x[n-1])
//
// with a midpoint fallback y[n] = f((x[n]+x[n-1])/2) when the denominator is
// numerically tiny. That is a one-sample-hold-smoothed version of f, which
// buys roughly 20-30 dB of alias suppression over naive waveshaping at the
// same sample rate, at the cost of a mild HF droop and half a sample of
// group delay.
//
// CONSISTENCY RULE (binding, brief section 3.3 / revision note 4)
// ---------------------------------------------------------------
// The divided difference amplifies any mismatch between F1' and f by 1/dx.
// For slow signal regions just above the fallback threshold that error lands
// squarely in the -60..-90 dB range - broadband junk that would silently
// break the alias-floor release gates in tests/AliasingTests.cpp. Therefore
// F1 is NEVER tabulated independently of f (no composite Simpson + separate
// interpolation): for the LUT flavour below, F1 is the *exact piecewise
// analytic antiderivative of the interpolating polynomial itself* - each
// cubic Hermite segment integrates to a quartic segment, with per-knot
// cumulative constants precomputed once so F1 is continuous. F1' == f holds
// identically, to the last bit, everywhere.
namespace tnbr::adaa
{
    // Relative epsilon for the midpoint fallback. Scaled by max(1, |x|) at
    // the call site, per the brief.
    inline constexpr double midpointEpsilon = 1.0e-6;

    //==========================================================================
    // ADAA1 evaluation state: one delayed input and its cached F1 value.
    // Trivially copyable, no allocation, one instance per channel per shaper.
    struct State
    {
        double previousX = 0.0;
        double previousF1 = 0.0;

        void reset (double atX = 0.0) noexcept
        {
            previousX = atX;
            previousF1 = 0.0;
        }
    };

    // Primes `state` at `atX` for a specific shaper.
    //
    // This is NOT optional, and State::reset() alone is not enough for any
    // shaper whose F1 is not zero at the reset point. The LUT flavour below
    // anchors F1 = 0 at the *left edge* of its table, so F1(0) is the whole
    // integral from the left edge to the origin - a number of order 1, not 0.
    // Leaving previousF1 at 0 makes the very first divided difference
    // (F1(x) - 0)/dx, i.e. a spike of order F1(0)/dx, which for a slowly
    // moving signal is enormous. Every reset() path must come through here.
    template <typename Shaper>
    inline void prime (const Shaper& shaper, State& state, double atX = 0.0) noexcept
    {
        state.previousX = atX;
        state.previousF1 = shaper.antiderivative (atX);
    }

    // Runs one ADAA1 step through `shaper`, which must expose
    //   double value (double x) const     -> f(x)
    //   double antiderivative (double x)  -> F1(x), with F1' == f exactly
    template <typename Shaper>
    inline double process (const Shaper& shaper, State& state, double x) noexcept
    {
        const auto x1 = state.previousX;
        const auto delta = x - x1;

        double y;

        if (std::abs (delta) < midpointEpsilon * std::max (1.0, std::abs (x)))
        {
            // Denominator too small to divide by: evaluate f directly at the
            // midpoint. Safe precisely because F1' == f identically (see the
            // consistency rule above), so the two branches agree to within
            // O(delta^2) instead of disagreeing by an interpolation error.
            y = shaper.value (0.5 * (x + x1));
            state.previousF1 = shaper.antiderivative (x);
        }
        else
        {
            const auto f1 = shaper.antiderivative (x);
            y = (f1 - state.previousF1) / delta;
            state.previousF1 = f1;
        }

        state.previousX = x;
        return y;
    }

    //==========================================================================
    // Closed-form ADAA1 flavour for the output-transformer saturator
    // OT(x) = tanh(k*x) / k, whose antiderivative is ln(cosh(k*x)) / k^2
    // (brief section 3.2; research-triode-adaa.md section 2.4).
    //
    // ln(cosh(u)) is evaluated as |u| + log1p(exp(-2|u|)) - ln(2), which is
    // finite for every double instead of overflowing cosh() above u ~ 710.
    struct TanhShaper
    {
        explicit TanhShaper (double kIn = 1.2) noexcept : k (kIn), invK (1.0 / kIn) {}

        double value (double x) const noexcept
        {
            return std::tanh (k * x) * invK;
        }

        double antiderivative (double x) const noexcept
        {
            const auto u = std::abs (k * x);
            const auto lnCosh = u + std::log1p (std::exp (-2.0 * u)) - 0.6931471805599453;
            return lnCosh * invK * invK;
        }

        double k = 1.2;
        double invK = 1.0 / 1.2;
    };

    //==========================================================================
    // LUT flavour: a tabulated shaper stored as C1-continuous cubic Hermite
    // segments over a uniform grid, together with the exact quartic
    // antiderivative of those very segments.
    //
    // Segment i covers [x0 + i*h, x0 + (i+1)*h]; with t = (x - knot_i)/h the
    // interpolant is
    //     S(x)  = a0 + a1*t + a2*t^2 + a3*t^3
    // and therefore, exactly,
    //     F1(x) = base_i + h*(a0*t + a1*t^2/2 + a2*t^3/3 + a3*t^4/4)
    // where base_i is the cumulative F1 at the left knot. Differentiating the
    // second line reproduces the first line identically - which is the whole
    // point (see the consistency rule).
    //
    // Outside the tabulated range the curve is extended as a constant (the
    // triode plate curve is already saturated there), so F1 continues
    // linearly and ADAA1 stays exact rather than merely plausible.
    //
    // Built once in prepare()/the constructor and then read-only: process()
    // never allocates.
    class LutShaper
    {
    public:
        LutShaper() = default;

        // `samples` must hold numPoints values of f evaluated on a uniform
        // grid spanning [minX, maxX] inclusive. numPoints >= 4.
        void build (const std::vector<double>& samples, double minX, double maxX)
        {
            const auto numPoints = samples.size();

            if (numPoints < 4 || ! (maxX > minX))
                return;

            x0 = minX;
            x1 = maxX;
            h = (maxX - minX) / static_cast<double> (numPoints - 1);
            invH = 1.0 / h;

            const auto numSegments = numPoints - 1;
            segments.assign (numSegments, Segment {});

            // Catmull-Rom tangents: m_i = (y_{i+1} - y_{i-1}) / 2, one-sided
            // at the ends. These are what make the interpolant C1, so the
            // quartic antiderivative below is C2 and its derivative is
            // continuous across knots.
            std::vector<double> tangents (numPoints, 0.0);

            for (size_t i = 0; i < numPoints; ++i)
            {
                if (i == 0)
                    tangents[i] = samples[1] - samples[0];
                else if (i + 1 == numPoints)
                    tangents[i] = samples[i] - samples[i - 1];
                else
                    tangents[i] = 0.5 * (samples[i + 1] - samples[i - 1]);
            }

            double cumulative = 0.0;

            for (size_t i = 0; i < numSegments; ++i)
            {
                const auto y0 = samples[i];
                const auto y1v = samples[i + 1];
                const auto m0 = tangents[i];
                const auto m1 = tangents[i + 1];

                auto& segment = segments[i];
                segment.a0 = y0;
                segment.a1 = m0;
                segment.a2 = 3.0 * (y1v - y0) - 2.0 * m0 - m1;
                segment.a3 = 2.0 * (y0 - y1v) + m0 + m1;
                segment.f1Base = cumulative;

                // Exact integral of this segment over its full width.
                cumulative += h * (segment.a0 + segment.a1 * 0.5 + segment.a2 / 3.0 + segment.a3 * 0.25);
            }

            valueAtMin = samples.front();
            valueAtMax = samples.back();
            f1AtMax = cumulative;
        }

        bool isBuilt() const noexcept { return ! segments.empty(); }

        double value (double x) const noexcept
        {
            if (segments.empty())
                return 0.0;

            if (x <= x0)
                return valueAtMin;

            if (x >= x1)
                return valueAtMax;

            size_t index;
            double t;
            locate (x, index, t);

            const auto& s = segments[index];
            return s.a0 + t * (s.a1 + t * (s.a2 + t * s.a3));
        }

        double antiderivative (double x) const noexcept
        {
            if (segments.empty())
                return 0.0;

            // Linear continuation outside the table (constant f there), so
            // F1 stays C1 across the boundary and the ADAA1 divided
            // difference remains the exact average of f over [x1, x].
            if (x <= x0)
                return valueAtMin * (x - x0);

            if (x >= x1)
                return f1AtMax + valueAtMax * (x - x1);

            size_t index;
            double t;
            locate (x, index, t);

            const auto& s = segments[index];
            const auto integral = t * (s.a0 + t * (s.a1 * 0.5 + t * (s.a2 / 3.0 + t * s.a3 * 0.25)));
            return s.f1Base + h * integral;
        }

        double getMinX() const noexcept { return x0; }
        double getMaxX() const noexcept { return x1; }

    private:
        struct Segment
        {
            double a0 = 0.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;
            double f1Base = 0.0;
        };

        void locate (double x, size_t& index, double& t) const noexcept
        {
            const auto position = (x - x0) * invH;
            auto i = static_cast<size_t> (position);
            i = std::min (i, segments.size() - 1);
            index = i;
            t = position - static_cast<double> (i);
        }

        std::vector<Segment> segments;
        double x0 = -1.0, x1 = 1.0, h = 1.0, invH = 1.0;
        double valueAtMin = 0.0, valueAtMax = 0.0, f1AtMax = 0.0;
    };
}
