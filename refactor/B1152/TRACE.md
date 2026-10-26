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
00445554  mov ecx,edi
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
- **`edi+0x180` (the effect/stage class byte)** — selects the launch geometry
  via four byte tests at `00476bec..00476c08` (eqgame.dis 152450-152458;
  Ghidra, eqgame.exe.c 95048-95049: `bVar1 = *(byte *)(iVar7 + 0x180);
  if ((bVar1 == 8) || ((0x14 < bVar1 && ((bVar1 < 0x18 ||
  (bVar1 == 0x1c))))))`):
  - `== 8` (`00476bec cmp al, 8` / `00476bee je 0x476d31`), **0x15..0x17**
    (`00476bf8 cmp al, 0x17` / `00476bfa jbe 0x476d31`) or `== 0x1c`
    (`00476c00 cmp al, 0x1c` / `00476c02 je 0x476d31`) → `00476d31`: **start
    at the target's own position** (`0x401b28(esi+0x50)` gives the target
    entity; its transform `+0x30/0x34/0x38` is copied into the descriptor's
    `+0x10/0x14/0x18`), all velocity terms zero, `+0x28 = 0`,
    `+0x1c = +0x20 = +0x24 = +0x28 = 0`. A no-motion (target-anchored) effect.
    So the **anchor set is {8, 0x15, 0x16, 0x17, 0x1c}**.
  - **every other class** → `00476c08`: the **flight** path below. The flight
    set is {0x00..0x07, 0x09..0x14} (via `00476bf4 cmp al, 0x14` /
    `00476bf6 jbe 0x476c08`) and {0x18..0x1b, 0x1d..0x1f} (no test catches
    them; they fall through to `00476c08` at eqgame.dis 152458).
- **The emitter stage** (`edi+0x1a8+0x50`, the per-spell "stage" record) is
  read for its `+0x50` value (`00476c18..00476c26`), sign-checked against the
  descriptor's `+0x50` and converted to a scalar with the `0x6410b8` (=0.02)
  factor. This is the value that drives the velocity below.

So the **selection rule**: a spell draws a *moving* projectile when its effect
record has the moving-stage flag (`edi+0x188 == 3`) **or** its stage-class byte
(`edi+0x180`) is in the flight set {0x00–0x07, 0x09–0x14, 0x18–0x1b,
0x1d–0x1f}; a spell with a stage class in the anchor set {8, 0x15, 0x16, 0x17,
0x1c} is *target-anchored* (no flight), and one whose record lacks a Middle
stage has no descriptor built at all (the `0x445569` remove path). That last
case is exactly **Frost Bolt** (measured §4).

## 3. Launch geometry, speed and arc (the `00476c08` flight path)

Constants are read from the binary at these VAs (float32 unless noted):

| VA | value | role (from the FPU sequence) |
|---|---|---|
| `0x6410b8` | `0.02` | cs→s scale on the stage time-difference (`fldm`… `fmul`, `00476c31`) |
| `0x63f4d8` | `0x4F800000` = +2³² (4294967296.0f) | added when the stage time-difference is negative (`fadd`, `00476c30`) — the 32-bit clock wrap correction |
| `0x64757c` | `32.5` | multiplier on the **start offset** (`fmul [esi+0x1c]` etc., `00476c7b`) |
| `0x647578` | `12` | subtracted from the start Z (`fsub`, `00476c9c`) |
| `0x647574` | `-72` | base of the time term: `time * (float)(esi+0x28) + (-72)` (`00476ca8`) |
| `0x64263c` | `5.5` | the **arc** scalar: `arc = 5.5 * (esi+0x1c)` when not flat (`00476ce8`) |
| `0x647570` | `20.5` | "spawn count" when [record+0x260] in {0x31, 0x9e, 0xc4} (00476daf-00476dc6) |
| `0x6410d0` | `1.5` | an alternative spawn count (else branch) |

Launch math, in order:

```
; stageScalar = float( time() - (stage->+0x50) ), 0x63f4d8 (0x4F800000 = +2^32,
;   the 32-bit clock-wrap correction) added only when that difference is negative, then *0.02
;   (0x6410b8)  -- the 0.02 reads as a centisecond->second scale on the
;   time() - offset difference (inferred; time() = 0x46d655, a global clock)
; launch vector: 0x47bf6c(target, 0) -> 3-float attach vector, taken ONLY for
;   creature types [edi+0x260] in {0x31, 0x9e, 0xc4} (sub-0x31/+0x6d/+0x26 cascade, 00476c3c-00476c52)
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
count" is chosen (`20.5` when [record+0x260] in {0x31, 0x9e, 0xc4}, else `1.5`), and finally
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
  Middle stages) has no missile despite `c123 = 3` (§7).
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
  recomputes `target_pos − projectile_pos` (`0x47f5a8..0x47f5db`). The
  sub-cascade at `0047f57b..0047f594` matches `[edi+0x120]` against **{0x21,
  0x47, 0x6a, 0x6c}** as cumulative deltas (`sub 0x21`/`sub 0x26`/`sub 0x23`/
  `dec`/`dec`, eqgame.dis 163811-163819; confirmed in `FUN_0047f4af`,
  eqgame.exe.c 101936-101952) to select the arrival position — {0x21, 0x6a,
  0x6c}: the `[esi+0x30/0x34/0x38]` position transformed by the target's
  matrix (`0047f596`); 0x47: the target entity's own transform (`0047f5fc`,
  `test ecx` at eqgame.dis 163859); anything else: the projectile's current
  position (the `0047f5e1` fallthrough, NULL vector). It then fires
  `FUN_0049b260(stage=2)` with the target entity and that position —
  i.e. the impact burst (`spelcc/spelcm/spelcf` for record 80) is spawned
  *where the projectile is*, on the tick the loop decides it has arrived.
- For spell **54** the removal routine has a dedicated branch:
  `00477956 mov eax, [esi+0x5c]` (the spell id), `00477959 cmp eax, 0x36`,
  `0047795c je 0x477a55` — which fires stage 2 **unconditionally at removal
  time**: the moving-offset subtract (`00477a55-0x477a6f`), the entity lookup
  (`00477a9b call 0x40cbd6`), `00477aa1 push 2`, `00477aa3 call 0x49b260` —
  with **no distance check on that path**. That is how Frost Bolt "arrives"
  with no missile at all: the descriptor's Target stage fires when the
  (instant) effect is torn down, at the target's position.
- Spell **859** (`00477962 cmp eax, 0x35b`) does not take that branch: it
  falls into the distance branch — `004779c6 call 0x439c41`,
  `004779cb fcomp dword ptr [0x645ec0]` (the constant is 40.0, read from the
  binary), `004779d6 jp 0x477aab` — distance-gated at < 40 world units, and
  its continuation is `00477a4e call 0x47f614` (`00477a53 jmp 0x477aab`),
  not the stage-2 fire. (Not implemented in fable — the B1153 FOLLOW-UP in
  `refactor/B1152/README.md`.)

Either way the **damage landing is server-authoritative** and arrives by
`OP_Action` at cast completion (measured §8); the client-side stages are the
cosmetic on top.

## 6. Death

`FUN_0047789d` unlinks the descriptor from the active-effect list
(`[0x905cd0]`), and if the effect's target (`+0x48`) is a live creature
(`[0x92551c]+0x4c == esi` and `[0x850ccc] == 8`) it tears down that creature's
effect handle. The per-spell arrival branch above (spell 54's unconditional
stage-2 fire; 859's distance-gated `0x47f614` call) sits inside this
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

| gem | spell | travel (c123) | record (f145) | Middle stages |
|---|---|---|---|---|
| 93 | Burst of Flame | 0 | 80 | **1** (edd 279) |
| 38 | Lightning Bolt | 3 | 80 | **1** (edd 279) |
| 54 | Frost Bolt | 3 | 82 | **0** |
| 41 | Weaken | 0 | 89 | 0 |
| 253 | Whirling Wind | 0 | 104 | 0 |
| 458 | Fire Spiral of Al'Kabo | 0 | 121 | 0 |
| 656 | Shock of Ice | 0 | 82 | 0 |

Only **93 and 38** are true missiles, and both use record 80 / edd 279. **54
Frost Bolt is the instructive negative**: `c123 = 3` but record 82 has **no
Middle stage**, so the spawner builds no descriptor and no missile flies — a
pure Source→Target burst. These two (38 = real missile, 54 = travel-3 no
missile) form the contrast pair for the legs.

**Column provenance (re-derived 2026-10-25 from the oracle's `spells_us.txt`):**
- **travel** = the 123rd `^`-separated field of `spells_us.txt` (1-based column
  123; 0-based field 122). It is **not a named field** in
  `docs/spellsnew_format.md` or the B1141 tables, so the earlier "(f122)" label
  is withdrawn as an unsupported field name. Re-derived: the 1-based col 123 of
  the seven gem rows gives 0 3 3 0 0 0 0 — exactly this column's values (python
  over `spells_us.txt`, ThinkPad `/home/user/eq-oracle/client/`).
- **record** = B1141's `f145` (the `spellsnew.eff` record index; 0-based field
  145 = 1-based col 146): 93/38→80, 54→82, 41→89, 253→104, 458→121, 656→82
  (matches `review/B1141/crosscheck.tsv`).
- **253 Whirling Wind**: the travel column (1-based col 123) = **0** (raw:
  `c122=13 c123=0 c124=35`), yet the *adjacent* 1-based col 124 — B1141's
  `f123`, the SpellAffectIndex into the old `spells.eff` — is **35**
  (`crosscheck.tsv` 253 row: col1=35, col3=104). B1130 measured record 35 as a
  travelling FX — "orbit: giant star-ribbon whirl" (B1130 README:305), in the
  "travelling sprite" class (B1130 README:390) — and B1141 as "whole body
  (target) -> frame-filling whirl" (crosscheck.tsv col 11). So 253's visible
  movement comes from the `f123=35` field, a **different column** from the
  travel column: the two coexist without conflict. The travel column marks the
  `FUN_00476b12` missile spawner's input, not a general "moves" flag.

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
(lightning 121 dmg vs the low-level target → corpse visible from frame 14199,
473.30 s); M2's
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

- **spell 38**: the in-flight missile (deep-gap bright-pixel census, box
  x660-760 y440-620, >=180 grayscale) is visible in the gap in frames 14170-
  14179 (472.33-472.63 s); the target-impact burst (the Target-stage emitters,
  §6) from frame 14188 (472.93 s); the target (Oraclewatch) a corpse from
  frame 14199 (473.30 s) — the 121-damage hit itself lands in the server
  stream on the same tick as `OP_Action` (`OP_FormattedMessage` string_id 9073,
  args Testcaster/Oraclewatch/121 at 472.816 s, `all-opcodes.tsv`).
  Strip: `strips/m1_38_flight_471.5-473.7.png` (6 cells,
  per-cell frame times 471.500 / 472.000 / 472.500 / 473.000 / 473.500 /
  473.700 s = frames 14145 / 14160 / 14175 / 14190 / 14205 / 14211; the
  in-flight missile in cell 3, the impact burst in cell 4, the corpse in
  cells 5-6). Per-cell gap-box census (>=180 grayscale) of those frames:
  16 / 0 / 38 / 42 / 67 / 66 (raw frames 14166 and 14181 read 0, so the
  missile is in cell 3 only). Regenerate the strip and re-run the census:
  `python3 ~/kb-evidence/kb-b1152-trace/strip_regen.py
  ~/kb-evidence/kb-b1152-trace/oracle/m1_rig38/video.mp4
  14145,14160,14175,14190,14205,14211
  refactor/B1152/strips/m1_38_flight_471.5-473.7.regen.png
  refactor/B1152/strips/m1_38_flight_471.5-473.7.png`
  (cells 1-5 come out byte-identical to the committed strip, cell 6 within
  56 px; the box and grayscale formula are encoded in the script).
- **spell 54**: cast glow around the caster only, **no projectile in the gap**
  (the deep-gap census stays at the pre-cast baseline through the whole window),
  then a frost burst at the target from frame 13729 (457.63 s) — consistent
  with "travel 3, 0 Middle stages". Strip: `strips/m2_54_cast_456.0-458.0.png`
  (6 cells, per-cell frame times 456.000 / 456.500 / 457.000 / 457.500 /
  457.633 / 458.000 s = frames 13680 / 13695 / 13710 / 13725 / 13729 / 13740;
  the arrival frost burst in cell 5, the previously solid-black last cell
  replaced by frame 13740). Per-cell gap-box census (>=180 grayscale) of
  those frames: 0 / 0 / 0 / 0 / 1226 / 181. Regenerate and re-run:
  `python3 ~/kb-evidence/kb-b1152-trace/strip_regen.py
  ~/kb-evidence/kb-b1152-trace/oracle/m2_rig54/video.mp4
  13680,13695,13710,13725,13729,13740
  refactor/B1152/strips/m2_54_cast_456.0-458.0.regen.png
  refactor/B1152/strips/m2_54_cast_456.0-458.0.png`
  (all six cells come out byte-identical to the committed strip). The ~458 s
  corpse visible in the strip is Nixx Darkpaw's, from an unrelated ~338 s
  death.

## 9. Summary of the mechanism (what a future FX unit needs)

1. A spell gets a projectile descriptor **once**, lazily, on its first live tick
   (`0x445556` → `FUN_00476b12`), and only if its record has a moving stage
   (`+0x188==3`) or a flight stage-class (`+0x180` in {0x00–0x07, 0x09–0x14,
   0x18–0x1b, 0x1d–0x1f}). Stage class in {8, 0x15, 0x16, 0x17, 0x1c} ⇒
   target-anchored (no flight); **no Middle stage ⇒ no descriptor at all**
   (the remove path).
2. **Speed** = `0.02 × (spell stage "run" value)` (per-axis), **arc** =
   `5.5 × launch-Z term`; start position = `32.5 × stageVec` (z − 12) plus the
   target attach-point vector from `FUN_0047bf6c`; a moving offset
   `t·(float)[esi+0x28] − 72` is added to z.
3. The **Middle stage starts** via `FUN_0049b260(stage=1)` and **ends when the
   Middle emitter's `life` elapses** (spell 38 / edd 279 = **0.25 s**).
4. **Per tick** (`0x460737` → `0x47f614` → `0x47f4af`) the
   distance-to-target is recomputed and the position advanced; **arrival is the
   Target stage (stage = 2) firing through `FUN_0049b260`** in that loop — not
   a client hit-test, not the damage packet. Spell 54 gets a dedicated,
   **unconditional** at-removal Target-stage fire (`0047795c je 0x477a55` →
   `0x477aa3 call 0x49b260`); spell 859's removal branch is a distance-gated
   (< `[0x645ec0]` = 40) `0x47f614` call, not the stage-2 fire (unimplemented
   in fable — B1153).
5. **Death** = `FUN_0047789d` unlink + `0x476ad4` cleanup; the Target-stage
   emitters (record 80: lives 1.5/1.0/1.5/1.25 s, all `delay 0.25 s`) play the
   impact burst at the target after the missile is gone.

Open items (not blocking this report): the exact integer semantics of the
`[esi+0x28]`/`[esi+0x1c]` fields are inferred from the FPU ordering, not from a
named struct; a future unit that ports this should confirm them against a second
spell (93, same record 80) in a caster-view leg where the Source aura is not in
the observer's band.
