#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <random>
#include <vector>

TEST_CASE ("State round-trip preserves non-default values of every parameter", "[state]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    auto* tightParam = processor.apvts.getParameter (ParamIDs::tight);
    auto* gainParam = processor.apvts.getParameter (ParamIDs::gain);
    auto* bassParam = processor.apvts.getParameter (ParamIDs::bass);
    auto* midParam = processor.apvts.getParameter (ParamIDs::mid);
    auto* trebleParam = processor.apvts.getParameter (ParamIDs::treble);
    auto* levelParam = processor.apvts.getParameter (ParamIDs::level);
    auto* mixParam = processor.apvts.getParameter (ParamIDs::mix);
    auto* voicingParam = processor.apvts.getParameter (ParamIDs::voicing);
    auto* brightParam = processor.apvts.getParameter (ParamIDs::bright);
    auto* toneVoiceParam = processor.apvts.getParameter (ParamIDs::toneVoice);
    auto* presenceParam = processor.apvts.getParameter (ParamIDs::presence);
    auto* gateThresholdParam = processor.apvts.getParameter (ParamIDs::gateThreshold);
    auto* gateAttackParam = processor.apvts.getParameter (ParamIDs::gateAttack);
    auto* gateHoldParam = processor.apvts.getParameter (ParamIDs::gateHold);
    auto* gateReleaseParam = processor.apvts.getParameter (ParamIDs::gateRelease);
    auto* gateOnParam = processor.apvts.getParameter (ParamIDs::gateOn);

    REQUIRE (tightParam != nullptr);
    REQUIRE (gainParam != nullptr);
    REQUIRE (bassParam != nullptr);
    REQUIRE (midParam != nullptr);
    REQUIRE (trebleParam != nullptr);
    REQUIRE (levelParam != nullptr);
    REQUIRE (mixParam != nullptr);
    REQUIRE (voicingParam != nullptr);
    REQUIRE (brightParam != nullptr);
    REQUIRE (toneVoiceParam != nullptr);
    REQUIRE (presenceParam != nullptr);
    REQUIRE (gateThresholdParam != nullptr);
    REQUIRE (gateAttackParam != nullptr);
    REQUIRE (gateHoldParam != nullptr);
    REQUIRE (gateReleaseParam != nullptr);
    REQUIRE (gateOnParam != nullptr);

    tightParam->setValueNotifyingHost (tightParam->convertTo0to1 (150.0f));
    gainParam->setValueNotifyingHost (gainParam->convertTo0to1 (33.0f));
    bassParam->setValueNotifyingHost (bassParam->convertTo0to1 (9.0f));
    midParam->setValueNotifyingHost (midParam->convertTo0to1 (-7.0f));
    trebleParam->setValueNotifyingHost (trebleParam->convertTo0to1 (5.0f));
    levelParam->setValueNotifyingHost (levelParam->convertTo0to1 (-6.5f));
    mixParam->setValueNotifyingHost (mixParam->convertTo0to1 (42.0f));
    // Non-default choice/bool values (defaults are index 0 / false for
    // Voicing/Bright/Tone Voice, true for Gate On), so the round trip below
    // genuinely exercises them.
    voicingParam->setValueNotifyingHost (voicingParam->convertTo0to1 (1.0f)); // Loose
    brightParam->setValueNotifyingHost (brightParam->convertTo0to1 (1.0f)); // on
    toneVoiceParam->setValueNotifyingHost (toneVoiceParam->convertTo0to1 (2.0f)); // Boost
    presenceParam->setValueNotifyingHost (presenceParam->convertTo0to1 (7.5f));
    gateThresholdParam->setValueNotifyingHost (gateThresholdParam->convertTo0to1 (-30.0f));
    gateAttackParam->setValueNotifyingHost (gateAttackParam->convertTo0to1 (10.0f));
    gateHoldParam->setValueNotifyingHost (gateHoldParam->convertTo0to1 (250.0f));
    gateReleaseParam->setValueNotifyingHost (gateReleaseParam->convertTo0to1 (900.0f));
    gateOnParam->setValueNotifyingHost (gateOnParam->convertTo0to1 (0.0f)); // off (non-default)

    const auto savedTight = tightParam->getValue();
    const auto savedGain = gainParam->getValue();
    const auto savedBass = bassParam->getValue();
    const auto savedMid = midParam->getValue();
    const auto savedTreble = trebleParam->getValue();
    const auto savedLevel = levelParam->getValue();
    const auto savedMix = mixParam->getValue();
    const auto savedVoicing = voicingParam->getValue();
    const auto savedBright = brightParam->getValue();
    const auto savedToneVoice = toneVoiceParam->getValue();
    const auto savedPresence = presenceParam->getValue();
    const auto savedGateThreshold = gateThresholdParam->getValue();
    const auto savedGateAttack = gateAttackParam->getValue();
    const auto savedGateHold = gateHoldParam->getValue();
    const auto savedGateRelease = gateReleaseParam->getValue();
    const auto savedGateOn = gateOnParam->getValue();

    juce::MemoryBlock savedState;
    processor.getStateInformation (savedState);
    REQUIRE (savedState.getSize() > 0);

    // Reset every parameter back to its default before restoring, so the
    // round-trip assertion below can't pass by accident.
    tightParam->setValueNotifyingHost (tightParam->getDefaultValue());
    gainParam->setValueNotifyingHost (gainParam->getDefaultValue());
    bassParam->setValueNotifyingHost (bassParam->getDefaultValue());
    midParam->setValueNotifyingHost (midParam->getDefaultValue());
    trebleParam->setValueNotifyingHost (trebleParam->getDefaultValue());
    levelParam->setValueNotifyingHost (levelParam->getDefaultValue());
    mixParam->setValueNotifyingHost (mixParam->getDefaultValue());
    voicingParam->setValueNotifyingHost (voicingParam->getDefaultValue());
    brightParam->setValueNotifyingHost (brightParam->getDefaultValue());
    toneVoiceParam->setValueNotifyingHost (toneVoiceParam->getDefaultValue());
    presenceParam->setValueNotifyingHost (presenceParam->getDefaultValue());
    gateThresholdParam->setValueNotifyingHost (gateThresholdParam->getDefaultValue());
    gateAttackParam->setValueNotifyingHost (gateAttackParam->getDefaultValue());
    gateHoldParam->setValueNotifyingHost (gateHoldParam->getDefaultValue());
    gateReleaseParam->setValueNotifyingHost (gateReleaseParam->getDefaultValue());
    gateOnParam->setValueNotifyingHost (gateOnParam->getDefaultValue());

    REQUIRE (tightParam->getValue() != Catch::Approx (savedTight));
    REQUIRE (gainParam->getValue() != Catch::Approx (savedGain));
    REQUIRE (bassParam->getValue() != Catch::Approx (savedBass));
    REQUIRE (midParam->getValue() != Catch::Approx (savedMid));
    REQUIRE (trebleParam->getValue() != Catch::Approx (savedTreble));
    REQUIRE (levelParam->getValue() != Catch::Approx (savedLevel));
    REQUIRE (mixParam->getValue() != Catch::Approx (savedMix));
    REQUIRE (voicingParam->getValue() != Catch::Approx (savedVoicing));
    REQUIRE (brightParam->getValue() != Catch::Approx (savedBright));
    REQUIRE (toneVoiceParam->getValue() != Catch::Approx (savedToneVoice));
    REQUIRE (presenceParam->getValue() != Catch::Approx (savedPresence));
    REQUIRE (gateThresholdParam->getValue() != Catch::Approx (savedGateThreshold));
    REQUIRE (gateAttackParam->getValue() != Catch::Approx (savedGateAttack));
    REQUIRE (gateHoldParam->getValue() != Catch::Approx (savedGateHold));
    REQUIRE (gateReleaseParam->getValue() != Catch::Approx (savedGateRelease));
    REQUIRE (gateOnParam->getValue() != Catch::Approx (savedGateOn));

    processor.setStateInformation (savedState.getData(), static_cast<int> (savedState.getSize()));

    CHECK (tightParam->getValue() == Catch::Approx (savedTight).margin (1e-6));
    CHECK (gainParam->getValue() == Catch::Approx (savedGain).margin (1e-6));
    CHECK (bassParam->getValue() == Catch::Approx (savedBass).margin (1e-6));
    CHECK (midParam->getValue() == Catch::Approx (savedMid).margin (1e-6));
    CHECK (trebleParam->getValue() == Catch::Approx (savedTreble).margin (1e-6));
    CHECK (levelParam->getValue() == Catch::Approx (savedLevel).margin (1e-6));
    CHECK (mixParam->getValue() == Catch::Approx (savedMix).margin (1e-6));
    CHECK (voicingParam->getValue() == Catch::Approx (savedVoicing).margin (1e-6));
    CHECK (brightParam->getValue() == Catch::Approx (savedBright).margin (1e-6));
    CHECK (toneVoiceParam->getValue() == Catch::Approx (savedToneVoice).margin (1e-6));
    CHECK (presenceParam->getValue() == Catch::Approx (savedPresence).margin (1e-6));
    CHECK (gateThresholdParam->getValue() == Catch::Approx (savedGateThreshold).margin (1e-6));
    CHECK (gateAttackParam->getValue() == Catch::Approx (savedGateAttack).margin (1e-6));
    CHECK (gateHoldParam->getValue() == Catch::Approx (savedGateHold).margin (1e-6));
    CHECK (gateReleaseParam->getValue() == Catch::Approx (savedGateRelease).margin (1e-6));
    CHECK (gateOnParam->getValue() == Catch::Approx (savedGateOn).margin (1e-6));
}

