# B1152 (#472): the official client's projectile-spawner, `FUN_00476b12`, traced

Report-only unit. Traced the missile-spawner in the official Titanium `eqgame.exe`,
picked two representative spells from Char 67's gems, ran two lane-5 observer legs
(the B1134 rig: observer char 103 **Oraclewatch**, caster char 67 **Testcaster**,
~1.4 caster-heights apart, observer F9×3 camera), and measured the per-frame missile.
No game code was modified.

Evidence lives in `/home/user/kb-evidence/kb-b1152-trace/` (plans, `oracle/` legs,
captures, per-frame tracks); the deliverable files are this file plus
`refactor/B1152/missile_oracle.tsv` and `refactor/B1152/strips/*.png`.

Binary analyzed: `/mnt/projects/fable/review/B1141/bin/eqgame.exe` (PE image base
**0x400000**; all sections map VA→file 1:1, so file offset = VA − 0x400000).
Ghidra outputs: `eqgame.exe.c` (decompile) and `eqgame.dis` (linear disasm),
same base. Floats below were read straight from the binary at the VA, not quoted
from memory.

## 1. What the spawner is and who calls it

`FUN_00476b12` (`eqgame.dis` line 152387) is the **per-spell projectile setup /
launch** routine. It has exactly ONE call site, `0x445556`, inside a caller that
walks the active-effect list (`[0x905cd0]`, each effect 0x300 bytes, linked at
`+4`/`+8`) and, for each effect whose `+0x48` (a per-spell "fx descriptor") is
still NULL, calls the spawner once to build that descriptor; if the descriptor's
`+0x1005` byte comes back 1 it immediately calls the removal routine
`0x47789d` (`0x445569`) and frees (`0x60d0d5`) the descriptor. So a spell gets
its projectile descriptor lazily, once, the first tick the effect is live.

```
00445547  mov eax,[edi+8]            ; next effect in the active-effect list
0044554d  mov eax,[eax+0x48]         ; the effect's fx descriptor
00445550  cmp eax,ebx
00445552  jne 0x445577               ; already built -> advance
0445554  mov ecx,edi
00445556  call 0x476b12              ; FUN_00476b12 (this spawner), ecx = effect
0044555b  mov eax,[edi+0x48]
0044555e  cmp byte [eax+0x1005],1
00445565  jne 0x44559f
00445567  mov ecx,edi
00445569  call 0x47789d              ; remove (the descriptor said "no projectile")
0044556f  call 0x60d0d5              ; free
```

`FUN_00476f34` (`eqgame.dis` line 152713) is the **per-tick effect updater**: it
runs on the *same* descriptor (`[esi+0x10]` = pos, `[esi+0x2c]` = distance-to-
target) that the spawner fills in. Its single call site is `0x460737`.
`FUN_0047789d` (`eqgame.dis` line 153472) is the **removal** routine, called from
three sites: `0x43c268`, `0x477fc1`, `0x47cc21` (plus the `0x445569` no-projectile
path above).

## 2. Selection criteria — when a spell *is* a projectile

The spawner operates on `esi` = the spell effect, `esi+0x48` = its fx descriptor
(created on first call via `0x401b28(esi+0x4c)` when empty), and
`esi+0x48+0xfe4` = the per-spell record pointer it resolves.

- **`edi+0x188` (== 3)** — a "has a moving stage" flag on the record.
  `00476b43 / 00476bc3 / 00476cd5` test it three times. When it is 3, the record
  base is resolved differently (`+0x7530` vs `+0x32`) and the descriptor's arc
  term is **zeroed** (`00476ce0 fstp [esi+0x24]`), i.e. no arc — flat travel.
- **`edi+0x180` (the effect/stage class byte)** — selects the launch geometry:
  - `== 8` → `00476d31`: **start at the target's own position** (`0x401b28(esi+0x50)`
    gives the target entity; its transform `+0x30/0x34/0x38` is copied into the
    descriptor's `+0x10/0x14/0x18`), all velocity terms zero, `+0x28 = 0`,
    `+0x1c = +0x20 = +0x24 = +0x28 = 0`. A no-motion (target-anchored) effect.
  - `== 0x17 (23)` or `== 0x1c (28)` → same target-position branch (`00476d31`).
  - **`<= 0x14 (20)`** (the common projectile class) → `00476c08`: the
    **flight** path below.
  - `00476bf4` (0x15..0x16) and `00476bf8` (0x18..0x1f) fall through to no-op /
    other paths.
- **The emitter stage** (`edi+0x1a8+0x50`, the per-spell "stage" record) is
  read for its `+0x50` value (`00476c18..00476c26`), sign-checked against the
  descriptor's `+0x50` and converted to a scalar with the `0x6410b8` (=0.02)
  factor. This is the value that drives the velocity below.

