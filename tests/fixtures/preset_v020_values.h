#pragma once

// Captured v0.2.0 factory-preset parameter values.
//
// These are the sixteen v0.1/v0.2 parameter values baked into each of the
// eight presets/factory/*.json files as they shipped in v0.2.0 - transcribed
// here, once, so that T-PR2 (tests/PresetManagerTests.cpp) can set them
// directly on a processor and compare that render against the same preset
// loaded through PresetManager.
//
// Why this shape and not a committed render: the two renders being compared
// are both produced inside this test process, so byte equality between them
// is meaningful on every platform. A committed float render would not be -
// CI runs ctest on both AppleClang/arm64 and MSVC/x64, whose libm, FP
// contraction and vectorisation differ, so a render captured on one leg can
// never be byte-equal on the other (brief section 6's platform note). And a
// v0.2 *binary's* render cannot be reproduced by the v0.3 binary across
// platforms either, so there is no cross-binary reference to compare to.
//
// Together with the byte-untouched preset JSONs and the untouched Classic
// DSP, comparing these two in-process renders is what expresses "the factory
// presets still render as they did in v0.2".
//
// If a factory preset is ever intentionally revoiced, this table must be
// updated in the same commit - the test failing is the point.
//
// It was, once, and only in one column: the `level` values below are the
// derived output-headroom trims of issue #45, not the v0.2 originals (which
// were Foundation Chug 0.0, Low-Tuned Percussive 0.0, Vintage Cascade 0.0,
// Scooped Wall -1.0, Cut-Through Lead-Adjacent -1.0, Bright Aggressive -2.0,
// Loose & Open +1.0, Full Dry/Wet Blend +2.0 dB). Every other column is
// untouched, which is the whole point of the trim: `level` is an output
// trim, so removing a preset's measured overshoot changes how loud it is and
// nothing about how it sounds. T-PR2 comparing this table against the shipped
// JSON therefore still proves exactly what it always did - that no OTHER
// parameter of an original preset has moved.
namespace PresetV020Values
{
    struct Preset
    {
        const char* displayName;
        const char* fileStem;

        // In the order of PresetV020Values::parameterIds below.
        float values[16];
    };

    inline constexpr const char* parameterIds[16] = {
        "tight", "gain", "bass", "mid", "treble", "level", "mix", "voicing",
        "bright", "toneVoice", "presence", "gateThreshold", "gateAttack",
        "gateHold", "gateRelease", "gateOn"
    };

    inline constexpr Preset presets[] = {
        { "Foundation Chug", "foundationChug",
          { 90.000000f, 24.000000f, 0.000000f, 0.000000f, 0.000000f, -6.470000f, 100.000000f, 0.000000f, 0.000000f, 0.000000f, 0.000000f, -48.000000f, 1.000000f, 20.000000f, 150.000000f, 1.000000f } },
        { "Low-Tuned Percussive", "lowTunedPercussive",
          { 130.000000f, 26.000000f, -2.000000f, 2.000000f, 1.000000f, -8.060000f, 100.000000f, 0.000000f, 0.000000f, 0.000000f, 2.000000f, -42.000000f, 1.000000f, 20.000000f, 80.000000f, 1.000000f } },
        { "Vintage Cascade", "vintageCascade",
          { 70.000000f, 20.000000f, 2.000000f, 0.000000f, -1.000000f, -6.590000f, 100.000000f, 1.000000f, 0.000000f, 0.000000f, -2.000000f, -50.000000f, 1.000000f, 20.000000f, 150.000000f, 1.000000f } },
        { "Scooped Wall", "scoopedWall",
          { 100.000000f, 28.000000f, 3.000000f, -3.000000f, 1.000000f, -5.490000f, 100.000000f, 0.000000f, 0.000000f, 1.000000f, 3.000000f, -46.000000f, 1.000000f, 20.000000f, 150.000000f, 1.000000f } },
        { "Cut-Through Lead-Adjacent", "cutThroughLeadAdjacent",
          { 80.000000f, 30.000000f, 0.000000f, 2.000000f, 2.000000f, -9.060000f, 100.000000f, 0.000000f, 1.000000f, 2.000000f, 0.000000f, -48.000000f, 1.000000f, 20.000000f, 150.000000f, 1.000000f } },
        { "Bright Aggressive", "brightAggressive",
          { 110.000000f, 32.000000f, -1.000000f, 1.000000f, -1.000000f, -6.930000f, 100.000000f, 0.000000f, 1.000000f, 0.000000f, -1.000000f, -44.000000f, 1.000000f, 20.000000f, 150.000000f, 1.000000f } },
        { "Loose & Open", "looseAndOpen",
          { 50.000000f, 16.000000f, 3.000000f, 1.000000f, 2.000000f, -7.440000f, 100.000000f, 1.000000f, 0.000000f, 0.000000f, 1.000000f, -58.000000f, 1.000000f, 20.000000f, 300.000000f, 1.000000f } },
        { "Full Dry/Wet Blend", "fullDryWetBlend",
          { 90.000000f, 30.000000f, 0.000000f, 0.000000f, 1.000000f, -1.820000f, 55.000000f, 0.000000f, 0.000000f, 0.000000f, 1.000000f, -48.000000f, 1.000000f, 20.000000f, 150.000000f, 1.000000f } },
    };

    inline constexpr int numPresets = static_cast<int> (sizeof (presets) / sizeof (presets[0]));
}
