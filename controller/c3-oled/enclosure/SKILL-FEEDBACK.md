# Feedback on the enclosure-design skill and kit

From building the SignalPatch C3 case (2026-10-07).

1. **The library has no panel-mounted parts.** `pot_rd901f` assumes a board
   at Z = 0 under the pot. A handheld built from loose parts needs the pot,
   an encoder, a panel button and a wall jack hanging from the panel by
   their nuts. This project's `parts_lib.py` adds `pot_rd901f_panel`,
   `encoder_ec11_panel`, `button7_panel` and `jack14_wall`, plus number
   dicts for the EC11, the PBS-110 button, the 1/4" jack, the ESP32-C3 OLED
   board, its OLED panel, the GY-521 and M2 thread-forming screws. They are
   candidates for the master library; not copied there yet.

2. **Wire space is not modelled.** Hand-wired parts need room below their
   pins. The access volumes added here start 0.35 mm from their own part's
   envelope, because the judge grows envelopes by their clearance and an
   access volume touching its own part reads as a conflict. A note in
   `kit.md` would have saved one build.

3. **Findings are for disagreements only.** Parts with no drawing at all
   (the 7 mm buttons, the board's thickness, the OLED's stack height) have
   nothing to compare. They were registered as findings with a review note
   so they show in the build log; a separate "gap" kind would read better.

4. **Stand-ins along Y.** `blender_render.py` draws plugs only along X.
   The USB-C plug and the jack plug here run along Y and are drawn as boxes.

5. **`write_assembly` colours** only know the part names `shell` and `lid`.

6. **Research delegation.** The skill says to send research agents per group
   of parts. The user preferred the research done directly in the session;
   for a handful of parts that was quick enough.

7. **Kit bug, fixed in the master and here: a cavity cut erases what is
   added inside it.** A part is `union(add) - union(cut)`. `spec_board.py`
   (and this project's first spec) added bosses and then cut the cavity,
   which removed them. The judge passed (nothing collides with a missing
   boss) and the mesh check passed (the shell is still a closed solid); only
   the print-plate render showed the empty shell. The Lunetta 100 reference
   shell in `~/Projects/4010666/enclosure/stl/shell.stl` has lost its four
   board bosses this way: a slice 1.1 mm above its floor shows only the walls.
   Fixes: `ekit.additions_check` and a build stage ("additions survive the
   cuts": an added solid keeping under 10 % of its volume fails);
   `spec_board.py` now adds `Diff(block, [cavity])`; `references/checks.md`
   and `kit.md` say so. The Lunetta project itself was not rebuilt.
