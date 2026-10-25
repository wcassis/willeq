# B1151 trace: five CEmitterDef fields in CParticleSystem::Render — bytes to C++

Target: `EQGraphicsDX9.dll` (Ti 5.9.255, B1141 decompile + disassembly).
The particle render is `CParticleSystem::Render` (`FUN_100605b0`, decompile line 68017).
Register map (confirmed from the prologue):
- `edi` = `CEmitterDef*` (the 416-byte emitter descriptor)
- `ebp` = the emitter/attach context (holds the attach object at +0x68, bone at +0x6c, track at +0x70)
- `esi` = the per-particle or per-emitter runtime state (varies by block)

Cross-checked against raw `/mnt/projects/EverQuestP1999/spellsnew.edd` (2,118 records)
and fable's `config/race_models.json` + `entityScaleForServerSize`.

## 0. The 8-byte header and the raw-file == in-memory CEmitterDef

`spellsnew.edd` = 8-byte header (`45 44 44 00 31 31 30 00` = "EDD\0" + ASCII
"110\0") followed by 2,118 × 416-byte `CEmitterDef` records. Total = 881,096
bytes = exact file size.
```
$ head -c 8 spellsnew.edd | od -An -tx1
 45 44 44 00 31 31 30 00
```
With this header accounted for, the raw-file census matches B1141's
in-memory census **exactly** for every field below. The CONV function
(0x1005eef0, decompile line 67132) copies record words into the def array and
rewrites a subset of fields; +0x6C, +0x70, +0x74, +0xA8, +0x130, +0x134, +0x178
are **not** rewritten, so the file value is the value `Render` reads.

Record layout (first fields): name at +0x00 (64 bytes), texture at +0x40 (64 bytes),
then the fields traced below. Records 0-based **2116/2117 are the last two**
(`VetRewards_Y2_a` / `VetRewards_Y7_a`); `VetRewards_Y1_a` / `VetRewards_Y1_b`
are 2114/2115 (the two D178 records). Re-derived from the raw file:
`python3 refactor/B1151/edd_census.py <spellsnew.edd>` prints
`e2114 VetRewards_Y1_a … e2115 VetRewards_Y1_b … e2116 VetRewards_Y2_a …
e2117 VetRewards_Y7_a` (records 2114..2117, 0-based, of 2118).

## (a) +0x6C — the attach-scale flag: read path, multiplier, value

**The flag.** `Render` reads D6C at four sites:
0x100608b5 (`mov ecx,[edi+0x6c]`), 0x1006091c (`mov eax,[edi+0x6c]`),
0x100609e1 and 0x10060ae0 (`cmp [edi+0x6c],ebx`) — each compared against 1.
There is no `fld 0x3f800000` anywhere in Render. Instead, three stack slots are
seeded with the float literal 1.0f (`0x3f800000`) before the scale reads:
`mov [esp+0x30],0x3f800000` (0x100608bf), `mov [esp+0x48],0x3f800000`
(0x100608c7), `mov [esp+0xa0],0x3f800000` (0x100608cf); the scale reads that
run overwrite the slots with the attach/attached-object scale, so when D6C == 0
(the reads are skipped) the factor stays 1.0. When D6C == 1 the particle
dimensions are multiplied by the attach object's scale.

**The read path (verified, 0x10060906..0x10060955).** Two stages:

Stage 1 (always, 0x10060906):
```
0x10060906  mov  ecx, [ebp+0x68]       ; the attach object (actor or bone)
0x10060909  test ecx, ecx
0x1006090b  je   0x1006098c            ; unattached: skip, scale stays 1.0
0x1006090d  mov  eax, [ecx]            ; its vtable
0x1006090f  call [eax+0x1b4]           ; virtual: attach-object scale (returns float)
0x10060915  fstp [esp+0xa0]            ; store base scale
```

