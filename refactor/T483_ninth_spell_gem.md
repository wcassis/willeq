# T483 — fable tracks 8 spell gems; Titanium has 9 (B762)

Class A (unconfirmed live), **no lock (Leg A)**, slot3. Found by T482 (read its
Review, `refactor/T482_cast_ended_by_stopcasting_or_resist.md`, B762). Context:
Will's Pi report on 2026-09-13 was "no spell gems visible". T459 and T462 later
showed the gem column IS in the rendered buffer, with 8 slots. Still, the gem
count is part of what Will looks at.

## The lead

`SpellManager` (and/or the gem state, the bridge payloads, the static UI gem
panel, `/cast`) sizes the gem bar at 8. Titanium has 9 memorised gems:
eqemu's `MAX_PP_MEMSPELL` for the Titanium client
(`/mnt/projects/akk-stack/code/common/patches/titanium_*`, `common/eq_limits.h`)
and the oracle UI's `EQUI_CastSpellWnd.xml` `CSPW_Spell0..CSPW_Spell8` (T461
and board ~5121 quote it). So slot 8 is dropped everywhere: the profile's
memorised spell 9, `OP_MemorizeSpell` for slot 8, the 9th gem button, and
`/cast 9`.

## Tasks

1. **Establish the count from the sources.** Cite the eqemu Titanium limit and
   the struct sizes (PlayerProfile `mem_spells`, `MemorizeSpell_Struct`), the
   oracle XML (`/mnt/projects/EverQuestP1999/uifiles/default/EQUI_CastSpellWnd.xml`),
   and whether the 9th gem is gated by level or AA in Titanium (and if so, what
   the client shows before it unlocks). Do NOT invent a gate.
2. **Census every 8** tied to gems: constants, arrays, loops, bridge payloads,
   UI layout (`UILayout` spellGems rect, `renderSpellGemPanel`, the atlas
   `SpellGem*` sprites), hotkeys (`/cast N`, gem key bindings), persistence, and
   tests. Name each one in the Review.
3. **Pre-fix failing tests:**
   - a profile with 9 memorised spells populates 9 gems;
   - `OP_MemorizeSpell` for slot 8 lands;
   - the gem panel emits 9 slots at 800x600 and at 1280x720, all on-screen,
     using the rect arithmetic T459/T462 used;
   - `/cast 9` reaches `beginCastFromGem(8)`.

   Then fix the count at one named constant.
4. **Layout.** Place the 9th gem the way the oracle XML does, scaled to fable's
   layout like T393's purse: proportionally, with the source numbers quoted.
   Capture offline before/after stills at 800x600 and 1280x720 into
   `review/T483/` with a README. Never self-assess.

## Proof

- Standard verification bar, builds strictly in sequence.
- G-batch goldens for every TU touched, updated in place per G-correction 32.
  Say so; `zone_dispatch`, `bridge_component_intents` and `renderer_frame_render`
  are pinned.
- Visual gate. If an image golden moves because the gem column grew, STOP and
  report; Will blesses.
- `UIAtlasPixelTest`, ARM, file sizes.
- Board B770–B779; strike or narrow B762.

File territory: `src/client/spell/`, the gem UI panel and layout, gem
payloads/events, `/cast` and gem hotkeys, spell and UI tests, `review/T483/`.
**Not** `camera_rig.h` (T478, slot1). **Not** any loader or the T479 branch
(slot2).

STOP: an image golden moves; the 9th gem is gated in Titanium by something fable
does not track (report the gate).

## Review

**Verdict: STOPPED at task 1, on the plan's second STOP condition. In the
server fable plays against, the ninth gem is gated by an AA fable does not
track. The oracle's own UI does not show it either. No code was changed, no
test was written and no build was run. Tasks 3 and 4 are left for Will's
decision (board B770).**

### 1. The count, and the gate (source evidence)

The wire carries 9 gem slots:
- `/mnt/projects/akk-stack/code/common/patches/titanium_limits.h:308-328`:
  `CastingSlot` is `Gem1 = 0` .. `Gem9 = 8`, `MaxGems = 9`, and
  `SPELL_GEM_COUNT = MaxGems`.