TEST_CASE ("State round-trip: an old v0.1.0-style state tree (missing Presence/Gate IDs) falls back to v0.2.0 defaults",
           "[state]")
{
    // design-brief.md section 6: "old (v0.1.0) state trees loaded into
    // v0.2.0 must not fail or reset to defaults wholesale - missing
    // Presence/Gate parameter IDs in an old state tree fall back to their
    // v0.2.0 defaults." Simulated here by building a state tree containing
    // only the v0.1 parameter set (constructed via a second processor
    // instance whose new-in-v0.2.0 parameters are never touched, so its
    // APVTS state naturally omits nothing - JUCE's ValueTree always writes
    // every known parameter - so this instead directly strips the new
    // parameter IDs' XML attributes out of a saved state to reproduce a
    // genuinely old, pre-v0.2.0 state file).
    TenebraeAudioProcessor sourceProcessor;
    sourceProcessor.prepareToPlay (48000.0, 512);

    auto* tightParam = sourceProcessor.apvts.getParameter (ParamIDs::tight);
    REQUIRE (tightParam != nullptr);
    tightParam->setValueNotifyingHost (tightParam->convertTo0to1 (200.0f));

    juce::MemoryBlock savedState;
    sourceProcessor.getStateInformation (savedState);

    const std::unique_ptr<juce::XmlElement> xml (juce::AudioProcessor::getXmlFromBinary (
        savedState.getData(), static_cast<int> (savedState.getSize())));
    REQUIRE (xml != nullptr);

    // Strip every v0.2.0 parameter's <PARAM> child element, simulating a
    // state tree saved by the pre-v0.2.0 plugin (which never wrote them at
    // all).
    static constexpr const char* newParamIds[] = {
        ParamIDs::presence, ParamIDs::gateThreshold, ParamIDs::gateAttack,
        ParamIDs::gateHold, ParamIDs::gateRelease, ParamIDs::gateOn,
    };

    for (int i = xml->getNumChildElements(); --i >= 0;)
    {
        auto* child = xml->getChildElement (i);

        if (child == nullptr || ! child->hasTagName ("PARAM"))
            continue;

        const auto id = child->getStringAttribute ("id");

        for (const auto* newId : newParamIds)
        {
            if (id == juce::String (newId))
            {
                xml->removeChildElement (child, true);
                break;
            }
        }
    }

    juce::MemoryBlock strippedState;
    juce::AudioProcessor::copyXmlToBinary (*xml, strippedState);

    TenebraeAudioProcessor destinationProcessor;
    destinationProcessor.prepareToPlay (48000.0, 512);

    CHECK_NOTHROW (destinationProcessor.setStateInformation (strippedState.getData(), static_cast<int> (strippedState.getSize())));

    // The old (present) parameter round-tripped correctly...
    auto* destinationTight = destinationProcessor.apvts.getParameter (ParamIDs::tight);
    REQUIRE (destinationTight != nullptr);
    CHECK (destinationTight->convertFrom0to1 (destinationTight->getValue()) == Catch::Approx (200.0f).margin (1.0e-3));

    // ...and every v0.2.0-new parameter, absent from the loaded tree, fell
    // back to its own ParameterLayout default rather than failing the load
    // or resetting the whole state wholesale.
    auto* presenceParam = destinationProcessor.apvts.getParameter (ParamIDs::presence);
    auto* gateThresholdParam = destinationProcessor.apvts.getParameter (ParamIDs::gateThreshold);
    auto* gateOnParam = destinationProcessor.apvts.getParameter (ParamIDs::gateOn);

    REQUIRE (presenceParam != nullptr);
    REQUIRE (gateThresholdParam != nullptr);
    REQUIRE (gateOnParam != nullptr);

    CHECK (presenceParam->getValue() == Catch::Approx (presenceParam->getDefaultValue()).margin (1.0e-6));
    CHECK (gateThresholdParam->getValue() == Catch::Approx (gateThresholdParam->getDefaultValue()).margin (1.0e-6));
    CHECK (gateOnParam->getValue() == Catch::Approx (gateOnParam->getDefaultValue()).margin (1.0e-6));
    CHECK (gateOnParam->getValue() > 0.5f); // Gate defaults to ON - see design-brief.md section 5
}

