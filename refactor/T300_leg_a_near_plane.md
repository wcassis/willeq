# T300 Leg A — the near plane, measured. The near-plane hypothesis is REFUTED.

**Headline: there is no large near clip plane, and there is no plane of any size
doing the hiding.** Driving the official Titanium client under Wine/Xvfb against
our own eqemu server, world geometry and NPC geometry are drawn at point-blank
range from the eye; the player's own arm and hand ARE drawn during a swing; and
growing the player model five times over does not make the body appear. The one
mechanism that fits every frame is **a clamped first-person pitch**: the body
hangs directly below the eye, and the down-pitch clamp puts the bottom edge of
the frustum at approximately the nadir, so the body is never inside the view
volume at rest. A limb or weapon that swings forward and up enters the frustum
and is drawn.

Nothing below is self-declared acceptable. It is a description of what is in the
pixels plus measurements with their method and their error bars. Will judges.

---

## Session facts

| thing | value |
|---|---|
| client | official `eqgame.exe patchme`, Wine + Xvfb `:99`, llvmpipe, `LP_NUM_THREADS=1` |
| window | 1024x768 at root **(-1, 30)**; every capture taken with `import -window <wid>` or `x11grab :99+0,30`, never a root-origin crop (T298's trap) |
| character | `willeq_test` / **Testchar**, Human male Warrior, temporarily level 20, **Halberd 5006 in slot 13** (2H, longest lever), slot 14 empty |
| zone | **qeynos2** North Qeynos, `#set time 11 0`, `#weather 0` |
| ground | flat grass at `(-74, 428)` and `(-300, 428)`; `#loc` reports **Best Z 0.00** and player z **3.75** there |
| probes | `a_training_dummy` (npc_types 77002) spawned with `#npctypespawn` at coordinates read back from `#loc` |

**Coordinate trap, recorded for the next unit:** `character_data.zone_id` takes
the **`zone.zoneidnumber`** (qeynos2 = **2**), not `zone.id` (303). Writing 303
logs the character into Titanium's zone 303, *Nobles' Causeway*, whose geometry
has nothing to do with the server's map — the first 40 minutes of this session
were spent inside the wrong zone believing the coordinates were wrong.

---

## 1. hFOV, re-derived independently: **62.7 deg**

Not inherited from T298. Method, which needs no known camera pose beyond the one
`#goto` sets and no assumption about heading units:

1. Seven `a_training_dummy` NPCs spawned at exactly known coordinates — at
   `y = 488`, `x = -74 + dx` for `dx = -40, -25, -12, 0, +12, +25, +40` — each
   spawned by teleporting to the coordinate with `#goto` and spawning in place.
2. Camera placed at `(-74, 428, 3.75)`, heading 0, so every dummy is at a known
   horizontal angle `theta = atan(dx / 60)` from the view axis.
3. Each dummy's horizontal centroid measured from the pixels (reddish-brown mask
   against grass, column-weighted centroid), giving screen offsets from the
   `dx = 0` dummy.
4. `f = offset / tan(theta)` per dummy, for the four dummies that are fully
   inside the frame. `hFOV = 2 * atan(511.5 / f)`.

| frame | f from dx=-12 | +12 | -25 | +25 | mean |
|---|---|---|---|---|---|
| `T300_A_30` | 837.5 | 845.5 | 835.9 | 839.0 | 839.5 |
| `T300_A_31` (carries `#loc` in the same frame) | 833.0 | 843.0 | 840.7 | 840.5 | 839.3 |

**f = 839.4 +/- 4.0 px (sd, n = 8)** →
**hFOV = 62.71 deg, statistical range 62.47 .. 62.95 deg**
(vFOV = 49.11 deg at 1024x768).

*Systematic term, stated honestly:* the whole scale rides on the dummies really
being at `y = 488`. A 1-unit error in that baseline moves f by 1.7% and hFOV by
0.85 deg, so the defensible figure is **62.7 +/- 0.3 (stat) +/- 0.9 (baseline)
deg**. The two frames are minutes apart and agree to 0.2 px, so the dummies did
not drift between them. The internal ratio check also holds: measured
`offset(25) / offset(12) = 2.078` against the geometric `2.083`.

**This confirms T298's 62.8 deg by an independent route.** Fable should target
62.7 deg; the difference from 62.8 is inside both error bars.

---

## 2. The near plane: an upper bound, not a large value

### 2a. Ground at the eye's own height is drawn
At max down-pitch, standing, the ground fills the frame
(`T300_A_11`, `T300_A_23`). The eye is ~6.75 above that ground. A plane at 6.87
— the value the implementation leg computes as the minimum needed to swallow a
Human male at rest — would have erased the ground with the body.

### 2b. Scene geometry is drawn at point-blank range
A dummy was spawned at a `#loc`-verified coordinate and the camera walked onto it
in 0.5-unit steps by `#goto` (`T300_A_21`, traverse d = 6, 4, 3, 2, 1, 0.5, 0).
At **d = 0** — camera and NPC on the same x,y — the model's head and shoulders
are drawn sharply across the lower two-thirds of the frame with no cut
(`T300_A_20`). The nearest drawn geometry there is well inside 1 unit of the eye.

### 2c. The player's own arm is drawn at the eye
`T300_A_10` is a single rendered frame from a halberd swing: the shaft, **a hand
gripping it, a forearm and a shoulder mass**, all at point-blank range, running
off the bottom-right of the frame. That geometry is a fraction of a unit from the
camera and it is not clipped.

**Bracket: the official client's near plane is < 1 unit; the frames support no
tighter figure and I will not guess one.** It behaves like an ordinary small
world-camera near plane. It is emphatically not 6.9, and it is not the lever that
hides the body.

