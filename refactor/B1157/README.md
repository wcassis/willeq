# B1157 (#476): the caster plays the spell's CastingAnim, client and harness, one code path

Branch `task/kb-b1157-cast-anim`, cut at origin/fable e83a097d (the B1152 merge). Evidence:
`/home/user/kb-evidence/kb-b1157-cast-anim/` (all frames + `video.mkv` per run; the orchestrator
copies it to `/mnt/projects/fable/review/B1157/` at hand-back). The specification is
`glab issue view 476`.

Will 2026-10-26: "Casting particles often emit from entities based on body-part locations, so
having correct model rendering/animation is critical. Casting animations are simple; why have you
not implemented them first?" THE ISSUE WAS THE SPECIFICATION: the client did not play the
CastingAnim either (scope correction 13:05Z, issue note) -- step 1 is the client, step 2 the
harness through the client's own function.

## 0. Result

- The client now does what eqgame does on OP_BeginCast: every casting entity (the player
  included) plays the track its spell's field-120 CastingAnim names through eqgame's id -> code
  table (42/43/44 = T04/T05/T06), at speed 1.0, one shot, holding (or looping, per the model's own
  looped flag) through the channel; OP_Action / interrupt ends it to the idle. One shared helper
  (`include/client/graphics/spell_cast_animation.h`) is the client's AND the harness's one code
  path -- no second table, no harness-side mapping, the o01 loop and the guessed t04/t05/t06 are
  gone.
- Fable's pre-B1157 rule: `entity_castbars.cpp` played a hard-coded "t04" (playThrough, no loop,
  stretched to 20 % of the cast time) for every NPC caster and nothing for the player; the harness
  looped o01 during the channel and played a guessed t04/t05/t06 once at completion. Every
  B1149-B1152 gate number was measured on that idle caster, and the 18 unattributed B1151
  SOURCE rows (its L502 tally) were its signature.
- Re-gates, animated caster vs the B1152/B1151 idle baselines, before/after per row with an owner
  per miss (sec. 6): b1118 tour-7 + 40 records = 40/47 PASS (4 tour rows + 36 records); b1130
  40 records = 40/40 owned (27 model/animation, 12 cast-anim-residual, 1 unattributed; zero
  PASS-band rows -- the b1130 bands are the ORACLE's, and the harness has never been in band
  there).

## 1. What exists (the grep lines)

The client's CastingAnim path before B1157 -- field, copy-in, the code table, the ONE place it
was used (the track LENGTH, i.e. timing only), the bridge forward and the bone anchors the
emitters follow:

```
$ grep -n 'cast_anim' include/client/spell/spell_data.h
116:    uint16_t cast_anim = 0;       // Casting animation ID

$ grep -n 'd.castAnim = spell->cast_anim' src/client/spell/spell_packet_handlers.cpp
90:            if (spell) d.castAnim = spell->cast_anim;  // B1148: field 120
134:            if (spell) d.castAnim = spell->cast_anim;  // B1148: field 120

$ grep -n '0x8ffbc8' include/client/spell/spell_fx_timing.h
27: * The id names the track through eqgame's code table (0x8ffbc8 + id * 20,
28- * filled by 0x44e7ec..: 1 = C01 .. 42 = T04, 43 = T05, 44 = T06;

$ grep -n 'CastAnimationTrackMs(it' src/client/graphics/renderer/renderer_entities.cpp
308:                return CastAnimationTrackMs(it->second.animatedNode, code);

$ grep -n 'createCastGlow' src/client/bridge/spell_cast_fx.cpp
25:    fx->createCastGlow(d.casterId, d.spellId, d.castTimeMs, d.fxGateLevel, d.castAnim, d.targetId);

$ sed -n '10,16p' include/client/graphics/spell_fx_bone_anchor.h
//
// B1144 (#464): which pseudo-id a spellsnew.eff slot follows is chosen from
// the slot's attach point by EQ::SpellVisualFX::attachPointEntityId():
// LHAND/RHAND -> the hand ids, HEAD/LEYE/REYE/MOUTH -> the head id, every
// other point -> the entity (resolved by the caller's plain-entity half).
```

The traced rules, quoted from where they were traced (do not re-derive):

- `refactor/B1145/README.md` 15: "The cast-animation term (eqgame 0x47ddd4) was MEASURED, not
  traced: 4.35 s (median of 20 unsaturated B1130 other-target captures, IQR 4.12..4.81 ...)" --
  that is the TERM (the Source duration extension), used for timing.
- `refactor/B1148/README.md` 25-30: "Per-caster Source term (wired, oracle-confirmed).
  `0x47ddd4(spell+0x186)` = the caster actor's track for the CastingAnim id; eqgame's id -> code
  table (0x8ffbc8 + id x 20) names 42/43/44 = T04/T05/T06. fable reads the length off the
  caster's skeleton (`CastAnimationTrackMs`), falls back to B1145's 4.35 s when it cannot."
  and 58: "| eqgame 0x47ddd4 | actor table (actor +0x1a8 -> +300 -> +0x2c)[CastingAnim] -> track
  -> length (ftol) |".
