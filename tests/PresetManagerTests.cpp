#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "params/ParameterIds.h"
#include "presets/PresetManager.h"

#include <BinaryData.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>
#include <vector>

#include <algorithm>
#include <vector>

// M2 preset system tests (.scaffold/specs/preset-system-m2.md's "Tests"
// section - each TEST_CASE below maps to one of that section's numbered
// items, called out in the test names/comments). Ported from the pilot
// implementation in basilica-audio/nave - see docs/preset-system-notes.md
// (copied verbatim from nave) for the replication recipe this follows.
namespace
{
    using basilica::presets::FactoryPresetAsset;
    using basilica::presets::PresetManager;
    using basilica::presets::PresetManagerConfig;

    // Mirrors PluginProcessor.cpp's own makeFactoryPresetAssets() - kept as
    // an independent copy here rather than exported from PluginProcessor.cpp
    // so this test file can construct its own, fully isolated PresetManager
    // instances (see makeIsolatedConfig() below) without depending on
    // production wiring internals.
    std::vector<FactoryPresetAsset> makeTestFactoryPresetAssets()
    {
        return {
            { BinaryData::foundationChug_json, BinaryData::foundationChug_jsonSize },
            { BinaryData::lowTunedPercussive_json, BinaryData::lowTunedPercussive_jsonSize },
            { BinaryData::vintageCascade_json, BinaryData::vintageCascade_jsonSize },
            { BinaryData::scoopedWall_json, BinaryData::scoopedWall_jsonSize },
            { BinaryData::cutThroughLeadAdjacent_json, BinaryData::cutThroughLeadAdjacent_jsonSize },
            { BinaryData::brightAggressive_json, BinaryData::brightAggressive_jsonSize },
            { BinaryData::looseAndOpen_json, BinaryData::looseAndOpen_jsonSize },
            { BinaryData::fullDryWetBlend_json, BinaryData::fullDryWetBlend_jsonSize },
        };
    }

    // A fresh, isolated scratch directory per test case, so this file never
    // reads or writes the real ~/Library/Audio/Presets/... (or Windows
    // equivalent) location on the machine running the tests - see
    // PresetManagerConfig::userPresetsDirectoryOverrideForTests. Deleted on
    // destruction.
    struct ScopedTestDirectory
    {
        ScopedTestDirectory()
            : dir (juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("TenebraePresetManagerTests")
                       .getChildFile (juce::String (juce::Time::getHighResolutionTicks())
                                       + "_" + juce::String (juce::Random::getSystemRandom().nextInt (1000000))))
        {
            dir.createDirectory();
        }

        ~ScopedTestDirectory()
        {
            dir.deleteRecursively();
        }

        JUCE_DECLARE_NON_COPYABLE (ScopedTestDirectory)

        juce::File dir;
    };

    PresetManagerConfig makeIsolatedConfig (const juce::File& userDir)
    {
        PresetManagerConfig config;
        config.pluginId = "com.yvesvogl.tenebrae";
        config.pluginName = "Tenebrae";
        config.manufacturerName = "Basilica Audio";
        config.pluginVersion = "0.2.0-test";
        config.userPresetsDirectoryOverrideForTests = userDir;
        return config;
    }

    void setParam (TenebraeAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    float getParam (TenebraeAudioProcessor& processor, const char* id)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        return param->convertFrom0to1 (param->getValue());
    }
}

