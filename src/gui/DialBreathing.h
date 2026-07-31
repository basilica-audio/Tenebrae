#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Ritual-design-specific addition to the tubecomp-family GUI component set
// (src/gui/HubNeedle.h, MasterCropKnob.h, SubtractiveGlow.h - all copied
// verbatim from the basilica-audio/aureate pilot): a subtle warm "breathing"
// darkening of a VU dial's own interior, driven by the same idle-wander +
// signal-push ballistics as SubtractiveGlow's stepGlowMix() (reused directly
// from SubtractiveGlow.h, unmodified - see that function's own docs).
//
// No separate glow/diff sprite asset exists for this design's dial
// backlight (unlike SubtractiveGlow's vent-glow asset in tubecomp/aureate),
// so rather than subtracting a hue-carrying diff sprite, this component
// precomputes a FLAT-DARKENED, circularly feathered crop of the dial face
// directly from the master render itself - combining MasterCropKnob's own
// circular-feathered-crop masking technique (buildFeatheredCrop(), reused
// conceptually here so the darkening never bleeds onto the surrounding
// metal bezel/rim) with SubtractiveGlow's cross-blend-at-opacity draw model.
//
// Hard ceiling (binding rule, see the M3 GUI briefing): the precomputed dim
// frame is the master's own pixels multiplied by (1 - maxDarkenFraction) -
// strictly less than or equal to the baked master, never brighter - and
// drawZone() draws it at opacity (1-t), so t=1 is a true no-op (zero alpha)
// exactly like SubtractiveGlow's own t=1 ceiling. There is structurally no
// path that can render this dial brighter than its own baked master frame.
namespace basilica::gui
{
    class DialBreathing
    {
    public:
        DialBreathing() = default;

        // masterImage: the design's full master render (the t=1 frame).
        // centreInMasterPx/radiusInMasterPx: the dial's own visible optical
        // centre/radius in MASTER PIXELS (layout-manifest.json's own
        // "meters" cx/cy/r - the dial's own centre, NOT the needle's hub
        // pivot, which sits below it - see PluginEditorLayout.h's docs).
        // contentFraction: how much of radiusInMasterPx the darkened crop's
        // content reaches before feathering to fully transparent (kept
        // comfortably inside the dial's own cream face, short of the metal
        // bezel ring, which must never appear to breathe).
        // maxDarkenFraction: the deepest darkening at t=0 (e.g. 0.04 = the
        // dimmest frame is 4% darker than the baked master - the brief's
        // "+/-3-4%" breathing amplitude).
        DialBreathing (const juce::Image& masterImage, juce::Point<float> centreInMasterPx,
                       float radiusInMasterPx, float maxDarkenFraction = 0.04f,
                       float contentFraction = 0.85f);

        // Draws the precomputed darkened frame at opacity (1-t) into
        // destRectOnScreen - a true no-op at t>=1 (0 alpha, so the caller's
        // own already-drawn base master shows through completely unmodified
        // - the hard ceiling). destRectOnScreen should correspond to this
        // layer's own footprint (the same square canvas buildDarkenedCrop()
        // sized) at the caller's current on-screen scale.
        void drawZone (juce::Graphics& g, juce::Rectangle<float> destRectOnScreen, float t) const;

        bool isValid() const noexcept { return dimImage.isValid(); }

        // Builds the feathered circular darkened crop: the master's own
        // pixels multiplied by (1-maxDarkenFraction) inside
        // radiusPx*contentFraction*(1-featherFraction), fading linearly back
        // to the FULL-BRIGHTNESS master (not to transparent - see below) at
        // radiusPx*contentFraction, then fully transparent (a true no-op)
        // beyond that. Exposed as an independently testable static function
        // (see tests/gui/DialBreathingTests.cpp) - message-thread only (does
        // real per-pixel work), never called from paint()/the audio thread.
        //
        // Unlike MasterCropKnob::buildFeatheredCrop (which feathers ALPHA
        // down to transparent, revealing the static master underneath at
        // the crop's own edge), this crop's ALPHA is uniformly opaque across
        // its whole canvas (every pixel of the canvas is part of the
        // breathing zone) - what feathers is the darkening AMOUNT itself,
        // ramping from full darkening at the centre back to zero darkening
        // (i.e. pixel-identical to the master) at the edge, so the circular
        // zone blends seamlessly into the surrounding bezel with no visible
        // boundary at any t.
        static juce::Image buildDarkenedCrop (const juce::Image& masterImage, juce::Point<float> centreInMasterPx,
                                              float radiusPx, float contentFraction, float maxDarkenFraction,
                                              float featherFraction = 0.35f);

    private:
        juce::Image dimImage; // sized exactly to the darkened crop's own canvas

        JUCE_LEAK_DETECTOR (DialBreathing)
    };
}
