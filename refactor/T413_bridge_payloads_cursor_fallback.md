# T413 — bridge payload fields nothing draws (B296); the cursor name (B285); the spell fallback blend (B286)

Class A + B + B, slot1, no server. Read the three board entries, T407's
census (`scripts/refactor/check_write_only_fields.py`, the B296 row
list), T406's §Review (B285/B286).

## Leg A — B296

Ten bridge-event payload fields are filled on the game thread and read
by no renderer consumer. For each: what the official client SHOWS for it
(a target's HP bar, a buff's remaining ticks, a group member's mana, a
spell gem's refresh…) — name the oracle UI element from the default UI
XML (`/mnt/projects/EverQuestP1999/uifiles/default/EQUI_*.xml`) or an
existing review screenshot. Then either draw it (the static-panel seam;
placement scaled from the XML as T393 did) or, if fable has no panel for
it yet, board the panel as its own item (B320+) and leave the field.
Do the ones whose panel exists. Tests pre-fix failing per field drawn.

## Leg B — B285

`createSoftwareCursor()` asks the cache for `softwareCursor` while the
pack entry is `software_cursor`; it works only because the cursor loads
before the pack opens. Make the name the pack's (one name), and a test
that the cursor resolves from the pack after the pack is open (pre-fix:
FATAL). DRM path only — say how you exercised it headlessly.

## Leg C — B286

`spell_particle_fallback` is drawn `GL_ONE, GL_ONE` (additive) with
constant white RGB and the shape in alpha, so it is a white square by
construction. Match the oracle: T389's generic blue dome for
EmptyRecord/NoAffectIndex spells is the thing this fallback stands in
for. Either premultiply in the shader for additive (rgb *= a) or switch
the fallback material to alpha-blend — decide from what the oracle's dome
looks like (T389 frames: soft, translucent). Test pre-fix failing on the
rendered fallback's edge alpha (headless GLES2, T411's harness shape).

## Proof

Build (all trees), `ctest -j6` (no expected failures on this tip),
pre-fix/post-fix, visual gate standalone + ARM (a moved image golden is a
STOP with the contact sheet), file sizes. Strike B285, B286; strike or
annotate B296 per field.

STOP: none expected.

---

## Review

Branch `task/T413-bridge-payloads-cursor-fallback`, base `45d30d57`, slot1
(RelWithDebInfo). Commits: **Leg B `cd7a9bab`**, **Leg A `89e773df`**, **Leg C + board — this commit (a commit cannot carry its own hash; the orchestrator has it at merge)**. No server touched (T408 held the eqemu lock; nothing
here needed one).

### Leg A — B296, per field

| field | disposition | oracle element |
|---|---|---|
| `CastingStateChangedData::castTimeRemainingMs` | **drawn** | `EQUI_CastingWindow.xml` `<Gauge item="Casting_Gauge">` EQType 7, 116x10 at (3,19) in the 140x60 "Casting Time" screen |
| `CastingStateChangedData::castTimeTotalMs` | **drawn** | same gauge |
| `SpellMemorizingData::progressMs` | **drawn** | `EQUI_SpellBookWnd.xml` `<Gauge item="SBW_Memorize_Gauge">` EQType 9, 100x8 at (204,312), FillTint (200,0,200), in the 510x360 `SpellBookWnd` |
| `SpellGemChangedData::memorizeTotalMs` | **dropped** | none — `EQUI_CastSpellWnd.xml`'s `<SpellGem item="CSPW_Spell0".."Spell8">` carry only a `SpellGemDrawTemplate` (Holder/Background/Highlight); no Gauge piece exists on a gem |
| `CursorItemChangedData::queueSize` | **dropped** | none — `EQUI_CursorAttachment.xml` is `CA_Anim`, `CA_Anim2`, `CA_SpellGem`, three 40x40 icon slots with no count label |
| `SkillActivationFeedbackData::cooldownMs` | **boarded B320** (panel missing) | `EQUI_CombatAbilityWnd.xml` `<Gauge item="CAW_CombatEffectTimeRemainingGauge">` EQType 26 — fable has no combat-abilities window |
| `TradeskillContainerOpenedEvent::containerName` / `::isWorldObject` / `::slotCount`, `TradeskillContainerClosedEvent::wasWorldObject` | **boarded B321** (panel missing) | `EQUI_TradeskillWnd.xml` — fable has no tradeskill container window |

