#include "PluginEditor.h"
#include "PluginEditorLayout.h"
#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "presets/Localisation.h"

#include <BinaryData.h>

#include <cmath>
#include <utility>

namespace
{
    // Base (@1x, 100% scale) faceplate geometry lives in PluginEditorLayout.h
    // (tnbr::layout) rather than here, so tests/gui/EditorLayoutTests.cpp can
    // assert layout invariants against the exact constants this file lays
    // components out with.
    using namespace tnbr::layout;

    //==========================================================================
    // VU dial tick tables (dB label -> needle rotation in degrees, clockwise
    // from straight up) - measured directly against this repo's own shipped
    // master (resources/gui/master_ritual.png, sourced from
    // brand/mocks/ritual/master-01-base.png) by analysis/measure_dial_ticks.py.
    // NOT copied from any sibling design's table (aureate's tubecomp dial has
    // a visibly different tick layout and is not interchangeable, and the
    // ritual design's own two dials measure to slightly different angles from
    // each other too - see that script's own docstring for the measurement
    // method and this file's citation of its output). Re-run that script if
    // this repo's master render is ever replaced.
    const std::vector<basilica::gui::HubNeedle::Tick> vuLeftTicks {
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

    const std::vector<basilica::gui::HubNeedle::Tick> vuRightTicks {
        { -20.0f, -43.6f },
        { -10.0f, -31.7f },
        { -7.0f, -21.5f },
        { -5.0f, -8.2f },
        { -3.0f, 1.6f },
        { 0.0f, 14.1f },
        { 1.0f, 18.4f },
        { 2.0f, 26.8f },
        { 3.0f, 36.0f },
    };

    // Suite Standard-A convention (see docs/gui-mapping.md): 0 VU on the
    // dial's own printed scale corresponds to -18 dBFS at the processor's
    // own metering point, so a comfortable mixing level (well below 0 dBFS)
    // reads near the dial's own "0" tick rather than pinned at the bottom.
    constexpr float vuZeroReferenceDbfs = -18.0f;

    float dbfsToVuDb (float dbfs) noexcept
    {
        return dbfs - vuZeroReferenceDbfs;
    }

    struct KnobLayoutEntry
    {
        const char* parameterId;
        const char* labelText; // accessible name only - no baked text labels
        float cxMaster, cyMaster, rMaster; // true measured knob geometry (crop source) - layout-manifest.json "knobs" array
        int cx1x;                          // interactive slider hit-area X centre (Y comes from the shared knobRowY1x)
        const char* engravedLabel;         // the ledge's own gilded caps (typography pass)
    };

    // Mapping decided for the M3 GUI pilot (docs/gui-mapping.md has the full
    // rationale table): the ritual design bakes only 4 physical knob
    // positions (unlike aureate's 10), so Tenebrae's 26 APVTS parameters had
    // to be triaged down to the 4 most musically primary ones. Gain + the
    // 3-band tone stack (Bass/Mid/Treble) is the single most universally
    // recognisable "4-knob high-gain channel" layout in the whole reference
    // class - reached for on every use, left to right in signal-flow order.
    // Master px geometry from brand/mocks/ritual/layout-manifest.json's own
    // "knobs" array (index 1-4, reading order left to right).
    constexpr std::array<KnobLayoutEntry, 4> knobLayout {
        KnobLayoutEntry { ParamIDs::gain, "Gain", 339.0f, 558.0f, 63.0f, knobCx1x[0], "GAIN" },
        KnobLayoutEntry { ParamIDs::bass, "Bass", 578.0f, 560.0f, 58.0f, knobCx1x[1], "BASS" },
        KnobLayoutEntry { ParamIDs::mid, "Mid", 808.0f, 549.0f, 62.0f, knobCx1x[2], "MID" },
        KnobLayoutEntry { ParamIDs::treble, "Treble", 1055.0f, 578.0f, 58.0f, knobCx1x[3], "TREBLE" },
    };

    // ==================== typography pass ====================
    // Gilded lettering on the plate's bottom ledge (see
    // PluginEditorLayout.h's typography block): aged gold, slightly
    // subdued against apotheosis's brighter leaf - this design's bronze is
    // colder and more weathered - with a dark drop shadow one scaled pixel
    // below (EngravedTextStyle's offset pass doubles as the shadow).
    const basilica::gui::EngravedTextStyle gildedKnobLabelStyle {
        juce::Colour (0xe6c9a05a), juce::Colour (0x8c000000), 13.0f, 0.14f, true
    };

    // Dial-breathing ballistics (SubtractiveGlow.h's stepGlowMix(), reused
    // unmodified - see DialBreathing.h's docs): idle breathing wanders across
    // most of the [0,1] range (idleCentre/idleHalfRange below), giving a
    // visible +/-3-4% brightness swing even at total silence (the suite's
    // "idle flicker must never read as off" rule); the signal-driven term
    // then PUSHES TOWARD THE CEILING as level rises (t->1, i.e. less
    // darkening) - the same "the section is alive/working" convention
    // aureate's own vent-glow breathing uses for its gain-reduction reading,
    // reapplied here to a level reading: the dial reads brighter/livelier
    // while signal is actually flowing through it, and settles back to its
    // idle wander at silence. See docs/gui-mapping.md for this deliberate
    // direction choice.
    constexpr float dialBreathingTauSeconds = 0.20f;
    constexpr float dialBreathingFloorDb = -60.0f;
    constexpr float dialBreathingCeilingDb = 0.0f;
    constexpr float dialBreathingIdleCentre = 0.55f;
    constexpr float dialBreathingIdleHalfRange = 0.45f;
    constexpr float dialBreathingPhaseSeedLeft = 1.0f;
    constexpr float dialBreathingPhaseSeedRight = 4.0f;

    juce::Image loadImage (const char* data, int size)
    {
        return juce::ImageCache::getFromMemory (data, size);
    }

    // M2 i18n frame: selects German (resources/i18n/de.txt) or falls through
    // to English, once, at editor construction - see Localisation.h's docs.
    basilica::presets::PresetManager& initLocalisationThenGetPresetManager (TenebraeAudioProcessor& processor)
    {
        basilica::presets::installLocalisation (BinaryData::de_txt, BinaryData::de_txtSize);
        return processor.presetManager;
    }

    // Non-parameter, per-session UI state: the stepped scale choice (0/1/2)
    // stored as a plain property directly on apvts.state.
    constexpr const char* uiScaleStepProperty = "uiScaleStep";
}

TenebraeAudioProcessorEditor::TenebraeAudioProcessorEditor (TenebraeAudioProcessor& processorToEdit)
    : juce::AudioProcessorEditor (&processorToEdit),
      audioProcessor (processorToEdit),
      presetBar (initLocalisationThenGetPresetManager (processorToEdit)),
      typography (BinaryData::EBGaramondRegular_ttf, BinaryData::EBGaramondRegular_ttfSize,
                  BinaryData::EBGaramondSemiBold_ttf, BinaryData::EBGaramondSemiBold_ttfSize)
{
    masterImage = loadImage (BinaryData::master_ritual_png, BinaryData::master_ritual_pngSize);

    // Creation order doubles as the accessibility/keyboard focus order
    // (JUCE's default FocusTraverser walks children in z-order, i.e.
    // creation order) - kept matching visual reading order: preset bar +
    // scale control, the two VU needles (left to right), then the 4 rune
    // knobs left to right.
    addAndMakeVisible (presetBar);

    scaleButton.setComponentID ("scaleButton");
    scaleButton.onClick = [this] { cycleScale(); };
    addAndMakeVisible (scaleButton);

    basilica::gui::HubNeedle::Assets leftNeedleAssets;
    leftNeedleAssets.needleSprite = loadImage (BinaryData::needle_left_ritual_png, BinaryData::needle_left_ritual_pngSize);
    inputNeedle = std::make_unique<basilica::gui::HubNeedle> (
        leftNeedleAssets, "Input Level meter",
        needleSpritePivotFraction, needleSpritePivotFraction,
        needleSpriteSizeFraction, needleLeftBakedAngleDeg, vuLeftTicks);
    addAndMakeVisible (*inputNeedle);

    basilica::gui::HubNeedle::Assets rightNeedleAssets;
    rightNeedleAssets.needleSprite = loadImage (BinaryData::needle_right_ritual_png, BinaryData::needle_right_ritual_pngSize);
    outputNeedle = std::make_unique<basilica::gui::HubNeedle> (
        rightNeedleAssets, "Output Level meter",
        needleSpritePivotFraction, needleSpritePivotFraction,
        needleSpriteSizeFraction, needleRightBakedAngleDeg, vuRightTicks);
    addAndMakeVisible (*outputNeedle);

    for (size_t i = 0; i < knobLayout.size(); ++i)
    {
        auto& entry = knobLayout[i];
        knobs[i].slider = std::make_unique<basilica::gui::MasterCropKnob> (
            masterImage, juce::Point<float> (entry.cxMaster, entry.cyMaster), entry.rMaster);
        configureKnob (knobs[i], entry.parameterId, entry.labelText);
    }

    dialBreathingLeft = basilica::gui::DialBreathing (
        masterImage, dialLeftCentreMasterPx, dialLeftRadiusMasterPx,
        dialBreathingMaxDarkenFraction, dialBreathingContentFraction);
    dialBreathingRight = basilica::gui::DialBreathing (
        masterImage, dialRightCentreMasterPx, dialRightRadiusMasterPx,
        dialBreathingMaxDarkenFraction, dialBreathingContentFraction);

    const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    dialBreathingStateLeft.startTimeSeconds = now;
    dialBreathingStateRight.startTimeSeconds = now;
    dialBreathingMixLeft = dialBreathingIdleCentre;
    dialBreathingMixRight = dialBreathingIdleCentre;

    setResizable (false, false);

    const auto storedStep = (int) audioProcessor.apvts.state.getProperty (uiScaleStepProperty, 0);
    applyScaleStep (juce::jlimit (0, (int) scaleSteps.size() - 1, storedStep));

    startTimerHz (30);
}

TenebraeAudioProcessorEditor::~TenebraeAudioProcessorEditor() = default;

void TenebraeAudioProcessorEditor::configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText)
{
    knob.slider->setPopupDisplayEnabled (true, true, this);
    knob.slider->setTitle (labelText);
    knob.slider->setName (labelText);
    addAndMakeVisible (*knob.slider);

    if (auto* param = audioProcessor.apvts.getParameter (parameterId))
    {
        const auto defaultValue = param->getNormalisableRange().convertFrom0to1 (param->getDefaultValue());
        knob.slider->setDoubleClickReturnValue (true, defaultValue);
    }

    // SliderAttachment MUST be constructed before the textFromValueFunction
    // override below - JUCE 8.0.14's SliderParameterAttachment constructor
    // itself assigns slider.textFromValueFunction as part of wiring the
    // attachment, which would silently clobber an override set beforehand.
    knob.attachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, parameterId, *knob.slider);

    if (auto* param = audioProcessor.apvts.getParameter (parameterId))
    {
        knob.slider->textFromValueFunction = [param] (double v)
        {
            return param->getText (param->convertTo0to1 ((float) v), 0) + " " + param->getLabel();
        };
        knob.slider->updateText();
    }
}

