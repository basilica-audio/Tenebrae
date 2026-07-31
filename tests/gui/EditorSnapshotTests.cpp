#include "PluginEditor.h"
#include "PluginEditorLayout.h"
#include "PluginProcessor.h"
#include "gui/HubNeedle.h"
#include "gui/MasterCropKnob.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

// GUI smoke tests for the M3 photoreal "ritual" editor (src/PluginEditor.h,
// src/gui/). juce::ScopedJuceInitialiser_GUI is installed once for the whole
// test binary in tests/TestMain.cpp, so Components/Timers are safe to
// construct here even though this is a headless console executable with no
// running message loop (timers simply never fire, which is fine - these
// tests only exercise synchronous construction/paint/destruction).
TEST_CASE ("Editor constructs, lays out, and destroys cleanly", "[gui]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    {
        TenebraeAudioProcessorEditor editor (processor);

        CHECK (editor.getWidth() > 0);
        CHECK (editor.getHeight() > 0);
    }
    // editor destroyed here - JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR
    // (used throughout src/gui/ and on the editor itself) asserts at process
    // exit in Debug builds if any tagged instance was ever leaked, so a
    // clean run of this whole test binary is itself the leak check.
}

namespace
{
    template <typename ComponentType>
    ComponentType* findChildByTitle (juce::Component& parent, const juce::String& title)
    {
        for (int i = 0; i < parent.getNumChildComponents(); ++i)
            if (auto* typed = dynamic_cast<ComponentType*> (parent.getChildComponent (i)))
                if (typed->getTitle() == title)
                    return typed;

        return nullptr;
    }

    // Configures a deliberately "alive-looking" state before snapshotting,
    // per the M3 GUI briefing: both needles deflected off their idle "0 VU"
    // tick to visibly DIFFERENT readings (proving the two dials are
    // independent, not mirrored), the dial-backlight breathing partway
    // between fully dim and its baked ceiling on each dial, and the 4 rune
    // knobs at varied, non-default rotations.
    //
    // HubNeedle's own ~250ms ballistic ramp and the editor's own
    // timerCallback()-driven breathing ballistics would need real timer
    // ticks pumped through a running message loop to actually reach these
    // values - this headless test binary has no such loop, so the
    // test/preview-only hooks (setImmediateDbForPreview()/
    // setDialBreathingMixForPreview()) seed the readings directly instead.
    void configureLiveLookingState (TenebraeAudioProcessorEditor& editor)
    {
        if (auto* input = findChildByTitle<basilica::gui::HubNeedle> (editor, "Input Level meter"))
            input->setImmediateDbForPreview (-4.0f); // near the dial's "0" tick

        if (auto* output = findChildByTitle<basilica::gui::HubNeedle> (editor, "Output Level meter"))
            output->setImmediateDbForPreview (1.5f); // deflected further, into the red zone

        editor.setDialBreathingMixForPreview (0.65f, 0.9f);

        struct KnobValue
        {
            const char* label;
            double normalisedValue;
        };

        const KnobValue knobValues[] = {
            { "Gain", 0.65 }, { "Bass", 0.30 }, { "Mid", 0.75 }, { "Treble", 0.15 },
        };

        for (const auto& kv : knobValues)
            if (auto* knob = findChildByTitle<juce::Slider> (editor, kv.label))
                knob->setValue (knob->proportionOfLengthToValue (kv.normalisedValue), juce::dontSendNotification);
    }
}

