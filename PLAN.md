# MICtoMIDI — Development Plan

Real-time voice-to-MIDI: **Melody Mode** (singing/humming/whistling → notes) and
**Beatbox Mode** (vocal percussion → drum notes).

---

## 1. Constraints and decisions

| Topic | Decision |
|---|---|
| Analysis method | Deterministic DSP only. No neural networks or pre-trained models ship in the product. |
| Per-user calibration | **Allowed.** The user records a few examples of each sound, and new sounds are matched to the closest stored example average (nearest-centroid on normalized features). It's deterministic and transparent, with no training step. |
| Delivery | **Both supported:** (a) VST3/AU plugin, (b) standalone app that publishes a **virtual MIDI port**. |
| Platform | macOS Apple Silicon first, then Windows. |
| Stack | C++20, JUCE, CMake. |
| Dependencies | None at runtime beyond JUCE (and the VST3 SDK, which JUCE bundles). |

### Why both delivery modes
Ableton Live only forwards MIDI from a plugin when the plugin loads as an
**instrument**. The plugin therefore has to be an instrument that takes the mic
signal through a **sidechain input**, and a second MIDI track picks up its output via
*MIDI From → [track] → [plugin]*. That works, but setup is awkward. The standalone
app with a virtual MIDI port works with any DAW and needs no routing tricks.
Phase 1 checks that both routes work before any DSP is written.

> Windows has no built-in virtual MIDI port API. The Windows standalone app will
> need loopMIDI or Windows MIDI Services. That decision is deferred to the Windows port.

---

## 2. Architecture

```
            ┌──────────────────────────────────────────────┐
            │  mictomidi_core  (plain C++, NO JUCE)          │
            │                                                │
 audio ───► │ Preprocessor ─► Melody engine ─┐               │
 (float*)   │               └► Beatbox engine ┴► MidiEvent[] │ ───► events with
            │                                                │      sample offsets
            └──────────────────────────────────────────────┘
                 ▲                ▲                 ▲
                 │                │                 │
         plugin/ (JUCE)    standalone/ (JUCE)   tools/m2m-cli
         VST3 + AU          virtual MIDI port    WAV → MIDI + metrics
```

**Key rules**

1. **The core library has no JUCE dependency.** It takes `const float*` blocks and
   returns MIDI events with sample offsets. That keeps it testable on Linux CI and by
   the coding agent, which can't run a DAW.
2. **No separate analysis thread at first.** Analysis runs on the audio thread
   with a fixed hop size, so results are deterministic and event timestamps are
   accurate. Add a worker thread only if profiling shows it's needed.
3. **Audio-thread rules:** no allocation, locks, file I/O, logging or GUI calls.
   All buffers are allocated in `prepare(sampleRate, maxBlockSize)`.
4. **UI communication:** lock-free single-producer/single-consumer (SPSC) FIFO from
   the audio thread to the editor (detected pitch, events, levels). Parameters use
   JUCE's `AudioProcessorValueTreeState` (APVTS).
5. **Report latency** to the host with `setLatencySamples()`. Whether Live
   compensates for MIDI routed out of a plugin is tested in Phase 1.
6. **Every DSP stage has a narrow interface** (`prepare`, `reset`, `process`)
   and its own unit tests.

---

## 3. Latency budget (physical limits)

| Stage | Minimum delay | Notes |
|---|---|---|
| Pitch window | ~2 periods of lowest note | 80 Hz floor ≈ 25 ms; 150 Hz floor ≈ 13 ms. The **Min Frequency** setting is shown in the UI as a latency trade-off. |
| Velocity window | 5–15 ms after onset | Velocity has to be known at note-on time. |
| Beatbox classify | 10–25 ms after onset | Enough audio to separate kick, snare and hat. |
| Open vs. closed hat | **Not by decay length** | Decay isn't known until it's over. Classify from the first ~20 ms of the spectrum, or use distinct sounds chosen during calibration. |
| Host buffer | Block size | Recommend 64–128 samples. |

Targets: melody note-on ≤ 30 ms, beatbox hit ≤ 20 ms, measured from the start of
the sound to the MIDI event in the offline tool.

