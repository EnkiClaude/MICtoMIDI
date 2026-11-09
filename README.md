# MICtoMIDI

Real-time voice-to-MIDI as a VST3/AU instrument and a standalone app:
**Melody Mode** (singing/humming → notes) and **Beatbox Mode** (vocal percussion → drums).
It uses conventional, deterministic signal processing only (no neural networks).

See [PLAN.md](PLAN.md) for the full design and roadmap.

## Status

**Phase 1: plugin shell and routing test.** The plugin plays one fixed note
whenever the input level goes above a threshold. This proves that audio in →
MIDI out works in Ableton Live, through both the plugin and the standalone app,
before any pitch detection is built.
Manual test checklist: [docs/phase1-ableton-test.md](docs/phase1-ableton-test.md).

## Layout

```
core/                 JUCE-free DSP + MIDI library (all analysis logic lives here)
plugin/               JUCE plugin: VST3, AU (macOS) and Standalone
tools/plugin-smoke-test/  loads the built VST3 in a host and checks MIDI comes out
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

## Using it in Ableton Live

Live only forwards MIDI from plugins that load as instruments, so MICtoMIDI is an
**instrument** that takes the mic through its **sidechain** (or main) input.
Step-by-step setup: [docs/phase1-ableton-test.md](docs/phase1-ableton-test.md).
The standalone app instead creates a virtual MIDI port named **MICtoMIDI**
(macOS/Linux) that any DAW can use as a MIDI input.