//==============================================================================
// 1. Save -> load round-trip restores every parameter exactly.
TEST_CASE ("PresetManager: save -> load round-trip restores every parameter exactly", "[presets]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::tight, 130.0f);
    setParam (processor, ParamIDs::gain, 28.0f);
    setParam (processor, ParamIDs::bass, -4.0f);
    setParam (processor, ParamIDs::mid, 3.0f);
    setParam (processor, ParamIDs::treble, 2.0f);
    setParam (processor, ParamIDs::level, -3.5f);
    setParam (processor, ParamIDs::mix, 72.0f);
    setParam (processor, ParamIDs::voicing, 1.0f);
    setParam (processor, ParamIDs::bright, 1.0f);
    setParam (processor, ParamIDs::toneVoice, 1.0f);
    setParam (processor, ParamIDs::presence, 6.0f);
    setParam (processor, ParamIDs::gateThreshold, -35.0f);
    setParam (processor, ParamIDs::gateAttack, 5.0f);
    setParam (processor, ParamIDs::gateHold, 100.0f);
    setParam (processor, ParamIDs::gateRelease, 400.0f);
    setParam (processor, ParamIDs::gateOn, 0.0f);

    REQUIRE (manager.saveUserPreset ("Round Trip", "Init"));

    // Perturb every parameter away from the saved values before reloading,
    // so the assertions below can't pass by accident.
    setParam (processor, ParamIDs::tight, 20.0f);
    setParam (processor, ParamIDs::gain, 0.0f);
    setParam (processor, ParamIDs::bass, 15.0f);
    setParam (processor, ParamIDs::mid, -15.0f);
    setParam (processor, ParamIDs::treble, -15.0f);
    setParam (processor, ParamIDs::level, 24.0f);
    setParam (processor, ParamIDs::mix, 0.0f);
    setParam (processor, ParamIDs::voicing, 0.0f);
    setParam (processor, ParamIDs::bright, 0.0f);
    setParam (processor, ParamIDs::toneVoice, 0.0f);
    setParam (processor, ParamIDs::presence, -12.0f);
    setParam (processor, ParamIDs::gateThreshold, 0.0f);
    setParam (processor, ParamIDs::gateAttack, 20.0f);
    setParam (processor, ParamIDs::gateHold, 0.0f);
    setParam (processor, ParamIDs::gateRelease, 5.0f);
    setParam (processor, ParamIDs::gateOn, 1.0f);

    REQUIRE (manager.loadPreset ("Round Trip"));

    CHECK (getParam (processor, ParamIDs::tight) == Catch::Approx (130.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::gain) == Catch::Approx (28.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::bass) == Catch::Approx (-4.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::mid) == Catch::Approx (3.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::treble) == Catch::Approx (2.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::level) == Catch::Approx (-3.5f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::mix) == Catch::Approx (72.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::voicing) == Catch::Approx (1.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::bright) == Catch::Approx (1.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::toneVoice) == Catch::Approx (1.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::presence) == Catch::Approx (6.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::gateThreshold) == Catch::Approx (-35.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::gateAttack) == Catch::Approx (5.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::gateHold) == Catch::Approx (100.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::gateRelease) == Catch::Approx (400.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::gateOn) == Catch::Approx (0.0f).margin (1.0e-3));
}

//==============================================================================
// 2. Import ignores unknown IDs, keeps defaults for missing IDs.
TEST_CASE ("PresetManager: import ignores unknown parameter IDs and keeps defaults for missing ones", "[presets]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    // Move mix and level away from their defaults so it's meaningful when
    // the import below leaves them untouched (they're absent from
    // "parameters").
    setParam (processor, ParamIDs::mix, 55.0f);
    setParam (processor, ParamIDs::level, 3.0f);

    // A fixture JSON generated inline (not committed under tests/fixtures/)
    // to avoid brittle relative-path resolution across CI runners with
    // different working directories (macOS vs Windows ctest invocations) -
    // this is the forward/backward-compat scenario the spec's "fixture
    // JSONs in tests/" line calls for: an unknown ID ("futureParameter",
    // simulating a newer plugin version's preset) and two known IDs
    // (tight/gain), deliberately omitting mix/level/presence/gate*.
    const juce::String fixtureJson = R"({
        "format": "basilica-preset-1",
        "plugin": "com.yvesvogl.tenebrae",
        "pluginVersion": "9.9.9",
        "name": "Forward Compat Fixture",
        "category": "Init",
        "parameters": { "tight": 120.0, "gain": 18.0, "futureParameter": 42.0 }
    })";

    const auto fixtureFile = juce::File::createTempFile (".basilicapreset");
    REQUIRE (fixtureFile.replaceWithText (fixtureJson));

    juce::String errorMessage;
    REQUIRE (manager.importPresetFile (fixtureFile, errorMessage));
    CHECK (errorMessage.isEmpty());

    // Known IDs present in the fixture were applied...
    CHECK (getParam (processor, ParamIDs::tight) == Catch::Approx (120.0f).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::gain) == Catch::Approx (18.0f).margin (1.0e-3));

    // ...IDs absent from the fixture were reset to their ParameterLayout
    // defaults (loadPreset()/importPresetFile() always reset-then-apply -
    // see PresetManager.h), not left at the pre-import 55%/3 dB values.
    auto* mixParam = processor.apvts.getParameter (ParamIDs::mix);
    auto* levelParam = processor.apvts.getParameter (ParamIDs::level);
    CHECK (getParam (processor, ParamIDs::mix) == Catch::Approx (mixParam->convertFrom0to1 (mixParam->getDefaultValue())).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::level) == Catch::Approx (levelParam->convertFrom0to1 (levelParam->getDefaultValue())).margin (1.0e-3));

    // The v0.2.0-new Presence/Gate parameters, also absent from the
    // fixture, must fall back to their own defaults too - the exact
    // migration guarantee design-brief.md section 6 requires.
    auto* presenceParam = processor.apvts.getParameter (ParamIDs::presence);
    auto* gateOnParam = processor.apvts.getParameter (ParamIDs::gateOn);
    CHECK (getParam (processor, ParamIDs::presence) == Catch::Approx (presenceParam->convertFrom0to1 (presenceParam->getDefaultValue())).margin (1.0e-3));
    CHECK (getParam (processor, ParamIDs::gateOn) == Catch::Approx (gateOnParam->convertFrom0to1 (gateOnParam->getDefaultValue())).margin (1.0e-3));

    fixtureFile.deleteFile();
}