void TenebraeAudioProcessorEditor::cycleScale()
{
    applyScaleStep ((scaleStepIndex + 1) % (int) scaleSteps.size());
}

void TenebraeAudioProcessorEditor::applyScaleStep (int newStepIndex)
{
    scaleStepIndex = juce::jlimit (0, (int) scaleSteps.size() - 1, newStepIndex);
    audioProcessor.apvts.state.setProperty (uiScaleStepProperty, scaleStepIndex, nullptr);

    const auto percentText = juce::String ((int) (scaleSteps[(size_t) scaleStepIndex] * 100.0f)) + "%";
    scaleButton.setButtonText (percentText);
    scaleButton.setTitle ("Window scale, " + percentText);

    const auto scale = scaleSteps[(size_t) scaleStepIndex];

    setSize ((int) std::lround ((float) baseEditorWidth * scale),
             (int) std::lround ((float) baseEditorHeight * scale));
}

void TenebraeAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    const auto scale = scaleSteps[(size_t) scaleStepIndex];
    const auto s = [scale] (float v) { return v * scale; };

    const auto stripHeight = (float) topStripHeight1x * scale;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff17141a), 0.0f, 0.0f,
                                             juce::Colour (0xff0b090d), 0.0f, stripHeight, false));
    g.fillRect (juce::Rectangle<float> (0.0f, 0.0f, (float) getWidth(), stripHeight));
    g.setColour (juce::Colour (0xff3a4a3f));
    g.fillRect (juce::Rectangle<float> (0.0f, stripHeight - 1.0f * scale, (float) getWidth(), 1.0f * scale));

    const auto plateOrigin = juce::Point<float> (0.0f, stripHeight + (float) topStripGap1x * scale);
    const auto plateBounds = juce::Rectangle<float> (plateOrigin.x, plateOrigin.y,
                                                      (float) plateWidth1x * scale, (float) plateHeight1x * scale);

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

    // 1. Baseline plate: the single master render, filling the plate bounds.
    // Bakes the bronze plate, gargoyles, both VU dials (empty, no needles),
    // and all 4 rune knobs at 12 o'clock - nothing else is drawn for any of
    // those elements. stretchToFit (not centred/"contain") deliberately:
    // plateWidth1x/plateHeight1x's own rounding means their aspect ratio is
    // not bit-exactly the master's own (1376/768 vs 900/502), and "contain"
    // placement would otherwise letterbox by a sub-pixel amount and pick an
    // effective scale that silently disagrees with masterToPlateScale (the
    // single scale factor every other overlay in this file - the dial-
    // breathing zones, the knob/needle positions - is computed against),
    // which showed up as a real, if small, misregistration between the
    // plate's own rendered content and the breathing overlay's destRect at
    // high-contrast edges (tick marks). stretchToFit fills plateBounds
    // exactly, matching masterToPlateScale on both axes to within rounding.
    if (masterImage.isValid())
        g.drawImage (masterImage, plateBounds, juce::RectanglePlacement::stretchToFit, false);

    // 2. Dial-backlight breathing (DialBreathing, see its own docs) - drawn
    // directly on top of the baseline plate, before the knobs/needles (which
    // are separate child components painted after this method returns, and
    // which physically sit at a different plate location so paint order
    // relative to them is irrelevant here).
    const auto toScreenPoint = [&] (juce::Point<float> local1x)
    {
        return juce::Point<float> (plateOrigin.x + s (local1x.x), plateOrigin.y + s (local1x.y));
    };

    const auto drawDialBreathing = [&] (const basilica::gui::DialBreathing& breathing, juce::Point<float> centreMasterPx,
                                        float radiusMasterPx, float mix)
    {
        const auto centre1x = juce::Point<float> (centreMasterPx.x * masterToPlateScale, centreMasterPx.y * masterToPlateScale);

        // Mirrors DialBreathing::buildDarkenedCrop()'s own canvas-size
        // formula EXACTLY (outerRadius -> ceil(outerRadius*2)+2, in MASTER
        // px, only converted to @1x at the very end) rather than an
        // independently-rounded approximation - the two must agree to
        // sub-pixel precision, or the crop's own destination rectangle
        // registers a fraction of a pixel off from the plate's own
        // stretchToFit mapping of the same master-space region, which is
        // visible as a hairline mis-registration exactly at this design's
        // thin, high-contrast bezel-to-dial-face highlight ring (caught by
        // tests/gui/EditorSnapshotTests.cpp's ceiling regression).
        const auto outerRadiusMasterPx = radiusMasterPx * dialBreathingContentFraction;
        const auto canvasSizeMasterPx = (float) juce::jmax (2, (int) std::ceil (outerRadiusMasterPx * 2.0f) + 2);
        const auto canvasSize1x = canvasSizeMasterPx * masterToPlateScale;

        const auto screenCentre = toScreenPoint (centre1x);
        const auto destRect = juce::Rectangle<float> (s (canvasSize1x), s (canvasSize1x)).withCentre (screenCentre);

        breathing.drawZone (g, destRect, mix);
    };

    drawDialBreathing (dialBreathingLeft, dialLeftCentreMasterPx, dialLeftRadiusMasterPx, dialBreathingMixLeft);
    drawDialBreathing (dialBreathingRight, dialRightCentreMasterPx, dialRightRadiusMasterPx, dialBreathingMixRight);

    // 3. Typography layer (suite typo phase - PlateTypography.h): one
    // gilded function label per rune knob, on the plate's bottom ledge.
    // Drawn last within paint() so the dial-breathing blit can never
    // cover it.
    drawPlateTypography (g, plateOrigin, scale);

    // (4. The 4 rune knobs are separate MasterCropKnob child components,
    // drawn automatically after this method returns - see resized() for
    // their bounds. 5. The two VU needles are separate HubNeedle child
    // components, same story - everything else - the bronze plate texture,
    // the gargoyle/thorn relief, the dial faces' own tick marks, the knobs'
    // own baked outer rim/plate shadow - stays BAKED in the master, no draw
    // calls for any of it.)
}

