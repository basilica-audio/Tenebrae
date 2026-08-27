#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

// Tenebrae's own @1x faceplate/control-bay geometry table for the M3
// photoreal "ritual" GUI - lives in its own header, rather than as an
// anonymous-namespace block inside PluginEditor.cpp, so
// tests/gui/EditorLayoutTests.cpp can assert layout invariants directly
// against the SAME numbers PluginEditor.cpp actually lays components out
// with (basilica-audio/aureate's own PluginEditorLayout.h convention,
// copied here - aureate is the M3 photoreal GUI pilot for this suite wave).
//
// Every constant below is derived from the ritual design's own measured
// provenance (repo-relative to the suite root, one level above this repo):
//   - brand/mocks/ritual/layout-manifest.json - plate/meter/knob geometry,
//     measured against master-01-base.png (this build's own shipped
//     background - unlike aureate's tubecomp design, ritual's manifest and
//     shipped master are the SAME render generation, no cross-generation
//     provenance assumption needed here).
//   - brand/mocks/ritual/components/needle-left.json / needle-right.json -
//     the two needles' own hub pivots, sprite canvas size, and baked rest
//     angle.
//   - analysis/measure_dial_ticks.py - the two dials' own measured
//     dB->angle tick tables (see docs/gui-mapping.md for the full table and
//     measurement method).
namespace tnbr::layout
{
    // Master render's own canvas size (layout-manifest.json's own
    // "imageSize.w/h") - the scale factor below is
    // plateWidth1x / masterCanvasWidthPx.
    constexpr int masterCanvasWidthPx = 1376;
    constexpr int masterCanvasHeightPx = 768;

    constexpr int plateWidth1x = 900;
    constexpr int plateHeight1x = 502; // masterCanvasHeightPx scaled by the same factor as plateWidth1x

    // plateWidth1x / masterCanvasWidthPx = 900/1376, computed once here as a
    // constexpr rather than re-derived at each call site.
    constexpr float masterToPlateScale = (float) plateWidth1x / (float) masterCanvasWidthPx;

    //==========================================================================
    // VU needles: TWO independent HubNeedle instances (left = input level,
    // right = output level - see docs/gui-mapping.md), each centred squarely
    // on its own measured hub pivot (components/needle-{left,right}.json's
    // own pivotXInMaster/pivotYInMaster - NOT the dial's own optical centre,
    // which sits well above the pivot on this design, same "pivot below the
    // visible arc" convention as a real VU meter movement).
    //
    // componentMasterPx (430 master px) converts to 281 @1x. UNLIKE
    // aureate's tubecomp pilot, this cannot use a generously-oversized
    // component: the two dials' pivots sit only 296.5 @1x apart (926.84 -
    // 473.54 = 453.3 master px), so a too-large component would make the
    // two meter bays overlap (caught by tests/gui/EditorLayoutTests.cpp).
    // 430 master px is the smallest size that still keeps the needle
    // sprite's own visible tip (up to 214px from the pivot, within the
    // sprite's own 462x462 canvas - see needle-{left,right}.json's
    // spriteSize/tipYInMaster) from clipping against the component's own
    // bounds at any rotation angle (safe up to spriteSizeFraction <=
    // ~1.08 - see this repo's own layout-derivation notes; 430 gives
    // 462/430 = 1.074), while leaving a real (if modest, ~15 @1x px)
    // clearance margin between the two meter bays.
    //
    // juce::Point/Rectangle's constructors are not constexpr in JUCE 8.0.14
    // (verified against basilica-audio/silentium's own PluginEditorLayout.h
    // docs, and aureate's own copy of this same note) - plain `const`
    // namespace-scope values instead, still initialised exactly once, well
    // before any constructor runs.
    const juce::Point<float> needlePivotLeftMasterPx { 473.54f, 384.0f };
    const juce::Point<float> needlePivotRightMasterPx { 926.84f, 384.0f };

    constexpr int meterComponentSize1x = 281;
    const juce::Point<int> meterLeftTopLeft1x { 169, 111 };  // needlePivotLeftMasterPx*scale - meterComponentSize1x/2
    const juce::Point<int> meterRightTopLeft1x { 466, 111 }; // needlePivotRightMasterPx*scale - meterComponentSize1x/2

    constexpr float needleSpritePivotFraction = 0.5f; // needle-{left,right}.json: spriteIsPivotCentred - pivot at the sprite's own canvas centre (the pivotXInSprite/YInSprite pixel fields are stale pre-"+30.1px move" bookkeeping, not authoritative - see the json's own pivotNote)
    constexpr float needleSpriteSizeFraction = 462.0f / (float) 430; // spriteSize(462 master px) / componentMasterPx(430) - scale cancels, see PluginEditorLayout derivation notes
    constexpr float needleLeftBakedAngleDeg = 1.968f;   // needle-left.json bakedAngleDeg
    constexpr float needleRightBakedAngleDeg = 1.172f;  // needle-right.json bakedAngleDeg

