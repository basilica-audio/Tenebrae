#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "presets/PresetBar.h"

class TenebraeAudioProcessor;

// A simple, functional v0.1 editor: one rotary slider per parameter, bound
// to the APVTS via SliderAttachment. A custom vector-drawn GUI is a later
// milestone; this is deliberately plain but fully wired and usable.
class TenebraeAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit TenebraeAudioProcessorEditor (TenebraeAudioProcessor& processorToEdit);
    ~TenebraeAudioProcessorEditor() override;

    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // One knob + label per continuous parameter, in signal-flow order.
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    // One combo box + label per choice parameter (Voicing, Tone Voice).
    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<ComboBoxAttachment> attachment;
    };

    void configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText);
    void configureChoice (Choice& choice, const juce::String& parameterId, const juce::String& labelText);

    TenebraeAudioProcessor& audioProcessor;

    // M2 preset system (src/presets/PresetBar.h) - a horizontal strip
    // docked at the top of the editor. Constructed after the localisation
    // frame is installed (see the constructor) so its TRANS()'d strings
    // (and any of its own dialogs opened later) pick up the right language
    // from the very first paint.
    basilica::presets::PresetBar presetBar;

    Knob tightKnob;
    Knob gainKnob;
    Choice voicingChoice;
    juce::ToggleButton brightButton;
    std::unique_ptr<ButtonAttachment> brightAttachment;
    Knob bassKnob;
    Knob midKnob;
    Knob trebleKnob;
    Choice toneVoiceChoice;
    // v0.2.0 additions (docs/design-brief.md): Presence and the Gate.
    Knob presenceKnob;
    Knob gateThresholdKnob;
    Knob gateAttackKnob;
    Knob gateHoldKnob;
    Knob gateReleaseKnob;
    juce::ToggleButton gateOnButton;
    std::unique_ptr<ButtonAttachment> gateOnAttachment;
    // v0.3.0 additions (brief section 5: "minimal - new params wired into the
    // existing pre-M3 generic layout, attachment + label only"). The
    // photoreal M3 GUI lives on its own branches and is untouched here.
    Choice engineChoice;
    Choice qualityChoice;
    Knob stageBiasKnob;
    juce::ToggleButton powerAmpButton;
    std::unique_ptr<ButtonAttachment> powerAmpAttachment;
    Knob resonanceKnob;
    Knob sagKnob;
    Choice gateKeyChoice;
    Knob gateHysteresisKnob;
    Knob gateRangeKnob;
    Choice gateReleaseModeChoice;

    Knob levelKnob;
    Knob mixKnob;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TenebraeAudioProcessorEditor)
};
