#include "gui/HubNeedle.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

// HubNeedle's ballistic integration and dB->tick-angle mapping are pure,
// static functions precisely so they're testable without a running
// juce::Timer/message loop (see HubNeedle.h's docs). Unlike the aureate
// pilot (one implicit global tick table), this repo's HubNeedle takes the
// tick table as an explicit parameter - see HubNeedle.h's own "GENERALISATION
// FROM THE AUREATE PILOT" docs for why (the ritual design bakes two dials
// with independently measured tables).
namespace
{
    using Tick = basilica::gui::HubNeedle::Tick;

    // Left-dial table, exactly as measured in PluginEditor.cpp/
    // analysis/measure_dial_ticks.py - duplicated here (not #included) so
    // this test exercises the same literal numbers the shipped GUI does,
    // the same convention aureate's own HubNeedleTests.cpp uses.
    const std::vector<Tick> testTicks {
        { -20.0f, -45.9f },
        { -10.0f, -30.1f },
        { -7.0f, -18.8f },
        { -5.0f, -8.2f },
        { -3.0f, -1.2f },
        { 0.0f, 13.8f },
        { 1.0f, 18.0f },
        { 2.0f, 27.0f },
        { 3.0f, 36.0f },
    };
}

TEST_CASE ("HubNeedle::stepBallistics step response", "[gui]")
{
    using basilica::gui::HubNeedle;

    SECTION ("non-positive dt or tau snaps straight to target (defensive floor, never divides by zero)")
    {
        CHECK (HubNeedle::stepBallistics (-20.0f, 0.0f, 0.0f, 0.25f) == Catch::Approx (0.0f));
        CHECK (HubNeedle::stepBallistics (-20.0f, 0.0f, 1.0f / 30.0f, 0.0f) == Catch::Approx (0.0f));
    }

    SECTION ("repeated stepping monotonically approaches target without overshoot")
    {
        constexpr float tau = HubNeedle::ballisticsTauSeconds;
        constexpr float dt = 1.0f / 30.0f;
        constexpr float target = 3.0f;

        auto smoothed = -20.0f;
        auto previous = smoothed;

        for (int i = 0; i < 300; ++i)
        {
            smoothed = HubNeedle::stepBallistics (smoothed, target, dt, tau);
            CHECK (smoothed >= previous);
            CHECK (smoothed <= target);
            previous = smoothed;
        }

        CHECK (smoothed == Catch::Approx (target).margin (0.01f));
    }
}

TEST_CASE ("HubNeedle::tickAngleDegreesForDb interpolates a supplied tick table", "[gui]")
{
    using basilica::gui::HubNeedle;

    // Exact table points - see analysis/measure_dial_ticks.py and
    // PluginEditor.cpp's vuLeftTicks docs for provenance.
    CHECK (HubNeedle::tickAngleDegreesForDb (-20.0f, testTicks) == Catch::Approx (-45.9f));
    CHECK (HubNeedle::tickAngleDegreesForDb (0.0f, testTicks) == Catch::Approx (13.8f));
    CHECK (HubNeedle::tickAngleDegreesForDb (3.0f, testTicks) == Catch::Approx (36.0f));

    SECTION ("midpoint between two adjacent ticks interpolates linearly")
    {
        // -10 -> -30.1deg, -7 -> -18.8deg; -8.5 is exactly halfway.
        CHECK (HubNeedle::tickAngleDegreesForDb (-8.5f, testTicks) == Catch::Approx (-24.45f).margin (0.01f));
    }

    SECTION ("values beyond the table clamp to the nearest end, never extrapolate")
    {
        CHECK (HubNeedle::tickAngleDegreesForDb (-60.0f, testTicks) == Catch::Approx (-45.9f));
        CHECK (HubNeedle::tickAngleDegreesForDb (12.0f, testTicks) == Catch::Approx (36.0f));
    }

    SECTION ("angles increase monotonically across the whole table (no crossed/reversed ticks)")
    {
        constexpr std::array<float, 9> dbs { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, 0.0f, 1.0f, 2.0f, 3.0f };
        for (size_t i = 1; i < dbs.size(); ++i)
            CHECK (HubNeedle::tickAngleDegreesForDb (dbs[i], testTicks) > HubNeedle::tickAngleDegreesForDb (dbs[i - 1], testTicks));
    }
}

TEST_CASE ("HubNeedle exposes a read-only, unit-suffixed accessible value", "[gui][a11y]")
{
    basilica::gui::HubNeedle::Assets assets; // deliberately default/invalid image - fine, this test never calls paint()
    basilica::gui::HubNeedle needle (assets, "Input Level meter", 0.5f, 0.5f, 0.65f, 1.968f, testTicks);

    // createAccessibilityHandler() directly (not getAccessibilityHandler()) -
    // the latter only returns non-null once the component has a live native
    // window peer, which this headless, no-message-loop test binary never
    // has.
    const auto handler = needle.createAccessibilityHandler();
    REQUIRE (handler != nullptr);

    auto* valueInterface = handler->getValueInterface();
    REQUIRE (valueInterface != nullptr);

    CHECK (valueInterface->isReadOnly());

    const auto valueText = valueInterface->getCurrentValueAsString();
    INFO ("HubNeedle accessible value = \"" << valueText.toStdString() << "\"");
    CHECK (valueText.endsWith ("dB"));
}

TEST_CASE ("HubNeedle::setImmediateDbForPreview seeds both target and smoothed reading immediately", "[gui]")
{
    basilica::gui::HubNeedle::Assets assets;
    basilica::gui::HubNeedle needle (assets, "Input Level meter", 0.5f, 0.5f, 0.65f, 1.968f, testTicks);

    needle.setImmediateDbForPreview (-7.0f);

    const auto handler = needle.createAccessibilityHandler();
    REQUIRE (handler != nullptr);
    auto* valueInterface = handler->getValueInterface();
    REQUIRE (valueInterface != nullptr);

    CHECK (valueInterface->getCurrentValueAsString().getFloatValue() == Catch::Approx (-7.0f));
}
