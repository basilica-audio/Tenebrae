#include "DialBreathing.h"

#include <cmath>

namespace basilica::gui
{
    juce::Image DialBreathing::buildDarkenedCrop (const juce::Image& masterImage, juce::Point<float> centreInMasterPx,
                                                   float radiusPx, float contentFraction, float maxDarkenFraction,
                                                   float featherFraction)
    {
        const auto outerRadius = juce::jmax (1.0f, radiusPx * contentFraction);
        const auto innerRadius = outerRadius * (1.0f - juce::jlimit (0.0f, 1.0f, featherFraction));

        // Same "+2px margin beyond 2*outerRadius" rationale as
        // MasterCropKnob::buildFeatheredCrop: without it, the canvas's own
        // outermost pixel row sits fractionally inside outerRadius (pixel
        // centres, not edges) and never quite reaches zero darkening right
        // at the crop's own edge.
        const auto canvasSize = juce::jmax (2, (int) std::ceil (outerRadius * 2.0f) + 2);

        juce::Image crop (juce::Image::ARGB, canvasSize, canvasSize, true);

        if (! masterImage.isValid())
            return crop;

        const auto srcLeft = centreInMasterPx.x - (float) canvasSize * 0.5f;
        const auto srcTop = centreInMasterPx.y - (float) canvasSize * 0.5f;
        const auto centrePx = (float) canvasSize * 0.5f;

        juce::Image::BitmapData dst (crop, juce::Image::BitmapData::writeOnly);

        for (int y = 0; y < canvasSize; ++y)
        {
            for (int x = 0; x < canvasSize; ++x)
            {
                const auto dx = (float) x + 0.5f - centrePx;
                const auto dy = (float) y + 0.5f - centrePx;
                const auto r = std::sqrt (dx * dx + dy * dy);

                // darkenAmount: 1.0 (full maxDarkenFraction applied) inside
                // innerRadius, ramping linearly to 0.0 (pixel-identical to
                // the master - the hard ceiling) at/past outerRadius. Unlike
                // MasterCropKnob's alpha feather, this crop's own alpha
                // channel stays fully opaque everywhere (see the class docs'
                // rationale) - what feathers here is the darkening amount.
                float darkenAmount;
                if (r <= innerRadius)
                    darkenAmount = 1.0f;
                else if (r >= outerRadius)
                    darkenAmount = 0.0f;
                else
                    darkenAmount = 1.0f - (r - innerRadius) / (outerRadius - innerRadius);

                const auto srcX = juce::jlimit (0, masterImage.getWidth() - 1, (int) std::floor (srcLeft + (float) x));
                const auto srcY = juce::jlimit (0, masterImage.getHeight() - 1, (int) std::floor (srcTop + (float) y));

                const auto colour = masterImage.getPixelAt (srcX, srcY);
                const auto brightnessScale = 1.0f - maxDarkenFraction * darkenAmount;

                const auto darkR = (juce::uint8) juce::jlimit (0.0f, 255.0f, (float) colour.getRed() * brightnessScale);
                const auto darkG = (juce::uint8) juce::jlimit (0.0f, 255.0f, (float) colour.getGreen() * brightnessScale);
                const auto darkB = (juce::uint8) juce::jlimit (0.0f, 255.0f, (float) colour.getBlue() * brightnessScale);

                dst.setPixelColour (x, y, juce::Colour::fromRGBA (darkR, darkG, darkB, colour.getAlpha()));
            }
        }

        return crop;
    }

    DialBreathing::DialBreathing (const juce::Image& masterImage, juce::Point<float> centreInMasterPx,
                                  float radiusInMasterPx, float maxDarkenFraction, float contentFraction)
        : dimImage (buildDarkenedCrop (masterImage, centreInMasterPx, radiusInMasterPx, contentFraction, maxDarkenFraction))
    {
    }

    void DialBreathing::drawZone (juce::Graphics& g, juce::Rectangle<float> destRectOnScreen, float t) const
    {
        const auto dimAlpha = juce::jlimit (0.0f, 1.0f, 1.0f - t);

        if (! isValid() || dimAlpha <= 0.001f)
            return;

        juce::Graphics::ScopedSaveState saveState (g);
        g.setOpacity (dimAlpha);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (dimImage, destRectOnScreen, juce::RectanglePlacement::stretchToFit, false);
    }
}