- `titanium_structs.h:827` `spellSlotRefresh[SPELL_GEM_COUNT]` and `:854`
  `/*04360*/ mem_spells[SPELL_GEM_COUNT]`: the PlayerProfile has 9 x uint32 at
  4360. `titanium.cpp:1358` and `:1410` ENCODE all 9 with `OUT_array`.
- `titanium_structs.h:3321` (the unmemorize-all struct) is also `spell[9]`.
- fable already has `EQT::SPELL_GEM_COUNT = 9`
  (`include/common/packets/common.h:33`, pinned by `test_packet_structs.cpp:134`).
  No gem code uses it.

**The server gates slot 8 (the ninth gem):**
- `zone/client_packet.cpp:4387-4392`, `Handle_OP_CastSpell`: if
  `RuleB(Spells, RequireMnemonicRetention)` and the slot is in 8..11 and
  `GetAA(aaMnemonicRetention) < slot - 7`, the server calls `InterruptSpell`
  and sends the red message "You do not have the required AA to use this spell
  slot." So slot 8 needs rank 1.
- `common/ruletypes.h:534`: `RULE_BOOL(Spells, RequireMnemonicRetention, true,
  "Enabling will require spell slots 9-12 to have the appropriate Mnemonic
  Retention AA learned.")`. **The default is ON.** The live server's
  `rule_values` row was NOT read, because T478 holds the lock and this leg must
  not connect. No local SQL or config file under `/mnt/projects/akk-stack`
  overrides it (grep over `*.sql *.json *.ini *.yml *.env *.conf`).
- `zone/aa.h:467`: `aaMnemonicRetention = 1071`.
- `utils/sql/peq_aa_tables.sql`, `altadv_vars` row 1071, decoded against that
  file's `CREATE TABLE` column order: `name='Mnemonic Retention'`, `cost=3`,
  `max_level=1`, `classes=64892`, `class_type=55`, `aa_expansion=8`. The class
  bitmask 64892 = 0xFD7C sets bits 2-6, 8 and 10-15. That is every class except
  Warrior (1), Monk (7) and Rogue (9). `class_type` is the minimum level, 55.
  The number 8 is quoted as it appears; I did not map it to an expansion name.
- Memorising is NOT gated. `Client::MemorizeSpell` (`client.cpp:3346-3404`)
  bounds only `slot < SpellbookSize` and the spell id, and `zonedb.cpp:601-606`
  loads a slot-8 row from the database with no AA check. A character without
  the AA can therefore hold a spell in gem 9 but cannot cast it.

**fable tracks none of this.**
`PlayerComponent::ZoneProcessRespondAA` and `ZoneProcessSendAAStats`
(`src/client/player/player_info.cpp:67-73`, `:94-97`) only log. The
PlayerProfile parse (`src/client/zone/zone_entry.cpp:630-672`) reads no
`aa_array`. No AA rank exists anywhere in the client state (grep `aa_array`,
`Mnemonic`, `1071` over `src/` and `include/`).

**The oracle UI does not show a ninth gem.** The oracle client loads
`UISkin=Default` (`/mnt/projects/EverQuestP1999/eqclient.ini:77`, and every
per-host `eqclient-*.ini`). Its `uifiles/default/EQUI_CastSpellWnd.xml` lays
out `CSPW_Spell0..7` at X=3, Y=10+30i, 36x28, in a `CastSpellWnd` of 48x290,
with `CSPW_SpellBook` at (10,252). `CSPW_Spell8` IS in the `<Pieces>` list, but
its only geometry is `<Location><X>-5000</X><Y>-5000</Y></Location>` with no
`<Size>` and no draw template (lines 152-158). The shipped skin parks it off
screen. The plan's lead ("the oracle UI's CSPW_Spell0..CSPW_Spell8") and board
line ~5121 quoted the name list, not the geometry.
`uifiles/default_old/EQUI_CastSpellWnd.xml:157-166` does place it at (1,264),
40x32, but the oracle does not load that skin. Both files have the same mtime
(2026-02-11 20:40), so the dates cannot tell which one is newer.