//==============================================================================
// 3. Import refuses wrong-plugin and wrong-format files.
TEST_CASE ("PresetManager: import refuses a preset belonging to a different plugin", "[presets]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    const juce::String wrongPluginJson = R"({
        "format": "basilica-preset-1",
        "plugin": "com.yvesvogl.overture",
        "pluginVersion": "0.2.0",
        "name": "Not Tenebrae's",
        "category": "Init",
        "parameters": { "tight": 299.0 }
    })";

    const auto file = juce::File::createTempFile (".basilicapreset");
    REQUIRE (file.replaceWithText (wrongPluginJson));

    juce::String errorMessage;
    CHECK_FALSE (manager.importPresetFile (file, errorMessage));
    CHECK (errorMessage.isNotEmpty());

    // State must be left untouched - tight must NOT have picked up 299.
    CHECK (getParam (processor, ParamIDs::tight) != Catch::Approx (299.0f));

    file.deleteFile();
}

TEST_CASE ("PresetManager: import refuses a file with an incompatible format tag", "[presets]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    const juce::String wrongFormatJson = R"({
        "format": "some-other-format-2",
        "plugin": "com.yvesvogl.tenebrae",
        "pluginVersion": "0.2.0",
        "name": "Wrong Format",
        "category": "Init",
        "parameters": { "tight": 299.0 }
    })";

    const auto file = juce::File::createTempFile (".basilicapreset");
    REQUIRE (file.replaceWithText (wrongFormatJson));

    juce::String errorMessage;
    CHECK_FALSE (manager.importPresetFile (file, errorMessage));
    CHECK (errorMessage.isNotEmpty());

    file.deleteFile();
}

//==============================================================================
// 4. Factory presets all parse and load.
TEST_CASE ("PresetManager: every factory preset parses and loads without error", "[presets]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    const auto all = manager.getAllPresets();
    const auto factoryCount = std::count_if (all.begin(), all.end(), [] (auto& e) { return e.isFactory; });

    REQUIRE (factoryCount == 8); // design-brief.md's Factory Presets section

    for (auto& entry : all)
    {
        if (! entry.isFactory)
            continue;

        CAPTURE (entry.name);
        CHECK (manager.loadPreset (entry.name));
        CHECK (manager.isCurrentPresetFactory());
        CHECK (manager.getCurrentPresetName() == entry.name);
    }
}

TEST_CASE ("PresetManager: factory preset content is plausible (Foundation Chug is Init category, all parameters in range)",
           "[presets]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    const auto all = manager.getAllPresets();
    // See docs/presets.md's "Note on Default resolution": Foundation Chug
    // (not a preset literally named "Default") is this repo's Init-category
    // neutral starting point - its values are identical to
    // ParameterLayout.cpp's built-in defaults.
    const auto defaultEntry = std::find_if (all.begin(), all.end(), [] (auto& e) { return e.name == "Foundation Chug"; });

    REQUIRE (defaultEntry != all.end());
    CHECK (defaultEntry->category == "Init");
    CHECK (defaultEntry->isFactory);

    // Loading every factory preset must leave every parameter's live value
    // inside its own ParameterLayout range - APVTS's setValueNotifyingHost()
    // clamps out-of-range normalised input, so an out-of-range preset value
    // wouldn't crash, but it would silently mean the JSON doesn't say what
    // the plugin actually does - worth catching explicitly.
    for (auto& entry : all)
    {
        if (! entry.isFactory)
            continue;

        REQUIRE (manager.loadPreset (entry.name));

        CHECK (getParam (processor, ParamIDs::tight) >= 20.0f);
        CHECK (getParam (processor, ParamIDs::tight) <= 300.0f);
        CHECK (getParam (processor, ParamIDs::gain) >= 0.0f);
        CHECK (getParam (processor, ParamIDs::gain) <= 40.0f);
        CHECK (getParam (processor, ParamIDs::presence) >= -12.0f);
        CHECK (getParam (processor, ParamIDs::presence) <= 12.0f);
        CHECK (getParam (processor, ParamIDs::gateThreshold) >= -80.0f);
        CHECK (getParam (processor, ParamIDs::gateThreshold) <= 0.0f);
        CHECK (getParam (processor, ParamIDs::mix) >= 0.0f);
        CHECK (getParam (processor, ParamIDs::mix) <= 100.0f);
    }
}