Stage 2 (only when D6C == 1, 0x10060928):
```
0x10060928  mov  ecx, [ebp+0x68]       ; attach object
0x1006092b  mov  edx, [ecx]            ; its vtable
0x1006092d  call [edx+0x258]           ; GetAttachedObject() -> eax
0x10060933  mov  ecx, [ebp+0x68]
0x10060936  mov  edx, [ecx+0x10]       ; a secondary multiplier (float at +0x10)
0x10060939  mov  [esp+0x2a8], edx
0x10060940  mov  edx, [eax]            ; attached object's vtable
0x10060942  mov  ecx, eax
0x10060944  call [edx+0x84]            ; virtual: attached-object scale (float)
0x1006094a  fmul [esp+0x2a8]           ; scale *= secondary multiplier
0x10060951  fstp [esp+0x30]            ; final scale factor
```

The attached object's vtable is **not pinned** by this trace, and it is **not**
the 0xb8 sub-object vtable. The 0xb8-byte sub-object does exist — created in
`FUN_100878c0` (decompile line 93247) by `operator_new(0xb8)` (line 93264) with
`*puVar1 = &PTR_LAB_101cea98` (line 93269; a second ctor, `FUN_100852b0`, stores
the same vtable at line 91240) — and its vtable's low slots (verified by raw
dump) are:

| vtable offset | function | body |
|---|---|---|
| +0x3C (slot 15) | 0x1016bd90 | `fld [ecx+0x60]; ret` — scale GETTER (float) |
| +0x54 (slot 21) | 0x1016be70 | `mov eax,[esp+4]; mov [ecx+0x60],eax; ret 4` — scale SETTER |
| +0x84 (slot 33) | 0x1016bed0 | `add ecx,0x30; jmp 0x101a6020` — **pointer thunk, not a float getter** |

0x101a6020 (the thunk target) returns a **pointer**:
```
101a6020  push esi
101a6021  mov esi, ecx              ; esi = this+0x30
101a6023  mov eax, [esi]            ; child object's vtable
101a6025  test eax, eax
101a6027  jne 0x101a6030
101a6029  mov eax, [0x1021a2b4]     ; static default
101a602e  pop esi
101a602f  ret
...
101a6050  lea eax, [esi + 0x14]     ; else: the child object
101a6053  pop esi
101a6054  ret
```
The stage-2 call 0x10060944 (`call [edx+0x84]`) is immediately followed by
`fmul [esp+0x2a8]` (0x1006094a) with **no float load in between**, so the call
must return its float in st(0) on the FPU stack. The 0x1016bed0 thunk touches
no FPU register and returns a pointer — therefore the attached object on the
stage-2 path is **not** the 0xb8 sub-object, and the vtable actually consulted
at +0x84 (the one of whatever `vtbl+0x258` = GetAttachedObject returns at
runtime) is not pinned statically. The **stage-2 slot is a float getter by
consumption** (proven), the **object and its vtable are not pinned** (PARTIAL).

The scale is stored as a **float** at sub-object offset +0x60 (getter/setter
above, raw-verified). The setter (0x1016be70) is called from the model-load /
actor-scale-change path, **not** from `Render`. A raw scan of the DLL
disassembly found **zero** `fstp`/`mov` stores to sub-object+0x60 in any render
or update function — the write happens in the model-load initialization
(FUN_10087930, decompile line 93281) or the actor-scale-change path, which the
decompile does not isolate as a single addressable `fstp` instruction.

**Mode-2 variant (0x10060a40..0x10060a91).** When the emitter is attached to an
actor and D6C==1, the mode-2 path reads the attached object's scale via
`vtbl+0x1b4` (0x10060a7d) instead of `vtbl+0x84` — a different slot on the same
sub-object vtable. It also reads a position vector via `vtbl+0x54` (0x10060a58)
and checks a boolean via `vtbl+0x268` (0x10060a49). If the attach object is a
track (0x10060a96), it reads the matrix via `track->vtbl+4` (0x10060aa3),
copies it via 0x100369e0, and reads the scale via 0x1005c4c0. If a bone
(0x10060ae0), it reads the bone matrix at `bone+0xa0` and the scale via
0x1005c4f0.

**The value.** On the fable side, the scale is
`entityScaleForServerSize(getRaceScale(race), serverSize / 6.0f)`
(`include/client/graphics/entity_renderer.h:100`,
`src/client/graphics/eq/race_codes.cpp:170`). For the two named races:

| race (models.json) | raceScale | server size | **entityScaleForServerSize** |
|---|---|---|---|
| Dark Elf female (char 67) | 1.0 | 6 | **1.0** |
| Troll female (char 109) | 1.4 | 6 | **1.4** |

The official client multiplies every particle dimension (quad half-extent,
offset, velocity integration step) by this value. A Troll's `OoW Gate02`
runecircle is 1.4× the Dark Elf's in every dimension.

**Census.** Raw spellsnew.edd (8-byte header): +0x6C = 1 for 2,013 of 2,118
records (95%), 0 for 105. Matches B1141 in-memory exactly.

## (b) +0x178 — spin alignment to the attach axis: dispatch, matrix, angle

**The read.** `Render` reads D178 (`mov eax, [edi+0x178]`) at three sites:
0x1006235f (update: `test eax,eax; je 0x100626d9`), 0x1006246f (dispatch),
0x100634d7 (draw: if nonzero, uses the pre-computed angle from esp+0xb0).

**The dispatch (verified, 0x1006246f..0x1006247f).** When D178 ≠ 0:
```
0x1006246f  mov  eax, [edi+0x178]
0x10062475  dec  eax                ; eax = value - 1
0x10062476  cmp  eax, 5
0x10062479  ja   0x1006255c         ; value > 6: default = column C
0x1006247f  jmp  [eax*4 + 0x100640b4]
```

The dispatch table at 0x100640b4 (verified by raw `jtab.py` dump):

| value | table index | target | axis |
|---|---|---|---|
| 1 | [0] | 0x10062486 | column A (esp+0x2f4), positive |
| 2 | [1] | 0x100624a0 | column B (esp+0x304), positive |
| 3 | [2] | 0x1006255c | column C (esp+0x314), positive |
| 4 | [3] | 0x100624ba | column A, negated (fmul -1.0 at 0x101cbd44) |
| 5 | [4] | 0x100624f2 | column B, negated |
| 6 | [5] | 0x10062527 | column C, negated |
| >6 | default | 0x1006255c | column C, positive |

Each column entry loads a 3-word vector from esp+0x2f4/0x304/0x314 (3 floats
each), optionally negates it (values 4/5/6 multiply by -1.0 at 0x101cbd44),
then falls through to the shared angle computation at 0x10062571.

**The matrix source (verified, 0x10062429..0x1006246a).** Before the dispatch,
the attach object's 16-word 4×4 matrix is fetched and copied to esp+0x2f4
by 0x100399f0 (a straight 16-dword copy, verified). Three sources, in priority
order:

1. **Track**: `ebp+0x70` (the track pointer); `track->vtbl+4` returns the
   matrix address (0x10062440).
2. **Bone**: `ebp+0x6c` (the bone pointer); the matrix is at `bone+0xa0`
   (0x1006244c: `add eax, 0xa0`).
3. **Actor**: `ebp+0x68` (the attach object); `actor->vtbl+0x234` returns the
   matrix address (0x1006245c).

The three columns of this matrix (the first 12 words, stored at esp+0x2f4,
esp+0x304, esp+0x314) are the attach object's local X, Y, Z axes.

**The angle computation (verified, 0x10062571..0x100626d2).** After the column
is selected:
1. Normalize the 3-vector (0x100044c0, 0x10062581).
2. Add the particle's local position (the particle pos minus the emitter
   center, computed at 0x10062385..0x10062412 via a full 4×4 matrix multiply
   in the attach frame).
3. Project through the **camera matrix** (eax = camera, using rows at
   [eax+0x34]..[eax+0x54] for the 3×3 rotation, [eax+0x10]/[eax+4]/[eax+0x18]
   for translation, [eax+0x18] for the perspective divide at 0x101c5948=1.0).
4. Compute the 2D screen-space angle between the projected axis and the
   particle's screen position (atan2 via 0x101ab6f0, 0x100626a7).
5. If the angle is too close to 0 or π (within epsilon 0x101c593c ≈ 1.2e-7 of
   the depth clamp 0x101c5940 = 20000), zero it out (degenerate case).