- THE PLAY CALL, traced for this card from the decompile (PROVEN against
  `review/B1141/ghidra/out/eqgame.exe.c` 122125-122140): the cast-start handler (FUN_0049b39d,
  122042: u16 spell id, checked castable via FUN_0044d0d0 with a non-zero cast time from
  FUN_0040cbd6) branches on FUN_0044ced9() (122125). The true side writes the cast-time words
  (model +0xecc/+0xed0/+0xed4), takes the animation manager (FUN_0047c08e, 122131) and calls
  its set function (vtbl+8) with `(*(spell+0x186), 1, 0, 0x3f800000, 1)` (122132) -- the
  CastingAnim word (spell+0x186, B1148) at speed 1.0f. The false side measures that same word's
  length: FUN_0047ddd4(spell+0x186) (122136) -- the timing term above. The concrete setter
  FUN_00450c2f (71181-71618) is the actor's animation-set path for that call: state-gated on
  the actor's state words (+0x251/+0x252), it puts the actor into the cast state (+0x251 = 100,
  'c'), remaps a couple of ids (0x32<->0x3e) and drives the model off +0x1a8. The
  already-playing no-op path is PROVEN too: when the model's current id (model+0x134, read
  through the actor's +0x1a8) already equals the requested one, FUN_00450c2f returns without
  re-requesting (71467), so the model is not re-driven per frame; the play itself goes through
  the model's vtbl+0x178, one-shot (71532, 71543). WHAT THE MODEL DOES once the track is set is MEASURED, not traced: the oracle's caster keeps the
  raised-hands end frame through the whole channel (issue #476;
  `refactor/B1148/README.md` sec. 3 leg C1 frames), and fable's `SkeletalAnimator::playAnimation`
  carries the model's own looped flag: one-shot tracks hold their last frame, looped tracks
  loop.
- OP_Action / interrupt: the cast state ends and the state machine plays the new state's
  animation -- the idle of a standing caster (p01 the local player, o01 an idle NPC).

## 2. What changed (files)

Shared helper (the one code path; PROVEN header comment as quoted above):

