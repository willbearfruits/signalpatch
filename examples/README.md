# Example patches

Five patches after pedals and modules that do odd things. Each is one pedal on
the Board (press Tab) with four knobs; in the rack you can see how it is made.
Open them from FILE > Examples. Guitar goes into input 1, sound comes out of
outputs 1 and 2, and every patch ends in a limiter.

| Patch | After | What it does | Knobs |
| --- | --- | --- | --- |
| `clouds` | Mutable Instruments Clouds | Two grain clouds over the guitar, one an octave up, into a long reverb. The grain position wanders by itself and playing harder thickens the cloud. | Size, Density, octave level, reverb mix |
| `whammy` | DigiTech Whammy | The Treadle knob bends the guitar up to an octave. MIDI-learn it to an expression pedal. Mix at 50% is the harmony mode. | Treadle, Mix, Window, Glide |
| `fuzz-factory` | Z.Vex Fuzz Factory | A gated fuzz with a feedback loop around it. Past about 70% Stab it squeals when you stop playing, and Squeal tunes the pitch of that. | Gate, Drive, Stab, Squeal |
| `rainbow-machine` | EarthQuaker Devices Rainbow Machine | A short delay into a pitch shifter, fed back into the delay, so each echo is shifted again and the repeats climb. | Pitch, Tracking, Magic, wet level |
| `slicer` | Boss Slicer | A clock steps two eight-step patterns: one chops the volume, one moves a filter. Draw your own patterns on the sequencers in the rack. | Tempo, Cutoff, Resonance, Smooth |

They use only built-in modules, so no amp captures or impulses are needed. Put
a Neural Amp after any of them.

Two of them lean on the inspector: the Whammy's pitch knob is limited to 0 to
+12 semitones so the Treadle sweeps exactly an octave, and the Slicer's gain
knob runs from -60 to 0 dB so a step at full opens it and a step at zero
closes it.

If a feedback patch latches its guard off (the Fuzz Factory with everything
up), press RESET LOOP on the guard module.

`make_examples.py` writes the files. `SIGNALPATCH_RENDER_EXAMPLES=<folder>
signalpatch_tests` renders each one with a plucked test string to a WAV.
