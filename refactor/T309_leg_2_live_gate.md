# T309 Leg 2 — the live gate on the measured first-person draw rule

Executor unit, slot1, branch `task/T309-gate`, cut from `task/T309` @ `6e1f4634`.
Contract: `refactor/T309_draw_rule_implementation.md`, **Leg 2**. Owns eqemu.
Read first: `refactor/T307_official_draw_rule.md` (absent from the tree; the
measurement survives in `review/T307/README.md`), `review/T307/README.md`,
`review/T299/README.md`, `refactor/T300_leg_c_live_gate.md`.

Media: `/mnt/projects/fable/review/T309/`, guide in that directory's `README.md`.

**Nothing here is judged acceptable or unacceptable as a LOOK — Will judges.
What follows is what is in the pixels, with its method.**

---

## Verdict, per contract question

(filled in as each leg lands; see the numbered sections)

## 1. Method

fable `build-gles2/bin/willeq` built from this branch, under `Xvfb :98`,
`--egl-x11 -r 1024 768 --eqe1-path /mnt/projects/EverQuest_ETC1 --no-audio`,
account `willeq_test` / **Testchar** (Human male Warrior 20, GM), zone
**qeynos2**, `#set time 11 0` (daylight confirmed in every judged frame).

* **Video, not stills.** `--dump-frames-dir <dir> --dump-every 1` — EVERY
  rendered frame is written. ~6.4 effective fps in the combat run; that rate is
  an artefact of per-frame glReadPixels + PNG encode, not a performance
  measurement.
* **UI hidden with fable's own `/hideui`** (T301 Leg D) in every judged run, so
  T300 Leg C's "a sliver could hide under the opaque chat panel" caveat does not
  apply here.
* **Pitch is driven through the real input path** — `playerPitch_` has exactly
  one writer (`updatePlayerMovement()`), so mouse-look is driven with `xdotool`
  (right button held, `mousemove_relative -- 0 5` = 1.5 deg per tick). Each
  frame is tagged with the `[FP-CAMERA] lookPitch=` in force when it was
  written; the index is shipped as a TSV per run.
* **The draw rule logs its own transitions** (`--log-module=GRAPHICS:DEBUG`
  gives `first-person draw rule: player body DRAWN/hidden (anim='...')`). That
  log is used only as an INDEX into the frames. Every verdict below rests on
  pixels.
* Target: `#npctypespawn 77002` — `a_training_dummy`, level 75, 9e8 hp,
  maxdmg 5. The same class of target T307 filmed against, and unkillable, so a
  swing sequence never ends early. Weapon: item 5006 **Halberd**, delay 50 —
  T307's exact weapon and its 5.0 s timer.

### Site traps found and worked around (record these; they cost most of an hour)

