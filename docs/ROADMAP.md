# Roadmap

Where this is going: one rig that runs on the desktop where it is built, and
on a handheld for playing out, driven by a foot controller. The same app on
both, scaled to the screen.

## Done

- **0.2** Undo, the Cabinet, portable project zips, context menus.
- **0.3** The GLFW app replaces the JUCE rack. The Board with groups and rig
  slots. Looper, tuner, clock. Recordings saved with the patch. MIDI learn,
  MIDI notes, controller feedback, gamepad. Stereo as left/right port pairs.
  TONE3000 browser. Touch mode. Windows builds.
- **Since 0.3.2** The inspector (range, curve and mod depth per knob),
  modulated knobs that move, session restore, hardware inputs that match the
  interface.

## Next

- Go through every module while playing it and fix what the notes say:
  default ranges, curves, units. The inspector exists to find out what those
  should be.
- The foot controller. The protocol is in `CONTROLLER.md`; the firmware is an
  ESP32-S3 patch made with Daisypatcher, kept as a template in both repos.
- MIDI clock in.
- Touch and gamepad fixes from real use on the Ally.

## Later

- Sub-patches: a group becomes a module with its own ports that can be saved
  and reused.
- One cable carrying two channels, if left/right pairs turn out to be clumsy.
- Exporting a rig as a Daisypatcher `.dpatch` so a board built here can run
  on a Daisy Seed pedal. The neural amp, the cabinet, the vocoder and the
  granular module would be marked as desktop-only. Importing the other way.
- A bootable image: a stripped-down Linux that starts straight into the rig
  and turns a PC into a dedicated multi-effect. Notes in `HANDHELD.md`.
- A named screen profile for 7-inch handhelds.
- OSC, a plug-in build, a node SDK. None of these are planned in detail.

## Constraints that apply to everything

- Nothing goes into the UI that does not scale with the window or cannot be
  reached with a gamepad.
- Every module has to meet a 128-sample deadline on a laptop CPU.
