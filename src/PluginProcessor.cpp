#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIds.h"
#include "params/ParameterLayout.h"

#include <BinaryData.h>

namespace
{
    // The small, Tenebrae-specific config surface PresetManager needs (see
    // src/presets/PresetManager.h's class docs) - everything else about the
    // preset system is fully generic and portable to sibling plugins (see
    // basilica-audio/nave's docs/preset-system-notes.md).
    basilica::presets::PresetManagerConfig makePresetManagerConfig()
    {
        // JucePlugin_CFBundleIdentifier expands to a raw (unquoted) token
        // sequence, not a string literal - JUCE_STRINGIFY() is the
        // documented way to turn it into one. This is always
        // "com.yvesvogl.tenebrae" here (BUNDLE_ID in CMakeLists.txt),
        // matching the "plugin" field baked into every
        // presets/factory/*.json file.
        basilica::presets::PresetManagerConfig config;
        config.pluginId = JUCE_STRINGIFY (JucePlugin_CFBundleIdentifier);
        config.pluginName = JucePlugin_Name;
        config.manufacturerName = "Yves Vogl";
        config.pluginVersion = JucePlugin_VersionString;
        // userPresetsDirectoryOverrideForTests intentionally left
        // default-constructed (empty) - production instances always use the
        // real platform-standard preset location (see PresetManager.h).
        return config;
    }

    // BinaryData symbol names are derived from the presets/factory/*.json
    // file names passed to juce_add_binary_data() in CMakeLists.txt (dots
    // become underscores) - this list must stay in sync with that SOURCES
    // list. Order here only affects factory-preset iteration order before
    // getAllPresets() re-sorts alphabetically, so it isn't otherwise
    // significant.
    std::vector<basilica::presets::FactoryPresetAsset> makeFactoryPresetAssets()
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
            // v0.3.0: four presets showcasing the Triode engine, the power-amp
            // block and the new gate capabilities.
            { BinaryData::triodeFoundation_json, BinaryData::triodeFoundation_jsonSize },
            { BinaryData::saggingDoom_json, BinaryData::saggingDoom_jsonSize },
            { BinaryData::feedbackTightRhythm_json, BinaryData::feedbackTightRhythm_jsonSize },
            { BinaryData::adaptiveGateChug_json, BinaryData::adaptiveGateChug_jsonSize },
        };
    }
}

