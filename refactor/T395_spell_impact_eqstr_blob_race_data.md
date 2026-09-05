# T395 — spell impact draws nothing (B204); the eqstr hex blob (B205); race_data deleted (B216)

Class B ×3, slot2, no server. Read the three board entries and T386 §3.

## Leg A — B204

An observing client 15 units away logged both `SpellAction` packets for a
Fire and a Cold cast and its pixels did not move. T381 keyed colours on
the vocabulary; B204 says nothing reaches a frame. Trace the consumer
chain from `SpellCastVisualComplete` in `irrlicht_bridge.cpp` to pixels:
`createSpellComplete` / `createImpact` → which scene node / particle
emitter; is the emitter attached to the ENTITY node of the caster (which
for the observer is a remote entity — does the lookup by spawn id find
it?), is it created with zero lifetime / zero particles, is it culled by
the entity render distance, is the particle manager's render pass
reached on the observer? Instrument nothing permanent: a headless test
that publishes the two events for a REMOTE entity through the bridge and
asserts a node/emitter exists with non-zero particles and a lifetime
(pre-fix failing), and a frame-render assertion that the pass draws it
(G05 frame order golden if a statement changes — learn in place). Fix
the first broken link you can prove; if the chain is intact and the gap
is the atlas (B162/B165 territory), say so with the evidence and annotate
instead of striking. Live proof owed to T389.

## Leg B — B205

`Your spell 0FFFFF000000004F00000000000000000000000000000Burst of Flame
has failed to land…` — a 45-char blob before the spell name. Find the
eqstr (grep `has failed to land` in `eqstr_us.txt`), the assembler that
substitutes `%1`, and what fills the argument: the blob looks like a
link-encoded item/spell name (Titanium's `\x12` link format encodes a
hex header before the name). Read how the official client renders the
message (T386's oracle screenshot of the same refusal if captured;
otherwise T345's) — it shows the bare spell name. Fix the consumer to
strip/decode the link header (there may be an existing link parser for
item links — reuse it). Test, pre-fix failing, over the two measured
strings. Strike B205.

## Leg C — B216

`race_data`: 475 rows, zero consumers in six directories. Delete the
loader, the table from the schema and any builder docs; willeq.db
rebuilt (content-only — no version bump unless the schema check
requires it; read how the loader validates tables and say which). T392's
identity proof over the untouched tables. Strike B216.

## Proof

Build (all trees if the db changes), `ctest -j6` — the suite has NO
expected failures since T392 (a red is real), the tests with
pre-fix/post-fix runs, file sizes; graphics change in Leg A → visual gate
standalone + ARM.

STOP: Leg A's chain is intact end to end (annotate, do not invent a
draw); the blob is not a link header (report the bytes).

---

## Review

Executed 2026-09-05 in `wt/slot2`, branch `task/T395-spell-impact-eqstr-race-data`,
base `7436c025` (verified at checkout). No server lock taken; no live run.

| leg | board | commit |
|---|---|---|
| A | B204 struck | `4537d40f` |
| B | B205 struck | `54643545` |
| C | B216 struck | `fd8f2779` |

New board items: **B220**, **B221** (both found while tracing Leg A).

### Leg A — B204: the chain is NOT intact, and the atlas is not the gap

Two broken links, both upstream of B162/B165, and both fixed.

**1. The gate.** `SpellManager::handleSpellAction` published
`SpellCastVisualComplete` with `isSuccess = (effect_flag == 4)` and
`irrlicht_bridge.cpp` gated `createSpellComplete()` + `createImpact()` on it.
eqemu's `Mob::SpellOnTarget`
(`/mnt/projects/akk-stack/code/zone/spells.cpp:3963..4020`, `:4647..4680`)
sends **two** OP_Action packets for one cast:

| packet | server's own comment | recipients |
|---|---|---|
| `effect_flag = 0` | *"Actual cast action - this causes the caster animation and the particles around the target"* | target, caster, **and `entity_list.QueueCloseClients(...)`** — everyone in range |
| `effect_flag = 0x04` | *"send the action packet again now that the spell is successful … this is what causes the buff icon to appear"* | **only** the target client and the caster client |