- `include/client/graphics/spell_cast_animation.h` (new): `castAnimTrackFor()` (the id -> code
  -> model track, nullptr = the client's no-track path), `playCastAnim()` (speed 1.0, one shot,
  playThrough, hold/loop per the model), `endCastAnim()` (stop + the new state's idle).

Client:

- `src/client/graphics/entity_castbars.cpp` -- `startEntityCast`/`completeEntityCast`/
  `cancelEntityCast` go through the helper for every casting entity, the player included
  (replaces the "t04" literal; the 20 % stretch is gone -- the track plays at its own speed and
  holds).
- `src/client/bridge/irrlicht_bridge.cpp` -- the BeginCast event carries the spell's `castAnim`
  word to the renderer (it already carried it for the timing term).
- `src/client/graphics/entity_updates.cpp`, `entity_first_person_draw.cpp` -- a casting model is
  not pulled back to walk/idle mid-channel (the state machine's already-playing path).
- `src/client/graphics/eq/skeletal_animator.{h,cpp}` + `scripts/refactor/check_zero_callers.py`
  -- `setTargetDuration` (the stretch mechanism) had zero callers after the replacement and is
  deleted; its exemption line dropped.

Harness:

- `tools/model_viewer/model_viewer_fx_harness.cpp` -- the scripted BeginCast/Action drive the
  caster through `playCastAnim`/`endCastAnim` (the client's own functions), and per rendered
  frame writes `emitter_origin.tsv` (every open emitter's world origin + screen position through
  the same pinhole as `scene.tsv`'s boxes -- the Source stage's attach point: the hand bone for
  a hand-attached slot, the anchor otherwise).
- `tools/model_viewer/model_viewer_spell_test.cpp` -- the interactive trigger drops the o01
  loop + guessed t04/t05/t06 (the `--target` model stays idle, as the oracle's does).
- `tools/model_viewer/README.md` -- updated.

Tests:

- `tests/test_cast_animation.cpp` (new, `tests/cmake/packets_skills.cmake`): the device-free
  half (always on) pins the code table (42/43/44 = t04/t05/t06, out-of-range nullptr) and the
  no-track path; the EDT_NULL entity half (skips without the shipped db or a client tree, the
  same gate as `test_corpse_fade`) pins BeginCast -> track + start time for 93/38/54 (all
  CastingAnim 44 = t06), that OP_Action and interrupt end it to the idle, that an Action
  without a BeginCast switches nothing, and that a model without the track keeps its current
  animation.
- `tests/test_fx_harness.cpp` (+106): `FxHarness.TheCasterPlaysTheSpellCastAnimThroughTheClientCode`
  -- for 93/38/54 the harness's chosen track name + start time equal the client path's for the
  same spell (both sides call the helper; the test asserts the equality, not the literal).

## 3. Tests (results, rbuild on the ThinkPad, Release GLES2 tree)

- `rbuild <wt> test -R 'CastAnimation|FxHarness|Spell|Oracle'`: 715/715 passed, 0 failed
  (`/home/user/kb-evidence/kb-b1157-cast-anim/rbuild_test.log`). The four CastAnimationEntity
  EDT_NULL cases skip in that run for lack of a client tree in the repo (`EQ_CLIENT_DATA_PATH` =
  `data/eq_client`, not shipped). Pointed at the real tree
  (`EQ_CLIENT_PATH=/mnt/projects/EverQuestP1999 ctest -R CastAnimationEntity`): 5/6 passed, the
  6th (`AModelWithoutTheTrackKeepsItsCurrentAnimation`) self-skips because the Troll model has
  t06; its no-track path is the device-free `castAnimTrackFor(nullptr)` case, which ran.
- `FxHarness.TheCasterPlaysTheSpellCastAnimThroughTheClientCode` passed (130 s).
- `rbuild <wt> hygiene`: "100% tests passed, 0 tests failed out of 48" (`rbuild_hygiene.log`,
  HYG_EXIT=0).
- Cite checker (B1156, origin/fable `scripts/refactor/cite_check.py`, both .dis files):
  `refactor/B1157/README.md` -> "checked 5 cites, 0 problems, 0 address warnings";
  `tools/model_viewer/README.md` -> "checked 0 cites, 0 problems, 0 address warnings". The
  checker's patterns are file:line / sed ranges / 0x addresses; the eqgame.exe.c line cites in
  sec. 1 (122042, 122125, 122131-122132, 122136, 71181-71618, 71467, 71532, 71543) were
  verified against the file by hand in this run (each line printed and checked).

## 4. The hand-anchor strip (spell 93, the card's acceptance B)

Spell 93's Source stage attaches to the caster's LHAND (B1144's attach-point rule,
`include/client/graphics/spell_fx_bone_anchor.h` 13-16). `emitter_origin.tsv` (the harness's new
per-frame dump) says the emitter origin IS the hand bone, and it moves through the raised pose:
`refactor/B1157/hand_strip_93.tsv` (extracted from a b1118 tour-93 run, spell 93, CastingAnim
44 = t06):

```
frame  t        fxid   kind    wx       wy      wz      sx      sy
45     1.500000 49153  LHAND   -0.676   0.821   0.220   664.3   466.1    <- BC+0.000 (t06 starts here)
...
89     2.966667 49153  LHAND   -2.028   2.270  -0.117   712.7   412.8    <- raised: wy 0.82 -> 2.27, screen (664,466) -> (713,413)
```

The Source emitter (fxid 49153 = LHAND-attached) tracks the hand bone: wy 0.821 -> 2.270 world,
screen (664,466) -> (713,413), through the t06 raise. (The RHAND-attached emitter 32769 and the
entity row 1 ride along in the strip; the full 136-row file is committed.)

## 5. Re-gate method (both rigs)

`refactor/B1157/scripts/run_sweep_b1118.sh` and `run_sweep_b1130.sh` copy B1152's OWN plans
(`refactor/B1152/plans/sweep/list.tsv`: record, spell, target, the record's representative
cast_ms; t_begin 1.5 s, distance 0, caster level 60) and B1155's b1130 rig, so the ONLY variable
between the baseline and this sweep is the caster's animation. Render on the ThinkPad (rbuild
tree + client files + the eqe1 pack, `--headless --camera b1118` / `--camera b1130`, fps 30,
seed 1), ALL frames + `video.mkv` kept in the evidence dir (B1158's visual ledger consumes them),
measured HERE with B1139's `harness_measure.py` (`FX_KEEP=1 FX_SPELLS=auto`) + B1130's
`band_table.py` / `anchor.py`, exactly as `review/B1149/scripts/run_scene.sh` does. Both sweeps
exited 0 (b1118: 47 runs, 0 failed; b1130: 40 records, 0 failed).

## 6. The tables (the point: before/after per row, owner per miss)

### 6.1 b1118 (tour-7 + 40 records) -- `b1118_table.tsv`

Gate axes (harness-against-harness; the b1118 rig is a proxy, not the lane): landing coverage
±3 % (B1131's band), landing end ±0.10 s (B1149's method). The Source axis is where the cast
animation is ALLOWED to move the gate -- the raised hand changes what the Source window
contains. BEFORE: tour-7 = B1151's committed tour rows (B1152 did NOT re-gate the tour; its own
claim: 40/40 records within 0.028 src_cov of B1151, i.e. B1152 == B1151 for the idle caster);
the 40 records = B1152's committed runs (`refactor/B1152/runs/`).

Verdicts: 40/47 PASS; misses: tour 93/38 (over), tour 458 (short), r1/r2/r7/r8 (over).

```tsv
row	spell	baseline	verdict	land_cov_b	land_cov_n	d_land_cov	end_b	end_n	d_end	src_cov_b	src_cov_n	d_src_cov	src_on25_b	src_on25_n	src_cen_b	src_cen_n	owner
tour 93	93	b1151_tour_93	over	2.113	1.993	-0.120	+6.467	+6.633	+0.166		0.385			+0.433		0.86	B1152 sim delta (tour not re-gated by B1152; B1152->B1157 for this spell = record r0: +0.000 land / +0.000 end)
tour 38	38	b1151_tour_38	over	2.113	1.993	-0.120	+6.467	+6.633	+0.166		0.385			+0.433		0.86	B1152 sim delta (tour not re-gated by B1152; 38 renders identically to 93 in both gates: 2.113/+6.467 in B1151, 1.993/+6.633 here; the B1152->B1157 half is record r0: +0.000 land / +0.000 end)
tour 54	54	b1151_tour_54	PASS	1.204	1.204	+0.000	+6.333	+6.333	+0.000		0.415			+0.500		0.63	
tour 656	656	b1151_tour_656	PASS	1.204	1.204	+0.000	+6.333	+6.333	+0.000		0.415			+0.500		0.63	
tour 41	41	b1151_tour_41	PASS	2.478	2.478	+0.000	+5.667	+5.667	+0.000		1.129			+0.567		0.91	
tour 253	253	b1151_tour_253	PASS	5.306	5.306	+0.000	+5.317				3.804			+0.500		0.99	
tour 458	458	b1151_tour_458	short	0.499	0.5	+0.001	+5.183	+5.050	-0.133		0.12			+0.300		0.53	unattributed (no B1152 baseline: 458 is not one of the 40 records; the row spans B1151->B1157 incl. B1152's sim change)
r4	50	b1152_r4_50	PASS	1.427	1.431	+0.004				1.301			+0.900		0.77		
r2	202	b1152_r2_202	over	0.663	0.694	+0.031	+4.700	+4.633	-0.067	0.225			+0.667		0.67		model/animation (B1157 cast pose: the raised hand -- the only delta vs the B1152 baseline; see the README for the mechanism)
r7	40	b1152_r7_40	over	0.973	0.902	-0.071	+5.433	+5.633	+0.200	0.364	0.524	+0.160	+0.433	+0.433	0.89	0.85	model/animation (B1157 cast pose: the raised hand -- the only delta vs the B1152 baseline; see the README for the mechanism)
r1	200	b1152_r1_200	over	1.238	1.308	+0.070	+6.167	+6.233	+0.066	0.247	0.304	+0.057	+0.533	+0.500	0.62	0.64	model/animation (B1157 cast pose: the raised hand -- the only delta vs the B1152 baseline; see the README for the mechanism)
r9	375	b1152_r9_375	PASS	1.32	1.321	+0.001	+7.200	+7.200	+0.000	0.833	0.866	+0.033	+0.433	+0.433	0.84	0.83	
r10	590	b1152_r10_590	PASS	1.571	1.57	-0.001	+6.067	+6.100	+0.033	1.039			+0.600		0.77		
r12	208	b1152_r12_208	PASS	3.409	3.409	+0.000	+7.000	+7.000	+0.000	1.719	1.744	+0.025	+0.600	+0.600	0.79	0.78	
r20	341	b1152_r20_341	PASS	2.113	2.113	+0.000	+5.567	+5.567	+0.000	1.048	1.079	+0.031	+0.567	+0.567	0.82	0.81	
r40	602	b1152_r40_602	PASS	0.639	0.639	+0.000	+4.133	+4.133	+0.000	0.965	0.986	+0.021	+0.433	+0.433	0.83	0.83	
r25	14	b1152_r25_14	PASS	2.088	2.088	+0.000	+6.533	+6.533	+0.000	0.394			+0.600		0.84		
r18	378	b1152_r18_378	PASS	0.944	0.945	+0.001	+5.200	+5.200	+0.000	0.39	0.433	+0.043	+0.500	+0.500	1.06	1.03	
r16	242	b1152_r16_242	PASS	0.845	0.846	+0.001				0.702	0.76	+0.058	+0.700	+0.700	0.35	0.37	
r0	93	b1152_r0_93	PASS	1.993	1.993	+0.000	+6.633	+6.633	+0.000	0.337	0.385	+0.048	+0.433	+0.433	0.90	0.86	
r14	373	b1152_r14_373	PASS	1.881	1.881	+0.000	+8.400	+8.433	+0.033	0.072	0.11	+0.038	+0.467	+0.100	0.67	0.73	
r5	80	b1152_r5_80	PASS	0.249	0.248	-0.001	+4.267			0.135	0.214	+0.079	+0.467	+0.467	0.72	0.78	
r17	237	b1152_r17_237	PASS	1.24	1.237	-0.003	+4.800	+4.833	+0.033	0.546	0.603	+0.057	+0.567	+0.533	0.72	0.74	
r6	722	b1152_r6_722	PASS	0.401	0.401	+0.000				0.34	0.34	+0.000	+0.600	+0.600	0.76	0.76	
r19	359	b1152_r19_359	PASS	1.297	1.297	+0.000				1.092	1.129	+0.037	+0.567	+0.567	0.70	0.70	
r23	372	b1152_r23_372	PASS	0.839	0.838	-0.001	+6.433	+6.433	+0.000	0.381			+0.500		0.66		
r43	720	b1152_r43_720	PASS	0.389	0.39	+0.001	+4.500	+4.567	+0.067	0.283	0.328	+0.045	+0.600	+0.500	1.16	1.11	
r31	313	b1152_r31_313	PASS	2.497	2.498	+0.001	+6.217	+6.217	+0.000	1.153	1.166	+0.013	+0.433	+0.433	0.69	0.69	
r13	209	b1152_r13_209	PASS	0.621	0.621	+0.000	+5.467	+5.467	+0.000	0.588	0.61	+0.022	+0.467	+0.467	0.89	0.85	
r3	377	b1152_r3_377	PASS	7.414	7.413	-0.001	+4.033	+4.033	+0.000	0.043			+0.433		0.54		
r42	700	b1152_r42_700	PASS	0.204	0.204	+0.000	+5.300	+5.300	+0.000	0.075	0.075	+0.000	+0.433	+0.433	0.86	0.86	
r15	289	b1152_r15_289	PASS	0.827	0.829	+0.002				0.732	0.789	+0.057	+0.600	+0.600	0.62	0.62	
r24	348	b1152_r24_348	PASS	1.76	1.76	+0.000				1.253	1.282	+0.029	+0.667	+0.633	0.52	0.52	
r21	218	b1152_r21_218	PASS	4.209	4.209	+0.000	+5.633	+5.633	+0.000	0.922			+0.600		0.71		
r30	231	b1152_r30_231	PASS	2.286	2.286	+0.000	+6.300	+6.300	+0.000	0.097	0.185	+0.088	+0.800	+0.700	0.78	0.73	
r32	374	b1152_r32_374	PASS	4.09	4.09	+0.000	+7.217	+7.217	+0.000	0.845	0.852	+0.007	+0.500	+0.467	0.49	0.49	
r41	703	b1152_r41_703	PASS	0.8	0.8	+0.000	+5.533	+5.533	+0.000	0.209	0.222	+0.013	+0.533	+0.500	1.02	0.94	
r11	285	b1152_r11_285	PASS	0.232	0.232	+0.000	+4.333	+4.333	+0.000	0.097			+0.500		0.59		
r22	201	b1152_r22_201	PASS	2.125	2.125	+0.000	+4.700	+4.700	+0.000	0.294	0.325	+0.031	+0.500	+0.467	0.78	0.73	
r35	253	b1152_r35_253	PASS	5.297	5.297	+0.000				3.789	3.806	+0.017	+0.533	+0.500	0.99	0.99	
r27	290	b1152_r27_290	PASS	0.244	0.244	+0.000	+4.300			0.146	0.178	+0.032	+0.433	+0.433	0.72	0.67	
r33	382	b1152_r33_382	PASS	0.36	0.361	+0.001	+4.917	+4.917	+0.000	0.046			+0.433		0.61		
r8	346	b1152_r8_346	over	0.262	0.261	-0.001	+2.167	+2.500	+0.333	0.513	0.55	+0.037	+0.600	+0.567	0.72	0.71	model/animation (B1157 cast pose: the raised hand -- the only delta vs the B1152 baseline; self-target landing ON the caster, see the README for the mechanism)
r29	286	b1152_r29_286	PASS	0.902	0.897	-0.005	+7.033	+7.033	+0.000	0.355	0.411	+0.056	+0.600	+0.467	0.56	0.56	
r28	73	b1152_r28_73	PASS	1.635	1.635	+0.000				1.363	1.419	+0.056	+0.700	+0.667	0.65	0.65	
r34	340	b1152_r34_340	PASS	1.068	1.068	+0.000	+5.967	+5.967	+0.000	0.387			+0.500		0.60		
r26	310	b1152_r26_310	PASS	1.87	1.87	+0.000	+4.667	+4.667	+0.000	0.368	0.401	+0.033	+0.533	+0.500	0.57	0.58	
```

(Verbatim `b1118_table.tsv`, generated by `scripts/compare_b1118.py <ev> <b1152_runs>
<b1151_readme> <out>`. Blank cells are the instrument's own: an empty `src_cov` is
`source_all` not clearing its blob threshold in the window (a measurement, not a hole
in the sweep -- every row has a `summary.tsv` and a `video.mkv`), and a blank `end`
is fx_timing finding no LANDING run in its own form (e.g. the tour-253 row, whose
B1157 summary carries `fxt_cov none`). The B1151 tour rows have no Source columns
(its table never measured them), so `src_cov_b`/`src_on25_b`/`src_cen_b` are blank
on every tour row.)

Owner reading of the six misses:

- **tour 93 / tour 38 (over: end +0.166, land -0.120).** The baseline is B1151's tour because
  B1152 never re-gated it. The B1152->B1157 half is readable on the record row: r0 (spell 93) is
  +0.000 land / +0.000 end -- so the entire tour-row delta is B1152's sim change (missile path +
  Middle stage), NOT this card. 38 renders identically to 93 in both gates (same sprite group).
- **tour 458 (short: end -0.133).** 458 is not one of the 40 records, so there is no B1152
  baseline; the row spans B1151->B1157 including B1152's sim change. Honestly unattributed
  (FOLLOW-UP below).
- **r1 (200) / r2 (202) / r7 (40) / r8 (346), all other-target except r8.** The sim is the same
  tree (B1157 touches only the animation path) and the plans/seed/fps are B1152's own, so every
  delta on these rows is the caster's animation. The mechanism, all three couplings in the
  instrument (`review/B1132/scripts/fx_timing.py`): (a) the lit series is a WHOLE-FRAME count
  against the pre-cast median (line 18) -- the raised arm is lit during the channel and gone
  after Act, and the hand-anchored held Source (B1131's rule: released Act+0.33, dead by
  Act+1.53) sits at HAND height in the animated run and at rest height in the idle run, inside
  the landing window's first 0.53 s; (b) the landing threshold is `max(thr2, 1.5 x stail)` where
  stail is the source-tail max over Act-0.25..Act (lines 45-46) -- the raised hand at Act raises
  stail and therefore the landing threshold; (c) r8 is self-target, so the landing stage is ON
  the caster's model and the pose is in the landing window outright. Named observations per row:
  r2's Source blob vanished from `source_all` (0.225 -> none: the raised-hand window does not
  clear the blob threshold the idle-hand window did); r7's Source grew +0.160 (0.364 -> 0.524,
  the raised hand IN the window); r1's Source grew +0.057; r8's end +0.333 with the self-target
  landing. None of the four is a sim word the sim does not honour -- the sim is unchanged; the
  card's "record word" owner category has no row in this table.
