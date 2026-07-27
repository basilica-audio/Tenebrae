#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/TenebraeEngine.h"
#include "presets/PresetManager.h"

// Tenebrae: cascaded, oversampled high-gain distortion for symphonic-metal
// rhythm guitar. Signal flow lives in TenebraeEngine (src/dsp) so it stays
// unit-testable independent of this AudioProcessor; this class is just
// APVTS + host plumbing around it.
class TenebraeAudioProcessor final : public juce::AudioProcessor
{
public:
    TenebraeAudioProcessor();
    ~TenebraeAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // APVTS-root attribute stamped on every state this version saves - see
    // getStateInformation(). States written by v0.1/v0.2 carry no such
    // attribute; loading them is unaffected, because migration is purely
    // additive (missing IDs fall back to their neutral defaults).
    static constexpr const char* stateSchemaAttribute = "stateSchema";
    static constexpr const char* stateSchemaVersion = "3";

    juce::AudioProcessorValueTreeState apvts;

    // M2 preset system (.scaffold/specs/preset-system-m2.md,
    // src/presets/PresetManager.h). Constructed after apvts (its
    // constructor registers APVTS parameter listeners) and public so
    // TenebraeAudioProcessorEditor's PresetBar can talk to it directly - the
    // same "processor owns it, editor references it" pattern apvts itself
    // already uses.
    basilica::presets::PresetManager presetManager;

private:
    TenebraeEngine engine;

    // Raw atomic pointers into the APVTS-managed parameter values, resolved
    // once at construction time so processBlock() never has to search for
    // them (no allocation/locks on the audio thread).
    std::atomic<float>* tightHz = nullptr;
    std::atomic<float>* gainDb = nullptr;
    std::atomic<float>* bassDb = nullptr;
    std::atomic<float>* midDb = nullptr;
    std::atomic<float>* trebleDb = nullptr;
    std::atomic<float>* levelDb = nullptr;
    std::atomic<float>* mixPercent = nullptr;
    std::atomic<float>* voicingChoice = nullptr;
    std::atomic<float>* brightToggle = nullptr;
    std::atomic<float>* toneVoiceChoice = nullptr;
    std::atomic<float>* presenceDb = nullptr;
    std::atomic<float>* gateThresholdDb = nullptr;
    std::atomic<float>* gateAttackMs = nullptr;
    std::atomic<float>* gateHoldMs = nullptr;
    std::atomic<float>* gateReleaseMs = nullptr;
    std::atomic<float>* gateOnToggle = nullptr;

    // v0.3.0 additions.
    std::atomic<float>* engineChoice = nullptr;
    std::atomic<float>* qualityChoice = nullptr;
    std::atomic<float>* stageBiasPercent = nullptr;
    std::atomic<float>* powerAmpToggle = nullptr;
    std::atomic<float>* resonanceDb = nullptr;
    std::atomic<float>* sagPercent = nullptr;
    std::atomic<float>* gateKeyChoice = nullptr;
    std::atomic<float>* gateHysteresisDb = nullptr;
    std::atomic<float>* gateRangeDb = nullptr;
    std::atomic<float>* gateReleaseModeChoice = nullptr;

    // Pushes the engine's parameter values across. Shared by prepareToPlay()
    // and processBlock() so the two can never drift apart.
    void pushParametersToEngine();

    //==========================================================================
    // Latency renegotiation (brief section 3.3/risk 4).
    //
    // Engine and Quality both change the oversampling factor and therefore
    // the reported latency, but juce::AudioProcessor::setLatencySamples() is
    // a message-thread call (it notifies every host listener). The engine
    // publishes its current latency into an atomic from the audio thread and
    // this timer re-reports it from the message thread. Both parameters are
    // flagged non-automatable, so this path only ever runs on a deliberate
    // user switch, never per block during a render.
    class LatencyReporter : public juce::Timer
    {
    public:
        explicit LatencyReporter (TenebraeAudioProcessor& ownerToUse) : owner (ownerToUse) {}
        void timerCallback() override;

    private:
        TenebraeAudioProcessor& owner;
    };

    std::atomic<int> pendingLatencySamples { 0 };
    LatencyReporter latencyReporter { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TenebraeAudioProcessor)
};