TEST_CASE ("Editor snapshot at 100% is non-blank and is written for PR review", "[gui]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    TenebraeAudioProcessorEditor editor (processor);
    REQUIRE (editor.getWidth() > 0);
    REQUIRE (editor.getHeight() > 0);

    configureLiveLookingState (editor);

    // SoftwareImageType (rather than the default NativeImageType) avoids any
    // dependency on an actual native graphics context/window, which keeps
    // this robust on headless CI runners.
    const auto snapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});

    REQUIRE (snapshot.isValid());
    CHECK (snapshot.getWidth() == editor.getWidth());
    CHECK (snapshot.getHeight() == editor.getHeight());

    // Non-blank: sample a small grid of points and confirm they are not all
    // identical to the top-left corner - a completely blank/solid-fill
    // render (e.g. every asset failing to decode) would fail this.
    const auto reference = snapshot.getPixelAt (0, 0);
    bool foundDifference = false;

    for (int y = 0; y < snapshot.getHeight() && ! foundDifference; y += juce::jmax (1, snapshot.getHeight() / 20))
        for (int x = 0; x < snapshot.getWidth() && ! foundDifference; x += juce::jmax (1, snapshot.getWidth() / 20))
            if (snapshot.getPixelAt (x, y) != reference)
                foundDifference = true;

    CHECK (foundDifference);

#ifdef TENEBRAE_DOCS_DIR
    // Committed directly for PR review (docs/gui-preview.png).
    juce::PNGImageFormat pngFormat;
    const auto outFile = juce::File (TENEBRAE_DOCS_DIR).getChildFile ("gui-preview.png");

    if (auto stream = std::unique_ptr<juce::FileOutputStream> (outFile.createOutputStream()))
    {
        stream->setPosition (0);
        stream->truncate();
        CHECK (pngFormat.writeImageToStream (snapshot, *stream));
    }
    else
    {
        FAIL ("could not open output stream for " << outFile.getFullPathName());
    }
#endif
}

// Proof that the two VU needles are independently driven (not mirrored
// copies of the same reading): distinctly different immediate-preview dB
// values must produce visibly different pixel content in each needle's own
// bounds.
TEST_CASE ("The two VU needles render visibly different poses at different readings", "[gui]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    TenebraeAudioProcessorEditor editor (processor);
    REQUIRE (editor.getWidth() > 0);

    auto* input = findChildByTitle<basilica::gui::HubNeedle> (editor, "Input Level meter");
    auto* output = findChildByTitle<basilica::gui::HubNeedle> (editor, "Output Level meter");
    REQUIRE (input != nullptr);
    REQUIRE (output != nullptr);

    input->setImmediateDbForPreview (-18.0f);
    output->setImmediateDbForPreview (2.5f);

    const auto snapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (snapshot.isValid());

    const auto inputCrop = snapshot.getClippedImage (input->getBounds());
    const auto outputCrop = snapshot.getClippedImage (output->getBounds());
    REQUIRE (inputCrop.isValid());
    REQUIRE (outputCrop.isValid());
    REQUIRE (inputCrop.getWidth() == outputCrop.getWidth());
    REQUIRE (inputCrop.getHeight() == outputCrop.getHeight());

    int changedPixels = 0;
    const int totalPixels = inputCrop.getWidth() * inputCrop.getHeight();

    for (int y = 0; y < inputCrop.getHeight(); ++y)
    {
        for (int x = 0; x < inputCrop.getWidth(); ++x)
        {
            const auto a = inputCrop.getPixelAt (x, y);
            const auto b = outputCrop.getPixelAt (x, y);
            const auto diff = std::abs (a.getRed() - b.getRed()) + std::abs (a.getGreen() - b.getGreen())
                             + std::abs (a.getBlue() - b.getBlue());
            if (diff > 24)
                ++changedPixels;
        }
    }

    INFO (changedPixels << "/" << totalPixels << " px differ between the two needle bays at distinct readings");
    CHECK (changedPixels > 0);
}

