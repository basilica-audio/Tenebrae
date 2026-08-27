# M3 photoreal GUI — parameter mapping (ritual design)

Status: implementation complete, **owner visual sign-off pending** — see the
PR. Merge is gated on Yves looking at `docs/gui-preview.png` (and ideally the
live plugin) and approving the look.

## Design source

`brand/mocks/ritual/` (repo-relative to the suite root, one level above this
repo): `master-01-base.png` (production background, embedded as
`resources/gui/master_ritual.png`), `components/needle-{left,right}.{png,json}`,
`layout-manifest.json`.

Tenebrae has 26 automatable-or-not APVTS parameters (see
`src/params/ParameterIds.h`). The ritual design provides exactly **4** knob
positions (all 4 "rune-capped" knobs on one shared row) and **2** VU dials —
far fewer physical controls than aureate's tubecomp pilot (10 knobs, 4
toggles). The rest of Tenebrae's parameters remain fully functional via
automation, host generic UI, and presets; nothing is removed from the APVTS.

## Knob mapping (4 of 26 parameters)

| # | Position (master px cx, cy, r) | Parameter | Why here |
|---|---------------------------------|-----------|----------|
| 1 | (339, 558, 63) | **Gain** (`gain`) | The single most-reached-for control of any distortion plugin — "how much distortion." Pre-cascade, first in signal flow, leftmost knob. |
| 2 | (578, 560, 58) | **Bass** (`bass`) | Tone-stack low band. |
| 3 | (808, 549, 62) | **Mid** (`mid`) | Tone-stack mid band. |
| 4 | (1055, 578, 58) | **Treble** (`treble`) | Tone-stack high band. |

