# TinyBox: a Hortator-style groovebox for Android

8 tracks, each running one of 11 sound machines. Includes a sequencer with probability, trig conditions, micro-timing, ratchets and p-locks, a sample slicer, and a polyphonic synth. The audio engine is C++ on AAudio and needs no extra libraries. The UI is a single Kotlin view laid out for a landscape tablet (S7 FE).

## Build and install

To build on GitHub and install on the tablet without a cable, see **GITHUB_SETUP.md**.

1. Unzip, then in Android Studio use **File → Open** and pick the `TinyBox` folder.
2. Let Gradle sync. If Android Studio asks for the **NDK** and **CMake 3.22.1**, install them (SDK Manager → SDK Tools). If it offers an AGP upgrade, accepting it is fine.
3. Connect the tablet over wireless debugging (same as TinyRack) and press **Run**.

## Machines
| # | Machine | Type |
|---|---|---|
| 0 | BD Classic: sine kick with pitch sweep, click, drive | Classic (Machinedrum TRX-style) |
| 1 | SD Classic: two-tone body + filtered noise | Classic |
| 2 | HH Metal: six detuned squares (808 ratios) + noise | Classic |
| 3 | Clap: multi-burst noise + room tail | Classic |
| 4 | FM Perc: 2-op FM with pitch and FM envelopes (EFM-style) | Classic |
| 5 | Metal Bell: cowbell / metallic | Classic |
| 6 | PM Drum: 16-mode circular membrane, mallet exciter, tension bend, snare wires, shape morph to tonal | Physical model |
| 7 | PM Cymbal: 32-mode plate, spread, sizzle, bell | Physical model |
| 8 | PM Bar: wood block → marimba → glass morph, tube resonator; follows step note | Physical model |
| 9 | Slicer: WAV/MP3/FLAC/OGG/M4A, even or transient slicing, pitch, reverse, LP/HP filter, choke | Sampler |
| 10 | Poly Synth: 8 voices, 2 polyBLEP osc + sub, resonant filter, ADSR, vibrato, chords | Synth |
| 11 | Granular: up to 96 grains from a loaded sample; position, size, density, spray, pitch, jitter, stereo spread, reverse, scan, tone | Sample / synth |
| 12 | FM Poly: 8-voice 2-op FM with feedback, index envelope, ADSR | Synth |
| 13 | PM String: 8-voice Karplus-Strong, two detuned strings per voice, pick position, body resonance | Physical model |

Every track also has **Level, Pan, Delay send and Reverb send**. The master section has a tempo-synced ping-pong delay, a reverb, drive and a soft limiter.

## Pages (top bar)
- **SEQ**: the step sequencer, sound knobs and performance area.
- **MIX**: one channel strip per track, plus master. Each strip has Pan, Delay and Reverb knobs, a fader with dB readout, a peak meter, and Mute/Solo. The master strip has delay return, reverb return, drive and volume. Double-tap any control to reset it.
- **SONG**: a list of up to 64 rows, each a pattern × number of repeats.
  - ADD and INSERT use the current pattern. Drag a row's letter to change its pattern, or its ×N to change the repeats.
  - Tap a row to start playback from there. Turn on SONG MODE, then press PLAY. LOOP repeats the song; with LOOP off, playback stops at the end. Long-press CLEAR to empty the song.

## Chords
Synth, granular and slicer steps can hold up to 4 notes.
- **Step edit:** hold several keys together.
- **Tapping a step on:** hold keys while you tap, and the step takes all of them.
- **Live REC:** notes played together land on one step. So do notes added to a step later in the same loop pass. On the next pass, playing that step again replaces it.

The step's CHORD setting is still available on top of this.

## Controls
- **Track list (left).** Tap a row to select it. Tap **M** to mute. **Long-press a row to change its machine.** The MASTER row opens the master FX knobs.
- **Steps.** Tap to switch a step on or off. Use pages 1–4 and LEN −/+ to set each track's length (1–64). Tracks can have different lengths, so polymeter works.
- **Long-press a step** to edit it:
  - VEL
  - PROB: 0–100 %
  - COND: FILL, !FILL, PRE, !PRE, NEI, !NEI, 1ST, !1ST, and A:B from 1:2 to 8:8
  - MICRO: ±12/24 of a step
  - RATCH: ×1–8
  - NOTE
  - LEN: gate length
  - CHORD
- **P-locks.** While a step is being edited, turning any sound knob locks that value to the step; locked knobs show yellow. Double-tap or long-press a knob to clear its lock. CLR LOCKS clears all locks on the step.
- **Knobs.** Drag up or down. Double-tap to reset to the default.
- **Top bar.**
  - Pattern buttons A–H switch at the end of the bar (they flash while queued).
  - Hold FILL for fill mode.
  - REC records live: hits land on the nearest step, and the offset is kept as micro-timing. Held synth notes also record their length.
  - COPY / PASTE copy a whole pattern. CLR clears the selected track; long-press it to clear the whole pattern.
  - Drag BPM or SWING up or down. Double-tap BPM to type a value.
- **Bottom area.**
  - Drum tracks: 8 finger-drum pads. Velocity follows how low on the pad you hit.
  - Synth tracks: a 2-octave keyboard. In step-edit mode, pressing a key sets that step's note.
  - Slicer tracks: LOAD, ÷4/8/16/32, AUTO (transient detection). Tap a slice to play it. In step-edit mode, tapping a slice assigns it to the step. New slicer steps default to slice = step number, so the loop plays in order.
- **SAVE / LOAD / NEW.** Projects are stored inside the app. The current state is also autosaved whenever the app is backgrounded.
