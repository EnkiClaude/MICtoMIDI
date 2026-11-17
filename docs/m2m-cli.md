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
            [--report r.json] [--block N] [--tolerance-ms T]
```

- Reads PCM 8/16/24/32-bit or float 32/64-bit WAV (any channel count, mixed
  down to mono), runs the engine in blocks of `N` samples (default 256), then
  flushes held notes at the end.
- Prints processing time per block (mean/max), realtime factor (processing
  time / audio duration; lower is better) and the detected notes.
- `--truth`: scores the notes (see Metrics) and prints a summary.
- `--out`: writes a format-0 Standard MIDI File (120 bpm, 960 ppq).
- `--report`: writes a JSON report (run info, detected notes, metrics).

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
  (latency), false triggers per minute, octave errors, wrong pitches.
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
(`makeEngine (mode)`). `trigger` is the Phase 1 `LevelTriggerEngine` (note 60,
-30 dBFS threshold). `melody` and `beatbox` return `nullptr` until those
engines are wired in there; the CLI then prints
"engine not available in this build" and exits 3.

## Scenarios

All scenarios are deterministic (fixed seeds; noise comes from an
integer-only PRNG, so audio is bit-identical across runs and compilers) and
default to 48 kHz.

| category | scenarios |
|----------|-----------|
| trigger  | `level_bursts` (440 Hz bursts, truth note 60) |
| melody   | `sustained_notes`, `chromatic_scale`, `major_scale_legato`, `vibrato`, `glide`, `repeated_same_pitch`, `weak_fundamental`, `dynamics`, `noisy_20db_snr`, `silence`, `noise_only` |
| beatbox  | `kick_only`, `snare_only`, `closed_hat_only`, `open_hat_only`, `clap_only`, `basic_beat_120bpm`, `fast_hats_16ths`, `beat_dynamics` |

Conventions:

- Melody truth notes span the sound, including its 15 ms attack and 30 ms release.
- `major_scale_legato` has no gaps: each note's offset equals the next onset.
- `glide` truth switches pitch at the midpoint of each 150 ms glide.
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
equals `onsetSec` (drum hits). Notes are sorted by onset on reading. Use this
format for real-recording corpus files too.

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
- `minF1`, `maxMeanAbsOnsetErrorMs`, `maxFalseTriggersPerMinute`: the gates.
  `--write-baseline` sets them to measured ∓ `tolerance` (an existing
  `--baseline`'s tolerance is reused). Edit them by hand to tighten or loosen a gate;
  `null` or a missing field disables that gate (the onset gate is null when a
  scenario has no matches, e.g. `silence`).
- A scenario missing from the baseline is reported but not gated. A `mode`
  mismatch is a usage error; a different `blockSize`/`toleranceMs` only prints a note.

Workflow after changing DSP code: run `eval --baseline`; if a change is an
intended improvement, re-create the baseline with `--write-baseline` and
commit it with the change.

### Report (`--report`)

`run` reports have `tool`, `version`, `command`, `mode`, `blockSize`,
`toleranceMs`, `input`, `run` (sample rate, blocks, latency, mean/max block
µs, realtime factor), `notes`, and with `--truth` also `metrics` and
`onsetOnly`. `eval` reports have `scenarios: [{ name, metrics, onsetOnly, run }]`,
`summary` (mean F1, mean onset-only F1, worst F1) and, with `--baseline`,
`baseline: { path, passed, failures }`. Non-finite values are written as `null`.