//==============================================================================
// 5. Default resolution order (user Default > factory Default > plain defaults).
TEST_CASE ("PresetManager: applyStartupDefault() is a no-op when no factory or user Default exists",
           "[presets]")
{
    // Unlike nave's factory bank (which ships a preset literally named
    // "Default"), this repo's factory bank does not - see
    // docs/presets.md's note. loadPreset("Default") therefore returns false
    // and, per its own documented contract ("Returns false (state left
    // untouched) if no preset with that exact name exists" - see
    // PresetManager.h's loadPreset() docs), applyStartupDefault() leaves
    // whatever the APVTS was already holding completely alone - it is a
    // pure no-op here, not a reset to the ParameterLayout defaults (that
    // would only happen if a matching preset were found and *loaded*; the
    // "use the defaults the APVTS was already constructed with" part of the
    // spec's resolution order describes the state the very first
    // construction already left it in, before this test's setParam() call
    // below perturbs it).
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::tight, 250.0f); // perturb first, within range

    manager.applyStartupDefault();

    CHECK (manager.getCurrentPresetName().isEmpty());
    CHECK (getParam (processor, ParamIDs::tight) == Catch::Approx (250.0f).margin (1.0e-3));
}

TEST_CASE ("PresetManager: a user Default preset wins and is found by applyStartupDefault()", "[presets]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::tight, 111.0f);
    REQUIRE (manager.setCurrentAsDefault()); // writes a user preset literally named "Default"

    setParam (processor, ParamIDs::tight, 20.0f); // perturb away before the resolution check

    manager.applyStartupDefault();

    CHECK (manager.getCurrentPresetName() == "Default");
    CHECK_FALSE (manager.isCurrentPresetFactory()); // resolved to the *user* Default
    CHECK (getParam (processor, ParamIDs::tight) == Catch::Approx (111.0f).margin (1.0e-3));
}

TEST_CASE ("PresetManager: resetDefault() removes the user Default so applyStartupDefault() becomes a no-op again",
           "[presets]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::tight, 111.0f);
    REQUIRE (manager.setCurrentAsDefault());
    REQUIRE (manager.resetDefault());

    // Neither setCurrentAsDefault() nor resetDefault() touch the live APVTS
    // state (see their docs) - tight is still 111 here, and
    // applyStartupDefault() below is once again a no-op (no user or factory
    // "Default" left to find), so it stays exactly 111, not reset to
    // ParameterLayout's own default (90).
    manager.applyStartupDefault();

    CHECK (manager.getCurrentPresetName().isEmpty());
    CHECK (getParam (processor, ParamIDs::tight) == Catch::Approx (111.0f).margin (1.0e-3));
}

//==============================================================================
// 6. Dirty flag: clean after load, dirty after any param change, clean after save.
TEST_CASE ("PresetManager: dirty flag lifecycle - clean after load, dirty after a change, clean after save", "[presets]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    REQUIRE (manager.loadPreset ("Foundation Chug"));
    CHECK_FALSE (manager.isDirty());

    setParam (processor, ParamIDs::tight, 250.0f);
    CHECK (manager.isDirty());

    REQUIRE (manager.saveUserPreset ("Dirty Flag Preset", "Init"));
    CHECK_FALSE (manager.isDirty());
}

//==============================================================================
// 7. prev/next ordering and wrap-around.
TEST_CASE ("PresetManager: nextPreset()/previousPreset() traverse alphabetically and wrap around", "[presets]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    const auto all = manager.getAllPresets();
    REQUIRE (all.size() >= 2);

    REQUIRE (manager.loadPreset (all.front().name));

    manager.nextPreset();
    CHECK (manager.getCurrentPresetName() == all[1].name);

    manager.previousPreset();
    CHECK (manager.getCurrentPresetName() == all.front().name);

    // Wrap backward from the first entry to the last.
    manager.previousPreset();
    CHECK (manager.getCurrentPresetName() == all.back().name);

    // Wrap forward from the last entry back to the first.
    manager.nextPreset();
    CHECK (manager.getCurrentPresetName() == all.front().name);
}

