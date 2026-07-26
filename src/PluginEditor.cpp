#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "presets/Localisation.h"

#include <BinaryData.h>

namespace
{
    constexpr int knobSize = 90;
    constexpr int textBoxHeight = 20;
    constexpr int labelHeight = 20;
    constexpr int margin = 16;
    constexpr int presetBarHeight = 28;
    // Signal-flow order (docs/design-brief.md section 2), now 26 control
    // slots: row 1 is the v0.1/v0.2 chain (Tight, Gain, Voicing, Bright,
    // Bass, Mid, Treble, Tone Voice, Presence, Gate Threshold/Attack/Hold/
    // Release/On) plus Level and Mix; row 2 is the v0.3.0 additions (Engine,
    // Quality, Bias Shift, Power Amp, Resonance, Sag, Gate Key, Gate
    // Hysteresis, Gate Range, Gate Release Mode).
    //
    // Sixteen slots in a single row was already at the width limit, so v0.3.0
    // wraps onto a second row rather than growing the window past 2.7k px. A
    // custom vector-drawn GUI is a later milestone (M3) - per this suite's
    // "do not gold-plate" convention, this release only adds the new controls
    // to the existing plain grid rather than redesigning the layout.
    constexpr int slotsPerRow = 16;
    constexpr int numRows = 2;
    constexpr int rowHeight = labelHeight + knobSize + textBoxHeight;
    constexpr int editorWidth = margin * 2 + slotsPerRow * knobSize + (slotsPerRow - 1) * margin;
    constexpr int editorHeight = margin * 3 + presetBarHeight + numRows * rowHeight + margin;

    // M2 i18n frame (.scaffold/specs/preset-system-m2.md): selects German
    // (resources/i18n/de.txt) or falls through to English, once, at editor
    // construction - see Localisation.h's docs. `presetBar` is a member
    // initialised via the constructor's initialiser list, and its own
    // constructor already calls TRANS() on every button label - member
    // initialisers run in declaration order regardless of the order
    // they're written in, so this helper (called from presetBar's own
    // initialiser expression below) is what actually guarantees
    // installLocalisation() runs before presetBar exists, not an
    // installLocalisation() call in the constructor *body*, which would run
    // too late.
    basilica::presets::PresetManager& initLocalisationThenGetPresetManager (TenebraeAudioProcessor& processor)
    {
        basilica::presets::installLocalisation (BinaryData::de_txt, BinaryData::de_txtSize);
        return processor.presetManager;
    }
}

TenebraeAudioProcessorEditor::TenebraeAudioProcessorEditor (TenebraeAudioProcessor& processorToEdit)
    : juce::AudioProcessorEditor (&processorToEdit),
      audioProcessor (processorToEdit),
      presetBar (initLocalisationThenGetPresetManager (processorToEdit))
{
    addAndMakeVisible (presetBar);

    configureKnob (tightKnob, ParamIDs::tight, "Tight");
    configureKnob (gainKnob, ParamIDs::gain, "Gain");
    configureChoice (voicingChoice, ParamIDs::voicing, "Voicing");

    brightButton.setButtonText ("Bright");
    addAndMakeVisible (brightButton);
    brightAttachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, ParamIDs::bright, brightButton);

    configureKnob (bassKnob, ParamIDs::bass, "Bass");
    configureKnob (midKnob, ParamIDs::mid, "Mid");
    configureKnob (trebleKnob, ParamIDs::treble, "Treble");
    configureChoice (toneVoiceChoice, ParamIDs::toneVoice, "Tone Voice");

    configureKnob (presenceKnob, ParamIDs::presence, "Presence");
    configureKnob (gateThresholdKnob, ParamIDs::gateThreshold, "Gate Thresh");
    configureKnob (gateAttackKnob, ParamIDs::gateAttack, "Gate Attack");
    configureKnob (gateHoldKnob, ParamIDs::gateHold, "Gate Hold");
    configureKnob (gateReleaseKnob, ParamIDs::gateRelease, "Gate Release");

    gateOnButton.setButtonText ("Gate");
    addAndMakeVisible (gateOnButton);
    gateOnAttachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, ParamIDs::gateOn, gateOnButton);

    configureKnob (levelKnob, ParamIDs::level, "Level");
    configureKnob (mixKnob, ParamIDs::mix, "Mix");

    // ---- v0.3.0 -----------------------------------------------------------
    configureChoice (engineChoice, ParamIDs::engine, "Engine");
    configureChoice (qualityChoice, ParamIDs::quality, "Quality");
    configureKnob (stageBiasKnob, ParamIDs::stageBias, "Bias Shift");

    powerAmpButton.setButtonText ("Power Amp");
    addAndMakeVisible (powerAmpButton);
    powerAmpAttachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, ParamIDs::powerAmp, powerAmpButton);

    configureKnob (resonanceKnob, ParamIDs::resonance, "Resonance");
    configureKnob (sagKnob, ParamIDs::sag, "Sag");
    configureChoice (gateKeyChoice, ParamIDs::gateKey, "Gate Key");
    configureKnob (gateHysteresisKnob, ParamIDs::gateHysteresis, "Gate Hyst");
    configureKnob (gateRangeKnob, ParamIDs::gateRange, "Gate Range");
    configureChoice (gateReleaseModeChoice, ParamIDs::gateReleaseMode, "Gate Rel Mode");

    setResizable (false, false);
    setSize (editorWidth, editorHeight);
}