---

## 3. Size sweep — the plane is not derived from the model either

`#size` verified applying from the client's own echo ("Size set for yourself to
30.00").

| test | result | file |
|---|---|---|
| rest, max down-pitch, **size 6** (normal) | no body | `T300_A_11` |
| rest, max down-pitch, **size 15** | no body; camera visibly higher | `T300_A_12` |
| rest, max down-pitch, **size 30** | no body; ground still drawn at ~34 units | `T300_A_13` |
| **/sit at size 30** | no body | `T300_A_14` |
| **45 s of /wave /bow /dance /point at size 30**, UI hidden, **417 distinct rendered frames** | **no player geometry in any frame** | `T300_A_15`, `video/T300_A_v2` |

Answers to the three questions put to this leg:

1. **The body does NOT become visible at large size.** Five times normal scale,
   sitting, dancing and pointing — nothing. So the hiding is not a fixed distance.
2. **A size-derived plane is refuted too.** At size 30 the eye is ~34 units above
   the ground and **the ground is still drawn**. A plane scaled with the model
   (6.9 x 5 = ~35) would have eaten it.
3. **Shrinking** (`#size 3`, `#size 1`) changed nothing at the bottom of the
   frame. Note the camera height did not visibly fall below the size-6 height for
   these values, so this row is weaker than the grow rows and I am not resting
   anything on it.

---

## 4. What the pixels DO support: a clamped down-pitch

Every observation lands on one mechanism, and it is scale-invariant, which is
exactly what Will's objection demands:

* **The body hangs directly below the eye.** Its angular position under the
  camera is the same whatever the model's size — which is why size changes
  nothing. A distance-based rule cannot be scale-invariant; an angular one is.
* **The down-pitch is clamped short of the nadir.** At maximum down-pitch a
  dummy 10 units ahead (its base at 34.0 deg depression) is entirely above the
  top edge of the frame (`T300_A_23`), and a dummy only becomes visible when the
  camera is within about 2-3 units of it, entering from the **top** edge
  (`T300_A_21`, `T300_A_22`). With vFOV 49.1 deg that puts the top edge of the
  frustum at roughly 40 deg depression and the **bottom edge at roughly the
  nadir**: **max down-pitch ~65 deg, bracket 55 .. 75 deg.** The player's own
  feet sit right at or just past that lower edge, which is why looking "straight
  down" never shows them.
* **Geometry that appears, appears from a frame edge.** The arm and halberd in
  `T300_A_10` run off the bottom-right corner. There is no straight cut across
  the middle of the frame anywhere in this session's footage. T299 read the same
  thing in its own frames and called it a plane; the frame boundary is the
  simpler and, on this evidence, the correct reading.
* **A swing rotates the arm up and forward, into the frustum.** That is why the
  weapon and the kicking boot reappear while the resting body never does —
  world-space animation crossing a frustum edge, not geometry crossing a plane.

So of the three candidates put to this leg: **(a) one global near plane — no.
(b) a plane derived from the model size — no. (c) the hiding is not a plane at
all** — supported, with the specific mechanism being the pitch clamp, and with
the player model rendered normally throughout.

**Implication for Leg B, stated plainly:** implementing a near plane as "the
single lever that hides the body" would be implementing a mechanism the official
client does not use, and it would clip walls, doorways and ground the official
client draws. The lever is the first-person pitch clamp, together with drawing
the ordinary player model at the eyes. The exact clamp angle is the one number
worth a short dedicated follow-up run; this leg brackets it at 55-75 deg.

---

## 5. Deviations, anomalies and honest gaps

* **Level 2 -> 20 -> 2** on Testchar, as T299 did, so that Kick exists. Fully
  reverted. Halberd added to slot 13 and removed; slot 14 left empty.
* **General inventory** holds 8x *Scaled Curskin* (slots 23-30) and a *Wooden
  Fishing Tackle* (slot 33). I did not loot, buy or summon anything, and I did
  not check those slots at the start, so I cannot prove they pre-date this
  session — flagged rather than deleted, because deleting could destroy state
  another unit put there.
* **Client camera desync.** For roughly ten minutes one client instance kept
  rendering from its old position while `#loc` and the client's own `/loc` both
  reported the teleported coordinates. Killing and relaunching the client cleared
  it and it did not recur. Any live unit that teleports with `#goto` should
  verify the *view* changed, not only the coordinates.
* **Vertical `#goto` does not stick.** The client is snapped back to standing
  height within ~3 s even with `#flymode 1`, so the camera cannot be lowered
  toward the ground. That is why the near plane is reported as an upper bound
  from point-blank geometry rather than as a threshold from a height sweep.
* **The weapon-truncation measurement in the contract was not performed as
  written**, and on this evidence it cannot be: it presumes a truncation, and the
  frames show the weapon and arm leaving the frame at its edge rather than being
  cut at a plane. The mechanism result replaces it.
* **Pitch control under xdotool is unreliable** — the client samples key state
  per rendered frame at 6-9 fps, so short taps are dropped and held keys behave
  non-linearly. A future unit wanting a specific pitch should bisect with
  `keydown/sleep/keyup` and verify against the horizon in each frame.
* Testchar restored: level 2, cur_hp 60, zone_id **36**, x -77.9232, y 21.9086,
  z 3, h 190; `#size` and `#flymode` are runtime-only and were reset before
  logout. eqemu shut down with `down.sh`, `.eqemu-lock` deleted.

## Media

`/mnt/projects/fable/review/T300/` — see `README.md` there.
