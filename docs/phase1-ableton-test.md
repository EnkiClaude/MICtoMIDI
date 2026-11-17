# Manual test checklist: Ableton Live (macOS)

This checklist covers the real Melody and Beatbox modes on a real Mac with a
real microphone in Ableton Live. Things that can't be checked automatically on
Linux CI (Live's routing, the AU version, real voices, real audio devices)
are tested here. Tick each box; write down anything odd.

You need: headphones, a microphone, Ableton Live 11 or 12, about 30 minutes.
How to set everything up is in [ableton-setup.md](ableton-setup.md); the
routing steps are the same as before.

## 0. Get the build

Build it yourself:

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

The build isn't code-signed yet. If macOS blocks it, run
`xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/MICtoMIDI.vst3`
(and the same for the `.component` and the standalone `.app`).

## A. Plugin route (instrument + sidechain)

1. In Live, rescan plug-ins (Settings → Plug-Ins → Rescan).
2. **Track 1 (audio):** input = your mic, Monitor = **In**.
3. **Track 2 (MIDI):** load **MICtoMIDI** (Plug-ins → VST3 or AU, it shows as an instrument).
   Open the device's **sidechain** section (triangle at the top-left of the
   device) and choose **Audio From → 1-Audio**.
4. **Track 3 (MIDI):** load a synth (e.g. Operator) for now. Set
   **MIDI From → 2-MICtoMIDI**, second dropdown **MICtoMIDI**. Monitor = **In**.

- [ ] The INPUT meter in the MICtoMIDI window moves with your voice.
- [ ] The top-right says **Plugin MIDI out**, and **Notes sent** goes up when notes play.
- [ ] Clicking **Melody** / **Beatbox** switches the page. The mode also shows up
      as an automatable parameter in Live (“Mode”).
- [ ] Try it with the AU version as well as the VST3.

If Live offers the plugin's main input instead of a sidechain, use that (both are
mixed together). Write down which one Live offered: ______.

## B. Melody mode

Select **Melody**. Track 3: a simple synth.

- [ ] Sing a steady “ooh” on one note: the big note name shows it, **NOTE ON**
      lights up, track 3 plays the same note. The pitch trace draws a line with
      a bar under it.
- [ ] Sing a slow scale (do-re-mi…): each step plays one new note, no extra notes in between.
- [ ] Sing one long note with vibrato: exactly **one** note (one bar in the trace).
- [ ] Sing the same note three times (“da da da”): three separate notes.
- [ ] Stay silent and breathe near the mic: no notes. Talk normally: few or no notes.
- [ ] Hum and whistle: both work (whistling is high: check **Max freq** if not).
- [ ] Sing soft then loud: **Last velocity** goes up, track 3 gets louder.
      Switch on **Fixed velocity**: every note uses the fixed value.
- [ ] Set **Scale** = Major, **Root** = C, sing a slightly off note: it lands on
      a note of C major. **Transpose** +12 plays an octave higher.
- [ ] Change **Channel** to 2: track 3 still plays if it listens to all channels
      (and stops if you set track 3 to channel 1 only).
- [ ] Move **Min freq** from 80 Hz to 150 Hz: the latency readout in the header
      (and under the slider) goes down.
- [ ] Open **Advanced**, raise **Hysteresis**: vibrato is even less likely to
      change the note.
- [ ] While a note is held, click **Beatbox**: the note stops immediately (no stuck note).
- [ ] While a note is held, bypass the plugin (device on/off): the note stops.

Rate the melody tracking from 1 (unusable) to 5 (feels like playing a keyboard): ___

## C. Beatbox mode

Select **Beatbox**. Track 3: a **Drum Rack** (any kit).

- [ ] Do a few kicks (“b”/“boom”): the **Kick** lane flashes, track 3 plays the kick.
- [ ] Snares (“k”/“psh”), closed hats (“ts”), open hats (“tsss”), claps:
      each lane flashes and the drum rack plays the matching drum.
- [ ] A simple beat (“boots and cats”) at a slow tempo: count how many hits
      land on the wrong drum out of 20: ___
- [ ] Silence, breathing, talking: few or no hits.
- [ ] Lower a sound's **Sensitivity**: that sound is accepted less often
      (dim outlines appear in its lane instead of solid ticks).
- [ ] Switch a sound off with its toggle: it no longer plays.
- [ ] Change the Kick's **MIDI note** to 37: the drum rack now plays that pad.
- [ ] Notes go out on channel 10 by default; change **Channel** and confirm.
- [ ] Shorter **Analysis** window: lower latency readout. Do classifications get worse?
      Note the shortest window that still works for you: ___ ms

### Calibration walkthrough

- [ ] In the Calibration panel pick **Kick**, press **Calibrate**. The header
      says “Calibrating: no MIDI is sent”; the panel says “Perform 6 kicks…”.
- [ ] Do 6 kicks: the counter goes 1, 2, … 6 and the Kick lane shows circles.
      No drums play while calibrating.
- [ ] Press **Done**. The Kick lane shows “6 ex”.
- [ ] Repeat for Snare and Closed Hat (and the others you use).
- [ ] Try **Cancel** during a take: the old examples for that sound come back.
- [ ] Set **Classifier** to **Calibrated**. The status line turns green
      (“Using your calibration”). If it is yellow, it tells you what is missing.
- [ ] Do the same 20-hit beat again: wrong drums out of 20: ___
      (compare with the Rules result above).
- [ ] **Export…** the calibration to a file, **Clear All**, then **Import…** the
      file: the example counts come back.
- [ ] Save the Live set, close Live, reopen: mode, settings **and calibration**
      are all restored.

## D. Latency

1. Put a metronome-like click in your headphones (Live's metronome is fine).
2. On track 1 (audio) and track 3 (MIDI, armed), record yourself doing sharp
   “t” sounds (Beatbox) on the beat for a few bars.
3. Zoom in and compare where the audio transient and the MIDI note start.

- Melody mode: header latency ____ ms; measured audio → MIDI offset ____ ms
- Beatbox mode: header latency ____ ms; measured audio → MIDI offset ____ ms
- [ ] Does Live shift the recorded MIDI to make up for the reported latency?
      (Offset roughly 0 = yes; offset ≈ the header value = no.)
- Live's audio buffer size during the test: ____ samples

## E. Standalone route (virtual MIDI port)

1. Open `MICtoMIDI.app` (in `build/plugin/MICtoMIDI_artefacts/Release/Standalone/`).
   Allow microphone access when macOS asks.
2. **Options → Audio/MIDI Settings**: choose your mic as the input.
3. The top-right should say **MIDI out: virtual port "MICtoMIDI"**.
4. In Live: Settings → Link, Tempo & MIDI → turn on **Track** for the *MICtoMIDI* input port.
5. On a MIDI track with an instrument: **MIDI From → MICtoMIDI**, Monitor = **In**.

- [ ] The *MICtoMIDI* port appears in Live's MIDI settings.
- [ ] Melody and Beatbox both play the instrument in Live.
- [ ] Quit the app → the port disappears from Live; reopen it → the port comes back,
      with the last mode and settings.

## Report back

Please send:

- Live version, macOS version, Mac model, audio interface and buffer size.
- Every box that failed, with what you saw (a screenshot of the MICtoMIDI window helps).
- The numbers written above (wrong-drum counts, latency measurements, ratings).
- If you can: a short WAV of your voice for anything that went badly (a
  melody it tracked wrong, a beat it misclassified). These become test cases.
