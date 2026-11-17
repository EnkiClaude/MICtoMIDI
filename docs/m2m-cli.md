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
`retriggerSensitivity`, `attackWindowMs`, `transpose`. Beatbox:
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

## Scenarios

All scenarios are deterministic (fixed seeds; noise comes from an
integer-only PRNG, so audio is bit-identical across runs and compilers) and
default to 48 kHz.

| category | scenarios |
|----------|-----------|
| trigger  | `level_bursts` (440 Hz bursts, truth note 60) |
| melody   | `sustained_notes`, `chromatic_scale`, `major_scale_legato`, `vibrato`, `glide`, `repeated_same_pitch`, `weak_fundamental`, `dynamics`, `noisy_20db_snr`, `voice_formants`, `low_male_range`, `high_range`, `fast_16ths_legato`, `fast_16ths_detached`, `soft_rearticulation`, `consonant_onsets`, `pitch_drift`, `tremolo_vibrato`, `scoop_ups`, `hum_50hz`, `hum_60hz`, `silence`, `noise_only` |
| beatbox  | `kick_only`, `snare_only`, `closed_hat_only`, `open_hat_only`, `clap_only`, `basic_beat_120bpm`, `fast_hats_16ths`, `beat_dynamics`, `beat_noise_floor`, `fast_16ths_200bpm`, `flams_50ms`, `voice_kick_only`, `voice_snare_only`, `voice_closed_hat_only`, `voice_open_hat_only`, `voice_clap_only`, `voice_beat_100bpm` |

`m2m-cli scenarios` describes each one. The harder melody scenarios use
vowel formants (a/o/i/e) with breath noise, the extremes of the voice range,
125 ms notes, 6-10 dB re-articulation dips, 20-40 ms consonants before the
vowel (truth onset = vowel), ±30 cent drift, vibrato with ±3 dB tremolo,
150-cent scoops and 50/60 Hz hum at -40 dBFS.

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

The committed gates are `tests/baselines/melody.json` (limits: F1 ≥ 0.9,
mean onset error ≤ 30 ms, false triggers ≤ 0.5/min) and
`tests/baselines/beatbox.json` (F1 ≥ 0.95, ≤ 20 ms, ≤ 0.5/min). They run in
ctest (`m2m_cli_gate_melody`, `m2m_cli_gate_beatbox`) and in CI. Only
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
