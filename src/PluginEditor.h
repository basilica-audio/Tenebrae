#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <memory>

#include "gui/DialBreathing.h"
#include "gui/HubNeedle.h"
#include "gui/MasterCropKnob.h"
#include "gui/PlateTypography.h"
#include "gui/SubtractiveGlow.h" // GlowMixState/stepGlowMix, reused by the dial-breathing ballistics
#include "presets/PresetBar.h"

class TenebraeAudioProcessor;

// M3 photoreal GUI (the "ritual" faceplate design) - built from the
// component family piloted in basilica-audio/aureate (src/gui/HubNeedle,
// MasterCropKnob, SubtractiveGlow - copied verbatim - plus this repo's own
// DialBreathing, a ritual-specific addition documented in that header).
//
// Architecture (same master-baseline pattern as aureate/silentium): a
// SINGLE baked master image (resources/gui/master_ritual.png) is the sole
// faceplate - verdigris-bronze plate, 4 carved gargoyles, snake/thorn
// relief, both VU dials empty (no needles), all 4 rune knobs baked at 12
// o'clock - and every dynamic element is a small, targeted live overlay
// drawn on top of it:
//   1. baseline master (paint())
//   2. dial-backlight breathing, both dials (paint(), DialBreathing)
//   3. 4x MasterCropKnob (own child components, each rotating a feathered
//      crop of its own rune knob's baked art - the WHOLE disc, cap
//      included, per the briefing's "a real knob turns entirely" rule)
//   4. 2x HubNeedle (own child components - left = input level, right =
//      output level; the dial faces themselves stay fully baked)
class TenebraeAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit TenebraeAudioProcessorEditor (TenebraeAudioProcessor& processorToEdit);
    ~TenebraeAudioProcessorEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    // Test/preview-only: mirrors aureate's own setVentGlowMixForPreview()/
    // setVentGlowElapsedSecondsForPreview() - headless test binaries have no
    // running message loop to pump real timer ticks through (see
    // tests/gui/EditorSnapshotTests.cpp's own docs). Normal operation never
    // calls these.
    void setDialBreathingMixForPreview (float leftT, float rightT) noexcept;
    void setDialBreathingElapsedSecondsForPreview (double elapsedSeconds) noexcept;
    void recomputeDialBreathingForPreview() noexcept;

private:
    void timerCallback() override;
    void updateDialBreathingMix() noexcept;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    struct Knob
    {
        std::unique_ptr<basilica::gui::MasterCropKnob> slider;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText);
    void applyScaleStep (int newStepIndex);
    void cycleScale();
    void drawPlateTypography (juce::Graphics& g, juce::Point<float> plateOrigin, float scale) const;

    TenebraeAudioProcessor& audioProcessor;

    juce::Image masterImage;

    basilica::presets::PresetBar presetBar;
    juce::TextButton scaleButton;
    int scaleStepIndex = 0; // 0 = 100%, 1 = 150%, 2 = 200%

    std::unique_ptr<basilica::gui::HubNeedle> inputNeedle;  // left dial
    std::unique_ptr<basilica::gui::HubNeedle> outputNeedle; // right dial

    static constexpr int numKnobs = 4;
    std::array<Knob, numKnobs> knobs;

    basilica::gui::PlateTypography typography;
    basilica::gui::DialBreathing dialBreathingLeft;
    basilica::gui::DialBreathing dialBreathingRight;
    float dialBreathingMixLeft = 1.0f;
    float dialBreathingMixRight = 1.0f;
    basilica::gui::GlowMixState dialBreathingStateLeft;
    basilica::gui::GlowMixState dialBreathingStateRight;
    juce::Rectangle<int> dialBreathingRepaintBoundsLeft;
    juce::Rectangle<int> dialBreathingRepaintBoundsRight;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TenebraeAudioProcessorEditor)
};