---

## 4. Melody Mode

### Pipeline
```
audio → DC block + high-pass (~60 Hz) → noise gate
      → [pitch path]  framing (hop 64–128) → YIN/MPM F0 + confidence
                      → median/one-pole smoothing (in cents)
                      → optional scale/key snap
      → [level path]  envelope follower + onset detector (for retriggering)
      → NoteTracker (state machine) → MIDI
```

### Pitch detector
- **YIN** with an FFT-based difference function and parabolic interpolation.
- Behind an interface (`IPitchDetector`) so **MPM (McLeod)** can be tried as an
  alternative. MPM often does better on voice with short windows.
- pYIN-style Viterbi smoothing (an HMM with no training, so still deterministic)
  is held in reserve in case octave errors stay a problem.

### NoteTracker
States: `Idle → Pending → Active` (anything else is a transition, not a state).

| From | Condition | Action |
|---|---|---|
| Idle | level > on-threshold AND confidence > threshold | → Pending, start timer |
| Pending | pitch stays within ±hysteresis for `minStableMs` | emit Note On (velocity from attack window) → Active |
| Pending | level falls or confidence drops | → Idle |
| Active | pitch moves > `changeHysteresis` (cents) and stays there for `minStableMs` | **Legato change:** Note Off(old) then Note On(new), same sample offset, off first |
| Active | **amplitude onset detected** on a stable pitch | **Retrigger** the same note (so "da-da-da" gives repeated notes) |
| Active | level < off-threshold for `releaseMs`, or confidence lost | Note Off → Idle |

Vibrato tolerance comes from measuring pitch in cents and using a hysteresis band
(default ±50–70 cents) plus the stability timer. The goal is stable musical notes,
not exact pitch.

### Parameters
Min/Max frequency · Confidence threshold · Pitch smoothing · On/Off threshold ·
Min note duration · Change hysteresis (cents) · Retrigger sensitivity ·
Scale/key snap (Off, Chromatic, Major, Minor, …, Root) · Transpose (semitones/octaves) ·
Velocity sensitivity · Velocity curve · Fixed-velocity option · MIDI channel.

Later: pitch-bend output (configurable bend range), then MPE.

---

## 5. Velocity

Measured over a short attack window right after the onset:
- peak level in the window (dBFS)
- attack slope (rise in the envelope per ms)

`v = curve(clamp((peak_dB − floor_dB) / range_dB, 0, 1))` blended with the attack
slope, then mapped to 1–127. The curve can be linear, exponential or logarithmic,
plus a fixed-velocity option. Shared by both modes.

---

## 6. Beatbox Mode

### Pipeline
```
audio → preprocess → onset detection (spectral flux + envelope, adaptive threshold)
      → capture an N ms window after onset (default 20 ms, configurable)
      → FeatureExtractor → Classifier → DrumMapping → MIDI (velocity from §5)
```

### Onset detection
- Short-hop spectral flux (hop 64–128) combined with the rise in the envelope.
- Adaptive threshold (median plus a margin over a moving window) and a refractory
  period of 30–50 ms.
- The event timestamp is the **detected onset sample**, not the moment
  classification finishes. In the plugin the event is still sent late. Latency
  reporting and the user's "delay to grid" choice determine how this is handled.

### Features (computed on the post-onset window)
Energy in bands (<150 Hz, 150 Hz–1 kHz, 1–5 kHz, >5 kHz) · spectral centroid ·
spectral spread · rolloff (85 %) · spectral flatness · zero-crossing rate ·
crest factor · attack time · number of sub-onsets within the window (for claps).

### Classifiers (both behind `IDrumClassifier`)
1. **RuleClassifier** (default, no setup): weighted scores per class from the
   features, highest score wins, below a minimum score → no event.
2. **CalibratedClassifier** (recommended): in calibration mode the user performs
   ~5–8 examples of each sound. The plugin stores the per-class average and
   spread of the normalized features. A new sound is assigned to the nearest
   class (normalized distance). A distance threshold rejects sounds that match no
   class. Calibration data is saved in the plugin state and can be exported or
   imported as JSON.