Two findings worth naming:

1. **Both drawn gauges already existed.** `renderCastingBar()` and the
   spellbook popup have been in the frame for units. What was missing was the
   wiring: the `CastingStateChanged` and `SpellMemorizing` bridge arms were
   `LOG_TRACE`s whose comments said the bars were "driven by direct
   SpellManager -> WindowManager calls" — `WindowManager` was deleted in
   `0f25cb57`. So this was not an unimplemented feature; it was two arms left
   pointing at a class that no longer exists.
2. **The casting bar had no authoritative clock at all.** Its progress came
   from a render-thread stopwatch started when `SpellCastVisualStarted`
   arrived, while the game thread was computing and publishing the real
   remaining/total on a different event that nothing read. The stopwatch is
   kept as the fallback for frames before the first sample (`totalMs == 0`),
   so nothing regresses when only `SpellCastVisualStarted` has fired.

Placement is the oracle's, mapped by the same proportional integer-division
rule `InventoryLayout::coinRect()`/`paperdollRect()` use:
`spellbookMemorizeGaugeRect()` is `{360,428,438,435}` at 800x600 and holds the
oracle's proportion, and stays inside `centerPopup`, at 800x600, 1024x768,
1280x720 and 1920x1080.

Tests: `tests/test_bridge_payload_fields.cpp`, 12 new.
**Pre-fix** (production reverted with `git diff > x.patch` + `git apply -R`,
tests untouched): the TU does not compile —
`'applyCastingStateChanged' was not declared in this scope`,
`'CastingBarState' has no member named 'totalMs'` / `'remainingMs'`,
`'SpellbookPopupState' has no member named 'isMemorizing'`.
**Post-fix**: `12 tests from 3 test suites ran. [ PASSED ] 12 tests.`

No render-order golden moved: the memorise gauge is drawn *inside*
`renderSpellbookPopup()`, so `frame_render_order_g05.txt` is unchanged.

### Leg B — B285

Fixed as planned: the cache name is the pack's (`software_cursor`), one name,
and the load-bearing ordering is now commented as no longer load-bearing
rather than relied upon.

Exercised headlessly, no DRM and no driver: `getOrLoad()` resolves an EQE1 hit
and returns before any GL call. `tests/test_software_cursor_pack_name.cpp`
reads the name out of `createSoftwareCursor()`'s own source (so the test
cannot pass by agreeing with itself), looks it up in the real shipped
`willeq.eqe1` (125 textures, 61 alpha, 1 raw, 5,532,254 bytes), then drives
the real `ConstrainedTextureCache` **with the pack already open** — the
configuration the initialisation order was hiding — and finally pins the
consequence with a death test: with the pack open, an unmatchable name is
`ExitedWithCode(1)`.

**Pre-fix** (only the production literal reverted): 2 FAILED —
`TheCacheNameResolvesInTheShippedPack` and
`TheCursorResolvesFromTheCacheAfterThePackIsOpen`, both reporting
*"createSoftwareCursor() asks the cache for 'softwareCursor', and willeq.eqe1
has no such entry"*. **Post-fix**: 4 passed.

### Leg C — B286: DEVIATION, and why

**Measured first.** `data/textures/spell_particle_fallback.bmp` is 32x32,
**24-bit**, and **every one of its 3,072 pixel bytes is 255**:

```
$ od -An -tu2 -j28 -N2 spell_particle_fallback.bmp   ->  24        (bpp)
$ od -An -tu4 -j18 -N8 ...                           ->  32   32   (w, h)
$ tail -c +55 ... | od -An -tu1 -v | sort -n | uniq -c
   3072 255
```