So the **selection rule**: a spell draws a *moving* projectile when its effect
record has the moving-stage flag (`edi+0x188 == 3`) **or** its stage-class byte
(`edi+0x180`) is ≤ 0x14; a spell with stage class 8/0x17/0x1c is *target-anchored*
(no flight), and one whose record lacks a Middle stage has no descriptor built
at all (the `0x445569` remove path). That last case is exactly **Frost Bolt**
(measured §4).

## 3. Launch geometry, speed and arc (the `00476c08` flight path)

Constants are read from the binary at these VAs (float32 unless noted):

| VA | value | role (from the FPU sequence) |
|---|---|---|
| `0x6410b8` | `0.02` | cs→s scale on the stage time-difference (`fldm`… `fmul`, `00476c31`) |
| `0x63f4d8` | `0x4F800000` = −2⁻⁹ | added when the stage time-difference is negative (`fadd`, `00476c30`) |
| `0x64757c` | `32.5` | multiplier on the **start offset** (`fmul [esi+0x1c]` etc., `00476c7b`) |
| `0x647578` | `12` | subtracted from the start Z (`fsub`, `00476c9c`) |
| `0x647574` | `-72` | base of the time term: `time * (float)(esi+0x28) + (-72)` (`00476ca8`) |
| `0x64263c` | `5.5` | the **arc** scalar: `arc = 5.5 * (esi+0x1c)` when not flat (`00476ce8`) |
| `0x647570` | `20.5` | "spawn count" when the target is a creature type 0x0464 (see §5) |
| `0x6410d0` | `1.5` | an alternative spawn count (else branch) |

Launch math, in order:

```
; stageScalar = float( time() - (stage->+0x50) ), 0x63f4d8 (0x4F800000,
;   = -2^-9) added only when that difference is negative, then *0.02
;   (0x6410b8)  -- the 0.02 reads as a centisecond->second scale on the
;   time() - offset difference (inferred; time() = 0x46d655, a global clock)
; launch vector: 0x47bf6c(target, 0) -> 3-float attach vector, taken ONLY for
;   creature types [edi+0x260] in {0x31, 0x6d, 0x26} (00476c3c-00476c52)
; start position = attachVec + 32.5 * [esi+0x1c..0x24] (0x64757c), z -= 12 (0x647578)
; a moving offset term: t_now * (float)[esi+0x28] - 72 (0x647574), added to z
```

So the projectile's **speed is not a fixed constant** — the stage scalar is
`0.02 × (time() − the stage's +0x50 offset)`, i.e. a scaled elapsed-time
quantity (the 0.02 scale is the inferred cs→s factor), and the **arc/vertical
term is `5.5 × [esi+0x1c]`** (0x64263c) applied per-axis as
`[esi+0x10/14/18] += stageScalar·[esi+0x1c/0x20/0x24]`. The launch origin is the
target's attach vector (for those three creature types) offset by
`32.5×` a per-spell direction. Different spells therefore fly differently even
though they share this one spawner.

The launch helper `FUN_0047bf6c` (`eqgame.dis` line 159400) resolves the
target entity's attach-point vector via its vtable slot `+0xc0`
(`esi+0x1a8` = the target, `esi+0x260` checked against `0xd8`/`0x1d8` for
"has an attach point") and writes the 3-float start vector to `ebp-0xc`,
returning 1 on success — this is what `00476c60` feeds into the start position
above.

The descriptor is then finalised: `0x43972a` (a `0043972a` "make a projectile
record" call, `00476e3f`) builds the actual in-world projectile object; if the
effect is active (`[esi+0x60]`) the distance-to-target `[esi+0x2c]` is set by
`0x439377` (magnitude of target-minus-start), else `−1` (`0x476eaf`). A "spawn
count" is chosen (`20.5` for creature type `0x0464`, else `1.5`), and finally
`FUN_0049b260` (`eqgame.c` line 121984) is called with `stage = 1` (Middle) to
**fire the Middle-stage emitter** — this is the moment the visible missile
begins.

## 4. Middle-stage start and end

- **Start** — `FUN_0049b260` guards `param_1` (the stage) to 0/1/2 and, for the
  Middle stage, looks up the emitter row: `param_2[0x8b]` is the record base and
  the emitter id is `param_1*0x3a0 + 0x224 + param_2[0x8b]` (the `spellsnew.eff`
  Middle slot, record stride 0x10c, stage base `+0x40`, Middle at `+0x44`). If a
  Middle emitter id exists it is spawned via `0x479ee8` (sfx sound) and
  `0x49ab78` (the emitter itself). **A spell with no Middle stage never reaches
  this** — its descriptor is not built, which is why Frost Bolt (record 82, 0
  Middle stages) has no missile despite `travel_type = 3`.
- **End** — the Middle emitter's own **life** ends it. For spell 38 (Lightning
  Bolt, record 80) the Middle emitter is **edd 279**: `life = 0.25 s`,
  `burst = 20`, `rate = 15.0`, `delay = 0.0` (read from `spellsnew.edd`, entry
  `8 + 279*0x1a0`). So the visible missile exists for **0.25 s** after launch,
  then the emitter times out and the descriptor's distance/`+0x9c` are cleared by
  the per-tick updater.

