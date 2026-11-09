# Phase 1 — Manual routing test (Ableton Live, macOS)

Phase 1 is done when every box below is ticked on a real Mac running Ableton Live.
In this phase the plugin only checks the input level: **any sound above the
threshold plays one fixed note** (C4 = 60 by default). There is no pitch
detection yet.

## Get the build

Either build it yourself:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build
```

…or download the `MICtoMIDI-macOS-arm64-unsigned` artifact from the latest CI run
on GitHub (Actions tab → CI → the run → Artifacts).

Install:

```sh
cp -R build/plugin/MICtoMIDI_artefacts/Release/VST3/MICtoMIDI.vst3 ~/Library/Audio/Plug-Ins/VST3/
cp -R build/plugin/MICtoMIDI_artefacts/Release/AU/MICtoMIDI.component ~/Library/Audio/Plug-Ins/Components/
```

The build isn't code-signed yet. If macOS blocks it, run:
`xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/MICtoMIDI.vst3`
(and the same for the `.component` and the standalone `.app`).

## A. Plugin route (instrument + sidechain)

1. In Live, rescan plug-ins (Settings → Plug-Ins → Rescan).
2. **Track 1 (audio):** input = your mic, Monitor = **In**. Leave its output
   as is; it only needs to exist as a sidechain source.
3. **Track 2 (MIDI):** load **MICtoMIDI** (Plug-ins → VST3 or AU, it shows as an instrument).
   Open the plugin's device view, open the **sidechain** section
   (the triangle icon at the top-left of the device) and choose
   **Audio From → 1-Audio** (post FX or pre FX).
4. **Track 3 (MIDI):** load any instrument (e.g. Drum Rack or Operator). Set
   **MIDI From → 2-MICtoMIDI**, and in the second dropdown choose **MICtoMIDI**.
   Monitor = **In**.
5. Make a sound into the mic.

- [ ] The meter in the MICtoMIDI window moves with your voice.
- [ ] The red light comes on while you make a sound, and the "notes sent" count goes up.
- [ ] Track 3's instrument plays C4 each time you make a sound.
- [ ] Changing **Note** / **Channel** in the plugin changes what track 3 receives.
- [ ] Arm track 3 and record: notes appear in the clip.
- [ ] Save the Live set, close it and reopen it: the plugin's settings are kept.
- [ ] Try it with the AU version as well as the VST3.

**Latency check:** record a few sharp claps on track 1 (audio) and track 3 (MIDI)
at the same time and compare where they land in the timeline. Write down the
offset in ms here: ______. (The plugin currently reports 0 samples of latency,
so this measures Live's own routing delay.)

**Fallback if the sidechain route doesn't appear:** some Live versions show the
plugin's main input instead of a sidechain. The plugin listens to both inputs
(they are mixed together), so either route works. Write down which one Live offered: ______.

## B. Standalone route (virtual MIDI port)

1. Open `MICtoMIDI.app` (in `build/plugin/MICtoMIDI_artefacts/Release/Standalone/`).
   Allow microphone access when macOS asks.
2. In the app, click **Options → Audio/MIDI Settings** and choose your mic as the input.
3. The bottom line of the window should say **Virtual MIDI port: "MICtoMIDI"**.
4. In Live: Settings → Link, Tempo & MIDI → turn on **Track** for the *MICtoMIDI* input port.
5. On a MIDI track with an instrument: **MIDI From → MICtoMIDI**, Monitor = **In**.

- [ ] The *MICtoMIDI* port appears in Live's MIDI settings.
- [ ] Making a sound into the mic plays the instrument in Live.
- [ ] Quit the app → the port disappears from Live; reopen it → the port comes back.

## Report back

Write down the Live version, macOS version, and each box that failed with what you saw.
