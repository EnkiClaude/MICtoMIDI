# Using MICtoMIDI with Ableton Live

MICtoMIDI listens to your microphone and plays MIDI notes:

- **Melody** mode: sing, hum or whistle a tune and it plays the notes.
- **Beatbox** mode: make drum sounds with your mouth and it plays a drum kit.

There are two ways to get its MIDI into Live. Pick one.

> **For now, use the standalone app (Option B).** It has been confirmed to
> work with Live on a Mac. The VST3 plugin did not show up in Live's plug-in
> list in the first real test; that is being investigated separately. All
> Melody and Beatbox features, including Note lock and Stability, are the
> same in the app.

| | Plugin (inside Live) | Standalone app |
|---|---|---|
| Setup | Three tracks, a little routing | Open the app, pick a MIDI input in Live |
| Works with | Live (and other DAWs that route MIDI from instruments) | Any DAW or MIDI app |
| Latency compensation | Live may compensate for it (see the test checklist) | None (the DAW cannot know the delay) |
| Settings saved with your Live set | Yes, including calibration | Saved by the app itself |

**Use headphones.** If your speakers play into the microphone, MICtoMIDI hears
the synth too and plays extra notes.

---

## Option A: the plugin

Live only passes on MIDI from plugins that load as instruments, so MICtoMIDI is
an **instrument** that gets the mic through its **sidechain** input.

1. **Install.** Copy `MICtoMIDI.vst3` to `~/Library/Audio/Plug-Ins/VST3/` and
   (on a Mac) `MICtoMIDI.component` to `~/Library/Audio/Plug-Ins/Components/`.
   In Live: *Settings → Plug-Ins → Rescan*.
   The build is not code-signed yet. If macOS blocks it, run
   `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/MICtoMIDI.vst3`
   (and the same for the `.component`).
2. **Track 1 – audio (the mic).** Input: your microphone. Monitor: **In**.
   You can turn its volume down; it only feeds the plugin.
3. **Track 2 – MIDI (MICtoMIDI).** Drag **MICtoMIDI** onto it from
   *Plug-ins* (it is listed as an instrument). Open the device's **sidechain**
   section (the small triangle at the top-left of the device) and set
   **Audio From → 1-Audio**.
4. **Track 3 – MIDI (the sound you want to hear).** Load any instrument:
   a synth for Melody mode, a **Drum Rack** for Beatbox mode. Set
   **MIDI From → 2-MICtoMIDI**, second box **MICtoMIDI**. Monitor: **In**.
5. Open MICtoMIDI's window, choose **Melody** or **Beatbox** at the top and
   make a sound. The input meter should move and track 3 should play.

To record, arm track 3 and press record: the notes land in a MIDI clip.

If Live offers the plugin's main input instead of a sidechain, that works too:
MICtoMIDI mixes both inputs together.

## Option B: the standalone app

1. Open `MICtoMIDI.app` and allow microphone access.
2. *Options → Audio/MIDI Settings*: choose your microphone as the input.
3. The top-right of the window should say **MIDI out: virtual port "MICtoMIDI"**.
4. In Live: *Settings → Link, Tempo & MIDI*: switch on **Track** for the
   *MICtoMIDI* input.
5. On a MIDI track with an instrument: **MIDI From → MICtoMIDI**, Monitor **In**.

On Windows the app cannot create a MIDI port by itself yet (it needs loopMIDI);
use the plugin instead.

---

## Melody mode in 30 seconds

- Sing one steady note. The big note name shows what it hears; the trace on
  the right draws your pitch and the notes that were sent as bars.
- If the input meter stays below the yellow line, notes will not start: raise
  **Gain** or lower **On level**.
- Stray notes on breaths or noise: raise **Confidence** or **On level**.
- Notes jumping around with vibrato or a wobbly voice: turn **Stability**
  up (see below).
- Only want notes from a key: set **Scale** and **Root**.
- **Min freq** decides how low it listens. Lower values make every note a bit
  later (the latency is shown right under it). If you only sing above ~150 Hz,
  raise it for snappier notes.

## If it's hard to hold a steady note

Most voices waver a little: the pitch drifts, wobbles, or slides up into a
note. MICtoMIDI can then play extra, unwanted notes. Try these, in order:

1. **Set Scale and Root to the key of your song** (e.g. *Major*, *C*). Notes
   outside the key can no longer be chosen, which removes most of the wrong
   ones.
2. **Turn Stability up** (the *Steady notes* panel above the pitch trace,
   *Loose ↔ Tight*). Tighter means your pitch must move further and longer
   before the note changes. It costs a little delay: the panel shows the
   latency (about 22 ms at the default 40 %, about 32 ms fully Tight).
3. **Try Note lock** (*Note lock — one note per syllable*). Each sung
   syllable now gives exactly one note, picked once your pitch has settled.
   While you hold it, wobbling never changes it. To play the next note,
   **start a new syllable**: sing "da-da-da" or "la-la-la" instead of one
   long "aaah". A clear jump of more than a whole tone also changes the
   note, but smaller legato steps (a semitone) need a new syllable. Note lock
   waits until a slide into the note is over, so a scooped note comes a bit
   later instead of starting on the wrong note.

Stability sets several Advanced values at once (Hysteresis, Min stable, Min
note). They are greyed out in **Advanced** and show what Stability chose. If
you prefer to set them yourself, switch on **Manual** in Advanced; Stability
is then ignored.

## Beatbox mode in 30 seconds

- Each sound has a lane (Kick, Snare, Clap, Closed Hat, Open Hat). Lanes flash
  and draw a tick for every hit. A dim outline means the hit was heard but not
  accepted.
- Too many or too few of one sound: change that sound's **Sensitivity**.
- Change the note a sound plays with **MIDI note** (default General MIDI on
  channel 10, which matches Live's Drum Rack: kick C1 = 36, snare 38, clap 39,
  closed hat 42, open hat 46).
- Don't use a sound? Switch it off with its toggle; its hits go to the others.

### Calibrating (recommended)

The built-in **Rules** classifier works without setup, but it was designed
around average sounds. Calibration teaches it *your* sounds:

1. In the Calibration panel, pick a sound (e.g. **Kick**) and press **Calibrate**.
2. Perform that sound about 6 times, the way you will use it. The counter
   counts each one. No MIDI is sent while you calibrate.
3. Press **Done** (or **Cancel** to throw the take away).
4. Repeat for every sound you use (at least two sounds, 3+ examples each).
5. Set **Classifier** to **Calibrated**. The panel says when the calibration
   is usable.

Calibration is saved with your Live set. **Export…** writes it to a file
so you can load it into another project with **Import…**.

## Latency

The header shows how long MICtoMIDI waits before it can decide on a note
("Latency … ms"). It is reported to Live. Typical values: Melody around
20–30 ms (Note lock adds about 5 ms, a tight Stability up to 10–15 ms),
Beatbox around 20–25 ms. Lower it with a higher **Min freq** or a looser
**Stability** (Melody) or a shorter **Analysis** window (Beatbox). Use a small audio buffer
(64–128 samples) in Live's audio settings.
