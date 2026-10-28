# B1176 (#495): the local player is drawn at scale 1.0 while the client scales every actor

**THE ISSUE IS THE SPECIFICATION** (#495, asks 1–3, acceptance A–E). This README
carries the decompile trace (acceptance A), the fix (B), the live-leg table (C),
and the verification (D). The MR closes #495 (E).

**WILL'S RULE:** willeq copies the official client exactly. Every number below is
either read from the decompile (cited to the line) or measured on the live lane —
never inferred, and a deviation is never presented as an option.

The decompile is `review/B1141/ghidra/out/eqgame.exe.c` (Ghidra, the full text
decompile of the Titanium `eqgame.exe`), cross-checked against the raw binary
`/mnt/projects/EverQuestP1999/eqgame.exe` for the data dwords.

---

## 1. The scale the client draws every actor at (acceptance A: scale)

The client has **no local-player branch**: both EQPlayer build paths run the same
scale initialiser and the same scale applier, in order, for every player.

- Build path A: `review/B1141/ghidra/out/eqgame.exe.c:106152` calls
  `FUN_0047fa9e()` (the scale/camera-height initialiser, sec. 2), then
  `:106154` calls `FUN_0043c84b()`.
- Build path B: `review/B1141/ghidra/out/eqgame.exe.c:106406` calls
  `FUN_0047fa9e()`, then `:106416` calls `FUN_0043c84b()`.
- `FUN_0043c84b` hands `actor+0xC4` (the resolved scale) to the graphics actor;
  it skips the scale **only** for a null player/actor. There is no
  `isLocalPlayer` test anywhere in the scale path.

`FUN_0047fa9e` resolves the size and the scale in this order
(`review/B1141/ghidra/out/eqgame.exe.c:102153`):

1. **Size** (`actor+0x23c`): if the spawn's size word is not > 0
   (`:102188`), it is replaced by the race's default size, `FUN_0047a147`
   (`:102194`). This is the ONE number the player has: the wire's
   `Spawn_Struct` size with the client's race default for 0 — already unified
   by B1172/B1164 into `clientResolvedActorSize` (`actor_scale.h`).
2. **Scale** (`actor+0xC4`): `FUN_0047a0e9(size)` (`:102204`), which is
   `size × 0.2` for the classic races — `review/B1141/ghidra/out/eqgame.exe.c:97652`
   multiplies the size by `_DAT_00640fec` = `0x3e4ccccd` = **0.2**. The race
   override `FUN_0047a239` (`:102217`) then runs and, for the two
   `size/6 × 1.3` races (403, ≥405) and the forced-scale races, rewrites the
   scale; for every classic race (< 0x1a = 26) it returns before writing
   anything (`review/B1141/ghidra/out/eqgame.exe.c:97765-97766`).

This rule is already implemented in
`include/client/graphics/actor_scale.h` (`clientActorScale`, merged by B1172
#491) and pinned by `tests/test_actor_scale.cpp`. The figures this card needs:

| race / size | resolved size | scale (`size × 0.2`) |
|---|---|---|
| Human, size 6 (the live-leg human) | 6.0 | **1.2** |
| Dark Elf, default 5 (size 0 on the wire) | 5.0 | **1.0** |

---

## 2. The player's own camera and wire z, exactly as the client derives them
## (acceptance A: eye and wire z; ask 1a/1b/1c)

`FUN_0047fa9e` sets two more fields on the actor before the scale override runs
(`review/B1141/ghidra/out/eqgame.exe.c:102153`):

- `:102200-102201` — `actor+0x238 = FUN_004b6d42(...)`, i.e. the ini key
  **`ROffset`**, defaulting to `_DAT_0064b86c`. `FUN_004b6d42`
  (`review/B1141/ghidra/out/eqgame.exe.c:138498`) is
  `FUN_00575140("ROffset", _DAT_0064b86c, ...)`. The dword at `0x64b86c` is
  `0x40480000` = **3.125** (measured from the binary, `pe_dwords.py`). So the
  un-scaled `0x238` is 3.125 — the client model's origin-to-feet distance.
- `:102202` — `actor+0x234 = 0x40c66666` = **6.2** (the un-scaled eye model
  height).

For a player (`actor+0x1a8 != 0`) the scale is applied to **both**:

- `:102206-102207` — `0x238 = (actor+0xC4) × 0x238` = **scale × 3.125**.
- `:102208-102210` — `0x234 = 0x234 × (actor+0xC4)` = **scale × 6.2**
  (the `FUN_004899cc()` at `:102208` is the `0x234` accessor
  (`review/B1141/ghidra/out/eqgame.exe.c:109250`)).

The race override `FUN_0047a239` (`:102217`) may rewrite `0x234`/`0x238` for
forced-scale races, but for the classic races it returns first
(`review/B1141/ghidra/out/eqgame.exe.c:97765-97766`), so for Human/Dark-Elf:

> **`0x234 = 6.2 × s` and `0x238 = 3.125 × s`.**

### (a) The first-person camera eye height

The first-person camera function `005701c0`
(`review/B1141/ghidra/out/eqgame.exe.c:284488`) places the eye:

- `:284515-284516` — eye.x/y = player.x/y (`actor+0x30`/`0x34`); no horizontal
  offset.
- `:284517` — eye.z = player.z (`actor+0x38`), the base.
- `:284518-284519` — a corpse (`actor+0x251 == 'o'`) keeps eye.z = player.z.
- `:284522-284524` — otherwise **eye.z = player.z + `FUN_00489bf5()`**.

`FUN_00489bf5` (`review/B1141/ghidra/out/eqgame.exe.c:109359`) is the eye
height above the player's own z:

- `:109371-109372` — unmounted (the normal case): **`0x234 − 0x238`** =
  `(6.2 − 3.125) × s` = **3.075 × s**.
- `:109373-109379` — mounted on a player-mountable (race 0x14a adds
  `_DAT_0063f4d0`, else `_DAT_006410bc`); out of scope for a standing player.
- `:109383-109384` — the non-player-mounted variant (a different `0x44 ×
  0xC4` term); not the player's own first-person.

So the first-person **eye sits at `player.z + (6.2 − 3.125) × s`**. With the
z-settle below, that is `ground + 3.125 s + 3.075 s = ground + 6.2 s`.

The ini key **`FPCOffset`** (`FUN_004b6d29`,
`review/B1141/ghidra/out/eqgame.exe.c:138483-138485`) is read with default
`0x0` and is **not** applied in the unmounted first-person branch
(`:284522-284524` has no FPCOffset term); the oracle's ini does not set it.

### (b) The z the client draws and transmits (the wire z)

The z-settle `FUN_00479e37` (`review/B1141/ghidra/out/eqgame.exe.c:97453`)
puts the actor's z on the ground and lifts it by `0x238`:

- `:97462-97463` — raycast the ground under the actor (`FUN_0048a0fc`).
- `:97465` — `actor+0x48 = ground` (the settled ground z).
- `:97466` — **`actor+0x38 = ground + (actor+0x238)`** = **ground + 3.125 × s**.

`actor+0x38` is the position field the client both draws the model at **and**
transmits on the wire (T311 Leg A measured the same `ground + 0.625 ×
resolved size` for a size-6 human, `= 3.125 × 1.2 = 3.75`, so the two
formulations agree for the standard races). **The node origin is the wire
z.** This is the number fable was getting wrong: it placed the node at
`ground + measured-bbox`, not at `ground + 3.125 × s`.

### (c) The third-person camera

The third-person/orbit camera `005703a0`
(`review/B1141/ghidra/out/eqgame.exe.c:284668`) uses the **same** eye term:
the orbit centre is `player.z + FUN_00489bf5()` —
`review/B1141/ghidra/out/eqgame.exe.c:284622-284641` read `actor+0x38` then
added `FUN_00489bf5()` (the `0x234 − 0x238` term) before handing the position
to `FUN_0043a15c`. So the third-person orbit centres on
`ground + 3.125 s + 3.075 s = ground + 6.2 s`, exactly the first-person eye
line — the scale moves both cameras together, which is what the live leg
measures.

### Summary of the client rule (what fable now copies)

For the local player, at the resolved actor scale `s = clientActorScale`:

| quantity | client derivation | fable value |
|---|---|---|
| node scale | `s` (the same as every actor) | `entityNodeScale` (was 1.0) |
| node origin / wire z | `ground + 3.125 × s` (`0x238`) | `clientPlayerZOffsetFromGround` |
| eye / orbit centre above the node | `(6.2 − 3.125) × s` (`0x234 − 0x238`) | `clientCameraHeightAbovePlayerZ` |
| eye / orbit centre above ground | `6.2 × s` | the two above, added |

The three constants live in `include/client/graphics/actor_scale.h`:
`kClientEyeModelHeight = 6.2f` (`:102202`), `kClientROffsetModelHeight =
3.125f` (`:138498`, `0x64b86c`), and the two derivations
`clientCameraHeightAbovePlayerZ` / `clientPlayerZOffsetFromGround`.

---

## 3. What fable was doing (the deviation)

- **Node scale:** `entityNodeScale(isLocalPlayer, race, gender, size)`
  returned **1.0** for the local player and the client's actor scale for
  everyone else; `EQAnimatedMeshSceneNode::render` then used a
  translation-only world matrix for the player node, so the 1.0 was the
  actually-drawn scale. A human player (scale 1.2) was drawn 17 % too small.
- **Node origin / wire z:** every entity (player included) used
  `collisionZOffsetFromBounds(bbox, scale)` — the mesh's measured
  origin-to-feet distance — so the player's node (and thus its wire z) was
  `ground + bbox`, not the client's `ground + 3.125 × s`.
- **First-person eye:** the eye height came from the head bone's own vertex
  cluster, not the client's `0x234 − 0x238` term.

---

## 4. The fix (acceptance B)

| file | change |
|---|---|
| `include/client/graphics/actor_scale.h` | `kClientEyeModelHeight`, `kClientROffsetModelHeight`, `clientCameraHeightAbovePlayerZ`, `clientPlayerZOffsetFromGround` |
| `include/client/graphics/entity_renderer.h` | `entityNodeScale` drops `isLocalPlayer` (every spawn gets the client's actor scale); new `entityNodeCollisionZOffset` — the local player sits at `clientPlayerZOffsetFromGround`, every other entity keeps the measured bbox |
| `src/client/graphics/entity_mesh_build.cpp` | build sites call the no-player-branch `entityNodeScale` and `entityNodeCollisionZOffset` (animated, static, placeholder) |
| `src/client/graphics/entity_size.cpp` | the AT_Size rescale re-derives scale and the player's ROffset placement |
| `src/client/graphics/entity_build_step.cpp` | build sites updated to the same two helpers |
| `src/client/graphics/eq/animated_mesh_scene_node.cpp` | the player world matrix now carries the node's uniform scale (position + scale; rotation stays vertex-baked) |
| `src/client/graphics/eq/animated_mesh_eye.cpp` | the player's eye **height** is `0x234 − 0x238 = 3.075` model units (scaled), not the head cluster; the cluster still supplies the horizontal; the offset is multiplied by the node scale |
| `src/client/graphics/entity_first_person_draw.cpp` | comment re-based onto the client's derivation (the code just forwards the node offset) |

Tests (acceptance B): `tests/test_actor_scale.cpp`
(`ClientActorScale.TheB1176CameraHeights`, per race/size including the
dark-elf default that leaves the first-person view unchanged and the
size-6 human; plus the wire-equivalence `3.125 s == 0.625 × size`),
`tests/test_entity_size_change.cpp`
(`EntitySizeScale.TheLocalPlayerNodeHasTheClientActorScale`,
`EntitySizeScale.TheLocalPlayerNodeSitsAtTheClientROffsetPlacement` — the
size-6 human's node z is 3.75, the measured T311 wire z).

---

## 5. Live leg (lane 5, acceptance C)

The oracle + eqemu live on the ThinkPad; driven from this host with
`EQEMU_LANE=5` through the repo scripts. Observer = char 103 Oraclewatch
(acct willeqwatch); caster = char 67 Testcaster. Both races at the same spot,
both cameras; measured (never inferred), fable before/after.

| race (size) | scale | camera | oracle drawn h (px) | fable BEFORE drawn h (px) | fable AFTER drawn h (px) | wire z (oracle / after) |
|---|---|---|---|---|---|---|
| Human (6) | 1.2 | first-person | — | — | — | — |
| Human (6) | 1.2 | third-person | — | — | — | — |
| Dark Elf (5) | 1.0 | first-person | — | — | — | — |
| Dark Elf (5) | 1.0 | third-person | — | — | — | — |

**Expected (from sec. 2):** the dark-elf first-person view is **unchanged**
(scale 1.0, so `3.125 s` and `3.075 s` are the same before and after); the
human is drawn **1.2×** and its eye/centre and wire z move by the 1.2 factor.

*(table filled by the live leg below)*

---

## 6. Verification (acceptance D)

- Targeted ctest: the actor-scale / size / collision-z / first-person /
  local-player suites — all green (rbuild).
- Hygiene: `rbuild <wt> hygiene` green.
- CI: full pipeline green on the final head.