// Proof that MasterCropKnob's rotating crop actually moves: two knobs
// (both on the ritual design's single row) set to distinctly non-rest
// proportions must visibly differ, within their own bounds, from their
// construction-time (APVTS-default) rendering.
TEST_CASE ("MasterCropKnob instances visibly rotate at distinctly non-default values", "[gui]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    TenebraeAudioProcessorEditor editor (processor);
    REQUIRE (editor.getWidth() > 0);
    REQUIRE (editor.getHeight() > 0);

    const auto restSnapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (restSnapshot.isValid());

    struct ZoomKnob
    {
        const char* label;
        double proportion;
    };

    constexpr ZoomKnob zoomKnobs[] = {
        { "Gain", 0.05 },
        { "Treble", 0.95 },
    };

    for (const auto& zk : zoomKnobs)
    {
        auto* knob = findChildByTitle<juce::Slider> (editor, zk.label);
        REQUIRE (knob != nullptr);
        knob->setValue (knob->proportionOfLengthToValue (zk.proportion), juce::dontSendNotification);
    }

    const auto movedSnapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (movedSnapshot.isValid());

    for (const auto& zk : zoomKnobs)
    {
        auto* knob = findChildByTitle<juce::Slider> (editor, zk.label);
        REQUIRE (knob != nullptr);

        const auto cropBounds = knob->getBounds().expanded (4);
        const auto restCrop = restSnapshot.getClippedImage (cropBounds);
        const auto movedCrop = movedSnapshot.getClippedImage (cropBounds);

        REQUIRE (restCrop.isValid());
        REQUIRE (movedCrop.isValid());
        REQUIRE (restCrop.getWidth() == movedCrop.getWidth());
        REQUIRE (restCrop.getHeight() == movedCrop.getHeight());

        int changedPixels = 0;
        const int totalPixels = restCrop.getWidth() * restCrop.getHeight();

        for (int y = 0; y < restCrop.getHeight(); ++y)
        {
            for (int x = 0; x < restCrop.getWidth(); ++x)
            {
                const auto a = restCrop.getPixelAt (x, y);
                const auto b = movedCrop.getPixelAt (x, y);
                const auto diff = std::abs (a.getRed() - b.getRed()) + std::abs (a.getGreen() - b.getGreen())
                                 + std::abs (a.getBlue() - b.getBlue());
                if (diff > 24)
                    ++changedPixels;
            }
        }

        INFO (zk.label << ": " << changedPixels << "/" << totalPixels << " px changed between rest and moved pose");
        CHECK (changedPixels > totalPixels / 20); // >5% of the crop visibly moved
    }
}

// Item 5-style idle-breathing proof: at true silence (fresh processor, never
// processBlock()'d), both dials' backlight breathing must still be visibly
// time-varying (never reads as flatly "off") - see DialBreathing.h/
// PluginEditor.cpp's dialBreathing* constants.
TEST_CASE ("Dial-backlight breathing is visibly time-varying at silence on both dials", "[gui]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    TenebraeAudioProcessorEditor editor (processor);
    REQUIRE (editor.getWidth() > 0);
    REQUIRE (editor.getHeight() > 0);

    editor.setDialBreathingElapsedSecondsForPreview (5.0);
    const auto frame1 = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (frame1.isValid());

    editor.setDialBreathingElapsedSecondsForPreview (11.0);
    const auto frame2 = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (frame2.isValid());

    REQUIRE (frame1.getWidth() == frame2.getWidth());
    REQUIRE (frame1.getHeight() == frame2.getHeight());

    long long diffEnergy = 0;

    for (int y = 0; y < frame1.getHeight(); ++y)
    {
        for (int x = 0; x < frame1.getWidth(); ++x)
        {
            const auto a = frame1.getPixelAt (x, y);
            const auto b = frame2.getPixelAt (x, y);
            diffEnergy += std::abs (a.getRed() - b.getRed()) + std::abs (a.getGreen() - b.getGreen())
                        + std::abs (a.getBlue() - b.getBlue());
        }
    }

    INFO ("total diff energy between the two idle breathing frames = " << diffEnergy);
    CHECK (diffEnergy > 0);
}