//==============================================================================
// Additional coverage beyond the spec's minimum list: save/rename/delete
// guards, single-file export round-trip, and bank import/export.

TEST_CASE ("PresetManager: saveUserPreset() refuses to shadow a factory preset name", "[presets]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    CHECK_FALSE (manager.saveUserPreset ("Foundation Chug", "Init")); // already exists as a factory preset
    CHECK_FALSE (manager.saveUserPreset ("Scooped Wall", "Guitar"));
}

TEST_CASE ("PresetManager: renameUserPreset() moves a user preset to a new name and preserves its parameters", "[presets]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::gain, 33.0f);
    REQUIRE (manager.saveUserPreset ("Old Name", "Init"));

    REQUIRE (manager.renameUserPreset ("Old Name", "New Name"));

    setParam (processor, ParamIDs::gain, 0.0f); // perturb before reloading

    CHECK_FALSE (manager.loadPreset ("Old Name")); // gone
    REQUIRE (manager.loadPreset ("New Name"));
    CHECK (getParam (processor, ParamIDs::gain) == Catch::Approx (33.0f).margin (1.0e-3));
}

TEST_CASE ("PresetManager: deleteUserPreset() removes a user preset but never a factory preset", "[presets]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    REQUIRE (manager.saveUserPreset ("Temporary", "Init"));
    REQUIRE (manager.deleteUserPreset ("Temporary"));
    CHECK_FALSE (manager.loadPreset ("Temporary"));

    // A factory preset name isn't a file on disk in the user directory, so
    // there's nothing to delete - deleteUserPreset() must return false, and
    // the factory preset must still load afterwards.
    CHECK_FALSE (manager.deleteUserPreset ("Foundation Chug"));
    CHECK (manager.loadPreset ("Foundation Chug"));
}

TEST_CASE ("PresetManager: exportPreset()/importPresetFile() single-file round-trip", "[presets]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    setParam (processor, ParamIDs::presence, 9.0f);
    REQUIRE (manager.saveUserPreset ("Exportable", "Init"));

    const auto exportFile = juce::File::createTempFile (".basilicapreset");
    REQUIRE (manager.exportPreset ("Exportable", exportFile));
    REQUIRE (exportFile.existsAsFile());

    REQUIRE (manager.deleteUserPreset ("Exportable")); // remove the original before reimporting

    juce::String errorMessage;
    REQUIRE (manager.importPresetFile (exportFile, errorMessage));
    CHECK (getParam (processor, ParamIDs::presence) == Catch::Approx (9.0f).margin (1.0e-3));

    exportFile.deleteFile();
}

TEST_CASE ("PresetManager: exportBank()/importBank() round-trips every user preset through a zip", "[presets]")
{
    ScopedTestDirectory sourceScratch;
    ScopedTestDirectory destScratch;

    TenebraeAudioProcessor sourceProcessor;
    sourceProcessor.prepareToPlay (48000.0, 512);
    PresetManager sourceManager (sourceProcessor.apvts, makeIsolatedConfig (sourceScratch.dir), makeTestFactoryPresetAssets());

    setParam (sourceProcessor, ParamIDs::tight, 111.0f);
    REQUIRE (sourceManager.saveUserPreset ("Bank Preset A", "Init"));

    setParam (sourceProcessor, ParamIDs::tight, 222.0f);
    REQUIRE (sourceManager.saveUserPreset ("Bank Preset B", "Init"));

    const auto bankFile = juce::File::createTempFile (".zip");
    REQUIRE (sourceManager.exportBank (bankFile));
    REQUIRE (bankFile.existsAsFile());

    TenebraeAudioProcessor destProcessor;
    destProcessor.prepareToPlay (48000.0, 512);
    PresetManager destManager (destProcessor.apvts, makeIsolatedConfig (destScratch.dir), makeTestFactoryPresetAssets());

    const auto importedCount = destManager.importBank (bankFile);
    CHECK (importedCount == 2);

    REQUIRE (destManager.loadPreset ("Bank Preset A"));
    CHECK (getParam (destProcessor, ParamIDs::tight) == Catch::Approx (111.0f).margin (1.0e-3));

    REQUIRE (destManager.loadPreset ("Bank Preset B"));
    CHECK (getParam (destProcessor, ParamIDs::tight) == Catch::Approx (222.0f).margin (1.0e-3));

    bankFile.deleteFile();
}