//==============================================================================
TenebraeAudioProcessor::TenebraeAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout()),
      presetManager (apvts, makePresetManagerConfig(), makeFactoryPresetAssets())
{
    tightHz = apvts.getRawParameterValue (ParamIDs::tight);
    gainDb = apvts.getRawParameterValue (ParamIDs::gain);
    bassDb = apvts.getRawParameterValue (ParamIDs::bass);
    midDb = apvts.getRawParameterValue (ParamIDs::mid);
    trebleDb = apvts.getRawParameterValue (ParamIDs::treble);
    levelDb = apvts.getRawParameterValue (ParamIDs::level);
    mixPercent = apvts.getRawParameterValue (ParamIDs::mix);
    voicingChoice = apvts.getRawParameterValue (ParamIDs::voicing);
    brightToggle = apvts.getRawParameterValue (ParamIDs::bright);
    toneVoiceChoice = apvts.getRawParameterValue (ParamIDs::toneVoice);
    presenceDb = apvts.getRawParameterValue (ParamIDs::presence);
    gateThresholdDb = apvts.getRawParameterValue (ParamIDs::gateThreshold);
    gateAttackMs = apvts.getRawParameterValue (ParamIDs::gateAttack);
    gateHoldMs = apvts.getRawParameterValue (ParamIDs::gateHold);
    gateReleaseMs = apvts.getRawParameterValue (ParamIDs::gateRelease);
    gateOnToggle = apvts.getRawParameterValue (ParamIDs::gateOn);

    // v0.3.0 additions.
    engineChoice = apvts.getRawParameterValue (ParamIDs::engine);
    qualityChoice = apvts.getRawParameterValue (ParamIDs::quality);
    stageBiasPercent = apvts.getRawParameterValue (ParamIDs::stageBias);
    powerAmpToggle = apvts.getRawParameterValue (ParamIDs::powerAmp);
    resonanceDb = apvts.getRawParameterValue (ParamIDs::resonance);
    sagPercent = apvts.getRawParameterValue (ParamIDs::sag);
    gateKeyChoice = apvts.getRawParameterValue (ParamIDs::gateKey);
    gateHysteresisDb = apvts.getRawParameterValue (ParamIDs::gateHysteresis);
    gateRangeDb = apvts.getRawParameterValue (ParamIDs::gateRange);
    gateReleaseModeChoice = apvts.getRawParameterValue (ParamIDs::gateReleaseMode);

    jassert (engineChoice != nullptr);
    jassert (qualityChoice != nullptr);
    jassert (stageBiasPercent != nullptr);
    jassert (powerAmpToggle != nullptr);
    jassert (resonanceDb != nullptr);
    jassert (sagPercent != nullptr);
    jassert (gateKeyChoice != nullptr);
    jassert (gateHysteresisDb != nullptr);
    jassert (gateRangeDb != nullptr);
    jassert (gateReleaseModeChoice != nullptr);

    jassert (tightHz != nullptr);
    jassert (gainDb != nullptr);
    jassert (bassDb != nullptr);
    jassert (midDb != nullptr);
    jassert (trebleDb != nullptr);
    jassert (levelDb != nullptr);
    jassert (mixPercent != nullptr);
    jassert (voicingChoice != nullptr);
    jassert (brightToggle != nullptr);
    jassert (toneVoiceChoice != nullptr);
    jassert (presenceDb != nullptr);
    jassert (gateThresholdDb != nullptr);
    jassert (gateAttackMs != nullptr);
    jassert (gateHoldMs != nullptr);
    jassert (gateReleaseMs != nullptr);
    jassert (gateOnToggle != nullptr);

    // M2 default resolution: user "Default" preset > factory "Default"
    // preset > the ParameterLayout defaults apvts was just constructed
    // with above (see PresetManager::applyStartupDefault()'s docs and
    // docs/presets.md's note on why this repo's factory bank has no preset
    // literally named "Default").
    presetManager.applyStartupDefault();

    // 100 ms is far below any perceptible delay on a deliberate Engine/
    // Quality switch, and far above any per-block cost concern.
    latencyReporter.startTimer (100);
}

TenebraeAudioProcessor::~TenebraeAudioProcessor()
{
    latencyReporter.stopTimer();
}

void TenebraeAudioProcessor::LatencyReporter::timerCallback()
{
    const auto pending = owner.pendingLatencySamples.load (std::memory_order_relaxed);

    if (pending != owner.getLatencySamples())
        owner.setLatencySamples (pending);
}