## 5. Per-tick flight (`FUN_00476f34`) and arrival

`FUN_00476f34` copies the descriptor's state and, each tick:
- recomputes the **distance-to-target** `[esi+0x48]` as the magnitude of
  (target − projectile) via `0x439f26` (`004771c6`);
- clamps it: if `dist < [0x640ea0]` (a very large negative sentinel, i.e.
  NaN/invalid) it is set to `1.0` (`004771d1-004771e8`);
- for effect class 9 (`[ebp+0x18] == 0xb` at `00476fe5` / `004772a2`) it
  doubles a counter and applies a `0x41a877` step (a homing/seek correction);
- advances the projectile's stored position.

**Arrival** is *not* a client hit-test and it is *not* the damage packet
either — it is the **Target stage (stage = 2) firing through the same
`FUN_0049b260`**:

- For a normal missile the per-tick flight path
  (the caller of `FUN_00476f34` at `0x460737` → `0x47f614` → `0x47f4af`,
  `0047f5eb push 2; call 0x49b260`)
  recomputes `target_pos − projectile_pos` (`0x47f5a8..0x47f5db`), and once the
  spell's arrival condition holds (`[esi+0x4c]`-scaled time term, spell
  classes 0x21/0x23/0x26 handled, `0047f57b..0047f594`) it fires
  `FUN_0049b260(stage=2)` with the target entity and the current position —
  i.e. the impact burst (`spelcc/spelcm/spelcf` for record 80) is spawned
  *where the projectile is*, on the tick the loop decides it has arrived.
- For spell **54** (and 859) the removal routine has a dedicated branch
  (`00477959 cmp eax, 0x36 / cmp eax, 0x35b`): it subtracts the descriptor's
  own offset from its position, resolves the target entity via `0x40cbd6`, and
  fires `FUN_0049b260(stage=2)` **at removal time** (`00477aa1 push 2; call
  0x49b260`) — gated by `distance < [0x645ec0] = 40` world units
  (`004779cb fcomp [0x645ec0]`). That is how Frost Bolt "arrives" with no
  missile at all: the descriptor's Target stage fires when the (instant)
  effect is torn down, at the target's position.

Either way the **damage landing is server-authoritative** and arrives by
`OP_Action` at cast completion (measured §8); the client-side stages are the
cosmetic on top.

## 6. Death

`FUN_0047789d` unlinks the descriptor from the active-effect list
(`[0x905cd0]`), and if the effect's target (`+0x48`) is a live creature
(`[0x92551c]+0x4c == esi` and `[0x850ccc] == 8`) it tears down that creature's
effect handle. The per-spell arrival branch above (54/859) sits inside this
same routine, just before the unlink. For the descriptor itself "death" is the
list unlink plus a `0x476ad4` cleanup when the effect's sub-state `+0x63 == 9`
and it is the first sub-effect. The Target-stage emitters (record 80: edd 280
life 1.5 s, edd 281 life 1.0 s, edd 282 life 1.5 s, edd 9 life 1.25 s, all
`delay 0.25 s`) continue playing *at the target* after the missile is gone —
that is the impact burst the strip shows.

## 7. The two measured spells (Char 67's gems)

Char 67 (Testcaster, the caster) has the fixed gem set
`{93, 38, 54, 41, 253, 458, 656}`. Scanning all seven against
`spells_us.txt` + `spellsnew.eff`:

| gem | spell | travel (f122) | record | Middle stages |
|---|---|---|---|---|
| 93 | Burst of Flame | 0 | 80 | **1** (edd 279) |
| 38 | Lightning Bolt | 3 | 80 | **1** (edd 279) |
| 54 | Frost Bolt | 3 | 82 | **0** |
| 41 | Weaken | 0 | 89 | 0 |
| 253 | Whirling Wind | 0 | 104 | 0 |
| 458 | Fire Spiral of Al'Kabo | 0 | 121 | 0 |
| 656 | Shock of Ice | 0 | 82 | 0 |

Only **93 and 38** are true missiles, and both use record 80 / edd 279. **54
Frost Bolt is the instructive negative**: `travel_type = 3` (the "missile" wire
class) but record 82 has **no Middle stage**, so the spawner builds no descriptor
and no missile flies — a pure Source→Target burst. These two (38 = real missile,
54 = travel-3 no missile) form the contrast pair for the legs.