So the impact visual was gated on the one packet a bystander never receives —
the visual was unreachable for an observer *by construction*, independently of
colours, atlas tiles or `spells.eff`. **T386's own logs already said so and
were read the other way**: the observer
(`review/T386/logs/fable_session_spells13_observer.log:952,1161`) records
`effect_flag=0` twice and `effect_flag=4` **never**, while the caster's log
(`fable_session_spells13_caster.log:104`) records the 4. B204's text ("both
landed, effect_flag=4") is describing the caster's log, not the observer's.

Fix: `SpellCastVisualCompleteData` gains `isCastAnimation = (effect_flag == 0)`
and the bridge gates the FX on that. `isSuccess` keeps its meaning for the chat
lines, the buff icon and `completeCast()`. The cast-timeout path (no OP_Action
ever arrives there) sets `isCastAnimation` too, so it is not a regression. The
caster, which receives both packets, now draws exactly one impact instead of
one — pre-fix it drew one, from the wrong packet; a naive "draw on both" would
have double-fired, which the second test pins against.

**2. The origin burst.** `SimulationWorker::computeParticles` runs
`---- Spawn phase ----` **before** `---- Spell effect lifecycle ----`, and only
the lifecycle phase refreshed an entity-attached emitter's position from
`pi.entityPositions`. That map is one frame behind by design: the renderer
(`SimBridgeComponent::applyWorkerResults`) resolves only the entities the
**previous** frame's `output.particleOutput.positionRequestEntities` named. So
on the frame an impact effect is created the target's position is necessarily
absent, the emitter is placed at `(0,0,0)`, and its *first spawn frame* still
sees `(0,0,0)`:

- frustum culling off → the whole one-shot burst fires at the world origin and
  `isBurstSpawned` latches, so nothing ever appears on the target. **Measured
  in the pre-fix test run: every particle 1,020.76 units away, at
  `(-0.06, 1.02, 0.01)`.**
- frustum culling on (the shipped configuration) → the emitter is culled at the
  origin instead, spending a frame out of the impact preset's **0.1 s**
  `emitterLifetime`. Below ~20 fps it is dead before its position ever arrives.

Fix: refresh an attached (non-projectile) emitter's position in the spawn
phase, before the lifetime check and the frustum test.

**Tests** — `tests/test_spell_impact_visual.cpp`, 5 tests, **4 red pre-fix**:

| test | pre | post |
|---|---|---|
| `SpellImpactVisualProducer.AnimationPacketArmsTheImpactVisual` | FAIL | PASS |
| `SpellImpactVisualProducer.LandedPacketDoesNotRearmTheImpactVisual` | PASS | PASS |
| `SpellImpactVisualProducer.ExactlyOneOfTheMeasuredPairArmsTheVisual` | FAIL | PASS |
| `ImpactParticleTest.TheBurstSpawnsOnTheTargetNotAtTheWorldOrigin` | FAIL | PASS |
| `ImpactParticleTest.TheBurstIsNotCulledAtTheWorldOrigin` | FAIL | PASS |