**Rationale**: Gain + a 3-band Bass/Mid/Treble tone stack is the single most
universally recognisable "4-knob high-gain channel" layout in the whole
reference class (matches a classic amp channel or a preamp-in-a-box far more
directly than any other 4-parameter subset of Tenebrae's 26) — every one of
these four is a control a player reaches for constantly, in real time, while
actually playing. This was chosen over including `level` (output trim) or
`mix` (dry/wet) in the physical row, both of which are more "set once per
session" gain-staging controls than "reached for while playing" tone
controls, and over `tight` (pre-cascade HPF), which is genuinely
genre-defining but is more of a per-guitar/per-tuning "set and forget" than a
continuously-adjusted control the way the 4 chosen knobs are.

Rotation convention: `MasterCropKnob`'s ±135° sweep maps parameter
proportion 0.5 → 0° (12 o'clock, the master's own baked rest pose). Gain
defaults to 24 dB of 0–40 dB (proportion 0.6, a slight clockwise rest);
Bass/Mid/Treble all default to 0 dB of ±15 dB (proportion 0.5 exactly, so
all three rest knobs render pixel-identical to the baked master at their own
own default — the honest "reset to true neutral" case, unlike Gain).

### Not wired to a knob (automation/preset-only)

Every one of the following remains a fully functional, automatable,
saveable/preset-able APVTS parameter — simply without a dedicated physical
control in this pass:

- **Tight** (`tight`) — pre-cascade HPF; genre-defining but "set once per
  guitar/tuning," honestly explained above as the closest runner-up that
  still didn't make the physical cut.
- **Level** (`level`), **Mix** (`mix`) — output trim and dry/wet; gain-staging
  controls, not real-time tone-shaping controls.
- **Voicing** (`voicing`), **Bright** (`bright`), **Tone Voice** (`toneVoice`)
  — discrete switches (Tight/Loose cascade, bright pre-emphasis, EQ tilt
  shapes); no baked switch art exists on this design's plate for them.
- **Presence** (`presence`) — post-cascade shelf; a real 4th/5th tone control
  in spirit, but Bass/Mid/Treble already claims all 3 EQ-shaped knob slots
  this design offers, and Gain is non-negotiable as #1.
- **Gate** (`gateThreshold`, `gateAttack`, `gateHold`, `gateRelease`,
  `gateOn`, `gateKey`, `gateHysteresis`, `gateRange`, `gateReleaseMode`) —
  9 parameters, all preset/automation-only. A tight gate is genuinely a
  structural expectation of this genre (see `docs/design-brief.md`), but
  wiring even one gate control would have meant dropping one of Gain/Bass/
  Mid/Treble, and this pass judged the 4-knob amp-channel archetype more
  valuable than a partial gate control that couldn't cover the whole
  gate section anyway.
- **Engine** (`engine`), **Quality** (`quality`) — both explicitly
  non-automatable already (changes reported latency); no natural rotary/
  toggle shape on this plate regardless.
- **Bias Shift** (`stageBias`), **Power Amp** (`powerAmp`), **Resonance**
  (`resonance`), **Sag** (`sag`) — the v0.3.0 Triode-engine block; all four
  are inert at their neutral defaults unless Engine is switched to Triode
  (itself automation-only here), making them a reasonable set to leave
  off the physical panel in this pass.

## VU dials

**Left dial = input level, right dial = output level** (classic distortion-
unit In/Out metering) — `TenebraeAudioProcessor::getCurrentInputLevelDb()` /
`getCurrentOutputLevelDb()`, both new atomics added to the processor for
this GUI (see `src/PluginProcessor.h`/`.cpp`): a relaxed-atomic peak-
magnitude reading, captured once per block on the audio thread
(`buffer.getMagnitude()`, allocation-free) — input measured before the
engine touches the buffer, output measured after — and read by the editor's
own 30 Hz polling timer. Same real-time-safety pattern as sibling
`basilica-audio/silentium`'s `AnalogMeter` metering.

**0 VU = -18 dBFS** (this suite's "Standard-A" convention): `needleDb =
processorLevelDbFS - (-18) = processorLevelDbFS + 18`, so a comfortable
mixing level reads near the dial's own printed "0" rather than pinned at the
bottom of the scale.

### Measured dB→angle tables

Measured directly against this repo's own shipped master
(`resources/gui/master_ritual.png`) by `analysis/measure_dial_ticks.py` — a
minimum-luminance-in-a-narrow-angular-window scan along each dial's own
tick-mark radius band (112–122 master px from the needle's own hub pivot),
cross-validated by directly overlaying the resulting candidate angle as a
radial line on a zoomed crop and visually confirming it tracks the printed
tick's own length axis (the same method basilica-audio/aureate's own
`measure_dial_ticks.py` uses for its single tubecomp dial, generalised here
to two independently-measured dials — see that script's own docstring for
the full method, and its `SEARCH_WINDOWS` for the per-tick angular windows).
0° = straight up, positive = clockwise.

**Left dial** (pivot 473.54, 384.0 master px):

| dB | Angle (deg) |
|----|-------------|
| -20 | -45.9 |
| -10 | -30.1 |
| -7 | -18.8 |
| -5 | -8.2 |
| -3 | -1.2 |
| 0 | 13.8 |
| +1 | 18.0 |
| +2 | 27.0 |
| +3 | 36.0 |

**Right dial** (pivot 926.84, 384.0 master px):

| dB | Angle (deg) |
|----|-------------|
| -20 | -43.6 |
| -10 | -31.7 |
| -7 | -21.5 |
| -5 | -8.2 |
| -3 | 1.6 |
| 0 | 14.1 |
| +1 | 18.4 |
| +2 | 26.8 |
| +3 | 36.0 |

The two tables are **close but genuinely not identical** — a real,
independently-measured per-dial difference (consistent with
`layout-manifest.json`'s own note that the two dials' measured radii differ
by ~4.8%, flagged there as "a genuine (if small) asymmetry in the render
rather than a measurement error"), not a copy/paste of one table into two
slots. `HubNeedle` was generalised from the aureate pilot (which hardcodes a
single implicit tick table) to take its tick table as an explicit
constructor parameter specifically to carry this — see `src/gui/HubNeedle.h`'s
own "GENERALISATION FROM THE AUREATE PILOT" docs.

**Known, documented consequence**: the dial's own labelled range is -20..+3
VU; at 0 VU = -18 dBFS, the dial's usable window covers roughly -38 dBFS
(bottom) to -15 dBFS (top of the red zone) at the processor's own metering
point — comfortably covers typical guitar-tracking gain-staging, but a
signal driven hot into this high-gain distortion (e.g. post-cascade, pre-
Level) can and will pin the needle at the dial's own "+3" end during heavy
playing, exactly like a real analogue VU meter under the same conditions.

## Dial-backlight breathing

No separate glow/diff sprite asset exists for this design (unlike the
tubecomp/aureate design's vent-glow sprite), so this is a **new**,
ritual-specific addition to the component family: `src/gui/DialBreathing.h`
precomputes a circularly-feathered, flatly-darkened crop of each dial's own
interior directly from the master render (combining `MasterCropKnob`'s own
circular-feather-mask technique with `SubtractiveGlow`'s cross-blend-at-
opacity draw model — both reused conceptually, `SubtractiveGlow.h`'s
`stepGlowMix()`/`GlowMixState` reused directly, unmodified).

- **Amplitude**: ±3–4% (`dialBreathingMaxDarkenFraction = 0.04`) — subtle,
  never a heavy-handed darken.
- **Direction (binding rule)**: only ever **darkening** from the master's own
  baked pixels — the master render is the hard ceiling (t=1 is a true no-op,
  zero-alpha draw; see `DialBreathing.h`'s own docs and
  `tests/gui/EditorSnapshotTests.cpp`'s "never renders brighter than the
  baked master" regression, exercised through the real editor).
- **Idle**: multi-sine wander (`Flicker.h`'s `slowDriftLayers`, reused via
  `stepGlowMix`) across most of the breathing range even at total silence —
  this suite's "idle flicker must never read as fully off" rule.
- **Signal response**: each dial's own breathing is driven by that SAME
  dial's own level reading (left dial ← input level, right dial ← output
  level) and **pushes toward the t=1 ceiling** (i.e. less darkening, a
  livelier/brighter-reading breathing pattern) as signal passes through —
  the same convention aureate's own vent-glow breathing uses for its gain-
  reduction reading ("the section is alive/working" reads as brighter,
  within the hard ceiling). This is a deliberate direction choice,
  documented explicitly here: "stronger with signal level" was interpreted
  as *more energetic/livelier* breathing under signal, not literally deeper
  darkening under signal — reusing the suite's own established convention
  rather than inventing a new, less-precedented one.

## Component family (ritual-specific additions vs. reused-verbatim)

Copied **verbatim** from `basilica-audio/aureate` (`feat/m3-photoreal-gui`):

- `src/gui/MasterCropKnob.{h,cpp}` — feathered circular master-crop rotary
  knob. Used unmodified for the whole-disc-rotation rune knobs (crop radius
  94% content fraction, feathered edge, so the plate shadow around each
  knob stays part of the static master).
- `src/gui/Flicker.h` — irregular multi-sine flicker/breathing primitive.
- `src/gui/SubtractiveGlow.{h,cpp}` — kept for its `stepGlowMix()`/
  `GlowMixState` ballistics (reused by `DialBreathing`); its own
  `SubtractiveGlow` class is not directly wired into this design (no vent-
  glow-shaped element exists on the ritual plate) but is kept intact for
  suite family consistency and future reuse.

**Generalised** from the aureate pilot (see each header's own "GENERALISATION"
docs for the exact diff/rationale):

- `src/gui/HubNeedle.{h,cpp}` — the tick table is now a constructor
  parameter (`std::vector<Tick>`) rather than a hardcoded file-local
  constant, because this design's two dials measure to genuinely different
  angles (see above). `tickAngleDegreesForDb()` stays a static, independently
  testable pure function, now taking the table as an explicit parameter.

**New, ritual-specific**:

- `src/gui/DialBreathing.{h,cpp}` — dial-interior breathing darkening (see
  above); not present in the aureate pilot at all (no equivalent element on
  that design).

**Not reused**: `ToggleZoneSwap.h` (aureate's toggle-lever crop-swap helper)
— the ritual design bakes no toggle switches on its plate, so there is
nothing for it to swap.

## Typography pass (suite typo phase)

Owner decision 2026-07-26: lettering is never AI-baked - it is set locally
as a sharp JUCE text layer in the suite serif (EB Garamond via BinaryData,
OFL). Implementation: `src/gui/PlateTypography.h` (copied verbatim from
the aureate pilot's typography pass), drawn last within `paint()`.

The ritual design's four rune knobs carry baked sigils, not function
names - this pass adds one gilded label per knob (`GAIN` / `BASS` / `MID`
/ `TREBLE`) on the plate's own bottom ledge (master y ~662..688), the only
clean lettering surface this heavily-sculpted plate offers (everything
between the dials and knobs is thorn/vine relief). Aged gold with a dark
drop shadow rather than dark engraving ink: the ledge is dark aged bronze
(luminance ~40..80), where an incision-ink read vanishes - and gold
lettering matches the brand's antique-gold-on-charcoal system. The VU
dials keep their baked numerals/wordmark (this master DID render those
legibly) - no changes there.

Tests: `tests/gui/EditorTypographyTests.cpp`.
