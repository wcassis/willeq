# T309 Leg 2 — the live gate on the measured first-person draw rule

Executor unit, slot1, branch `task/T309-gate`, cut from `task/T309` @ `6e1f4634`.
Contract: `refactor/T309_draw_rule_implementation.md`, **Leg 2**. Owns eqemu.
Read first: `review/T307/README.md` (the measurement; the write-up
`refactor/T307_official_draw_rule.md` it names is **not present in the tree** on
this branch), `review/T299/README.md`, `refactor/T300_leg_c_live_gate.md`.

Media: `/mnt/projects/fable/review/T309/`, file-by-file guide in that
directory's `README.md`.

**Nothing here is judged acceptable or unacceptable as a LOOK — Will judges.
What follows is what is in the pixels, plus a diagnosis.**

---

## Verdict, per contract question

| # | question | answer |
|---|---|---|
| 1 | Body invisible at rest at every pitch **including the clamp**, and while running? | **YES — PASS.** 62 pitch steps to a total down-pitch of **92.65 deg**: bare ground throughout, the rule logged one transition (to hidden) and never left it. Running forward at level pitch: no scalp, no head. Both of T300 Leg C's failures are closed. |
| 2 | Body **and weapon** visible for the duration of a swing, absent between swings? | **YES at the rule and at the clamp — but the LOOK at LEVEL PITCH does not match the oracle.** 15 swings at the clamp and 21 at level pitch (29 more on Troll), drawn 1.508 s mean on the Halberd's 5.0 s timer, nothing in between. At the clamp this reproduces T307. **At level pitch the player's own FACE fills the entire viewport on every swing**, where T307 filmed the spearhead sweeping in low with the body below the frustum. See §3. |
| 3 | Do emotes draw nothing? | **YES — PASS, and by T307's own measurement.** 296 consecutive frames across four emotes at **0.0000%** max per-frame difference, with a **79.19%** positive control in the same run at the same camera. The predicate is on the right signal. |
| 4 | Does an out-of-range auto-attack draw nothing? | **YES — PASS.** `/attack` on for 70 s with the target at **19.6** units and 30 s at **190** units: no swing produced, **396 frames all `hidden`**, no player geometry. The only motion a difference scan flags is a bat crossing frame. |
| 5 | Does a kick draw the body and the held weapon? | **YES — PASS.** 3 kicks at the clamp; the two-handed halberd is drawn in full — pale shaft, pale blue-white spearhead — while the leg is what animates. T307's "not only the animating limb", reproduced. |
| 6 | Does first person clip world geometry no differently from third? | **YES — PASS.** Flush into a Qeynos stone wall: solid in first person, solid in `follow`, solid on return to first person, no hole and no difference. Ground renders solid at every pitch to past the nadir. |
| 7 | A race beyond Human male? | **Done — Troll male (`TRM`).** The rule behaves identically: bare ground between swings, own arm on each swing at the clamp. **The level-pitch face-fill is present and worse** — larger, 3-4 frames instead of 2-3. |
| — | **Does the weapon hang or snap on reveal?** | **NO — measured, not asserted.** See §4. The withhold mechanism works live. |

Leg 1's §7 list of what it could not prove is discharged: the body is invisible
at rest at every pitch including the clamp; body and weapon are visible for the
duration of a swing; the animation phase on reveal is right; emotes draw nothing
live. Its one unmeasurable claim that **failed** is none of those — it is the
consequence of Leg 1b's forward-offset decision, §3.

## 1. Method

fable `build-gles2/bin/willeq` built from this branch (both trees green, exit 0),
under `Xvfb :98`, `--egl-x11 -r 1024 768 --eqe1-path /mnt/projects/EverQuest_ETC1
--no-audio`, account `willeq_test` / **Testchar** (Warrior 20, GM), zone
**qeynos2**, `#set time 11 0` — daylight confirmed in every judged frame.

* **Video, not stills.** `--dump-frames-dir <dir> --dump-every 1`: EVERY rendered
  frame is written. ~6.4-6.8 effective fps, an artefact of per-frame
  `glReadPixels` + PNG encode, **not** a performance measurement. Videos in
  `review/T309/video/`, each with its `encode_capture.sh` `.txt`.
