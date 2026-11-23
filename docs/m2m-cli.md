# m2m-cli — offline test tool

`m2m-cli` runs the MICtoMIDI analysis engines on audio files or on built-in
synthetic scenarios, scores the result against ground truth, writes MIDI and
JSON reports, and gates CI on metric regressions (PLAN.md §9, Phase 2).

It is built from `tools/m2m-cli/` when `M2M_BUILD_TOOLS=ON` (the default) and
does not need JUCE:

```
cmake -S . -B build -G Ninja -DM2M_BUILD_PLUGIN=OFF
cmake --build build
build/tools/m2m-cli/m2m-cli --help
```

The harness code lives in the `m2m_testsupport` library (`tests/support/`):
signal generators, drum synths, the scenario library, WAV/MIDI/JSON I/O,
`EngineRunner` and metrics. The unit tests use the same library.

## Commands

### `run` — analyse one WAV file

```
m2m-cli run --mode <trigger|melody|beatbox> in.wav [--truth t.json] [--out out.mid]
            [--report r.json] [--block N] [--tolerance-ms T] [engine options]
```

- Reads PCM 8/16/24/32-bit or float 32/64-bit WAV (any channel count, mixed
  down to mono), runs the engine in blocks of `N` samples (default 256), then
  flushes held notes at the end.
- Prints processing time per block (mean/max), realtime factor (processing
  time / audio duration; lower is better) and the detected notes.
- `--truth`: scores the notes (see Metrics) and prints a summary.
- `--out`: writes a format-0 Standard MIDI File (120 bpm, 960 ppq).
- `--report`: writes a JSON report (run info, detected notes, metrics).

### Engine options (`run` and `eval`)

| option | meaning |
|--------|---------|
| `--set key=value` | override one engine parameter; repeatable, applied in order |
| `--detector yin\|mpm`, `--min-hz HZ`, `--max-hz HZ` | melody shorthands for `--set detector=…`, `minHz=…`, `maxHz=…` |
| `--dump-hits` (`-v`, `--verbose`) | beatbox: print every detected onset (see below) |

`m2m-cli settings --mode melody|beatbox` lists the keys. Melody:
`detector`, `minHz`, `maxHz`, `confidenceThreshold`, `medianFrames`,
`smoothingMs`, `highPassHz`, `onThresholdDb`, `offThresholdDb`, `minNoteMs`,
`changeHysteresisCents`, `minStableMs`, `releaseMs`, `retriggerEnabled`,
`retriggerSensitivity`, `attackWindowMs`, `transpose`, `follow`
(`free` | `lock`, see "Note lock"), `stability` (0..1, the Stability macro:
sets `changeHysteresisCents`, `minStableMs`, `medianFrames`, `smoothingMs`,
`minNoteMs` and `lockSettleMs` at once; later `--set` keys override single
values), `lockSettleMs`, `lockLeapSemitones`, `lockLeapMs`,
`lockRetriggerBoost`. Beatbox:
`onset.sensitivity`, `onset.refractoryMs`, `onset.minLevelDb`,
`analysisWindowMs`, `classifier` (`rules|calibrated`), `classifier.minScore`,
`gateMs`, `velocityWindowMs`, `highPassHz`. An unknown key or a bad value is
a usage error (exit 2). The effective parameters are printed and stored in
the report (`"engine"`).

`--dump-hits` prints one line per onset the beatbox engine detected, whether
or not it produced MIDI: onset time (s), delay from onset to NoteOn (ms),
class, score, accepted (`yes`/`no`, `cal` = captured as a calibration
example), velocity, the nearest truth hit with its timing error (or
`spurious`), and the full feature vector. Truth hits with no onset within
the tolerance are listed as `MISSED ONSET`. This separates the two ways a
hit can be lost: not detected (onset detector) vs. rejected or
misclassified (classifier).

### `gen` — write scenarios to disk

```
m2m-cli gen <scenario|all|all-melody|all-beatbox|all-trigger> --out <dir>
            [--sample-rate SR] [--format float32|pcm16]
```

Writes `<name>.wav` (mono, float32 by default) and `<name>.truth.json`.

### `scenarios` — list scenarios

Prints name, category and a description for each built-in scenario.

### `eval` — score an engine on scenarios (and gate regressions)