//==============================================================================
void TenebraeAudioProcessor::pushParametersToEngine()
{
    engine.setTightFrequencyHz (tightHz->load (std::memory_order_relaxed));
    engine.setGainDb (gainDb->load (std::memory_order_relaxed));
    engine.setBassDb (bassDb->load (std::memory_order_relaxed));
    engine.setMidDb (midDb->load (std::memory_order_relaxed));
    engine.setTrebleDb (trebleDb->load (std::memory_order_relaxed));
    engine.setLevelDb (levelDb->load (std::memory_order_relaxed));
    engine.setMixProportion (mixPercent->load (std::memory_order_relaxed) * 0.01f);
    engine.setVoicing (juce::roundToInt (voicingChoice->load (std::memory_order_relaxed)));
    engine.setBright (brightToggle->load (std::memory_order_relaxed) >= 0.5f);
    engine.setToneVoice (juce::roundToInt (toneVoiceChoice->load (std::memory_order_relaxed)));
    engine.setPresenceDb (presenceDb->load (std::memory_order_relaxed));
    engine.setGateThresholdDb (gateThresholdDb->load (std::memory_order_relaxed));
    engine.setGateAttackMs (gateAttackMs->load (std::memory_order_relaxed));
    engine.setGateHoldMs (gateHoldMs->load (std::memory_order_relaxed));
    engine.setGateReleaseMs (gateReleaseMs->load (std::memory_order_relaxed));
    engine.setGateOn (gateOnToggle->load (std::memory_order_relaxed) >= 0.5f);

    // ---- v0.3.0 ---------------------------------------------------------
    engine.setEngineMode (juce::roundToInt (engineChoice->load (std::memory_order_relaxed)) == 1
                              ? TenebraeEngine::EngineMode::triode
                              : TenebraeEngine::EngineMode::classic);

    switch (juce::roundToInt (qualityChoice->load (std::memory_order_relaxed)))
    {
        case 0:  engine.setQuality (TenebraeEngine::Quality::eco); break;
        case 2:  engine.setQuality (TenebraeEngine::Quality::hq); break;
        default: engine.setQuality (TenebraeEngine::Quality::standard); break;
    }

    engine.setStageBiasPercent (stageBiasPercent->load (std::memory_order_relaxed));
    engine.setPowerAmpOn (powerAmpToggle->load (std::memory_order_relaxed) >= 0.5f);
    engine.setResonanceDb (resonanceDb->load (std::memory_order_relaxed));
    engine.setSagPercent (sagPercent->load (std::memory_order_relaxed));

    engine.setGateKeySource (juce::roundToInt (gateKeyChoice->load (std::memory_order_relaxed)) == 1
                                 ? Gate::KeySource::pre
                                 : Gate::KeySource::post);
    engine.setGateHysteresisDb (gateHysteresisDb->load (std::memory_order_relaxed));

    // The Gate Range parameter's top position is "Mute" (the v0.2 hard-zero
    // target), not 90 dB of attenuation - see ParameterLayout.cpp.
    const auto rangeDb = gateRangeDb->load (std::memory_order_relaxed);
    engine.setGateRange (rangeDb, rangeDb >= Gate::maxRangeDb);

    engine.setGateReleaseMode (juce::roundToInt (gateReleaseModeChoice->load (std::memory_order_relaxed)) == 1
                                   ? Gate::ReleaseMode::automatic
                                   : Gate::ReleaseMode::manual);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout TenebraeAudioProcessor::createParameterLayout()
{
    return tnbr::createParameterLayout();
}

//==============================================================================
const juce::String TenebraeAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool TenebraeAudioProcessor::acceptsMidi() const
{
    return false;
}

bool TenebraeAudioProcessor::producesMidi() const
{
    return false;
}

bool TenebraeAudioProcessor::isMidiEffect() const
{
    return false;
}

double TenebraeAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int TenebraeAudioProcessor::getNumPrograms()
{
    return 1;
}

int TenebraeAudioProcessor::getCurrentProgram()
{
    return 0;
}

void TenebraeAudioProcessor::setCurrentProgram (int)
{
}

const juce::String TenebraeAudioProcessor::getProgramName (int)
{
    return {};
}

void TenebraeAudioProcessor::changeProgramName (int, const juce::String&)
{
}

//==============================================================================
void TenebraeAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32> (getTotalNumOutputChannels());

    // Seed the engine's parameters from the current APVTS state before
    // prepare() primes the filter coefficients, so the very first block
    // after prepareToPlay() already reflects the host/session's actual
    // parameter values rather than the engine's built-in defaults.
    pushParametersToEngine();

    engine.prepare (spec);

    // Oversampling (applied around the cascade) is the only source of
    // reported latency; the dry path is compensated against it internally by
    // TenebraeEngine's DryWetMixer (see docs/architecture.md). v0.3.0's
    // Engine/Quality switches change the oversampling factor and therefore
    // this value, so it is also published for LatencyReporter to re-report
    // from the message thread if it changes later - see PluginProcessor.h.
    pendingLatencySamples.store (engine.getLatencySamples(), std::memory_order_relaxed);
    setLatencySamples (engine.getLatencySamples());
}