//==============================================================================
// v0.3.0 state migration (brief section 6, T-S1/T-S2).

namespace
{
    // Loads the committed v0.2.0 state fixture (tests/fixtures/state_v020.xml).
    // The path comes from a compile definition set in CMakeLists.txt, so this
    // works regardless of ctest's working directory.
    juce::String loadV020StateFixture()
    {
        const juce::File fixture (juce::String (TENEBRAE_TEST_FIXTURE_DIR) + "/state_v020.xml");
        return fixture.existsAsFile() ? fixture.loadFileAsString() : juce::String();
    }

    // Applies an XML state to a processor exactly the way a host would: via
    // setStateInformation(), through the binary wrapper.
    void applyStateXml (TenebraeAudioProcessor& processor, const juce::String& xmlText)
    {
        const std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (xmlText));
        REQUIRE (xml != nullptr);

        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary (*xml, block);
        processor.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
    }

    // A 5 s deterministic programme: a sine burst, a noise bed, and silence -
    // enough to exercise the cascade, the gate's open/hold/release path and
    // the dry/wet alignment in a single render.
    juce::AudioBuffer<float> renderMigrationProgramme (TenebraeAudioProcessor& processor,
                                                       double sampleRate,
                                                       int blockSize)
    {
        processor.prepareToPlay (sampleRate, blockSize);

        const auto totalSamples = (static_cast<int> (sampleRate * 5.0) / blockSize) * blockSize;

        juce::AudioBuffer<float> result (2, totalSamples);
        result.clear();

        juce::AudioBuffer<float> block (2, blockSize);
        juce::MidiBuffer midi;

        std::mt19937 engine (0x7E7Eu);
        std::uniform_real_distribution<float> noise (-0.01f, 0.01f);

        for (int position = 0; position < totalSamples; position += blockSize)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto index = position + i;
                const auto t = index / sampleRate;

                // 0.0-2.0 s: 110 Hz burst. 2.0-3.5 s: noise bed only.
                // 3.5-5.0 s: silence.
                float value = noise (engine);

                if (t < 2.0)
                    value += 0.5f * static_cast<float> (
                        std::sin (juce::MathConstants<double>::twoPi * 110.0 * t));
                else if (t >= 3.5)
                    value = 0.0f;

                block.setSample (0, i, value);
                block.setSample (1, i, value);
            }

            processor.processBlock (block, midi);

            for (int channel = 0; channel < 2; ++channel)
                result.copyFrom (channel, position, block, channel, 0, blockSize);
        }

        return result;
    }
}

