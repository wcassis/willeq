# T300 Leg C — the live gate. The clamp does not hide the body.

Executor unit, slot3, branch `task/T300-legC`, cut from `task/T300-legB` @
`dddd8aef`. Contract: `refactor/T300_firstperson_real_mechanism.md`, **Leg C**.
Owns eqemu. Read first: `refactor/T300_leg_b_implementation.md`,
`refactor/T300_leg_a_near_plane.md`, `review/T300/README.md`,
`review/T299/T299_B_README.md`.

Media: `review/T300/` prefixed `T300_C_`, plus `review/T300/video_C/`.
Method and file-by-file guide: `review/T300/T300_C_README.md`.

**Nothing here is judged acceptable or unacceptable as a LOOK — Will judges.
What follows is what is in the pixels, and a diagnosis.**

---

## Verdict, per contract question

| # | question | answer |
|---|---|---|
| 1 | At max down-pitch, does fable show the player's own body? | **YES — FAIL.** Human male: own face fills the lower two thirds at the 65.40-deg clamp; first geometry at **26.5 +/- 1.2 deg** total down-pitch. Troll: own shoulders and hands at the clamp; first geometry at **60.6 +/- 1.5 deg**. |
| 2 | Is the body invisible at rest at every pitch from level to the clamp? | **NO — FAIL, and worse than question 1 implies.** It is visible from 26.5 deg down (Human male), and at **LEVEL pitch while running** the animated head bobs into the bottom of the frame. |
| 3 | Does the main-hand weapon appear during a swing, entering and leaving at a frame edge? | **NO — FAIL.** 285 first-person frames covering seven auto-attack damage exchanges with a two-handed Halberd: zero weapon geometry. |
| 4 | Does a foot appear on a kick? | **NO — FAIL.** Six kicks, every rendered frame captured: nothing. |
| 5 | Does first person clip world geometry no differently from third person? | **YES — PASS.** `near=1.000` measured in both modes in one run; a wall at point-blank renders solid and identical in both; ground renders with no hole at every pitch to the clamp. |
| 6 | More than one race? | **Done — Human male and Troll male.** Both show their own body inside the clamp. |

Leg B's §7.2 prediction ("this leg's own geometry measurement says the clamp
does not hide fable's body") is **confirmed live**. Its §4 per-race table is
**not** confirmed — see §3 below; the numbers are wrong in both directions.

## 1. Method (and why the negatives are load-bearing)

fable `build-gles2/bin/willeq` under `Xvfb :98`, `--egl-x11 -r 1024 768
--eqe1-path /mnt/projects/EverQuest_ETC1`, qeynos2, `#set time 11 0`,
Testchar (Human male Warrior 20, GM).

* **Every rendered frame captured** (`--dump-frames-dir <dir> --dump-every 1`),
  never stills — `dont-sample-transients-with-stills`, and T298's 25.2%
  false-null. Videos in `review/T300/video_C/`, each with the
  `encode_capture.sh` `.txt` carrying its measured fps/frames/span.
* **Pitch is driven through the real input path.** `playerPitch_` has exactly
  one writer (`updatePlayerMovement()`) and no scripted setter, so mouse-look
  is driven with `xdotool` (right button held, `mousemove_relative 0 4..5` =
  1.2-1.5 deg per tick, settle between steps). Each frame is tagged with the
  `[FP-CAMERA] lookPitch=` in force when it was written
  (`review/T300/T300_C_60/61_*.tsv`).
* **The combat negative has a positive control.** The same script, the same
  character, the same capture rate, `/camera follow`: the kick and the halberd
  swing are both plainly visible
  (`T300_C_33_thirdperson_CONTROL_kick_perframe.png`). So ~3-5 effective fps is
  not what emptied the first-person run.
* **The weapon has a control too.** Every rest measurement was repeated with
  the primary slot emptied in the DB; level pitch and the clamp are identical
  with and without the Halberd (`T300_C_13`, `T300_C_14`). (An early read that
  a shaft was in frame at level pitch was **wrong** — it was a world object at
  the spawn point, and the empty-slot control is what settled it.)

### Honest limits

* **fable has no UI-hide command.** The chat window (bottom-left ~310x140) and
  the hotbar (a bottom-centre strip ~y 720-760) are opaque in every frame. The
  region the body occupies is clear, but a small weapon sliver appearing ONLY
  under the chat box or the hotbar could be missed. Adding a UI toggle would
  make this class of evidence cleaner and is worth a line item.