//==============================================================================
// 8. PresetManager never allocates or locks on the audio thread.
//
// Verified primarily *by design*: nothing in TenebraeAudioProcessor::
// processBlock()/TenebraeEngine ever calls into PresetManager (see
// PluginProcessor.cpp - presetManager is only touched from the constructor
// and from PresetBar's message-thread-only UI callbacks), so there is no
// code path for this test to exercise in the first place. The one nuance is
// PresetManager::parameterChanged() (an AudioProcessorValueTreeState::
// Listener callback that JUCE does not document as guaranteed message-
// thread-only) - it is implemented as a single lock-free std::atomic<bool>
// store and nothing else (see PresetManager.h/.cpp), which this test
// exercises indirectly by driving parameter changes and processBlock() back
// to back and confirming nothing misbehaves.
TEST_CASE ("PresetManager: parameter-driven dirty tracking coexists safely with real-time audio processing", "[presets]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    ScopedTestDirectory scratch;
    PresetManager manager (processor.apvts, makeIsolatedConfig (scratch.dir), makeTestFactoryPresetAssets());

    REQUIRE (manager.loadPreset ("Foundation Chug"));
    CHECK_FALSE (manager.isDirty());

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;

    for (int block = 0; block < 8; ++block)
    {
        // Every parameterChanged() callback below happens interleaved with
        // real audio processing - if it ever became audio-thread-unsafe
        // (e.g. someone later added a lock or allocation to it), a helgrind/
        // TSan CI run would be the real detector; this test's job is just to
        // confirm normal operation isn't disrupted by the two coexisting.
        setParam (processor, ParamIDs::tight, 20.0f + static_cast<float> (block) * 10.0f);
        CHECK_NOTHROW (processor.processBlock (buffer, midi));
    }

    CHECK (manager.isDirty());
}

//==============================================================================
// v0.3.0 preset assertions (brief section 6, T-PR2).

#include "fixtures/preset_v020_values.h"

