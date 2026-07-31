#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <vector>

// Suite-reusable pivot-centred VU-needle overlay - the tubecomp-family
// faceplate design's needle component, piloted in basilica-audio/aureate
// (src/gui/HubNeedle.{h,cpp} there) and copied here for the "ritual" design's
// M3 GUI (this repo, Tenebrae).
//
// GENERALISATION FROM THE AUREATE PILOT: aureate's own HubNeedle hardcodes a
// single dB->angle tick table as a file-local anonymous-namespace constant,
// because that design bakes exactly one VU dial. The ritual design bakes
// TWO dials (input/output) which measure to slightly different angles per
// tick (see docs/gui-mapping.md's measured tables, analysis/
// measure_dial_ticks.py) - both real, independently-measured differences,
// not a copy/paste of one table. Rather than hand two near-duplicate static
// tables and pick between them with an enum (fragile, easy to wire wrong),
// this revision generalises the tick table to a constructor parameter
// (`std::vector<Tick>`), so each HubNeedle instance owns its own measured
// table explicitly, and tickAngleDegreesForDb() is a static, table-in-param
// pure function - independently testable exactly like aureate's original,
// just without a single implicit global table baked into the class.
//
// The dial FACE (plate, bezel, tick marks, "VU" wordmark) is baked into the
// design's master render (resources/gui/master_ritual.png) - this component
// draws ONLY the live needle sprite on top of it, rotated about the sprite's
// own measured hub pivot via a live juce::AffineTransform (never a
// pre-rotated frame stack - see the needle-{left,right}.json files'
// provenance notes for why the master-extraction pipeline deliberately does
// not rotate the sprite to a canonical pose: doing so would resample and
// soften the master's own pixels).
//
// CRITICAL (binding rule, see the M3 GUI briefing): the sprite's pivot is
// the needle's HUB CENTRE, not the visible rod end - components/needle-
// {left,right}.json's own pivotXInMaster/pivotYInMaster fields already
// encode this, and this component's pivotXFraction/pivotYFraction
// constructor parameters must be derived from that same point (never the
// rod end), or the needle base will visibly lift off its hub as it rotates.
namespace basilica::gui
{
    class HubNeedle : public juce::Component
    {
    public:
        struct Assets
        {
            // The master-extracted needle sprite (needle_left_ritual.png /
            // needle_right_ritual.png) - PIVOT-CENTRED canvas (pivot sits at
            // the sprite's own exact canvas centre, fraction 0.5/0.5 - see
            // the needle json's pivotXFrac/pivotYFrac), so no additional
            // pivot-offset maths is needed when rotating it about its own
            // centre.
            juce::Image needleSprite;
        };

        // One dB->angle tick-table entry. Degrees are clockwise from
        // straight-up (12 o'clock), matching the needle json's own
        // bakedAngleConvention.
        struct Tick
        {
            float db;
            float deg;
        };

        // pivotXFraction/pivotYFraction: where the needle's hub pivot sits,
        // as a fraction of this component's own local bounds - measured
        // once against the master render (see PluginEditorLayout.h's
        // needlePivot docs) and passed in here, so this component itself
        // carries no design-specific geometry beyond the dB->angle tick
        // table passed in below.
        //
        // spriteSizeFraction: the needle sprite's own drawn diameter, as a
        // fraction of jmin(width,height) of this component's bounds.
        //
        // bakedAngleDegIn: the sprite's own rest pose in the master render
        // it was extracted from (needle json's bakedAngleDeg) - rotation
        // applied each frame is (targetAngle - bakedAngleDegIn), NOT
        // targetAngle alone (see paint()'s docs).
        //
        // tickTableIn: this instance's own measured dB->angle table (see
        // docs/gui-mapping.md and analysis/measure_dial_ticks.py) - copied
        // once at construction (message-thread only, never touched again),
        // not real-time-relevant.
        HubNeedle (Assets assetsIn, juce::String accessibleTitle,
                  float pivotXFraction, float pivotYFraction, float spriteSizeFraction,
                  float bakedAngleDegIn, std::vector<Tick> tickTableIn);
        ~HubNeedle() override;

        // Thread-safe (plain atomic store): the instantaneous value in dB,
        // written from the audio thread (or the editor's own polling
        // timer). Ballistic smoothing is applied separately, on the GUI
        // thread, so this is real-time safe to call from anywhere.
        void setTargetDb (float newTargetDb) noexcept { targetDb.store (newTargetDb, std::memory_order_relaxed); }

        // Advances the ballistic smoothing by dtSeconds and repaints if the
        // smoothed value changed meaningfully - called from the editor's own
        // timer (see PluginEditor.cpp), NOT owned internally by a
        // juce::Timer on this component, so headless tests can drive it
        // deterministically without a running message loop.
        void tick (float dtSeconds) noexcept;

        // Test/preview-only: seeds both the raw target and the ballistic-
        // smoothed reading to the same value immediately, bypassing the
        // ramp - mirrors basilica-audio/silentium's AnalogMeter::
        // setImmediateDbForPreview() rationale (headless test binaries have
        // no running message loop to pump real ticks through). Normal
        // operation never calls this.
        void setImmediateDbForPreview (float db) noexcept;

        void paint (juce::Graphics& g) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

        // One-pole ballistic integration step, exposed as a pure/static
        // function so it is directly unit-testable without a running timer.
        static float stepBallistics (float currentSmoothed, float target, float dtSeconds, float tauSeconds) noexcept;

        // dB -> face-relative rotation angle in degrees, piecewise-linearly
        // interpolated across the supplied tick table and clamped beyond
        // the table's own ends. Degrees are clockwise from straight-up
        // (12 o'clock). Static + table-as-parameter (rather than reading an
        // implicit global table) so it stays independently unit-testable
        // per dial, exactly like the aureate pilot's own single-table
        // version.
        static float tickAngleDegreesForDb (float db, const std::vector<Tick>& tickTable) noexcept;

        static constexpr float ballisticsTauSeconds = 0.25f;

    private:
        class ValueInterface;

        Assets assets;
        juce::String title;
        std::vector<Tick> tickTable;

        std::atomic<float> targetDb { 0.0f };
        float smoothedDb = 0.0f;

        const float pivotXFraction;
        const float pivotYFraction;
        const float spriteSizeFraction;
        const float bakedAngleDeg;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HubNeedle)
    };
}