6. Convert to degrees (× 0x101cc268 = 0.15915 = 1/2π, × 0x101c9954 = 512.0),
   negate if negative, store as a 0..511 angle index at esp+0xb0.

This angle is then used in the mode-2 draw block (0x100634d7) to rotate the
quad in the attach plane, locking the quad's orientation to the attach
object's chosen axis rather than the camera.

**Census.** Raw spellsnew.edd: +0x178 = 0 for 2,116, 3 for 1
(`e2115 VetRewards_Y1b`), 6 for 1 (`e2114 VetRewards_Y1a`). Only these two
records use axis alignment (they are NOT the last two; 0-based 2116/2117 are
the last records). They pin opposite axes (C-positive and C-negative)
on the same texture (`blueglobesp501.dds` — re-derived by
`python3 refactor/B1151/edd_census.py`), consistent with two quads facing each
other. The B1141 OLD hypothesis ("1,2 = A,B; default=3; 4,5,6 = A-,B-,C-")
is **correct**: values 1-3 select positive A/B/C, values 4-6 select negated
A/B/C, value >6 defaults to positive C.

## (c) +0x130 / +0x134 — frame count / fps: the atlas grid and per-particle frame

**The frame block (verified, 0x10063b7a..0x10063d002).**

```
0x10063b7a  mov  eax, [edi+0x130]       ; frame count N
0x10063b8a  cmp  eax, 1
0x10063b8d  mov  [esp+0x60], 0          ; x offset (init 0)
0x10063b95  mov  [esp+0x78], 0x3f800000  ; w (init 1.0)
0x10063ba8  mov  [esp+0x68], 0x3f800000  ; y offset (init 1.0)
0x10063bb0  jne  0x10063bd5             ; N != 1: compute frame
0x10063bb2  fld  [0x101cbd40]           ; N == 1: whole texture
...
0x10063bd5  fld  [esp+0x14]             ; age (the particle's own age)
0x10063be0  fmul [edi+0x134]           ; age * fps (D134)
0x10063be8  fstp [esp]
0x10063beb  call 0x10003ba0            ; ftol -> integer frame
0x10063bf7  mov  ecx, [edi+0x130]      ; N
0x10063bfd  cdq
0x10063bfe  idiv ecx                   ; frame % N (remainder in edx)
0x10063c06  mov  eax, edx              ; frame index
```

**The frame is `ftol(age × D134) mod D130`**, where `age` is the **particle's
own age** (since its birth), not the emitter clock. The strip loops over the
particle's lifetime: a 16-frame strip at 16 fps advances one frame per
particle second.

**The atlas rect (verified, 0x10063c03..0x10063cfc).** Row-major, inferred from N:

| N | grid | cell size | constants used |
|---|---|---|---|
| 1 | whole texture | (0, 0, 1, 1) | — |
| 2-4 | 2×2 | 0.5 × 0.5 | 0x101c594c = 0.5 |
| 5-8 | 4 cols × 2 rows | 0.25 × 0.5 | 0x101c59d0 = 0.25, 0x101c594c = 0.5 |
| 9-16 | 4×4 | 0.25 × 0.25 | 0x101c59d0 = 0.25 |
| >16 | whole texture | (0, 0, 1, 1) | — |

The cell x-offset = `frame_in_row × cell_w`, y-offset = `row × cell_h`.
The constants 0.5 and 0.25 are at 0x101c594c and 0x101c59d0 respectively
(verified by raw float read).

**Per-frame spin.** The CONV function (0x1005eef0) rewrites +0x138 from the
record (it is in the rewrite list), so the in-memory per-frame spin values are
the record's own. The render loop reads them to advance the particle's angle
per frame. The per-frame spin is not isolated in the disassembly windows
extracted for this trace (it is in the particle-update loop, not the draw
block).

**Census.** Raw spellsnew.edd (full distributions, each sums to 2118; re-derived
by `python3 refactor/B1151/edd_census.py`):

+0x130 — exactly three values present: 16 for 1,064, 1 for 855, 4 for 199.