TEST_CASE ("T-S1: a v0.2.0 session state renders byte-identically to the v0.3.0 defaults", "[state][migration]")
{
    // THE RELEASE GATE. Ten new parameters were added; every one of them is
    // specified to be neutral at its default; a v0.2 session carries none of
    // them and therefore loads them all at those defaults. If that is true,
    // a processor that loaded a v0.2 state and a fresh processor at its own
    // defaults must produce the same samples - not similar, the same.
    //
    // Both renders are produced inside this process, which is the only place
    // byte equality is meaningful (brief section 6's platform note).
    const auto fixtureXml = loadV020StateFixture();
    REQUIRE (fixtureXml.isNotEmpty());

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    TenebraeAudioProcessor migrated;
    applyStateXml (migrated, fixtureXml);
    const auto migratedRender = renderMigrationProgramme (migrated, sampleRate, blockSize);

    TenebraeAudioProcessor fresh;
    const auto freshRender = renderMigrationProgramme (fresh, sampleRate, blockSize);

    CHECK (TestHelpers::buffersAreByteIdentical (migratedRender, freshRender));

    SECTION ("and the migrated state really is missing the new IDs")
    {
        // Guards the fixture itself: if someone "helpfully" adds the v0.3.0
        // IDs to it, the test above would still pass while no longer testing
        // migration at all.
        const std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (fixtureXml));
        REQUIRE (xml != nullptr);

        int paramCount = 0;

        for (auto* child : xml->getChildIterator())
            if (child->hasTagName ("PARAM"))
                ++paramCount;

        CHECK (paramCount == 16);
        CHECK (! xml->hasAttribute (TenebraeAudioProcessor::stateSchemaAttribute));

        static constexpr const char* newIds[] = {
            ParamIDs::engine, ParamIDs::quality, ParamIDs::stageBias, ParamIDs::powerAmp,
            ParamIDs::resonance, ParamIDs::sag, ParamIDs::gateKey, ParamIDs::gateHysteresis,
            ParamIDs::gateRange, ParamIDs::gateReleaseMode
        };

        for (const auto* id : newIds)
        {
            bool found = false;

            for (auto* child : xml->getChildIterator())
                if (child->hasTagName ("PARAM") && child->getStringAttribute ("id") == id)
                    found = true;

            INFO ("fixture unexpectedly contains " << id);
            CHECK (! found);
        }
    }

    SECTION ("and every new parameter did land on its documented neutral default")
    {
        struct Expectation { const char* id; float value; };

        for (const auto& expectation : { Expectation { ParamIDs::engine, 0.0f },        // Classic
                                         Expectation { ParamIDs::quality, 1.0f },       // Standard
                                         Expectation { ParamIDs::stageBias, 100.0f },
                                         Expectation { ParamIDs::powerAmp, 0.0f },      // Off
                                         Expectation { ParamIDs::resonance, 0.0f },
                                         Expectation { ParamIDs::sag, 0.0f },
                                         Expectation { ParamIDs::gateKey, 0.0f },       // Post
                                         Expectation { ParamIDs::gateHysteresis, 0.0f },
                                         Expectation { ParamIDs::gateRange, 90.0f },    // Mute
                                         Expectation { ParamIDs::gateReleaseMode, 0.0f } }) // Manual
        {
            auto* parameter = migrated.apvts.getParameter (expectation.id);
            REQUIRE (parameter != nullptr);

            INFO ("parameter " << expectation.id);
            CHECK (parameter->convertFrom0to1 (parameter->getValue())
                    == Catch::Approx (expectation.value).margin (1.0e-4));
        }
    }
}