The producer half drives the real `ReplayZonePacket` → `ZoneProcessAction`
dispatch through a bare `EverQuest` (T348's seam). The worker half drives
`SimulationWorker::computeOnce()` with the exact `ParticleCommandData`
`ParticleManager::createSpellEffect()` enqueues for `createImpact()`, and
**honours the one-frame position-request protocol** — an earlier draft fed
`entityPositions` unconditionally and went green pre-fix, which is precisely
the shape of test that cannot see this bug.

No golden covers `irrlicht_bridge.cpp` or `sim_particles.cpp`
(`frame_render_order_g05` pins `renderer_frame_render.cpp`, untouched), so no
statement golden changed and none was re-blessed.

**OWED, and deliberately not claimed:** the live pixels. B204's closing
condition is "a landed spell paints something an observing client can see and
the two vocabulary colours differ on screen". This unit held no eqemu lock
(T391 has it) and measured nothing live. The board strike says so; the proof is
T389's.

**A trap for whoever runs it.** T386's observer log contains **no BeginCast at
all**, so the cast-glow half was never exercised there either — and that is not
a defect: eqemu *does* broadcast `OP_BeginCast` to close clients
(`Mob::SendBeginCast`, `QueueCloseClients`, `RuleI(Range, BeginCast)`), but a
GM `#castspell` never calls it. **Use a real gem cast**, or the glow half stays
unmeasured a second time.

### Leg B — B205: it is a say-link, and the board's read of the bytes was wrong

`grep "has failed to land"` finds nothing in `eqstr_us.txt` or `eqstr_en.txt` —
the line is not an eqstr at all. It is built server-side in
`Mob::SpellOnTarget` (`zone/spells.cpp:4093`) as

    "Your spell {} has failed to land on {} because {} are invulnerable."

with the first argument `Saylink::Silent(fmt::format("#castspell {}", spell_id),
spells[spell_id].name)`. `EQ::SayLinkEngine::GenerateLink()`
(`common/say_link.cpp:86`) emits

    0x12  <fixed-width hex body>  <display text>  0x12

and the body width is client-version specific; `generate_body()`'s own comment
lists the Titanium/"6.2" layout as **45** characters. Decoding the measured
blob against it:

| field | width | bytes | value |
|---|---|---|---|
| `action_id` | `%1X` | `0` | 0 |
| `item_id` | `%05X` | `FFFFF` | 1048575 = `SAYLINK_ITEM_ID` |
| `augment_1` | `%05X` | `00000` | 0 |
| `augment_2` | `%05X` | `0004F` / `00050` | **79 / 80** |
| `augment_3..5` | `%05X`×3 | `00000` | 0 |
| `is_evolving` | `%1X` | `0` | 0 |
| `evolve_group` | `%04X` | `0000` | 0 |
| `evolve_level` | `%1X` | `0` | 0 |
| `hash` | `%08X` | `00000000` | 0 |

`GenerateQuestSaylink()` puts a **silent** saylink's row id in `augment_2` (a
spoken one goes in `augment_1`), so 79 and 80 are consecutive `saylink` table
row ids for `#castspell 93` and `#castspell 54`. That is exactly the "sequential
and not the spell ids" the board recorded and could not explain.

**Two corrections to B205's own text.** (a) `0FFFFF` is not `0F` + a colour —
it is `action_id` + `item_id`. (b) The 0x12 delimiters *are* in the line and
were invisible in the transcription: `cat -A` on
`review/T386/logs/fable_session_selfcast.log:1041,1879` shows `^R` on both
sides of the body, and both blobs are 45 characters on the wire (the board's
second one is one character short).

Fix: `eqt::StripSayLinks()`
(`include/client/say_link_text.h`, `src/client/core/say_link_text.cpp`, a new
TU), applied at the two consumers that print server-authored text —
`ZoneComponent::OnSpecialMesg` and `ChatComponent::ZoneProcessChannelMessage`,
because a player's item link is the same wire shape. An unpaired delimiter or a
payload shorter than the body width is left exactly as it arrived rather than
guessed at; silently eating 45 characters of a real message is the worse
failure.

**Tests** — `tests/test_say_link_text.cpp`, 7 tests, **2 red pre-fix**:

| test | pre | post |
|---|---|---|
| `SayLinkText.TheTwoMeasuredRefusalsReadAsTheBareSpellName` | PASS | PASS |
| `SayLinkText.PlainTextIsUntouched` | PASS | PASS |
| `SayLinkText.TwoLinksInOneLineBothResolve` | PASS | PASS |
| `SayLinkText.AnUnpairedDelimiterIsLeftAlone` | PASS | PASS |
| `SayLinkText.AShortPayloadKeepsItsText` | PASS | PASS |
| `SayLinkTextConsumer.SpecialMesgPrintsTheBareSpellName` | **FAIL** | PASS |
| `SayLinkTextConsumer.ChannelMessagePrintsTheBareLinkText` | **FAIL** | PASS |

The helper tests are green pre-fix because the helper is the fix; the two
consumer tests are what pin the defect, and both replay the measured bytes
through the real packet dispatch.

### Leg C — B216: race_data deleted, version unmoved

Census re-run over `src/ include/ tests/ tools/ benchmarks/ docker/ scripts/`
for `race_data` and `getRaceData`: only the builder. Deleted `loadRaceData()`,
its `CREATE TABLE`, its call site and the `race_data:` report line; a tombstone
names the reason. The `readLine()` comment that listed `race_data` among T392's
four CR-tainted tables was amended, not deleted — it is the record of why that
fix mattered.

