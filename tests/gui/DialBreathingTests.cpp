#include "gui/DialBreathing.h"
#include "gui/SubtractiveGlow.h" // GlowMixState/stepGlowMix, reused by DialBreathing's ballistics

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
    juce::Image makeFlatImage (int w, int h, juce::Colour colour)
    {
        juce::Image image (juce::Image::ARGB, w, h, true);
        juce::Graphics g (image);
        g.fillAll (colour);
        return image;
    }
}

// Hard-ceiling guarantee (see DialBreathing.h's class docs): drawZone() at
// t=1 must be a true no-op (zero alpha), so the caller's own already-drawn
// baseline master shows through completely unmodified - structurally
// impossible to render brighter than the baked master.
TEST_CASE ("DialBreathing::drawZone is a true no-op at t=1 (never brighter than the baked master)", "[gui]")
{
    constexpr int size = 200;
    const auto master = makeFlatImage (size, size, juce::Colours::orange);

    basilica::gui::DialBreathing breathing (master, juce::Point<float> (100.0f, 100.0f), 80.0f, 0.04f, 0.85f);
    REQUIRE (breathing.isValid());

    juce::Image canvas (juce::Image::ARGB, size, size, true);
    {
        juce::Graphics g (canvas);
        g.drawImageAt (master, 0, 0); // caller's own baseline draw, as the real editor does
        breathing.drawZone (g, juce::Rectangle<float> (20.0f, 20.0f, 160.0f, 160.0f), 1.0f);
    }

    const auto pixel = canvas.getPixelAt (size / 2, size / 2);
    CHECK (pixel.getRed() == juce::Colours::orange.getRed());
    CHECK (pixel.getGreen() == juce::Colours::orange.getGreen());
    CHECK (pixel.getBlue() == juce::Colours::orange.getBlue());
}

TEST_CASE ("DialBreathing::drawZone darkens the dial centre at t=0 (fully dim), never brightens it", "[gui]")
{
    constexpr int size = 200;
    const auto master = makeFlatImage (size, size, juce::Colours::white);

    basilica::gui::DialBreathing breathing (master, juce::Point<float> (100.0f, 100.0f), 80.0f, 0.04f, 0.85f);
    REQUIRE (breathing.isValid());

    juce::Image canvas (juce::Image::ARGB, size, size, true);
    {
        juce::Graphics g (canvas);
        g.drawImageAt (master, 0, 0);
        breathing.drawZone (g, juce::Rectangle<float> (20.0f, 20.0f, 160.0f, 160.0f), 0.0f);
    }

    const auto pixel = canvas.getPixelAt (size / 2, size / 2);
    INFO ("t=0 centre pixel = " << pixel.toDisplayString (false).toStdString());
    CHECK (pixel.getRed() < 255);
    CHECK (pixel.getRed() > 0); // "subtle" - never crushes to black
}

TEST_CASE ("DialBreathing::buildDarkenedCrop feathers the darkening amount back to zero at the crop's own edge", "[gui]")
{
    using basilica::gui::DialBreathing;

    constexpr int masterSize = 400;
    constexpr float radiusPx = 100.0f;
    constexpr float contentFraction = 0.85f;
    constexpr float maxDarkenFraction = 0.10f; // exaggerated for a clearly measurable test signal
    constexpr float featherFraction = 0.35f;

    const auto master = makeFlatImage (masterSize, masterSize, juce::Colours::white);
    const auto crop = DialBreathing::buildDarkenedCrop (
        master, juce::Point<float> ((float) masterSize * 0.5f, (float) masterSize * 0.5f),
        radiusPx, contentFraction, maxDarkenFraction, featherFraction);

    REQUIRE (crop.isValid());

    const auto centreXY = crop.getWidth() / 2;

    SECTION ("alpha is fully opaque everywhere on the canvas (darkening feathers, not transparency)")
    {
        CHECK (crop.getPixelAt (centreXY, centreXY).getAlpha() == 255);
        CHECK (crop.getPixelAt (0, centreXY).getAlpha() == 255);
        CHECK (crop.getPixelAt (crop.getWidth() - 1, centreXY).getAlpha() == 255);
    }

    SECTION ("darkest at the centre, brightest (pixel-identical to master) at/past the outer radius")
    {
        const auto centreRed = crop.getPixelAt (centreXY, centreXY).getRed();
        const auto edgeRed = crop.getPixelAt (crop.getWidth() - 1, centreXY).getRed();

        INFO ("centre red = " << (int) centreRed << ", edge red = " << (int) edgeRed);
        CHECK (centreRed < 255);
        CHECK (edgeRed == 255);
        CHECK (centreRed < edgeRed);
    }

    SECTION ("centre darkening matches maxDarkenFraction exactly")
    {
        const auto expected = (int) juce::jlimit (0.0f, 255.0f, 255.0f * (1.0f - maxDarkenFraction));
        CHECK (crop.getPixelAt (centreXY, centreXY).getRed() == expected);
    }
}

TEST_CASE ("stepGlowMix (reused from SubtractiveGlow.h) breathes around the idle centre at silence and never exceeds 1.0", "[gui]")
{
    basilica::gui::GlowMixState state;

    for (double t = 0.0; t < 40.0; t += 1.0)
    {
        const auto mix = basilica::gui::stepGlowMix (state, /*instantaneousDb*/ -100.0f, 1.0f / 30.0f, t,
                                                      0.20f, -60.0f, 0.0f, 0.55f, 0.45f, 1.0f);
        CHECK (mix >= 0.0f);
        CHECK (mix <= 1.0f);
    }
}
