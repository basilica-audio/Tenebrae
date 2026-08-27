# Factory presets

Twelve factory presets ship with Tenebrae, embedded via BinaryData from
`presets/factory/*.json` (see `docs/preset-system-notes.md`-equivalent CMake
wiring in this repo's `CMakeLists.txt`). All are engineered starting points
against the v0.2.0 parameter set introduced in `docs/design-brief.md`'s
"Factory Presets" section (section 7) - see that document's own honesty
section for what these numbers are and aren't calibrated against (research/
forum/manual-derived reasoning, not measured hardware). No preset name
references any manufacturer or artist.

| Preset | Category | Intent |
|---|---|---|
| **Foundation Chug** | Init | The plugin's own default voicing, unchanged from v1's defaults (Gate added at its own default-on state) - a neutral starting point. Its parameter values are identical to `ParameterLayout.cpp`'s built-in defaults, so a fresh install's out-of-the-box sound matches this preset exactly even though `PresetManager::applyStartupDefault()` does not find a literal "Default"-named factory preset (see the note below). |
| **Low-Tuned Percussive** | Guitar | Tighter low end (Tight 130 Hz) and a hotter, faster-releasing gate for down-tuned rhythm work, where string noise/rumble is worst per the research (`docs/research-notes.md` section 7). |
| **Vintage Cascade** | Guitar | Leans on the Loose voicing for a wider-band, less modern-tight character; Presence pulled back to match. |
| **Scooped Wall** | Guitar | Tone Voice = Scoop, leaning into the "smiley curve" high-gain rhythm shape already documented in `ToneStack.cpp`'s tilt table, paired with a slightly hotter Presence since Scoop's own treble tilt is modest. |
| **Cut-Through Lead-Adjacent** | Guitar | Tone Voice = Boost (mid-forward) with Bright engaged pre-cascade; Presence pulled back to avoid stacking two upper-mid pushes in the same region. |
| **Bright Aggressive** | Guitar | Bright engaged pre-cascade, paired with a pulled-back Treble/Presence post-cascade to avoid fizz - consistent with the research's "High Presence at high preamp gain is a common source of the 'fizzy digital sound'" warning. |
| **Loose & Open** | Guitar | The Loose voicing pushed further toward its own character: lower gain, wider tone-stack settings, a much longer gate release since Loose's own interstage filtering is already less aggressive. |
| **Full Dry/Wet Blend** | Guitar | A parallel-distortion starting point, demonstrating Mix (55%) as a creative control rather than always-100%-wet - the plugin's own documented default rationale for Mix=100% notwithstanding, this preset is the intentional counter-example. |

## Note on "Default" resolution

`PresetManager::applyStartupDefault()` looks for a factory or user preset
literally named `"Default"`. This repo's factory bank does not ship one
(the design brief's Factory Presets section specifies exactly these eight
presets, none named "Default") - **Foundation Chug** fills that role
functionally instead: its parameter values are byte-for-byte identical to
`ParameterLayout.cpp`'s built-in defaults, so a fresh plugin instance (no
factory "Default" match, no user "Default" preset yet) falls through to
"use the `AudioProcessorValueTreeState` defaults it was already constructed
with" - which sounds exactly like Foundation Chug, because it is. The one
cosmetic difference: until the user explicitly loads "Foundation Chug" from
the preset menu, `PresetBar` shows "Init" (an empty current-preset name)
rather than "Foundation Chug" as the display name - the parameter values are
correct either way.

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