The file is solid white with **no falloff in any channel**.
`generateSpellParticleFallback()` computes the cosine falloff and writes it
into ALPHA only (`SColor(alpha, 255, 255, 255)`); the 24-bit BMP write
discards that channel. The shape is destroyed at **generation** time, not at
blend time.

That refutes **both** options this plan offered:

- *premultiply in the shader for additive (`rgb *= a`)* — there is no alpha to
  premultiply; a 24-bit BMP decodes with `hasAlpha == false` and alpha 255
  everywhere. It would also change every other additive draw in the game
  (`SOLID3D_FS` is shared with the storm-cloud dome and the billboards), so it
  would need a new uniform in the vendored Irrlicht for zero benefit here.
- *switch the fallback material to alpha-blend* — same reason: alpha would be
  1 everywhere and the result is the same opaque white square, only composited
  differently.

**The decision, with the oracle reason: keep the additive blend and put the
falloff in RGB.** T389's generic dome is soft and translucent, and an additive
sprite gets exactly that from an RGB falloff — which is also what the official
client's Glowflare DDS carries, and what B286's own option (a) says.

**Why it is not in this commit.** That is an asset change, and the asset
cascade is `data/textures/*.bmp` -> `data/textures.sha256` -> the SHARED
`/mnt/projects/EverQuest_ETC1/willeq.eqe1` -> `data/willeq.eqe1.sha256`, all in
one commit. `scripts/build_willeq_pack.sh --check` (the `PackReproducible`
ctest test) compares the rebuild against the pinned hash **and against the
INSTALLED pack**, which every worktree reads live — so installing a new pack
turns `PackReproducible` red in every slot running a unit at that moment. T408
(slot2) and T412 (slot3) were both live. Reinstalling the shared pack from
under them is the hazard T405 boarded, and the executor rule is to stay out of
parallel units' territory. It needs a unit that owns the pack exclusively, the
way T411 did.

Delivered instead: the measurement, the decision, **B322** with the full recipe
and the sequencing constraint, and a corrected reason string in
`tests/test_texture_alpha_census.cpp` — the old one said "converting the file
would change nothing" because the alpha is unsampled, which understates it:
there is nothing in the file to convert. **B286 is annotated, not struck.**

Also worth recording: the fallback is reached only when Glowflare03.dds,
glittersp501.dds and Corona1.dds are all missing, so this is low urgency and
deferring costs nothing visible.

### Proof

| step | result |
|---|---|
| `cmake --build build -j24` (RelWithDebInfo) | EXIT=0 |
| `ctest --test-dir build -j6` | **100% tests passed, 0 failed out of 4250** (256 s) |
| `bash scripts/build-gles2-x86.sh` | EXIT=0 |
| `bash scripts/ci/visual_gate.sh` (standalone) | **=== VISUAL GATE: PASS ===**, goldens untouched (`git status` clean under `tests/goldens/`) |
| `bash scripts/build-arm-noble.sh` | EXIT=0 |
| `bash scripts/refactor/check_file_sizes.sh fable` | 18 files checked: **18 ok, 0 warn, 0 fail** (largest touched: `event_bus.h` 1,173; `static_panels.cpp` 1,099) |

No image golden moved, so no contact sheet and no STOP on that account.

### Surprises

- The first three `cmake --build` invocations hit **exit 143** at the 600 s
  ceiling, all three while `ar` was writing `lib/libwilleq_core.a` (324
  members, RelWithDebInfo debug info, 46 GB build tree on NFS at 94% full) with
  two other slots building concurrently. The post-143 sweep found two
  truncated non-ELF binaries (`gles2_shader_diagnostic`, `test_door_loading`),
  a `gmake: *** Deleting file 'lib/libwilleq_core.a'` (the ABSENT-artifact
  shape), and two `build/lib/st??????` `ar` temp files. All were deleted and
  rebuilt; the eventual build is clean and the suite proves it.
- `check_file_sizes.sh fable` reported **18** changed files against a branch
  that touches 12 — `fable` advanced under the slot while this unit ran. All
  18 are ok, so nothing is hidden by it, but a size check's file count is not
  a diff summary.
