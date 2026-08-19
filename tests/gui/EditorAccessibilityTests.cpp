#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "gui/HubNeedle.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// a11y coverage for every wired M3 photoreal-GUI control. Deliberately calls
// createAccessibilityHandler() directly rather than getAccessibilityHandler()
// - the latter (JUCE 8.0.14 juce_Component.cpp) only returns a handler once
// the component has a live native window peer, which this headless,
// no-message-loop test binary never has.
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

    std::unique_ptr<juce::AccessibilityHandler> createHandlerForTest (juce::Component& component)
    {
        return component.createAccessibilityHandler();
    }
}

TEST_CASE ("Every wired MasterCropKnob (the 4 rune knobs) exposes an accessible title, value, and declared unit", "[gui][a11y]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    TenebraeAudioProcessorEditor editor (processor);

    // See docs/gui-mapping.md for the full 4-knob mapping table and the
    // rationale for why exactly these 4 (of Tenebrae's 26 APVTS parameters)
    // were chosen for a physical control.
    struct Expectation
    {
        const char* label;
        const char* unitSuffix;
    };

    const Expectation expectations[] = {
        { "Gain", "dB" },
        { "Bass", "dB" },
        { "Mid", "dB" },
        { "Treble", "dB" },
    };

    for (const auto& expectation : expectations)
    {
        auto* knob = findChildByTitle<juce::Slider> (editor, expectation.label);
        REQUIRE (knob != nullptr);
        CHECK (knob->getTitle() == expectation.label);

        const auto handler = createHandlerForTest (*knob);
        REQUIRE (handler != nullptr);

        auto* valueInterface = handler->getValueInterface();
        REQUIRE (valueInterface != nullptr);

        const auto valueText = valueInterface->getCurrentValueAsString();
        INFO ("knob \"" << expectation.label << "\" accessible value = \"" << valueText.toStdString() << "\"");
        CHECK (valueText.isNotEmpty());
        CHECK (valueText.endsWith (expectation.unitSuffix));
    }
}

TEST_CASE ("Both VU needles expose a read-only accessible value inside the real editor", "[gui][a11y]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    TenebraeAudioProcessorEditor editor (processor);

    for (const auto* title : { "Input Level meter", "Output Level meter" })
    {
        auto* needle = findChildByTitle<basilica::gui::HubNeedle> (editor, title);
        REQUIRE (needle != nullptr);

        const auto handler = createHandlerForTest (*needle);
        REQUIRE (handler != nullptr);

        auto* valueInterface = handler->getValueInterface();
        REQUIRE (valueInterface != nullptr);
        CHECK (valueInterface->isReadOnly());
        CHECK (valueInterface->getCurrentValueAsString().endsWith ("dB"));
    }
}

TEST_CASE ("Scale button's accessible title reflects the current scale percentage, not a static string", "[gui][a11y]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    TenebraeAudioProcessorEditor editor (processor);

    auto* scaleButton = dynamic_cast<juce::TextButton*> (editor.findChildWithID ("scaleButton"));
    REQUIRE (scaleButton != nullptr);

    CHECK (scaleButton->getTitle().contains ("100%"));

    REQUIRE (scaleButton->onClick);
    scaleButton->onClick();

    CHECK (scaleButton->getButtonText() == "150%");
    CHECK (scaleButton->getTitle().contains ("150%"));
    CHECK_FALSE (scaleButton->getTitle().contains ("100%"));
}

// Issue #5 (keyboard navigation): juce::Slider ships with
// setWantsKeyboardFocus(false) in JUCE 8.0.14 (juce_Slider.cpp:1461,
// Slider::init), so MasterCropKnob was silently unreachable by Tab and its
// keyPressed()/focus ring never fired - and even when focused, the base
// keyPressed (juce_Slider.cpp:1029) steps by the raw parameter interval
// (0.01 dB on Gain's 40 dB range) and ignores Shift entirely. These tests
// pin the fixed contract (setWantsKeyboardFocus(true) + KeyboardSteps.h).

TEST_CASE ("Every interactive control is keyboard-focusable", "[gui][a11y]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    TenebraeAudioProcessorEditor editor (processor);

    int knobsSeen = 0;

    for (int i = 0; i < editor.getNumChildComponents(); ++i)
    {
        if (auto* slider = dynamic_cast<juce::Slider*> (editor.getChildComponent (i)))
        {
            ++knobsSeen;
            INFO ("knob \"" << slider->getTitle().toStdString() << "\"");
            CHECK (slider->getWantsKeyboardFocus());
        }
    }

    // All 4 rune knobs must be present AND focusable - a zero-match loop
    // must not pass vacuously.
    CHECK (knobsSeen == 4);

    auto* scaleButton = editor.findChildWithID ("scaleButton");
    REQUIRE (scaleButton != nullptr);
    CHECK (scaleButton->getWantsKeyboardFocus());
}

TEST_CASE ("Arrow keys step knobs by a practical amount, Shift+Arrow steps finer", "[gui][a11y]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    TenebraeAudioProcessorEditor editor (processor);

    // Gain: linear 0..40 dB, 0.01 dB interval (ParameterLayout.cpp) - the
    // base-class step would be 0.01 dB (4000 presses per sweep).
    auto* knob = findChildByTitle<juce::Slider> (editor, "Gain");
    REQUIRE (knob != nullptr);

    knob->setValue (20.0, juce::sendNotificationSync);

    // Called through Component& for the same [class.access.virt] reason
    // documented on createHandlerForTest().
    juce::Component& knobAsComponent = *knob;

    // Plain Right = 1% of the 40 dB range = 0.4 dB.
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (knob->getValue() == Catch::Approx (20.4).margin (1.0e-4));

    // Shift+Right = 0.1% = 0.04 dB (the keyboard analog of Shift-drag).
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::rightKey,
                                                          juce::ModifierKeys::shiftModifier, 0)));
    CHECK (knob->getValue() == Catch::Approx (20.44).margin (1.0e-4));

    // Plain Left steps back down symmetrically.
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::leftKey)));
    CHECK (knob->getValue() == Catch::Approx (20.04).margin (1.0e-4));

    // PageDown = 10% = 4 dB.
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::pageDownKey)));
    CHECK (knob->getValue() == Catch::Approx (16.04).margin (1.0e-4));

    // Home/End jump to the range extremes (WAI-ARIA slider pattern).
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::homeKey)));
    CHECK (knob->getValue() == Catch::Approx (0.0).margin (1.0e-4));
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::endKey)));
    CHECK (knob->getValue() == Catch::Approx (40.0).margin (1.0e-4));

    // Ctrl/Cmd-modified presses are host shortcuts - never consumed.
    CHECK_FALSE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::rightKey,
                                                              juce::ModifierKeys::ctrlModifier, 0)));
    CHECK (knob->getValue() == Catch::Approx (40.0).margin (1.0e-4));
}
