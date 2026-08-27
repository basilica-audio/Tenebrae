# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **A factory-preset headroom gate** (`tests/PresetHeadroomTests.cpp`). Every shipped
  factory preset is rendered through the real `AudioProcessor` at 48 kHz against the suite
  reference programme at −12 dBFS peak, and its output peak asserted below 0 dBFS. The
  shipped trims target −0.3 dBFS, so there is 0.3 dB between "a voicing tweak moved the
  peak" and "this gate goes red". The case also asserts the number of factory presets it
  exercised, so a preset library that stopped loading is distinguishable from every preset
  passing. A preset added later that clips this reference fails here.

  It measures **both** ways a user arrives at a preset — a restored session (state first, then
  `prepareToPlay()`, so every smoothed stage is primed at the preset's own values) and a
  mid-session click in the preset browser (parameters jump while the DSP is primed for the old
  ones). The recall path is held to "below full scale **or** below where you already were",
  which needs no tolerance constant and blames a transition only for clipping it *introduced*.
  Right now the departure state is the parameter defaults at +6.16 dBFS, so that ceiling is
  slack; it tightens to 0 dBFS automatically once the fresh-instance level is fixed.

### Changed

- **The suite now presents itself as Basilica Audio in every host.** `COMPANY_NAME` moves from
  `Yves Vogl` to `Basilica Audio`, so Tenebrae files under the brand in Logic's plugin manager,
  Cubase's vendor column and Reaper's FX browser instead of under a person's name. **Plugin
  identity is untouched** and no session is affected: the VST3 class ID derives from
  `PLUGIN_MANUFACTURER_CODE` + `PLUGIN_CODE` alone (JUCE 8.0.14, `juce_VST3ModuleInfo.h`'s
  `VST3Interface::jucePluginId`) and the Audio Unit triple stays `(aufx, <PLUGIN_CODE>, Yvsv)` -
  both diffed on a real build before and after the change. The bundle ID stays
  `com.yvesvogl.tenebrae` on purpose, because changing it is what would break existing projects, and
  `COMPANY_COPYRIGHT` still names the copyright holder rather than the trading name. See
  [`docs/branding.md`](docs/branding.md) and basilica-audio/.github ADR 0001.
- **User presets now live under `Basilica Audio`, and the ones you already saved come with them.**
  The folder moves to `~/Library/Audio/Presets/Basilica Audio/Tenebrae/` (macOS) and
  `%APPDATA%\Basilica Audio\Tenebrae\Presets\` (Windows). On first launch `PresetManager` copies
  every preset out of the old `Yves Vogl` folder into the new one. It **copies rather than moves**,
  so an older build - or a downgrade - still finds its presets where it left them, and it never
  overwrites a file already present under the new name. Nothing is deleted, ever.
- **Plugin metadata now carries the vendor URL, the copyright string, a real description and
  the VST3 sub-category.** `COMPANY_WEBSITE`, `COMPANY_COPYRIGHT` and `DESCRIPTION` were never
  set, so a shipped bundle carried an empty `NSHumanReadableCopyright`, an empty VST3 vendor
  URL, and an AU `description` that was just the plugin name again; `VST3_CATEGORIES` fell back
  to JUCE's bare `Fx` default, which filed every plugin in the suite under the same
  undifferentiated heading in a VST3 host's browser. Tenebrae now declares
  `Fx Distortion` (JUCE 8.0.14, `juce_add_plugin`). **Plugin identity is unchanged** — the VST3 class
  ID is derived from `PLUGIN_MANUFACTURER_CODE` + `PLUGIN_CODE` alone
  (`juce_VST3ModuleInfo.h`'s `VST3Interface::jucePluginId`) and the AU type/subtype/manufacturer
  triple is untouched, so existing sessions keep resolving to the same plugin.

### Fixed

- **Release notes are the changelog again, not a list of PR titles.** `release.yml` now builds the
  release body from this file's section for the tag being released, via the suite-wide
  `basilica-audio/.github/release-notes` action, and appends what a downloader actually needs: what
  each archive contains, the signing status per platform stated accurately (macOS signed, notarised
  and stapled; Windows **not** code-signed, so SmartScreen will warn), the install paths, the AU
  rescan hint, and links to the manual and the product page. A tag whose version has no section in
  this file now fails the release job rather than publishing an empty page.
- **Nine of the twelve factory presets pushed a nominally tracked programme signal past
  0 dBFS; each now carries a derived `Level` trim.** Rendered through the real
  `AudioProcessor` at 48 kHz against the suite reference programme (four plucked notes,
  E1 41.203 Hz to A5 880.000 Hz, twelve harmonics each, peak-normalised to −12 dBFS),
  eight presets clipped outright — worst *Loose & Open* at **+8.13 dBFS** — and a ninth,
  *Triode Foundation*, sat inside the 0.3 dB drift margin at −0.29 dBFS. A preset that
  clips at the level its own author must be assumed to have voiced for asks the user to
  pull the fader before they can audition it: a defect, not gain-staging taste.

  Each offending preset gets a derived `level` trim and **nothing else**, so its voicing is
  untouched. Trim = measured overshoot + a −0.3 dBFS headroom target, rounded up to the
  parameter's 0.01 dB step:

  | Preset | Before | Trim | After |
  |---|---:|---:|---:|
  | Loose & Open | +8.13 dBFS | −8.44 dB | −0.31 dBFS |
  | Low-Tuned Percussive | +7.75 dBFS | −8.06 dB | −0.31 dBFS |
  | Cut-Through Lead-Adjacent | +7.76 dBFS | −8.06 dB | −0.30 dBFS |
  | Vintage Cascade | +6.29 dBFS | −6.59 dB | −0.30 dBFS |
  | Foundation Chug | +6.16 dBFS | −6.47 dB | −0.31 dBFS |
  | Bright Aggressive | +4.63 dBFS | −4.93 dB | −0.30 dBFS |
  | Scooped Wall | +4.19 dBFS | −4.49 dB | −0.30 dBFS |
  | Full Dry/Wet Blend | +3.52 dBFS | −3.82 dB | −0.35 dBFS |
  | Triode Foundation | −0.29 dBFS | −0.02 dB | −0.31 dBFS |

  *Full Dry/Wet Blend* lands 0.05 dB below the target rather than on it because `Level` is a
  wet-path trim applied before the dry/wet mixer, so at `Mix` 55 % it does not scale the
  output quite linearly; the residual is in the safe direction and is left as measured.
  The three presets already at or below the target (*Sagging Doom* −3.75, *Adaptive Gate
  Chug* −4.33, *Feedback Tight Rhythm* −2.44 dBFS) are **not raised** — that would be
  level-matching the set, which is a taste question and stays open.

- **The manual's caveat list no longer says the binaries are unsigned.** macOS release
  bundles are Developer-ID-signed, notarised and stapled; Windows is not yet
  Authenticode-signed, which is what the caveat now says.
  The Tenebrae entry additionally claimed macOS release assets were *"blocked on an
  org-level signing-secret visibility issue"*; v0.5.0 shipped a signed, notarised and
  stapled macOS bundle.
- **The README no longer tells users the binaries do not exist.** The Installation section
  said *"No pre-built binaries are published yet"* while the banner four lines above it linked
  the Releases page, and the banner in turn described the macOS builds as *"currently
  unsigned"*. Both claims were false. The Installation section now describes the actual
  download-and-copy flow, and the banner states what the release workflow actually produces:
  verified against the shipped `v0.5.0` `.component` with `codesign --verify --strict`
  (`Developer ID Application: Yves Vogl (M5WT732AY5)`), `spctl -a -t open`
  (`source=Notarized Developer ID`) and `stapler validate`.
- **`docs/presets.md` no longer pins its factory-preset count to a superseded release**; `presets/factory/` holds 12.
- **Removed committed scratch/diagnostic test files** that were documented in their own
  headers as throwaway probes: `tests/ScratchDebugTests.cpp`.

### Added

- **A `Documentation` section in the README** pointing at the user manual, the factory-preset
  reference, the changelog and the product page — the manual was only reachable from a
  sentence in the middle of the Signal flow section.

## [0.5.0] - 2026-08-20

An accessibility release. The ritual faceplate's four rune knobs are now reachable and
operable from the keyboard alone, with step sizes a person can actually use. Nothing
about the audio changes: a v0.4.0 session renders byte-identically, and both engines'
reported latency is untouched.

### Added

- **WAI-ARIA-style keyboard stepping on the four rune knobs** (`src/gui/KeyboardSteps.h`,
  PR #32). Arrow moves 1% of the control's range, Shift+Arrow 0.1% - the keyboard analog
  of the Shift-drag fine mode the knobs already had on the mouse - PageUp/PageDown 10%,
  and Home/End the range extremes. Steps are taken in the slider's proportional domain,
  so a skewed range sweeps as evenly under the arrow keys as it does under a drag, and
  the result is still snapped to the parameter's own interval grid, so quantisation is
  never violated. This needed its own helper rather than a focus flag alone: JUCE's stock
  handler steps by the raw parameter interval - 0.01 dB across Gain's 40 dB range, 4000
  presses end to end - and refuses outright the moment any modifier key is held, so
  Shift+Arrow did nothing whatsoever. Ctrl/Cmd-modified arrows are deliberately passed
  through to the host as shortcuts.
- New `tests/gui/EditorAccessibilityTests.cpp` cases pinning the contract: focus
  reachability of all four knobs and the scale button (asserted by count, so a zero-match
  loop cannot pass vacuously), and coarse/fine/page/Home/End stepping plus Ctrl/Cmd
  passthrough on Gain.

### Fixed

- **The four rune knobs (Gain, Bass, Mid, Treble) could not be reached by keyboard at
  all** (PR #32). `juce::Slider::init()` ships `setWantsKeyboardFocus(false)` (JUCE
  8.0.14, `juce_Slider.cpp:1461`) and `MasterCropKnob` never opted back in, so Tab
  skipped straight past all four, the WCAG 2.4.7 focus ring already drawn in `paint()`
  could never appear, and no key press ever reached a knob. They now take focus in
  reading order and show their ring while they hold it.

### Known limitations

- This release covers **keyboard** operation (WCAG 2.1.1, 2.4.7). Assistive-technology
  increment and decrement actions - VoiceOver's rotor, NVDA's value adjustment - never
  reach `keyPressed()`; they go through JUCE's accessibility value interface, which still
  reports the raw parameter interval as its step size. A screen-reader user therefore
  still moves Gain 0.01 dB per action. Closing that gap means giving each control a
  custom `AccessibilityHandler` carrying its own value interface, which is the next step
  and is not part of this release.

## [0.4.0] - 2026-08-19

The M3 GUI release: the generic slider grid is replaced by the photoreal "ritual"
faceplate, reusing the component family piloted by aureate's M3 GUI.

### Added

- **M3 photoreal GUI (ritual design)** (PR #29). Two VU dials (left = input level,
  right = output level, classic distortion-unit In/Out metering) at the suite's
  Standard-A calibration (0 VU = -18 dBFS), each with its own independently measured
  dB-to-angle tick table (`analysis/measure_dial_ticks.py` - the two dials are genuinely
  not identical); four whole-disc rotating rune knobs (`MasterCropKnob`, reused
  unmodified) mapped to Gain / Bass / Mid / Treble. Every other parameter stays fully
  automatable/preset-only - mapping table and rationale in `docs/gui-mapping.md`.
- New `DialBreathing` component: darkening-only dial-backlight breathing (idle multi-sine
  wander plus signal-driven liveliness) with a hard ceiling that is never brighter than
  the baked master, pinned by a dedicated regression through the real editor.
- Real-time-safe input/output level metering on the processor
  (`getCurrentInputLevelDb()` / `getCurrentOutputLevelDb()`), same pattern as sibling
  silentium.
- `HubNeedle` generalised from the aureate pilot to take its tick table as a constructor
  parameter.
- 28 new GUI tests (layout invariants, editor construction/snapshot, needle/knob
  rotation, accessibility, the breathing hard-ceiling regression) - the suite grows from
  107 to 135 cases.
- Elision-safe allocation-guard self-test and a sample-rate-matrix reprepare test
  (44.1k -> 96k -> 192k, crossing 32/2048-sample blocks and a mono/stereo bus-layout
  change; Engine/Quality deliberately left untouched, both being documented
  non-automatable, latency-affecting switches) (PR #30).

### Changed

- `docs/manual.md`: "Under the hood" engineering notes for the v0.3.0 triode engine
  (solved 12AX7 stages, power-amp feedback loop, ADAA, gate), a per-mode reported-latency
  statement, and a "Known limitations" section; the Presence parameter row now notes its
  relocation into the power-amp feedback return when Power Amp is engaged (PR #27).
- Branding: v3 flat squircle icon (no dish/ring) (PR #28).

## [0.3.0] - 2026-07-27

The "stateful triode engine" release. v0.2.0 shipped a static `tanh` cascade; v0.3.0 adds a second,
selectable engine built from solved 12AX7 stage equations with the time-variant behaviour a
memoryless waveshaper cannot produce - dynamic bias shift, blocking-distortion recovery,
frequency-dependent cathode drive and Miller interstage filtering - plus a negative-feedback
power-amp block and a substantially more capable gate.

**Nothing about an existing session changes.** All ten new parameters default to neutral, the
Classic engine's code is untouched, and a v0.2.0 session state renders *byte-identically* to a fresh
v0.3.0 instance at its defaults. That is a test, not a hope: `T-S1` in `tests/StateTests.cpp`
renders both in the same process and compares the floats bit for bit, and `T-PR2` does the same for
each of the eight original factory presets.

### Added

- **Engine** (Classic / Triode, default **Classic**, not automatable): selects the tone-generating
  core. Classic is the v0.2.0 path with its code unchanged. Triode is the new three-stage stateful
  engine. Not automatable because it changes the reported latency.
- **Triode engine** — three stateful triode stages per channel, built from the Dempwolf–Zölzer
  12AX7 model (DAFx-11, Table 1 "RSD-1" fit). At `prepare()` each stage solves its own DC operating
  point (grid at 0 V through the grid leak, cathode cap open) and then a 2048-point static plate
  curve, with the grid stopper in circuit so grid conduction compresses the positive swing the way
  the circuit does. Around that curve sit the three things a static waveshaper cannot do:
  - **dynamic bias shift / blocking distortion** — rectified grid overshoot charges the coupling
    cap fast and bleeds off over ~20 ms, biasing the stage toward cutoff after an overload. This is
    what makes palm mutes feel compressed and cranked stages "breathe" per note.
  - **cathode bypass** — Rk‖Ck only bypasses above its corner, so bass sees local feedback and stays
    cleaner while mids and treble get the full stage gain. The Tight voicing uses the 1k5 / 0.68 µF
    pairing (a real, measurable bass-versus-treble drive difference); Loose uses 820R / 25 µF, which
    bypasses the whole band. Plus a slow bloom follower.
  - **Miller interstage low-pass** — a one-pole per stage from Cin = Cgk + Cag·(1+|A|) against the
    source impedance.
  Both voicing tables (Tight/Loose) are built at `prepare()` and kept resident, so the Voicing
  switch stays a branch. Stage polarity alternates, as it does in the circuit, with a single ×(−1)
  normalisation at the cascade output so the wet path stays aligned with the dry path and with
  Classic.
- **Quality** (Eco 2× / Standard 4× / HQ 8×, default **Standard**, not automatable): oversampling
  for the Triode engine only. Eco and Standard use polyphase IIR half-bands for near-zero
  low-frequency latency; HQ uses an equiripple FIR for linear phase. All four chains (including
  Classic's 8×) are built at `prepare()` and stay resident, so switching is a pointer swap plus an
  in-place coefficient rebuild — measured at zero additional allocations.
- **First-order ADAA** on every Triode-engine shaper. The stage LUT stores its interpolant as cubic
  Hermite segments and evaluates the antiderivative as the *exact quartic antiderivative of those
  same segments*, so F1′ ≡ S identically. (Tabulating F1 independently would leave a mismatch that
  the divided difference amplifies by 1/Δ, landing broadband junk in the −60…−90 dB range.)
- **Power Amp** (off by default) with **Resonance** (0…12 dB) and **Sag** (0…100 %): a global
  negative-feedback loop around an ADAA output-transformer saturator. Resonance and Presence act as
  cut-only shelves in the *feedback return path*, which is where a real amp's Depth and Presence
  controls live — cutting the return raises the closed-loop gain in that band. Sag lets the output
  envelope squeeze the transformer's headroom, giving gain droop and recovery per note. With Power
  Amp on, Presence moves to the return path and the post-EQ shelf is structurally bypassed.
  The loop's small-signal gain is bounded at 0.5 by design — at least 6 dB of margin at *every*
  frequency up to the oversampled Nyquist — asserted at `prepare()` and gated in CI.
- **Bias Shift** (0–200 %, default 100 %): scales all three stages' dynamic-bias depths.
- **Gate v2** — a strict superset of the v0.2 gate. Four new controls, each a no-op at its default:
  - **Gate Key** (Post / Pre, default Post): keys the detector from a pre-distortion copy of the
    input through an 80 Hz – 8 kHz detector band-pass. A cascade at high gain compresses the
    30 dB difference between "noise floor" and "playing" down to a few dB by the time the gate sees
    it, which is why a post-distortion detector cannot separate them; the pre tap keeps the full
    range.
  - **Gate Hysteresis** (0–12 dB, default 0): separate open and close thresholds.
  - **Gate Range** (20–90 dB, default **Mute**): a finite closed-state floor instead of a hard mute.
  - **Gate Release Mode** (Manual / Auto, default Manual): a two-envelope race that distinguishes a
    note that stopped from a note that is decaying, fading at the note's own measured decay rate
    plus a small margin rather than at a fixed one.
- Four factory presets for the new engine: **Triode Foundation**, **Sagging Doom**,
  **Feedback Tight Rhythm** and **Adaptive Gate Chug**. The eight v0.2.0 presets are byte-untouched.
- `stateSchema="3"` is stamped on the APVTS root when saving. Migration itself stays purely additive
  (an absent ID falls back to its default), so nothing reads the attribute to decide how to load —
  it exists so a future non-additive change has an unambiguous discriminator.

### Changed

- Engine and Quality switches now reset every nonlinear chain, not only the one being switched to,
  and the swap fade is 2 ms rather than 16 samples. A chain switched away from and back to was
  resuming from seconds-old state, and 16 samples is shorter than a half-band IIR's own reset
  transient; together these took the worst mid-stream switch peak from 1.65 to 1.27.
- A non-finite input sample is now replaced at the engine boundary. Previously a single Inf or NaN
  latched permanently in the one-pole and IIR states and every subsequent block came out NaN, even
  after the input went clean again. Finite input is passed through untouched.
- The editor wraps onto a second row for the new controls. Still the plain pre-M3 layout.

### Measured

Numbers CI computes, not estimates. Alias-to-signal ratio at 36 dB pre-gain (48 kHz, 2^18 FFT,
Blackman-Harris), against the same cascade run with no oversampling and no ADAA:

| f0 | naive | Eco (2×) | Standard (4×) | HQ (8×) |
|---|---|---|---|---|
| 1244 Hz | −39.4 dB | −76.8 dB | −88.6 dB | −90.3 dB |
| 2489 Hz | −28.5 dB | −59.3 dB | −83.1 dB | −89.4 dB |
| 4978 Hz | −14.5 dB | −47.9 dB | −61.6 dB | −77.6 dB |
| 9956 Hz | −5.9 dB | −18.0 dB | −38.4 dB | −54.7 dB |

Swept-sine (20 Hz – 10 kHz at −8 dBFS, 40 dB pre-gain) worst masked residual: **−67.7 dBFS**
(Standard), **−83.0 dBFS** (HQ). Above roughly 2.5 kHz the floor is set by the stock half-band
decimation stopband rather than by the shapers; closing that needs the steeper suite-wide
oversampler that is on the roadmap, not in this release.

Also asserted: the Dempwolf equations against their published form; DC operating points
(Tight 1.475 V / 201.7 V, Loose 1.05 V / 171.9 V); Tier B against an inline Tier A reference solve
(−164 dB RMS residual at −20 dBFS); bias-shift suppression and its fitted ~20 ms recovery; the
cathode shelf against its analytic response; sample-rate invariance at 44.1 k versus 96 k; wet/dry
and Classic/Triode polarity; the power-amp loop-gain bound across the full shelf grid at every
oversampled rate; sag depth (1.9 dB) and its 5 ms / 120 ms constants; the gate's superset identity,
hysteresis, chatter immunity, range floor, program-dependent release slope and hold behaviour;
reported latency equal to measured impulse delay in every engine/quality combination; and zero
allocations added by any v0.3.0 path.

### Known issues

- `processBlock` allocates four times per block, unchanged from v0.2.0:
  `juce::dsp::IIR::Coefficients::makeXxx` heap-allocates, and the engine rebuilds the Tight
  high-pass plus the tone stack's three bands every block. Fixing it means rewriting those
  coefficient updates in place, which touches the Classic path and is therefore deferred to a change
  that can re-establish the bit-identity guarantee alongside it. `T-X1` measures this baseline and
  gates on the delta, so no new code can add to it.
- The macOS release workflow is still blocked on an org-level signing-secret visibility issue.
  Windows release assets are unaffected.

### Third-party

None added. All the mathematics is implemented from the cited papers. JUCE 8.0.14 and Catch2
v3.15.2 (BSL-1.0, tests only) are unchanged.

## [0.2.0] - 2026-07-16

Research-derived deep-dive rework against the reference class of high-gain cascaded rhythm-guitar
distortion (modern cascade-amp lineages and their software/pedal equivalents) - see
`docs/design-brief.md` and `docs/research-notes.md` for the full sourcing (the only two documents
in this repo that name specific reference products, as citations). This is an additive/corrective
pass, not a rewrite: the core clipper transfer function,
per-stage cascade constants, 8x oversampling, Tight/Loose voicing tables, and all real-time-safety
patterns are unchanged and were validated by the research as either directly consistent with the
reference class or reasonable engineering simplifications with no sourced correction available.
Also ships the suite-wide M2 preset system (this repo's implementation of the pilot pattern from
`basilica-audio/nave`) and a German frame-string localisation.

### Added

- **Presence** parameter (-12 dB to +12 dB, default 0 dB/unity): a new post-cascade, post-tone-stack
  high-shelf at a sourced 2.4 kHz corner (see `docs/research-notes.md` for the exact citation),
  modelled on the reference class's power-amp Presence feedback control's functional position (not
  its circuit). At the 0 dB default the shelf is skipped entirely (a true structural bypass, not
  merely a near-unity filter), so existing sessions/presets are unaffected on migration.
- **Gate**: a new conventional fixed attack/hold/release expander/gate, inserted after Presence and
  before Level, gating the fully-voiced wet signal (catches noise the cascade's own gain generates,
  not just input noise). Four new parameters - Gate Threshold (-80 to 0 dB, default -48 dB), Gate
  Attack (0.1-20 ms, default 1 ms), Gate Hold (0-500 ms, default 20 ms), Gate Release (5-2000 ms,
  default 150 ms) - plus a Gate on/off toggle. **Deliberately defaults to ON**: this is the single
  highest-impact structural gap the research identified in v0.1 (every reference plugin/pedal in
  this genre ships a gate as a first-class module), and shipping it off by default would silently
  reproduce that gap. This is the one v0.2.0 change with a user-facing consequence for old sessions:
  loading a pre-v0.2.0 session now engages the Gate at its default settings on top of whatever was
  saved, which may audibly change the tail/silence behaviour of that session.
- M2 preset system (`src/presets/`): factory bank of 8 presets (`presets/factory/*.json` - Foundation
  Chug, Low-Tuned Percussive, Vintage Cascade, Scooped Wall, Cut-Through Lead-Adjacent, Bright
  Aggressive, Loose & Open, Full Dry/Wet Blend), user preset save/save-as/rename/delete, single-file
  and zip-bank import/export, dirty-state tracking, and a `PresetBar` UI strip docked at the top of
  the editor. See `docs/presets.md` for what each factory preset does and `docs/preset-system-notes.md`
  for the underlying architecture (copied verbatim from the pilot implementation in
  `basilica-audio/nave`).
- German (`de`) frame-string localisation for the preset bar (labels, menus, dialogs, error
  messages), auto-selected from the host OS's system language; core DSP/parameter terminology
  (Tight, Gain, Presence, Gate, dB, Hz, ms, %, etc.) is never translated, matching the suite-wide
  i18n convention.
- `tests/GateTests.cpp` (module-level Gate ballistics/bypass/NaN coverage) and
  `tests/PresetManagerTests.cpp` (17 preset-system tests, ported from the nave pilot) - see
  "Changed" below for extensions to existing test files.

### Changed

- **Tone stack Treble corner raised from 3.5 kHz to 5 kHz** (`ToneStack.cpp`'s internal filter
  constant - not a parameter ID/range change, so existing sessions keep working, but a non-zero
  Treble setting now sounds slightly different). Reasoned (not directly sourced to one reference
  number): with the new Presence control now owning the ~2.4 kHz region, Treble sits clearly above
  it instead of stacking on the same corner Bright already occupies (3.5 kHz, unchanged, pre-cascade),
  while staying below the cascade's own top-end rolloff ceiling.
- `docs/architecture.md` and `docs/manual.md` updated for the new signal flow (Presence, Gate),
  the Treble corner change, and the preset system.
- Extended `tests/StateTests.cpp` (Presence + all four new Gate parameters + Gate on/off in the
  non-default-round-trip test, plus a new test simulating an old v0.1.0-style state tree missing
  the new parameter IDs, confirming `AudioProcessorValueTreeState` falls back to the v0.2.0
  defaults rather than failing the load), `tests/ParameterTests.cpp` (parameter count 10 -> 16,
  new sections for Presence/Gate defaults/ranges), `tests/EngineTests.cpp` (Presence boost/cut and
  true-passthrough-at-0dB tests, Gate bypass/engaged/NaN-sweep tests), `tests/RobustnessTests.cpp`
  (extreme-value sweep and the exhaustive Voicing x Bright x Tone Voice combination test both now
  also cover Presence and Gate on/off x Threshold extremes), and `tests/ToneStackTests.cpp` (Treble
  test comment updated for the new corner).
- Bumped to JUCE 8.0.14's plugin version 0.2.0; `CMakeLists.txt` now embeds the preset/i18n
  BinaryData assets and links `juce_gui_basics` into `SharedCode` (needed by `PresetBar`'s
  `AlertWindow`/`FileChooser`/`PopupMenu` usage).

### Fixed

- Housekeeping: `ci/fix-release-workflow` (create-release-before-asset-upload fix) and dependabot
  `actions/checkout` 4->7 / `actions/cache` 4->6 bumps merged ahead of this release.

## [0.1.1] - 2026-07-16

### Changed

- Housekeeping: new icon motif with canonical squircle cutout embedded into the plugin binary (`ICON_BIG`) and README/manual, org link sweep, heavy-music copy reframe, README pointed at GitHub Releases, and the signed tag-triggered release CI workflow added.

### Fixed

- `clampBelowNyquist()` (`TenebraeEngine.cpp`, duplicated in `CascadeStage.cpp`) and `ToneStack::clampCombinedGainDb()` relied solely on `juce::jlimit()`, which is not NaN-safe (both of its internal comparisons evaluate false for NaN, so a NaN input previously fell through unclamped). A NaN Tight-frequency or Bass/Mid/Treble gain reaching these from host automation could produce NaN filter coefficients that poison a filter's delay-line state (persistently on arm64, where JUCE's snap-to-zero denormal cleanup is a no-op, vs. self-healing after one block on x86_64). Both helpers now replace a NaN input with a safe default before clamping. (#14)
- `CascadeStage::driveGain`, `TenebraeEngine::preGain`, and `TenebraeEngine::outputLevel` (all `juce::dsp::Gain<float>`) called JUCE's own `Gain::process()`, whose multichannel branch `alloca()`s a scratch buffer sized to the block on every call, with no upper bound or heap fallback - `driveGain` runs on the 8x-oversampled block, making it the single largest stack allocation in the whole signal chain. All three now go through a new `RealtimeGain::process()` helper (`src/dsp/RealtimeGain.h`) that replicates JUCE's own per-sample math into a caller-owned buffer sized once in `prepare()`, eliminating the audio-thread `alloca()`. (#12)
- `TenebraeEngine::process()` never compared the incoming block's sample count against the `maximumBlockSize` declared to `prepare()`. An oversized block (some hosts occasionally hand over one - offline bounce/render, buffer-size renegotiation) went straight into `juce::dsp::Oversampling`'s internal buffer, which is sized to exactly that declared maximum and only bounds-checks with a debug-only `jassert` - silent heap corruption with no exception to catch in a Release build. `process()` now chunks any oversized block down into `prepare()`-sized pieces via a new internal `processChunk()` rather than truncating it, so no more than the declared maximum ever reaches the oversampler (or any other `prepare()`-sized internal buffer) in one call. (#13)

## [0.1.0] - 2026-07-14

### Added

- Project bootstrap: README, license, contributing guide, architecture and build docs, ADRs, and CI workflow.
- DSP core: initial working Tenebrae signal path (Tight HPF, Gain, 3-stage oversampled waveshaper cascade, 3-band tone stack, Level, delay-compensated dry/wet Mix) with unit tests.
- **Voicing** parameter (Tight/Loose): switches the fixed cascade drive/asymmetry/interstage-filter constants between the original "Tight" cascade and a new, softer-driven, wider-band "Loose" alternative. Both cascade triplets are fully preallocated so switching is allocation-free.
- **Bright** parameter: fixed pre-cascade high-shelf switch (+5 dB @ 3.5 kHz), modelled on a high-gain amp channel's bright switch, feeding a brighter signal into the cascade's nonlinearity rather than just re-EQing the already-clipped output.
- **Tone Voice** parameter (Flat/Scoop/Boost): a fixed dB tilt added on top of the live Bass/Mid/Treble tone-stack bands, for one-switch access to canned high-gain-rhythm tone shapes (a "smiley" scoop curve and a mid-forward boost curve) without overriding the individual band knobs.
- `docs/manual.md`: full user manual (what the plugin is, where it sits in a symphonic-metal chain, signal flow, complete parameter reference, usage tips).

### Changed

- Tone stack corner frequencies refined against typical high-gain rhythm voicings: Mid peak moved from 800 Hz to 650 Hz with a narrower Q (0.8 -> 1.1) for a more surgical scoop, and the Treble shelf moved from 3000 Hz to 3500 Hz to sit at a more typical amp "presence" corner.
- Broadened the Catch2 suite from 26 to 36 test cases: sample-rate sweeps (44.1-192 kHz), mono/stereo bus-layout coverage, a longer-running (400-block) NaN/Inf automation soak test, exhaustive Voicing/Bright/Tone Voice combination coverage, and dedicated tests for every new parameter (state round-trip, defaults/ranges, DSP-level effect).

### Fixed

- `TenebraeEngine::outputLevel` (the Level gain stage) was never primed with a starting gain in `prepare()`; since `juce::dsp::Gain` default-constructs its internal smoothed value to linear zero (silence) rather than unity, any `prepare()` call not immediately preceded by a fresh `setLevelDb()` call produced a permanently silent wet path. This never reached the shipped plugin (`PluginProcessor::prepareToPlay()` always seeds Level from the host/session state first) but was a real trap for anything exercising `TenebraeEngine` directly, found via the broadened mono-bus test coverage above. Fixed by re-priming `outputLevel` from a new `lastLevelDb` member on every `prepare()`, matching the existing `lastGainDb`/`lastTightHz` pattern.