+0x134 (fps, full): 0:59, 1:217, 2:2, 4:22, 6:4, 7:1, 8:67, 9:7, 10:6, 11:106,
12:96, 13:2, 14:2, 14.3125:1, 16:1320, 16.8:1, 18:12, 20:2, 22:124, 24:5, 26:2,
32:13, 33:29, 44:7, 64:3, 66:1, 116:1, 128:1, 256:5.

The dominant configuration is 16 frames / 16 fps (1,064 records): a 4×4 strip
at one frame per 1/16 s.

## (d) +0x70 — three sprite modes: billboard, streak, world-oriented quad

`Render` reads D70 (`mov eax, [edi+0x70]`) at 0x100630a1 and 0x1006349a.

- **0: billboard.** Screen-facing quad: half-width = `w × s × 0.5`,
  half-height = `h × s × 0.5` (s = the attach scale from item (a)). The two
  in-plane axes are the screen X and Y. The four vertices are the particle
  world pos ± the two axes. Classic particle quad.

- **1: streak.** The quad is stretched along the emitter's velocity axis: the
  second point is `pos + emitter_axis × (s × h)`, and the quad is built between
  the two projected points. Streak length = `h` (the emitter's half-height).

- **2: world-oriented quad (verified, 0x100634b0..0x10063680).** NOT
  screen-facing. The geometry:
  1. If D178 ≠ 0, the pre-computed alignment angle (esp+0xb0) is used
     (0x100634e1).
  2. The particle's position is fetched via two virtual calls on the
     particle/scene object (0x102326f8): `vtbl+0x0c` (0x100634ff) and
     `vtbl+8` (0x10063516).
  3. A 3D vector is built from the emitter's runtime state (ebx = the
     emitter's matrix or basis) via dot products (0x10063535..0x100635a0).
  4. **0x1005dc80** (0x100635a7): a **matrix-vector multiply** (NOT sin/cos —
     verified: it computes the dot product of each of the matrix's three rows
     with the input vector, producing a rotated 3D vector). This rotates the
     emitter's local axes by the spin/alignment angle.
  5. Half-extent 1 = `w × s × 0.5` (0x100635b3..0x100635ba, using
     0x101c594c = 0.5).
  6. Half-extent 2 = `h × s × 0.5` (0x100635f2..0x10063600, using
     0x101c594c = 0.5).
  7. The two rotated axes are scaled by their half-extents and added to the
     particle's world position to produce the four quad vertices.

  The quad is sized in **world units** and projected through the camera, so it
  shrinks with distance — it looks "painted on the ground" rather than a
  billboard. The two mode-2 records in spellsnew.edd are `e2008 OoW Gate02`
  (`runecircle00.dds`) and `e2075 Disease02` (`darknesssp502.dds`) — both
  ground-painted circles, consistent with the world-oriented geometry.

**Census.** Raw spellsnew.edd: +0x70 = 0 for 1,911 (billboard), 1 for 205
(streak), 2 for 2 (world-oriented). Matches B1141 in-memory exactly.

## (e) Shape 7 — the two random helpers and the line/cone geometry

The shape switch (0x10061753) dispatches on D_A8 via the jump table at
0x1006408c (10 entries, verified):

| shape | target |
|---|---|
| 0 | 0x10061769 |
| 1 | 0x10061779 |
| 2 | 0x10061826 |
| 3 | 0x10061905 |
| 4 | 0x100619d1 |
| 5 | 0x10061acb |
| 6 | 0x10061be3 |
| **7** | **0x10061d9c** |
| 8 | 0x10061e7d |
| 9 | 0x10061fa5 |

**The two random helpers (verified, full function bodies).**

`0x1005d400` — 1000-entry sequential float table:
```
0x1005d400  mov  ecx, [0x10235f54]      ; counter
0x1005d40b  mov  esi, 0x3e8             ; 1000
0x1005d410  div  esi                    ; eax = ctr/1000, edx = ctr%1000
0x1005d412  inc  ecx
0x1005d413  mov  [0x10235f54], ecx     ; ctr++
0x1005d41a  fld  [edx*4 + 0x10234fa0]  ; load pre-computed float in [0,1)
0x1005d421  ret
```

`0x1005d3e0` — 512-entry sequential float table:
```
0x1005d3e0  mov  eax, [0x10235f4c]      ; counter
0x1005d3e7  and  ecx, 0x1ff            ; ctr & 511
0x1005d3ed  fld  [ecx*4 + 0x102347a0]  ; load pre-computed float in [0,1)
0x1005d3f4  inc  eax
0x1005d3f5  mov  [0x10235f4c], eax     ; ctr++
0x1005d3fa  ret
```

Both tables are **arrays of pre-computed floats in [0,1)**, filled with
i/N and shuffled once at init by FUN_1005d2a0 (verified,
0x1005d2a0..0x1005d3cf, called from 0x1005de21). The init:
1. Fills 0x102347a0 with 512 floats (i/512 for i=0..511), then shuffles them
   with swap index = `rand() × 2^-15 × 512.0` (0x101c7410 = 1/32768,
   0x101c9954 = 512.0).
2. Fills 0x10234fa0 with 1000 floats (i/1000 for i=0..999), then shuffles
   with swap index = `rand() × 2^-15 × 1000.0` (0x101c6eac = 1000.0).
3. Also builds and shuffles two intermediate tables (0x10232860, 2048
   entries, and 0x1023285c, 1999 entries) used by other code.

The static bytes at 0x102347a0 and 0x10234fa0 are all zero because the
shuffle happens at runtime. Within each 1000- or 512-draw cycle, each value
appears exactly once (a full permutation of the float table), so the
distribution is uniform with no repeat until the counter wraps. The
community description "permutation of 0..999" is imprecise: the table holds
**floats in [0,1)** (the shuffled i/N values), not integer indices.

**The shape-7 geometry (verified, 0x10061d9c..0x10061e00).**

```
0x10061d9c  fld  [0x101cbd40]           ; 1.0
0x10061da2  fld  [edi+0xb0]             ; D_B0 (a size parameter)
0x10061da8  fucompp
0x10061daa  fnstsw ax
0x10061dac  test ah, 0x44
0x10061daf  jp   0x10061de1             ; if D_B0 < 1.0: alternate path
; --- main path (D_B0 >= 1.0): ---
0x10061db1  call 0x1005d400            ; u1 = float from 1000-table
0x10061db6  fstp [esp+0x10]
0x10061dba  call 0x1005d3e0            ; u2 = float from 512-table
0x10061dbf  fstp [esi+0x30]            ; base offset = u2
0x10061dc2  fld  [esp+0x10]
0x10061dc6  fmul [edi+0xac]            ; radius = u1 × D_AC
0x10061dcc  fstp [esi+0x34]
0x10061dcf  fld  [esp+0x10]
0x10061dd3  fmul [edi+0xb4]            ; height = u1 × D_B4
0x10061dd9  fstp [esi+0x38]
0x10061ddc  jmp  0x10062051
; --- alternate path (D_B0 < 1.0): ---
0x10061de1  call 0x1005d3e0            ; u2 from 512-table
0x10061de6  fstp [esp+0x24]
0x10061df7  call [edx+0x0c]            ; virtual: get some parameter
0x10061dfa  fmul [edi+0xac]            ; × D_AC
```

Shape 7 is a **line/cone**: a single random u1 (1000-table) drives BOTH the
radius (`r = u1 × D_AC`) and the height (`h = u1 × D_B4`) — that's what makes
it a line (u1 parameterizes both axes together) rather than a disc (which
would need two independent uniforms). A second, independent u2 (512-table) is
the base offset along the cone's axis. The particle spawns at
`origin + axis × (u2 × h) + radial(u1 × r)`.

**Census.** Raw spellsnew.edd: +0xA8 = 7 for 9 records. Full distribution (sums
to 2118; re-derived by `python3 refactor/B1151/edd_census.py`):
0:297, 1:202, 2:13, 3:125, 4:275, 5:808, 6:5, 7:9, 8:18, 9:366. Matches B1141
in-memory exactly.

## Addresses cross-referenced (all verified against the disassembly)

| VA | meaning |
|---|---|
| 0x100605b0 | CParticleSystem::Render (decompile line 68017) |
| 0x100608b5 / 0x1006091c / 0x100609e1 / 0x10060ae0 | the four D6C reads |
| 0x1006090f | `call [vtbl+0x1b4]` on attach object — base scale |
| 0x1006092d | `call [vtbl+0x258]` on attach object — GetAttachedObject() |
| 0x10060944 | `call [vtbl+0x84]` on attached object — result consumed as float (fmul, 0x1006094a); vtable not pinned (see (a)) |
| 0x10060a49 | `call [vtbl+0x268]` — mode-2 attach check |
| 0x10060a58 | `call [vtbl+0x54]` — mode-2 position vector |
| 0x10060a7d | `call [vtbl+0x1b4]` on attached object — mode-2 scale |
| 0x1016bd90 | scale getter: `fld [ecx+0x60]; ret` (sub-object vtbl +0x3C) |
| 0x1016be70 | scale setter: `mov [ecx+0x60],eax; ret 4` (sub-object vtbl +0x54) |
| 0x1016bed0 | vtbl 0x101cea98 +0x84: `add ecx,0x30; jmp 0x101a6020` — pointer thunk (NOT the stage-2 scale getter) |
| 0x101cea98 | the 0xb8 sub-object's vtable (refs at file 0x852b2/0x87910) — NOT the attached object on the stage-2 path |
| 0x1006235f / 0x1006246f / 0x100634d7 | the D178 reads |
| 0x10062486/0x24a0/0x255c/0x24ba/0x24f2/0x2527 | the 6 axis-alignment columns |
| 0x100640b4 | the +0x178 dispatch table (6 entries) |
| 0x10062440/0x244c/0x245c | the 3 matrix sources (track/bone/actor) |
| 0x100399f0 | the 16-dword matrix copy |
| 0x100369e0 | the track matrix copy (mode-2 path) |
| 0x1005c4c0 / 0x1005c4f0 | track/bone scale getters (mode-2 path) |
| 0x100044c0 | the 3-vector normalize |
| 0x101ab6f0 | atan2 wrapper |
| 0x101cc268 | 0.15915 (1/2π) |
| 0x101c9954 | 512.0 |
| 0x101c593c | 1.19e-7 (epsilon) |
| 0x101c5940 | 20000.0 (depth clamp) |
| 0x101c5944 | -1.0 |
| 0x101c5948 | 1.0 |
| 0x101c594c | 0.5 (half-extent constant) |
| 0x101c59d0 | 0.25 (4-column atlas cell width) |
| 0x101cbd40 | 1.0 |
| 0x101cbd44 | -1.0 |
| 0x10063b7a..0x10063d002 | the frame block (D130/D134/atlas rect) |
| 0x10003ba0 | ftol |
| 0x100630a1 / 0x1006349a / 0x100634a0 | the D70 reads (mode 0/1/2) |
| 0x100634b0..0x10063680 | the mode-2 oriented-quad geometry |
| 0x1005dc80 | the matrix-vector multiply (mode-2 rotation) |
| 0x10061753 | the shape switch (D_A8) |
| 0x1006408c | the shape jump table (10 entries) |
| 0x10061d9c | shape-7 case entry |
| 0x1005d400 | the 1000-entry float random helper (table 0x10234fa0, ctr 0x10235f54) |
| 0x1005d3e0 | the 512-entry float random helper (table 0x102347a0, ctr 0x10235f4c) |
| 0x1005d2a0 | the Fisher-Yates init (rand()-based, called from 0x1005de21) |
| 0x101c7410 | 1/32768 (shuffle index scale) |
| 0x101c6eac | 1000.0 (1000-table shuffle scale) |
| 0x1005eef0 | the CONV function (record → CEmitterDef, decompile line 67132) |
| 0x100878c0 | the 0xb8 sub-object alloc (operator_new(0xb8) at line 93264, vtable store at 93269; decompile header line 93247) |
| 0x10087930 | FUN_10087930, model-load path (decompile header line 93281; the 0xb8 alloc is in the PRECEDING FUN_100878c0) |
| 0x102347a0 / 0x10234fa0 | the two random float tables (runtime-initialized) |
| 0x10235f4c / 0x10235f54 | the two random counters |

Decompile-line cites re-derived (not inferred):
`grep -n "FUNC 100605b0" EQGraphicsDX9.dll.c` → `68017:// FUNC 100605b0
FUN_100605b0`; `grep -n "FUNC 10087930" EQGraphicsDX9.dll.c` →
`93281:// FUNC 10087930 FUN_10087930`; `grep -n "FUNC 100878c0"
EQGraphicsDX9.dll.c` → `93247:// FUNC 100878c0 FUN_100878c0` — and
`operator_new(0xb8)` (line 93264) plus the vtable store `*puVar1 =
&PTR_LAB_101cea98` (line 93269) both sit inside FUN_100878c0 (lines 93247..93280),
i.e. the PRECEDING function, not FUN_10087930.

## Regenerating every census quoted in this document

One command (run from the repo root):

```
scp thinkpad:/mnt/projects/EverQuestP1999/spellsnew.edd /tmp/spellsnew.edd \
  && python3 refactor/B1151/edd_census.py /tmp/spellsnew.edd
```

It prints the 8-byte header, the full distribution of +0x6C / +0x70 / +0xA8 /
+0x130 / +0x134 / +0x178 over all 2,118 records (each summing to 2,118), the
names/textures of records 2114..2117, every record with D178 ≠ 0, and every
record with D70 == 2. `spellsnew.edd` lives on the ThinkPad oracle client
copy (path recorded above; `find / -name spellsnew.edd` on `ssh thinkpad`
finds the same bytes at three locations there).

## fable mirror

The fable-side attach-scale rule is mirrored in
`tools/model_viewer/model_viewer_spell_test.cpp` (the `--spell_test` harness)
and `src/client/graphics/eq/race_codes.cpp` /
`include/client/graphics/entity_renderer.h` (`entityScaleForServerSize`,
`getRaceScale`). Per-race values from `config/race_models.json`:
Dark Elf female `raceScale = 1.0`, Troll female `raceScale = 1.4`.
`entityScaleForServerSize(1.0, 6/6) = 1.0`;
`entityScaleForServerSize(1.4, 6/6) = 1.4`. The official client's +0x6C
multiplier is exactly this value.

## PARTIAL rows (what this trace did NOT fully resolve)

- **The attached object's vtable on the stage-2 (+0x6C == 1) scale read.**
  Stage 2's `call [vtbl+0x84]` (0x10060944) returns a float by consumption,
  but the object it is called on is whatever the attach object's `vtbl+0x258`
  (GetAttachedObject) returns at runtime; its vtable is not pinned statically.
  Proven negative: it is NOT the 0xb8 sub-object vtable 0x101cea98 — that
  vtable's +0x84 slot (0x1016bed0) is `add ecx,0x30; jmp 0x101a6020`, a
  pointer-returning thunk (0x101a6020 returns a child object / static default
  and touches no FPU register), incompatible with the `fmul [esp+0x2a8]` that
  immediately follows the call. Missing piece: dump the runtime vtable of the
  object GetAttachedObject returns (e.g. via the oracle client) and read its
  +0x84 slot.
- **The exact setter call-site for sub-object+0x60.** FUN_100878c0
  (decompile line 93247) allocates the 0xb8 sub-object; FUN_10087930
  (decompile line 93281) is the model-load path where the per-race value is
  suspected to be written via the setter (0x1016be70) when the actor's scale
  changes. The float store does not appear as an isolated `fstp` in the
  `.dis` — the write-site is not pinned. The stage-1 read path and the value
  (fable mirror) are proven; the stage-2 read is PROVEN by consumption, the
  stage-2 object is PARTIAL (above).
- **The per-frame spin (+0x138) array layout.** The CONV function rewrites it
  and the render loop reads it, but the record's per-frame spin words (count,
  offset) are not isolated in the disassembly windows extracted here.
- **Which matrix column is X vs Y vs Z.** The dispatch, negation, normalization
  and the three matrix sources are all traced. The column→axis-letter mapping
  is a convention question: the matrix is the attach object's local frame, and
  the columns are its first three 3-word vectors.