namespace
{
    // Renders a short deterministic programme through `processor`.
    juce::AudioBuffer<float> renderPresetProgramme (TenebraeAudioProcessor& processor,
                                                    double sampleRate,
                                                    int blockSize,
                                                    int totalSamples)
    {
        processor.prepareToPlay (sampleRate, blockSize);

        const auto usableSamples = (totalSamples / blockSize) * blockSize;

        juce::AudioBuffer<float> result (2, usableSamples);
        result.clear();

        juce::AudioBuffer<float> block (2, blockSize);
        juce::MidiBuffer midi;

        std::mt19937 engine (0x9A9Au);
        std::uniform_real_distribution<float> noise (-0.02f, 0.02f);

        for (int position = 0; position < usableSamples; position += blockSize)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto t = (position + i) / sampleRate;
                const auto value = 0.4f * static_cast<float> (
                                        std::sin (juce::MathConstants<double>::twoPi * 98.0 * t))
                                    + noise (engine);

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

TEST_CASE ("T-PR2: the eight original factory presets still render exactly as their v0.2 values do",
           "[preset][migration]")
{
    // The comparison is in-process A/B (brief section 6's platform note):
    //   (a) a processor with the preset loaded through PresetManager, against
    //   (b) a processor with the captured v0.2 parameter values set directly.
    //
    // Together with the byte-untouched preset JSONs and the untouched Classic
    // DSP, that expresses "these presets still sound like they did" without a
    // cross-binary reference - which does not exist, because a v0.2 binary's
    // float render cannot be byte-reproduced by the v0.3 binary across the
    // two-OS CI matrix.
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr int totalSamples = 24576;

    for (int index = 0; index < PresetV020Values::numPresets; ++index)
    {
        const auto& expected = PresetV020Values::presets[index];

        // (a) through the preset system.
        TenebraeAudioProcessor viaPresetManager;
        const auto presets = viaPresetManager.presetManager.getAllPresets();

        bool found = false;

        for (const auto& preset : presets)
            if (preset.name == juce::String (expected.displayName))
                found = true;

        INFO ("preset " << expected.displayName);
        REQUIRE (found);
        REQUIRE (viaPresetManager.presetManager.loadPreset (juce::String (expected.displayName)));

        const auto presetRender = renderPresetProgramme (viaPresetManager, sampleRate, blockSize, totalSamples);

        // (b) the captured v0.2 values, set directly.
        TenebraeAudioProcessor viaCapturedValues;

        for (int i = 0; i < 16; ++i)
        {
            auto* parameter = viaCapturedValues.apvts.getParameter (PresetV020Values::parameterIds[i]);
            REQUIRE (parameter != nullptr);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (expected.values[i]));
        }

        const auto capturedRender = renderPresetProgramme (viaCapturedValues, sampleRate, blockSize, totalSamples);

        CHECK (TestHelpers::buffersAreByteIdentical (presetRender, capturedRender));
    }
}

TEST_CASE ("T-PR2b: the factory bank has twelve presets and loads deterministically", "[preset]")
{
    TenebraeAudioProcessor processor;
    const auto presets = processor.presetManager.getAllPresets();

    int factoryCount = 0;

    for (const auto& preset : presets)
        if (preset.isFactory)
            ++factoryCount;

    // Eight from v0.2 plus four new ones showcasing the Triode engine.
    CHECK (factoryCount == 12);

    SECTION ("loading a preset twice gives identical parameter values")
    {
        for (const auto& preset : presets)
        {
            if (! preset.isFactory)
                continue;

            REQUIRE (processor.presetManager.loadPreset (preset.name));

            std::vector<float> firstLoad;

            for (auto* parameter : processor.getParameters())
                firstLoad.push_back (parameter->getValue());

            // Move everything away, then load again.
            for (auto* parameter : processor.getParameters())
                parameter->setValueNotifyingHost (0.25f);

            REQUIRE (processor.presetManager.loadPreset (preset.name));

            size_t i = 0;

            for (auto* parameter : processor.getParameters())
            {
                INFO ("preset " << preset.name << ", parameter index " << i);
                CHECK (parameter->getValue() == firstLoad[i]);
                ++i;
            }
        }
    }
}

//==============================================================================
// Vendor identity (basilica-audio/.github#2, ADR 0001): user presets moved from
// the `Yves Vogl` manufacturer folder to `Basilica Audio`, and a user must not
// lose a preset over it. These cases pin the migration's whole contract - it
// adopts, it copies rather than moves, it never overwrites, it stays out of the
// real per-user folder during tests, and both folder shapes match the platform
// convention (asserted on whichever platform is running, so macOS and Windows
// CI each check their own).
namespace
{
    // Writes a preset document straight to disk rather than going through
    // PresetManager::saveUserPreset(), so the migration is exercised against a
    // file shaped like one an older build left behind - not one this build
    // happened to produce a moment earlier.
    void writeBrandingLegacyPresetFile (const juce::File& directory,
                                const juce::String& presetName,
                                const juce::String& category,
                                const juce::String& pluginId)
    {
        directory.createDirectory();

        auto* preset = new juce::DynamicObject();
        preset->setProperty ("format", PresetManager::presetFormatTag);
        preset->setProperty ("plugin", pluginId);
        preset->setProperty ("pluginVersion", "0.1.0-legacy");
        preset->setProperty ("name", presetName);
        preset->setProperty ("category", category);
        preset->setProperty ("parameters", juce::var (new juce::DynamicObject()));

        const auto written = directory
            .getChildFile (juce::File::createLegalFileName (presetName)
                            + PresetManager::presetFileExtension)
            .replaceWithText (juce::JSON::toString (juce::var (preset), false));

        REQUIRE (written);
    }

    bool brandingContainsUserPreset (const std::vector<PresetManager::PresetEntry>& entries,
                             const juce::String& name)
    {
        return std::any_of (entries.begin(), entries.end(),
                            [&name] (const PresetManager::PresetEntry& entry)
                            { return entry.name == name && ! entry.isFactory; });
    }