TenebraeAudioProcessorEditor::~TenebraeAudioProcessorEditor() = default;

void TenebraeAudioProcessorEditor::configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobSize, textBoxHeight);
    addAndMakeVisible (knob.slider);

    knob.label.setText (labelText, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    // false => label sits above the slider it tracks; JUCE repositions it
    // automatically whenever the slider's bounds change, so resized() only
    // needs to place the sliders themselves.
    knob.label.attachToComponent (&knob.slider, false);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, parameterId, knob.slider);
}

void TenebraeAudioProcessorEditor::configureChoice (Choice& choice, const juce::String& parameterId, const juce::String& labelText)
{
    choice.box.addItemList (audioProcessor.apvts.getParameter (parameterId)->getAllValueStrings(), 1);
    addAndMakeVisible (choice.box);

    choice.label.setText (labelText, juce::dontSendNotification);
    choice.label.setJustificationType (juce::Justification::centred);
    choice.label.attachToComponent (&choice.box, false);
    addAndMakeVisible (choice.label);

    choice.attachment = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, parameterId, choice.box);
}

void TenebraeAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (margin);

    presetBar.setBounds (bounds.removeFromTop (presetBarHeight));
    bounds.removeFromTop (margin);

    const auto slotWidth = bounds.getWidth() / slotsPerRow;
    const auto comboBoxHeight = textBoxHeight;

    auto firstRow = bounds.removeFromTop (rowHeight);
    firstRow.removeFromTop (labelHeight); // room for the attached labels above each control

    bounds.removeFromTop (margin);

    auto secondRow = bounds.removeFromTop (rowHeight);
    secondRow.removeFromTop (labelHeight);

    // Places one control in the next slot of `row`. Knobs fill the slot;
    // combo boxes and toggles take only the top strip of it, exactly as the
    // v0.2 layout did.
    const auto placeKnob = [slotWidth] (juce::Rectangle<int>& row, Knob& knob)
    {
        knob.slider.setBounds (row.removeFromLeft (slotWidth).reduced (margin / 2, 0));
    };

    const auto placeComponent = [slotWidth, comboBoxHeight] (juce::Rectangle<int>& row, juce::Component& component)
    {
        auto slot = row.removeFromLeft (slotWidth).reduced (margin / 2, 0);
        component.setBounds (slot.removeFromTop (comboBoxHeight));
    };

    placeKnob (firstRow, tightKnob);
    placeKnob (firstRow, gainKnob);
    placeComponent (firstRow, voicingChoice.box);
    placeComponent (firstRow, brightButton);
    placeKnob (firstRow, bassKnob);
    placeKnob (firstRow, midKnob);
    placeKnob (firstRow, trebleKnob);
    placeComponent (firstRow, toneVoiceChoice.box);
    placeKnob (firstRow, presenceKnob);
    placeKnob (firstRow, gateThresholdKnob);
    placeKnob (firstRow, gateAttackKnob);
    placeKnob (firstRow, gateHoldKnob);
    placeKnob (firstRow, gateReleaseKnob);
    placeComponent (firstRow, gateOnButton);
    placeKnob (firstRow, levelKnob);
    placeKnob (firstRow, mixKnob);

    placeComponent (secondRow, engineChoice.box);
    placeComponent (secondRow, qualityChoice.box);
    placeKnob (secondRow, stageBiasKnob);
    placeComponent (secondRow, powerAmpButton);
    placeKnob (secondRow, resonanceKnob);
    placeKnob (secondRow, sagKnob);
    placeComponent (secondRow, gateKeyChoice.box);
    placeKnob (secondRow, gateHysteresisKnob);
    placeKnob (secondRow, gateRangeKnob);
    placeComponent (secondRow, gateReleaseModeChoice.box);
}