1. **`character_data.zone_id` 36 is `befallen`, not `qeynos2`** — 36 is
   `zone.zoneidnumber` for befallen (qeynos2's zoneidnumber is 2). The
   restore target named in the dispatch is therefore befallen, and it is
   restored as such. Working zone is reached in-session with `/say #zone
   qeynos2`, which leaves the DB untouched.
2. **`#goto` loses the player's own model.** After a `#goto` teleport the
   camera moves but the player's scene node does not follow: third person
   renders an empty view. Reproduced twice (`site`, `ctrl3p` runs), absent
   without `#goto` (`recon3`, `ctrl3p3`). **This is a real client defect,
   incidental to this unit, and it is not the draw rule** — it happens with
   `cameraMode=Follow` and the rule is a no-op outside first person.
3. **`#depopzone` also leaves the player's own model unrendered**
   (`ctrl3p2`). Same signature. Both were avoided; every judged run does
   neither.
4. qeynos2's zone-in point is crowded with wandering NPCs, and one standing at
   point-blank puts a stranger's FACE in the lower frame. T307 warned about
   exactly this class with its training-dummy ring. Every "nothing is drawn"
   claim below is therefore backed by a **numeric** null against a rest
   baseline, not by eyeballing.

## 2. Q3 — EMOTES DRAW NOTHING. The discriminator holds.

**PASS, by the same measurement T307 used on the official client.**

`review/T309/` run `fpclamp`, camera at the maximum down-pitch clamp, UI hidden,
camera untouched, four emotes (`/wave /dance /cheer /laugh`, all confirmed
executed in the client log).

| window | frames | max per-frame differing pixels vs the rest baseline |
|---|---|---|
| rest hold, no input (noise floor) | 116 | **0.0000%** |
| **all four emotes** | **296** | **0.0000%** |
| auto-attack window (positive control) | 595 | **79.1884%** (frame_000657) |

Threshold: any channel differing by more than 12/255. Baseline
`frame_000559.png`, a hidden-at-rest frame at the clamp.

T307's official-client null was 2250 consecutive frames at 0.0000%. fable
reproduces the null, and the positive control in the same run at the same camera
rules out "the scan cannot see anything".

The predicate is reading the right signal. The client log shows the emotes DO
reach the local player's own node —
`setEntityAnimation: spawn_id=285 animation 's16'/'s07'/'s21' not found,
falling back to 's01'` — so an `s`-family animation was played on the player's
own animator and the rule still withheld it. This is not a vacuous null.

**One honest caveat on the control.** In fable three of the four emotes fall
back to `s01` because `s16`/`s07`/`s21` are absent from the model, so fable's
third-person emotes are not as visually distinct as T307's official-client
control panel. The discriminator is unaffected — a non-attack animation played
on the player's own node did not draw — but fable's emote animation coverage is
a separate gap worth a board item.

## 3. Q1 (part) — nothing at rest, at every pitch INCLUDING the clamp

**PASS at rest.** Run `fpclamp`, a 0 -> -89.0 deg mouse-look sweep in 1.5 deg
ticks (total down-pitch 3.65 -> **92.65 deg**; the effective floor is the +/-89
gimbal guard, which bites before the -91.25 deg draw clamp). 62 distinct pitch
steps, every rendered frame captured.

* The draw rule logged **one** transition in the whole pre-combat phase — to
  `hidden` at `p01` — and never went back.
* Pixels: bare ground at every step from 14 deg down to the clamp. Contact
  sheet `T309_A_10_rest_pitch_sweep_perpitch.png`.
* This is the exact case T300 Leg C failed: at its 65.40 deg clamp a Human
  male's own face filled the lower two thirds
  (`review/T300/T300_C_12`). Here the same camera shows ground.

## 4. Q2/Q5 (part) — the body and the halberd ARE drawn, at the clamp

15 auto-attack swings (`c03`) and 3 kicks (`c01`) in run `fpclamp`.

* **The body is drawn for the duration of the swing and gone between swings.**
  `T309_A_20_CLAMP_swing_every_rendered_frame.png` is every rendered frame
  across one swing: bare ground, then the player's own shoulder, gold-trimmed
  belt, bare tan arm and grey-white cloth filling the frame, then bare ground.
  Full-res `frames/T309_B_10_CLAMP_swing_own_body_f657.png`.
* **The kick draws the halberd in full** — T307's discriminator that kills
  "only the animating limb". `frames/T309_B_11_CLAMP_kick_own_halberd_f1614.png`
  shows the pale wooden shaft and pale blue-white spearhead at top right while
  the leg is what is animating. Colours match T307's third-person reference
  panel exactly.

Drawn-window durations, from the rule's own transitions (indices into the
frames, not the verdict):

| animation | n | mean drawn duration | period |
|---|---|---|---|
| swing `c03` | 15 | **1.52 s** | 5.0 s (the Halberd's delay 50) |
| kick `c01` | 3 | **1.56 s** | commanded |

T307 measured the official client at **1.13 s** mean on the same 5.0 s timer
(22.5% duty cycle). fable's is **1.52 s / 30.4%** — about 35% longer per swing.
That is a duration difference in the animation, not a difference in the rule.
It is reported, not judged.

## 5. Still owed by this leg

- Q1 running head-bob at level pitch
- Q2 at LEVEL pitch, where T307 saw the weapon rather than the body
- weapon-snap-on-reveal check
- Q4 out-of-range auto-attack
- Q6 world clipping, first person vs third
- Q7 a second race