```
m2m-cli eval --mode <mode> [--scenarios a,b,c|all|all-melody|all-beatbox|all-trigger]
             [--report r.json] [--baseline b.json] [--write-baseline b.json]
             [--block N] [--tolerance-ms T] [--sample-rate SR]
```

- Builds the scenarios in memory (default: all scenarios of the mode's
  category), runs a fresh engine on each and prints a table:
  P, R, F1, onset-only F1, mean |onset error|, mean signed onset error
  (latency), false triggers per minute, octave errors, wrong pitches and
  CPU (% of one core, wall clock; informational, never gated). Scenarios
  with glides also print the glide check.
- `--report`: combined JSON report (per scenario metrics + run info + summary).
- `--write-baseline`: stores the current metrics as a baseline (see below).
- `--baseline`: compares against a stored baseline; exits **4** if any gate
  fails, printing each regression.

### Exit codes

| code | meaning |
|-----:|---------|
| 0 | success |
| 1 | I/O error (file missing, unreadable, unwritable, bad WAV/JSON) |
| 2 | usage error (bad option, unknown mode or scenario) |
| 3 | the mode's engine is not available in this build |
| 4 | metric regression against `--baseline` |

## Engines

Engines are created in exactly one place, `tools/m2m-cli/EngineFactory.cpp`
(`makeEngine (mode, settings)`), which also maps the `--set` keys onto
`MelodyParams` / `BeatboxParams`. `trigger` is the Phase 1
`LevelTriggerEngine` (note 60, -30 dBFS threshold); `melody` is
`MelodyEngine`, `beatbox` is `BeatboxEngine`, both with default parameters
unless overridden.

### Detector choice (melody)

`MelodyParams::detector` defaults to MPM. Measured on all 23 melody
scenarios (48 kHz, block 256, defaults otherwise):

| | YIN | MPM |
|---|---|---|
| scenarios at F1 = 1 | 21 / 23 | 21 / 23 (same two known limitations) |
| octave errors / wrong pitches | 0 / 6 (all in `scoop_ups`) | 0 / 6 (all in `scoop_ups`) |
| false triggers | only `scoop_ups` (70.6/min) | only `scoop_ups` (60.5/min) |
| mean note-on latency (scenarios with notes) | 31.2 ms | 21.4 ms |
| `sustained_notes` / `low_male_range` / `high_range` latency | 30.5 / 36.8 / 27.1 ms | 19.8 / 32.3 / 15.1 ms |
| CPU (mean, one core) | 1.03 % | 0.72 % |
| 22.05 kHz | `high_range` F1 0.91 | all as at 48 kHz |

MPM's normalised autocorrelation is confident once about two periods of
the tone are in the window, YIN needs the tone in the oldest half of the
window; that is the latency difference. `latencySamples()` reports
window/4 (MPM) or window/2 (YIN) plus the stability wait and one hop.
Lowering `minStableMs` to 5 ms saves another ~5 ms but breaks legato steps
and low notes (F1 0.29-0.77), so it stays at 10 ms; `medianFrames` 1 and
`attackWindowMs` 5 gain < 0.5 ms.

### Note lock (melody, `--set follow=lock`)

`MelodyParams::pitchFollow = PitchFollow::Lock` ("one note per syllable",
plugin parameter `mel_follow`). Rules (`NoteTracker`, "Note lock"):

- **Choosing the note.** A note starts as in Free mode (level + confident
  pitch), but its number comes from the pitch after it has settled: the
  note-on waits until the last `lockSettleMs` (default 15 ms) of pitch
  frames are flat (trimmed spread <= 60 cents and least-squares slope <= 4.5
  semitones/s), then sends the snapped median of that window. A scoop or
  fall into the note moves at 10-25 semitones/s, so it delays the note-on
  instead of choosing the wrong note. If the pitch never settles (wide
  vibrato from the first moment) the note is chosen 60 ms after the window
  filled, from the median of the newer half since the onset.
- **Holding.** While a note sounds, pitch movement never changes it.
- **Ending / next note.** Release (level or confidence gone, as in Free) or
  a re-articulation (amplitude re-onset, retrigger rule as in Free). Lock
  adds `lockRetriggerBoost` (0.4) to `retriggerSensitivity` (0.5 -> 0.9,
  i.e. a 5.6 dB rise instead of 12 dB), because re-articulations are its only
  note boundary; 0.5 would break `tremolo_vibrato` (+-3 dB) with false
  retriggers. A re-articulation chooses the next note afresh.