    //==========================================================================
    // Dial backlight breathing zone (DialBreathing.h): centred on each
    // dial's own OPTICAL centre (layout-manifest.json's "meters" cx/cy/r -
    // distinct from the needle's hub pivot above), radius taken directly
    // from the manifest's own measured meter radius.
    const juce::Point<float> dialLeftCentreMasterPx { 463.4f, 306.2f };
    const juce::Point<float> dialRightCentreMasterPx { 907.5f, 303.4f };
    constexpr float dialLeftRadiusMasterPx = 115.0f;
    constexpr float dialRightRadiusMasterPx = 120.8f;
    // 0.62 (not a larger, more face-filling fraction): the darkened crop's
    // own SQUARE canvas reaches its own CORNERS at r*sqrt(2), well beyond
    // the circular content radius itself - and this design's dial face
    // carries a thin (2-3px), very high-contrast bezel/highlight ring at
    // roughly r=116 master px (measured directly against
    // resources/gui/master_ritual.png). A larger content fraction let the
    // crop canvas's own corners reach into that ring, where two
    // independently-resampled g.drawImage() calls (the plate's own direct
    // draw vs. the crop's own nearest-neighbour-sampled-then-stretched
    // draw - see DialBreathing::buildDarkenedCrop()'s per-pixel sampling)
    // can legitimately land a fraction of a pixel apart, which showed up as
    // a large, if narrow and practically invisible, false "brighter than
    // ceiling" reading (tests/gui/EditorSnapshotTests.cpp's regression).
    // 0.62 keeps the crop canvas's own worst-case (diagonal corner) reach
    // safely inside r=116 with margin, eliminating the whole class of
    // artifact rather than tolerating it.
    constexpr float dialBreathingContentFraction = 0.62f;
    constexpr float dialBreathingMaxDarkenFraction = 0.04f; // brief's "+/-3-4%" breathing amplitude, darkening-only

    //==========================================================================
    // Rune knob grid: 4 knobs, one shared row (unlike aureate's two-row
    // tubecomp grid). layout-manifest.json's own "knobs" array gives each
    // knob's true (cx, cy, r) in master px - used for MasterCropKnob's own
    // feathered-crop source (crop geometry, never for hit-testing - see
    // knobMasterGeometry1x in PluginEditor.cpp).
    //
    // The manifest's own "layoutInvariantViolations" note flags that these
    // 4 knobs' raw measured centres do NOT share a single row Y within the
    // suite's 2px invariant (up to 16.75px deviation, confirmed "real
    // geometry, not a measurement artifact" by its own 2026-07-27 QA pass) -
    // exactly the same situation aureate's own lower knob row hit, and
    // resolved the same way there: the INTERACTIVE slider hit-area is
    // row-snapped to the manifest's own "rowYs" mean_cy (561.25 master px),
    // while the crop artwork itself still samples each knob's own true
    // measured centre (so the rune cap's crop is pixel-accurate even though
    // the row-Y invariant is a structural simplification of the interactive
    // hit-area, not a claim about the baked art's own geometry).
    constexpr int knobRowY1x = 367; // rowYs mean_cy (561.25 master px) * scale, rounded
    constexpr int knobDiameter1x = 79; // mean measured knob radius (60.25 master px) * 2 * scale, rounded

    //==========================================================================
    // Typography pass (suite typo phase, owner decision 2026-07-26: text is
    // never baked into the AI master - lettering is set locally as a sharp
    // JUCE text layer, see src/gui/PlateTypography.h and
    // docs/gui-mapping.md's typography section). The ritual design's four
    // rune knobs carry baked sigils, not function names - one gilded label
    // per knob names its function, set on the plate's own bottom ledge
    // (the smooth horizontal band under the knob bays, master y ~662..688
    // - the only clean lettering surface this heavily-sculpted plate
    // offers; everything between the dials and knobs is thorn/vine
    // relief). Gilded gold rather than dark engraving ink: the ledge is
    // dark aged bronze (luminance ~40..80), where an incision-ink read
    // vanishes - aged-gold lettering matches the brand's antique-gold-on-
    // charcoal system.
    constexpr int knobLabelCy1x = 441; // ledge band centre (675 master px * scale)
    constexpr int knobLabelWidth1x = 96;
    constexpr int knobLabelHeight1x = 14;

    constexpr std::array<int, 4> knobCx1x { 222, 378, 528, 690 };

    constexpr int baseEditorWidth = plateWidth1x;
    constexpr int topStripHeight1x = 32;
    constexpr int topStripGap1x = 6;
    constexpr int scaleButtonWidth1x = 64;
    constexpr int baseEditorHeight = topStripHeight1x + topStripGap1x + plateHeight1x;

    constexpr std::array<float, 3> scaleSteps { 1.0f, 1.5f, 2.0f };
}
