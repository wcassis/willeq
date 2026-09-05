# T361 — Ultravision against the oracle (B067, and B060's table question)

Measurement first, then match; slot1; eqemu lock (take it atomically; it
must be ABSENT — if present, read it and STOP). Class A. Read the B067 and
B060 board text and `review/T358/README.md` first: fable's Ultravision at
night is worth +4.9% brightness while the day/night cycle is worth 4x, and
the only consumers of `visionType` scale 218 data-only point lights. B066
(the swapped switch) is being fixed in T360 in parallel — branch from the
tip you are given and do NOT touch `sim_animation.cpp`'s switch.

## Leg A — measure the official client (the golden)

The rig: `review/T328/scripts/login-as-traced.sh` (Xvfb + Wine Titanium
client; screenshots with `import -window`), T311's death capture for video
under Wine, the `eq-oracle-provisioning` memory (account/character
creation is automated). Same server, same zone (qeynos2), same position
and heading as T358 (Testchar's recorded spot: zone 2, x=-74, y=428.04,
z=3.75, h=160), forced night (`#set time 22 0` — verify the submit line,
T358 lesson: blind chat input silently loses text).

1. Official client, night, no buff: capture 3 s of video, then stills
   every 0.5 s — the 400x300 world crop's mean luminance and RGB.
2. `#castspell 46` on the official client's character (invulnerable OFF),
   wait for the landing, capture the same. Then `#nukebuffs`, capture.
3. Repeat 1–2 with Infravision (`#castspell 45`? — find Infravision's id in
   `spells_new`; record it) and, if the character can be a Dark Elf
   (racial floor), one zone-in with no buff.
4. Record ALSO what changes qualitatively: does the whole scene lift
   (ambient), does the sky change, do only near objects lift (a fog / draw
   distance change), does the palette shift? Frame diffs, not adjectives.
5. Same protocol on fable at the same tip, same spot, same time — the
   T358 driver. Both sets go to `review/T361/` with a table: client ×
   condition → luminance, R, G, B, and the ratio to the no-buff night frame.

## Leg B — match

With the measured ratio in hand, implement the closest mechanism that
reproduces it: the obvious candidate is raising the zone shader's ambient
(`ZoneShader::setAmbientColor`) from the vision type, with the value
solved by A/B against the oracle table (a parameter picked by ARGUMENT is
the smell — search until fable's night+Ultravision frame matches the
oracle's within the still-to-still noise floor T358 measured, 1.3%). If
the oracle shows a palette shift or fog change instead, implement THAT.
Tests: a unit test on the vision→ambient mapping (pre-fix failing) and the
live A/B frames in the README. Strike B067; if the oracle's Dark Elf
zone-in answers B060, record the answer on B060 (it stays class C for Will
only if it still needs a call — say which).

## Proof

Leg A is the proof of Leg B. Build, `ctest -j6`, visual gate standalone +
ARM (graphics files change; rebuild all three trees — willeq.db v6), file
sizes. Restore every server flag/time; `down.sh`; delete the lock.

STOP: the official client will not cast (10 minutes of trying, record
why); the measured oracle effect cannot be reached by any single parameter
in 2 hours of search (report the table and the nearest reach as a board
note; do not commit a guessed value).