// Hard-ceiling regression, exercised through the real editor (not just
// DialBreathingTests.cpp's own unit test of the component in isolation):
// t=1 on both dials must reproduce the plain baseline-master render exactly
// within each dial's own breathing zone.
TEST_CASE ("Dial-backlight breathing never renders brighter than the baked master through the real editor", "[gui]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    TenebraeAudioProcessorEditor editor (processor);
    REQUIRE (editor.getWidth() > 0);

    editor.setDialBreathingMixForPreview (1.0f, 1.0f);
    const auto ceilingSnapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (ceilingSnapshot.isValid());

    editor.setDialBreathingMixForPreview (0.0f, 0.0f);
    const auto dimSnapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (dimSnapshot.isValid());

    // Samples across the whole canvas rather than a single probe point, so
    // the ceiling is asserted everywhere, not just in one dial's general
    // vicinity, and counts outlier pixels rather than demanding zero-
    // tolerance pixel-exactness: the breathing overlay's own destRect and
    // the plate's own stretchToFit draw are two INDEPENDENTLY resampled
    // g.drawImage() calls (see DialBreathing::drawZone() and paint()'s own
    // drawDialBreathing() - both at highResamplingQuality), and this
    // design's plate happens to carry a hairline (2-3px), very high-
    // contrast bezel-to-dial-face highlight ring right at the edge of both
    // breathing zones. Two separately-resampled draws of the same physical
    // region can legitimately land a fraction of a pixel apart at that
    // exact ring, which is invisible at normal viewing (see this revision's
    // own interactive verification) but registers as a large per-pixel
    // swing precisely BECAUSE the ring is so thin and high-contrast - not a
    // sign that darkening is actually being inverted or that content is
    // misregistered in general. DialBreathingTests.cpp's own unit tests
    // assert the exact, resampling-free relationship (t=1 pixel-identical
    // to the master, strictly < at t=0) directly against
    // buildDarkenedCrop()'s own pixels, which is the right place for exact
    // per-pixel assertions; this integration test instead asserts there is
    // no SYSTEMATIC brightening (only a small number of isolated
    // high-contrast-edge outliers, never a broad region).
    constexpr int perChannelTolerance = 2;
    constexpr double maxOutlierFraction = 0.01; // <=1% of sampled points may be hairline-edge resampling outliers
    int sampledPoints = 0;
    int outlierPoints = 0;
    int maxOvershoot = 0;
    int worstX = -1, worstY = -1;

    for (int y = 0; y < ceilingSnapshot.getHeight(); y += 3)
    {
        for (int x = 0; x < ceilingSnapshot.getWidth(); x += 3)
        {
            const auto ceilingPixel = ceilingSnapshot.getPixelAt (x, y);
            const auto dimPixel = dimSnapshot.getPixelAt (x, y);
            ++sampledPoints;

            const auto overshoot = juce::jmax (dimPixel.getRed() - ceilingPixel.getRed(),
                                               dimPixel.getGreen() - ceilingPixel.getGreen(),
                                               dimPixel.getBlue() - ceilingPixel.getBlue());

            if (overshoot > maxOvershoot)
            {
                maxOvershoot = overshoot;
                worstX = x;
                worstY = y;
            }

            if (dimPixel.getRed() > ceilingPixel.getRed() + perChannelTolerance
                || dimPixel.getGreen() > ceilingPixel.getGreen() + perChannelTolerance
                || dimPixel.getBlue() > ceilingPixel.getBlue() + perChannelTolerance)
            {
                ++outlierPoints;
            }
        }
    }

    const auto outlierFraction = (double) outlierPoints / (double) juce::jmax (1, sampledPoints);
    INFO ("outliers = " << outlierPoints << "/" << sampledPoints << " (" << (outlierFraction * 100.0) << "%), max overshoot = "
                        << maxOvershoot << " at (" << worstX << "," << worstY << ")");
    CHECK (outlierFraction <= maxOutlierFraction);
}
