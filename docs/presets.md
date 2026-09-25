# Factory presets

Thirteen factory presets ship with Tenebrae, embedded via BinaryData from
`presets/factory/*.json` (see `docs/preset-system-notes.md`-equivalent CMake
wiring in this repo's `CMakeLists.txt`). All are engineered starting points
against the v0.2.0 parameter set introduced in `docs/design-brief.md`'s
"Factory Presets" section (section 7) - see that document's own honesty
section for what these numbers are and aren't calibrated against (research/
forum/manual-derived reasoning, not measured hardware). No preset name
references any manufacturer or artist.

| Preset | Category | Intent |
|---|---|---|
| **Default** | Init | Startup state; identical to Foundation Chug. The literal preset `PresetManager::applyStartupDefault()` resolves to on a fresh plugin instance (basilica-audio/Tenebrae#47) - see "Note on 'Default' resolution" below. |
| **Foundation Chug** | Init | The plugin's own default voicing, unchanged from v1's defaults (Gate added at its own default-on state) - a neutral starting point, and the preset **Default** (above) is copied from. Its parameter values are identical to `ParameterLayout.cpp`'s built-in defaults **except `Level`**, which carries the -6.47 dB headroom trim of issue #45 (see "Level trims and the fresh-instance level" below). |
| **Low-Tuned Percussive** | Guitar | Tighter low end (Tight 130 Hz) and a hotter, faster-releasing gate for down-tuned rhythm work, where string noise/rumble is worst per the research (`docs/research-notes.md` section 7). |
| **Vintage Cascade** | Guitar | Leans on the Loose voicing for a wider-band, less modern-tight character; Presence pulled back to match. |
| **Scooped Wall** | Guitar | Tone Voice = Scoop, leaning into the "smiley curve" high-gain rhythm shape already documented in `ToneStack.cpp`'s tilt table, paired with a slightly hotter Presence since Scoop's own treble tilt is modest. |
| **Cut-Through Lead-Adjacent** | Guitar | Tone Voice = Boost (mid-forward) with Bright engaged pre-cascade; Presence pulled back to avoid stacking two upper-mid pushes in the same region. |
| **Bright Aggressive** | Guitar | Bright engaged pre-cascade, paired with a pulled-back Treble/Presence post-cascade to avoid fizz - consistent with the research's "High Presence at high preamp gain is a common source of the 'fizzy digital sound'" warning. |
| **Loose & Open** | Guitar | The Loose voicing pushed further toward its own character: lower gain, wider tone-stack settings, a much longer gate release since Loose's own interstage filtering is already less aggressive. |
| **Full Dry/Wet Blend** | Guitar | A parallel-distortion starting point, demonstrating Mix (55%) as a creative control rather than always-100%-wet - the plugin's own documented default rationale for Mix=100% notwithstanding, this preset is the intentional counter-example. |

## Note on "Default" resolution

`PresetManager::applyStartupDefault()` looks for a factory or user preset
literally named `"Default"`, and since basilica-audio/Tenebrae#47 (Option 1)
this repo's factory bank ships one: a byte-for-byte copy of Foundation Chug,
`presets/factory/default.json`. On a fresh plugin instance with no user
"Default" preset yet, resolution finds this factory preset and loads it -
`PresetBar` now shows "Default" as the current preset name out of the box,
rather than "Init" (an empty current-preset name) as it did before this
preset existed. Foundation Chug remains a separate, explicitly-selectable
entry in the factory bank carrying the identical parameter values; picking it
by name from the preset menu behaves exactly as it always has.

A user can still override the startup preset via the preset menu's "Set
current as default", which writes a user preset file literally named
"Default" (see `PresetManager.h`'s `setCurrentAsDefault()`) - user presets
are resolved before factory ones (see `PresetManager::loadPreset()`), so a
user "Default" always wins over the factory one. `resetDefault()` removes the
user override; resolution then falls back to the factory "Default" again,
not to the raw `ParameterLayout.cpp` defaults, since the factory preset is
always there to be found.

Restoring a saved session is unaffected either way: `AudioProcessor::
setStateInformation()` overwrites whatever the startup preset applied and
does not go through `PresetManager` at all (see `PluginProcessor.cpp`).

## Level trims and the fresh-instance level

Every factory preset's `Level` value is gated by
`tests/PresetHeadroomTests.cpp`: rendered through the real processor at 48 kHz
against the suite reference programme (four plucked notes spanning E1 41.203 Hz
to A5 880.000 Hz, twelve harmonics each, peak-normalised to -12 dBFS), a factory
preset's output peak must stay below 0 dBFS. Nine presets needed a trim to get
there; each trim is exactly that preset's own measured overshoot plus a -0.3 dBFS
headroom target, rounded up to the parameter's 0.01 dB step, and **nothing else
in the preset changed** - `Level` is an output trim, so this changes how loud a
preset is and not how it sounds. Presets already below the target were not
raised: the gate is a ceiling, not a level-matching target.

**A fresh instance is now covered by that gate.** Since basilica-audio/
Tenebrae#47 (Option 1), the factory bank ships a preset literally named
"Default" (a copy of Foundation Chug), so `applyStartupDefault()` is no longer
a no-op: a fresh instance loads it and inherits the same -6.47 dB `Level`
trim, so out of the box the plugin sits below 0 dBFS on the reference
programme rather than pushing it to +6.16 dBFS. `tests/PresetHeadroomTests.cpp`
asserts this directly (`[presets][headroom]`, "a fresh instance's own startup
state stays below 0 dBFS"), on top of the mid-session-recall gate that already
measured this state as its departure point.

The `level` parameter's own *default* (0 dB, `ParameterLayout.cpp`) is
deliberately left unchanged - it is still what `T-S1` uses to render a v0.2.0
session state, and moving it would silently re-level every existing session
that predates the parameter. That is safe precisely because a restored
session never runs through the startup preset in the first place:
`setStateInformation()` overwrites whatever `applyStartupDefault()` applied,
so this change is invisible to a session save/reload and only changes what a
brand-new plugin instance sounds like before the user touches anything.

A user can still make any preset (including Foundation Chug) the literal
startup default via the preset menu's "Set current as default", which writes
a user preset file literally named "Default" (see `PresetManager.h`'s
`setCurrentAsDefault()`).

## v0.3.0 additions — the Triode engine bank

Four presets that put the new engine, the power-amp block and the gate's new capabilities to work.
All four select **Engine = Triode**; the eight presets above are unchanged and still run the Classic
engine, exactly as they did in v0.2.0.

| Preset | What it is |
|---|---|
| **Triode Foundation** | The reference Triode voice and the right place to start. Standard quality, no power amp, a slightly higher Tight corner than Foundation Chug, and the gate keyed pre-distortion with a few dB of hysteresis so it separates playing from not-playing properly at high gain. |
| **Sagging Doom** | Loose voicing, low Tight corner, power amp on with deep Resonance and heavy Sag, and Bias Shift pushed past neutral. Slow, spongy and heavy - the note blooms and the amp visibly recovers between hits. Gate Range is finite rather than Mute so long decays fade instead of being switched off. |
| **Feedback Tight Rhythm** | The opposite end: high Tight corner, Bright on, moderate Resonance and a lot of Presence, with the power amp's feedback loop doing the top-end shaping rather than the EQ. Tight, modern and articulate. |
| **Adaptive Gate Chug** | Eco quality (lowest latency, for tracking), pre-distortion key, hysteresis, and **Gate Release Mode = Auto** - the gate works out for itself whether a note stopped or is decaying. Built for fast palm-muted parts where a single fixed release time cannot cover both. |