TEST_CASE ("T-S2: v0.3.0 stamps stateSchema on save and still loads unstamped states", "[state][migration]")
{
    SECTION ("saving writes stateSchema = 3")
    {
        TenebraeAudioProcessor processor;

        juce::MemoryBlock saved;
        processor.getStateInformation (saved);

        const std::unique_ptr<juce::XmlElement> xml (
            juce::AudioProcessor::getXmlFromBinary (saved.getData(), static_cast<int> (saved.getSize())));
        REQUIRE (xml != nullptr);

        CHECK (xml->hasAttribute (TenebraeAudioProcessor::stateSchemaAttribute));
        CHECK (xml->getStringAttribute (TenebraeAudioProcessor::stateSchemaAttribute)
                == TenebraeAudioProcessor::stateSchemaVersion);
    }

    SECTION ("a state with no stateSchema attribute loads without error")
    {
        const auto fixtureXml = loadV020StateFixture();
        REQUIRE (fixtureXml.isNotEmpty());

        TenebraeAudioProcessor processor;
        CHECK_NOTHROW (applyStateXml (processor, fixtureXml));

        // And the old values survived the load.
        auto* tight = processor.apvts.getParameter (ParamIDs::tight);
        REQUIRE (tight != nullptr);
        CHECK (tight->convertFrom0to1 (tight->getValue()) == Catch::Approx (90.0f).margin (0.01));
    }

    SECTION ("a round trip through save and load preserves all 26 parameters")
    {
        TenebraeAudioProcessor source;

        // Move every parameter off its default so the round trip has
        // something to lose.
        struct Setting { const char* id; float value; };

        const std::vector<Setting> settings = {
            { ParamIDs::tight, 150.0f },      { ParamIDs::gain, 31.0f },
            { ParamIDs::bass, -4.0f },        { ParamIDs::mid, 5.0f },
            { ParamIDs::treble, 3.0f },       { ParamIDs::level, -2.0f },
            { ParamIDs::mix, 80.0f },         { ParamIDs::voicing, 1.0f },
            { ParamIDs::bright, 1.0f },       { ParamIDs::toneVoice, 2.0f },
            { ParamIDs::presence, 4.0f },     { ParamIDs::gateThreshold, -35.0f },
            { ParamIDs::gateAttack, 3.0f },   { ParamIDs::gateHold, 60.0f },
            { ParamIDs::gateRelease, 400.0f },{ ParamIDs::gateOn, 0.0f },
            { ParamIDs::engine, 1.0f },       { ParamIDs::quality, 2.0f },
            { ParamIDs::stageBias, 160.0f },  { ParamIDs::powerAmp, 1.0f },
            { ParamIDs::resonance, 7.5f },    { ParamIDs::sag, 45.0f },
            { ParamIDs::gateKey, 1.0f },      { ParamIDs::gateHysteresis, 5.0f },
            { ParamIDs::gateRange, 40.0f },   { ParamIDs::gateReleaseMode, 1.0f },
        };

        CHECK (static_cast<int> (settings.size()) == source.getParameters().size());

        for (const auto& setting : settings)
        {
            auto* parameter = source.apvts.getParameter (setting.id);
            REQUIRE (parameter != nullptr);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (setting.value));
        }

        juce::MemoryBlock saved;
        source.getStateInformation (saved);

        TenebraeAudioProcessor destination;
        destination.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));

        for (const auto& setting : settings)
        {
            auto* parameter = destination.apvts.getParameter (setting.id);
            REQUIRE (parameter != nullptr);

            INFO ("parameter " << setting.id);
            CHECK (parameter->convertFrom0to1 (parameter->getValue())
                    == Catch::Approx (setting.value).margin (0.05));
        }
    }
}