    juce::String brandingCategoryOf (const std::vector<PresetManager::PresetEntry>& entries,
                             const juce::String& name)
    {
        for (auto& entry : entries)
            if (entry.name == name)
                return entry.category;

        return {};
    }
}

TEST_CASE ("PresetManager: a preset saved under the legacy manufacturer folder still loads", "[presets][branding]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory legacyDirectory;
    ScopedTestDirectory currentDirectory;

    auto config = makeIsolatedConfig (currentDirectory.dir);
    config.legacyManufacturerName = "Yves Vogl";
    config.legacyUserPresetsDirectoryOverrideForTests = legacyDirectory.dir;

    writeBrandingLegacyPresetFile (legacyDirectory.dir, "Legacy Preset", "User", config.pluginId);

    PresetManager manager (processor.apvts, config, makeTestFactoryPresetAssets());

    REQUIRE (brandingContainsUserPreset (manager.getAllPresets(), "Legacy Preset"));
    REQUIRE (manager.loadPreset ("Legacy Preset"));
    REQUIRE (manager.getCurrentPresetName() == juce::String ("Legacy Preset"));
    REQUIRE_FALSE (manager.isCurrentPresetFactory());

    const auto fileName = juce::String ("Legacy Preset") + PresetManager::presetFileExtension;

    // Copied, not moved: an older build of this plugin - or a downgrade - still
    // finds its own presets exactly where it left them.
    REQUIRE (legacyDirectory.dir.getChildFile (fileName).existsAsFile());
    REQUIRE (currentDirectory.dir.getChildFile (fileName).existsAsFile());
}

TEST_CASE ("PresetManager: the legacy migration never overwrites a preset already in the new folder", "[presets][branding]")
{
    TenebraeAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ScopedTestDirectory legacyDirectory;
    ScopedTestDirectory currentDirectory;

    auto config = makeIsolatedConfig (currentDirectory.dir);
    config.legacyManufacturerName = "Yves Vogl";
    config.legacyUserPresetsDirectoryOverrideForTests = legacyDirectory.dir;

    writeBrandingLegacyPresetFile (legacyDirectory.dir, "Shared Name", "From Legacy", config.pluginId);
    writeBrandingLegacyPresetFile (currentDirectory.dir, "Shared Name", "From Current", config.pluginId);

    PresetManager manager (processor.apvts, config, makeTestFactoryPresetAssets());

    REQUIRE (brandingCategoryOf (manager.getAllPresets(), "Shared Name") == juce::String ("From Current"));

    // Idempotent: constructing a second manager over the same pair of folders
    // must not suddenly prefer the legacy copy either.
    PresetManager second (processor.apvts, config, makeTestFactoryPresetAssets());
    REQUIRE (brandingCategoryOf (second.getAllPresets(), "Shared Name") == juce::String ("From Current"));
}

TEST_CASE ("PresetManager: overriding only the current preset directory disables the legacy lookup", "[presets][branding]")
{
    ScopedTestDirectory currentDirectory;

    auto config = makeIsolatedConfig (currentDirectory.dir);
    config.legacyManufacturerName = "Yves Vogl";

    // Without this, a test that redirects only the current directory would read
    // - and copy from - the real presets of whoever is running the suite.
    REQUIRE (PresetManager::getLegacyUserPresetsDirectory (config) == juce::File());
}

TEST_CASE ("PresetManager: current and legacy preset folders follow the platform convention", "[presets][branding]")
{
    PresetManagerConfig config;
    config.pluginName = "Tenebrae";
    config.manufacturerName = "Basilica Audio";
    config.legacyManufacturerName = "Yves Vogl";

    const auto current = PresetManager::getUserPresetsDirectory (config);
    const auto legacy = PresetManager::getLegacyUserPresetsDirectory (config);

   #if JUCE_MAC
    const auto presetsRoot = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                 .getChildFile ("Library")
                                 .getChildFile ("Audio")
                                 .getChildFile ("Presets");

    REQUIRE (current == presetsRoot.getChildFile ("Basilica Audio").getChildFile ("Tenebrae"));
    REQUIRE (legacy == presetsRoot.getChildFile ("Yves Vogl").getChildFile ("Tenebrae"));
   #else
    const auto applicationData = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);

    REQUIRE (current == applicationData.getChildFile ("Basilica Audio")
                            .getChildFile ("Tenebrae").getChildFile ("Presets"));
    REQUIRE (legacy == applicationData.getChildFile ("Yves Vogl")
                            .getChildFile ("Tenebrae").getChildFile ("Presets"));
   #endif

    // The two are the same path shape and differ only in the manufacturer
    // component - which is what makes "copy from legacy to current" a rename of
    // one folder rather than a move between two unrelated layouts.
    REQUIRE (current != legacy);
    REQUIRE (current.getFileName() == legacy.getFileName());
}

TEST_CASE ("PresetManager: an empty legacy manufacturer name disables the migration", "[presets][branding]")
{
    PresetManagerConfig config;
    config.pluginName = "Tenebrae";
    config.manufacturerName = "Basilica Audio";

    REQUIRE (PresetManager::getLegacyUserPresetsDirectory (config) == juce::File());

    // And so does a legacy name that has already caught up with the current one,
    // so re-running a completed rename is a no-op rather than a self-copy.
    config.legacyManufacturerName = "Basilica Audio";
    REQUIRE (PresetManager::getLegacyUserPresetsDirectory (config) == juce::File());
}