Classes: Kick, Snare, Clap, Closed Hat, Open Hat (+ Tom Low/High later).

### Default drum mapping (General MIDI, remappable)
Kick 36 · Snare 38 · Clap 39 · Closed Hat 42 · Open Hat 46 · Low Tom 45 · High Tom 50.
Channel default: 10.

---

## 7. Timing / quantization

- **No BPM detection.** Tempo and position come from the host
  (`AudioPlayHead`); the standalone app has a manual BPM setting.
- **Raw timing is the default.** Ableton's record quantize and groove already
  handle quantizing after the fact.
- **Optional "delay to grid":** in real time, quantizing can only *delay* events.
  Settings: grid (1/4–1/32), strength (0–100 %), swing, and a "max delay" limit
  (events further than that from the next grid line pass through unchanged).
  Low priority (Phase 8).

---

## 8. UI (first version)

- Mode switch: **Melody | Beatbox**
- Input meter + gate threshold marker
- **Melody:** detected note name, Hz, cents deviation, confidence bar, last velocity,
  a scrolling pitch trace with note bars on top.
- **Beatbox:** per-class sensitivity, a lane per class that flashes on each hit,
  a **Calibrate** button with a guided "perform 5 kicks…" flow, classifier
  choice (Rules/Calibrated), mapping table.
- Common: MIDI channel, velocity sensitivity and curve, latency readout.
- Standalone only: audio device selection, virtual MIDI port name, manual BPM.
- Advice shown in the UI: use headphones (speaker playback leaking into the mic
  produces stray notes).

---

## 9. Testing strategy

### Tiers
1. **Unit tests** (Catch2, core library, run on Linux CI): every DSP stage.
2. **Synthetic signals** (generated in code, exact answers known):
   sines at every MIDI note within the range, sine sweeps/glides, vibrato (±50 cents,
   5–7 Hz), harmonic-rich tones with a weak fundamental (to catch octave errors),
   added noise at set signal-to-noise ratios, repeated same-pitch notes with gaps,
   synthesized kicks (pitch-dropping sine), snares (tone + noise), hats (filtered noise).
3. **Real-recording corpus** (`tests/corpus/`, large files kept outside git or in
   Git LFS): WAV files plus a matching `.json`/`.mid` with the correct notes or hits.
   Own recordings first, then public datasets after a licence check
   (e.g. AVP vocal percussion, MIR-1K / vocadito for singing).
4. **Offline tool `m2m-cli`:**
   `m2m-cli --mode melody in.wav --truth in.json --out out.mid --report report.json`
   reports note precision/recall/F1 (onset tolerance 50 ms), pitch accuracy, octave
   error rate, onset/offset timing error, false triggers per minute, drum
   confusion matrix, and the average processing time per block.
5. **Plugin validation:** pluginval (strictness ≥ 5) in CI, and manual checks in
   Ableton Live (instrument + sidechain route).

### Regression gate
CI stores baseline metrics. A change that pushes any corpus metric past its
tolerance fails the build. This is how the coding agent can change DSP code with
confidence without hearing the result.

---

## 10. Repository layout

```
CMakeLists.txt
cmake/                     # JUCE fetch (CPM/FetchContent), warnings, sanitizers
core/                      # mictomidi_core — no JUCE
  include/m2m/…
  src/
    audio/      Preprocessor, EnvelopeFollower, NoiseGate, Fft (wrapper)
    pitch/      YinDetector, MpmDetector, PitchSmoother, ScaleSnap
    onset/      OnsetDetector
    melody/     NoteTracker, MelodyEngine
    beatbox/    FeatureExtractor, RuleClassifier, CalibratedClassifier, BeatboxEngine
    midi/       MidiEvent, VelocityMapper, DrumMapping, GridDelay
plugin/                    # JUCE AudioProcessor (instrument + sidechain), Editor, Params
standalone/                # JUCE app, virtual MIDI output
tools/m2m-cli/             # offline WAV → MIDI + metrics
tests/
  unit/                    # Catch2
  synth/                   # synthetic signal generators + tests
  corpus/                  # manifest + ground truth (audio via LFS/external)
.github/workflows/         # linux: core+tests+cli; macos: plugin build + pluginval
```

