#pragma once

#include <juce_dsp/juce_dsp.h>

// juce::dsp::Gain<float>::process()'s multichannel branch (numChannels != 1)
// alloca()s a `sizeof(FloatType) * numSamples`-byte scratch buffer on the
// stack (verified against JUCE 8.0.14 source,
// juce_dsp/widgets/juce_Gain.h:135: `alloca (sizeof (FloatType) * len)`) to
// hold one block's worth of per-sample gain values before broadcasting them
// across channels via FloatVectorOperations::multiply. There is no upper
// bound or heap fallback - JUCE performs this alloca regardless of how large
// `len` is. Tenebrae's default bus layout is stereo, so every
// juce::dsp::Gain::process() call in this plugin (CascadeStage::driveGain,
// TenebraeEngine::preGain, TenebraeEngine::outputLevel) takes this branch;
// driveGain runs on the 8x-oversampled block, making it the single largest
// stack allocation in the whole signal chain (see GitHub issue #12) - a
// genuine unbounded-stack-allocation-on-the-audio-thread risk.
//
// This header replicates JUCE's own math exactly - one
// gain.processSample(1) call per sample (equivalent to JUCE's internal
// `gain.getNextValue()`: both advance the same internal SmoothedValue ramp
// by exactly one step per call) into a caller-owned buffer sized once in
// prepare(), then the identical FloatVectorOperations::multiply broadcast
// JUCE itself uses - just without the per-call stack allocation. Same idea
// as sibling plugin lancet's RealtimeCoefficients.h (route around a JUCE
// convenience path's hidden per-call cost on the audio thread).
namespace RealtimeGain
{
    // Applies `gain` to every channel of `context` in place. `scratch` must
    // be a buffer owned/sized by the caller (see
    // CascadeStage::prepare()/TenebraeEngine::prepare()), ideally with at
    // least `context`'s sample count of capacity; if it is smaller (should
    // never happen given the oversized-block guard in
    // TenebraeEngine::process(), see GitHub issue #13), the excess samples
    // are left unprocessed rather than overrunning the buffer.
    inline void process (juce::dsp::Gain<float>& gain,
                          juce::dsp::ProcessContextReplacing<float>& context,
                          float* scratch,
                          size_t scratchCapacity) noexcept
    {
        auto block = context.getOutputBlock();
        const auto numChannels = block.getNumChannels();

        // The bypassed branch and the single-channel branch of JUCE's own
        // Gain::process() never alloca (bypass only skips the ramp
        // forward; mono is already a plain per-sample loop with no scratch
        // buffer at all) - route both straight through to JUCE's own
        // implementation unmodified, and only take the manual path below
        // for the one branch (multichannel, not bypassed) that would
        // otherwise alloca.
        if (context.isBypassed || numChannels <= 1)
        {
            gain.process (context);
            return;
        }

        const auto numSamples = block.getNumSamples();
        jassert (numSamples <= scratchCapacity);
        const auto samplesToProcess = juce::jmin (numSamples, scratchCapacity);

        for (size_t i = 0; i < samplesToProcess; ++i)
            scratch[i] = gain.processSample (1.0f);

        for (size_t channel = 0; channel < numChannels; ++channel)
            juce::FloatVectorOperations::multiply (block.getChannelPointer (channel), scratch, static_cast<int> (samplesToProcess));
    }
}