void TenebraeAudioProcessorEditor::drawPlateTypography (juce::Graphics& g, juce::Point<float> plateOrigin, float scale) const
{
    const auto toScreen = [&] (juce::Rectangle<float> local1x)
    {
        return juce::Rectangle<float> (plateOrigin.x + local1x.getX() * scale,
                                       plateOrigin.y + local1x.getY() * scale,
                                       local1x.getWidth() * scale,
                                       local1x.getHeight() * scale);
    };

    for (const auto& entry : knobLayout)
    {
        const juce::Rectangle<float> box1x ((float) (entry.cx1x - knobLabelWidth1x / 2),
                                            (float) (knobLabelCy1x - knobLabelHeight1x / 2),
                                            (float) knobLabelWidth1x,
                                            (float) knobLabelHeight1x);

        typography.drawEngraved (g, entry.engravedLabel, toScreen (box1x), scale, gildedKnobLabelStyle);
    }
}

void TenebraeAudioProcessorEditor::resized()
{
    const auto scale = scaleSteps[(size_t) scaleStepIndex];
    const auto s = [scale] (int v) { return (int) std::lround ((float) v * scale); };
    const auto sf = [scale] (float v) { return v * scale; };

    auto bounds = getLocalBounds();
    auto topStrip = bounds.removeFromTop (s (topStripHeight1x));

    scaleButton.setBounds (topStrip.removeFromRight (s (scaleButtonWidth1x)).reduced (0, s (2)));
    presetBar.setBounds (topStrip.reduced (0, s (2)));

    // Everything below is expressed in plate-local coordinates (the base
    // @1x table in PluginEditorLayout.h), then offset by the top strip +
    // gap and scaled.
    const auto toPlatePoint = [&] (juce::Point<int> plateLocal)
    {
        return juce::Point<int> (s (plateLocal.x),
                                 s (topStripHeight1x + topStripGap1x) + s (plateLocal.y));
    };

    const auto meterSize = s (meterComponentSize1x);
    const auto leftMeterTopLeft = toPlatePoint (meterLeftTopLeft1x);
    inputNeedle->setBounds (leftMeterTopLeft.x, leftMeterTopLeft.y, meterSize, meterSize);

    const auto rightMeterTopLeft = toPlatePoint (meterRightTopLeft1x);
    outputNeedle->setBounds (rightMeterTopLeft.x, rightMeterTopLeft.y, meterSize, meterSize);

    for (size_t i = 0; i < knobLayout.size(); ++i)
    {
        auto& entry = knobLayout[i];
        knobs[i].slider->setBounds (juce::Rectangle<int> (s (knobDiameter1x), s (knobDiameter1x))
                                        .withCentre (toPlatePoint ({ entry.cx1x, knobRowY1x })));
    }

    // Dial-breathing repaint regions: recomputed here so timerCallback()'s
    // per-tick repaint() call only invalidates each dial's own area rather
    // than the whole plate.
    const auto plateOriginPoint = toPlatePoint ({ 0, 0 });

    const auto breathingRect = [&] (juce::Point<float> centreMasterPx, float radiusMasterPx)
    {
        // Matches paint()'s drawDialBreathing() canvas-size formula exactly
        // (see that lambda's own docs) - only the repaint-invalidation
        // bounds here, so exact sub-pixel agreement isn't load-bearing
        // (the .expanded(4) margin below already covers it), but kept
        // consistent to avoid two silently-diverging formulas for the same
        // quantity.
        const auto outerRadiusMasterPx = radiusMasterPx * dialBreathingContentFraction;
        const auto canvasSizeMasterPx = (float) juce::jmax (2, (int) std::ceil (outerRadiusMasterPx * 2.0f) + 2);
        const auto canvasSize1x = canvasSizeMasterPx * masterToPlateScale;
        const auto centre1x = juce::Point<float> (centreMasterPx.x * masterToPlateScale, centreMasterPx.y * masterToPlateScale);

        const auto centreScreen = juce::Point<int> (plateOriginPoint.x + s ((int) std::lround (centre1x.x)),
                                                     plateOriginPoint.y + s ((int) std::lround (centre1x.y)));
        const auto sizeScreen = (int) std::lround (sf (canvasSize1x));

        return juce::Rectangle<int> (sizeScreen, sizeScreen).withCentre (centreScreen).expanded (s (4));
    };

    dialBreathingRepaintBoundsLeft = breathingRect (dialLeftCentreMasterPx, dialLeftRadiusMasterPx);
    dialBreathingRepaintBoundsRight = breathingRect (dialRightCentreMasterPx, dialRightRadiusMasterPx);
}

