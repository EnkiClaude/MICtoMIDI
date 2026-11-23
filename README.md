# MICtoMIDI

Real-time voice-to-MIDI as a VST3/AU instrument and a standalone app:
**Melody Mode** (singing/humming → notes) and **Beatbox Mode** (vocal percussion → drums).
It uses conventional, deterministic signal processing only (no neural networks).

See [PLAN.md](PLAN.md) for the full design and roadmap.

## Status

**Phase 7: both modes are wired into the plugin with a full UI.**

- **Melody mode:** sing, hum or whistle → MIDI notes (YIN/MPM pitch detection,
  note tracking with vibrato hysteresis, retrigger, scale snap, transpose,
  level-based velocity). For wavering voices: a **Stability** control
  (Loose ↔ Tight) and **Note lock** (one note per sung syllable; pitch
  wobble never changes a held note). The window shows the detected note, Hz, cents,
  confidence and a scrolling pitch trace with the sent notes.
- **Beatbox mode:** vocal percussion → General MIDI drums on channel 10
  (kick, snare, clap, closed hat, open hat). Rule-based classifier out of the box,
  or calibrate it to your own sounds (a few examples per sound; saved with your
  project, exportable to a file).
- Latency is reported to the host and shown in the window (about 20–30 ms in
  Melody mode and about 19 ms in Beatbox mode with the default settings).
- Accuracy is measured on every change by an offline harness (`m2m-cli`) with
  44 synthetic scenarios (26 melody, 17 beatbox, 1 trigger) and a CI regression gate; see [docs/m2m-cli.md](docs/m2m-cli.md).

On a real Mac the standalone app works with Ableton Live and is the
recommended way to use it for now (the VST3 did not appear in Live in the
first test; under investigation). Manual checklist:
[docs/phase1-ableton-test.md](docs/phase1-ableton-test.md).

## Using it

Set-up guide for Ableton Live (plugin or standalone app, calibration, latency):
**[docs/ableton-setup.md](docs/ableton-setup.md)**.

In short: MICtoMIDI is an **instrument** that takes the mic through its
**sidechain** (or main) input, because Live only forwards MIDI from
instruments. A second MIDI track picks up its notes with *MIDI From →
MICtoMIDI*. The standalone app instead creates a virtual MIDI port named
**MICtoMIDI** (macOS/Linux) that any DAW can use. Use headphones: speaker
sound leaking into the mic produces stray notes.

All settings are host-automatable parameters with stable IDs (`mode`,
`input_gain`, `vel_*`, `mel_*`, `bb_*`); see `plugin/Source/Parameters.h`.

## Layout

```
core/                 JUCE-free DSP + MIDI library (all analysis logic lives here)
plugin/               JUCE plugin: VST3, AU (macOS) and Standalone
tools/plugin-smoke-test/  loads the built VST3 in a host and checks melody/beatbox MIDI,
                      mode switching, latency and state; also m2m_ui_snapshot
                      (renders the editor to PNG with synthetic input)
tests/                Catch2 unit tests for core/
```

## Build

Requirements: CMake ≥ 3.22, a C++20 compiler, Ninja (recommended). JUCE 8 and
Catch2 are downloaded automatically when you configure.

```sh
# Everything (plugin + standalone + tests)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure

# Core library + tests only (fast, no JUCE)
cmake -S . -B build-core -G Ninja -DM2M_BUILD_PLUGIN=OFF
cmake --build build-core && ctest --test-dir build-core
```

macOS (Apple Silicon): add `-DCMAKE_OSX_ARCHITECTURES=arm64`. To install into
your user plug-in folders after building, add `-DM2M_COPY_PLUGIN_AFTER_BUILD=ON`.

Linux needs the JUCE system packages:
`libasound2-dev libx11-dev libxext-dev libxinerama-dev libxrandr-dev libxcursor-dev libxcomposite-dev libfreetype6-dev libfontconfig1-dev libgl1-mesa-dev`.

Build outputs: `build/plugin/MICtoMIDI_artefacts/Release/{VST3,AU,Standalone}/`.

## Checking a build

```sh
# Hosts the VST3 and checks Melody/Beatbox output end to end (Linux: run under xvfb-run -a)
build/tools/plugin-smoke-test/m2m_plugin_smoke_test_artefacts/Release/m2m_plugin_smoke_test \
    build/plugin/MICtoMIDI_artefacts/Release/VST3/MICtoMIDI.vst3

# Renders the editor (both modes, default and minimum size) to PNG files
build/tools/plugin-smoke-test/m2m_ui_snapshot_artefacts/Release/m2m_ui_snapshot /tmp/m2m-ui
```