- **Big-leap escape** (legato without any amplitude dip): a pitch more than
  `lockLeapSemitones` (1.2) away from where the note is being sung (a
  100 ms one-pole average of its pitch, which follows wander and vibrato but
  pauses while the pitch heads away), for `lockLeapMs` (20 ms) and then
  settled by the same rule (and still a leap), changes the note: Note Off +
  Note On at the same sample. So whole-tone and wider legato steps come
  through; semitone legato steps need a re-articulation. Measured
  alternatives (all melody scenarios, onset window 100 ms): leap off -> mean
  F1 0.88 (legato scenarios 0.12-0.22); 1.5 -> 0.969; 1.2 -> 0.977;
  1.0 -> 0.983; 0.9 -> 0.991 (also follows semitone steps, but then Lock
  behaves like a smarter Free mode and a wobble of a semitone would change
  the note). 1.2 keeps the rule simple to explain: "a semitone needs a new
  syllable, a whole tone does not".
- **Latency.** `latencySamples()` = detection + max(lockSettleMs + one
  median frame, attack window) + one hop: 26.6 ms at the defaults (Free:
  21.6 ms). A scooped note comes later than that (until the pitch settles,
  at most ~75 ms after the first confident frame).

### Stability macro (melody, `--set stability=`)

`m2m::applyStability (MelodyParams, amount01)` (`core/include/m2m/melody/Stability.h`)
is the one mapping behind the plugin's `mel_stability` control. Piecewise
linear through three anchors; at 0.4 every value equals the `MelodyParams`
default, so the default position changes nothing:

| amount | hysteresis | minStable | median | smoothing | minNote | lockSettle | latency Free / Lock |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 (Loose) | 45 ct | 8 ms | 1 | 0 | 30 ms | 10 ms | 18.9 / 18.9 ms |
| 0.2 | 52 ct | 9 ms | 2 | 0 | 35 ms | 12.5 ms | 20.6 / 24.1 ms |
| **0.4 (default)** | **60 ct** | **10 ms** | **3** | **0** | **40 ms** | **15 ms** | **21.6 / 26.6 ms** |
| 0.6 | 70 ct | 13.3 ms | 4 | 0 | 47 ms | 20 ms | 24.9 / 31.6 ms |
| 0.8 | 80 ct | 16.7 ms | 4 | 0 | 53 ms | 25 ms | 28.3 / 36.6 ms |
| 1 (Tight) | 90 ct | 20 ms | 5 | 0 | 60 ms | 30 ms | 31.6 / 41.6 ms |

(Latency = `latencySamples()`, MPM, minHz 80, 48 kHz.) How the anchors were
chosen (Free mode, all 26 melody scenarios, one value changed at a time):
hysteresis 100+ ct loses legato semitone steps (`major_scale_legato` F1
0.09); minStableMs 25-30 pushes note-ons past the 30 ms target (mean F1
0.84 -> 0.77); medianFrames 5/7 costs `low_male_range`; any one-pole
`smoothingMs` (10 or 25 ms) made every pitch change late (mean F1 0.87 ->
0.76/0.73), so smoothing stays 0 at every position. The Tight end (90 ct /
20 ms / median 5) is the most a Free-mode user can get before legato and
latency break.

Measured (`eval`, all melody scenarios; F1 / false triggers per minute
summed over scenarios / mean signed onset error):