FFT: start with JUCE-free code in the core library (e.g. a small radix-2 FFT, or
pffft (BSD), copied into the source tree). Apple's vDSP can be added later on macOS
behind the same wrapper.

---

## 11. Phases and acceptance criteria

### Phase 1 — Plugin shell and routing proof
- CMake + JUCE project: VST3 + AU + standalone, CI on Linux (core) and macOS (plugin).
- Plugin is an **instrument with sidechain input**. Test logic: input level over a
  threshold → MIDI note 60.
- Standalone app: the same logic, sent to a virtual MIDI port named "MICtoMIDI".
- **Done when:** in Ableton Live (macOS) the sidechain → MIDI route plays a synth
  on another track; the standalone port shows up in Live; parameters save and
  restore; pluginval passes; latency compensation behaviour is written down.

### Phase 2 — Test setup
- Core library skeleton, `m2m-cli`, synthetic signal generators, Catch2, metrics,
  JSON reports, baseline file, CI regression gate.
- **Done when:** CI runs the CLI on synthetic signals and fails if a metric gets worse.

### Phase 3 — Pitch detection
- YIN (+ MPM behind the same interface), confidence, smoothing.
- **Done when:** synthetic sines across the range are within ±10 cents; octave
  errors < 1 % on harmonic-rich tones; processing time per block is logged.

### Phase 4 — Note tracking + velocity → **Milestone 1: sing → clean MIDI in Live**
- NoteTracker, onset retrigger, velocity mapping, scale snap, transpose.
- **Done when:** note F1 ≥ 0.9 on synthetic signals; vibrato test produces exactly
  one note; repeated same-pitch notes retrigger; 0 false triggers on silence/noise
  tests; tried live in Ableton.

### Phase 5 — Onset detection
- **Done when:** onset F1 ≥ 0.95 on synthetic drums, timing error ≤ 5 ms.

### Phase 6 — Features + classifiers → **Milestone 2: beatbox → drums in Live**
- RuleClassifier, CalibratedClassifier, calibration UI flow, drum mapping.
- **Done when:** synthetic kick/snare/hat ≥ 95 % correct; real-recording corpus
  results reported (target ≥ 85 % calibrated); tried live in Ableton.

### Phase 7 — UI
- Monitors, calibration flow, mapping table, latency readout.

### Phase 8 — Optional delay-to-grid / swing

### Phase 9 — Latency + CPU optimization
- Profile; tune hop and window sizes; vDSP; consider a worker thread only if needed.
- **Done when:** latency targets in §3 are met; < 5 % of one core at 48 kHz / 128 samples.

### Phase 10 — Packaging
- macOS code signing + notarization, installer, Windows build (+ decision on the
  virtual MIDI port), wider testing across DAWs.

---

## 12. Risks

| Risk | Mitigation |
|---|---|
| Live doesn't compensate for latency on routed plugin MIDI | Measure in Phase 1; standalone app as the alternative; document the result. |
| Rule-based classifier fails on new performers | Calibrated classifier is the recommended mode. |
| Octave errors on breathy/low voices | Tune YIN thresholds, try MPM, add Viterbi smoothing, offer an octave-lock option. |
| Stray notes from speaker bleed | Noise gate, headphone advice, confidence threshold. |
| Users find sidechain routing confusing | Step-by-step setup guide; the standalone app needs no routing. |

## 13. Licensing notes
- **JUCE:** AGPLv3 or a commercial licence. Choose before any public release.
- **VST3 SDK:** check the licence of the version used (recent versions are MIT).
- **Essentia:** AGPL. Use as a reference only; don't link or copy code.
- Datasets: check each licence before adding anything to the corpus.

## 14. Development principles
- Build the simplest deterministic version that works, then measure, then improve.
- Every DSP change is measured with `m2m-cli` before it is merged.
- The core library stays free of JUCE and allocation-free during `process()`.