**How the loader validates tables, and therefore whether the version moves:**
`WilleqDatabase::open()` (`src/client/willeq_database.cpp:80..140`) checks
exactly one thing — `SELECT value FROM metadata WHERE key='version'` against
`kWilleqDbExpectedVersion`. There is **no table list**, no per-table probe.
Dropping a table is therefore content-only and the version **stays at 7**.

**T392 identity proof** (`sqlite3` row count + sha256 over the full
`SELECT *`, before vs after):

| table | rows | identical |
|---|---|---|
| `spells` | 8,066 | yes |
| `dbstrings` | 8,305 | yes |
| `eqstrings` | 5,901 | yes |
| `item_models` | 3,661 | yes |
| `equipment_textures` | 2,623 | yes |
| `sound_assets` | 2,165 | yes |
| `equipment_models` | 1,000 | yes |
| `login_strings` | 470 | yes |
| `race_models` | 190 | yes |
| `music_files` | 24 | yes |
| `animation_sounds` | 9 | yes |

`diff` of the two manifests is empty. Tables 14 → 13 (`race_data` gone).
`metadata` differs only in `build_date` and `total_entries`, 32,889 → 32,414 =
**475**, the deleted rows exactly. File committed at mode 664 (git 100644).

`WilleqDatabaseShippedContentTest.RaceDataTableIsGone` — FAIL pre-fix, PASS
post-fix — sits beside T262's `ZoneCharactersTableIsGone`.

### New board items

- **B220** (B) `ParticleManager::removeSpellEffect()` writes
  `cmd.preAssignedEffectID`; the worker's `RemoveSpellEffect` arm matches on
  `cmd.effectID`, which is never set and defaults to 0 while effect ids start
  at 1 — the removal can never hit. Zero callers tree-wide, so nothing is
  broken today.
- **B221** (B) `ParticleManager`'s constructor loads
  `config/spell_effects.json` by a **CWD-relative** path. Started from anywhere
  else the load fails silently and every preset falls back to `EmitterConfig`'s
  defaults — which for `impact` is a *permanent 10/s emitter* instead of a
  36-particle burst with a 0.1 s lifetime, i.e. a different effect, not a
  degraded one. Measured: the Leg A worker test produced zero particles until
  it loaded the config the way the renderer does.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0** |
| `ctest --test-dir build -j6` | **4,067 / 4,067, 0 failures** |
| `bash scripts/build-gles2-x86.sh` | **exit 0** (first attempt hit the 600 s ceiling, **exit 143**, mid-link — `bin/willeq` had been *deleted* by make, the absent-artifact shape; archives checked with `ar t \| wc -l` (319 / 15 members), no `st??????` temp files, rebuilt clean, ELF magic verified) |
| `scripts/ci/visual_gate.sh` standalone | **PASS**, goldens untouched (`git status tests/goldens` empty). CPU p95: qeynos2 7.513/10.108, gfaydark 7.791/10.672, befallen 6.353/10.948 |
| `bash scripts/build-arm-noble.sh` | **exit 0**, `build-arm-noble/bin/willeq` ELF 32-bit ARM |
| `scripts/refactor/check_file_sizes.sh fable` | 15 files checked, **15 ok, 0 warn, 0 fail** |

### Deviations and surprises

1. **B204's headline evidence was misattributed and nobody had noticed.** The
   entry says "both landed, `effect_flag=4`"; no observing client in any T386
   log ever saw a 4. The fix was findable from the logs already on disk.
2. **The atlas was never the gap.** The plan's fallback branch (annotate if the
   chain is intact and the gap is B162/B165) did not apply — the chain was cut
   twice before any texture question could arise.
3. **Leg A fixes two links, not one.** The plan says "fix the first broken link
   you can prove". Fixing only the gate would have left the burst firing at the
   world origin, so the unit would have shipped a change that still paints
   nothing; both are pinned by their own tests.
4. **The first draft of the worker test passed pre-fix** because it fed
   `entityPositions` unconditionally. The one-frame request protocol is the
   whole mechanism — a test that ignores it proves nothing.
5. **`SpellEffectsConfig` had to be loaded by hand in the test**, which is how
   B221 was found: without it `SpellPresets::Impact()` is not the shipped
   impact at all.
6. **B204 struck, but its stated closing condition is not met.** Per the
   dispatch ("struck only if a broken link was proven and fixed") it is struck;
   the strike text carries the owed live proof explicitly so it is not lost.