## 8. Lane-5 legs and measurements

Rig: B1134 observer rig on lane 5 (ThinkPad). Observer = char 103 **Oraclewatch**
at B1130's caster pose (−67.6, 434.4, 3.75, h160); caster = char 67 **Testcaster**
at the TrollF spot (−74, 428, 3.75, h160); ~9.05 world units ≈ **1.4 caster
heights**; observer camera F9×3; video at 30 fps (1280×800). Both legs ran clean
(rc=0) and char 67 was restored (class 12, gems re-memmed); the lane lock was
released. Plans: `plans/m1_rig38.tsv`, `plans/m2_rig54.tsv`.

Packet clock (observer's own stream, `OP_BeginCast`/`OP_Action`/`OP_Death`,
video-relative after calibration):

| leg | spell | BeginCast (video s) | cast_ms | OP_Action (video s) | cast s | target OP_Death (video s) |
|---|---|---|---|---|---|---|
| M1 | 38 | 470.285 | 2500 | 472.817 | 2.532 | 472.817 (same tick) |
| M2 | 54 | 456.133 | 1000 | 457.175 | 1.041 | — (no death) |

Note the `OP_Action` (server-side resolution) lands **at cast completion**, and
for spell 38 the target dies on the same tick — the client-side missile flight is
a cosmetic that plays *over* an already-resolved hit. M1 one-shot the observer
(lightning 121 dmg vs the low-level target → corpse visible from ~473.5 s); M2's
frost bolt did not kill, so the observer stayed up for the second cast.

**Per-frame missile track** (differential blob detector on the 30 fps frames in
the caster↔observer band): the spell-38 missile is a **short burst of
orange/white star particles** (`spelcb.png` — the *same* texture as the caster's
Source aura), not a single solid object. It is **not reliably separable from the
caster's Source aura by blob area**, because the two share a texture and the
Source burst is larger; the per-frame `n`/centroid in `measure/*_track.tsv` is
therefore confounded with the Source aura and is reported as **not a clean
single-track** (the mechanism is authoritative from §2–§6, not from a shaky
pixel track). What *is* cleanly visible frame-to-frame is:

- **spell 38**: in flight (right→left, caster toward target) in the frame at
  ~472.5 s; target is a corpse by ~473.5 s. Strip:
  `strips/m1_38_flight_471.0-473.0.png` (6 cells; projectile in cell 5, moving
  right→left; corpse in cell 6).
- **spell 54**: cast glow around the caster only, **no projectile in the gap**,
  then a frost burst at the target — consistent with "travel 3, 0 Middle stages".
  Strip: `strips/m2_54_cast_455.5-457.5.png`.

## 9. Summary of the mechanism (what a future FX unit needs)

1. A spell gets a projectile descriptor **once**, lazily, on its first live tick
   (`0x445556` → `FUN_00476b12`), and only if its record has a moving stage
   (`+0x188==3`) or a flight stage-class (`+0x180 ≤ 0x14`). Stage class
   8/0x17/0x1c ⇒ target-anchored (no flight); **no Middle stage ⇒ no
   descriptor at all** (the remove path).
2. **Speed** = `0.02 × (spell stage "run" value)` (per-axis), **arc** =
   `5.5 × launch-Z term`; start position = `32.5 × stageVec` (z − 12) plus the
   target attach-point vector from `FUN_0047bf6c`; a moving offset
   `t·(float)[esi+0x28] − 72` is added to z.
3. The **Middle stage starts** via `FUN_0049b260(stage=1)` and **ends when the
   Middle emitter's `life` elapses** (spell 38 / edd 279 = **0.25 s**).
4. **Per tick** (`0x460737` → `0x47f614` → `0x47f4af`) the
   distance-to-target is recomputed and the position advanced; **arrival is the
   Target stage (stage = 2) firing through `FUN_0049b260`** in that loop — not
   a client hit-test, not the damage packet. Spells 54/859 get a dedicated
   at-removal Target-stage fire gated by distance < 40 (0x645ec0).
5. **Death** = `FUN_0047789d` unlink + `0x476ad4` cleanup; the Target-stage
   emitters (record 80: lives 1.5/1.0/1.5/1.25 s, all `delay 0.25 s`) play the
   impact burst at the target after the missile is gone.

Open items (not blocking this report): the exact integer semantics of the
`[esi+0x28]`/`[esi+0x1c]` fields are inferred from the FPU ordering, not from a
named struct; a future unit that ports this should confirm them against a second
spell (93, same record 80) in a caster-view leg where the Source aura is not in
the observer's band.