So the plan's second STOP fired. The gate exists, fable cannot evaluate it, and
"place the 9th gem the way the oracle XML does" means not drawing it. Showing a
castable ninth gem would offer a cast that the default server refuses for every
character without the AA. Faking one would invent a gate, which the dispatch
forbids.

### 2. Census of every 8 tied to gems (for whichever decision B770 gets)

| # | site | what |
|---|---|---|
| 1 | `include/client/spell/spell_constants.h:19-20` | `EQ::MAX_SPELL_GEMS = 8`, commented "Titanium client supports 9". The root constant |
| 2 | `include/client/spell/spell_manager.h:383-389` | four `std::array<..., MAX_SPELL_GEMS>` (gems, states, cooldown start, duration) |
| 3 | `src/client/spell/spell_gems.cpp` :32 :94 :122 :154 :162 :170 :187 :233 :310 :323 :363; `spell_manager.cpp` :77 :120 :171 :276 :351 :435; `spell_publish.cpp:117`; `spell_packet_handlers.cpp` :286 :369 :393 :424 | bounds and loops on `MAX_SPELL_GEMS` (they follow #1) |
| 4 | `include/client/state/spell_state.h:15` | a SECOND constant, `eqt::state::SPELL_GEM_COUNT = 8`, sizing `m_gems` (:258); used 10x in `src/client/state/spell_state.cpp` |
| 5 | `src/client/zone/zone_entry.cpp:635`, `:659` | a THIRD, local `constexpr SPELL_GEM_COUNT = 8` and `std::array<uint32_t, 8>`: the PlayerProfile read drops `mem_spells[8]` |
| 6 | `src/client/zone/zone_dispatch.cpp:600`, `:617` | `OP_MemorizeSpell` arms `slot < EQ::MAX_SPELL_GEMS` (T482's scribing-3 arm too, via `handleSpellBarRefresh` :424) |
| 7 | `src/client/core/everquest.cpp:642`; `src/client/bridge/bridge_component_intents.cpp:659` | the gem snapshot publish, and the hotbar-spell search loop |
| 8 | `include/client/graphics/ui/static_panels.h:268-269` | `SpellGemPanelState::GEM_COUNT = 8`, `gems[8]` (a FOURTH constant) |
| 9 | `src/client/graphics/ui/static_panels.cpp:507` | `gemLabels[8] = {"A1".."A8"}` |
| 10 | `include/client/graphics/ui/ui_layout.h:112` | `gemsH = SLOT_SIZE * 8 + MARGIN * 7` (a literal, not `GEM_COUNT`) |
| 11 | `src/client/graphics/ui/static_ui_input.cpp:98-102` | hit test loops `GEM_COUNT` |
| 12 | `src/client/input/graphics_input_handler.cpp:175` | `gem >= 0 && gem < 8` |
| 13 | `src/client/input/console_input_handler.cpp:460` | stdin `cast` `gem >= 1 && gem <= 8` |
| 14 | `src/client/action/command_processor_spells_gm.cpp` :40 :46 :48-49 :60 :64 :66-67 :74 :80 :82-83 | `/cast`, `/mem`, `/forget` bounds and usage strings "1-8" |
| 15 | `src/client/action/action_dispatcher.cpp:497-501`; `action_dispatcher_social.cpp:212-218`, `:243-247` | dispatcher bounds `1..8` |
| 16 | `src/client/action/command_processor_registration.cpp:629` | help text "(1-8)" (G03 golden `command_registration_order_g03.txt` pins registration) |
| 17 | `include/client/input/hotkey_manager.h:139-141`; `src/client/input/hotkey_manager.cpp:192-199`, `:555-563`; `config/hotkeys.json:67-74` | `SpellGem1..8` actions, names and Alt+1..8 defaults |
| 18 | comments: `action_dispatcher.h:233,243,441`; `action_handler.h:115,220`; `input_handler.h:94`; `spell_manager.h:92`; `eq_action_handler*.cpp` :41 :84 :513; `renderer_frame_input.cpp:517` | "1-8" / "0-7" in prose |
| 19 | `include/client/combat.h:83-95`, `src/client/combat.cpp:56` | an unrelated `SpellSlot` enum with `MAX_SPELL_GEMS = 12` sizing `CombatManager::m_memorized_spells` (pinned by `test_combat.cpp:51,55`). A different namespace from #1, and a 12 rather than an 8 |
| 20 | tests: `test_spell_manager.cpp` :134-183 (`gems[MAX_SPELL_GEMS]` with 8 initialisers at :182), :755; `test_spell_manager_timing.cpp:53-54`; `test_memorize_into_occupied_gem.cpp:72-75,135`; `test_spellbook_memorize.cpp:79-303`; `test_spell_gem_key_race.cpp:98` (literal 8); `test_state_modules.cpp:727-770` | out-of-range probes at index 8, which would move with any change |
| 21 | `include/client/graphics/ui/CLAUDE.md` table ("8 gems") and Key Bindings ("Alt+1-8") | docs |

The UI atlas has no gem-count-shaped sprite: the gem uses `SlotBackground`.
Nothing in `persistence` stores gems, because the server's PlayerProfile does.

### 3-4. Tests, fix, layout

Not done, by the STOP. No failing test was written, no constant moved, and no
capture was made. Nothing exists under `review/T483/`.

### Goldens

Untouched. No source file changed, so no G golden or image golden could move.

### Verification

| step | result |
|---|---|
| 1-6 (build, ctest, gles2, visual gate, ARM, file sizes) | **NOT RUN.** The diff is two Markdown files (this Review and `refactor/BOARD.md`) |

### Deviations

1. STOPped before tasks 3-4, per the plan's STOP list.
2. The plan's premise about the oracle XML was wrong. `CSPW_Spell8` exists in
   the shipped default skin but is parked at (-5000,-5000) with no size.
3. The server rule's live value is unread (lock held by T478). The finding
   rests on the eqemu default plus the absence of any local override.

### Board

- **B762** narrowed. The count is 9 on the wire, but the ninth gem is AA-gated
  and hidden in the oracle skin.
- **B770** (C) added: Will's decision on what fable does with gem 9.

---

## Resumed: B770 decided as option (a)

**The decision is the orchestrator's, recorded 2026-09-14, and Will may veto
it.** The standing rule is that the official client is the golden, and its
Default skin shows eight gems. So fable keeps 8 visible gems and 8 cast and
hotkey slots, stores all 9 memorised spells, never offers the ninth, and parses
no AA ranks. The branch was rebased onto `bb248847` first; the rebase was clean.

**DocCitations fix.** The first docs commit cited `zone/client_packet.cpp:4387`
and `common/ruletypes.h:534` on the board as paths relative to this repo, so
the checker failed them (2 FAIL). Both are now absolute
`/mnt/projects/akk-stack/code/...` paths, which the checker skips as
cross-project citations. After the fix it reports 1,378 citations, 0 fail.

### The one source of truth

`include/common/packets/common.h` holds both counts, each with its citation:
- `EQT::SPELL_GEM_COUNT = 9`, the protocol count. It already existed, pinned by
  `test_packet_structs.cpp:134`. Sources: `titanium_limits.h:308-328` and
  `titanium_structs.h:854`.
- `EQT::VISIBLE_SPELL_GEM_COUNT = 8`, new. Source: the Default skin's
  `CSPW_Spell0..7` at X=3, Y=10+30i, 36x28, in a 48x290 window, with
  `CSPW_Spell8` parked at (-5000,-5000) (`EQUI_CastSpellWnd.xml:32-158`).

Four constants were deleted: `EQ::MAX_SPELL_GEMS`,
`eqt::state::SPELL_GEM_COUNT`, `SpellGemPanelState::GEM_COUNT` (plus
`gems[8]`), and the local `SPELL_GEM_COUNT = 8` in `zone_entry.cpp`. So was the
literal 8 in `ui_layout.h:112`. One trap was checked by census: `combat.h`
declares a GLOBAL `SpellSlot` enum with `MAX_SPELL_GEMS = 12`. An unqualified
leftover inside `namespace EQ` would have silently bound to 12. After the patch,
`grep -w MAX_SPELL_GEMS` over `src include tests tools benchmarks docker` finds
only `combat.h`, `combat.cpp` and `test_combat.cpp`, and nothing else mentions
`GEM_COUNT` or an unqualified `SPELL_GEM_COUNT`.

| count | sites |
|---|---|
| **stored, 9** | `SpellManager` arrays (`spell_manager.h`); `getMemorizedSpell`, `getGemState`, cooldown progress/remaining, `setSpellGems`, `applyServerGemUpdate`, `startGemCooldown` (`spell_gems.cpp`); gem-reset and cooldown loops (`spell_manager.cpp` x4, `spell_packet_handlers.cpp` x4, `spell_publish.cpp`); `SpellState::m_gems` and its 10 bounds; the PlayerProfile read (`zone_entry.cpp`, now `kGems = EQT::SPELL_GEM_COUNT`); both `OP_MemorizeSpell` gem arms (`zone_dispatch.cpp:600,617`); the zone-in gem snapshot (`everquest.cpp:642`) |
| **shown / offered, 8** | `beginCastFromGem`, `beginCastById`'s gem lookup (`findGemSlotForSpell`), `memorizeSpell`, `forgetSpell`, `swapGems`; the hotbar spell search (`bridge_component_intents.cpp:659`); `/cast` `/mem` `/forget` (`command_processor_spells_gm.cpp` x3); dispatcher bounds (`action_dispatcher.cpp`, `action_dispatcher_social.cpp` x2); Alt+N (`graphics_input_handler.cpp`) and stdin `cast` (`console_input_handler.cpp`); `SpellGemPanelState::gems[]`, `gemLabels[]`, `resolveMemorizeGem`, the panel loop (`static_panels.cpp`), the hit test (`static_ui_input.cpp`), `gemsH` (`ui_layout.h`); the bridge's panel filter (`irrlicht_bridge.cpp:671`), where a `SpellGemChanged` for slot 8 is published but not drawn |

The hotkey table (`SpellGem1..8`, Alt+1..8) is already eight and unchanged.
User-facing text ("Gem slot must be 1-8") is unchanged. Nothing moved on
screen: `gemsH` is still 8x32 + 7x4 = 284 px, the 800x600 rect is still
{764,268,796,552}, and at 1280x720 it is {1244,388,1276,672}. The panel emits
eight 32x32 slots at x = `spellGems.X`, y = top + 36i, for i = 0..7.

### Tests (literal 8 and 9, so a drifting constant is a red test)

`tests/test_ninth_spell_gem.cpp` (new target `test_ninth_spell_gem`,
`tests/cmake/spells.cmake`) and one case in
`tests/test_command_processor_migration.cpp`:

| test | pre-fix (source at `bb248847`, tests added) | post-fix |
|---|---|---|
| `NinthSpellGem.PlayerProfileKeepsTheNinthMemorisedSpell` (a 9-spell profile keeps spell 9) | **FAIL**: `getMemorizedSpell(8)` = 4294967295, expected 1008 ("mem_spells[8] was dropped at the PlayerProfile read") | pass |
| `NinthSpellGem.MemorizeSpellForSlotEightLandsInState` (lands in SpellManager AND GameState; the un-memorise empties it) | **FAIL**: 4294967295 vs 2345 in both stores ("dropped at the zone dispatcher") | pass |
| `NinthSpellGem.TheNinthGemIsNeverCastFromTheClient` (cast/memorize/forget/swap at 8 refused; the stored spell survives) | **FAIL**, but only its last line, because nothing was stored to survive. The refusals already held | pass |
| `NinthSpellGem.ThePanelHoldsEightGems` | pass (guard) | pass |
| `NinthSpellGem.PanelEmitsExactlyEightOnScreenSlotsAt800x600` | pass (guard) | pass |
| `NinthSpellGem.PanelEmitsExactlyEightOnScreenSlotsAt1280x720` | pass (guard) | pass |
| `CommandProcessorTest.Cast_NinthGem_RefusedClientSideWithoutDispatching` (`/cast 9` -> "Gem slot must be 1-8", handler never called) | pass (guard) | pass |

**Four of the seven are guards, not red-first tests, and this is flagged as a
deviation from "every test fails first".** Before the fix the ninth gem could
be neither stored nor offered, so "still refused" and "still eight on screen"
were already true. They exist to catch the storage growing into the offer path,
and the three red tests prove that growth happened. The panel tests count the
quads `renderSpellGemPanel` queues on the `EDT_NULL` driver (no atlas, so every
sprite is a solid quad in `UIRenderer::atlasQuads_`). Each slot is asserted on
screen, above the hotbar, and inside `layout.spellGems`.

Existing tests updated to the split, in place (G-correction 32 style):
- `test_spell_manager.cpp`: the offer-side probes use the visible count, the
  read-side probes use the stored count, and the `setSpellGems` array has 9
  entries.
- `test_spell_manager_timing.cpp`, `test_memorize_into_occupied_gem.cpp` and
  `test_state_modules.cpp` use the stored count.
- `test_spellbook_memorize.cpp` uses the visible count.

No assertion was weakened: each probe still names the first out-of-range index
on its own side.

### Goldens

- G goldens: `zone_opcodes_g01.txt`, `command_registration_order_g03.txt` and
  `frame_render_order_g05.txt` are unchanged, and no golden file contains any
  gem-count text (grepped `tests/goldens/`). The zone dispatch table gained and
  lost no opcode; the change sits inside existing `On*` bodies. The pins for
  `bridge_component_intents` (`BridgeIntentTable`, `ReachabilitySweep`,
  `OptionsMerchantResidue`) all passed in the full run.
- Image goldens: see step 4.

### Media

No `review/T483/` capture was made, because option (a) changes no pixel. The
panel geometry is identical before and after (the numbers above), so a
before/after pair would be two identical images. This is a deviation from plan
task 4, which was written for a ninth VISIBLE gem.

### Verification

| step | result |
|---|---|
| 1. `cmake --build slot3/build -j24` | **PASS**, exit 0 (backgrounded; no 143) |
| 2. `ctest -j6` | **PASS**, 100% of 4,811, 0 failed. Includes `NinthSpellGem.*`, `UIAtlasPixelTest.*`, `ZoneDispatchTable.*`, `CommandRegistrationOrderTest.*`, `FrameRenderOrder.*`, `DocCitations`, `DocClaims`, `DocCounts`, `CommentClaims`, `FileSizeCeiling`, `ZeroCallers` |
| 3. `build-gles2-x86.sh` | **PASS**, exit 0 |
| 4. visual gate | **PASS** (`=== VISUAL GATE: PASS ===`, run against the freshly rebuilt `build-gles2`); `git status -- tests/goldens/` is empty, so no image golden moved |
| 5. `build-arm-noble.sh` | **PASS**, exit 0 |
| 6. `check_file_sizes.sh fable` | **PASS**, 29 checked, 0 warn, 0 fail |

### Board (resumed)

- **B770**: the decision is recorded as the orchestrator's with Will's veto
  open, and the entry stays open for that veto. Still owed live, under a lock:
  the `rule_values` read, and a character whose profile holds a ninth spell
  keeping it across a zone.
- **B762**: its citation is fixed; the substance is unchanged.

### Deviations (resumed)

1. Four of the seven new tests are guards that were green before the fix (see
   Tests).
2. No before/after captures, because no pixel moved (see Media).
3. The hotbar spell search, `beginCastById`'s gem lookup and the input handlers
   were narrowed to the visible count as well. They are "offer" paths, so this
   follows from rule 1 of the decision, and none of them could reach gem 9
   before.
