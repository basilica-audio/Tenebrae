#include "PluginEditor.h"
#include "PluginEditorLayout.h"
#include "PluginProcessor.h"

#include <BinaryData.h>

#include <catch2/catch_test_macros.hpp>

// Typography-pass proof (suite typo phase, owner decision 2026-07-26): the
// ritual master's four rune knobs carry baked sigils but no function
// names - the gilded GAIN/BASS/MID/TREBLE labels on the plate's bottom
// ledge are a live JUCE text layer (src/gui/PlateTypography.h, drawn last
// in PluginEditor::paint()). Proofs: (1) the gilded lettering brightens
// each label's ledge box where the raw master's dark bronze has almost no
// bright pixels; (2) a flat-ground unit render of the shared glyph draw
// path; (3) a layout invariant keeping the labels on the ledge, clear of
// the knob hit-areas.
namespace
{
    float brightFractionIn (const juce::Image& image, juce::Rectangle<int> area, int threshold)
    {
        int bright = 0, total = 0;

        for (int y = area.getY(); y < area.getBottom(); ++y)
        {
            for (int x = area.getX(); x < area.getRight(); ++x)
            {
                const auto c = image.getPixelAt (x, y);
                const auto lum = (int) std::lround (0.299f * c.getRed() + 0.587f * c.getGreen() + 0.114f * c.getBlue());

                ++total;

                if (lum > threshold)
                    ++bright;
            }
        }

        return total > 0 ? (float) bright / (float) total : 0.0f;
    }

    juce::Rectangle<int> toSnapshotRect (juce::Rectangle<float> plateLocal1x)
    {
        return plateLocal1x
            .translated (0.0f, (float) (tnbr::layout::topStripHeight1x + tnbr::layout::topStripGap1x))
            .getSmallestIntegerContainer();
    }

    juce::Rectangle<int> toMasterRect (juce::Rectangle<float> plateLocal1x)
    {
        const auto toMaster = (float) tnbr::layout::masterCanvasWidthPx / (float) tnbr::layout::plateWidth1x;

        return juce::Rectangle<float> (plateLocal1x.getX() * toMaster, plateLocal1x.getY() * toMaster,
                                       plateLocal1x.getWidth() * toMaster, plateLocal1x.getHeight() * toMaster)
            .getSmallestIntegerContainer();
    }
}

TEST_CASE ("Gilded knob labels brighten the dark bronze ledge", "[gui][typography]")
{
    using namespace tnbr::layout;

    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    TenebraeAudioProcessorEditor editor (processor);
    REQUIRE (editor.getWidth() > 0);

    const auto snapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (snapshot.isValid());

    const auto master = juce::ImageCache::getFromMemory (BinaryData::master_ritual_png,
                                                         BinaryData::master_ritual_pngSize);
    REQUIRE (master.isValid());

    for (const auto cx : knobCx1x)
    {
        // Tightened to a 44px text core (the full 96px label boxes span
        // ledge stretches whose baked verdigris highlights would dilute
        // the reading - and the shortest label, MID, covers only ~a
        // quarter of the full box).
        const juce::Rectangle<float> box1x ((float) cx - 22.0f,
                                            (float) (knobLabelCy1x - knobLabelHeight1x / 2),
                                            44.0f, (float) knobLabelHeight1x);

        // Gold lettering (luminance ~164 at coverage) against the aged
        // bronze ledge: at threshold 130 the raw master's boxes measure
        // 0-0.6% bright, the lettered snapshot 2.4-4.4%.
        const auto snapshotBright = brightFractionIn (snapshot, toSnapshotRect (box1x), 130);
        const auto masterBright = brightFractionIn (master, toMasterRect (box1x), 130);

        CHECK (snapshotBright > masterBright + 0.015f);
    }
}

TEST_CASE ("PlateTypography renders glyphs and its offset pass on a flat ground", "[gui][typography]")
{
    basilica::gui::PlateTypography typography (BinaryData::EBGaramondRegular_ttf,
                                               (int) BinaryData::EBGaramondRegular_ttfSize,
                                               BinaryData::EBGaramondSemiBold_ttf,
                                               (int) BinaryData::EBGaramondSemiBold_ttfSize);

    const juce::Colour ground (0xff433f33); // aged bronze, luminance ~62

    juce::Image canvas (juce::Image::RGB, 160, 24, true);
    {
        juce::Graphics g (canvas);
        g.fillAll (ground);

        const basilica::gui::EngravedTextStyle style {
            juce::Colour (0xe6c9a05a), juce::Colour (0x8c000000), 13.0f, 0.14f, true
        };

        typography.drawEngraved (g, "TREBLE", canvas.getBounds().toFloat(), 1.0f, style);
    }

    int goldPixels = 0, shadowPixels = 0;

    for (int y = 0; y < canvas.getHeight(); ++y)
    {
        for (int x = 0; x < canvas.getWidth(); ++x)
        {
            const auto c = canvas.getPixelAt (x, y);
            const auto lum = 0.299f * c.getRed() + 0.587f * c.getGreen() + 0.114f * c.getBlue();

            if (lum > 130.0f)
                ++goldPixels;
            else if (lum < 42.0f)
                ++shadowPixels;
        }
    }

    // 6 semibold capitals at 13px leave a solid body of gold pixels; the
    // one-pixel-down dark pass leaves a visible (thin - the gold pass
    // covers most of it) shadow fringe.
    CHECK (goldPixels > 60);
    CHECK (shadowPixels > 10);
}

TEST_CASE ("Ledge lettering never intrudes into a knob's interactive hit-area", "[gui][typography]")
{
    using namespace tnbr::layout;

    const auto sliderBottom = knobRowY1x + knobDiameter1x / 2;
    const auto labelTop = knobLabelCy1x - knobLabelHeight1x / 2;

    CHECK (labelTop > sliderBottom);
    CHECK (knobLabelCy1x + knobLabelHeight1x / 2 < plateHeight1x);
}