| mode, stability | mean F1 (50 ms) | sum FT/min | mean latency | mean F1 (100 ms window) | unsteady_sustained / _detached / _legato (50 ms) | same (100 ms) |
|---|---:|---:|---:|---:|---|---|
| Free 0 | 0.872 | 557 | 20.8 | 0.900 | 0.25 / 0.59 / 0.44 | 0.30 / 0.64 / 0.52 |
| Free 0.2 | 0.869 | 436 | 21.9 | 0.908 | 0.29 / 0.51 / 0.50 | 0.34 / 0.72 / 0.58 |
| **Free 0.4 (default)** | 0.871 | 318 | 22.4 | 0.917 | 0.38 / 0.57 / 0.41 | 0.46 / 0.80 / 0.64 |
| Free 0.7 | 0.872 | 210 | 28.4 | 0.925 | 0.53 / 0.47 / 0.40 | 0.63 / 0.77 / 0.80 |
| Free 1 | 0.852 | 340 | 33.5 | 0.929 | 0.77 / 0.53 / 0.06 | 0.92 / 0.77 / 0.73 |
| Lock 0 | 0.877 | 255 | 24.5 | 0.971 | 0.83 / 0.63 / 0.64 | 1.00 / 1.00 / 0.86 |
| Lock 0.2 | 0.849 | 288 | 26.6 | 0.971 | 0.83 / 0.63 / 0.57 | 1.00 / 1.00 / 0.86 |
| **Lock 0.4 (default)** | 0.852 | 232 | 30.4 | 0.977 | 0.83 / 0.63 / 0.50 | 1.00 / 1.00 / 0.86 |
| Lock 0.7 | 0.822 | 350 | 36.5 | 0.986 | 0.83 / 0.56 / 0.07 | 1.00 / 1.00 / 0.93 |
| Lock 1 | 0.703 | 852 | 42.2 | 0.986 | 0.83 / 0.19 / 0.07 | 1.00 / 1.00 / 0.93 |

Reading it: with a wavering voice Free mode gets better with Stability but
never right (unsteady_sustained 0.38 -> 0.77, at +11 ms), because its notes
must still follow real semitone steps. Note lock gets every note of the
unsteady scenarios right (F1 1.0 with a 100 ms onset window); at the 50 ms
window its F1 is lower only because a note-on after a scoop or a soft
re-articulation is 50-90 ms late, which the 50 ms metric counts as a miss
plus a false trigger. `scoop_ups`: Free 0.0 at either window (a short wrong
note first, 60 false triggers/min); Lock 0.0 at 50 ms but 0.83 at 100 ms
(5 of 6 notes right, ~85 ms late, no false triggers). Lock with a Tight
Stability waits longer for every note, which only the 100 ms view rewards;
so for Lock a low-to-default Stability is best, for Free a higher one.

## Scenarios

All scenarios are deterministic (fixed seeds; noise comes from an
integer-only PRNG, so audio is bit-identical across runs and compilers) and
default to 48 kHz.

| category | scenarios |
|----------|-----------|
| trigger  | `level_bursts` (440 Hz bursts, truth note 60) |
| melody   | `sustained_notes`, `chromatic_scale`, `major_scale_legato`, `vibrato`, `glide`, `repeated_same_pitch`, `weak_fundamental`, `dynamics`, `noisy_20db_snr`, `voice_formants`, `low_male_range`, `high_range`, `fast_16ths_legato`, `fast_16ths_detached`, `soft_rearticulation`, `consonant_onsets`, `pitch_drift`, `tremolo_vibrato`, `scoop_ups`, `unsteady_sustained`, `unsteady_melody_detached`, `unsteady_melody_legato`, `hum_50hz`, `hum_60hz`, `silence`, `noise_only` |
| beatbox  | `kick_only`, `snare_only`, `closed_hat_only`, `open_hat_only`, `clap_only`, `basic_beat_120bpm`, `fast_hats_16ths`, `beat_dynamics`, `beat_noise_floor`, `fast_16ths_200bpm`, `flams_50ms`, `voice_kick_only`, `voice_snare_only`, `voice_closed_hat_only`, `voice_open_hat_only`, `voice_clap_only`, `voice_beat_100bpm` |

`m2m-cli scenarios` describes each one. The harder melody scenarios use
vowel formants (a/o/i/e) with breath noise, the extremes of the voice range,
125 ms notes, 6-10 dB re-articulation dips, 20-40 ms consonants before the
vowel (truth onset = vowel), ±30 cent drift, vibrato with ±3 dB tremolo,
150-cent scoops and 50/60 Hz hum at -40 dBFS.

The `unsteady_*` scenarios model an untrained singer (seeded, deterministic;
truth = the intended notes): per note a slow wander of +-50-80 cents (two
slow sines, growing from 15 % to full over 350 ms), fast jitter +-10-20
cents, irregular vibrato 0-25 cents at 4.5-6.5 Hz fading in after 250 ms,
and on 40 % of articulated notes a scoop (60-150 cents below, or 60-100
above, 30-70 ms). `unsteady_sustained`: six 1.2-1.6 s notes;
`unsteady_melody_detached`: 16 notes with "normal" (60-120 ms gap) and
"soft" (9-15 dB dip, no silence; truth onset = dip centre)
re-articulations, repeated pitches included; `unsteady_melody_legato`: two
breaths of legato notes (30-70 ms portamento, no level change; truth onset =
portamento midpoint), whole tones, thirds, fourths and two semitone steps.