* **UI hidden with fable's own `/hideui`** (T301 Leg D) in every judged run.
  T300 Leg C's "a sliver could be hiding under the opaque chat panel" caveat
  does not apply to anything here.
* **Pitch through the real input path.** `playerPitch_` has exactly one writer
  (`updatePlayerMovement()`), so mouse-look is driven with `xdotool` (button
  held, `mousemove_relative -- 0 5` = 1.5 deg/tick). Movement likewise: `w`/`s`
  keydown/keyup, not a scripted setter.
* **The draw rule's own log is used ONLY as an index.**
  `--log-module=GRAPHICS:DEBUG` yields
  `first-person draw rule: player body DRAWN/hidden (anim='...')` on every
  transition. Every verdict rests on pixels or on a pixel-difference scan.
  Per-frame indices shipped as `review/T309/T309_C_6x_*.tsv`.
* Target: `#npctypespawn 77002` — `a_training_dummy`, level 75, 9e8 hp, maxdmg 5:
  the same class of target T307 filmed against, and unkillable, so a swing
  sequence never ends early. Weapon: item **5006 Halberd**, delay 50 — T307's
  exact weapon and its 5.0 s timer.
* Attribution of geometry to the player rests on the third-person colour
  reference `T309_A_70` (maroon trousers, bare tan arms, dark blue-grey tunic,
  pale wooden shaft, pale blue-white spearhead), which matches T307's own
  reference panel, plus the fact that the geometry **blinks** at the swing
  cadence while every world object and NPC in frame is static.

### Site traps found (record these — they cost the better part of an hour)

1. **`character_data.zone_id = 36` is `befallen`, not qeynos2.** 36 is
   `zone.zoneidnumber` for befallen; qeynos2's is **2**. The restore target
   named in the dispatch is therefore befallen, and it is restored as such.
   The working zone is reached in-session with `/say #zone qeynos2`, leaving
   the DB untouched.
2. **`#goto` leaves the player's own model unrendered.** After the teleport the
   camera moves but the player's scene node does not follow; third person then
   renders an empty view. Reproduced twice (`site`, `ctrl3p`), absent without it
   (`recon3`, `ctrl3p3`).
3. **`#depopzone` has the same signature** (`ctrl3p2`).
   2 and 3 are **real client defects, incidental to this unit, and not the draw
   rule** — they occur with `cameraMode=Follow`, where the rule is a no-op and
   logs no transition. Worth a board item. No judged run uses either command.
4. qeynos2's zone-in point is crowded with wandering NPCs, and one at
   point-blank puts a **stranger's face** in the lower frame — the same class of
   trap as T307's training-dummy ground ring. Every "nothing is drawn" claim
   below is therefore backed by a **numeric null**, not by eye.
5. Backpedalling to open a gap is fast: a 1.2 s `s` hold put the target at
   **19.6 units**, outside melee range, and produced a whole run with no swings
   at all. That run was kept — it is the Q4 evidence.

## 2. Q3 — EMOTES DRAW NOTHING. The discriminator holds.

Run `fpclamp`, camera at the maximum down-pitch clamp, UI hidden, camera
untouched, four emotes (`/wave /dance /cheer /laugh`, all confirmed executed in
the client log).

| window | frames | max per-frame differing pixels vs the rest baseline |
|---|---|---|
| rest hold, no input (noise floor) | 116 | **0.0000%** |
| **all four emotes** | **296** | **0.0000%** |
| auto-attack window, same run, same camera (positive control) | 595 | **79.1884%** (`frame_000657`) |

Threshold: any channel differing by more than 12/255. Baseline
`frame_000559.png`, a hidden-at-rest frame at the clamp.

T307's official-client null was 2250 consecutive frames at 0.0000%. fable
reproduces the null, and the positive control in the same run rules out "the
scan cannot see anything".

**It is not a vacuous null.** The client log shows the emotes DO reach the local
player's own animator —
`setEntityAnimation: spawn_id=285 animation 's16'/'s07'/'s21' not found,
falling back to 's01'`. An `s`-family animation was played on the player's own
node and the rule still withheld it.