- The 36 PASSing record rows with a nonzero `d_src_cov` (e.g. r0 +0.048, r18 +0.043, r30
  +0.088) are the animation's expected Source-window effect inside the gate; they PASS because
  the gate axes are landing coverage/end, which held.

### 6.2 b1130 (40 records) -- `b1130_table.tsv`

Banded by B1155's own instrument (`band_table.py`, the B1130 oracle table, the b1130 rig),
joined to B1155's committed `banded_table.tsv` (the pre-B1157 idle caster on the B1152 tree)
record by record; three gate metrics, before/after/delta/both verdicts + owner. Owner rule
(`compare_b1130.py`): a miss that moved every axis toward the oracle is "cast-anim (B1157),
residual: <axes>"; a source_cov miss is "model/animation (cast pose owns the Source window)"
(the raised hand changes what the oracle window contains); otherwise "unattributed". The
record's sim word is out of this card's scope (Will's rule: the sim, the reader and
spell_visual_fx are not this card's), so no row is blamed on a sim field here.

Verdicts: 40/40 owned; 27 model/animation, 12 cast-anim-residual (9 source_cov, 3
source_cov/landing_cov/landing_end_s), 1 unattributed; 0 rows in band (the b1130 bands are the
oracle's -- the harness has never been in band there; the table's job here is to show what the
animation changed per row, and it changed 40/40).

```tsv
record	kind	b1155_source_cov	b1157_source_cov	delta_source_cov	v55_source_cov	v57_source_cov	b1155_landing_cov	b1157_landing_cov	delta_landing_cov	v55_landing_cov	v57_landing_cov	b1155_landing_end_s	b1157_landing_end_s	delta_landing_end_s	v55_landing_end_s	v57_landing_end_s	owner
0	fixed_npc	0.055	0.210	+0.155	short	over	1.070	0.358	-0.712	short	short	4.367	4.700	+0.333	short	short	model/animation (cast pose owns the Source window)
1	fixed_npc	0.052	0.090	+0.038	short	short	0.422	0.237	-0.185	short	short	5.300	3.533	-1.767	short	short	model/animation (cast pose owns the Source window)
2	fixed_npc	0.153	0.274	+0.121	over	over	0.399	0.356	-0.043	short	short	4.967	5.067	+0.100	short	short	model/animation (cast pose owns the Source window)
3	fixed_npc	0.044	0.077	+0.033	short	PASS	6.652	6.605	-0.047	short	short	6.400	4.000	-2.400	short	short	unattributed
4	fixed_self	0.096	0.123	+0.027	short	short	0.075	nan		short	n-a	4.433	nan		short	n-a	cast-anim (B1157), residual: source_cov
5	fixed_npc	0.087	0.137	+0.050	short	short	0.094	nan		short	n-a	4.500	nan		PASS	n-a	cast-anim (B1157), residual: source_cov
6	fixed_self	0.100	0.098	-0.002	short	short	0.188	0.142	-0.046	short	short	4.967	1.200	-3.767	short	short	model/animation (cast pose owns the Source window)
7	fixed_npc	0.122	0.231	+0.109	short	short	2.423	0.358	-2.065	short	short	6.767	4.467	-2.300	PASS	short	model/animation (cast pose owns the Source window)
8	fixed_self	0.044	0.083	+0.039	short	short	0.939	0.885	-0.054	short	short	3.567	3.600	+0.033	short	short	model/animation (cast pose owns the Source window)
9	fixed_self	0.362	0.536	+0.174	short	short	0.718	0.709	-0.009	short	short	5.333	4.833	-0.500	short	short	model/animation (cast pose owns the Source window)
10	fixed_self	0.440	0.379	-0.061	short	short	1.972	0.177	-1.795	short	short	5.267	3.900	-1.367	short	short	model/animation (cast pose owns the Source window)
11	fixed_self	0.013	0.053	+0.040	short	short	0.331	0.069	-0.262	short	short	5.567	2.000	-3.567	PASS	short	model/animation (cast pose owns the Source window)
12	fixed_npc	0.370	0.397	+0.027	short	short	1.661	0.723	-0.938	short	short	7.700	6.767	-0.933	over	short	model/animation (cast pose owns the Source window)
13	fixed_npc	0.231	0.269	+0.038	short	short	0.708	0.375	-0.333	short	short	5.767	4.300	-1.467	short	short	model/animation (cast pose owns the Source window)
14	fixed_self	0.036	0.087	+0.051	short	over	0.832	0.360	-0.472	short	short	5.933	5.667	-0.266	short	short	model/animation (cast pose owns the Source window)
15	fixed_npc	0.056	0.162	+0.106	short	short	0.381	nan		short	n-a	4.400	nan		short	n-a	cast-anim (B1157), residual: source_cov
16	fixed_npc	0.015	0.095	+0.080	short	short	0.123	nan		short	n-a	5.367	nan		short	n-a	cast-anim (B1157), residual: source_cov
17	fixed_self	0.219	0.195	-0.024	short	short	0.410	nan		short	n-a	4.767	nan		short	n-a	model/animation (cast pose owns the Source window)
18	fixed_npc	0.153	0.282	+0.129	short	short	1.095	0.553	-0.542	short	short	5.300	4.133	-1.167	PASS	short	model/animation (cast pose owns the Source window)
19	fixed_self	0.153	0.158	+0.005	short	short	0.149	nan		short	n-a	4.600	nan		short	n-a	cast-anim (B1157), residual: source_cov
20	fixed_npc	0.049	0.109	+0.060	short	short	1.154	0.298	-0.856	short	short	4.367	4.333	-0.034	short	short	model/animation (cast pose owns the Source window)
21	fixed_npc	0.345	0.259	-0.086	short	short	1.134	0.553	-0.581	short	short	5.500	5.567	+0.067	short	short	model/animation (cast pose owns the Source window)
22	fixed_npc	0.085	0.140	+0.055	short	short	0.348	nan		short	n-a	4.633	nan		short	n-a	cast-anim (B1157), residual: source_cov
23	fixed_npc	0.132	0.152	+0.020	short	short	0.450	nan		short	n-a	4.700	nan		short	n-a	cast-anim (B1157), residual: source_cov
24	fixed_npc	0.840	0.865	+0.025	over	over	2.259	1.504	-0.755	short	short	5.350	5.050	-0.300	short	short	model/animation (cast pose owns the Source window)
25	fixed_npc	0.022	0.119	+0.097	short	short	1.311	0.674	-0.637	short	short	4.200	4.467	+0.267	short	short	model/animation (cast pose owns the Source window)
26	fixed_npc	0.305	0.199	-0.106	short	short	5.697	nan		short	n-a	5.667	nan		over	n-a	model/animation (cast pose owns the Source window)
27	fixed_npc	0.144	0.168	+0.024	short	short	0.182	nan		short	n-a	4.433	nan		short	n-a	cast-anim (B1157), residual: source_cov
28	fixed_npc	1.149	0.798	-0.351	over	over	1.344	1.524	+0.180	short	short	6.433	6.033	-0.400	short	short	model/animation (cast pose owns the Source window)
29	fixed_npc	0.006	0.091	+0.085	short	short	0.214	0.095	-0.119	short	short	7.567	7.267	-0.300	over	PASS	model/animation (cast pose owns the Source window)
30	fixed_npc	0.090	0.141	+0.051	over	over	1.948	2.121	+0.173	short	short	6.200	6.167	-0.033	short	short	model/animation (cast pose owns the Source window)
31	fixed_npc	0.441	0.556	+0.115	short	short	4.938	2.524	-2.414	over	short	6.183	6.217	+0.034	short	short	cast-anim (B1157), residual: source_cov/landing_cov/landing_end_s
32	fixed_npc	0.752	0.588	-0.164	over	over	5.722	4.414	-1.308	short	short	7.917	7.150	-0.767	short	short	model/animation (cast pose owns the Source window)
33	fixed_npc	0.045	0.104	+0.059	short	over	0.318	0.099	-0.219	short	short	5.683	5.117	-0.566	over	short	model/animation (cast pose owns the Source window)
34	fixed_npc	0.184	0.238	+0.054	short	short	2.298	1.418	-0.880	short	short	7.100	5.367	-1.733	PASS	short	model/animation (cast pose owns the Source window)
35	fixed_npc	0.755	0.953	+0.198	short	short	2.636	1.791	-0.845	short	short	6.950	5.217	-1.733	PASS	short	model/animation (cast pose owns the Source window)
40	fixed_self	0.864	0.967	+0.103	short	short	1.150	0.624	-0.526	over	over	4.167	4.167	+0.000	over	over	cast-anim (B1157), residual: source_cov/landing_cov/landing_end_s
41	fixed_npc	0.093	0.120	+0.027	short	short	0.542	0.680	+0.138	short	short	5.600	3.633	-1.967	over	over	cast-anim (B1157), residual: source_cov/landing_cov/landing_end_s
42	fixed_self	0.101	0.056	-0.045	over	short	0.236	0.183	-0.053	short	short	4.900	4.900	+0.000	short	short	model/animation (cast pose owns the Source window)
43	fixed_self	0.330	0.302	-0.028	over	over	0.465	nan		short	n-a	4.700	nan		over	n-a	cast-anim (B1157), residual: source_cov
```

(Verbatim `b1130_table.tsv`, generated by `scripts/compare_b1130.py <ev>
<b1155_banded.tsv> <out>`; the after side is banded by B1155's own `band_table.py
against the same B1130 oracle table and bands. The full banded after-table is
`b1130_table.tsv.after.tsv`, generated beside it.)

Record 3 in words (the one unattributed): its source_cov moved 0.044 -> 0.077 (short -> PASS,
toward the oracle 0.076) -- the cast pose's effect; its landing_cov moved -0.047 (6.652 ->
6.605, both short) and its landing_end moved -2.400 (6.400 -> 4.000, both short against the
oracle's +9.000). The caster's pose is the only variable between the two runs (same sim, same
plan, same rig); the b1130 instrument measures the landing window from whole-frame lit counts
coupled to the source-tail threshold, so the pose CAN reach the landing axis -- but this row
was not frame-verified (B1155's raw measure dirs were not committed, only its banded table),
so per the card's rule the owner stays "unattributed". The residual gap against the oracle
(harness +4.000 vs oracle +9.000) was already short in B1155 (harness +6.400): a pre-B1157
harness-landing gap for spell 377 -- the sim/reader's territory, not this card's.

## 7. FOLLOW-UP (per owner)

- **tour 458 (b1118) and b1130 record 3**: unattributed rows that span a sim change the card
  does not own (458) or pre-date it (record 3's landing). When the next FX-sim/reader card
  re-gates, give 458 a B1152-baseline tour row (run the tour on the B1152 tree once) so the
  span is separable, and take record 3's landing gap (oracle +9.000 vs harness +4.000) into the
  sim's scope.
- **The 12 cast-anim-residual b1130 rows**: the animation moved them toward the oracle and the
  residual is on the source/landing axis; the next re-band (B1158's ledger consumes these runs;
  the sim's card re-bands against the oracle) owns closing them.
- **The 27 model/animation b1130 rows**: expected, named; no action on this card. Re-banding
  against the oracle is the ledger/sim cards' job.
- **B1158 visual ledger**: its first run was the B1155 baseline (landed 2219680f); the B1157
  runs (`/home/user/kb-evidence/kb-b1157-cast-anim/{b1118,b1130}/`, all frames + video.mkv) are
  the animated-caster leg -- the orchestrator copies them to `/mnt/projects/fable/review/B1157/`
  at hand-back so the ledger can consume them.

## 8. Acceptance map

(A) harness caster track + timing == the client's, pinned by `FxHarness.TheCasterPlaysTheSpellCastAnimThroughTheClientCode`
(93/38/54, both sides call the helper) -- sec. 3.
(B) hand-anchor strip -- sec. 4.
(C) both re-gates tabled with before/after + owner per miss -- sec. 6.
(D) targeted ctest + hygiene + ONE whole-tree CI build green, pipeline success -- sec. 3 + the
hand-back line (the MR pipeline is the whole-tree build).
(E) changed: `include/client/graphics/spell_cast_animation.h` (the shared helper -- the card
allows it when the mapping must be extracted; it HAD to be, for the client half), the named
client TUs, tools, tests, `scripts/refactor/check_zero_callers.py` (the exemption line),
`refactor/B1157/` -- CLAUDE.md untouched.
(F) MR "Closes #476".