The `voice_*` beatbox scenarios use a second, deliberately different drum
family (`voiceDrum()` in `tests/support/m2m/test/Drums.h`): plosive "b/p"
kicks with a 75-125 Hz voiced body, "pf/k" snares (1-6 kHz noise, weak
tone), "ts" hats (4-10 kHz fricative), multi-burst claps, with wide random
variation of spectrum, length and level and a quiet breath/lip pre-roll
before the hit. `tests/unit/beatbox/CrossFamilyTests.cpp` checks the
classifiers on three families (the beatbox unit tests' own synth, the
harness drums and the voice family).

Conventions:

- Melody truth notes span the sound, including its 15 ms attack and 30 ms release.
- `major_scale_legato` has no gaps: each note's offset equals the next onset.
- `glide` truth is the start note until the glide begins and the end note
  from the moment the glide arrives; the glide itself is scored by the glide
  check (below).
- `noisy_20db_snr` uses pink noise; the SNR is relative to the RMS of a sustained note.
- Drum hits use GM notes (kick 36, snare 38, clap 39, closed hat 42, open hat 46)
  with `offsetSec == onsetSec`. The onset is the first sample of the synthetic hit.
  Beat patterns are monophonic (one sound at a time, as a beatboxer would make them).

## Metrics

Detections are matched one-to-one to truth notes with a maximum-cardinality
matching: the onsets must be within the tolerance (default 50 ms, inclusive),
and the MIDI note must match exactly. Closer onsets are preferred.

| metric | meaning |
|--------|---------|
| precision / recall / F1 | note-level, pitch-exact. No truth and no detections gives 1/1/1 |
| onset-only F1 | same matching, ignoring pitch |
| meanAbsOnsetErrorMs / maxAbsOnsetErrorMs | over matched pairs (null if none) |
| meanSignedOnsetErrorMs | detected − truth; positive = late (latency) |
| meanAbsOffsetErrorMs | over matched pairs whose truth has offset > onset (so not drum hits) |
| falseTriggersPerMinute | detections that match no truth onset even ignoring pitch, per minute of audio |
| octaveErrors / wrongPitch | unmatched truth/detection pairs that coincide in time but differ by ±12/±24 semitones / any other interval |
| confusion | `truth note → detected note → count`; row `"spurious"` = detections with no truth, column `"missed"` = truth with no detection |
| glideChecks / glideFailures / glidePassingNotes | see "Glides" below (only with glide segments) |

### Glides

A semitone-quantising tracker cannot follow a continuous glide with a single
switch at some reference time, and stepping through the notes in between
is legitimate. So a glide is not scored by a split point. Truth files (and
scenarios) can list glide segments `{ startSec, endSec, fromNote, toNote }`:

- truth notes are the start note (until `startSec`) and the end note with
  onset `endSec` (when the target pitch is reached);
- detections starting inside the glide (after `startSec`, up to
  `endSec` + tolerance) whose pitch lies strictly between the two notes are
  *passing notes*: neither hits nor false triggers;
- each glide is checked on the notes sounding from `startSec` to
  `endSec` + tolerance: the first is `fromNote` and the last `toNote`,
  pitches move monotonically in the glide's direction, at most
  |toNote - fromNote| + 1 notes, never two at once and no gaps (each note
  ends within 1 ms of where the next starts).

A broken rule counts one `glideFailure` (with a description in
`glideProblems`); baselines gate it with `maxGlideFailures`. This was
preferred over a truth of chromatic steps at the semitone crossings: with a
10 ms stability wait and ~20 ms detection delay, a 150 ms fifth (21 ms per
semitone) cannot be followed step by step, and skipping steps is musically
fine.

## File formats

### Truth (`*.truth.json`)

```json
{
  "version": 1,
  "name": "basic_beat_120bpm",
  "sampleRate": 48000,
  "durationSec": 4.75,
  "notes": [
    { "onsetSec": 0.25, "offsetSec": 0.25, "note": 36 },
    { "onsetSec": 0.5,  "offsetSec": 0.5,  "note": 42 }
  ]
}
```