**Two honest caveats.** fable has no `/bow` and no `/point` (the emote shortcuts
registered are `wave`, `dance`, `cheer`, `laugh`), so the four emotes shot are
not the four the contract named. And three of the four fall back to `s01`
because `s16`/`s07`/`s21` are absent from the model, so fable's third-person
emotes are far less visually distinct than T307's control panel. The
discriminator is unaffected — the question is whether a non-attack animation on
the player's own node draws, and it does not — but **fable's emote animation
coverage is a real gap and deserves its own board item.**

## 3. THE FINDING — at LEVEL pitch every swing fills the viewport with the player's own face

Run `fplevel2`: level pitch (the fixed 3.65 deg bias, no mouse-look), UI hidden,
target at **7.6 units**, 21 auto-attack swings, every rendered frame captured.

Between swings the view is clean — the field, the trees, the target. On **every
one of the 21 swings**, for 2-3 rendered frames, the player's own head — hair,
brow, eyes, moustache, beard — covers the **entire** 1024x768 viewport
(`frames/T309_B_20_LEVELPITCH_swing_own_face_fills_view_f206.png`;
`T309_A_31` is six consecutive swings; `video/T309_fplevel2_capture.mp4`).

**T307 filmed the official client at level pitch doing something else entirely:**
the body hangs **below** the frustum and what enters is the pale spearhead,
sweeping in from the bottom-left across two rendered frames of each swing, on
10 of 10 swings (`review/T307/T307_A_06`, `review/T307/frames/T307_B_20`).

Troll male is the same, worse: 3-4 frames, a larger head, yellow eyes and tusks
across the whole viewport (`T309_A_60`, `frames/T309_B_40`).

### Diagnosis — this is Leg 1b's forward offset, not the draw rule

Measured live, in the level-pitch run:

```
[FP-PROJ]   firstPerson=true near=1.000 fovY=0.8586rad aspect=1.3333
[FP-CAMERA] nodeOriginZ=3.0000 eyeOffset=(-0.0094,3.0774,-0.1692) fromHead=true
            lookPitch=0.0000deg fixedPitchDown=3.6500deg maxDownPitch=94.90deg
```

`fromHead=true` and the lateral component is the head cluster's own **0.17
behind the model axis** for a Human male, i.e. `kEyeForwardOffsetUnits = 0.0`
exactly as Leg 1b set it. The eye therefore sits inside the skull, the
first-person near plane is **1.000** (unchanged from the world value), and the
head's own geometry is on the far side of it. The moment the draw rule stops
withholding the model, that geometry is in front of the camera and it is all
there is.

Leg 1b's reasoning for 0.0 was sound as far as it went — T307 filmed the body
filling the **upper** frame at the clamp, which an eye shoved 0.83-1.0 units
out in front of the chest cannot see. That constraint is real and the clamp
frames here confirm it (§4 below matches T307). What was not checked at the same
time is the **level-pitch** constraint from the same measurement, and it points
the other way: at level pitch the official client's eye is far enough forward
(or its near plane far enough out) that the head is not in the view at all.

The two constraints are not satisfiable by the forward offset alone. Something
else has to give — the near plane in first person, a per-race eye placement
inside the head cluster, or head-geometry suppression distinct from the
whole-model withhold. **This leg does not pick one. It is a design call and Will
judges.** What is measured here is only that at 0.0 the head fills the view on
every swing, on two races, and that this is not what T307 filmed.

Also note: **Leg 1b's canary is not tripped by this.**
`FirstPersonWeaponAttachmentTest.HeldWeaponIsHeadingInvariantInTheCameraFrame`
asserts the weapon grip is in FRONT of the eye at the c01 attack frame, and it
is (0.848 units). The weapon being in front of the eye and the head being in
front of the eye are both true; the canary only guards the first.

## 4. The weapon does not hang or snap on reveal — measured

The contract's named failure mode: if the model froze while withheld, the
**first** drawn frame of window N+1 would be the pose window N ended on.

Run `fpclamp`, 18 drawn windows of 8-11 rendered frames each:

