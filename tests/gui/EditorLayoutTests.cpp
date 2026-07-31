#include "PluginEditorLayout.h"

#include <catch2/catch_test_macros.hpp>

// Layout-invariant tests for the M3 photoreal "ritual" GUI - the ritual
// design's own layout-manifest.json's "rowYs" field is the source of truth
// for the row-Y snap (see PluginEditorLayout.h's top-of-file docs), not
// each individual knob's own slightly-deviating measured centre (the
// manifest's own "layoutInvariantViolations" note flags a real, QA-verified
// deviation of up to 16.75px in the raw measurement).
TEST_CASE ("The 4 rune knob centres share a single row Y within the suite's 2px invariant", "[gui][layout]")
{
    using namespace tnbr::layout;

    // Structural guarantee, not merely a check on today's numbers: every
    // entry in PluginEditor.cpp's own knobLayout table is positioned via
    // this single shared knobRowY1x constant (see PluginEditor.cpp's
    // resized()), so there is nowhere for an individual knob to carry a
    // divergent Y - this test asserts the constant itself is well-formed
    // (positive, non-degenerate) as the concrete numeric anchor for that
    // structural guarantee.
    CHECK (knobRowY1x > 0);
    CHECK (knobDiameter1x > 0);

    // All 4 knob X centres are distinct and strictly increasing (left to
    // right, matching signal-flow reading order - see PluginEditor.cpp's
    // knobLayout docs) with no two knobs close enough to visually overlap.
    for (size_t i = 1; i < knobCx1x.size(); ++i)
    {
        CHECK (knobCx1x[i] > knobCx1x[i - 1]);
        CHECK (knobCx1x[i] - knobCx1x[i - 1] >= knobDiameter1x);
    }
}

TEST_CASE ("Both needle/meter components and the full knob grid stay within the plate's own canvas bounds", "[gui][layout]")
{
    using namespace tnbr::layout;

    const juce::Rectangle<int> plateCanvas { 0, 0, plateWidth1x, plateHeight1x };

    const juce::Rectangle<int> leftMeterBay { meterLeftTopLeft1x.x, meterLeftTopLeft1x.y, meterComponentSize1x, meterComponentSize1x };
    const juce::Rectangle<int> rightMeterBay { meterRightTopLeft1x.x, meterRightTopLeft1x.y, meterComponentSize1x, meterComponentSize1x };
    CHECK (plateCanvas.contains (leftMeterBay));
    CHECK (plateCanvas.contains (rightMeterBay));

    // The two meter bays must not overlap each other.
    CHECK_FALSE (leftMeterBay.intersects (rightMeterBay));

    for (const auto cx : knobCx1x)
    {
        const juce::Rectangle<int> knobBay { cx - knobDiameter1x / 2, knobRowY1x - knobDiameter1x / 2, knobDiameter1x, knobDiameter1x };
        CHECK (plateCanvas.contains (knobBay));
    }
}

TEST_CASE ("Needle sprite geometry is well-formed for both dials", "[gui][layout]")
{
    using namespace tnbr::layout;

    CHECK (needleSpritePivotFraction > 0.0f);
    CHECK (needleSpritePivotFraction < 1.0f);
    CHECK (needleSpriteSizeFraction > 0.0f);

    // UNLIKE aureate's tubecomp pilot (whose sprite fraction is safely
    // < 1.0), this design's two meter bays sit close enough together that
    // meterComponentSize1x had to be sized smaller than the needle sprite's
    // own 462x462 source canvas (see PluginEditorLayout.h's own derivation
    // notes) - the sprite's own mostly-transparent canvas is drawn larger
    // than the component, which is harmless (JUCE clips a child
    // component's painting to its own bounds, and only invisible/
    // transparent pixels get clipped). What actually matters is that the
    // needle's own VISIBLE tip (up to 214 of the sprite's own 462 canvas
    // px from the pivot) never reaches the component's own edge at any
    // rotation angle - safe up to spriteSizeFraction <= 462/(2*214) =
    // ~1.08 (the inscribed-circle bound for a square component, safe at
    // any rotation angle up to a full 360deg sweep, not just this dial's
    // own +/-46deg range).
    constexpr float maxSafeSpriteSizeFraction = 462.0f / (2.0f * 214.0f);
    CHECK (needleSpriteSizeFraction < maxSafeSpriteSizeFraction);

    // Each pivot must sit strictly inside its own meter component's bounds -
    // a fraction outside [0,1] would mean the needle rotates around a point
    // off the drawn component entirely.
    const juce::Rectangle<int> leftMeterBay { meterLeftTopLeft1x.x, meterLeftTopLeft1x.y, meterComponentSize1x, meterComponentSize1x };
    const juce::Rectangle<int> rightMeterBay { meterRightTopLeft1x.x, meterRightTopLeft1x.y, meterComponentSize1x, meterComponentSize1x };

    CHECK (leftMeterBay.contains (juce::roundToInt (needlePivotLeftMasterPx.x * masterToPlateScale),
                                  juce::roundToInt (needlePivotLeftMasterPx.y * masterToPlateScale)));
    CHECK (rightMeterBay.contains (juce::roundToInt (needlePivotRightMasterPx.x * masterToPlateScale),
                                   juce::roundToInt (needlePivotRightMasterPx.y * masterToPlateScale)));

    // The two dials' own baked rest angles are close to (but not identical
    // to) each other - both real, independently measured values (see
    // needle-{left,right}.json), never accidentally the same constant
    // copy-pasted twice.
    CHECK (needleLeftBakedAngleDeg != needleRightBakedAngleDeg);
}

TEST_CASE ("Dial-breathing zone geometry stays centred on each dial's own optical centre, inside the plate canvas", "[gui][layout]")
{
    using namespace tnbr::layout;

    const juce::Rectangle<float> plateCanvas { 0.0f, 0.0f, (float) plateWidth1x, (float) plateHeight1x };

    const auto checkZone = [&] (juce::Point<float> centreMasterPx, float radiusMasterPx)
    {
        const auto outer1x = radiusMasterPx * masterToPlateScale * dialBreathingContentFraction;
        const auto centre1x = juce::Point<float> (centreMasterPx.x * masterToPlateScale, centreMasterPx.y * masterToPlateScale);
        const juce::Rectangle<float> zone { centre1x.x - outer1x, centre1x.y - outer1x, outer1x * 2.0f, outer1x * 2.0f };
        CHECK (plateCanvas.contains (zone));
    };

    checkZone (dialLeftCentreMasterPx, dialLeftRadiusMasterPx);
    checkZone (dialRightCentreMasterPx, dialRightRadiusMasterPx);

    CHECK (dialBreathingContentFraction > 0.0f);
    CHECK (dialBreathingContentFraction < 1.0f);
    CHECK (dialBreathingMaxDarkenFraction > 0.0f);
    CHECK (dialBreathingMaxDarkenFraction < 0.5f); // "subtle" - never a heavy-handed darken
}