* ~3-5 effective fps means the animation is sampled at that rate. Every
  rendered frame is present, so nothing rendered was missed; what is
  undersampled is animation PHASE. Mitigated by the control run and by six
  kicks plus seven attack exchanges.
* Two runs drifted into game night. The wall pair (`T300_C_50`) is dark but
  legible; everything the verdict rests on is daylight.

## 2. THE DIAGNOSIS

Leg B named three candidates in order — eye height, the mesh forward-axis
assumption, then the clamp angle. **The order is wrong. The clamp is the
primary cause, the eye height is a real and independent second defect, and the
axis assumption is neither confirmed nor the explanation.**

### (a) PRIMARY: the clamp is derived from the wrong condition

Measured live, in one run: `[FP-PROJ] firstPerson=true near=1.000
fovY=0.8586rad` -> vFOV **49.20 deg**, and
`[FP-CAMERA] ... maxDownPitch=65.40deg`. So at the clamp the frustum spans

```
65.40 - 24.60  =  40.8 deg below the horizon   (upper edge)
65.40 + 24.60  =  90.0 deg below the horizon   (lower edge, the NADIR)
```

`maxDownPitch = 90 - vFOV/2` is, by construction, **the angle at which the
frustum's lower edge REACHES the nadir**. The player's own body hangs in
exactly that cone: from the eye it occupies directions from roughly 52 deg
below the horizon (a Human male's nose) down to 90 deg (the feet). The clamp
therefore does not exclude the body's directions — **it is precisely the
condition that admits all of them.** "The lower edge never passes the nadir" is
a guarantee about a direction in which there is nothing to see; the geometry is
everywhere *above* the nadir on the near side.

The live numbers follow directly. First geometry enters at the frustum's lower
edge, so the entering feature sits at `entry + vFOV/2` below the horizon:

| race | entry (total down-pitch) | feature angle below horizon | clamp |
|---|---|---|---|
| Human male | **26.5 +/- 1.2** | 51.1 | 65.40 |
| Troll male | **60.6 +/- 1.5** | 85.2 | 65.40 |

To hide a Human male's face the clamp would have to be ~27 deg — **less than
half** what is implemented, and a severe restriction on looking down. And it
would have to be ~61 for a Troll. **The claimed race- and scale-invariance does
not survive contact with the models**: the angle from a crown-mounted eye to
the nearest body geometry is a property of the mesh's proportions, not of its
size, and Human and Troll differ by 34 degrees. No single down-pitch clamp both
hides the body and lets a player look at the ground in front of them.

### (b) SECOND, INDEPENDENT: the eye is at the STATIC bounding-box crown

`[FP-CAMERA] nodeOriginZ=3.7500 eyeAboveOrigin=3.6527` -> camera world
Y = **7.4027**. `eyeAboveOrigin` is `bbox.MaxEdge.Y`: the eye sits on the very
top of the head, not at the eyes, and it is a **constant taken from the rest
pose**.

Two consequences, both observed:

1. **The run cycle lifts the head above the constant.** At level pitch (the
   fixed 3.65-deg bias, no mouse-look at all), running forward, the player's own
   scalp rises into and out of the bottom of the frame with each stride
   (`T300_C_20`, `T300_C_21`). No pitch clamp of any value can fix this; the
   camera has to track the animated head, or clear it.
2. **It is why nothing clips.** Sitting ON the crown rather than inside the
   skull puts the whole head more than 1.0 unit from the eye, so the world's
   1.000 near plane never touches it. Put the eye where the eyes actually are —
   inside the head — and that same 1.000 plane swallows the face and chest.
   **That is exactly the mechanism T299 inferred for the official client, and
   fable's eye placement is what defeats it.** Leg B §3 is right that 3.65 and
   6.75 are the same eye in two frames, and right not to add 3.05; the defect is
   that the official 6.75 is measured to the official client's EYE while
   fable's 3.6527 is a bbox crown, and those two are not the same point on the
   model.

This is offered as the mechanism to investigate, not as an instruction: it is a
design call and Will judges. What is measured here is only that the eye is at
the crown, that the head passes above it during animation, and that the head is
outside the 1.0 near plane.

### (c) The mesh forward-axis assumption: not confirmed, and not the explanation

Leg B flagged that its headless sweep assumes the baked player mesh's `+Z` is
camera-forward at heading 0. Live:

| race | Leg B headless entry | live entry | error |
|---|---|---|---|
| Human male | 39.5 deg | **26.5** | headless 13 deg **optimistic** |
| Troll male | 0 (in frame at level pitch) | **60.6** | headless 61 deg **pessimistic** |

**The error changes sign between races**, so it is not a single consistent
"forward read as sideways". The headless table in Leg B §4 should not be used
as a quantitative guide to anything; whatever it measures, it is not the angle
a player sees. Note also that this question is **not observable live by turning**
— the model and the camera rotate together, so the body's position relative to
the frustum is heading-invariant (confirmed: runs at heading 0 and heading 128
give the same picture). Isolating it needs a headless fixture checked against a
live frame, which is its own unit.

## 3. What DID pass

* **The projection is what Leg B specified**, measured live:
  `near=1.000 fovY=0.8586rad aspect=1.3333` -> hFOV **62.8 deg**, clamp
  **65.40 deg**, fixed bias **3.65 deg**, no standoff (`adjust=0.00`).
* **First person clips exactly like third person.** `near=1.000` in both modes
  in the same run; a wall at point-blank is solid and identical in both
  (`T300_C_50`); the ground draws with no hole at every pitch to the clamp.
  Leg B's `FirstPersonClipsNoDifferentlyFromThirdPerson` is confirmed on a
  screen.
* **The player node really is drawn and really is animated in first person.**
  T296's withholding is gone: the body appears, and the third-person control
  shows the same animations playing. The plumbing works; only the hiding does
  not.

## 4. Follow-ups this leg names but does not do

1. **The hiding mechanism needs re-opening.** A down-pitch clamp alone cannot
   do it (§2a). The combination that fits every observation in T299/T300 is
   *eye at the real eye position + the ordinary near plane*; that is a code
   change and a design call, not this leg's to make.
2. **`getPlayerEyeHeightAboveNodeOrigin()` returns a rest-pose constant.** It
   needs to be animation-aware or explicitly documented as not being the eye.
3. **fable has no UI-hide command.** Every visual unit since T63 has fought the
   overlay. One command, modelled on `/newui`, would pay for itself.
4. **Weapon/limb visibility in first person is untested downstream of the
   hiding question** — with the body drawn and nothing clipped, a swing still
   produced no visible weapon, which means the halberd never enters a
   49.2-degree frustum from a crown-mounted eye. Whether it should is part of
   follow-up 1.

## 5. Verification

No source file was changed by this leg (docs and review media only).

| step | result |
|---|---|
| `bash scripts/build-gles2-x86.sh` | **EXIT=0** (the binary all captures were made with) |
| `cmake --build build -j24` | **EXIT=0** |
| `ctest --test-dir build -j6 --output-on-failure` | **EXIT=8** — `99% tests passed, 2 failed out of 3227`: `AudioManagerTest.PreloadCommonSounds` (documented ALSA host exception) and `check_build_tree_orphans` (see the note below). After removing the orphan, `check_build_tree_orphans` passes and the ALSA test is the sole failure, matching Leg B. |
| server | up.sh / down.sh, lock taken atomically and deleted, Testchar restored (zone_id=36, x=-77.9232, y=21.9086, z=3, h=190, race=1, weapon slots empty) |

**§5 notes — two stale-slot traps worth writing down, neither a code defect:**

1. **A slot's `build/` tree must be rebuilt after checking out this branch, or
   the source-parsing tests lie.** Running `ctest` first, against a `build/`
   left over from `fable`, produced two extra failures —
   `FirstPersonCameraSingleFormula.TunablesAreReferencedFromOneTUOnly` and
   `.TheOneBodyAppliesBothTunablesAndMouseLookPitch` — because the *old* test
   binary asserts that `kFirstPersonCameraStandoffUnits` and
   `kFirstPersonPitchDownRad` are still present while parsing the *new* source
   that no longer has them. `cmake --build build -j24` clears both.
2. **`check_build_tree_orphans` fails in any slot that built T296.** Leg B
   deleted `tests/test_viewmodel_pass.cpp`, but cmake never prunes `bin/`, so
   the stale `build/bin/test_viewmodel_pass` binary is left claimed by no
   target. `rm build/bin/test_viewmodel_pass` -> the test passes. Whoever
   merges this should expect the same in their tree.