void TenebraeAudioProcessorEditor::updateDialBreathingMix() noexcept
{
    const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    constexpr float dt = 1.0f / 30.0f;

    dialBreathingMixLeft = basilica::gui::stepGlowMix (
        dialBreathingStateLeft, audioProcessor.getCurrentInputLevelDb(), dt, now,
        dialBreathingTauSeconds, dialBreathingFloorDb, dialBreathingCeilingDb,
        dialBreathingIdleCentre, dialBreathingIdleHalfRange, dialBreathingPhaseSeedLeft);

    dialBreathingMixRight = basilica::gui::stepGlowMix (
        dialBreathingStateRight, audioProcessor.getCurrentOutputLevelDb(), dt, now,
        dialBreathingTauSeconds, dialBreathingFloorDb, dialBreathingCeilingDb,
        dialBreathingIdleCentre, dialBreathingIdleHalfRange, dialBreathingPhaseSeedRight);
}

void TenebraeAudioProcessorEditor::timerCallback()
{
    inputNeedle->setTargetDb (dbfsToVuDb (audioProcessor.getCurrentInputLevelDb()));
    inputNeedle->tick (1.0f / 30.0f);

    outputNeedle->setTargetDb (dbfsToVuDb (audioProcessor.getCurrentOutputLevelDb()));
    outputNeedle->tick (1.0f / 30.0f);

    updateDialBreathingMix();
    repaint (dialBreathingRepaintBoundsLeft);
    repaint (dialBreathingRepaintBoundsRight);
}

void TenebraeAudioProcessorEditor::recomputeDialBreathingForPreview() noexcept
{
    updateDialBreathingMix();
    repaint (dialBreathingRepaintBoundsLeft);
    repaint (dialBreathingRepaintBoundsRight);
}

void TenebraeAudioProcessorEditor::setDialBreathingElapsedSecondsForPreview (double elapsedSeconds) noexcept
{
    const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0 - elapsedSeconds;
    dialBreathingStateLeft.startTimeSeconds = now;
    dialBreathingStateRight.startTimeSeconds = now;
    updateDialBreathingMix();
    repaint (dialBreathingRepaintBoundsLeft);
    repaint (dialBreathingRepaintBoundsRight);
}

void TenebraeAudioProcessorEditor::setDialBreathingMixForPreview (float leftT, float rightT) noexcept
{
    dialBreathingMixLeft = juce::jlimit (0.0f, 1.0f, leftT);
    dialBreathingMixRight = juce::jlimit (0.0f, 1.0f, rightT);
    repaint (dialBreathingRepaintBoundsLeft);
    repaint (dialBreathingRepaintBoundsRight);
}