void TenebraeAudioProcessor::releaseResources()
{
}

void TenebraeAudioProcessor::reset()
{
    engine.reset();

    // Idle-rest fix (silentium's own AnalogMeter precedent - see that
    // repo's PluginProcessor.cpp): many hosts call reset() on transport
    // stop/suspend, a point after which processBlock() may not fire again
    // for an arbitrary amount of time - without this, a loud reading
    // captured in the last block right before the stop would persist on the
    // VU dials indefinitely. Re-parking both atomics to the floor converges
    // the GUI needles back toward the bottom of the dial as soon as the
    // editor's next timer tick reads them.
    currentInputLevelDb.store (-100.0f, std::memory_order_relaxed);
    currentOutputLevelDb.store (-100.0f, std::memory_order_relaxed);
}

bool TenebraeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();

    const auto mainOut = layouts.getMainOutputChannelSet();
    const auto mainIn = layouts.getMainInputChannelSet();

    if (mainOut != mono && mainOut != stereo)
        return false;

    if (mainOut != mainIn)
        return false;

    return true;
}

void TenebraeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto totalNumInputChannels = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Buses are constrained to in == out (mono or stereo), so this is
    // normally a no-op, but it's cheap insurance against stray channels.
    for (auto channel = totalNumInputChannels; channel < totalNumOutputChannels; ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    pushParametersToEngine();

    // M3 GUI metering, input side: the block's peak level BEFORE the engine
    // touches it (silentium's AnalogMeter pattern - see PluginProcessor.h's
    // docs). getMagnitude() is a simple allocation-free scan; skipped for
    // zero-sample/zero-channel blocks so the last real level holds rather
    // than collapsing to the floor every offline-render boundary.
    const auto numSamples = buffer.getNumSamples();

    if (numSamples > 0 && buffer.getNumChannels() > 0)
        currentInputLevelDb.store (juce::Decibels::gainToDecibels (buffer.getMagnitude (0, numSamples), -100.0f),
                                   std::memory_order_relaxed);

    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);

    // Output side, read after process() so it reflects the fully-voiced wet
    // (or dry/wet-mixed) signal actually leaving the plugin this block.
    if (numSamples > 0 && buffer.getNumChannels() > 0)
        currentOutputLevelDb.store (juce::Decibels::gainToDecibels (buffer.getMagnitude (0, numSamples), -100.0f),
                                    std::memory_order_relaxed);

    // Publish the (possibly just-changed) latency for LatencyReporter: only
    // the message thread may call setLatencySamples() - see PluginProcessor.h.
    pendingLatencySamples.store (engine.getLatencySamples(), std::memory_order_relaxed);
}

//==============================================================================
bool TenebraeAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* TenebraeAudioProcessor::createEditor()
{
    return new TenebraeAudioProcessorEditor (*this);
}

//==============================================================================
void TenebraeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = apvts.copyState();
    const std::unique_ptr<juce::XmlElement> xml (state.createXml());

    // Explicit schema stamp (brief section 4). v0.1/v0.2 states carry no
    // stateSchema attribute at all; v0.3.0 writes "3". Migration itself is
    // purely additive - a state that predates a parameter simply does not
    // mention it, and APVTS falls back to that parameter's (neutral)
    // default - so nothing reads this attribute to decide how to load. It
    // exists so that a future schema change which is NOT purely additive has
    // an unambiguous discriminator to branch on, and so that a state file can
    // be identified out of band.
    xml->setAttribute (stateSchemaAttribute, stateSchemaVersion);

    copyXmlToBinary (*xml, destData);
}

void TenebraeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState != nullptr && xmlState->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TenebraeAudioProcessor();
}