Only `notes[].onsetSec` and `notes[].note` are required; a missing `offsetSec`
equals `onsetSec` (drum hits). Notes are sorted by onset on reading. An
optional `"glides": [{ "startSec", "endSec", "fromNote", "toNote" }]` array
marks glides (see Glides). Use this format for real-recording corpus files
too.

### Baseline (`--baseline` / `--write-baseline`)

```json
{
  "version": 1,
  "mode": "melody",
  "blockSize": 256,
  "toleranceMs": 50,
  "tolerance": { "f1": 0.02, "onsetErrorMs": 2.0, "falseTriggersPerMinute": 1.0 },
  "scenarios": {
    "sustained_notes": {
      "f1": 1.0, "meanAbsOnsetErrorMs": 12.3, "falseTriggersPerMinute": 0.0,
      "minF1": 0.98, "maxMeanAbsOnsetErrorMs": 14.3, "maxFalseTriggersPerMinute": 1.0
    }
  }
}
```

- `f1`, `meanAbsOnsetErrorMs`, `falseTriggersPerMinute`: measured when written (informational).
- `minF1`, `maxMeanAbsOnsetErrorMs`, `maxFalseTriggersPerMinute`,
  `maxGlideFailures` (scenarios with glides only): the gates.
  `--write-baseline` sets them to measured ∓ `tolerance` (an existing
  `--baseline`'s tolerance is reused), then clamps them to the optional
  top-level `"limits"` object (`minF1`, `maxMeanAbsOnsetErrorMs`,
  `maxFalseTriggersPerMinute`): the PLAN.md requirements a gate may never be
  looser than. Edit them by hand to tighten or loosen a gate;
  `null` or a missing field disables that gate (the onset gate is null when a
  scenario has no matches, e.g. `silence`).
- `knownLimitation` (text): the scenario is known to miss a PLAN.md target;
  its gates are measured ∓ tolerance without the limits, so it cannot get
  worse but is not required to meet the target. Kept by `--write-baseline`
  when rewriting from `--baseline`.

The committed gates are `tests/baselines/melody.json` (default settings =
the plugin's defaults: Follow pitch, Stability 40 %; limits: F1 ≥ 0.9,
mean onset error ≤ 30 ms, false triggers ≤ 0.5/min),
`tests/baselines/melody_lock.json` (Note lock, defaults otherwise, scored
with `--tolerance-ms 100`: Lock deliberately trades note-on latency for the
right note, so it is gated on pitch correctness; limits F1 ≥ 0.9, mean
onset error ≤ 60 ms, ≤ 0.5/min) and `tests/baselines/beatbox.json` (F1 ≥
0.95, ≤ 20 ms, ≤ 0.5/min). They run in ctest (`m2m_cli_gate_melody`,
`m2m_cli_gate_melody_lock`, `m2m_cli_gate_beatbox`) and in CI. The Lock gate
command is
`m2m-cli eval --mode melody --set follow=lock --tolerance-ms 100 --baseline tests/baselines/melody_lock.json`
(a baseline does not store engine settings; the `--set` and tolerance must
be given again, also with `--write-baseline`). Only
deterministic metrics are gated (no wall-clock time); the core library and
the harness are built with `-ffp-contract=off`, and gcc and clang give
identical metrics.
- A scenario missing from the baseline is reported but not gated. A `mode`
  mismatch is a usage error; a different `blockSize`/`toleranceMs` only prints a note.

Workflow after changing DSP code: run `eval --baseline`; if a change is an
intended improvement, re-create the baseline with
`eval --mode M --baseline tests/baselines/M.json --write-baseline tests/baselines/M.json`
(keeps tolerance, limits and known-limitation notes) and commit it with the
change.

### Report (`--report`)

`run` reports have `tool`, `version`, `command`, `mode`, `blockSize`,
`toleranceMs`, `input`, `run` (sample rate, blocks, latency, mean/max block
µs, realtime factor), `notes`, and with `--truth` also `metrics` and
`onsetOnly`. `eval` reports have `scenarios: [{ name, metrics, onsetOnly, run }]`,
`summary` (mean F1, mean onset-only F1, worst F1) and, with `--baseline`,
`baseline: { path, passed, failures }`. Non-finite values are written as `null`.