| comparison | differing pixels |
|---|---|
| `last(W1)` vs `first(W2)` | **72.579%** |
| `last(W2)` vs `first(W3)` | **72.579%** |
| `last(W3)` vs `first(W4)` | **78.116%** |
| `last(W4)` vs `first(W5)` | **72.581%** |
| `last(W5)` vs `first(W6)` | **72.579%** |
| `last(W6)` vs `first(W7)` | **78.119%** |
| `first(W2)` vs `first(W3)` | **0.000%** |
| `first(W5)` vs `first(W6)` | **0.000%** |
| `first(W1)`/`first(W3)`/`first(W4)`/`first(W6)` vs their successors | 44.671% (the capture-phase alternate) |

The reveal pose is **72-78% different** from the pose the previous window ended
on, so nothing is being held. And onsets are **byte-identical (0.000%)** between
windows sampled at the same capture phase, so the animation restarts
deterministically at its true first pose rather than resuming somewhere stale.
Within a window, consecutive frames differ by 15-53%: the model animates
smoothly through the drawn interval.

The withhold-not-`setVisible` mechanism works live. Leg 1's whole reason for it
is vindicated.

## 5. Duration and duty cycle

From the rule's own transitions (an index, not the verdict):

| animation | n | mean drawn duration | period | duty cycle |
|---|---|---|---|---|
| swing `c03`, Human, clamp | 15 | **1.508 s** (1.428-1.659) | onset-to-onset median **5.045 s** | **29.9%** |
| swing `c03`, Human, level | 21 | **1.204 s** (0.955-1.360) | median **5.036 s** | **23.9%** |
| swing `c03`, **Troll** | 29 | **1.469 s** (1.176-1.796) | median **5.004 s** | **29.4%** |
| kick `c01`, Human, clamp | 3 | **1.560 s** (1.532-1.602) | commanded | — |

T307 measured the official client at **1.13 s** mean on the same 5.0 s timer
(22.5% duty cycle) and kicks at 0.72 s. fable's level-pitch swings land close
(1.204 s / 23.9%); the clamp run's 1.508 s is ~33% longer, and kicks are more
than twice T307's. The measured period matches the Halberd's 5.0 s delay to
within 45 ms in all three runs. That is a difference in the **animations' own lengths**, not in
the rule, and it is reported rather than judged.

## 6. Honest limits

* ~6.4-6.8 effective fps means animation **phase** is undersampled; every
  rendered frame is present, so nothing rendered was missed. A 1.5 s drawn
  window is 8-11 frames, and 36 swings plus 3 kicks were captured.
* The phase offset between the drawn window and the swing's own damage event
  is not measured here either — same reason as T307: the UI was hidden.
* Bash, ranged, casting, being hit, jumping and sit/stand remain unshot by
  anyone. Leg 1 leaves them drawing nothing and says so.
* The level-pitch runs had 5 stacked training dummies at 7.6 units, accumulated
  from earlier `#npctypespawn` calls in the same zone process. They are static
  and never occlude the player's own geometry, which enters at point-blank.
* Nothing in this leg is judged as a **look**. §3 states a mismatch against a
  measurement; whether the mismatch matters, and which of the three candidate
  fixes is right, is Will's call.

## 7. Deliverables

* `/mnt/projects/fable/review/T309/README.md` — the file-by-file guide
* `/mnt/projects/fable/review/T309/T309_A_*.png` — 10 contact sheets
* `/mnt/projects/fable/review/T309/frames/T309_B_*.png` — 7 full-res frames
* `/mnt/projects/fable/review/T309/T309_C_6*.tsv` — per-frame pitch/rule indices
* `/mnt/projects/fable/review/T309/video/*.mp4` + `.txt` — 5 captures with their
  measured fps/frame count/span

## 8. Server state

eqemu taken under the atomic noclobber lock, brought up with
`scripts/eqemu/up.sh qeynos2`, and brought down with `down.sh`; the lock deleted.
Testchar restored to **zone_id=36, x=-77.9232, y=21.9086, z=3, h=190**, race
back to **1** (temporarily 9 for the Troll leg), and the Halberd (item 5006,
slot 13) that this leg equipped removed again — the character had **no equipment
at all** as found.
