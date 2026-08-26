# Deferred / Known-Open Items

Live list of open work items extracted from "done" batches and audits
(2026-07-22 doc reconciliation). **This file is a research record, not a TODO
list — when an item is fixed, mark it closed in place (re-title the heading,
add the evidence: a commit hash, a file:line, or a named test) and leave the
body intact. Never delete a section.** Its history has repeatedly been
load-bearing for later units (T46 resolved a conflict between two units by
consulting T43's classification table). Sources named inside entries (e.g.
`arch_design_update.md`, `audit_results_post.md`) are historical docs — do not
edit those.

**T53 (2026-08-05) hygiene pass**: walked every section against the tree at
`607a19c3`, re-titled entries with grep/git-log evidence, and added the index
below. Full verdict table, before/after open-item counts, and everything that
could not be settled cheaply are in `refactor/T53_deferred_board_hygiene.md`'s
Review — read that before scoping the next unit off this file.

## STATUS 2026-08-25 (T215 tracker refresh)

- **T264 `344e6eac` REACHABILITY SWEEP (2026-08-26): 54 intents / 102 events / 165 handler methods / 77 senders audited; 19 intents, 6 events, 18 handlers DELETED (-1125 lines); `tests/test_reachability_sweep.cpp` parses the sources every ctest and pins the sets. EIGHT features exist but cannot be reached (class A, one unit each, kept hops named in the T264 Review):**
  - ~~**F1 Loot window: take one item / loot all / destroy / close**~~ -- **CLOSED by T268**: `/loot <slot|item name>`, `/lootall`, `/lootclose`; `EverQuest::LootAllFromCorpse` deleted (CombatManager's T26 machine kept), `DestroyAllLootIntent` deleted (no Titanium destroy-loot packet). The three loot intents stay producer-less as the UI-gesture leg -- there is no loot popup (T48 class). T268 also fixed `EntityManager::markAsCorpse` having no caller, which made GameState keep a corpse's LIVING name (broke `/target <corpse>` too).
  - ~~F2 Read a book/note item~~ **CLOSED T273 `70cf280f`** (`/read <slot|name>`; text surfaced in the chat window -- `showNoteWindow()` was a LOG_INFO stub and there is no book panel, so it stays the hook for one). **The sender was already byte-correct (T149/T150 check clean); the real defects were the missing producer, the stub display, and a MIS-MAPPED ITEM FIELD TRIPLE** -- `SerializeItem` emits Book(100)/BookType(101)/Filename(102); the parser read 100 as the type, dropped 101 and stored 102 in a member called `bookText`. `/read` gates on Filename, not the Book flag (item 15005 'Tattered Note' has book=0). ⚠ Server note: `Client::ReadBook` GARBLES text when the reader's language skill is low and Testchar had NO `character_languages` rows -- `#set language 0 100` before any book/lore probe.
  - ~~F3 Bank coin deposit / withdraw / convert~~ CLOSED T269 `c97c995f` (live DB proof; convert is any-pair ratio arithmetic, NOT 10:1 only; the server sends NO echo on MoveCoin so the client applies the server’s own arithmetic and refuses anything it would clamp)
  - ~~F4 Spellbook popup click -> memorize~~ **CLOSED T272 `bc758c4a`** (real xdotool click live, twice; while the book is open a gem click picks the memorize destination -- the popup tracked no drag state). **Underneath it the consumer arm called `memorizeSpell(i.gemSlot, i.spellId)` against `memorizeSpell(spell_id, gem_slot)` -- ARGUMENTS SWAPPED; it had never executed because nothing produced the intent.** `cmdSpellbook`'s stale message fixed. **NEW (T272), for Will: nothing `UIRenderer::drawRect()` draws in the gem panel is visible in a capture -- including the Ready-state green borders that shipped long before T272. **CLOSED T275 `ad51cc3f` -- and it was far bigger than a marker: THE WHOLE STATIC UI ATLAS SAMPLED OPAQUE BLACK.** (a) `ConstrainedTextureCache::uploadCompressedETC1/V2` set `GL_LINEAR_MIPMAP_LINEAR` whenever mipLevels>1, but an `eqe1_builder` ETC1 chain stops at 4x4 (7 levels for the 256x256 ui_atlas where GLES2 needs 9 for MIPMAP COMPLETENESS) and nothing called glGenerateMipmap -> incomplete texture samples (0,0,0,1). The 3D path is immune because the driver overwrites unit 0's MIN_FILTER per material bind; the 2D `draw2DImage` path (the entire static UI) does not. P42 fixed exactly this for the RGBA path in the same file and missed these two. (b) `UIRenderer::endFrame()` passed `nullptr` for draw2DImage's `colors`, discarding every per-quad tint. Why it survived: every UI capture harness in tree history (T54/T57/T60/T62) used the FALLBACK path (`ui.init(driver, nullptr, ...)`) the real client never takes -- the atlas path had ZERO pixel coverage. Now covered by `tests/test_ui_atlas_pixels.cpp` (real GLES2 device + real willeq.eqe1). **TWO DECISIONS FOR WILL (class C):** (1) the ETC1 ALPHA companion has the same latent defect (`GL_NEAREST_MIPMAP_NEAREST` over the same truncated chain, unit 1's filters never set by the driver) -- whatever it samples today is BAKED INTO THE BLESSED GOLDENS, so fixing it is a re-bless; (2) `verify_eqe1` reports `mipLevels=7 (expected 9)` for EVERY entry of EVERY pack -- fix the builder to emit complete chains, or keep non-mipmap filters forever. ⚠ Harness fact: on surfaceless-EGL/llvmpipe a frame containing ONLY 2D colour quads never reaches the framebuffer; add one font draw and the identical quads appear.**
  - ~~F5 Scribe a scroll~~ **CLOSED T276 `bc415bef`** (`/scribe <slot|name>`, live: scroll consumed, `character_spells` gained the rows, spellbook popup lists them, `/mem`+`/cast` works). Wire truth: same `OP_MemorizeSpell` but **`scribing = 0`, not 1** (`zone/client.h:102`; T272's `scribing=1` was the MEMORIZE echo), and the server reads the scroll off the CURSOR requiring `Scroll.Effect == spell_id`. **The arm could never have worked: it assumed `InventoryManager::setSpellForScribe()` had already moved the scroll to the cursor -- that function had ZERO callers, so every packet would have drawn the server's "Scribing a spell without an Item Instance on your cursor?".** Three more: `bookSlot` was `uint8_t` against a 400-slot book; the spent scroll was never cleared from the client cursor (server's delete lands in a no-op handler) so a second `/scribe` was blocked; the class/level refusal read duplicate `m_class`/`m_level` instead of `GetClass()`/`GetLevel()`. No mouse gesture: `SlotDisplayInfo` carries no item id or scroll effect, so a click cannot tell a scroll from a sword (UI-gesture leg stays open, T48 class).
  - ~~F6 Click off a buff~~ **CLOSED T277 `d43ac0bb`** (`/removebuff <slot|name>` + a real buff-bar hit-test -- there was NO hit-test at all, and the bar PACKS so the on-screen column never equalled the slot; live both directions twice). Wire truth: **Titanium has no `OP_BuffRemoveRequest`** (that is Underfoot+); it uses `OP_Buff` (0x6a53) and `Handle_OP_Buff` reads only `spellid`. A REFUSAL echoes our own bytes back, so `bufffade` must be 0 on the request or the echo replays as a fade and deletes a buff the server just refused. Titanium does not honour `no_remove`. **Two defects: (1) the consumer arm removed the buff LOCALLY and optimistically with `target_id 0` and sent nothing -- never executed because nothing produced the intent (the T272 class again). (2) THE BUFF BAR COULD NEVER FILL: eqemu only sends OP_Buff on a duration change or fade, and the PlayerProfile's 25 slots reached BuffManager and stopped -- nothing published `BuffUpdated`, so the bar was empty every session, always.** Fixed; the `OP_Action` mid-session-cast leg is reported, not fixed.
  - F7 Manual tradeskill combine + close container
  - F8 Train a skill at a trainer / close trainer (`/train` only opens)
  - Not swept: InputAction/hotkey axis; `IOutputRenderer::updateCurrentTargetHP` family is caller-less.

~~- **NEW (T227 STOP note): `CombatManager::Update()` on `m_update_thread` → CheckForAggro/ProcessCombatRound/FindNextHuntTarget → `SetTarget`/`ClearTarget` → `combat()` writes OFF the game thread, gated on `/combat on`.** Pre-existing off-thread write class (the same reason UpdateMovement was moved off that thread, eq.cpp ~3766). Class A: route those target changes through the game thread (intent/queue) or move Update() itself. `review/T227/`.~~ **CLOSED T245: `m_update_thread` DELETED; `CombatManager::Update()` is ticked from `UpdateMovement()` on the game thread, self-paced to `kUpdateIntervalMs` (500 ms). TSAN BEFORE (`/hunt on`, three beetles) vs AFTER in `review/T245/`. Note found on the way: `CombatTarget::is_aggro` is hard-coded `false` (TODO in `GetNearbyHostiles`), so `CheckForAggro` can never fire -- the only reachable off-thread writer was the hunt path.**
- **T230 MERGED `660449be`: `CombatManager::SetTarget(name)` chain DELETED (zero production reach); keyboard hotbar Item slots fire via `useItemById`; `EqActionHandler::useItem` was a STUB, fixed. ⚠ REACHABILITY PATTERN #12 found LIVE: the ENTIRE T174 keyboard hotbar route (Spell/Skill/Item) was dead — `IrrlichtRenderer::processChatInput()` (render thread, `renderer_frame.cpp`) called `getHotbarActivationRequest()` (an exchange) and DISCARDED it every frame, starving `GraphicsInputHandler::consumeHotbarRequest()`. Two lines deleted; proven by DB item charges 5→3 per key press. T174 had never been probed live. **Emote/Macro/Combat CLOSED T239 `422ce591`** (one shared `ActivateHotbarCommandSlot`, macro proven on the observer, combat toggle live).**
- **T234 `bf448c0a` + T237 `e2092db3`: five EqActionHandler transforms pinned against the real client (21 tests, 8/8 deliberate breaks caught); movementMode/positionState now written into GameState at the source. Flagged, not done: server-FORCED self position changes (stun, AT_DIE, GM) have no non-sending internal setter — class A, small.**
- ~~**NEW (T239): `/emote <text>` (cmdEmote → OP_ChannelMessage channel 22) produced NO chat on a second client and no self-echo; `/wave` (OP_Animation) is not logged by a headless observer either. Titanium's official client sends OP_Emote (a different struct) — the `/emote` command's wire behaviour is a pre-existing open question. Class B: capture what the official client sends (eq-oracle) or read eqemu's Handle_OP_Emote, then fix cmdEmote. `review/T239/`.**~~ CLOSED T243 `8196394c` — AND two larger defects: `HC_OP_Emote` was OP_Animation's number (inbound text emotes never dispatched; renamed `HC_OP_Animation`, real `OP_Emote=0x547a` added), and **`ZoneSendSetServerFilter` sent 0xFFFFFFFF×29 which eqemu reads as HIDE for every filtered class** (socials, guild/group/shout/OOC/auction, others' hits, pet hits, DoTs, crits — now show-all like the official default; per-class defaults are a one-line table if chat volume is a problem). Observer-proven.
- **T245 leads:** ~~`CombatTarget::is_aggro` hard-coded false → no auto-defence~~ CLOSED T247 `451cecbf` (aggro inferred from OP_Damage/OP_Action on self, 10 s memory; `CheckForAggro` engages in ~1 s live; OP_Stun was decoding the 4-byte ms duration as two halves — fixed, cast refused while stunned, timed stuns expire client-side because eqemu never sends the unstun). ~~Open: stun-vs-movement; first-aggro-in-scan~~ CLOSED T254 `26c6394b` (server ACCEPTS movement while stunned -- client_packet.cpp:4831, cheat_manager.cpp:277 exempts stunned -- so no gate; latest attacker engaged, ties nearest, live two-beetle proof); befallen holds dozens of leftover GM-spawned `a_scaled_wolf_pup` at Testchar's home spot (server hygiene: `#depopzone`/`#repop` before hunt probes). **T248 `e4a73c9e` CLOSED the MidiPlayer/miniaudio teardown race — it was a CRASH class (heap-use-after-free + SEGV in `MidiPlayer::render` under TSAN, 139 reports → 0) fixed by a lock-free `RenderBarrier` epoch handshake on stop/seekTo/freeChannel; miniaudio log callback wired (T128 item closed).**
- ~~**NEW (T240): `beginCastFromGem()`'s result switch collapses OutOfRange / NoLineOfSight / InvalidTarget into "Cannot cast spell" — the official client names each. Class A, small (eq_strings ids exist).**~~ CLOSED T244 `25695e62` (one table `cast_refusal.h`; a SECOND drifted copy in eq_bridge.cpp's CastSpellIntent arm also folded). ~~⚠ `checkPlayerState/checkLineOfSight/checkReagents` are `return true` STUBS~~ CLOSED T246 `408cd295`: buff-based stun/mez/silence/fear, reagents (willeq.db had NO reagent columns — schema v4, 383 reagent rows), LoS via `HCMap::CheckLOS`. Still permissive: ~~instantaneous `OP_Stun`~~ (CLOSED T247), `noexpend_reagent_id`, count-0 components; ~~eqstr 433 per-item form not emitted~~ CLOSED T251 `a252a7a4` (name from the ItemPacket cache; unseen reagent → 272). ~~NEW (T251): a THIRD refusal site — the hotbar Spell arm~~ CLOSED T253 `7e1c7c0f` (no fourth site); `InventoryManager::getItemById()` returns `false` not `nullptr` on miss. Class A, tiny.
- **NEW (T240): `/target <substring>` returns the LOWEST spawn id, not the nearest match. Class A: rank by distance among matches (T173's resolver).**
- ~~**NEW (T235): spell-gem KEYS still do not cast — `EqActionHandler::castSpell()` → `CombatManager::CastSpell()` reads `CombatManager::m_spell_gems`, a SECOND gem map written only by `CombatManager::MemorizeSpell()` (never by the profile load), while the real gems live in `EQ::SpellManager` (`/gems` lists them). Same class as T101's `interruptCast` repoint. Class A: repoint castSpell/castSpellOnTarget at SpellManager, delete `m_spell_gems` and the hardcoded cast-time stub (`combat.cpp:681-732`, TODO). `review/T235/`.**~~ CLOSED T240 `84a22da9` — live: Alt+1/Alt+2//cast 1 → 4/4 server BeginCast; CombatManager cast stub + gem map DELETED.
- **NEW (T235): `Application::connectRendererCallbacks()` is UNREACHABLE (nothing derives from `output::GraphicalRenderer`; its dynamic_cast always fails) — target/door/chat-submit callbacks and `IrrlichtRenderer`'s parallel setters all dead; live routes are bridge intents. Reachability pattern #13. Class A: delete.**
- **NEW (T235): `enterKeyPressed`/`slashKeyPressed` and `popKeyEvent`/`clearPendingKeyEvents` are two-thread pairs whose GAME-thread consumers have no production caller (dead second readers). Class A: delete the dead game-thread side (also shrinks T225's vector-race inventory).** **CLOSED (T241): game-thread readers deleted (Enter, slash AND Escape -- the Escape one was a real raced-and-lost against the render thread's target-clear); `IInputHandler` text-input trio and `InputAction::OpenChat/OpenChatSlash/CloseChat` gone; `Application::connectRendererCallbacks()` and all 15 unreachable callback setters deleted. `pendingKeyEvents_` and the three chat flags are now render-thread-only (plain bool).**
- ~~**NEW (T230): same two-consumer race for spell-gem keys — `processChatInput()` still consumes `getSpellGemCastRequest()` while `GraphicsInputHandler::consumeSpellCastRequest()` also polls it. Class A, one-line delete + live keypress proof (T230's Xvfb+xdotool driver in `review/T230/`).**~~ CLOSED T235 `b926a306` (render-thread swallow removed, dead callback plumbing deleted; before/after live).
- ~~**NEW (T230): `/equip 30 22` (swap with CURSOR) reports a client-side swap the server never applied~~ **RETRACTED by T236 `1e1972d3`: the swap HAD applied on both sides (T230 misread — the meal was queued behind the cursor). T236 instead CLOSED T82's real sibling: `moveItem()` slot↔slot swaps now pre-flight the displaced item via `checkEquipDestination`, refuse locally, send nothing (live: DB untouched on refusal). ~~Open divergence: `placeItem()` validates against `cursorSourceSlot_`~~ CLOSED T249 `ee0e1479` (server rule matched; live 2 packets, 0 desync). ~~NEW (T249): after a cursor-source swap `cursorSourceSlot_` still names the original pickup slot~~ CLOSED T250 `4ed1bf17` (`returnCursorItem` deleted — eqemu has no counterpart). ~~NEW (T250): swapping two bags that BOTH have contents collides client-side~~ CLOSED T252 `79bd1fef` (server swaps both with contents — mirrored via lift-then-put; cursor bag range 331-340; the direct `/equip 22 23` bag move carried NO contents before — fixed; live DB matches). ~~Open (T252): general-BANK bag moves / front-cursor-only contents~~ CLOSED T255 `f7568144` (bank content range 6210+ server / 2031+ wire verified live; queued cursor bags keep contents). Two more defects found live and fixed: cursor bag contents were NEVER parsed at zone-in (slot 33 not mapped -> SLOT_INVALID), and BANK bags always loaded EMPTY (eqemu sends them as OP_ItemPacket with sub-items; `processItemPacket` never parsed them). ~~Open (T255): trade-window parents are not bag parents~~ CLOSED T259 `987ddc6d` (wire 3031+; completion/cancel semantics matched from zone/trading.cpp; live 3 runs). Four latent defects fixed on the way: `TradeManager::setInventoryManager()` had no caller; **`TradeAcceptIntent` had NO producer -- no willeq client could ever accept a trade** (`/accept` during a trade now clicks it; reachability #15); no OP_CancelTrade echo (partner's items stranded server-side); `/equip 22 3000` got the player KICKED (non-cursor trade move) -- now refused locally. Stray row 6011: T263 hygiene.** Original: — client/server inventory drift on swap-with-cursor moves. Same class as T82's "doMove swap branch never validates the displaced item". Class A/B: reproduce with the DB, then fix the swap validation.**
- ~~**NEW (T233): a `--script` command HELD pre-connection emits no `CMD outcome=held` line.**~~ CLOSED T238 `9c2f6bb4` (one held line per command, T30 vocabulary).
- **T233 MERGED `78e041ad`: all ~68 renderer replies go through ONE lambda in `processSlashCommand`; it now LOG_INFOs. T142 CLOSED.**
- ~~**NEW (T229): same-zone summon does not move the summoned WillEQ client.**~~ CLOSED T231 `ebc7f007` (relocate locally, forced position update; live: spawn id unchanged, observer saw the move). eqemu sends `OP_RequestClientZoneChange` for the CURRENT zone and expects NO `OP_ZoneChange` back (akk-stack `zoning.cpp` ~1060); `ZoneProcessRequestClientZoneChange` (`eq_zone.cpp:39`) replies unconditionally → server `SendZoneCancel` → full teardown/reconnect, lands where it started. T148's "server relocated B" was the ZonePC log line, not an observed move. Class A: on a same-zone request, relocate locally (set position, no zone cycle). `review/T229/`.
- **T224 `c94d2eba`: `/who` prints entries (world-sender layout verified live); `/who all` filter bug and GM `%9` formatting fixed. ~~Owed: anon/linkdead/guild lines exercised by unit tests only~~ CLOSED T256 `ace77a65` (all formats verified live with a real non-GM viewer; linkdead is unreachable on eqemu-world for Titanium, `clientlist.cpp:854` hardcodes it). Two fixes: phantom `[0 Class0]` line when the GM viewer is itself anon; **EVERY SQLite-served string carried a trailing CR** (14k rows; the text loader stripped it, the SQLite getters did not) -- fixed in `willeq_database.cpp`. NEW (T256): two willeq processes opening `willeq.db` in the same second -> one fails 'missing metadata table' (concurrent open); CLOSED T257 `d92e1f98`: WAL-mode DB on NFS, readers raced WAL recovery (SQLITE_BUSY_RECOVERY 261) -- now opened immutable, zero sidecars; ALSO explains flaky willeq.db test SKIPs under ctest -j. ~~Follow-up: builder journal_mode=DELETE~~ CLOSED T258 `cdd1bc85` (shipped willeq.db is a plain rollback-journal file; plain readers create no sidecars). NEW (harness): `visual_gate`'s befallen p95 CPU-ms ceiling trips under parallel ctest (T219, T220, T239, T258 all saw it; alone it passes) -- class A: RUN_SERIAL / a resource lock on the gate test, or a wall-clock-insensitive assertion. ~~visual_gate under -j / zone_chars 2213 vs 2188~~ CLOSED T261 `ffa783b1` (gate is RUN_SERIAL -- its cpu_ms is wall-clock by construction; +26 s suite, accepted; 25 duplicate lines collapsed by the PK, none lost). NEW (T261): `WilleqDatabase::getZoneCharacters()` -- the ONLY reader of the `zone_characters` table -- has ZERO callers, and `source_file` is consumed by nothing (reachability pattern #14). ~~wire or delete~~ CLOSED T262 `a73b34f1`: DELETED (1864/2188 rows named codes no race can produce; 318/324 spawnable rows resolved without the hint). The 6 that needed it exposed races 195/196/198 (BWD/GDR/PRI dragons) with an EMPTY `s3d_file` in race_models.json -- fixed, db rebuilt. Dragon pixel check (T263): mesh LOADS from bwd_chr.s3d (4631 verts, row correct) but the client HARD-EXITS: `EQE1: 'bwdhe0005' exists in 9 archive(s), none a candidate for zone` -> FATAL. **NEW CRITICAL (T263): the EQE1 candidate set is frozen at zone load (T199 spawn list); any race that spawns LATER (roamer, GM spawn, pet, summoned) and is not already present -> `openArchiveContaining()` refuses the archive that holds the texture -> all-or-nothing FATAL exit. Violates rule R2.~~ **CLOSED T266 `865b6219`: late spawns extend the candidate set (T199 race->archive) and `EQE1TextureIndex::resolve()` late-opens the first indexed owner, tagged `late-open` in the request log; FATAL kept for names in NO archive. Live: dragon placeholder=false, 4631 verts, 0 magenta of 480000. Will judges `review/T266/after/dragon_race195_qeynos2_800x600.png`.** Server-hygiene facts (T263): this eqemu PERSISTS GM spawns across restarts in `zone_state_spawns` (spawn2_id=0) -- `#depopzone`+`#repop` clears; qeynos2 (13) and arena (19) still hold GM leftovers, ids in review/T263/. A client that exits without `/camp` gets its position re-saved at TERM, overwriting a restore done while the server is up.**
- ~~**NEW LEAD (T220 live screenshot, 2026-08-25): a magenta patch in qeynos2 CEILING geometry**~~ **CLOSED T226 `046b061f` — NOT a defect: T220's driver ran WITHOUT `--eqe1-path`; on the S3D/BMP path qeynos2's ~230-texture working set exceeds the 64MB constrained budget, the LRU evicts textures the live zone still references, and `RegionMeshTextureGuard` paints the placeholder (29 evictions, 12,492 px). With EQE1: 0 magenta, 0 evictions. Eviction line is now WARN with the texture name. ⚠ RULE: every live x86 pixel run passes `--eqe1-path` (decision #2: EQE1 is the shipped path; S3D is dev-only and thrashes under the orangepi budget).** Original lead: (`review/T220/`). T211 fixed the 0x2C material-list guess for equipment; this is zone geometry — different loader path. Class B: reproduce with `--dump-frames-dir` at the same spot, then find which texture name misses (EQT_EQE1_REQUEST_LOG_DIR shows NOT-FOUND). Untriaged.
- ~~**T221 (stage 2) note: `GameState::tradeskill()` has NO writer on either instance and no bridge case**~~ **CLOSED T232: zero readers anywhere (src/include/tests) — the domain was deleted. The open tradeskill container lives in `EverQuest::m_active_tradeskill_object_id` (read by `EqActionHandler` via `GetActiveTradeskillObjectId()`), and `ZoneProcessClickObjectAction` already publishes `TradeskillContainerOpened/Closed` directly; recipes are send-only (`SendRecipes*`, eq_merchant.cpp). `refactor/T232_tradeskill_state.md`.**
- **NEW (T225, 2026-08-25): `RendererEventReceiver` still has `pendingKeyEvents_` (T241: no longer -- single-threaded since the game-thread `popKeyEvent`/`clearPendingKeyEvents` were deleted; only `bridgeQueue_` and the items below remain) / `bridgeQueue_` (std::vector pushed on the render thread, erased/swap-drained on BOTH threads — a push racing `erase(begin())` is heap corruption), `getMouseDeltaX/Y()` two-word RMW on both threads, and `keyWasPressed_[]`/`keyIsDown_[]`.** TSAN never sees them because `--script` injects no Irrlicht input; a real keyboard session would. Atomics cannot fix a vector; the right shape is one swap-drained queue behind the intent path — which requires deciding which thread owns chat key events. **Class C (thread-ownership decision), then a small unit.** `review/T225/`.
- ~~**T220 residual: 17 TSAN warnings, all T114 inventory #10~~ CLOSED T225 `b43106d4` (atomics + exchange, 17→0) (`RendererEventReceiver` read-and-reset key/mouse flags on game vs render thread).** Class A, own unit: make them atomics or route through the intent queue. `review/T220/tsan_after_*.txt`.

- **fable tip:** `e70e3347` (T214). **Suite:** 2746 registered, 2745 pass —
  sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host
  quirk). eqemu DOWN, lock RELEASED, no unit in flight.
- **Arcs closed since the T53 index:** action-layer migration (T97–T143),
  packet/wire verification (T144–T158), crash/memory tour (T105–T131), skill
  gate + corpse naming (T159–T162, T173, T185), raid (T169–T184), EQE1 /
  zone-load correctness (T186–T214).
- **Open items by class** (each anchored by a heading in the rebuilt Index
  below; every closure cites its commit):
  - **A — client correctness defects, unowned:** ~~`SendGMHideMe` misrouted~~ (CLOSED T217 `175c97ca`) /
    ~~`ZoneProcessGMFind` float decode / `SendGMSummon` zeroed coords (T148)~~ CLOSED T229 `9a06ecbd` (eqemu READS the summon coords — zeros really sent the target to 0,0,0; GMFind_Struct is x-first float);
    ~~`/reply` silent success with no pending sender (T100)~~ CLOSED T222 (live-verified two-client);
    ~~`rejectTradeRequest()` never fires `m_on_cancelled` (T103)~~ CLOSED T222;
    ~~`CombatManager::SetTarget(name)` bypasses T173's corpse ranking;
    keyboard hotbar Item/Emote slots skipped, not routed (T174)~~ CLOSED T230 (Emote/Macro still skipped);
    ~~`EqActionHandler` TRANSFORMS methods still untested (T93)~~ CLOSED T234; ~~blackburrow /
    crushbone blank benchmark render (Batch H)~~ CLOSED T242 `f32744c1` (benchmark camera seed 300 units in the air from sky-region Z; NO rendering bug; gfaydark/befallen were silently on the same fallback); "BSP region is SIZE_MAX" (T242: a client `CreateZoneLoadSnapshot` race, unrelated to the benchmark)
    race; pale fog shapes in goldens; `applyPaletteMask` plumbing gaps (P46d);
    ~~`ZoneProcessNewZone` never calls `CheckZoneRequestPhaseComplete` (T04,
    latent by causality)~~ CLOSED T228 `67a131bf`; race 330: T265 `0ab18c9e` audited all 187 race rows against the archives (4 wrong fixed: 17 Golem, 72 Ship, 330 Froglok s3d_file, 240 aliased; 3 classic ids added; 32 Ghost has no mesh anywhere). **330 STILL BLOCKED in src**: `race_codes.cpp getRaceModelFilename()` hardcodes 330->globalfroglok_chr (monster FRG) and `race_model_loader.cpp` does `stoi()` on non-numeric `global*` s3d_file names + the dir scan regex skips `global[a-z]+_chr` -- ~~two-line fix + pixel check~~ CLOSED T267 `2a75564d` (real 330 NPCs render: FRM 1169 / FRF 1159 verts, 0 magenta). NEW (T267): `getRaceCode()`'s DB-null fallback switch (and `getFallbackRaceCode`/`getZoneSpecificRaceCode`) is a wholesale PRE-JSON numbering contradicting T265's rows (17 ORC vs GOL, 35 RAT vs EEL, 72 ELE vs SHIP...) -- inert at runtime, 38 tests pin it. ~~Class A: delete-or-regenerate~~ CLOSED T270 `736f580e` -- and HALF THE SWITCH WAS NOT INERT: `getFallbackRaceCode()` fired whenever a row had no fallback_code (race 61 Shark rendered as a DARK ELF, 62 Tunare as an erudite), and `getZoneSpecificRaceCode()` ran UNCONDITIONALLY, overriding rows (a shark in a Neriak zone became a citizen) -- deleted; race codes are db-only, no row -> "" -> placeholder. Two tools (`gles2_zone_benchmark`, `model_viewer`) never opened willeq.db at all and were living off the switch -- now bootstrap it. **NEW (T270), user-visible: `EverQuest::GetRaceName()` in eq_strings.cpp is SHIFTED** -- 14 prints "Iksar" (row: Werewolf; Iksar is 128), 128 -> "Vah Shir" (130), 130 -> "Froglok" (330), 330 -> "Drakkin". ~~Class A~~ CLOSED T271 `65214239` (names + scale db-only; a THIRD copy of the pre-JSON numbering was live in `EntityRenderer::getScaleForRace()` -- rat/bat scales on wolf/bear rows; `test_who_response`'s fixture had no db so its pinned /who lines were rendering from the shifted table). Left: `getColorForRace()` (no colour column -- would be inventing data; comments annotated). NEW (T271): `Graphics::getRaceName()`/`getGenderName()` have ZERO callers (delete); race 330's row name is "Froglok (Playable)" which is what /who now PRINTS -- a data annotation leaking into display, fix in race_models.json + rebuild. Both ~~class A tiny~~ CLOSED T274 `eb4d4a33` (330 -> "Froglok", disambiguation moved to the row's `note` field; db rebuilt with row-level proof; only 1 of 190 rows was annotated; both dead getters deleted). **NEW (T274/T261): `visual_gate`'s p95 CPU-ms ceiling still trips when ANOTHER SLOT is building -- RUN_SERIAL only serialises within one ctest run, and the metric is wall-clock (`cpuDrawMs` in zone_bench_metrics.cpp). Class C for Will: switch the metric to process CPU time (getrusage) and RE-BLESS `p95CpuMsBaseline`, or accept the re-run. Every occurrence so far was a timing trip with ZERO image mismatches.** Same class, cosmetic: `getRaceScale()`, `getRaceName()` in race_codes.cpp, `entity_renderer.cpp getColorForRace()`. Also: `invisible` column has no reader; 166 eqemu ids 199-366 have Titanium models but no row (never spawn on P1999-era servers).
  - **B — in progress / owed:** ~~`AcceptGroupInvite()` reads
    `m_pending_inviter_name` after `SendGroupFollow()` cleared it (T99)~~ —
    **CLOSED T217 `175c97ca`, live-verified two-client (review/T217/)**; skills-window cap screenshot from the official
    client (owed task, Will's decision #4 — never ask him for a number);
    T149/T150 senders still wire-unverified.
  - **C — architecture / tooling residue, needs a ruling or a design pass:**
    TWO `state::GameState` instances (T78 census done); `InitGraphics`
    renderer parameter + asset-loader namespace + `GetDebugLevel()` static
    (S2 residue); registry 3 (renderer if-chain, ~36 names incl. `/newskills`)
    ~~invisible to `/help` (T143); headless `outcome=queued` for renderer
    commands (T139/T142); phantom `gequip7` in `config/eqe1_archives.json`
    (T198)~~ — all three CLOSED T223 `e6d31424` (catalog + drift-guard test;
    headless refusal; T142 console echo CLOSED T233 `78e041ad`); `protectedRegions_` all-false (T79 census); memory categories
    without budgets (T69); UI-gesture-only reachability pattern (not
    retired); Z5.3 / P52 / `waitForFlipComplete()` perf residuals;
    **doc debt:** `docker/CLAUDE.md` still documents
    `build-arm-noble.sh --no-gles2` (retired P32) and describes the Pi
    target as Lima/Mesa desktop GL — out of T215's territory, see the
    cross-reference entry at the end of this file.

## Index — open and recently-closed items (rebuilt T215, 2026-08-25; supersedes the T53 index)

**Each item is anchored by its `## ` section heading text (or a unit ID),
not a line number** — `grep -n '<quoted text>' refactor/deferred.md` finds
it. Items T53 listed that a later commit closed are kept here, struck, with
the closing hash, so the index and the body agree. The T53 snapshot this
replaces is preserved verbatim in git (`e6a8c4e9`) and in
`refactor/T53_deferred_board_hygiene.md`.

**Architecture / needs a ruling**
- TWO `state::GameState` instances exist, unconnected — needs a decision;
  T78 (`b02228eb`) did the census. Heading: `## TWO \`state::GameState\`
  instances exist, unconnected`.
- ~~Command-registry overlap (~34 names span two registries).~~ **CLOSED**:
  T98 `7cce0792` deleted the 34 shadowed `CommandProcessor` copies, T99–T140
  migrated every surviving family to `action::CommandProcessor`, T139
  `bb0ac689` deleted the 34 redundant game-registry copies, T143 `e979a821`
  made `/help` registry-1+2 aware. What remains is registry 3 (renderer
  if-chain) having no catalog — see T143 below. Heading: `## Command-registry
  overlap and the bare \`/target\` precedence gap`.
- `InitGraphics` keeps its `IrrlichtRenderer*` parameter; asset-loader
  namespace relocation; `EverQuest::GetDebugLevel()` static call from
  `door_manager.cpp` — still true at `e70e3347`. Heading: `## Decoupling
  residue — CLOSED by S2` ("Still open from that audit" list).
- ~~`action::EqActionHandler` cannot push a `GameStateBridge` intent
  directly.~~ **CLOSED as a class by T138 `13799cb7`** (finding 4: the
  per-command `EverQuest::Request*` wrapper recipe absorbs it; no standalone
  unit needed). Heading: `## T136 (2026-08-18): \`EqActionHandler\` cannot
  push bridge intents`.
- ~~`/equip` migration to `action::CommandProcessor` — deferred by T136 and
  T140.~~ **CLOSED by T141** (`e45c7f99` extraction into `InventoryManager`,
  `b71c885b` migration, merged `fe290cb0`). Headings: `## T136 (2026-08-18):
  \`/equip\` migration` and `## T140 (2026-08-18): \`/equip\` migration`.
- Registry 3 (`IrrlichtRenderer::processSlashCommand()`'s ~36-branch
  if-chain) has no query interface, so `/help` cannot list it and
  `/newskills` — the only working skills-window toggle — is registered in
  NO registry (T138 finding 2, T143). **CLOSED T223 `e6d31424`** — `renderer_command_catalog.{h,cpp}`, `/help graphics`, drift-guard test. Headings: `## T143 (2026-08-18)` and
  `## T138 (2026-08-18): The 61-command triage`.
- ~~Headless builds report `outcome=queued` for every renderer-routed name
  (`application.cpp:1243`)~~ **CLOSED T223 `e6d31424`** (refused with a message when `!m_graphicsInitialized`; unknown names now `unrecognized` in graphics mode too) — the `#ifdef EQT_HAS_GRAPHICS` refusal was lost
  when T139 deleted the game-registry copies; T142 widened it by two names.
  Headings: `## T139 — headless \`#ifdef EQT_HAS_GRAPHICS\` guard not
  preserved` and `## T142 — headless regression widened by 2 more names`.
- `protectedRegions_` bitvector permanently all-false — census by T79,
  decision deferred. Heading: `## \`protectedRegions_\` bitvector is
  permanently all-false`.
- Memory categories with no configured budget / Pi per-device budgets /
  untracked-RSS gating (T69) — follow-ups, not started. Headings: `## Memory
  categories with no configured budget`, `## Pi per-device memory budgets`,
  `## Untracked-RSS gating recommendation`.

**Client correctness bugs — open**
- `EqActionHandler` TRANSFORMS inventory: T93 tested 3 of ~11 methods; T95
  `02361953` validated the three int→enum casts; `targetSelf`,
  `targetGroupMember`, `hailTarget` remain untested against the real
  implementation. Heading: `## T93 fake-substitution sweep`.
- **T148 GM senders (eq_gm.cpp), two open (was three):** ~~`SendGMHideMe` reaches the
  server as `OP_SpawnAppearance` type 20 and is dropped~~ — CLOSED T217
  `175c97ca` (real `OP_GMHideMe`, parameter is a VISIBILITY flag; DB
  `account.hideme` toggled live); `ZoneProcessGMFind` (`eq_gm.cpp:200`) decodes
  the reply's floats as ints; `SendGMSummon` (`eq_gm.cpp:30`) sends zeroed
  coordinates. Heading: `## T148 (2026-08-19) — four defects surfaced by
  making the GM senders reachable`.
- **T99 `AcceptGroupInvite()` — CLOSED T217 `175c97ca`** (was:) `eq_group.cpp:143`
  builds the "You have joined X's group" message from
  `m_pending_inviter_name` after `SendGroupFollow()` (`eq_group.cpp:72`)
  has already cleared it (verified at `e70e3347`). Heading: `## T99: Group
  family migrated`.
- **T100 `/reply` silent success:** `CommandProcessor::cmdReply` →
  `ActionDispatcher::replyToLastTell()` returns `Success()` even when no
  tell sender exists (`command_processor.cpp:1040-1058`); game's "No one to
  reply to" cannot be reproduced at that layer. Heading: `## T100: Chat
  family migrated`.
- **T103 `TradeManager::rejectTradeRequest()` never fires `m_on_cancelled`**
  (`trade_manager.cpp:54-63`; `cancelTrade()` at :65 does). Heading: `## T102
  (Combat + Social migration) — findings deferred` (finding 7).
- **T04 `ZoneProcessNewZone` never calls `CheckZoneRequestPhaseComplete`**
  (`eq.cpp:1918`; the only callers are `eq_player_info.cpp:57,65,74,83`).
  Latent, not a demonstrated hang — request/response causality covers it;
  a one-line defensive call is still owed. Source:
  `refactor/testing_progress.md` "T04 findings" §2.
- **T174 keyboard hotbar — Item and Emote slots are skipped, not routed**
  (`input_action_bridge.cpp:424-437`): `slot.id` is an item id but
  `useItem()` takes an inventory slot; no resolver exists at that layer.
  Source: `refactor/T174_keyboard_hotbar.md` limitation 1. See also the
  T174 cross-ref entry at the end of this file.
- **T173 second name resolver:** `CombatManager::SetTarget(const
  std::string&)` (`combat.cpp:279`) does exact-then-substring match with no
  living-over-corpse ranking — divergent from the resolver T173 `eef57da0`
  fixed. `refactor/T173_corpse_targeting.md` line 99: "wire or delete".
- **T198 phantom `gequip7`:** `config/eqe1_archives.json:8` lists `gequip7`
  in `globalArchives` but `/mnt/projects/EverQuestP1999/gequip7.s3d` does not
  exist (gequip, 2–6, 8 do) — every zone load logs "cannot open
  gequip7.eqe1". Benign noise; remove it. Source: `09d6ef03` body.
- ~~Auto-loot on kill never wired to the authoritative death event; shift+click
  loot path unverified.~~ **CLOSED**: T33 `b3f6cda9` wired the loot machine
  to `ZoneProcessDeath()`; T34 `5a52cc4f` implemented `/loot` and verified
  the action-layer loot path live. Heading: `## Auto-loot on kill has NEVER
  worked against a real server`.
- ~~`race=415`/`race=330` render as placeholder cubes.~~ **415 CLOSED by
  T213 `b6d0576f`** (aliased to race 36 `RAT`; 457 `Gnomework` aliased to 88
  `CLM/CLF` in the same commit; `willeq.db` rebuilt 185→187 rows; verified
  placeholder=false, 904-vert mesh). **330 is NOT settled**: it has had a
  `race_models.json` entry since `35c51be2` (2026-01-13), so T56's "past the
  367 ceiling" framing was wrong for 330; whether gfaydark's race=330 still
  cubes is unverified. Heading: `` ## `race=415`/`race=330` render as
  placeholder cubes ``.
- blackburrow/crushbone render entirely blank in the benchmark — not
  root-caused. Heading: `## From Batch H`.
- ~~No unattended character creation on a fresh eqemu DB.~~ **CLOSED in
  practice by T157 `42eeb84d`**: account + character creation is automated
  by driving the official Titanium client under Wine/Xvfb on the eq-oracle
  rig (provisioned a Rogue). The client-side `OP_CharacterCreate` handshake
  is still unimplemented — that is now a feature gap, not a test blocker.
  Heading: `## From Batch H`.
- ~~"Player BSP region is SIZE_MAX" stale-zone-name race~~ CLOSED T260 `53e3dac4`: the P17 tag `m_zone_bsp_tree_zone_name` was NEVER written, so the snapshot region lookup was skipped on 100% of zone-ins since P17 (spurious "stale" WARN); `SetZoneBspTree` now refuses a tree built for another zone; `playerBspRegion` has zero consumers so the FATAL became a WARN (P44 befallen (-195,-982,3) is legitimately outside every region). CONFIRMED live T263 `48e6ae10` (region=1161 qeynos2, 2220 befallen; the restore spot itself is legitimately outside every region in geometry order; zero stale WARNs).
  Heading: `## "Player BSP region is SIZE_MAX" with stale zone name`.
- Pale fog-colored flat shapes in gfaydark/qeynos2 goldens — not chased.
  Heading: `## Pale fog-colored flat shapes in gfaydark/qeynos2 goldens`.
- ~~RaceModelLoader/equipment_model_loader ignore `textureMasked()`.~~
  **CLOSED by P37 `018bcdcb`** ("WLD-masked entity/equipment textures
  actually render masked"; `equipment_model_loader.cpp:637` consults
  `textureMasked()` at `e70e3347`). Heading: `## P20 live-visual pair`
  ("NEW (P21 audit)" bullet).
- ~~Magenta/black checkerboard missing-texture placeholder (qeynos2
  rooftops; T85 North Qeynos).~~ **ROOT-CAUSED AND FIXED by T211
  `9588c07e`** (fragment 0x2C has no material-list ref field; the code
  guessed `flags & 0xFFFF`, a flags word; fallback to the nearest preceding
  0x31 → NOT FOUND 26→0), **confirmed at data and pixel level by T212
  `54b9bc72`** (corrected `equipment_models.json`; forced NPC with model
  1036 renders a textured blade, 0 magenta pixels of 480000). Headings:
  `## P20 live-visual pair` ("New unrelated bug spotted") and `## Texture-
  fallback (magenta/checkerboard) defect reproduces reliably (T85`.
- ~~`ConstrainedTextureCache` public query API vs masked-variant key.~~
  **FIXED by T92 `ddf08b27`.** Heading: `## P50 (2026-07-31)`.
- Two `applyPaletteMask` plumbing gaps (`loadTextureFromBMP()` fallback;
  equipment texture-variant swap) — still open. Heading: `## P37
  follow-ups` (P46d "Open follow-up items").
- `#goto` / forced-relocation convention — CLEARED T55/T58; per-frame
  movement writes unconverged by decision. Heading: `` ## `#goto` same-zone
  teleport lands at the DB position ``.
- UI-gesture-only reachability pattern — narrowed (T48/T51/T52, T66 #7–8,
  T145/T146 #10–13), not retired. T148/T150/T153/T156 wired the GM, social,
  face and skill families; `SendControlBoat` remains caller-less (T151
  deferred boats). Heading: `## PATTERN: real features reachable only
  through a UI gesture`.
- ~~T84 `EqActionHandler::setHeading()` double-conversion.~~ **FIXED by T90
  `59b1dbce`.** ~~T87 ~252° outbound heading disagreement.~~ **SUPERSEDED
  by T89 `a9e1fc0c` — confirmed measurement artifact, no code defect**
  (headings say so). Headings: `` ## `EqActionHandler::setHeading()`
  double-converts `` and `## WillEQ's outbound heading disagrees`.
- ~~`SkillManager::activateSkill()` had zero callers.~~ **CLOSED by T156
  `5fee68ed`** (`/useskill`), re-probed by T158 `57167d94`; ~~`/taunt` and
  its class bypassed the skill gate~~ **CLOSED by T162 `91378195`**.
- ~~T109 entity load distance gate ships INERT.~~ **CLOSED**: T111
  `d21086be` fixed pickup and enabled the gate, T112 `59d85d52` measured its
  Pi cost, T210 `5505c8a0` re-verified deferred pickup on approach. Heading:
  `## T109 (entity load distance gate) — CLOSED`.
- ~~T188 "the gate is losing entities".~~ **WITHDRAWN by T190 `8bad78d4`**
  (the drain was never broken; the test position was — `/move` teleport
  artifact). ~~T191 "composite atlas has 0% adoption".~~ **RETRACTED by T192**
  (annotated `5f875227`: every T191 run had `eqe1Path=''`).
- ~~T193–T196 Pi `std::bad_alloc` / address-space exhaustion with
  `--eqe1-path`.~~ **CLOSED**: T195 `0157fdc7` named it address space, T197
  `09483f60` showed atlas-off removes it, T198 `28a59bf4` dropped the
  composite atlas from the `orangepi` preset; the x86 SIGSEGV in the same
  matrix (T201 `c1ac05c0`) was the UI-atlas use-after-free fixed by T205
  `3b456a1b`, after which T199 `3edeb0dd` verified spawn-driven archive
  selection end to end.

**Test coverage gaps** (unchanged from T53 except as noted)
- 8 `DoorManagerGraphicsTest.*` skips (DISPLAY-gated); `PfsArchiveContentTest.DdsFileValid`
  and `SoundBufferTest.LoadSwingWav` (asset-gated); 2 `SpellManagerGems.*`
  skips (no Warrior-usable spell in scanned range). `InventoryModelViewTest`
  skips RESOLVED by T54 `4fe0ad95`.
- `src/client/bridge/irrlicht_bridge.cpp` is compiled into `willeq_core`
  (`CMakeLists.txt:395`) and therefore linked by every `willeq_core` test
  target; six test sources reference `IrrlichtBridge` directly
  (`test_game_state_sync_bridge.cpp`, `test_zone_connection_flow.cpp`, …).
  The testing tracker's "in no test binary" handoff is struck (T215).
- `irrlicht_bridge.cpp`'s switch handles 99 of the 101 `GameEventType`
  enumerators; `AutoAttackChanged` and `EntityStateSnapshot` have no case
  there (consumed elsewhere). Root `CLAUDE.md`'s "100% coverage" wording is
  corrected by T215; whether those two SHOULD reach the renderer is not
  examined here.

**Perf / tooling residuals** (all narrowly scoped, unchanged)
- Z5.3 empty-draw measurement — Heading: `## From Batch Z`.
- Batch E table empty commit columns — Heading: `## Tracker hygiene`.
- P52 `B_e205` magnitude ~21-24% high — Heading: `## P52 (2026-08-01)`.
- `waitForFlipComplete()` swap-floor question — Heading: `## P41 landed`.
- Pi zone-in duration (~37–40s) is **NOT a work item**: Will ruled load
  time secondary to correctness with no maximum (`docs/decisions.md`
  2026-08-25). T209 `d6a96103` checked the one correctness angle (redundant
  `seq_entities` work) and refuted it.

**Units with no body entry in this file** (findings live in their plan doc
or commit; one-line cross-refs are appended at the end of this file, T215):
T149–T184 (plan docs `refactor/T1xx_*.md`), T185, T189, T192, T194, T196,
T198, T202, T204, T212, T214.

**A note on staleness**: this Index is a snapshot as of `e70e3347` (T215).
Heading texts were verified with `grep -n` at that commit. If a later unit
re-titles a section, search for the unit ID or a quoted symbol instead.


## FIXED: ParticleManager::renderUnified use-after-free (P34, 2026-07-29)

- Root cause confirmed: `atlasTexture_` was loaded once (init(), one-shot,
  guarded by `initialized_`) via `ConstrainedTextureCache::getOrLoad`/
  `uploadTexture("particle_atlas")` — an ordinary LRU cache entry — but
  `ParticleManager` was NEVER registered as a `TextureEvictionListener`,
  unlike every sibling texture-caching subsystem (AnimatedTextureManager,
  SkyRenderer, DetailManager, StormCloudLayer self-register; region/entity
  meshes and the software cursor have their own guards — see
  renderer_zone_merge.h). Exact freeing sequence:
  `IrrlichtRenderer::unloadZone()` runs SYNCHRONOUSLY on every zone
  transition (P17) and unconditionally calls
  `constrainedTextureCache_->clear()`, which notifies listeners then
  `driver_->removeTexture()`s every cached entry including
  `"particle_atlas"` — with no listener, `atlasTexture_` (and
  `particleMaterial_`'s texture slot) were left dangling. The next zone's
  first `renderUnified()` call dereferences it
  (`atlasTexture_->getDriverTextureHandle()`) — SIGSEGV, matching P33b's
  gdb backtrace exactly. Unconditional on every zone unload (not
  memory-pressure-gated like most of this bug class), hence the
  reliable ~1-2s-after-zone-in timing.
  **Fix**: `ParticleManager` now implements `TextureEvictionListener`
  (self-registers/deregisters in `setConstrainedTextureCache()`/
  `~ParticleManager()`, mirroring AnimatedTextureManager's idiom — the
  single-named-entry variant of this codebase's established two-layer
  pattern, not the scene-graph-scanning RegionMeshTextureGuard variant,
  since there's no node collection to scan here); `onTextureEvicted()`
  nulls `atlasTexture_` + clears `particleMaterial_`'s texture slot the
  instant eviction happens, and `onZoneEnter()` (runs every zone visit,
  always after `unloadZone()`'s `clear()`) re-acquires the atlas if null.
  particle_manager.h/.cpp only. Verified: 6-step bar clean,
  `particle_race_stress` clean (100k iters), and 8 live eqemu qeynos2
  sessions post-fix (including a 3m45s single-zone stay) show zero
  atlas-related crashes — see refactor/P34_particle_atlas_uaf.md Review.

## FIXED: SimulationWorker HCMap use-after-free on zone transition (P35, 2026-07-29)

- Root cause confirmed: NOT a missing join on the renderer side (that
  pairing — `stop()` -> `thread_->join()` -> `clearZoneData()` in
  `stopSimulationWorker()` — was always correct). The bug was on the GAME
  thread: `EverQuest::CleanupZone()` (`eq_zone.cpp`) queues a
  `CollisionMapChanged{nullptr}` bridge event and then IMMEDIATELY
  synchronously frees the outgoing zone's `HCMap` (`m_zone_map.reset()`),
  well before the renderer even starts the new zone's loading thread (the
  only place `stopSimulationWorker()`/`unloadZone()` actually runs). In
  the window between those two points — which can be seconds — the
  outgoing zone's `SimulationWorker` thread is still running with its
  `zoneData_.hcMap` pointing at the now-freed `HCMap`; every NPC
  ground-snap tick in that window (`sim_entity_sync.cpp` ->
  `HCMap::FindBestZ()`) was a use-after-free, matching all 4 recorded gdb
  backtraces exactly. Confirms the hypothesis this entry was filed with.
  **Fix**: `EverQuest::m_zone_map` (`eq.h`) is now
  `std::shared_ptr<const HCMap>` instead of `std::unique_ptr<HCMap>`, with
  the same shared_ptr (not a raw `.get()`) copied through every boundary
  it crosses — `CollisionMapChangedData::map` (event_bus.h),
  `ZoneLoadSnapshot::zoneMap`, `IrrlichtRenderer::collisionMap_`/
  `setCollisionMap()`, and `SimulationZoneData::hcMap`
  (simulation_worker.h). The game thread's `reset()` now only drops ITS
  reference; the object survives until every renderer-side holder also
  releases its copy (via `clearZoneData()`/`setCollisionMap()`), so it
  simply outlives every holder instead of dangling under whichever thread
  is slowest to finish with it. `HCMap` is accessed as `const` everywhere
  now (all its read paths were already const methods). 9 files touched
  (payload-type plumbing); all other consumers needed zero code changes
  (`operator->`/`operator bool` work identically on the shared_ptr).
  Verified: 6-step bar clean (1406/1407 ctest, ARM clean, goldens
  untouched), `particle_race_stress` clean, and a live eqemu session
  driving 6 consecutive befallen<->qeynos2 transitions (the exact P34 7/7
  repro) plus a 243s single-zone soak, both crash-free — see
  refactor/P35_hcmap_transition_uaf.md Review.

## Tree wind update unreachable on live GLES2 (P32 finding) — PARTIAL: wind animation FIXED by P33 (blessed); the eqemu port-collision item below is still open, needs Will's call

T53 (2026-08-05) heading note: the body already documents its own
resolution (P33 restored `TreeWindController::update()` and was blessed by
Will 2026-07-29) — this is a re-title only, no new evidence needed, the
original heading just never caught up with the body it introduces.

- `TreeWindController::update()` — the only code advancing wind time
  and the weather-multiplier transition — was ALREADY unreachable on
  the live GLES2 path before P32 (gated on a CPU-instancing tree count
  only the deleted software path populated). The weather multiplier
  has always been effectively frozen at 1.0. P32 preserved that exact
  behavior. RULED by Will 2026-07-28 ("trees are supposed to move;
  only still when the tree wind animation setting is disabled") →
  **P33 LANDED (merged `dd0912e`) with a REFRAMING finding**: the GPU
  wind trees run off IrrlichtRenderer::windTime_, which was ALREADY
  incrementing unconditionally — trees were already animating in
  production, and the real bug was the INVERSE: the animatedTrees
  setting gated nothing. P33 fix: update() restored (weather
  multiplier transitions live again) + weatherMult forced to exactly
  0.0 when disabled (traced GLSL: strength becomes exactly 0 → true
  rest pose). Gate byte-stable by construction (benchmark never calls
  IrrlichtRenderer) + double-run proof; bar green 1376/1377.
  Evidence: P33b stills + P33c videos (qeynos2; gfaydark blocked by
  the eqemu port bug below). **BLESSED by Will 2026-07-29** (video
  verdict). Wind amplitude stays tunable (uWindParams) if ever wanted.
- **eqemu ENVIRONMENT bug (pre-existing, now bit twice)**: the
  akk-stack image's entrypoint static-zone fleet collides with
  configured static zone ports — P29 measured captures landing on
  7012+/7026 (vs tests' hardcoded 7000, testing session's T03a), and
  P33 found gfaydark's configured static port occupied → world
  redirects to a wrong port → infinite login/world/zone-transfer loop
  (gfaydark UNREACHABLE for live sessions). qeynos2-via-#zone remains
  the reliable route. Real fix is server-container config (outside
  sanctioned executor scope) — needs Will's call on whether/how to
  adjust the akk-stack zone fleet.

**STANDING CROSS-SESSION POINTER (2026-07-28): the testing campaign
tracks its work in `refactor/testing_progress.md` (charter:
`refactor/TESTING_CHARTER.md`). Perf session: read its "Handoffs"
section each batch. Handbacks land in `refactor/TESTING_HANDBACK.md`.**

## → FOR THE TESTING SESSION: mutation-testing intel (benchmark session, 2026-07-29) — CLOSED

- The model-eval benchmark session (separate clone) ran mutation
  testing against b68c32a and found: **`wld_loader.cpp` has the most
  covered lines in the repo (952) but killed only 1 of 8 injected
  faults** — the suite executes the parser broadly and asserts almost
  nothing about it. Coverage there is misleading; kill rate is the
  better prioritization signal. Consider assertion-strengthening for
  the WLD parser (and kill-rate spot checks on other "well-covered"
  parsers) in a future T-batch.
- **DONE — WLD half (T10, merged `bd60ee3`, 2026-07-28).** Independently
  measured the baseline as **0/8** (marginally worse than the reported
  1/8) and strengthened it to **4/8**. Confirms the signal. Two dead-code
  findings fell out: `resolveTextureName` is unreachable and the
  per-mesh header bbox fields are read by nothing.
- **DONE — the other half (T22, merged `1559271`, 2026-07-31).** 43
  mutations across six parsers with test binaries: **24 → 37 killed**
  after assertion-strengthening (`spell_database` 2/7→7/7,
  `string_database` 6/8→8/8, `eqe1_loader` 5/7→6/7, `pfs` 5/7→6/7,
  `xmi_decoder` 3/7→5/7, `s3d_loader` 3/7→5/7). Confirms the intel's
  thesis a third time: `ZoneLightLoadingTest` asserted
  `EXPECT_GE(int, 0)` — a tautology that let a total zone-light-loading
  break survive — and `S3DLoader`'s own texture map was inspected by no
  test at all. Survivors representing unobservable behavior (a debug
  counter, a discarded return value, metadata nothing reads) were
  documented rather than papered over. **This item is closed; thanks for
  the pointer, it was worth three units.**
- Their coverage numbers also confirm the T01 gap map: renderer/
  simulation splits (irrlicht_renderer, simulation_worker,
  entity_renderer) at 0% — their benchmark would use coverage there
  as task-verification oracles if it ever exists (FYI, not a request).

## Desktop-GL elimination — COMPLETE (P32, 2026-07-28)

- P31 identified Phase 4 as blocked on a product decision (entry points
  had to close before the deletions were provably safe). Will's
  2026-07-28 scope-ruling addendum (`docs/decisions.md`) resolved it:
  `--renderer` limited to `gles2`, constrained presets made
  renderer-independent, ARM `--no-gles2` Lima build retired. P32
  (`refactor/P32_phase4_final.md`) executed exactly that sequence —
  entry points removed first, then `RenderingBackend::Software`/
  `::OpenGL` + `EDT_BURNINGSVIDEO`/`EDT_OPENGL` call sites deleted,
  `AnimatedTreeManager` deleted (its two live consumers extracted onto
  the pre-existing `TreeWindController`/`TreeIdentifier` TUs), and the
  ARM Lima build retired. The desktop-GL elimination roadmap
  (`refactor/desktop_gl_elimination_plan.md`) is now COMPLETE — see its
  Phase 4 entry and `refactor/P32_phase4_final.md`'s Review for full
  evidence.
- NEW BASELINES (P32): GLES2 tree 1361/1362, headless tree 939/940
  (sole failure = ALSA exception in both). Executor protocol baseline
  note should track ~136x, not 1235.
- Root `.dockerignore` added (P31) — docker builds no longer copy
  ~12GB of local build trees.

## Handback items owned by the perf session (Will, 2026-07-28) — ALL THREE ITEMS CLOSED (per their own inline markers)

T53 (2026-08-05) heading note: all three bullets below already carry their
own DONE/FIXED/RESOLVED markers (P28, P29+T03a, and the .eqemu-lock
process fix) — heading-only re-title for scan-ability, no new evidence
needed.

- **eq.h EQT_HAS_GRAPHICS misclassification — FIXED by P28 (`1cae489`,
  merged `1c4...`/`06cfed7`-adjacent; 2026-07-28)**: all five guard
  blocks reclassified (only renderer-facing API stays guarded);
  headless compile restored 351→0 errors; nogfx suite 812/813; all
  graphics trees at baseline. Combined-tree verification post
  P28+P30 merge: canonical GLES2 tree 1234/1235 equiv (segfault+gate
  failures were STALE main artifacts from Jul 25 — rebuilt, both
  green: gate PASS, headless test PASS). Two flagged seams for later:
  InitializeAudio's constrained-preset read is graphics-gated (no
  headless memory-budget concept — seam documented in P28 Review);
  BspTree/PfsArchive/WldLoader formally reclassified as base data
  (pfs/wld_loader moved to CLIENT_CORE_SOURCES). ORCHESTRATOR
  PRACTICE NOTE: main's gitignored artifacts (third_party static
  Irrlicht, build-gles2 binaries) age silently while slots rebuild —
  any verification run from main MUST rebuild them first (this cost a
  false-alarm investigation on 2026-07-28). Original diagnosis:
  — supersedes the earlier "GRAPHICS=OFF bitrot" framing below with a
  SHARPER diagnosis): the five guard blocks in eq.h enclose large
  amounts of NON-graphics code (GameTick, m_quit_requested,
  InventoryManager, CommandRegistry, hotbar model, EQ client path, the
  163-line member block at ~1689-1853). Correct fix = move non-graphics
  members OUT of the guards (leaving only renderer-facing API), NOT
  adding matching guards to the .cpp files — note this reverses the
  direction of the small hotbar-accessor guards added by the 55b101e
  quick fix. ~33k lines of base-layer source (all 18 eq*.cpp, combat,
  inventory, spell/skill managers, application) are in no test binary
  because of this; blocks the testing campaign's highest-risk backlog
  item (connection flow). Macro-name indirection to confirm:
  CMake option EQT_GRAPHICS vs macro EQT_HAS_GRAPHICS. Unit-sized;
  needs judgment on the mixed 163-line block.
- **Capture fixtures — DONE (P29, merged `8fdcd24`, 2026-07-28)**:
  canonical home `/mnt/projects/fable/captures/` (NFS, README inside),
  `data/captures` symlinks in main + slot3 (other worktrees: create the
  same symlink as needed; .gitignore fixed to match the symlink).
  session.sleq (2,943 records) + session.pcap (5,603 pkts, login/world/
  befallen/qeynos2) + self-created golden; replay deterministic (3
  runs). 10 tests skip→PASS; desktop skips 38→24.
  **→ FOR THE TESTING SESSION (tests/ territory) — CLOSED, T03a
  (merged `02d591d`, 2026-07-28)**: the remaining 4 FAILs + 8 skips
  shared ONE root cause — `tests/pcap_test_utils.h` + three test files
  hardcoded zone port 7000, but the akk-stack image's entrypoint
  auto-boots ~15 static zones claiming 7000-7011, so real captures land
  on 7012+ (befallen 7012, qeynos2 7026 in this fixture). T03a
  implemented BOTH suggested fixes: handshake-based autodiscovery
  (`OP_SessionResponse` sender = server port) plus `WILLEQ_ZONE_PORT` /
  `WILLEQ_ZONE_IP` overrides. 4 FAILs + 8 SKIPs → 12 PASS, and
  compression assertions were verified rather than weakened.
- **Process fix DONE**: .eqemu-lock protocol added to
  EXECUTOR_PROTOCOL.md (the testing session correctly called out that
  P25's session ran the server without a lock — the protocol predated
  the charter; now every executor checks/creates/removes the lock).

## Desktop-GL elimination — ROADMAP LANDED (P24, 2026-07-28)

- Standing decision (docs/decisions.md 2026-07-28): GLES2 is the only
  3D renderer. Full evidence-backed inventory + phased unit plan:
  **`refactor/desktop_gl_elimination_plan.md`** (the authority — this
  entry is just the pointer). Headlines: ~76/90 targets already
  GLES2-clean; 14 link-broken in the GLES2 tree (mechanical, Phase 0 —
  same never-declared-full-link-deps family as the IdentityMaterial
  item below); GLES2-tree ctest 1112 passing with ONE genuine new
  regression: **RaceModelAnimationHeadlessTest.ModelAnimatesProperly_
  Headless segfaults 100% on the GLES2 tree, passes on desktop
  (EDT_NULL fixture, crash in TestBody per gdb)** — Phase 1
  investigation unit, hard blocker for suite certification. Phase 3
  gate SATISFIED: P25 LANDED (`10d95f6`, merged 2026-07-28) — X11-window
  EGL backend, opt-in via `WILLEQ_EGL_X11` env var (deviation from the
  planned --egl-x11 CLI flag: private-header constraint; still explicit
  opt-in, no DISPLAY autodetect, surfaceless default byte-identical,
  gate PASS). Full input translation (keyboard/mouse/wheel/close),
  resize declined round 1 (fixed-size WM hints). Windowed live-session
  evidence: benchmarks/audits/p25_x11_window/EVIDENCE_MANIFEST.txt. Tools: 5 port /
  6 tools RETIRED by Will 2026-07-28, executed by P26 (`2c6a1f9`,
  merged `daefaf8`: sources → tools/retired/, targets removed).
  **Phase 0 EXECUTED (P26)**: both trees register identical 1235 tests
  (byte-identical name lists); GLES2 tree 1233 pass — only the ALSA
  host exception + the Phase-1 RaceModelAnimationHeadlessTest segfault
  (re-confirmed sole genuine failure). model_viewer's break was an
  EQT_DRM-only conditional gate; the other 7 targets had no backend
  conditional at all. GLES2 full-suite recipe: separate tree with
  EQT_BUILD_TESTS=ON WITH_RDP=ON (the build script's own config
  intentionally builds only willeq+benchmark). **Phase 1 EXECUTED
  (P27, merged `7521f32`)**: root cause was class (a) — the patched
  device fork never implemented EDT_NULL (constructor probed DRM/EGL
  unconditionally; createDriver() had no EDT_NULL case → null
  VideoDriver → no scene manager; createDeviceEx expects EDT_NULL
  devices to self-construct a null driver, an invariant every stock
  device upholds). Fixed EDT_NULL-scoped in BOTH device files (ARM
  path provably inert — only EDT_OGLES2 ever constructed there);
  bonus pre-existing fix: P26's 8 targets were missing the X11 link
  line P25's windowed backend requires (isolated via stash — not the
  segfault fix's doing). **GLES2 TREE NOW CERTIFIED: both trees
  1234/1235 with only the ALSA exception; 5x-consecutive pass proof
  both trees.** **Phases 2+3 EXECUTED (P30, merged `06cfed7`,
  2026-07-28)**: GLES2 config is THE primary tree (CLAUDE.md +
  EXECUTOR_PROTOCOL bar updated); system-Irrlicht FIND fallback and all
  X11/Xxf86vm/GL branches removed; resolved-Irrlicht now mandatory via
  either EQT_GLES2_IRRLICHT_DIR or ARM's direct cache vars, with clean
  configure-time FATAL naming the bootstrap (negative-tested); APPLE
  branches kept (ambiguous, never-supported target — flagged);
  X11/GLX log message fixed. Lockstep done by orchestrator:
  benchmark-clone addendum (build_errors_response.md) + charter
  wording. NEW SMALL ITEM: `docker/Dockerfile` (non-ARM image) still
  apt-installs desktop Irrlicht and configures plain — now fails at
  configure BY DESIGN; retire or update that image (fold into Phase
  4/5). — **FIXED by P31 (`4f2e9417`, "Phase 4+5 ... Dockerfile GLES2
  recipe"), T53 (2026-08-05) verification**: current `docker/Dockerfile`
  bootstraps the same patched GLES2 Irrlicht fork (comment at line 14)
  instead of apt-installing desktop Irrlicht. REMAINING: Phase 4 (delete
  dead source paths: BURNINGSVIDEO / AnimatedTreeManager /
  RenderingBackend::Software+OpenGL / EDT_OPENGL loading-screen branch) +
  Phase 5 (full doc sweep + decisions.md completion marker) — await Will's
  go. — **STALE, T53 (2026-08-05): this "REMAINING" was superseded the same
  day it was written.** The "Desktop-GL elimination — COMPLETE (P32,
  2026-07-28)" section earlier in this file (which this entry's own
  Phase-numbering predates) reports Phase 4 EXECUTED and the whole roadmap
  COMPLETE — see that entry and `refactor/desktop_gl_elimination_plan.md`.
  Treat this specific "await Will's go" as historical, not a live blocker.
  The
  willeq-benchmark clone's
  build_errors_response.md guidance (desktop build as default) stays
  CORRECT until the migration phases land — update it in lockstep with
  Phase 2/3.

## Build-config latent defects (found 2026-07-27 by an outside Release build)

An external benchmark setup building at `412d457` with
`-DCMAKE_BUILD_TYPE=Release` exposed two defects our default builds
(empty build type, -O0) never trip:
- **8 tool targets missing `${IRRLICHT_LIBRARY}` link dep — FIXED
  `55b101e` (2026-07-27)**: added the library per the zone_line_editor
  precedent. Root mechanism for the record: at -O0 no reference to
  `irr::video::IdentityMaterial` is emitted so they linked by luck; at
  -O3 the `ISceneNode::getMaterial` comdat carries the extern reference.
  Verified: Release+tests build clean (103 binaries), Release ctest
  1234/1235 (only the documented RDP intermittent; note the Audio ALSA
  exception PASSES on this host in Release — env-dependent as always).
- **`EQT_GRAPHICS=OFF` — RESOLVED by P28 (2026-07-28)**: the real fix
  was the eq.h guard DECLASSIFICATION (move non-graphics members out),
  not more guards — see the handback section near the top of this
  file. Headless compile + suite fully restored (812/813).

## Decoupling residue — CLOSED by S2 (2026-07-23, see refactor/S2_decoupling.md)

EntityPoseState moved to state layer; dual movement channel collapsed to
intent-only (was a render-thread call into game state + doubled position
sends); InventorySlotChanged events now carry display fields (the "dead"
inventoryManager_ pointer was actually a live cross-thread read — audit
claim corrected); bridge payloads typed (HCMap*/PathfinderNavmesh*);
PlayerPositionUpdate struct deleted; eq.cpp graphics includes pruned 12→2.

Still open from that audit (**T53, 2026-08-05: all four re-verified by
grep against current tree, all still true**):
- `InitGraphics` keeps its `IrrlichtRenderer*` parameter (drives
  initLoadingScreen + initDeferredGraphics); full removal needs
  Application-side init restructuring. Confirmed:
  `include/client/eq.h:1166` — `bool InitGraphics(int width, int height,
  EQT::Graphics::IrrlichtRenderer* renderer, ...)`.
- Asset-loader namespace relocation (`WldLoader`, `PfsArchive`, `BspTree`,
  … out of `EQT::Graphics` into a shared asset layer) — own unit.
  Confirmed: both classes still declared under `namespace EQT { namespace
  Graphics { ... } }` (`include/client/graphics/eq/pfs.h`,
  `include/client/graphics/eq/wld_loader.h`).
- Renderer reads spell static data (spell_visual_fx.cpp) and item DB
  (equipment_model_loader.cpp) — acceptable (static data).
- `EverQuest::GetDebugLevel()` static call from door_manager.cpp.
  Confirmed: `src/client/graphics/door_manager.cpp:955`.
- `output/graphical_renderer.h` legacy interface — dead-code deletion
  candidate (IrrlichtRenderer does not inherit from it). Confirmed the
  narrow claim (`class IrrlichtRenderer` has no `: public
  output::GraphicalRenderer` base) — but the header is NOT simply dead:
  `src/client/application.cpp:1869` does
  `dynamic_cast<output::GraphicalRenderer*>(renderer)` and 2 other files
  include it (`src/client/output/graphical_renderer.cpp`,
  `include/client/mode/graphical_mode.h`). The cast is always null for the
  real (Irrlicht) renderer, so the interface is currently unreachable in
  practice, but deleting the header would need checking those 3 sites
  first — not a pure delete-and-done as "dead-code candidate" implies.

## From Batch U / ui_fixes.md

- **Input-lag fix (original Chunk 1) deferred** — application-loop 1 ms sleep +
  60 fps input gating (application.cpp ~438/462) causes 17–20 ms input latency.
  Was waiting on D21b; D21b is done, so this is now unblocked.
- Target casting bar not yet in new UI (irrlicht_bridge.cpp:748).

## From arch_design_update.md / audit_results_post.md

- **Unified priority queue only partially implemented** — `BackgroundWorkQueue`
  instances (zone load, deferred work, BSP preload) remain strict FIFO;
  mesh/texture rebuild queues sort by distance only, not full `WorkPriorityKey`.
- **Door/object placeholder cube** — only asset type without a placeholder
  (arch_design_update.md §7 TODO).
- **Entity/equipment texture failure path** allocates GPU texture before budget
  check (post-audit §3.4, High).
- **GL calls on render thread as fallback** when GPU upload thread unavailable
  (texture_atlas.cpp:187,357) — desktop GL always takes this path (§7.2, High).
- **Unbudgeted `driver_->addTexture()` calls** — sky, animated textures,
  animated trees, zone geometry fallback, software cursor (§3.5, Medium).
- **Budget caches not thread-safe** — ConstrainedTextureCache /
  ConstrainedMeshCache have no mutex; blocks future multi-worker expansion (§3.6).
- `loadDisplaySettingsFromFile()` on render thread (irrlicht_renderer.cpp:3454).
- Dead code in zone_lines.cpp — three `#if 0` blocks (~130 lines) +
  `hasBspZoneLines()` (always false), `debugTestCoordinateMappings()`.
- RDP peer threads not joined until `stop()` (§1.3).

## From F00 test repair (2026-07-22)

- ~~**`SpellData::effects` is never populated.** `EQT::SpellInfo`
  (include/client/willeq_database.h) has no effects field, so
  `SpellDatabase::queryAndCache()` leaves every spell's effects array empty,
  and `isDamageSpell()`/`isHealSpell()` can never return true via this path.
  The old file-based loader populated it; the SQLite migration dropped it.
  Whoever owns the spell-effects data path should restore or formally remove
  it.~~ **RESOLVED BY POPULATION, T68 (2026-08-11)** — see the `## Client
  correctness bugs` entry above for the full writeup; this is step 1 of the
  spell-effects rewrite arc, not the whole rewrite.
- Case-insensitive and partial-name spell search no longer exist
  (`getSpellByName` is exact-match `WHERE name=?`); old tests covering them
  were deleted in F00. Restore only if a consumer needs them.

## Batch G — CLOSED (2026-07-23, see refactor/G_decomposition.md)

loadZoneSequential decomposed into 20 step methods (largest 408 lines);
RegisterCommands split into 3 domain TUs each under 1,500; StartCombatMovement
overloads co-located.

Files still over the 1,500-line warn ceiling (all under the 3,000 hard
ceiling; accepted): ~~renderer_zone_load.cpp (2,795 — now structured as step
methods)~~ **SPLIT by T166 (2026-08-21): it had grown to 2,996, i.e. onto the
hard ceiling, and was distorting the code inside it. Now four TUs —
renderer_zone_load{,_objects,_entities,_env}.cpp, largest 1,139 — plus
include/client/graphics/zone_load_context.h.**, eq_entities.cpp (2,438), eq_player_info.cpp (2,006),
eq_movement.cpp (~1,975), eq_bridge.cpp (1,837), renderer_frame.cpp (1,583),
renderer_player_physics.cpp (1,570), eq_merchant.cpp (1,558), plus the
accepted cores eq.cpp (~3,600) and entity_renderer.cpp (3,201).

## From Batch H (2026-07-23) — PARTIAL: spire-launcher item RESOLVED (marked inline); blackburrow/crushbone and char-creation confirmed still open, T53 (2026-08-05)

- **blackburrow and crushbone render entirely blank in the benchmark** at
  every camera position tried, even with all culling disabled (~2000-4000
  draws submitted, zero visible output). befallen renders fine. Not
  root-caused (H02 review speculates a BSP above/below-ground split issue).
  Could be a real rendering bug affecting those zones in production —
  investigate.
- **No unattended character creation on a fresh eqemu DB** — willeq lacks
  OP_CharacterCreate client-side, and eqemu has no console/GM command to
  create a character from nothing. seed.sh stops cleanly on a fresh install.
  Closing this means implementing the Titanium char-create handshake in the
  client (H03 review documents the server-side flow to match).
- ~~spire launcher "wedged"~~ RESOLVED 2026-07-23: never wedged — the H03
  executor invoked `spire eqemu-server:launcher start` directly, an internal
  entry point whose process check matches its own cmdline ("already
  running" by self-match). The sanctioned path is `spire spire:launcher
  start` (the container's `start` alias), verified to boot the full fleet.
  up.sh keeps direct-binary single-zone boot as CI default (lighter: 1 zone
  vs ~15) with `--launcher` for the sanctioned path.
- The container's `process-watcher.pl` kills any process using >30% CPU for
  60s whose name isn't allowlisted (gcc/mysql/world/etc.) — `zone` and
  `spire` are NOT allowlisted. A busy zone process during automated runs
  could be killed mid-test. Watch for this in CI; the allowlist lives in
  akk-stack assets (server-code-adjacent — flag before changing).

## Performance gap on real hardware (H04 baseline, 2026-07-23) — HISTORICAL: the headline 30fps gap this section opens with was later closed (see "KPI STATUS — befallen at the 30fps line (P11b...)" and P14 below); read as an arc, not a live blocker

T53 (2026-08-05) heading note: most sub-items already carry their own
FIXED/CLOSED/RESOLVED/DEAD markers inline (CMA leak overturned, v1 loader
fixed, format shootout decided). Genuinely still-open residuals from this
section are already carried forward into later sections (occlusion budget
— closed by P14; per-vertex-vs-per-pixel lighting — decided by P41/P42;
live-game-only full-population per-frame loops at sim_entity_sync.cpp:179
etc. — not independently re-verified by T53, flagged as
cannot-settle-cheaply without a live profiling session).

- **All three golden zones render below the 30fps KPI on the Orange Pi**
  (p95, 800×600, performance governor): qeynos2 ~28.8, gfaydark ~24.3,
  befallen ~22.4 fps (RGBA path — P01 confirmed RGBA numbers are stable/
  trustworthy; EQE1 columns are not, see below). P01 also CLOSED Will's
  lighting-vs-culling question: lighting is never applied to culled
  geometry (traced sim_visibility.cpp:91-149 → renderer_sim_bridge.cpp:
  700-705 → renderer_zone_draw.cpp:266-373), and culling is worth ~27x
  (947ms p95 with both culling systems off vs ~35ms on, qeynos2). This is the measured target for all
  future perf work; scripts/pi/bench_pi.sh + compare_pi.sh regression-track
  it automatically. Candidate levers already in this file: unified priority
  queue, GL-on-render-thread fallback, pointer-key draw order, render-loop
  blocking items.
- **P08 (2026-07-25) decomposed the CPU frame** (canonical befallen replay,
  production config, pinned poses bf355/472/626/791/917/987): submission
  (`setMaterial`+`drawMeshBuffer` loop) is 87-96% of CPU draw time on both
  x86 and Pi; Mali400/Lima per-draw cost 2.15-12.55 ms/100 draws synthetic
  (5.9-16.3 real). Ranked levers — RE-RANKED by P08b (2026-07-25,
  gameplay-load reality check; calibrated N=205 → median 38 visible
  entities, matching H04's live cap; P08's 200-entity "stress" config
  turned out already gameplay-representative by visible count):
  1. **Entity draw-path: composite entities LANDED by P08d (2026-07-26,
     blessed by Will; eligibility-gated: !isPlayer && !hasBodyPartOverride
     && !hasEquipmentTint; static quality evidence pixel-identical).
     Preset flip CARRIED 2026-07-26 ("bless flip") — composites live on
     the orangepi preset for eligible NPCs. Remaining work in this lever:**
     - ~~Builder-extension unit~~ **DONE by P08f (2026-07-26,
       b9e1a2a)**: --zone/--all-zones support, 76 new sheets (286 total,
       all audits clean), golden-zone coverage befallen 10.5→100%,
       qeynos2 21.4→100%, gfaydark 5.6→94.4%; 5/5 spot-checks
       pixel-identical; 8/8 befallen races resolve compositeData=YES.
       Sheets on NFS — live everywhere immediately. Residuals:
       (a) WOL_HS_DEF collision (gfaydark vs global6, different content;
       needs zone-qualified naming or runtime disambiguation — small
       follow-up if that model matters); (b) --all-zones sweep not yet
       run (documented invocation: entity_composite_builder --all-zones
       --zones-json docs/reference/pre_luclin_zones.json --eqe1; run it
       before playing non-golden zones with composites); (c) several new
       race codes lack numeric EQ race-ID mappings in race_codes.cpp
       (found in passing, unfixed).
     - **Animated-path crumple — RESOLVED by P12b (2026-07-27)**: the
       benchmark uploaded each entity's per-instance animated
       instanceMesh_ as a STATIC GLES2 hardware buffer before the first
       pose; COpenGLES2Driver::drawMeshBuffer() draws HWBufferMap
       entries from the frozen VBO snapshot forever (never checks
       setDirty/ChangedID) → CPU animation silently discarded.
       Benchmark-only (differential-tested); fix = drop the HW-buffer
       registration, use production's CPU-vertex-array path. Animated
       benchmark evidence is TRUSTWORTHY again. VertexMapping
       one-to-many patch LANDED (loop closed, byte-identical rendering,
       zero regressions); ModelAnimatesProperly replaced with a running
       headless variant + test_inventory_model_view wired into ctest
       (suite now 1235). NEW follow-ups: (a) **driver footgun**:
       COpenGLES2Driver static HW buffers never honor dirty updates —
       document/guard so production never HW-buffers an animated mesh;
       (b) **Animated composite delta — P18 INVESTIGATED, STOP with
       narrowed evidence (2026-07-27)**: original suspect list exhausted.
       Lighting flag is dead code at runtime (entity_renderer.cpp
       overrides mat.Lighting=false uniformly under the GLSL shader);
       normals through instance-mesh skinning disproven EMPIRICALLY
       (vertex-dump diagnostic: bit-identical Pos/Normal between 13-buffer
       and 1-buffer paths at the same anim frame); vertex color ruled out
       (both builders hardcode white). Note max_channel_delta is 182-224/255
       — the "low magnitude" was the MEAN only; the delta is structural,
       not FP noise. NEW LEAD: composite forces TextureWrapU/V=CLAMP_TO_EDGE,
       multi-buffer leaves REPEAT; a UV-extent-aware clamp patch REGRESSED
       the diff to 83-92% (reverted), pointing at GLES2 driver texture-object
       wrap-param state caching when a shared/cached ITexture* is used by
       materials wanting different wrap modes (draw-order-dependent stomping).
       P22 (2026-07-28) then REFUTED the wrap-param cache lead by
       instrumentation: wrap glTexParameteri is applied UNCONDITIONALLY
       per setMaterial (no dedup exists), 7,707 instrumented draws show
       0 readback mismatches and 0 shared-texture wrap conflicts
       (composite atlas textures are per-entity unique GL objects,
       never shared with multibuffer's) — the stomping scenario cannot
       arise. Bonus driver fact: NO mipmaps exist anywhere
       (regenerateMipMapLevels has zero callers; MIN_FILTER never
       *_MIPMAP_*), killing the mip-pollution framing too. The delta
       REMAINS OPEN; surviving suspect = per-path mesh construction
       differences (mesh_building.cpp caller-side "fixes" proved
       REGRESSIVE per P18 — a future unit needs a different angle,
       e.g. per-buffer UV/geometry stream comparison at the GL level).
       Static remains pixel-identical; not visually user-reported.
       Reviews: P18 + P22 plan files;
       (c) **P11b**: true composite
       perf magnitude on Pi (animated e205 replay) — P12b's optional
       phase was skipped.
       P12's original escalation record:** ownership verdict is SHARED PRODUCTION
       CODE (RaceModelLoader/EQAnimatedMesh/SkeletalAnimator) — the
       benchmark drives the identical path with the identical idiom;
       "production animates fine" is UNVERIFIED belief (the one test
       that would catch it, test_inventory_model_view
       ModelAnimatesProperly, is unconditionally GTEST_SKIP'd). Crumple
       present from frame 0 (not drift). Parked VertexMapping patch
       EMPIRICALLY NOT this bug (byte-identical A/B; real latent issue,
       still parked). Ruled out: synthetic OnAnimate driving, P05
       animation-track merge, per-buffer mapping population, bone-index
       correspondence, matrix convention, bone scale/translation.
       BEST LEAD for P12b: a raw vertex piece with bounding box wider
       than the whole skeleton's bone span → suspected per-vertex
       bind-pose disagreement between applySkinning's load-time bake
       and runtime frame-0 reconstruction. P12b should also UN-SKIP/
       repair ModelAnimatesProperly, and FIRST answer the discriminating
       question: do NPCs visibly animate correctly in live play
       (Pi/GLES2 and desktop paths)? If yes, there IS a path delta the
       bisection missed; if no one can confirm, this may be a live
       defect. Also noted by P12: a visual_gate foliage pixel mismatch
       on slot2 with ZERO source diff (environment/Xvfb suspected —
       cross-check against P11's slot1 gate run before trusting).
     - **Live-client animation verification** of composites (production
       clip selection + Will's eyes) before/with the preset flip.
     - gles2_composite_audit has an unrelated pre-existing
       device-creation bug (P08d worked around it; small fix someday).
     P08c original verdict for the record:** Head-to-head at gameplay load (befallen replay
     p95 speedup): batch 18.1% / composite 33.1% / lod 35.5% (worst
     visuals — flat silhouettes) / direct-gl BROKEN (garbled textures, no
     perf win — disqualified; genuine bug, undebugged per plan).
     Composite generalizes (56.0% on qeynos2) and — key finding —
     **production already has the full pipeline built and dormant**
     (EntityRenderer::setCompositeEntityPath + entity_composite_builder
     tool + 210 pre-baked sheets at /mnt/projects/EverQuest_ETC1/;
     ConstrainedRendererConfig::enableCompositeEntities defaults false,
     absent from orangepi preset). Landing unit = flip the preset flag +
     verify real assets + Will's quality verdict on PRODUCTION rendering
     (the benchmark's composite stand-in is not representative) + tier-2
     impact. Projected befallen p95 279.7 → ~187ms — real but NOT
     KPI-closing alone; lever #2 still needed after.
  2. **Cross-region zone draw merge — LANDED IN PRODUCTION by P10
     (2026-07-26, fable; flag-gated mergeZoneDraws default OFF +
     /mergedraws toggle; REDUCED shape per plan authorization: merge
     active only when the zone is fully resident, permanent per-visit
     fallback on first eviction — which covers ALL zones today because
     ConstrainedMeshCache is NEVER CONSTRUCTED in production, see new
     item below). In-app A/B on real x86 client vs eqemu: FP-class-only
     diffs (scattered singles, max_delta<=1) at every geometry pose;
     mechanism confirmed active (draw-count INFO line). Bonus fixes:
     /screenshot debug command + EGL-surfaceless capture; a real
     game/render-thread GL-context violation in --script dispatch.
     **CRASH FIXED by P10b — and FLIPPED 2026-07-27 (a8a9b03): Will
     blessed after P11b's full-stack measurement. mergeZoneDraws=true
     on the orangepi preset; the Pi now ships merge + composites
     (befallen avg 25.11ms/39.8fps, p95 35.32ms/28.3fps on the
     canonical replay). The P09→P11b arc is closed.** Real root cause (deferred-loading hypothesis
     falsified): ConstrainedTextureCache eviction under qeynos2's 64MB
     budget left DANGLING ITexture* in region materials because
     clearTextureReferences() only scans the live scene graph while
     manual-draw mode detaches ALL region nodes (1898/1898 detached) —
     **a latent PRODUCTION bug: the unmerged path carried the same
     dangling pointers and escaped only because it touches just the
     visible subset per frame.** Fixed properly: RegionMeshTextureGuard
     eviction listener scans regionMeshNodes_ directly (both paths now
     safe); build() defensive (skip+WARN); merge-activation gate now
     also watches the TEXTURE cache (merge auto-disables in zones that
     evict — qeynos2 provably never activates under the current 64MB
     budget; befallen engages: 10→4 draws, 60% on the sampled view).
     6/6 matrix crash-free; A/B FP-class only (max_delta=1). Bonus
     fixes: dangling softwareCursorTexture_; ParticleManager race
     workaround properly applied (real fix still owed). NEW small
     items: (a) qeynos2 merge coverage requires either a texture-budget
     bump or merge-under-eviction tolerance (only matters if qeynos2
     ever drops below KPI — it is above KPI today); (b) Testchar's
     befallen restore coordinate hits a pre-existing local map/BSP
     FATAL (flagged, unfixed); (c) two pre-existing post-camp
     shutdown-thread aborts (already documented class). Perf case = P09c benchmark numbers (befallen zone-only
     p95 173.8→25.5ms). FUTURE follow-ups: full shared-VBO/eviction
     reconciliation (unit-sized; needed only when lazy streaming gets
     wired); zone objects remain unmerged (inspection-verified).
     P09-series history for the record:** A-gate
     settled the architecture: per-frame client-side-index gather ("the
     1999 way") costs 0.147ms where N=1000 separate VBO draws cost
     59.17ms on Lima (~400x; streamed-IBO/coalescing also fine) — Lima
     dynamic-index handling is NOT pathological. Prototype
     (--merge-zone-draws, default off, in tree) hits P08's ceiling
     exactly (bf472: 1308→14 draws, 98.9%) but pixel-identity FAILS two
     ways: (a) small 2-44px z-fight diffs from intra-run reorder —
     expected class, needs a stable secondary sort key or documented
     tolerance; (b) UNATTRIBUTED texture corruption at golden views
     (up to 31% of frame — textured surfaces render as flat lit
     gradients; ruled out: hash collisions, GL errors, vertex format,
     index rebasing, overflow; leading hypothesis: per-mesh-buffer
     SMaterial-carried shader state, e.g. the MaterialTypeParam
     atlas-page pattern, copied from the run's FIRST buffer only).
     STOP condition honored — Pi perf phase not run.
     **P09b (2026-07-25) root-caused the big corruption: a PRE-EXISTING
     PRODUCTION DRIVER BUG** — COpenGLES2Driver::getOpaqueAlphaFallback()
     lazily binds its 1x1 white fallback WITHOUT glActiveTexture(unit 1)
     first, clobbering unit 0's diffuse texture for the process's first
     no-companion draw (invisible one-buffer glitch unmerged; whole-
     surface corruption when that draw is a coalesced run). Benchmark
     worked around it (first-draw-unmerged + SMaterial::operator==
     grouping); **the driver itself still has the bug — fix it in
     production (tiny: select unit 1 before the lazy-init bind), which
     also removes the benchmark workaround.** Remaining P09b status:
     canonical repro 148k→2k px, but 14/15 poses keep residuals — (a)
     ±1-3px alpha-test edge FP noise from baked-world-space vs
     per-region-matrix transform order (inherent to the design; the fair
     bar is bake-vs-bake, or a one-time visual bless of the FP delta),
     (b) at least one MORE unattributed contributor at qeynos2/befallen
     goldens (max_delta up to 209; suspects named in review: wind-shader
     per-buffer uMeshYBounds packing, atlas unit-1/AtlasSolid3D paths).
     **P09c (2026-07-26) CLOSED the thread: driver fixed in production**
     (glActiveTexture before lazy-init bind; the bug was BAKED INTO 2/9
     goldens — persistent corruption, re-blessed by Will 2026-07-25),
     merge mechanism PROVEN correct (merged vs baked-unmerged:
     max_delta=0 at 13/15 poses, 1 LSB at 2 — narrowed, documented), the
     old "unattributed contributor" fully attributed to the
     architecturally-inherent FP transform-order delta (baked
     world-space vs per-region matrices; up to 44k px / max_delta ≤104;
     contact sheet at benchmarks/contact_sheets/p09c_fp_delta/ — needs
     Will's one-time judgment when scoping the landing), and **the Pi
     numbers are in: befallen zone-only p95 173.8→25.5ms (draws
     253→7.9, 96.9%); befallen combined e205 p95 279.4→65.8ms (-76.4%);
     qeynos2 zone-only 19.3→17.1ms.** KPI: merged+today's-entities
     measured 65.8ms; projected ~44ms (~23fps) stacked with P08c
     composite — merge alone doesn't close 30fps; entity work needed in
     addition. **NEXT: the production landing unit** (sketch drafted in
     P09/P09c reviews: cell-or-zone-scoped baked VBOs, mesh-cache
     eviction keys, per-region lighting/occlusion preserved by the
     gather design, FP-delta bless).
  3. **Occlusion occluder-budget increase** — unchanged; cheap interim.
  4. Within-region merges: DEAD (~0% ceiling).
  - P08b method notes: --animate is nonrepresentative in the benchmark
    (4.5-5x visible inflation — don't use for perf runs); static
    --camera-pose runs don't render entities at all (pre-existing; use
    frame-indexed extraction from replays instead); tier-2's befallen
    39.5ms p95 vs the replay's 279.7ms is pose selection (3 curated views
    vs worst-case-inclusive path), so tier-2 alone understates the KPI
    gap.
- **CMA "leak" OVERTURNED (P03 controlled measurement):** no texture format
  leaks CMA kernel-side (true-leak check with cache drops: all formats
  <=+60kB per run). P02's "reboot-only drain" was PAGE CACHE borrowing
  movable CMA pages while reading pack files — a plain drop_caches restores
  it mid-session (52->102MB observed), and RGBA drained MORE than ETC1
  (proportional to bytes read). The P02 abort protocol reacted to a
  harmless-but-reclaimable condition; telemetry stays (it's how we learned
  all this) but thresholds should key on reclaim behavior, not raw CmaFree.
- ~~v1 loader whole-pack heap loading~~ **FIXED by P04 (2026-07-24)**:
  mmap + lazy referenced-only archive opening. Result: -107 to -110MB
  resident per zone; the historical /pmem "untracked RSS" mystery solved
  (the EQE1 open step's invisible heap was 110-112MB of it; steady
  untracked now ~21-24MB); EQE1 stall bursts eliminated on the FULL 4.7GB
  production dir (8/8 burst-free, p95 spread <=0.27ms); Pi steady RSS
  92-95MB. Bonus fixes: zone archive/wld released post-BSP (-7.5 to
  -12.7MB), ZoneLoadSnapshot lifetime, dead snapshot fields, a pre-existing
  loader-vector data race. Remaining named gap: entityprep bytes accessor
  for the audit line; live willeq zone-in audit pending next server
  session.
- **Format shootout verdict (P03, awaiting Will's decision):** measured
  fairly, ETC1 wins steady-state speed (-3.5 to -12.4% vs RGBA), GPU memory
  (8x), pack bytes, and cold load (NFS-transfer-bound → smallest pack
  fastest). Quality cost is real (29.5dB vs 8888). RGB565/4444 is now a
  fully implemented, measured fallback (+2.3-7.6% vs ETC1, 46.4dB visually
  transparent, no companion machinery). Draft recommendation in
  refactor/P03_payload_format_shootout.md: KEEP ETC1, fix the loader.
- **Bonus from P03:** the EQE1 upload path was silently compiled OUT of x86
  builds (guard widened) — ETC1 packs now work under llvmpipe, enabling
  future ETC1-path goldens/CI coverage on the VM.
- **EQE1-vs-RGBA (P01 correction of the H04 headline):** H04's "EQE1
  consistently slower" was partly a measurement-methodology artifact — its
  harness never varied A/B ordering (pi_bench_run.py), and P01's ABBA runs
  showed strong order/thermal effects. The REAL finding: RGBA p95 is stable
  within 2% across sessions while **EQE1 is erratic (>49% spread,
  30.7-46ms, no order correlation)** — the mystery is EQE1's instability,
  not a uniform slowdown. H1 (H02d fetch asymmetry) DISCONFIRMED; H2
  (filtering) refuted from source; H4 (Lima ETC1 sampling) best-supported
  but unprofiled. P02 re-measures with randomized interleaving + CmaFree
  telemetry (EQE1 variance may be CMA/allocation-correlated).
- **Per-vertex vs per-pixel lighting on real geometry (experiment 6b)** —
  the one P01 experiment that got zero data before the Pi panic; the
  benchmark hard-codes per-pixel (zone_bench_loader.cpp:209). Historical
  synthetic data showed ~3x cost for the model switch. Top priority on
  hardware resume (P02) — point-light COUNT is confirmed free (flat
  34.6-35.3ms across --lighting 0..8).
- **Pi kernel panic under dense benchmark cycling**: died between runs
  (zero GPU activity in the window) after 41 DRM init/teardown cycles in
  ~45min; H04's gentle 4 runs were fine. Consistent with cumulative
  kernel-side GEM/CMA state. P02 protocol: CmaFree sampled between runs
  (downward ratchet = the leak signature), capped runs/session, scripted
  reboots between matrices (fstab now persists NFS), kmsg follower running
  on the VM for lead-up capture; serial console for the trace itself.

## Outskirts anomaly (P05→P07, 2026-07-24) — CLOSED (P07d thread close + Q01 harness alignment); one optional lever left

- **P07 (merged 3c2b801)** adds the hybrid floor Will approved: in area
  mode the nearest `adaptiveLightingAreaPPFloor` regions (default 1)
  always keep per-pixel (their area still accumulates). Contact sheet
  (benchmarks/contact_sheets/p07_pp_floor/) confirms the near-black
  artifact is gone — anomaly/corridor poses byte-identical to count-mode,
  city pairs visually indistinguishable. All software gates green.
- **THREAD CLOSED (P07d, 2026-07-24)** — see docs/decisions.md
  "Zone lighting budget: COUNT mode stays production default" for the
  full chain and rationale. Area+floor flip was REJECTED at golden
  review (near-field blackouts of frame-dominant geometry); radius mode
  proved lossless savings don't exist (player light R=712 units covers
  the zone). Count/area/floor/radius all remain flag-gated; production
  unchanged. task/P07c-default-flip parked unmerged (rejection evidence
  in its contact sheets).
- ~~**Harness alignment (salvaged from P07c, still worth doing):** visual
  gate + Pi tier-2 baseline currently measure FULL per-pixel, not the
  production count-mode adaptive config — add the production flags to
  both, regenerate goldens (count-mode look), re-bless with Will,
  re-capture tier-2. Small unit; fold into the next harness-touching
  batch (path drift is 6 occurrences counting P07d — do record/replay
  first or together).~~ — **DONE by Q01 (2026-07-25, merged in the Q
  batch at `f502298`)**: harness aligned to the production count-mode
  config, goldens regenerated and **blessed by Will** after 3 review
  rounds (final fixture = count mode + Greater Lightstone level 9),
  Pi tier-2 baseline re-captured under the count config (all 6 combos
  faster). The record/replay half landed in the same unit — canonical
  1000-frame paths at `benchmarks/paths/`. Struck 2026-08-02 (stale
  since 2026-07-25).
- Optional future lever (only if lighting cost resurfaces post-P08):
  radius-mode margin<1 perceptual-knee study (mechanism landed in P07d,
  config `adaptiveLightingRadiusMargin`).
- **Pi froze HARD while idle** (2026-07-24 ~12:40 UTC, before P07 ran any
  timed invocation): monitor showed NO error output (Will confirmed) and
  kmsg follower captured no trace — a hard hang, not a panic, so a serial
  console would likely capture nothing either. Distinct from the P01
  panic (which followed 41 dense DRM cycles); this one was idle. If it
  recurs, consider a hardware watchdog (`/dev/watchdog`, sunxi-wdt exists
  on H3) so the Pi self-recovers instead of needing physical power cycles.
- ~~After the Pi numbers: default-flip decision (orangepi preset →
  area+floor), golden re-bless with Will, Pi tier-2 baseline rerun.~~ —
  **MOOT: the area+floor flip was REJECTED at golden review and the
  whole thread closed by P07d** (see the THREAD CLOSED bullet above and
  `docs/decisions.md` "Zone lighting budget: COUNT mode stays production
  default"). Count remains the shipped default; area/floor/radius stay
  flag-gated. The separate radius-mode flip question was also settled
  independently — NOT a candidate (P19, its own section below). Struck
  2026-08-02.

## Outskirts anomaly history (P05→P06, 2026-07-24) — mechanism landed, superseded by P07 above

- P05 root cause (flat region-COUNT budget, renderer_zone_draw.cpp) fixed by
  P06: flag-gated area-mode budget (`adaptiveLightingBudgetMode`, k=0.75),
  merged 23aee56, **default remains `count` everywhere — no behavior
  change yet**. Outskirts poses: area-mode within 0.1-1.6% of
  all-lightweight (2.2-2.6x gap closed). Whole-run: 11.0→12.2 fps.
- **Structural finding gating the default flip (orchestrator analysis of
  P06 CSVs):** area-mode is statistically all-lightweight — whole-run avg
  81.71ms vs all-LW's 81.47ms, median per-frame ratio 1.002, per-pixel
  meaningfully engaged in only 89/1000 frames. Mechanism: the
  camera-containing region nearly always straddles the near plane → the
  conservative fallback counts it as full-screen → budget exhausts at
  region 1. So flipping the default = zone per-pixel lighting effectively
  OFF ~91% of the time (entity per-pixel budget unaffected). Options for
  Will: (a) flip anyway if the visual delta is acceptable (contact sheet),
  (b) refine the estimator first (near-plane AABB clipping, or a
  world-extent discriminator so only genuinely huge regions trigger the
  full-screen fallback — small unit), (c) P05 candidate #2, frame-time-
  driven rolling budget (bigger, most robust). Scene graph exonerated
  (P05 hypothesis B refuted by code audit).
- **Live-game-only follow-up (benchmark-blind):** several per-frame loops
  iterate FULL zone populations regardless of visibility:
  sim_entity_sync.cpp:179, door_manager.cpp:964,
  animated_texture_manager.cpp:383, entity_castbars.cpp:149. Real mechanism
  in code, unquantified — needs a live-client measurement session (tour
  machinery + frame timing) to size before bounding.

## Benchmark camera path — CLOSED by Q01 (2026-07-25, see refactor/Q01_path_replay_harness_align.md)

- `--dump-path`/`--path-file` record/replay landed; canonical 1000-frame
  paths for the three golden zones committed at `benchmarks/paths/`.
  **All future perf-unit moving runs must REPLAY a canonical path** —
  record-vs-replay proven byte-identical on metric columns (x86 R0=R1=R2),
  and geometry columns byte-identical x86↔Pi. Orbit fallback now warns
  loudly + is in the help text. `benchmarks/pi_runs/` unignored (stranded
  P01-P04/H04 CSVs from both slots salvaged into git).
- Residual: entity placement now samples the resolved measured-frame trace
  (not the raw navmesh polyline arc length) — a future `--entities` +
  moving-path run WITHOUT replay places entities differently than the
  P05-P07-era runs did (those were never cross-comparable anyway).

## Radius mode as production default — RESOLVED: NOT a flip candidate (P19, 2026-07-27)

- **P19 re-measured under the FULL final stack** (merge + composite
  scenegraph + --animate + occluder 256 + production config, N=205,
  canonical 1000f replays, both zones, Pi/Mali400): radius is slower
  than count everywhere — befallen +3.3% avg / +6.2% p95, qeynos2
  +1.3% avg / +1.8% p95. Decisive detail: befallen count p95 = 32.39ms
  (under the 33.3ms 30fps line) vs radius p95 = 34.38ms (over it) —
  the flip alone would surrender the KPI that occluder-256 just closed.
  Radius losslessness RE-CONFIRMED under the new stack (x86 pixel spot:
  0/480000 px diff vs full-PP, with and without the stack). Verdict:
  keep count as shipped default; radius stays flag-gated as the
  provably-lossless option. Re-open only if a future unit changes the
  demotion-path cost structure (befallen demoted on 44.3% of frames —
  the per-frame demotion recompute is the plausible overhead locus).
  Full tables in refactor/P19_radius_flip_remeasure.md Review +
  benchmarks/pi_runs/p19_radius_flip_remeasure/.

## Radius mode as production default — original Q03 analysis (2026-07-25, superseded by P19 above)

- Under the honest Greater-Lightstone fixture (Q03; level 9, ×3 boost,
  R=336), radius mode is PROVABLY lossless (pixel-identical to full-PP at
  all 15 x86 poses incl. 6-10 real demotions per pose) while count mode
  provably unlights lit geometry at any view with >20 visible regions
  (7/15 poses, up to 69% px). But radius timed 1.1% (moving) to 8-17%
  (city static) SLOWER than count in Q03's Pi measurement — count buys its
  speed by unlighting reachable geometry. GLS reach tripled radius
  demotions vs the level-10 first pass; a flip unit should re-measure
  timing under the FINAL fixture (Q03 amendment: boost + slot-0 fill were
  added after the Pi timing runs), and per the P07c lesson a flip needs
  its own golden/tier-2 re-bless. Will has conceptually endorsed
  radius-limited pp lighting ("what's stopping efficient light calculation
  in a radius around the player") — the remaining question is purely perf.

## KPI status + the composite measurement gap (P11 synthesis, 2026-07-26) — SUPERSEDED: every bullet here is struck through and closed inline (P14, P11b); see "KPI STATUS — befallen at the 30fps line (P11b, 2026-07-27)" below for the actual resolution

- **Stacked measurement (P11, befallen combined e205 canonical replay)**:
  merge+composite-scenegraph = avg 54.66 / p95 65.93ms (15.2 fps p95) —
  does NOT cross 30fps. BUT the composite half of the stack measured ~0%
  because the benchmark's scenegraph route uses STATIC entities
  (getMeshForRace), and composite meshes are built ONLY on the ANIMATED
  path (getAnimatedMeshForRace → buildCompositeMeshFromGeometry, single
  SMeshBuffer = 1 draw/entity — orchestrator-verified in code).
  So: P08c's -33% (stand-in) unreliable; P11's ~0% (static scenegraph)
  measured the wrong route; **the TRUE production composite magnitude
  has never been measured and is gated on fixing the animated-path
  crumple (P12b)**. Production's flipped flag is directionally real
  (1 draw per animated entity) with unmeasured magnitude.
- ~~**Occluder budget: DEAD lever on befallen** (P11: +0.9-2.7% at 2x and
  uncapped). Production knob exists
  (occlusionMaxOccluderRegions, default 16) — one-line flip if another
  zone ever shows headroom; do not spend more on this.~~ — **REVERSED by
  P14 (2026-07-27)**: P11's verdict was measured PRE-stack. Under the
  merge+composite stack, 16→256 takes befallen whole-run p95 from
  35.320 to 32.313ms — it is the lever that closed the KPI gap. Blessed
  by Will and **shipped** (`9c5617b`, C++ orangepi preset; mirrored into
  `config/constrained_presets.json` by P39). Struck 2026-08-02.
- **Residual to 30fps** (at merge + static-entity dispatch): ~57-60%
  submission (entity draws dominate), ~37-39% GPU sync. The entity
  lever through the REAL animated path is the critical path.
- **--all-zones sweep DONE** (P11 side deliverable): 120 zones, 191 new
  sheets (477 total on NFS), 184s, 97 collisions logged, 39 failures
  all sleeper (DR2_HS_DEF texture won't fit 2048) — audit clean.
- Gate arbitration: P11's slot1 visual gate PASSED → P12's slot2
  zero-source-diff foliage mismatch is environment-specific
  (Xvfb-session suspected), not a gate regression.
- ~~**NEXT: P12b** — fix the animated-path crumple (bind-pose lead in the
  P12 review), un-skip ModelAnimatesProperly, THEN re-measure the
  stacked number through animated composite entities = the real KPI
  verdict. Will's live-animation answer is load-bearing twice over
  (crumple liveness + whether flipped composites render correctly in
  play).~~ — **DONE. Will's gates-video verdict (2026-07-27) settled the
  liveness question: models animate CORRECTLY in the real client, both
  multibuffer and composite → the crumple is benchmark-context-only and
  production animation is exonerated.** P11b then delivered the real
  measurement (p95 −36.9% / avg −54.6% from composites; see the P11b
  KPI section below). The remaining benchmark-side `--animate` garbling
  is tracked as its own root-cause unit, still open. Struck 2026-08-02.

## Live x86 client bugs (anim-video-capture session, 2026-07-26) — PARTIAL: 4 of 5 items closed (marked inline), 1 still open

T64 (2026-08-11) heading note: the missing-equipment-entry item, listed as
still-open below, was ALREADY WARN+skip in `fable` by the time T64 checked
(same wording as the parked branch's fix) — closed, evidence in
`refactor/T64_video_capture_extraction.md`'s Review. That was the last of
the three "capture-blocking workaround" items keeping the branch parked;
all three are now independently resolved in `fable` (saveScreenshot guard
and the particle race were already closed inline below by P23/T53). T64
extracted the `--dump-frames-dir`/`--dump-every` capture flag itself
(`include/client/application.h`/`src/client/application.cpp` only) onto
`fable`, added a `--dump-max-frames` bound and a first-class
`scripts/ci/encode_capture.sh` encode step, and it is now documented in
`CLAUDE.md`. **The rest of `task/anim-video-capture` (6c48bb4, slot2) is
superseded** — the `constrained_presets.json` scratch presets were
session-scoped scaffolding (not needed now that the flag is a supported
tool), and the renderer_frame.cpp/renderer_zone_load.cpp/
irrlicht_renderer.cpp changes are all already in `fable` via better fixes
(P23's real race fix, not the isEnabled() gate workaround). The branch can
be deleted once someone confirms nothing else on it is wanted — not done
here (T64 only extracts, does not clean up branches per protocol).

T53 (2026-08-05) heading note: saveScreenshot guard, the particle race, and
post-camp shutdown aborts are marked FIXED/CLOSED inline below with
commits. Still open: the befallen first-frame SIGSEGV (worked around by
zone substitution only, not root-caused).

Found while recording the P12 animation-evidence videos (real client,
llvmpipe, eqemu). Workarounds live on parked branch
task/anim-video-capture (6c48bb4, slot2) — NOT merged; each needs a
proper fix:
- **befallen SIGSEGVs on the very first rendered frame** on x86 GLES2
  even with vanilla `--constrained orangepi`, no capture flags —
  pre-existing, severe (adjacent to the H02-era blackburrow/crushbone
  blank-render item?). Zone had to be substituted (qeynos2) for capture.
- renderer_zone_load.cpp: one NPC with a missing item_models.json
  equipment entry ABORTS the whole zone load (FATAL) — should be
  WARN+skip. **CLOSED — already WARN+skip in `fable` (T64, 2026-08-11
  confirmed by direct read of `renderer_zone_load.cpp:1614`,
  identical wording/behavior to the parked branch's fix).**
- saveScreenshot() guard — FIXED by P23 (`e702341`, 2026-07-28):
  widened to EQT_HAS_DRM || EQT_HAS_GLES2; x86 GLES2 build now compiles
  the safe glReadPixels path (nm-verified), ARM path byte-identical.
- ParticleManager::renderUnified race — FIXED FOR REAL by P23
  (`e702341`, 2026-07-28): TWO races in SimulationWorker (unsynchronized
  input_ read during computeAll; worker starting a new compute into a
  buffer not yet consumed → swap onto mid-write index). TSAN evidence:
  55 warnings incl. 2 heap-use-after-free before; 0 across 3×100k-iter
  soaks after. Stress tool committed: tools/particle_race_stress.cpp
  (manual, TSAN invocation documented in P23 Review). The P10b-era
  isEnabled() gate on renderUnified REMOVED — NOTE the behavior
  restoration: unified fire/spell particles now render whenever
  zoneReady, independent of atmosphericParticles (they were incorrectly
  suppressed under the shipped default since the workaround landed).
  WATCH ITEM: if any zone's KPI replay contains fire emitters
  (befallen braziers?), the next Pi measurement may show a small
  frame-time delta from particles rendering again — attribute it here,
  don't treat it as a regression mystery.
- Post-camp shutdown aborts — CLOSED by P36 (`4b30404`, merged
  `e8efd87`, 2026-07-29). Root cause: UpdateMovement() ran on TWO
  threads (game thread + the 500ms m_update_thread); when the update
  thread won the race to process the deferred post-logout disconnect,
  CleanupZone → StopUpdateLoop → join() ran ON m_update_thread =
  self-join EDEADLK abort. Fix: update thread no longer calls
  UpdateMovement (game thread already drives it; CombatManager tick
  kept) — self-join structurally impossible. Causality proven by
  revert-and-reproduce. ALSO: SIGTERM had no handler (default-killed;
  now flag + requestQuit). Exit matrix all clean: 5×/camp exit-0
  (2 post-transition) + SIGTERM + script-end + headless.
  Residual small items: window-close exit verified by inspection only
  (no WM in sandbox); action::CommandProcessor's /q is dead code
  (chat-registry /q works) — cleanup candidate.

## Sheet quality DONE + deployed (P16, 2026-07-27)

- UV-inset seam fix (gutters rejected — would have inflated
  power-of-2-tight sheets up to 4x; inset keeps packing byte-identical,
  shrinks JSON uvOffset/uvScale to the safe interior). Tile-cap
  unroll discipline (cap=1, mathematically exact for GL_REPEAT-sampled
  character textures) + a justified one-line backward-compatible
  runtime change (fmod UV wrap in mesh_building.cpp, no-op when
  uncapped). **67.9% aggregate sheet reduction (813→261MB), worst
  outlier 16x smaller, no sheet grew.** Audit 498/498 clean. DEPLOYED
  to live NFS with full dated backup
  (/mnt/projects/EverQuest_ETC1_backup_20260727); post-swap spot
  render verified. NOTE: also cuts the composite share of texture
  memory ~3x — relevant to the cross-zone eviction fix sizing.

## P20 live-visual pair: pop-in RESOLVED (by design), foliage alpha real site found (2026-07-27) — PARTIAL: 3 items closed inline, 3 still open

T53 (2026-08-05) heading note: pop-in (non-bug), foliage alpha (FIXED
P20b, blessed), and door/entity material gap (FIXED P21) are closed
inline below. Still open: RaceModelLoader/equipment_model_loader ignoring
`textureMasked()` (needs its own unit with live visual evidence), the
magenta/black checkerboard placeholder on qeynos2 rooftops (not chased),
and — closed as a non-issue, listed only for completeness — the stale
mergeZoneDraws SIGSEGV retraction.

- **Geometry pop-in: NOT a bug** — controlled same-pose experiment
  (`--zone-load manual` vs automatic) proves it's deferredAssetLoading's
  progressive build; ~1-3s wall-clock fill after arriving in an unbuilt
  area (real-time measurement, not a capture-rate artifact; likely
  LONGER on Pi hardware). Mitigation candidates (proposed only):
  (a) camera-heading/velocity-aware WorkPriorityKey prioritization;
  (b) larger pre-warm halo at zone-in/teleport before releasing the
  loading screen; (c) per-tier deferredAssetLoading knob. Evidence:
  benchmarks/audits/p20_live_visual/EVIDENCE_MANIFEST.txt.
- **Foliage alpha FIXED by P20b (`ece11f0`), BLESSED by Will
  2026-07-27** (same-pose evidence, qeynos2 gates + gfaydark nekpine;
  bless page on the standing artifact). Historical diagnosis:
  AnimatedTreeManager is DEAD CODE on the GLES2 client (only runs under
  EDT_BURNINGSVIDEO — loadZoneStep11_Env gate); on the real GPU path
  trees build correctly via ZoneMeshBuilder::buildTexturedMesh
  (H02d-correct) and then a WIND-SHADER pass UNCONDITIONALLY CLOBBERS
  MaterialType (`renderer_zone_stream.cpp:559-576`,
  `renderer_zone_load.cpp:2288-2334`) with no masked/ETC1-vs-RGBA
  classification → companion variant with unbound unit 1 → opaque-white
  fallback → solid quads. Ground truth: nekpine.bmp is WLD
  TransparentMasked with REAL DXT5 alpha (47/53% hard cutout) — NOT
  citywlivy-class. P20's ed310e1 tree-path change is a correct H02d
  mirror but dormant on GLES2.
- **Door/entity setShaderMaterialTypes gap — FIXED by P21 (`6bde3e9`,
  2026-07-28)**: 2→3-arg RGBA variant plumbed through door_manager +
  entity_renderer + all call sites. REAL for doors: PAWHOOK chain doors
  (pchain.bmp, 6 placed instances in qeynos2; masked, RGBA-decoded)
  previously got EMT_TRANSPARENT_ALPHA_CHANNEL_REF=14 — NOT one of the
  GLES2 driver's compiled programs — now get the compiled RGBA
  alpha-test shader (258); numeric MaterialType trace is the evidence
  (door too small for salient stills, honestly noted). Entity side
  LATENT: correctly wired but currently unreachable, because of the
  next item.
- **NEW (P21 audit): RaceModelLoader/equipment_model_loader IGNORE
  textureMasked()** — `mesh_building.cpp` (both buildMeshFromGeometry
  AND buildCompositeMeshFromGeometry) and `equipment_model_loader.cpp`
  hardcode EMT_SOLID for every material, never consulting the WLD
  masked flag. Masked character-archive textures exist (zomch0001/
  zomlg0001 befallen zombies, beahe0008 qeynos2) and render OPAQUE
  today via this upstream gap. If/when fixed, P21's classification in
  entity_renderer is already wired to route them correctly. Needs its
  own unit with live visual evidence (zombie flesh gaps / bone helm
  holes are era-authentic transparency candidates) + composite-path
  care (composite meshes share one buffer — per-material alpha-test
  may not compose; check before promising).
- **New unrelated bug spotted**: magenta/black checkerboard
  missing-texture placeholder on some qeynos2 rooftops/awnings
  (approach_01/02 stills). Not chased.
- **Stale-crash retraction for the record**: the P08d-retake qeynos2
  first-frame mergeZoneDraws SIGSEGV does NOT reproduce on current fable
  (direct observation, stock orangepi preset, clean session +
  screenshots) — consistent with P10b's fix. No capture preset
  workaround needed anymore.

## Eviction hygiene LANDED (P17, 2026-07-27) — closes the P15 follow-ups

- **Cross-zone accumulation FIXED, mechanism deeper than P15 thought**:
  ConstrainedTextureCache::clear() existed and was correct, but its sole
  trigger — the queued "/unloadzone_internal" RendererCommand pushed by
  CleanupZone() — is only drained by PreRenderTick(), which
  Application::render() SKIPS on every early-return branch taken during
  a zone transition (handshake + loading-thread-active). The unload sat
  unapplied until after the NEW zone finished loading. Fix: unloadZone()
  now runs synchronously in Application::loadZoneGraphicsOnThread() (on
  the loading thread, which owns the GL context) before the new zone
  uploads anything; the queued push is removed (late-firing, it would
  have wiped the NEW zone's textures). PROOF: 5-hop multi-hop session
  (befallen↔qeynos2×2 + gfaydark) with per-zone ledger reset — eviction
  counts flat and zone-local (befallen 0, qeynos2 30, every visit), no
  accumulation.
- **Entity-node dangling-ITexture* guard LANDED** (P15 bug (a)):
  EntityMeshTextureGuard mirrors P10b's RegionMeshTextureGuard, covers
  EQAnimatedMeshSceneNode materials_ + instanceMesh_. Verification also
  surfaced a SECOND dangling-pointer hole: RaceModelLoader's shared
  per-race mesh cache's own ZoneMeshBuilder::textureCache_ was never
  cleared on zone unload (clearTextureCache() had zero callers) — wired
  into clearMeshCaches() + defensive isLiveTexture() at the crash site.
- **Stale-zone-name-vs-position snapshot race FIXED** (P15 bug (b)):
  SetZoneBspTree() tags the tree with its zone; CreateZoneLoadSnapshot()
  only trusts a tag-matching tree, else falls back to the safe
  no-tree-yet path instead of the SIZE_MAX FATAL.
- mergeZoneDraws spot: qeynos2 still correctly gated OFF — its own
  standalone footprint exceeds the 64MB budget (not cross-zone leakage;
  the P13b "would merge ACTIVATE in qeynos2 post-fix" hypothesis is
  answered: no, qeynos2 is genuinely over budget on its own).
- **NEW (unfixed, pre-existing, graceful)**: gfaydark zone-in FATAL —
  missing race model data, race=330; unrelated to eviction work, needs
  its own look (likely race_codes.cpp mapping gap class). T53
  (2026-08-05): possibly the same class as the still-open `race=415`
  missing-model FATAL confirmed by T32's 2026-08-03 re-test elsewhere in
  this file — not verified as the same root cause, flagging the
  connection for whoever picks either up.

## Eviction mystery SOLVED + re-zone crash fixed (P15, 2026-07-27) — follow-ups CLOSED by P17 above

- **Re-zone snapshot crash FIXED**: ProcessDeferredZoneChange signaled
  the zone load AFTER CleanupZone cleared m_current_zone_name → empty
  snapshot zoneName → loadZoneGraphicsOnThread FATAL. Path shared by GM
  #zone AND real zone-line crossings. Fix: OnGameStateComplete is now
  the sole re-zone trigger (matches initial zone-in). 8 crash-free
  teleport sessions + one full clean befallen→qeynos2 transition.
- **EVICTION MYSTERY SOLVED — REPRODUCED**: cross-zone accumulation.
  ConstrainedTextureCache (and the ledger) is NEVER reset across zone
  unloads — befallen→qeynos2 in one process = 27 evictions (all
  zone_region, cumulative uploads 817). P10b's 71-eviction scenario =
  a longer multi-zone session. **FOLLOW-UP UNIT NEEDED: release the
  outgoing zone's textures at zone unload** (the 1999-honest behavior;
  also shrinks the merge-activation gate's false trips).
- **TWO NEW BUGS found (documented, unfixed, blocked the 3+ hop
  verification)**: (a) intermittent dangling-ITexture* in ENTITY mesh
  materials post-eviction — the P10b RegionMeshTextureGuard protects
  REGION nodes only; entity nodes have the same hazard class and need
  the same guard; (b) a stale-zone-name-vs-position snapshot race
  (separate from the fixed bug).
- mergeZoneDraws regression spot: clean (matches P10b behavior).

## KPI STATUS — befallen at the 30fps line (P11b, 2026-07-27)

- **The true composite magnitude (animated production path, Pi,
  canonical befallen e205 replay): p95 −36.9%, avg −54.6%, entity
  draws/entity −66%.** Retires P08c's −33.1% (stand-in path, own cull
  bug, not a real dispatch) and P11's ~0% (static route — composite
  meshes only build under animation).
- **Full stack (merge + animated composites): avg 25.11ms = 39.8 fps
  (8.2ms UNDER budget — first time in the campaign); p95 35.32ms =
  28.3 fps (2.02ms over).**
- **Residual is now GPU-BOUND**: at full-stack p95, GPU sync 17.5ms
  (49.6%) vs submission 9.3ms (26.5%) — a reversal of every prior
  unit's CPU-submission-dominant finding. The p95 tail is driven by
  bf472's dense zone-geometry pose specifically.
- **P14 (2026-07-27) MEASURED THE OCCLUDER LEVER UNDER THE STACK — IT
  CLOSES THE GAP**: occlusionMaxOccluderRegions 16→256 takes whole-run
  p95 from 35.320ms to **32.313ms (0.99ms UNDER the 30fps line)**;
  uncapped ties 256 (saturated). bf472: total −10.6%, GPU-sync −9.5%,
  submission −23.1%, CPU occlusion cost +0.44ms. Reverses P11's
  pre-stack "not worth it" verdict. LANDED 2026-07-27 (blessed by Will;
  note: the C++ preset default was the real landing site — no JSON
  plumbing exists for this key, contrary to P14's claim). With it: befallen avg 39.8fps / p95 30.9fps —
  **the 30fps KPI fully met at p95 for the first time.**
- Calibration notes: N=205 → median 38 visible holds on the animated
  path (P08b's 4.5-5x inflation bug is gone post-P12b); watch the
  --frames-vs-path-length placement-density artifact documented in
  P11b's review.
- ~~NOTE: achieving these numbers in live play requires the
  mergeZoneDraws preset flip (composites already live).~~ — **FLIP
  LANDED 2026-07-27 (`a8a9b03`)**: `mergeZoneDraws: true` is in the
  shipped `orangepi` block of `config/constrained_presets.json`
  (verified in tree 2026-08-02), so live play gets the full measured
  stack — merge + composites + occluder-256. Struck 2026-08-02.
- CAUTION (P51, 2026-08-01): the p95 numbers in this section come from
  pre-`4386398` binaries and carry the fixed-function-entity benchmark
  bias. **34.3-34.6ms is the canonical KPI-guard band now**, not the
  32.24-32.5 figures above. Kept as the historical record of the arc.

## ConstrainedMeshCache is never constructed — RESOLVED BY REMOVAL (T67, 2026-08-11)

- Exhaustive grep (verified independently by orchestrator): no
  construction site exists for ConstrainedMeshCache anywhere in src/ —
  the lazy/progressive region-mesh streaming documented in
  src/client/graphics/CLAUDE.md ("mesh cache active → texture pixel
  data retained", 24MB budget, eviction) is NOT wired into production;
  regions are always built eagerly. Docs overstate reality; either wire
  it (with the P10 full shared-VBO reconciliation) or trim the docs and
  the dead conditional paths (renderer_zone_stream.cpp/renderer_zone_draw
  guard on a pointer that is never non-null).
- **T67 (Will's decision, 2026-08-11): removed the class from production**
  rather than wiring it — the eviction pressure it exists to relieve was
  never reproduced beyond one unconfirmed observation (P10b's "71
  evictions", vs zero across four later independently-measured
  configurations), it had been independently written twice (this class,
  plus once more on the deleted `mem` branch) and wired zero times, and
  there was no way to even turn it on (`meshMemoryBytes` was computed per
  preset and consumed by nothing). `constrained_mesh_cache.h`/`.cpp`
  (385 lines) deleted outright, along with ~40 guarded call sites and an
  entire background-mesh-build subsystem that existed only to feed it
  (see `refactor/T67_remove_constrained_mesh_cache.md`'s Review for the
  full per-file accounting). The one genuinely-live piece,
  `estimateMeshSize()`, was relocated verbatim to
  `estimateMeshSizeBytes()` in `include/client/graphics/eq/zone_geometry.h`
  / `.cpp` rather than deleted. Full protocol verification bar passed,
  including the visual gate (byte-identical rendering — this was
  dead-code removal, not a behavior change) and both x86/ARM builds.
- **This closes the item WITHOUT answering whether progressive
  region-mesh streaming is ever wanted.** If Pi memory pressure is later
  actually measured (it has not been, across five independent sessions'
  worth of attempts), the implementation is fully recoverable, not
  reimplemented from scratch: last shipped at commit `49844c72`
  (2026-03-08, "PVS direct index lookup, SimWorker copy elimination, DRM
  page flip fix, stderr buffering"), `include/client/graphics/
  constrained_mesh_cache.h` + `src/client/graphics/constrained_mesh_cache.cpp`
  — `git show 49844c72:src/client/graphics/constrained_mesh_cache.cpp`
  retrieves it in one command (this is `fable`'s own history, not the
  deleted `mem` branch — permanently recoverable, no archive tag needed).

## P10b's "71 evictions" scenario still unreproduced; new zone-transition crash found (P13b, 2026-07-27)

- P13's own audit (three measurement runs) and P13b's bounded side-quest (two
  more, ledger-instrumented) — four independent single/multi-zone qeynos2
  configurations total across two units — have now all measured **0
  evictions** under the shipped 64MB `orangepi` budget, whether composites
  are on/off, `mergeZoneDraws` is on/off, or a shipped vs custom-mirrored
  preset is used. P10b's original "71 evictions" (befallen→qeynos zone
  transition, mergeZoneDraws crash repro session) remains the only recorded
  observation of it.
- P13b's multi-zone-in-one-process attempt (befallen → `#zone qeynos2` GM
  teleport, no teardown — the leading hypothesis per P13's own Deviations:
  "evictions accumulating across MULTIPLE zone visits in one process
  lifetime") hit a **new, reproducible crash** before it could observe
  qeynos2's cumulative eviction count:
  `[FATAL] [GRAPHICS] loadZoneGraphicsOnThread: snapshot has empty zone name
  — aborting` (SIGSEGV), triggered specifically by the `#zone <shortname>`
  GM-command zone-transition path. Not investigated (out of P13b's bounded
  side-quest scope) — this is now the concrete blocker for whoever next
  attempts the multi-zone-accumulation repro: either fix this crash, or find
  a different zone-transition mechanism (a real client-walked zone line
  instead of the GM teleport command) that doesn't hit it.
- The eviction-tracing ledger machinery (P13's `TextureUploadLedger`) is
  fully built, verified working, and ready to capture the exact
  victim-order/thrash reconstruction the moment ANY future session actually
  reproduces eviction pressure — nothing further needs building, only a
  session shape that reaches it.
- **CLOSED by P15 (2026-07-27)**: the `#zone` empty-zone-name crash above is
  fixed (`ProcessDeferredZoneChange` in `src/client/eq_zone.cpp` no longer
  sets `m_zone_load_requested` immediately after `CleanupZone()` clears
  `m_current_zone_name` — that premature signal raced the game thread's
  snapshot creation ahead of the new zone's actual data arriving via
  `OnGameStateComplete()`, which is now the sole trigger for both initial
  zone-in and every re-zone, GM-teleport or zone-line alike). And the
  eviction mystery is **answered**: cross-zone texture accumulation
  reproduces evictions — a `befallen` (safe spot) → `#zone qeynos2` session
  hit 27 evictions (all `category=zone_region`, i.e. `befallen`'s now-stale
  geometry textures evicted to make room for `qeynos2`'s), cumulative
  uploads=817, 0 thrash. Root mechanism: `ConstrainedTextureCache` /
  `TextureUploadLedger` are never reset on zone unload (persist for the
  process lifetime), so a second zone's uploads share the first zone's
  budget headroom. Full ledger + condensed console evidence in
  `benchmarks/audits/p15_eviction/`. See `refactor/P15_zone_crash_eviction.md`
  Review for the complete writeup, including two NEW, separate, NOT-fixed
  bugs uncovered while verifying (both out of P15's scope, flagged below).

## Entity mesh materials hold dangling ITexture* after eviction — FIXED by P17 (2026-07-27)

- **CLOSED.** P17 landed BOTH halves: (a) `EntityMeshTextureGuard`
  (`renderer_zone_merge.h/.cpp`, the entity sibling of P10b's
  `RegionMeshTextureGuard`, owned by `IrrlichtRenderer` as
  `entityMeshTextureGuard_`) covering `EQAnimatedMeshSceneNode`'s
  per-instance `materials_`/`instanceMesh_`; and (b) a defensive
  `isLiveTexture(currentTex)` check at the exact call site named below
  (`animated_mesh_creation.cpp`, now ~line 456) — verified present in
  tree 2026-08-02. The guard alone was NOT sufficient and P17 says so:
  `buffer` there is the race's SHARED RaceModelLoader-cached base mesh,
  a different object set than the guard covers, and it can go dangling
  via the zone-unload full `clear()` even with zero LRU evictions —
  which is exactly why the original report saw crashes at a 256MB
  no-eviction budget. Live repro (befallen → qeynos2 → befallen 2nd
  visit) confirmed the mechanism before the fix. Struck 2026-08-02.
  Original report follows for the record:

- `RaceModelLoader::createAnimatedNodeWithEquipment` (`src/client/graphics/eq/animated_mesh_creation.cpp:433`)
  reads `currentTex->getName()` off an `ITexture*` cached on a mesh buffer's
  material without checking whether `ConstrainedTextureCache` has since
  evicted (and thus destroyed) that texture. Intermittent SIGSEGV/SIGABRT
  (heap-corruption-class, not always reproducible — sometimes the freed
  memory hasn't been reused yet) during entity mesh building whenever
  eviction pressure hits while building a populated zone's entities.
  Confirmed independent of eviction volume — a 256MB no-eviction
  `--constrained` budget still crashed at the same call site, so this is a
  general dangling-reference bug, not strictly gated on eviction. **Region**
  meshes already have the equivalent protection (P10b's
  `RegionMeshTextureGuard`, confirmed live in this unit's regression-spot
  log: `"RegionMeshTextureGuard: swapped N region-mesh material
  reference(s) to evicted texture -> placeholder"`) — entity meshes have no
  such guard. Shape of the fix: extend an equivalent guard/listener to
  `EntityRenderer`/`RaceModelLoader`'s cached `ITexture*` references, or
  have `ConstrainedTextureCache::evictTexture()` null out entity-held
  references the same way it already does for region meshes. Not attempted
  here (P15 is scoped to the `#zone` snapshot crash + eviction *recording*,
  not eviction fixes) — flagged for whoever picks up the eviction fix, since
  it's the direct answer to "why does eviction matter beyond a budget
  number."

## "Player BSP region is SIZE_MAX" with stale zone name (found by P15, NOT chased)

- Separately from the above, one `#zone` verification run hit `[FATAL]
  [MAIN] Player BSP region is SIZE_MAX! pos=(428.00,-74.00,3.00)
  zone=befallen` — i.e. the *target* zone's (qeynos2) coordinates paired
  with the *source* zone's name/BSP tree still resident, inside
  `CreateZoneLoadSnapshot()`. This is a different inconsistent-snapshot
  timing window than the one P15 fixed (that one was empty-name; this one
  is stale-name-with-new-position) and was not root-caused — reproduced at
  higher debug levels (`-d 5`) and under `gdb`, not reliably at `-d 3`,
  suggesting a timing-sensitive race in how position vs. zone-name updates
  land relative to snapshot creation across the `#zone`/re-zone sequence.
  Out of P15's scope (the assigned crash's signature — empty zone name —
  never recurred once fixed); flagged for whoever next touches zone-load
  snapshot timing. Per this same plan's own precedent (the pre-existing
  BSP-FATAL at Testchar's documented restore coordinate), not chased
  further here.

## Pale fog-colored flat shapes in gfaydark/qeynos2 goldens (H02c/H02d observation)

- Distant geometry renders as flat fog-colored (pale) shapes against the
  darker sky — visually jarring; likely fog color/sky color mismatch or fog
  applied to geometry but not sky. Pre-existing (byte-identical across the
  alpha fixes); visible in gfaydark goldens. Investigate fog pipeline vs
  zone fog color data. Will's eye flagged the general area; needs its own
  unit.

## Production draw-order nondeterminism — CLOSED by Q02 (2026-07-25, see refactor/Q02_draw_order_key.md)

- Both pointer-key sites in renderer_zone_draw.cpp now use the H02b
  FNV-1a texture-name hash (`textureGroupId()`, byte-for-byte mirror of
  zone_bench_loader.cpp's — keep the two copies in sync). Straggler sweep
  confirmed no other pointer-derived draw/sort keys in src/ or include/.
  Unblocks the future full-app llvmpipe golden tier.

## RDP frame-update race — ROOT-CAUSED (T03b) — **FIXED by T35 (2026-08-04)**

**CLOSED, T53 (2026-08-05) verification.** This entry's own title said "fix
still open" but the fix landed and the entry was never updated.
`RDPServer::stop()` (`src/client/graphics/rdp/rdp_server.cpp:325-342`) now
does `running_.store(false)` → `listenerThread_.join()` → `Close()`/`free()`
— join-before-close, exactly T03b's prescribed ordering. Commit `a835969`
("Merge T35: fix the RDPServer::stop() use-after-free, retire its waiver").
Per `refactor/EXECUTOR_PROTOCOL.md`'s verification-bar note, the
`RDPServerFrameTest.UpdateFrameMultipleTimes` "intermittent" ctest waiver
was formally retired in the same unit — a real crash, not a flake, as this
board warned. See `refactor/T35_rdp_stop_uaf.md` Review for the full
repro/verification evidence. Original report follows for the record:

- **RDPServerFrameTest.\* segfaults intermittently** (~once per <=10
  stress repetitions, confirmed on unmodified fable during F18
  integration; also seen under parallel ctest load in F01/F09/F24 runs).
  A real race in the RDP path, not test noise.
- **ROOT CAUSE FOUND — T03b (2026-07-28), handed to the perf session in
  `refactor/TESTING_HANDBACK.md` Round 3. Do not re-investigate from
  scratch.** It is a **use-after-free in `RDPServer::stop()`**: `Close()`
  runs before `join()` while `listenerThread()` is still holding fetched
  handles. Evidence was 12 kernel GP faults at an identical libwinpr3
  offset (the symbol resolution was double-checked against
  `readelf -lW` — the kernel reports the containing segment base, and
  libwinpr3's executable LOAD starts at vaddr `0x48000`). The correct
  ordering is `running_.store(false)` → `join()` → `Close()` → `free()`.
- **Still reproduces at fable tip (2026-07-31, testing session):**
  1-in-5 standalone in one worktree (`UpdateFrameAfterStart`) and under
  parallel ctest load in another (`UpdateFrameMultipleTimes`). Which
  family member fires varies with timing — treat the whole
  `RDPServerFrameTest`/`RDPServerLifecycleTest` set as one flake family
  until the ordering is fixed.
- **Standing caution (T03b lesson):** this sat behind a "known
  intermittent flake" waiver for weeks. A segfault is never a flake; it
  is a crash with a timing precondition.

## TWO `state::GameState` instances exist, unconnected — the structural cause of the whole dead-state bug class (T39, 2026-08-04) — NOT FIXED, needs a ruling

**2026-08-25 — DECIDED (Will): projection model is the contract; Option B COMPLETE.** `docs/decisions.md` 2026-08-25 entry. T218 `97c3d30a` (AFK/anon/roleplay projected — toggles can now turn off, live before/after) and T219 `b795a26e` (doors projected into BOTH GameStates, third `m_door_state_manager` copy DELETED, `/door` live-verified via a second client) close the last T78 breakages. ****Option A COMPLETE: T220 `f906f697` → T221 `eae2df68` → T227 `88b5e614`. ONE instance (`eq.h`), projection switch deleted, live table + TSAN=0 in `review/T227/`. See docs/decisions.md 2026-08-25 (second entry). CLOSED.**** — crux: `GameStateSyncBridge::applyEvent` runs on the RENDER thread in graphics builds (`application.cpp:1424` → `PreRenderTick`) while the action layer reads on the GAME thread with no lock — a pre-existing race that convergence must resolve first (fact base: agent census 2026-08-25, see resume-state memory). Remaining gap on the Application side: player position/heading (benign; the only reader is the dead `InputActionBridge` mouse-look path, T218).

**This is the root the entry below was a symptom of.** T39 found it while
looking for where to wire `CombatState`, and it invalidated the wiring
site the plan had suggested.

- `EverQuest::m_game_state` — a plain value member (`eq.h`).
- `Application::m_gameState` — a **separately heap-allocated**
  `std::unique_ptr<state::GameState>` (`application.cpp:179`).

They are distinct objects, each auto-wiring its own submodules to **its
own `EventBus`** (`game_state.cpp`). **Nothing connects them** —
orchestrator-confirmed: `application.cpp` and the game modes never call
`EverQuest::GetGameState()`.

Who reads which:

| instance | consumers |
|---|---|
| `Application::m_gameState` | `ActionDispatcher`, `CommandProcessor` (incl. `cmdHail`), the game mode → `ConsoleRenderer`, `InputActionBridge` |
| `EverQuest::m_game_state` | the `eq_commands.cpp` readers, the Phase 7.x `sync*State()` calls inside `EverQuest` |

So "populate the state layer" is ambiguous until you say *which* state
layer — and populating the wrong one produces exactly the observed
symptom: setters that run, state that looks populated, and readers that
still see nothing. That is the `/assist` bug (T28) and the `cmdHail` gap
(T39) in one sentence, and it is why those two readers behaved
differently despite reading "the same" field.

T39 worked with it rather than against it (wired via
`Application::syncCombatState()`/`syncInventoryState()`, mirroring the
existing `syncPetState()`/`syncNPCInteractionState()` polling pattern),
which was the right call for a unit that was not scoped to re-architect
this. But the duplication remains, and it will keep generating this bug
class every time someone reasonably assumes there is one game state.

**Needs a decision, not a drive-by fix.** Options, roughly ascending:
(a) leave both, document loudly at both declarations so the next person
does not assume; (b) make `Application` hold a reference to `EverQuest`'s
instance (or vice versa) so there is one object; (c) collapse to one
owner properly, which touches the action layer, the modes, the bridges
and the Phase 7.x syncs. (b) sounds small and is not: the two are
constructed at different times with different lifetimes, and the
headless/graphical modes differ in what exists when.

**T78 (2026-08-12) ANALYSIS DONE — read `refactor/T78_two_gamestate_analysis.md`'s
Review before scoping the next unit off this entry.** Full census of both
instances (every construct/hold/read/write site, file:line), what each
genuinely serves, the four options with costs/risks/unblocks, and — the
finding that changes this entry's shape — **neither instance is vestigial,
so "delete one" is not available**, and the split currently breaks more than
the position-write case this entry and T55/T58 described: `Application`'s
copy is never fed `entities()`/`doors()`/`group()` at all, so `/face`,
`/who`, `moveToEntity`/`followEntity`, `/disband`, and dispatcher-gated
`/decline`/accept-invite all silently no-op or return a misleading "not
found" in production today, independent of whether the two instances are
ever connected. Doors are broken worse than a two-instance split — a third,
unconnected `DoorStateManager` on `EverQuest` holds the only real door data,
and neither `GameState`'s `doors()` is ever populated by anyone. T78's own
recommendation: don't pick (a)/(b)/(c) wholesale — close the entities/group
gaps cheaply via a `GameStateSyncBridge` extension, fix doors as its own
unrelated item, and scope full convergence (b)/(c) as a separate, larger,
deliberately-scoped unit given its ~90+ call-site regression surface. Still
Will's decision, not foreclosed by this note.

**T81 (2026-08-12) ENTITIES CLOSED — first category of T78's recommended
`GameStateSyncBridge` extension.** `Application::m_gameState->entities()` is
now fed for real: `EntityStateSnapshot` (new, full `state::Entity`, published
from `EverQuest::SyncEntityToGameState()`, add-or-replace) for spawn/self;
existing `EntityDespawned` for despawn; existing coalesced `EntityMoved` for
position (no per-tick full snapshot — this stayed the volume-bounded path
per D24); existing `EntityAppearanceChanged` for post-spawn equipment
changes (`ZoneProcessWearChange()` mutates equipment without re-invoking
`SyncEntityToGameState()`, so a spawn-only snapshot would go stale — see
`refactor/T81_entity_projection.md` Review); existing `ZoneUnloading` to
clear the projection on zone change (the source-side clear,
`eq_zone.cpp:270`, only fires events on `EverQuest::m_game_state`'s own
private bus, which nothing production reads, so it never reached this
decorator any other way). `/face`, `/who`, `moveToEntity`/`followEntity`,
and the NPC half of `interactNearest()` now work against real data with
**zero changes to `ActionDispatcher`/`CommandProcessor`** — confirming this
was a projection gap, not a deeper bug. Still open from T78's list: **doors**
(worse than a two-instance split — a third, unconnected `DoorStateManager`
holds the only real data, per T78) and **group** (both need their own
reconstructibility argument, not assumed from this unit's), plus **combat**,
which stays on its existing per-tick `Application::syncCombatState()` poll
(T78 confirmed populated, not broken — not touched by this unit).

**Live leg CLOSED (T82, 2026-08-12), with an important correction to the
paragraph above.** `/face` was confirmed live against T81's projection
exactly as claimed — `ActionDispatcher::faceEntity()` reads
`m_state.entities()` directly and succeeded live with zero code changes.
**`/who`, `moveToEntity`, and `followEntity` do NOT "work against real
data" in the sense this paragraph implies — they are unreachable through
any live input path.** `Application::DispatchSlashCommand()`'s registry-1
stage (`EverQuest::m_command_registry`) always runs first, and `who`/
`moveto`/`follow` are ALL also registered there under the identical name,
reading `EverQuest::m_entities` (a separate, pre-existing, never-broken raw
map) — so `CommandProcessor`'s versions (the ones actually reading T81's
projection) are permanently shadowed, confirmed live by the exact output
text observed (registry 1's `"[{}] {} (Lvl {})"` / `"Moving to entity:
..."` / `"Following: ..."` format, never registry 2's distinct text). This
is not a T81 defect — T81's own scope was the projection and its consumers,
which are correctly wired and unit-tested; the shadowing is a pre-existing
command-dispatch property neither T81 nor this unit touched. `/list` looks
like a fourth live proof (not shadowed, dispatches to `registry=action`)
but also does not exercise the projection: `ActionDispatcher::
listEntities()` forwards cross-thread to `EverQuest::ListEntities()`, the
same raw-map path as `/who`. Full evidence:
`refactor/T82_live_verification_session.md`'s Review, `review/T82/`.
`interactNearest()` was not exercised (no slash-command binding at all,
hotkey-only — same reachability class, one level further).

**T104 (2026-08-14) COMBAT CLOSED — the last category of T78's Option B
migration apart from doors.** `Application::syncCombatState()` (the
per-tick poll T78 confirmed populated `GameState::combat()`, and which
T81 explicitly left untouched) is **deleted**, along with its three
dedupe members (`m_lastCombatTargetId`/`m_lastCombatTargetHp`/
`m_lastAutoAttacking`). Replaced by two events feeding
`GameStateSyncBridge`: `TargetChanged` (already existed and was already
published end-to-end for the graphics/renderer target HUD before this
unit — `eq_bridge.cpp`'s `TargetIntent` handler, `OnSpawnRemovedGraphics`,
and `eq_entities.cpp`'s `ZoneProcessMobHealth()` re-publish when the
updated entity is the current target — but nothing fed it into
`Application::m_gameState->combat()`, and most `SetTarget()`/
`ClearTarget()` callers — `/target`, `targetNearest*`, `cycleTargets`,
`assist`/`duel`/`taunt`, auto-hunt — never published it at all) and
`AutoAttackChanged` (new typed event). The fix moves the publish into
`CombatManager::SetTarget(uint16_t)`/`ClearTarget()`/`EnableAutoAttack()`/
`DisableAutoAttack()` themselves (`combat.cpp`) — the sole choke points
that mutate `m_current_target_id`/`m_auto_attack_enabled` — so every
caller is covered without touching any of them individually, and the
target-HP-while-still-targeted correlation risk (the plan's own "hard
part") is resolved by **reusing** the pre-existing
`ZoneProcessMobHealth()` re-publish rather than adding a second,
competing correlation mechanism. Falsification test held: **zero changes
to `CommandProcessor`/`ConsoleRenderer`** — both already read
`GameState::combat()` the same way; only the feed changed. Live-verified
(`review/T104/`): a real, server-driven HP change (100%->72%->36%->0%,
via the sanctioned GM command `#set hp <amount>` — real melee produced
zero `ZoneProcessDamage` packets against Testchar's test account, found
live to have **zero items equipped in any slot**, an environmental gap
unrelated to this unit) produced four fresh `TargetChanged` publishes
with the correct decreasing HP each time; the kill automatically
triggered both `TargetChanged(clear)` and `AutoAttackChanged(false)` via
`ProcessCombatRound()`'s pre-existing `hp_percent<=0` branch. One
command-surface gap found and flagged, not fixed (out of territory):
**no slash command reaches `EqActionHandler::clearTarget()`** —
`ActionDispatcher::clearTarget()`'s only production caller is
`InputActionBridge` (a keybinding); the headless verification of the
clear path relies on this unit's own unit tests
(`GameStateSyncBridgeTest.TargetChangedWithZeroSpawnIdClearsTarget`,
both-directions-proven) plus the live kill's automatic clear. `TargetHPUpdatedData`/
`GameEventType::TargetHPUpdated` remain defined but **still have zero
production publishers** (confirmed unchanged by this unit) — `TargetChanged`
already carries `hpPercent` and is what both the pre-existing HP-while-
targeted re-publish and this unit's projection wiring use; flagged, not
touched, since publishing it would be scope creep this unit's territory
didn't ask for. Full detail: `refactor/T104_combat_poll_to_bridge.md`'s
Review, `review/T104/README.md`.

**Board status after T104: entities (T81), group (T99), and now combat
are all migrated off polls onto typed bridge events. Doors remain the
sole open category** — per T78's finding, worse than a two-instance
split (a third, unconnected `DoorStateManager` on `EverQuest` holds the
only real door data; neither `GameState::doors()` is ever populated by
anyone) — its own unit, its own reconstructibility argument, separately
blocked on routing.

## Dead parallel state copies in the state layer, WITH LIVE READERS (orchestrator, 2026-08-02) — **ALL THREE CLAIMS CLOSED (T28/T39/T42/T49); this is the board's own worked example of why re-verification matters**

**CLOSED, T53 (2026-08-05) verification** (independently re-confirmed, not
just taken from the T53 plan's own framing):
1. `CombatState`'s target half — **populated**, confirmed via
   `Application::syncCombatState()` at `include/client/application.h:354`
   / `src/client/application.cpp:1785`; `world_state.cpp`'s sibling fields
   confirm the general pattern.
2. `InventoryState` occupancy — **wired**: `setEquipmentOccupied()`/
   `setGeneralOccupied()`/`setBankOccupied()` now have real production call
   sites at `src/client/bridge/game_state_sync_bridge.cpp:139-143`.
3. `WorldState::fireZoneChangedEvent()`'s pending-zone fields — **fixed**:
   `src/client/state/world_state.cpp:25-28,117-118` now uses
   `m_zoneArrivalX/Y/Z/Heading`, matching the fix this entry describes.

All three sub-claims hold up under direct grep, exactly as this entry
originally reported. No correction needed to the entry itself — it was
already accurate and evidenced. Kept as CLOSED, not deleted, per this
board's own governing rule.

Found while scoping a fix for T02's three pinned state-layer findings.
The T02 pins are accurate about what they assert, but the framing
("these events don't publish") understates and partly misdirects: for
two of the three, the *event* is not the problem — the state those
events would describe is never populated in the first place, and the
same dead state has live readers. All verified by grep against fable
tip `557a488`; commands in this entry, re-runnable.

**1. `state::CombatState`'s target half is never populated — and four
slash commands read it.**
- `combat().setTarget()` / `clearTarget()` / `setAutoAttacking()` /
  `updateTargetHP()` have **zero production call sites** (only tests
  call them). So `fireTargetChangedEvent()` being an empty stub is
  moot — the function it stubs is unreachable in production anyway.
- The authoritative target is `CombatManager::m_target_id`
  (`GetTargetId()`/`SetTarget()`), which is what `eq_bridge.cpp` uses
  throughout (~lines 763, 832, 906, 958, 1252, 1292, 1373).
- **But five production sites read the dead `CombatState` copy**:
  `eq_commands.cpp:887, 945, 974, 1020` (`combat().targetId()`) and
  `action/command_processor.cpp:771` (`combat().hasTarget()`).
  `eq_commands.cpp:887` is `/assist` with no args: it reads
  `targetId()`, gets 0 because nothing ever sets it, and prints
  **"You don't have a target."** even when the player does. The other
  three read sites are the same class.
- `TargetChanged` is meanwhile **already published** from three real
  sites (`eq_bridge.cpp:589` clear, `eq_bridge.cpp:771` set,
  `eq_entities.cpp:1728` HP update), and `eq_action_handler.cpp:21`
  documents a deliberate no-duplicate decision ("D20e1: TargetChanged
  event is already published by ProcessBridgeIntents"). ~~So naively
  making the stub fire — after wiring the setters — would DOUBLE-publish
  it.~~ **CORRECTION (orchestrator, 2026-08-04): that double-publish
  claim was WRONG, and I wrote it without checking. The two paths are
  DIFFERENT CHANNELS with DISJOINT CONSUMERS**, verified:
  - those three sites `m_bridge->pushEvent(...)` onto the
    **GameStateBridge**, consumed by `IrrlichtBridge` (renderer) and
    `ConsoleBridge`;
  - `CombatState::fireTargetChangedEvent()` would `m_eventBus->publish()`
    onto the **EventBus**, consumed by `ConsoleRenderer`
    (`console_renderer.cpp:107`, live in headless mode);
  - **nothing else publishes `TargetChanged` to the EventBus at all**,
    the bridges do not `subscribe()` to the EventBus, and no code
    forwards between the two. The EventBus is fed exclusively by
    `src/client/state/*.cpp`.

  So letting the stub publish is safe, and is in fact what every other
  state module already does. `eq_action_handler.cpp:21`'s no-duplicate
  note is about the bridge channel and does not apply. The migration is
  correspondingly smaller than this entry originally implied — it is
  "populate the state and let it publish", not "consolidate three
  publishers onto one".

  The stale source comment in `combat_state.cpp:68` claiming "we
  don't have a corresponding data struct" is simply wrong:
  `TargetChangedData` exists at `event_bus.h:384`.

**2. `state::InventoryState`'s occupancy half: same pattern.**
`setEquipmentOccupied()` / `setGeneralOccupied()` / `setBankOccupied()`
have zero production call sites, so the hardcoded
`event.hasItem = true; event.itemId = 0` at
`inventory_state.cpp:204-205` is real but unreachable. (The
*live* `InventorySlotChanged` events, enriched with display fields by
S2, come from the `eq_bridge` path, not from here.) No live readers
found for the occupancy arrays — the milder case.

**3. `state::WorldState::fireZoneChangedEvent()` IS live, and its
position is wrong.** Unlike 1 and 2, `world().setZone()` is genuinely
called in production (`eq.cpp:1909`). It publishes `ZoneChanged` on the
**EventBus** with `x/y/z/heading` read from the *pending-zone* fields
(`world_state.cpp:110-113`) rather than the actual arrival position.
Immediately afterward, `eq.cpp:1912-1922` pushes its OWN `ZoneChanged`
onto the **bridge** with the correct `m_safe_x/y/z`. Two channels, two
different payloads: `irrlicht_bridge`/`console_bridge` get the correct
one; the EventBus subscriber (`console_renderer.cpp:102,199`) gets
position `(0,0,0)` whenever `setZone()` runs without matching pending
fields. This one is a genuine live bug and matches its T02 pin exactly.

**Why this matters beyond the three pins**: the "Phase 7.x sync to
XState" comments show the intent was to migrate game state INTO the
state layer, and that migration was left half-done — setters written,
never wired, while readers were repointed at the new home anyway. The
`/assist` breakage is the visible cost. Whoever fixes this should decide
the direction deliberately (populate the state layer vs. repoint the
readers at the authoritative owner) rather than making stubs fire, which
would change nothing for 1 and 2 and would double-publish for 1.

Repro commands for a future session:
`grep -rn "combat()\.setTarget\|combat()\.clearTarget" src/ include/`
(empty) vs `grep -rn "combat()\.targetId()\|combat()\.hasTarget()" src/`
(five hits).

## willeq.db schema version is bumped but never validated (T27 follow-up, 2026-08-02) — **FIXED by T56 (2026-08-10)**

**CLOSED.** `WilleqDatabase::open()` (`src/client/willeq_database.cpp`) now
compares `metadata.version` against a new shared constant,
`EQT::kWilleqDbExpectedVersion` (`include/client/willeq_database.h`, = 2),
after reading it. On mismatch it `LOG_FATAL`s naming the rebuild command
(`tools/willeq_db_builder`) and returns false; both older-than-expected and
newer-than-expected are fatal (a newer db against an older client would
otherwise fail just as opaquely). `tools/willeq_db_builder.cpp`'s
`setMeta("version", ...)` call now reads the same constant instead of a
hardcoded literal, so builder and client cannot drift again. Missing
`metadata` table and missing-version-row-but-table-present behaviour are
unchanged (still ERROR+fail, and WARN+open respectively). Five new tests in
`tests/test_willeq_database.cpp` (`WilleqDatabaseVersionTest`) cover
matching/older/newer version and both missing-table/missing-row cases
against throwaway temp sqlite files — the tracked `data/willeq.db` was never
touched. See `refactor/T56_logging_db_board_fixes.md` Review for the full
verification transcript. Original report follows for the record:

T27 bumped `metadata.version` 1→2 when it added the `class_levels` BLOB
column, but **nothing reads that version** (the executor checked; it is
only logged). Meanwhile `willeq_database.cpp`'s spell queries now name
`class_levels` explicitly in their SELECT lists, so against a stale v1
database `sqlite3_prepare_v2()` fails with "no such column" and **every
spell lookup silently returns nothing** — a confusing "spell not found"
rather than "your database is out of date".

Low practical risk today because `data/willeq.db` is a TRACKED binary, so
any `git checkout`/`pull` of `fable` brings the matching v2 file. It
bites only someone with a locally-modified or separately-generated db —
e.g. a clone that regenerates with an older builder, or a worktree where
the file was manually replaced.

Small fix, worth doing under the 2026-08-02 automation-is-first-class
policy: validate `metadata.version` at open and fail loudly with an
actionable message naming the rebuild command, instead of degrading into
empty spell results. (Also note: the `willeq-benchmark` clone at
`b68c32a` predates all of this and its `build_errors_response.md`
channel file no longer exists — nothing was written to that clone.)

## Headless client aborts on shutdown and never exits when driven headlessly without a server (orchestrator, 2026-08-02) — **FIXED by T30 (2026-08-03)**

**CLOSED, T53 (2026-08-05) verification.** Commit `fa31ca97` ("T30: fix
headless client abort + non-exit on piped shutdown with no server"). Root
cause for the abort: `ConsoleInputHandler::shutdown()` only joined its stdin
thread inside `if (m_active.load())`, but `pipedInputThreadFunc()` also
clears `m_active` itself on EOF, skipping the join
(`src/client/input/console_input_handler.cpp:54-71`). For the never-exits
half: `Application::processPreConnectionLifecycleCommands()`
(`src/client/application.cpp:903,919`) now intercepts `/quit`/`/exit`/`/q`/
`/help`/`/?` *before* the `m_fullyConnected` gate at line 901, so `/quit`
piped with no server now works. Original report follows for the record:

Found while independently verifying T29 by actually piping commands into
the binary (rather than reading its diff). **Not caused by T29** —
attribution tested explicitly, see below.

Repro (no eqemu server running):
```
printf '/help\n/quit\n' | timeout 100 <build>/bin/willeq \
    -c /home/user/projects/claude/casterella.json --no-graphics
```
Two distinct problems, both reproduced **2/2**:

1. **`terminate called without an active exception` on stderr** — the
   classic signature of a `std::thread` destructor running while the
   thread is still joinable (neither joined nor detached). Same failure
   class as P36's self-join abort and T03b's RDP `stop()` use-after-free,
   both of which were real bugs.
2. **The process never exits** — `timeout` has to kill it (exit 124).
   `/quit` piped to stdin does not terminate the client here, because
   the input bridge that drains piped commands is gated on
   `m_fullyConnected` (`application.cpp:840`), so with no server the
   command is never dispatched at all.

**Attribution (this is the part that matters):** identical behaviour,
2/2 each, on BOTH the `task/T29-command-channel` binary AND the
already-merged `fable` tip (`51759f9`, i.e. with T26/T27/T28 in and T29
out). So T29 did not introduce it. Not tested against a pre-P36 binary,
so whether this is a regression since P36's "exit matrix all clean
(5×/camp exit-0 + SIGTERM + script-end + headless)" claim, or a case
that matrix never covered (headless + piped stdin + no server), is
open — worth knowing, because P36 explicitly asserted this was clean.

**Why it matters under the 2026-08-02 automation-is-first-class policy:**
every automated headless run that ends this way exits non-zero via
timeout and prints an abort, so **process exit codes are useless as a CI
signal** and every run leaves a core. That is squarely "in the way of
fully automated development and testing." Related limitation worth
fixing in the same unit: because piped commands are held until
`m_fullyConnected`, a script piping into a client that never connects
gets **no stdout at all** — no held/waiting line, no diagnostic, just
silence until something kills it. T29's stdout contract has no outcome
for "held, not yet connected".

Suggested shape: find the unjoined thread (the P36/T03b playbook —
`running_.store(false)` → `join()` → destroy), make `/quit` reachable
pre-connection or emit a held/waiting line so scripts can tell the
difference between "working" and "hung", and add a headless
piped-stdin-no-server case to the exit matrix so it stays fixed.

## Non-FATAL log output never reaches stdout during normal operation (T29 finding, 2026-08-02) — **FIXED by T31 (2026-08-03)**

**CLOSED, T53 (2026-08-05) verification.** Commit `c167acdc` ("T31: fix
non-FATAL log output never reaching stdout during normal operation").
`include/common/logging.h` now runs a dedicated `LogManager::drainLoop()`
background thread (started ~line 258) that calls `drainAllBuffers()` every
50ms unconditionally, instead of relying solely on `FlushThreadLog()`
(still called by `LOG_FATAL`, but no longer the only trigger). Confirmed in
current tree: `drainLoop`/`drainAllBuffers` both present and wired at
`include/common/logging.h:258-282`. Original report follows for the record:

`common/logging.h`'s per-thread double buffer (`ThreadLogBuffer`) only
flips its active/drain sides on `LOG_FATAL` or thread exit —
`FlushThreadLog()` is called by the `LOG_FATAL` macro and **nowhere
else**. The periodic drain thread (`LogManager::drainLoop()`, 50 ms)
only ever reads the *inactive* side, which nothing writes to for
non-FATAL levels absent an explicit flip.

**Net effect: `LOG_INFO`/`LOG_WARN`/`LOG_DEBUG`/non-fatal `LOG_ERROR`
never reach stdout during normal operation, regardless of
`--log-level`/`-d`.** That is how `AddChatSystemMessage`,
`ConsoleBridge::applyEvent`, and essentially all chat/system/combat
narration are implemented — so the client's own account of what is
happening is invisible.

Why this matters most, given the 2026-08-02 automation ruling: T29 gave
automation a way to *send* commands and a `CMD outcome=` line saying a
command was dispatched. But a script still cannot **observe the game's
response** — no chat, no system messages, no combat narration. Half the
loop is missing. This is very likely why T29's own loot-cycle
verification was so hard to drive, and it will block the next attempt
too.

Diagnosis confirmed, not guessed: a temporary local patch (self-flip in
`drainAllBuffers()` when the drain side is empty) made output appear,
and was **reverted before T29's commit** — `include/common/logging.h`
was outside that unit's territory. Also confirmed reproducible
independent of T29's changes; note the wrinkle that an isolated
standalone repro of `logging.h` alone works correctly, so something in
the full app's steady state (with no FATALs firing) is what exposes it.

Real fix needs its own unit: the naive always-flip has a wider blast
radius (drain-thread lock contention, `std::cout` write frequency on a
50 ms timer) than a drive-by change should carry. **Recommended as the
next automation unit after the shutdown fix.**

## Environment/asset blockers that stopped T29's live acceptance demo (2026-08-02)

Both hit while trying to complete the kill→loot cycle; neither is
T29's doing, and neither was fixed:

- **`race=415` model data missing → asset crash in graphics mode**,
  which blocked running the acceptance demo in graphical mode entirely
  (T29's transcript is `--no-graphics`). Same family as the recorded
  gfaydark `race=330` FATAL. Will's ruling wants "the same commands in a
  real session", and *real* means graphical — so this blocks the honest
  version of that demo.
- **A missing zone map file degraded pathfinding**, which is why
  `/moveto`-driven approach to a target could not reliably close range.
  Related to the recorded "Player BSP region is SIZE_MAX / missing
  maps/qeynos.map" family.

**T26's behavioural verification is still owed** because of these: the
*tooling* gap is closed (piped stdin reaches `/combat on` through the
real registry, confirmed against a live server), but no kill→loot cycle
has ever completed. — **T53 (2026-08-05) note: this is now stale.** T32
(2026-08-03, see the "Auto-loot on kill has NEVER worked" entry below)
*did* complete a real end-to-end kill→loot check and found the loot
machine's `AwaitingCorpse` entry is never reached — a genuine, verified
answer (the feature is dead, not merely unverified), not the open
question this paragraph describes. Treat T26's behavioural verification
as DONE via T32, with the auto-loot dead-path bug tracked separately
below. ~10 minutes of live piped interaction across
multiple target/combat/attack/moveto/who/camp cycles showed no freeze,
hang, or crash — meaningful negative evidence for the stall class T26
removed, but not a completed happy path.

## Command-registry overlap and the bare `/target` precedence gap (T29, 2026-08-02)

T29 unified command *dispatch* (registry 1 → 2 → renderer bridge)
without merging registry *ownership*. It found **~37 command names
defined in both** `EverQuest::m_command_registry` and
`action::CommandProcessor`. Spot-checks (say/shout/ooc/auction,
attack/target) showed they converge on the same underlying
`EverQuest`/`CombatManager` calls — same effect by two independently
written paths, not the "different behaviour" STOP case.

One visible difference: registry 1's `/target` requires an explicit
name and prints usage on empty args, while registry 2's falls back to
`targetNearest()`. Registry 1 now wins, so **bare `/target` no longer
targets nearest** on the `--script`/stdin path (it did before, when
`--script` reached only registry 2). Orchestrator checked: no committed
script uses bare `/target`, so blast radius is nil today. Still
reachable via `/listtargets` + an explicit name. Flagged rather than
silently absorbed; collapsing the duplicate vocabularies is a natural
part of a future command-layer cleanup.

**T51 (2026-08-05) reduced the "~37" estimate to a concrete, greppable
34-name list** — see `refactor/T51_attack_path_consolidation.md`'s Review.
It also closed three of them for real (`attack`, `stopattack`, `aa`):
registry 2's copies were deleted as always-shadowed dead code after a
live probe proved registry 1 served every one. **That is the template for
the rest of the list** — establish live which registry actually serves a
name before touching it, because reading the registrations is not enough:
T51's own plan asserted `/a` was a live attack alias, and the live probe
showed it had always been `/assist`'s. **Any future consolidation here
should be behaviour-neutral at the command line and should prove that,
not assume it.**

Smaller follow-up from the same unit: `ConsoleInputHandler`'s bespoke
non-slash `say`/`tell`/`quit` words are now redundant with `/say`,
`/tell`, `/quit` through the unified channel. Left in place (something
may depend on them); retire once nothing does.

## `--log-level=` is clobbered by `SetDebugLevel(0)` in main.cpp (T31 finding, 2026-08-02) — **FIXED by T56 (2026-08-10)**

**CLOSED.** `src/client/main.cpp` now detects whether `--log-level=` was
given explicitly (scans `argv` after `InitLogging()` has already applied
it) and, if so, restores that level after `EverQuest::SetDebugLevel()`
would otherwise clobber it back down from `-d`'s default of 0. **Decision:
explicit `--log-level=` beats numeric `-d`** when both are given (silent
precedence is what produced this bug; a one-line `LOG_INFO` fires when the
two disagree, naming which won). `-d` alone is unaffected — same behaviour
as before. `--help` (`src/client/application.cpp:2090`) now documents the
interaction. Verified end-to-end with three piped binary runs
(`--log-level=DEBUG` alone, `-d 3` alone, both together with disagreeing
levels) — see `refactor/T56_logging_db_board_fixes.md` Review for the full
transcripts. No test seam exists for this specific fix (the logic lives
inline in `main()`, which isn't compiled into a linkable target), so no
unit test was added for it — documented rather than invented, per the T56
plan's instruction. `eq.h`'s `SetDebugLevel()`/`logging.h`'s
`SetDebugLevel()` were both left untouched (out of this unit's territory);
the fix is entirely local to `main.cpp`. Original report follows for the
record:

Found by T31 while building its acceptance transcript, worked around
rather than fixed (outside that unit's `logging.h` territory).
`main.cpp` calls `SetDebugLevel(0)` *after* parsing `--log-level=`,
clobbering whatever the user asked for. `-d <n>` works; the documented
`--log-level=LEVEL` flag does not — and the client's own startup banner
tells you to use it ("Log level: DEBUG (use --log-level=LEVEL to
change)").

Relevant under the 2026-08-02 automation policy for the obvious reason:
a script that sets verbosity the documented way silently gets none.
Small fix; likely a one-line ordering change, but confirm which call
should win rather than just moving it.

## The live-session test config had THREE stale paths (orchestrator, 2026-08-03) — FIXED, and it probably explains both of T26's "blockers"

`/home/user/projects/claude/casterella.json` — the config CLAUDE.md
names for graphics integration tests, and the one every recent live
session used — still pointed at the pre-`/mnt/projects` layout. **All
three asset paths were dead:**

| key | was (missing) | now |
|---|---|---|
| `eq_client_path` | `/home/user/projects/claude/EverQuestP1999` | `/mnt/projects/EverQuestP1999` |
| `maps_path` | `/home/user/projects/claude/akk-stack/server/maps/base` | `/mnt/projects/akk-stack/server/maps/base` |
| `navmesh_path` | `…/maps/nav` | `/mnt/projects/akk-stack/server/maps/nav` |

The maps were never missing — 513 files sit at the real location,
byte-identical to the mirror at `/mnt/projects/maps/base`. Only the
prefix was stale, left behind by the workspace move.

**This very likely explains both blockers that stopped T29 finishing
T26's loot verification:** the "missing zone map file degrading
pathfinding" was this, and the `race=415` missing-model asset crash is a
strong candidate too — with `eq_client_path` pointing nowhere, model data
cannot load at all. That should be re-tested rather than assumed.

Fixed in place (backup at `casterella.json.bak-20260803`); the client now
starts with zero ERROR/FATAL lines where it previously reported an empty
EQ client path. **Lesson worth generalising: a config outside the repo
degraded live-session evidence for an unknown number of units without
ever failing loudly** — asset paths resolving should be checked at the
start of any live leg, not inferred from the absence of complaints.
Consider a startup validation that names missing configured paths.

## Auto-loot on kill has NEVER worked against a real server — pre-existing dead path, found by T32 (2026-08-03) — **FIXED by T33 (`b3f6cda9`) + T34 (`5a52cc4f`, `/loot` live-verified); heading annotated T215**

**T32 completed the first real end-to-end kill→loot check** (live eqemu,
GM-spawned lootable `a_scaled_wolf_pup`, driven entirely by piped
stdin) and the loot state machine's `AwaitingCorpse` entry was **never
reached** — zero loot-state markers across the kill and 60+ s of
observation.

**Mechanism (orchestrator-verified against source, not taken on
report):** two disjoint death-detection paths race, and the wrong one
wins.
- `CombatManager::ProcessCombatRound()` polls `entity.hp_percent <= 0`
  (`combat.cpp:1150`) and that block is the **only** site that enters
  `LootSubState::AwaitingCorpse`.
- But `ProcessCombatRound()` returns early at `combat.cpp:1100` when
  `!m_auto_attack_enabled`.
- The authoritative death signal arrives first as
  `EverQuest::ZoneProcessDeath()` (`eq.cpp:3233-3237`), which calls
  `m_combat_manager->DisableAutoAttack()` when the victim is our target
  — setting `m_auto_attack_enabled = false`.

So by the time `ProcessCombatRound()` next runs, it early-returns, and
the `hp_percent <= 0` block is unreachable after any server-signalled
death.

**ATTRIBUTION — NOT a T26 regression.** Verified against `677807c`
(fable immediately before T26 merged): the early-return guard, the
`hp_percent <= 0` block, and `ZoneProcessDeath`'s `DisableAutoAttack()`
call are **byte-for-byte the same structure pre-T26**. T26 replaced four
blocking sleeps *inside a block that a real kill already never reached*.
The dead path predates it, and on this evidence auto-loot-on-kill has
never worked against a live server.

**What T26 DID fix is a different, reachable entry.** `CheckAutoLoot()`
enters the machine at `AwaitingItems` (`combat.cpp:1851`) gated on
`m_combat_state == COMBAT_STATE_LOOTING`, which `LootCorpse()` sets —
the shift+click / action-layer path T23 identified as the one that
actually stalled the game thread. **T32 did not exercise that path**, so
T26's sleep-removal remains behaviourally unverified there even though
its liveness property held (see below).

**What T32 DID prove**: liveness is solid. `/who` and chat were answered
instantly during and after the kill, movement-sync packets never
stopped, clean `/camp` shutdown — i.e. nothing blocks the game thread on
this route any more.

**Two follow-ups, both real:**
1. **Wire the loot machine to the authoritative death event.** The fix
   shape is to enter `AwaitingCorpse` from `ZoneProcessDeath()` (or have
   `DisableAutoAttack()` hand off) rather than from a poll that the same
   event disables. Note this makes a dormant feature live for the first
   time — expect it to need its own verification, and decide whether
   auto-loot-on-kill is even wanted before wiring it.
2. **Verify the shift+click loot path**, which is the one T26 actually
   changed and the one that used to stall ~1.1 s per corpse.

Evidence: `benchmarks/audits/T32/` (run scripts + full transcripts) on
`task/T32-loot-verification`.

## `race=415` missing model data still crashes graphics mode (T32 re-test, 2026-08-03) — **FIXED by T38 (2026-08-04)**

**CLOSED, T56 (2026-08-10) correction.** This entry was stale: **T38 fixed
it on 2026-08-04** (`3e4f6bcd`, "T38: a missing base model skips one
entity, not the whole zone load"; `refactor/T38_missing_model_nonfatal.md`),
the day before T53's board-hygiene pass ran (2026-08-05). T53 checked this
exact entry, recorded "OPEN, confirmed still reproducing … no later fix
commit found" (`refactor/T53_deferred_board_hygiene.md:189`), and missed
it — one day wide.

**Re-verified at tip (T56, commit `d126e6fd` base) before writing this
correction, per that unit's hard precondition:**
- `src/client/graphics/renderer_zone_load.cpp:1437-1481`
  (`loadZoneStep8a_BaseModelPrep`) is now a per-entity `LOG_WARN` + skip:
  on `preloadModelData()` failure it logs a WARN naming the race/gender and
  increments `ctx.entitiesSkipped` / inserts into
  `ctx.skippedRaceGenderPairs` (declared `renderer_zone_load.cpp:94-95`),
  sets `ctx.entitySuccess = false`, and returns `true` (the zone load
  continues) — no `LOG_FATAL`, no abort. An end-of-step summary at
  `:1395-1407` reports the count and race/gender pairs whenever
  `entitiesSkipped > 0`.
- The entity falls through to `EntityRenderer::getMeshForRace()`'s
  documented placeholder-cube fallback (`src/client/graphics/CLAUDE.md`,
  "Model Loading Order" step 5).
- The parallel runtime path, `src/client/graphics/entity_prep_worker.cpp:139-145`
  (`EntityPrepWorker::processRequest`), was already non-fatal before T38 —
  confirmed still true: `preloadModelData()` failure just leaves
  `success = false` and is logged/handled downstream, no `LOG_FATAL`.
- `grep -rn LOG_FATAL src/client/graphics/` turns up exactly four
  survivors, none of them "missing race model" FATALs: two are
  whole-config-file-missing FATALs unrelated to any single entity
  (`entity_equipment.cpp:38` — `data/item_models.json` failed to load;
  `irrlicht_renderer.cpp:538` — `config/race_models.json` itself failed to
  load), and two are equipment-archive/WLD load FATALs
  (`eq/equipment_model_loader.cpp:920,927`), a different failure class
  from base race-model prep. **No `LOG_FATAL` on a missing race model
  remains anywhere in `src/client/graphics`.**
- T38's committed acceptance evidence is present:
  `benchmarks/audits/T38/run_race415_acceptance.sh`.

All of T56's Part 3 precondition held, so this re-title stands.

**Meta-finding, worth recording on its own:** T53's hygiene pass found and
fixed six stale-but-marked-open entries — and, per this correction, itself
produced a seventh miss, one commit and one day wide. The board's drift is
not a solved problem; T53's own closing caveat ("it is NOT self-maintaining")
is now directly evidenced, not just asserted. Treat every "OPEN, confirmed
still reproducing" note as time-stamped, not permanent — re-verify at the
commit you're standing on, not at the commit the note was written at.

**Genuine remainder, logged separately below** (out of T38's scope and out
of this unit's): race 415 and gfaydark's race=330 still render as
placeholder cubes, not their real models — `config/race_models.json` tops
out at race_id 367 (`refactor/T38_missing_model_nonfatal.md:85`). See
`` ## `race=415`/`race=330` render as placeholder cubes — data/content gap
past `config/race_models.json`'s race_id 367 ceiling `` below.

Original report follows for the record:

Re-tested after the stale-`eq_client_path` fix, since that was the
leading hypothesis. **Still reproduces** — genuinely separate: the model
data is missing from every S3D archive searched, not a path problem. It
fails clean (FATAL → requested quit → exit 0) rather than hanging, and
it blocks graphical live sessions, which is what Will's "the same
commands in a real session" ruling ultimately wants. Same family as the
recorded gfaydark `race=330` FATAL.

## `race=415`/`race=330` render as placeholder cubes — data/content gap past `config/race_models.json`'s race_id 367 ceiling (T56, 2026-08-10) — **415 FIXED by T213 (`b6d0576f`, aliased to race 36 RAT; 457 aliased to 88 CLM/CLF); 330 has had an entry since `35c51be2` and is UNVERIFIED — annotated T215**

**New entry, NOT the same claim as the one above** (that one — the crash —
is fixed; this is the genuine remainder T38 left explicitly out of scope
and this unit leaves out of scope too). Race 415 and gfaydark's race=330
no longer crash graphics mode, but both still render as generic colored
placeholder cubes rather than their intended models, because
`config/race_models.json` only maps race IDs up to 367
(`refactor/T38_missing_model_nonfatal.md:85`). This is a *data/content*
gap — either the S3D archives genuinely lack these models, or
`race_models.json` needs new entries pointing at models that do exist
somewhere — not a code bug. Needs its own unit: asset verification (do
these races have real model data anywhere in the client archives?) and a
live visual acceptance only Will can give. Not attempted here per the T56
plan's explicit scope boundary.

**T94 addendum (2026-08-13), performance side only, data gap still open:**
this class of race (`race=330`, `race=415`, and Nektulos's five other
foreign/unmapped races — `432`, `436`, `440`, `468`, `471`) used to have a
second, compounding cost on top of rendering as a placeholder cube: every
*entity* carrying one of these races re-ran the full S3D fallback-chain
search from scratch at zone load (`RaceModelLoader::preloadModelData()`
had no negative-result memo), which was 93.8% of Nektulos's Step 8 wall
time per T91's capture (130 of 452 entities). T94
(`refactor/T94_negative_model_lookup_cache.md`) added a permanent
negative cache keyed on `(raceId, gender)` so the search runs once per
*race*, not once per *entity* — Nektulos's live-measured Step 8 wall time
dropped from 19.425s to 14.447s with T91's fix already in place (both
numbers dwarfed by T82's original uncorrected 246s, almost all of which
was T91's `otherChrCaches_` LRU-accounting bug, not this one). The data
gap itself — these races still render as placeholder cubes — is
unchanged and remains exactly the open item described above.

## `check_file_sizes.sh` reports a vacuous pass indistinguishably from a real one (T33 finding, 2026-08-03) — **FIXED by T37 (2026-08-04)**

**CLOSED.** Investigating turned up **three** routes to a silent pass, not
the one originally reported:

1. **empty diff** (run from `main`, a slot detached at the base rev, or
   after a merge) — legitimate, but was invisible;
2. **`--files` with no paths after it** — a caller mistake;
3. **a `BASE_REV` that does not resolve** (typo, or not a git repo at all)
   — git wrote to stderr, `mapfile` got nothing, and the script exited 0.
   This was the worst of the three: a mistyped base *passed*.

Fix: (1) still exits 0 but prints `no C++ files to check (<source>) —
nothing was checked, this is not a pass`; (2) and (3) are now errors
(exit 2) because the caller asked for a check that never happened. Every
run that does check something now opens with `checking N file(s) (<source>)`
and closes with `checked N file(s): X ok, Y warn, Z fail`, so a Review
pasting the output shows what was examined rather than an absence of
output. Exit-code contract preserved: 0 ok, 1 FAIL over the hard ceiling,
2 error.

All five modes verified by hand (empty diff, bad base, empty `--files`,
explicit paths with a WARN and a FAIL, real diff with 10 changed files).

**This closes the verification-integrity trio** — golden-replay CWD skip
(T36), this, and the original golden-skip note (superseded by T36). All
three shared one shape: *a check that can quietly not run is worse than no
check, because it manufactures confidence.* Worth remembering the next
time a verification step is added.

Original report follows for the record:

`scripts/refactor/check_file_sizes.sh [BASE_REV]` builds its file list
from `git diff --name-only <base>` (default `fable`). When that diff is
empty — run from `main` (which *is* `fable`), or from a slot detached at
`fable`, or after a merge — the loop body never executes and the script
**prints nothing and exits 0**.

That is correct behaviour in the narrow sense (no changed files, nothing
to check), and it does work properly for an executor sitting on a task
branch in its own worktree. The problem is that **a vacuous pass and a
real pass are byte-identical from the caller's side**: no output, exit 0.
Every unit's verification bar ends with this step, so "no new FAILs" can
mean "checked and clean" or "checked nothing", and the report reads the
same either way.

T33's executor hit this and worked around it with `--files` plus absolute
paths, which is the right instinct but not something the next unit will
know to do.

Fix is small: emit a line when zero files are selected (e.g. `no files
changed vs <base> — nothing to check`), so a no-op run is visible. Worth
doing under the 2026-08-02 automation policy for the same reason the
golden-replay silent SKIP matters — a check that can quietly not run is
worse than no check, because it produces false confidence.

(Related, same class, already known: the golden replay test silently
SKIPS when a worktree lacks its `data/captures` symlink.)

## Golden replay silently SKIPS even with the repo-root `data/captures` symlink present (T35, 2026-08-03) — **FIXED by T36 (2026-08-03)**

**CLOSED.** `tests/CMakeLists.txt` now defines `WILLEQ_REPO_ROOT` for every
test target in that directory and below (one `ADD_COMPILE_DEFINITIONS`
line), and the four capture-path helpers resolve their defaults against it
instead of the process CWD: `test_session_replay.cpp`,
`pcap_test_utils.h`, `zone_stream_test_utils.h`,
`test_login_world_flow.cpp`. Env overrides (`WILLEQ_SESSION_LOG`,
`WILLEQ_PCAP`) still take precedence.

The pattern was not invented here — `test_zone_lines.cpp` already used
exactly this (`RepoPath()` + a per-target `WILLEQ_REPO_ROOT`, with the
comment "to stay independent of ctest's working directory"). It had simply
never been generalised beyond that one target.

**Measured, in a genuinely fresh build dir with only the documented
repo-root symlink**: 57 capture-dependent tests (golden replay + every
pcap/daybreak/login-world-capture suite) run and pass, where before every
one of them silently skipped. Isolated properly — with the symlink
correct, reverting *only* the source change still skipped all of them, so
the CWD resolution is what was at fault, not a missing capture. Full suite
on that fresh tree: 1818/1819, ALSA only.

**Remaining skips are legitimate** (graphics integration needing a
display, one sound buffer, one PFS archive fixture) — not path failures.

Original report follows for the record:

Sharper root cause than the existing note above: the repo-root symlink
(`<slot>/data/captures` -> `/mnt/projects/fable/captures`) is necessary but
**not sufficient**. `GTEST_DISCOVER_TESTS(test_session_replay)` sets
`SessionReplayGolden.MatchesRecordedSessionOrCreatesBaseline`'s ctest
`WORKING_DIRECTORY` to `<build>/tests` (the executable's own binary dir,
gtest_discover_tests' default), and `DefaultSessionLogPath()` in
`tests/test_session_replay.cpp` uses the relative string
`"data/captures/session.sleq"` — which resolves against `<build>/tests/`,
not the repo root. Every freshly created `build/` directory (routine —
`build/` is gitignored) silently re-skips this test until someone also
creates `<build>/tests/data/captures` -> the same real captures dir.
Verified: T35 hit this despite the worktree-root symlink already existing,
worked around it with
`ln -s /mnt/projects/fable/captures <slot>/build/tests/data/captures`,
reran, confirmed the test then runs and passes (not just "not skipped").

Not fixed in-tree — outside T35's territory. Two real fixes, either would
do: (1) set `WORKING_DIRECTORY` for this test to `${CMAKE_SOURCE_DIR}`
instead of the gtest_discover_tests default, or (2) have the test resolve
its session-log path relative to a compile-time source-dir macro instead
of CWD. Whoever owns `tests/CMakeLists.txt` / `test_session_replay.cpp`
next should pick one — this has now cost two units the same rediscovery.

## Item links in chat lose 11 characters of the item name (Will spotted it, root-caused 2026-08-03) — **FIXED, same T34 unit, commit `7a1e48b9`**

**CLOSED, T53 (2026-08-05) verification.** This entry was written "NOT
FIXED" but the fix landed in the same `task/T34-*` branch shortly after,
before merge (`7a1e48b9`, "T34: fix item links losing 11 chars of the item
name"; merge commit `5ec8ccc9` explicitly lists "fix item-link name
truncation"). Current `src/client/formatted_message.cpp` (both call sites,
~line 133 and ~277) disambiguates structurally via `isAllHex(data,
link_content_start + LINK_METADATA_SIZE_SHORT, ...)` on the byte range
between the SHORT and LONG metadata sizes, rather than guessing from total
link length alone. Pinned by `tests/test_formatted_message.cpp:558-605`
with the exact "Scaled Curskin" regression case this entry describes.
Original report follows for the record:

Will flagged that T34's loot evidence read *"You have looted a kin."* —
`"a kin"` is not a plausible item name. It is not: the real item is
**"Scaled Curskin"** (id 12654, confirmed against the live DB and the
client's own `TitaniumItemParser` output in the same transcript).

**Root cause — `src/client/formatted_message.cpp`, the link-metadata
size heuristic.** Item links arrive as `0x12 <hex metadata> <item name>
0x12`. The parser guesses the metadata length from the *total* link
length:

```cpp
if      (link_len > LINK_METADATA_SIZE_LONG /*56*/) { name_start_offset = 56; }
else if (link_len > LINK_METADATA_SIZE_SHORT/*45*/) { name_start_offset = 45; }
```

Titanium sends **45**-char metadata. So for a 14-character name,
`link_len = 45 + 14 = 59`, which is `> 56` — the parser takes the 56
branch and starts the name 11 characters too late:
`"Scaled Curskin"[11:]` = `"kin"`. Exactly the observed output.

**Trigger condition: any item name of 12+ characters** (45 + 12 = 57 >
56). Shorter names land under the threshold and render correctly, which
is why nobody noticed — and why the tests miss it: the longest item name
in `tests/test_formatted_message.cpp` is `"Epsilon"` (7 chars), so every
existing case stays in the safe branch.

**This is user-visible in normal play**, not just in audit transcripts:
every loot message, and every item another player links in chat, mangles
any name of 12+ characters. Most EQ item names are longer than that.

**Likely correct fix**: this client is Titanium-only ("All opcodes are
hard-coded for Titanium specifically" — CLAUDE.md), so the 56-char
variant belongs to later clients (SoF+) and arguably should not be in
the decision at all — use 45 unconditionally, or determine the boundary
structurally (metadata is uppercase hex) rather than by guessing from
total length. Add a regression case with a long item name; that alone
would have caught this.

**Correction to the record**: T33's and T34's Reviews both call this
"cosmetic" and "pre-existing". Pre-existing is right; cosmetic
undersells it — it corrupts displayed item names for the majority of
real items. Neither unit's loot verdict depends on it: both proved the
loot itself via `LootItem ACK` and the item landing in an inventory
slot, not via the chat string.

## PATTERN: `EQT_HAS_GRAPHICS` gates around non-graphics code — **SWEPT by T43 (2026-08-04)**

**CLOSED as a pattern.** T43 applied P28's rule exhaustively to every
guard outside `src/client/graphics/` and fixed **8 misclassified sites**:
hotbar load/publish on zone-connect; `setPlayerInfo()` on the inventory
manager; all three `OnPetCreated()` call sites (the residual below — pet
deltas never published headless); two `UpdateInventoryStats()` call sites
(headless equipment stats never refreshed on HP change); `/bank` and
`/train`; and **a sixth instance the sweep itself found — audio's
EQ-client-path read left audio silently disabled in headless even after
T42's `SetEQClientPath` fix.**

Notably **no site needed a new null-check**: every fixed function already
null-checks its own `m_bridge`/`m_inventory_manager`, which is *why* the
guards were redundant rather than protective. That is the general shape —
these gates were belt-and-braces around code that already defended
itself, and the belt silently disabled the feature.

**Pinned** by `FullStateSnapshot.HeadlessZoneInPopulatesGameState`
(`tests/test_zone_connection_flow.cpp`), a synthetic headless zone-in
over the server-free `ReplayZonePacket` seam asserting player stats, an
inventory slot, a spell gem and a pet all reach the bridge — state
*populated*, not "did not crash". Runs in both trees.

**Honest limit on that pin, worth keeping in mind**: it would have caught
P28, T41 and T42's item-dispatch drop, but **not** T29 (command
registration) or T42's `SetEQClientPath` — those need different oracles,
which is why the live headless leg stays in the verification bar rather
than being replaced by the test. Graphics-mode no-op-ness was established
by `visual_gate` PASS in both trees with goldens untouched.

The classification table — **with a one-line reason for every guard left
in place** — is in `refactor/T43_graphics_gate_sweep.md`'s Review. Consult
it before re-auditing; that artifact exists so nobody starts from
scratch. Historical record of the pattern follows:

Every unit that has gone looking in headless territory has found another
one. This is no longer a series of coincidences; it is a systemic
misclassification, and the instances keep getting more severe:

| unit | what was wrongly gated | effect on `EQT_GRAPHICS=OFF` |
|---|---|---|
| P28 | `eq.h` guard blocks enclosing non-graphics members | ~33k lines of base-layer source in no test binary |
| T29 | ~2,500 lines of `eq_commands*.cpp` registration | **no slash commands at all** |
| T41 | `m_inventory_manager`'s **construction** (P28 declassified the declaration, missed this) | manager never constructed; snapshot would publish nothing |
| T42 | the generic `HC_OP_ItemPacket` dispatch branch, plus a larger gate over `HC_OP_MoveItem`..`HC_OP_GMTraining` | regular inventory packets **dropped entirely** |
| T42 | `Application::initialize()` never calling `SetEQClientPath()` | **every headless zone-in force-quit within ~2 s** — headless could not stay connected to a real server |

Plus one known residual, noted here as "not yet fixed": **all three
`OnPetCreated()` call sites in `eq_entities.cpp` are `#ifdef
EQT_HAS_GRAPHICS`**, so pet *deltas* never publish in an `EQT_GRAPHICS=OFF`
build. T41's snapshot fix covers a pet present at zone-in; a pet summoned
mid-session is still invisible there.

**FIXED — same unit, T53 (2026-08-05) verification.** This residual reads
as open in the text above it, but T43's own commit already closed it:
`fabf0246` ("T43: sweep EQT_HAS_GRAPHICS guards, fix pet-delta residual,
pin headless with a test"). All three sites in current
`src/client/eq_entities.cpp` (~481, ~1236, ~1462) are unconditional, each
annotated "T43: was #ifdef EQT_HAS_GRAPHICS." The entry's prose was simply
never updated after the fix landed in the same commit that wrote it.

**Why this matters more than it looks:** headless is the mode the
automation work (T29/T30/T31) and the testing campaign run in. Each of
these made headless quietly *less functional than it appears* rather than
failing loudly — the T42 one meant a headless client could not hold a
server connection at all, and nobody noticed because nothing had tried.

**Recommended: one sweep unit** that audits every `EQT_HAS_GRAPHICS` /
`EQT_GRAPHICS` guard against P28's own rule (only renderer-facing API
stays guarded), rather than continuing to find these one per unit. The
rule is already settled; what is missing is that nobody has applied it
exhaustively. A test that builds `EQT_GRAPHICS=OFF` and exercises a real
headless zone-in end to end would have caught three of the five.

## PATTERN: real features reachable only through a UI gesture, invisible to automation — **NARROWED by T48 (2026-08-05), not closed**

**Vendor is closed.** T48 added `/vendor`, `/vendorlist`, `/buy <slot>
[qty]` and `/sell <slot> [qty]` to `EverQuest::m_command_registry`, all
pushing the **same intent structs the renderer gesture pushes**
(`VendorToggleIntent`/`VendorBuyIntent`/`VendorSellIntent`) through the
identical `ProcessBridgeIntents()` switch into unmodified
`RequestOpenVendor`/`BuyFromVendor`/`SellToVendor`. The one differing hop
is stated rather than glossed: commands push from the game thread, the
gesture from the render thread — safe either way via the bridge's mutex.

`/vendor` was added beyond the original scope because scoping the
acceptance revealed `VendorToggleIntent` had the **same** no-producer
problem — you could not have opened a vendor from a script to buy from
it. Reachability without discoverability is only half the gap, which is
why `/vendorlist` exists too.

**Attack/auto-attack is now closed too, but T51 (2026-08-05) found it was
never actually an instance of this pattern** — the framing above (T48's
"worth a small unit on the T34/T48 model") turned out to be wrong when
checked directly. `AttackIntent`/`ToggleAutoAttackIntent` did have working
`ProcessBridgeIntents()` handlers and no producer, but auto-attack was
**always reachable** the whole time: `/attack`/`att`, `/stopattack`, and
`/aa`/`~` in `EverQuest::m_command_registry` (registry 1) call
`CombatManager` directly and always won T29's dispatch order over a
**second, fully duplicate, always-shadowed** registration of the same
three commands in `action::CommandProcessor` (registry 2, routed through
`ActionDispatcher`/`EqActionHandler` to the same `CombatManager`). The
intents were a **third**, wholly dead path with no producer at all. T51's
live probe (`benchmarks/audits/T51/`) proved registry 1 served all of
`/attack`, `/stopattack`, `/aa`, `/att` in every case, proved auto-attack
genuinely engages (real melee rounds, real damage both directions, against
a live mob, three separate times), and deleted both the shadowed
registry-2 registrations and the producer-less intents. See
`refactor/T51_attack_path_consolidation.md`'s Review for the full registry
table, including a correction to the plan's own alias-trap framing (`/a`
was never live for attack — it collided with `/assist`'s pre-existing `a`
alias in registry 1 and was already dead before this unit touched
anything).

**The broader pattern (dozens of other producer-less intent types) is
still narrowed, not retired** — T48's scan is not repeated here. The
running instance list of *confirmed real* reachability gaps: `/combat on`
(T26), the whole `EverQuest` registry (T29), `/loot` as a stub (T34),
vendor buy/sell (T42→T48), `/bank` bypassing the validated intent (T50),
and **zone-line-triggered zoning (T52)** — `SetZoningEnabled()` was
reachable only via a renderer Ctrl+Z keybind, so a purely piped/headless
session could never cross a real zone line at all. T52 exposed the
existing API as `/zoning [on|off]` because it needed the zone-line
regression leg headlessly. **That is now six instances, every one found
because a unit happened to need it — the pattern keeps producing.** Attack/auto-attack (T51) turned out to be a
duplication defect wearing the same clothes, not a new instance — a
caution for whoever scans the remaining intent list next: "has a handler,
no producer" is necessary but not sufficient; check for a parallel
direct-call command before assuming a gap.

**T66 (2026-08-11) adds instances 7 and 8 — both surfaced not by scanning
the intent list, but by a live verification unit (T63) hitting the wall
directly, same as T26/T42 before it.** `IrrlichtRenderer::cycleCameraMode()`/
`setCameraMode()`/`getCameraModeString()` (`irrlicht_renderer.cpp`) were
fully implemented, with the ONLY producer being `HA::ToggleCameraMode` — a
raw Ctrl+F5 key event. A scripted/piped session could never enter first
person; **T63 could not produce any first-person evidence at all**, a third
of what its plan required, and correctly STOPped rather than inventing a
scripting affordance. T66 exposed the same functions as `/camera` (no
argument reports the current mode via `getCameraModeString()`; a named
argument sets a mode that round-trips with the getter's own strings —
deliberately NOT just exposing `cycleCameraMode()`, which would force a
script to cycle blind and count, exactly T51's `/attack` trap; cycling is
additionally exposed via `/camera cycle` since it was free). Modelled
directly on `/screenshot` (registry 3, the renderer's own
`processSlashCommand()`/`RendererCommand` bridge path) — same dispatch, same
`chat()`-reply idiom, no new path invented. Proven headlessly end to end
(`tests/test_irrlicht_camera_command.cpp`): a default-constructed, never-
`Init()`'d `IrrlichtRenderer` is safe to drive directly (constructor is
empty; the functions in this path only touch `cameraMode_` and
null-guarded members), so the test exercises the real
`processSlashCommand()` → `setCameraMode()`/`getCameraModeString()` chain,
not a mock of it — set-then-read round trips for all three modes, plus
alias/cycle/unknown-mode coverage.

Second: `EverQuest::SendMoveItem(fromSlot, toSlot, quantity)`
(`eq_merchant.cpp`, opcode `HC_OP_MoveItem=0x420f`) was fully implemented
and already had ONE producer — `MoveItemIntent`, pushed by the inventory
UI's drag-and-drop gesture (`inventory_manager.cpp`) — but no command-line
producer existed. **T63 had to write `inventory.item_id` directly into the
database and restart the client** to get a character holding a weapon at
all, a worse test than the real path since it bypasses the packet flow a
real equip uses. T66 added `/equip <fromSlot> <toSlot>`, pushing the SAME
`MoveItemIntent` the drag/drop gesture pushes (same maximum-fidelity choice
T48/T50 made for vendor/bank), with slot ids from the canonical
`eqt::inventory::EquipmentSlot` enum (`inventory_constants.h`, T57's
ruling — no numbering invented) and validation against the same slot
categories `InventoryManager::isValidSlot()` recognizes (that method is
private, so the command mirrors its predicate rather than widening its
access for one caller). Scope discipline held: no inventory management, no
swap semantics, no client-side prediction — the server is authoritative and
the existing `ZoneProcessMoveItem` path (untouched) handles the reply.
**The live end-to-end leg is explicitly OUTSTANDING** — T66 ran under NO
EQEMU (sibling T65 held the lock this round), so nothing has watched a real
`HC_OP_MoveItem` packet leave the wire or a real server actually equip a
real item. Coverage stops at the wiring (`tests/test_eq_commands_equip.cpp`):
command registered, arguments parsed (missing/non-numeric/trailing-garbage
rejected), invalid slots refused with an actionable message, and the right
intent pushed with the right fields for the equip/unequip/cursor/bank
directions the plan named. **The next unit that takes the eqemu lock should
run `/equip` live** (equip a weapon from inventory, confirm server ack and
visible appearance change) before this instance can be called closed the
way T52 closed zoning.

**That is now eight instances**, still producing exactly as this entry
predicts whenever a unit actually needs the capability rather than scanning
for it abstractly.

**Instance 8 (`/equip`'s live leg) is now CLOSED (T71, 2026-08-12).** T65
ran it live first and reported it "duplicating the primary item rather
than swapping" — the next unit taking the lock (this one) characterised
that report rather than trusting or dismissing it: three live eqemu round
trips (direct equipment<->equipment swap, cursor-mediated swap with a
clean cursor, cursor-mediated swap with a deliberately congested
multi-item cursor), each verified via fresh `inventory` table reads. All
three were CORRECT — `SendMoveItem`/`ZoneProcessMoveItem` (the shared wire
path drag/drop also uses) were never broken, and T65's exact duplication
was not reproduced despite the attempt. **The real, confirmed, fixed
defect: `/equip` never touched `InventoryManager`'s own local model or
fired `EquipmentChangedIntent` after a successful move** — the protocol
has no success-ack (only a failure-correction), so a request the server
silently accepted left the client's own inventory view (and the
appearance it drives) stale until an unrelated event refreshed it.
Drag/drop's own gesture was never affected — it already updates local
state before sending, which is exactly why nobody ever saw this on the
UI path. Fixed in `eq_commands.cpp` only, no legality judgment added (the
server is still the sole authority on whether a move is allowed); see
`refactor/T71_equip_fix_and_first_person.md`'s Review for the full
evidence and `review/T71/`.

**The first-person weapon question (open since T63) is also now answered
— NOT SHOWN, by design of un-wired code, not a capture failure.** Entering
first person (`/camera fp`, or the pre-existing Ctrl+F5 path — both funnel
through the same `IrrlichtRenderer::setCameraMode()`/`cycleCameraMode()`)
calls `EntityRenderer::setPlayerEntityVisible(false)`, which explicitly
hides the player's body mesh, name tag, AND both weapon/shield equipment
nodes for as long as the camera stays in first person — unconditionally of
combat state. Separately, `EntityRenderer::setPlayerFirstPersonMode()`
(which would switch to "arms + weapon visible"), `updateFirstPersonWeapons()`,
and `triggerFirstPersonAttack()` are fully implemented but have **zero
callers anywhere in the codebase** — confirmed by an exhaustive grep, not
inferred from the pixels. T63's own read of these three functions ("near-
no-op compatibility stubs... reuses the same skeletal bone-attachment
path") was half right: the bone-attachment path is indeed shared, but
nothing ever routes execution into it for first person, because nothing
calls the function that would flip `isFirstPersonMode` on in the first
place. Visually confirmed live in three equip configurations (primary
only, dual wield, shield) in a lit zone (`qeynos2`) with `/camera`'s own
`getCameraModeString()` round-trip visible on screen — see `review/T71/`
for stills and video. This is a finding, not a fix — T71's stage 2 is
verification-only and did not touch `irrlicht_renderer.cpp`/
`entity_renderer.cpp`; a future unit owns deciding whether/how to wire
`setPlayerFirstPersonMode()` in.

**T77 (2026-08-12) adds a TENTH instance of the same standing pattern in a
slightly different shape: not "no producer at all" but "the validation
existed and the caller did not use it."**

> **Numbering note (orchestrator, 2026-08-12).** T77's Review called this
> the ninth; it is the tenth. **The ninth is the first-person weapon
> feature** described immediately above — `setPlayerFirstPersonMode()` /
> `updateFirstPersonWeapons()` / `triggerFirstPersonAttack()`, found by
> T71's stage 2, implemented with zero callers. It was written up as a
> finding without being given a number, so T77 reasonably took the next
> one. Corrected here rather than left to drift: this file is a research
> record and later units cite these by number. The first-person instance
> is additionally distinctive — it is the only one so far where the
> production code was **hollow** rather than merely unreachable, which is
> why "just add the callers" (T74's initial framing) would have produced
> nothing visible. T75 hit `/equip` sending a move
the server was always going to reject (`slot 23 -> Primary (13)`, `"The
selected slot was invalid"`) — `/equip` validated only that a slot NUMBER
was recognized, never whether the source held an item or whether the item
could legally go in the destination, even though
`InventoryManager::canEquipItem()`/`ItemInstance::canEquipInSlot()`
already did exactly that legality check and T71 had already added
`GetInventoryManager()` to reach it. T77 wired pre-flight into `/equip`
(rejects with a specific reason — "X cannot go in Y — wrong slot type" /
"...does not meet its class/race/level requirements" — instead of sending
a doomed packet), added `/equip <item name>` (resolves the source by
case-insensitive substring search over carried items, and the destination
from the item's own `slots` bitmask via `canEquipInSlot()` — paired slots
fall out for free by iterating every equip slot and preferring an empty
member of the pair; reports what it resolved to), and `/equip list`
(equipped-and-where, carried-and-where-it-would-go). No new legality
predicate was written — same fence T71 held. **The live end-to-end leg for
all three of T77's additions is explicitly OUTSTANDING**: T77 ran under NO
EQEMU (sibling T76 held the lock this round), so nothing has watched a
real pre-flight-then-send round trip, a real name-form resolution, or a
real `/equip list` against a live character's actual inventory. Coverage
stops at 30 tests in `tests/test_eq_commands_equip.cpp` against a populated
(but never server-synced) `InventoryManager`: empty source, illegal
destination (both the wrong-slot-type and the restrictions-failure
shapes), no-op same-slot, name resolution (including case-insensitive
substring match), name not found, ambiguous name across distinct items,
duplicate stacks of the identical item (correctly NOT ambiguous), paired-
slot selection preferring the empty member, both paired slots full
("would replace", still resolves), an item fitting no equip slot at all,
and both `/equip list` shapes (empty inventory, populated). **The next unit
that takes the eqemu lock should run all three forms live** — a pre-flight
rejection against a real restricted item, a successful `/equip <name>`
resolving and equipping a real carried weapon, and `/equip list` against a
real character's live inventory — before this instance can be called
closed the way T52 closed zoning and T71 closed instance 8's wiring leg.

**Instance 10 (T77's `/equip` pre-flight/name-form/list live leg) is now
CLOSED (T82, 2026-08-12).** All three forms confirmed live against
`Testmelee`: pre-flight rejections (empty source, wrong slot type, class/
race/level restriction — the last genuinely a race exclusion, decoded
against the item's `races` bitmask, not a level issue), `/equip <name>`
resolving and equipping a real carried weapon end to end (item 1327,
"Grizznot's Claws", resolved via `item_models`), `/equip list` against a
real live inventory, and the T71 explicit-slot form round-tripped cleanly.
Also closed, as a byproduct: the original T75/T76 server rejection
(`/equip 23 13`, "The selected slot was invalid") — T77's pre-flight now
catches it locally (the item in that slot, 12654 "Scaled Curskin", fits no
equipment slot at all; it was never a weapon). See
`refactor/T82_live_verification_session.md`'s Review for the full evidence
and `review/T82/`. **A second, narrower gap was found live in the same
session and is NOT closed**: `doMove()`'s direct slot-swap branch never
validates the *displaced* item against the equipment slot it lands in when
`fromSlot` is equipment and `toSlot` is occupied (e.g. unequipping a weapon
into an occupied general slot can swap a non-equippable item into the now-
empty equipment slot) — the server correctly rejects this and the client
self-heals via its existing desync-detection/resync path (not data loss),
but it is a real, reproducible pre-flight gap. Not fixed under T82 (larger
than the plan's "small, clearly-scoped" exception); a future unit should
extend `CheckEquipDestination()` (or its caller) to validate both directions
of a swap.

Historical record follows:

The 2026-08-02 ruling says anything blocking fully automated development
and testing is a defect. This keeps showing up in the same shape:

- **T26**: `/combat on` reachable only from the live chat window, which is
  why T26 could not verify its own loot fix.
- **T29**: the *entire* `EverQuest::m_command_registry` unreachable from
  `--script`.
- **T34**: `/loot` was a stub that printed a message and never dispatched;
  `EqActionHandler::lootCorpse` was reachable only from the renderer's
  shift+click callback.
- **T42**: **no slash command or renderer intent exists anywhere to
  trigger vendor buy/sell** — `VendorBuyIntent`/`VendorSellIntent` are
  never constructed. T42 had to add temporary scratch hooks calling the
  production functions directly to run its live acceptance, then revert
  them.

Each was found only because a unit happened to need it. **The vendor case
is the open one**: a scripted session cannot buy or sell, so nothing in
that area can be verified without hand-written hooks — and the next unit
that needs it will write them again.

Worth a small unit to expose vendor buy/sell through the command channel
the way T34 did for `/loot`, and worth asking what else in the intent
list has no producer.

## `GameTick()` never runs headless — spell cooldowns don't tick, buffs never expire (T44 finding, 2026-08-04) — FIXED by T45

The **runtime** analogue of the compile-time guard family T43 swept, and
it was missed precisely because T43 audited `#ifdef`s.

`EverQuest::GameTick()` is gated on graphics **twice**:

1. `Application::updateGameState()` — `if (m_eqClient && m_graphicsInitialized) GameTick(...)`;
2. inside `GameTick()` itself — `#ifdef EQT_HAS_GRAPHICS { if (!m_graphics_initialized) return; }`.

`GameTick()` is the only caller of `SpellManager::update()` and
`BuffManager::update()` (orchestrator confirmed: the sole call site is
`eq_bridge.cpp:234`, inside `GameTick`). Its own comment describes that
work as "cooldowns, memorization progress, cast timeouts" and "buff
durations, expirations".

**So in headless: spell cooldowns never tick, memorization never
progresses, cast timeouts never fire, and buffs never expire.** These are
game-logic timers with no renderer dependency whatsoever.

Not a regression — it predates the sweep and affected the old
`syncSpellState()` poll equally. And T26's plan already noted in passing
that "`GameTick()` is graphics-gated and would never run headless" (which
is why T26 wired the loot machine elsewhere), but nobody established the
*consequence* until T44 tried to observe a gem leaving cooldown in a
headless session and couldn't.

**Why it matters:** headless is the automation and testing mode. Anything
time-dependent in spell or buff behaviour is currently untestable there,
and any future unit that assumes otherwise will get a silent wrong
answer, exactly as T44 nearly did.

Fix is presumably small (the two gates), but it is **behaviour-changing
for headless** — timers that have never run would start running — so it
wants its own unit with a real before/after rather than being folded into
something else. Worth checking whether anything else `GameTick()` calls
has a genuine graphics dependency before un-gating wholesale.

**Fixed by T45** (`refactor/T45_gametick_headless.md`): both gates now OR
in `EverQuest::IsGraphicsReady()` (loading phase reached `COMPLETE`, the
same headless "state ready" signal T41 used for the full-state snapshot)
alongside the existing `m_graphicsInitialized`/`m_graphics_initialized`
check, rather than replacing it — graphics mode's flag is a one-way latch
already `true` for the entire window `GameTick()` can run in that mode
(set once in `Init()`, before `gameThreadLoop()` starts), so the OR is
provably a no-op there. Verified via `visual_gate.sh` (pixel-exact,
goldens untouched) plus a live before/after against eqemu (a
`SpellManager::update()` cast-confirm timeout that never fires on `fable`
headless, confirmed firing after the fix, in both build trees). See that
plan's Review for full detail, including a bridge-wiring gap found along
the way (next entry).

## `InventoryManager`/`SpellManager`/`SkillManager`/`HotbarModel` never get their own bridge wired headless (T45 finding, 2026-08-04) — **FIXED by T46 (2026-08-05)**

**CLOSED, T53 (2026-08-05) verification — this was the single most
dangerous stale entry found in this pass** (marked open, needing "a small
unit", when a unit already fixed it and the board never caught up).
Commit `b5f5c1d6` ("T46: wire the four managers' bridge pointers headless,
not just in InitGraphics()") extracted the wiring into
`EverQuest::wireBridgeToSubsystems()`, called from both `setBridge()`
(moved out of the header, now in `eq_bridge.cpp`) and `InitGraphics()`.
Confirmed live in current tree: `src/client/eq_bridge.cpp:121`
(`wireBridgeToSubsystems(bridge)` inside `setBridge()`) and callers at
`src/client/application.cpp:654` reasoning explicitly about the
unconditional propagation this entry asked for. The "Bridge pointers are
never cleared at teardown" entry below (T47) also depends on and
references this exact function, which is corroborating evidence the fix
is real and not a documentation artifact. Original report follows for the
record:

Found while verifying T45's "un-gating `ProcessBridgeIntents()` closes a
latent unbounded-queue hazard" claim for the `inventory_manager.cpp`
pickup-intent exception the T45 plan named.

Each of these four classes holds its **own** `GameStateBridge*` member
(`InventoryManager::bridge_`, `SpellManager::m_bridge`,
`SkillManager::m_bridge`, `HotbarModel::bridge_` — all separate from
`EverQuest::m_bridge`), and each is wired via a `setBridge()` call **only**
inside `EverQuest::InitGraphics()` (`eq_bridge.cpp:121-133`) — a
graphics-only call path: the function itself is `#ifdef EQT_HAS_GRAPHICS`,
and `Application::Init()` only calls it inside the
`config.graphicsEnabled && operatingMode == GraphicalInteractive` branch.
`EverQuest::setBridge()` (`eq.h:1037`, the one call `Application::Init()`'s
*headless* branch makes) sets only `EverQuest::m_bridge` — it does not
propagate to any of the four managers.

**Consequence: in every headless run (`EQT_GRAPHICS=OFF`, or
`EQT_HAS_GRAPHICS` with `--no-graphics`), all four managers' bridge
pointers are `nullptr` for the entire process lifetime.** Every
`if (bridge_) bridge_->pushIntent(...)` / `if (m_bridge)
m_bridge->pushEvent(...)` guarded on one of these four pointers is a
silent no-op headless — confirmed by grep (`setBridge` on each has exactly
one call site, inside `InitGraphics()`) and, for `SpellManager`
specifically, by reasoning through T44's own live transcript: the gem/cast
events T44 documented as "published" were inferred from adjacent
`LOG_INFO`/`LOG_DEBUG` lines that fire unconditionally, not from confirmed
bridge delivery — the actual publish calls (`publishGemChanged()`,
`publishCastStarted()`, `publishCastComplete()`) all early-return on
`SpellManager::m_bridge`, which was never wired in that session.

**Practical impact, concretely:**
- Item pickup in headless never pushes `MoveItemIntent`/
  `EquipmentChangedIntent` (the T45 plan's named "exception" case) — not
  because anything filters it, but because it never reaches `pushIntent()`
  at all.
- `SpellManager`'s own bridge-event publishes (gem state changes, cast
  start/complete — the T44 "spell state via bridge" unit's entire
  deliverable) are inert headless in production, even though
  `GameTick()` now runs there (T45) and `SpellManager::update()`'s
  *internal* state machine (the thing T44 and T45's tests actually
  verified) works correctly — it is specifically the bridge-event
  *publish* half that's silently dropped. `GameStateSyncBridge`/`SpellState`
  reads would see nothing change in a live headless session despite the
  underlying gem/cast state genuinely transitioning.
- Same shape for `SkillManager` and `HotbarModel`.

**Not a T45 regression** — this predates T45 and is orthogonal to the
gating fix T45 made (T45's own live acceptance leg worked around it by
observing `SpellManager`'s internal state via `beginCastFromGem()`'s debug
log, not via bridge-delivered events). **Not fixed here** — deliberately
out of T45's territory (`Application::updateGameState()`'s gate,
`EverQuest::GameTick()`'s own gate, and tests). Worth a small unit: wire
`EverQuest::setBridge()` (or a new headless-path call site alongside it,
matching T41's `m_inventory_manager` construction precedent) to also call
`setBridge()` on all four managers, with a live headless acceptance
leg that confirms a `SpellGemChanged`/`InventorySlotChanged` event is
actually observed on the console bridge (not just inferred from a log
line) — the mistake this entry itself documents T44 nearly made.

## Bridge pointers are never cleared at teardown — **FIXED by T47 (2026-08-05)**

**CLOSED.** `Application::shutdown()` now calls `setBridge(nullptr)`
unconditionally at the top, outside the `#ifdef`, so it runs in both
graphics and true-headless builds and clears all five pointers via T46's
`wireBridgeToSubsystems()`. `ShutdownGraphics()`'s redundant
`m_bridge = nullptr` was **removed** rather than left alongside — one
source of truth instead of two competing partial-clear paths.

Verified nothing publishes during shutdown by reading every step the
function runs (`stopRDPServer`, `joinLoadingThread`, `ShutdownGraphics`,
`IrrlichtRenderer::shutdown`, all three `IGameMode::shutdown`
implementations, `~EverQuest`), so clearing first drops nothing.

**Scope of the claim, stated as the executor stated it** — this is the
part worth preserving: the opt-in ASAN repro
(`tests/test_bridge_teardown.cpp`, `WILLEQ_T47_UAF_REPRO=1`, skipped by
default) produced a genuine ASAN-named heap-use-after-free in
`GameStateBridge::pushEvent` when a manager's pointer is not cleared
before the bridge dies, and a clean no-op when it is. **But that
exercises the mechanism at manager+bridge granularity, not
`Application::shutdown()` end to end — because the end-to-end hazard is
not currently constructible.** `shutdown()` destroys `EverQuest` and every
manager together in a single `m_eqClient.reset()`, so no window exists
today where a manager outlives a freed bridge pointer.

So T47 is **defensive hygiene that makes an invariant explicit**, not the
repair of a reachable bug. It stops being hygiene the moment teardown
ordering changes or anything publishes at shutdown — which is why the
permanent `BridgeTeardownInvariant` test is the durable part, not the
repro.

Historical record of the finding follows:

Characterized precisely by T46 while wiring the managers:

- **Graphics builds**: `ShutdownGraphics()` clears `EverQuest::m_bridge`
  but **not** the four managers' (`InventoryManager`, `SpellManager`,
  `SkillManager`, `HotbarModel`).
- **True-headless (`EQT_GRAPHICS=OFF`) builds**: **none of the five** are
  cleared, because that teardown block sits inside
  `#ifdef EQT_HAS_GRAPHICS`.

`Application::shutdown` does drop its own pointer and destroy the
decorator, so after shutdown `EverQuest` and its managers hold pointers
to a destroyed bridge. Survivable today only because nothing uses them
post-shutdown — the kind of reasoning that stops being true the moment
someone adds a shutdown-time flush or a late event.

**T46 left the fix one line away on purpose**: `wireBridgeToSubsystems()`
propagates whatever it is given including null, so a single
`setBridge(nullptr)` in `Application::shutdown()` would clear all five at
once. Wants its own small unit with a real before/after (ASAN or a
deliberate post-shutdown publish), not a drive-by — the whole point is
proving the teardown ordering, and this codebase has been bitten by
lifecycle ordering three times already (P36 self-join, T03b/T35 RDP
use-after-free, T30's unjoined stdin thread).

## `PlayerSpawnIdSet` arrives later headless than in graphics mode (T46 observation, corrected 2026-08-05) — NOT a bug

Recorded because it was reported as a defect and is not one, so nobody
re-raises it.

T46 flagged that `PlayerSpawnIdSet`'s publish at `eq_entities.cpp:396`
(the `ZoneSpawns` path) is `#ifdef EQT_HAS_GRAPHICS`-gated, and read that
as breaking `CastingStateChanged`-based casting reconstruction headless.

**T43 had already investigated that exact site** and classified it
legitimate, on the grounds that the `ClientUpdate` path publishes the
same event unguarded. **Orchestrator verified T43 is right**: there is no
guard around the `ClientUpdate` publish (`eq_entities.cpp` ~1517), so
headless does receive `PlayerSpawnIdSet` — just later, at the first
position update rather than at `ZoneSpawns`.

So it is a *timing* difference across a window in which the player cannot
have cast anything, not a missing event. Not a seventh instance of the
graphics-gate family, and T43's classification stands.

## From Batch Z

- Z5.3 empty-draw measurement (1-triangle buffer isolation) — TODO.

## Tracker hygiene

- Batch E table in progress.md has empty commit columns (work is committed;
  hashes were never recorded).

## P37 follow-ups (2026-07-29, merged `0019ba6`)

- ~~Pi KPI attribution + re-measure needed~~ — **MEASURED by P45
  (2026-07-30, merged `6944a3a`; full tables in
  refactor/P45_p37_kpi_remeasure.md Review). AWAITING WILL's
  regress-response decision.** Canonical e205 replay: p95
  32.240/32.263ms, flat vs P14/P39 and under the line — but this is a
  BENCHMARK-COVERAGE GAP, not neutrality: the default race cycle (IDs
  1-12) structurally never contains P37's excluded races. Real-roster
  A/B (ZOM-heavy --test-races, tip vs pre-P37 fda2e34): draws_avg
  113.6→348.3 (+207%), p95 39.304→57.650ms (**P37 costs +18.3ms p95**,
  ~all submission: +19.4ms sub p95 vs +0.3ms GPU). Caveat: e205
  stress + that roster ≠ production encounter density; translation to
  player experience is Will's call. OPTIONS (neither implemented):
  (1) composite-sheet alpha support in the builder + NFS redeploy —
  restores composite eligibility for masked races; (2) accept the
  loss for era correctness. SIDE FINDING: canonical-replay draws_avg
  41.9 vs P11b's 287.7 for a similar stack — route/pose-mix
  difference, noted not alarming. Also: the canonical replay's
  synthetic roster hides any masked/exotic-race effect — future
  entity-rendering units should add a --test-races real-roster leg
  when the change touches race-conditional paths.
- ~~Bless evidence pending~~ — P44's live pairs (merged `2246de5`)
  were **REJECTED by Will 2026-07-30, rejection verified correct**:
  aggroed mobs' attack animation made before/after pose-mismatched
  (diff = pose, not alpha), and the dark-stone background makes
  masked-opaque fill indistinguishable from alpha cutout. P44's
  "textbook evidence" claim is UNSOUND — annotated VOID in
  `refactor/P44_p37_bless_evidence.md`'s Review by P44b.
  **P44b retake COMPLETE (task/P44b-bless-retake, slot2) — STILL NO
  VISUAL EVIDENCE, new root cause found**: pose-identical static-
  benchmark method worked as designed (deterministic camera/entity
  placement + frame-index animation clock, bright-navy-sky close-up
  framing), but BEFORE/AFTER renders came back PIXEL-IDENTICAL for
  both SKE and ZOM at every angle/radius/scale tried. Root cause is a
  `gles2_zone_benchmark` TOOL GAP, not a P37 regression: the tool
  never links `entity_renderer.cpp`, so `mesh_building.cpp`'s
  MaterialType marker (confirmed correct via a reverted diagnostic
  trace, texture-for-texture match to P37's ground-truth table) never
  gets its companion-alpha-texture binding — the built-in alpha-test
  shader the tool falls back to reads unit-0 alpha only, which is
  always opaque for ETC1-loaded entity textures, so the discard never
  fires regardless of framing. RECOMMENDATION (not actioned, out of
  P44b's errand scope): wire `applyAlphaTestMaterialType()`-equivalent
  companion-texture binding into `gles2_zone_benchmark`'s entity path
  so future entity-rendering blesses have a working offline visual
  instrument — this would unblock a THIRD bless attempt without
  needing a live client at all. Zombie chest (zomch0001) remains an
  expected null by design (confirmed independently, doubly-so).
  Full writeup: `refactor/P44b_bless_retake.md`,
  `benchmarks/audits/p44b_bless_retake/EVIDENCE_MANIFEST.txt`.
  LESSON (P44): never use live animated mobs for visual A/B evidence;
  always check what's behind the alpha regions. LESSON (P44b): before
  trusting an offline benchmark tool as a stand-in for production
  visual behavior, verify it actually links the production code path
  in question, not just a marker the production path would consume.
  **P44c RGBA leg COMPLETE (same branch/slot) — ALSO NULL, but
  root-caused one level deeper than P44b, and this one is NOT a
  benchmark-tool-only gap.** Hypothesis tested: dropping `--eqe1-path`
  (forcing the direct-BMP/RGBA decode path, the same path production
  `willeq` uses whenever no ETC1 pack is configured) should let the
  built-in `EOGLES2SP_ALPHA3D` shader's unit-0-alpha read actually see
  real alpha, since `EntityRenderer::applyAlphaTestMaterialType()`
  (entity_renderer.cpp) documents the RGBA/DDS path as carrying "real
  alpha in the main texture's own alpha channel." FOUND INSTEAD: alpha
  is lost one step earlier, at decode. `ConstrainedTextureCache::
  decodeBMP()` (`src/client/graphics/constrained_texture_cache.cpp`,
  ~line 1151) handles EQ's masked entity textures — confirmed 8bpp
  paletted BMPs via a temporary trace (skefa0001/skeft0001/skeft0002/
  skech0001/skelg0001 all 8bpp) — by hardcoding alpha=255 for every
  pixel ("ignore palette alpha", the code's own comment). It does NOT
  implement the EQ masking convention ("palette index 0 = transparent")
  that `tools/eqe1_builder.cpp`'s BMP decoder (~line 88) already
  implements correctly for the ETC1/eqe1 offline pipeline. So
  `hasAlpha` is false for every ground-truth masked entity texture on
  this path — confirmed via a reverted diagnostic trace, then via an
  actual BEFORE/AFTER render pair (SKE, befallen, same `--orbit-radius
  8 --orbit-height-offset 6 --animate` framing as P44b, frame 26,
  `--eqe1-path` omitted): PIXEL-IDENTICAL, AE=0, byte-identical PNGs
  (matching md5), same result P44b already got when it tried this as a
  side-check. This means: unlike the ETC1 leg (P44b, a benchmark-tool
  linking gap only), **the RGBA path has no way to visually demonstrate
  P37's fix at all, in ANY tool including production `willeq`, until
  `ConstrainedTextureCache::decodeBMP()` gets the same palette-index-0
  masking logic `eqe1_builder.cpp` already has.** Not fixed this unit
  (diagnostics reverted, no production source changes) — this is a
  second, more consequential tool-fix candidate than P44b's
  entity_renderer-linking one, and arguably belongs ahead of it since
  it affects any RGBA/non-ETC1 rendering path, not just the benchmark.
  Evidence: `/mnt/projects/fable/videos/p44b_bless_retake/rgba_leg/`
  (NFS, gitignored) — `pairs/ske_{BEFORE,AFTER}_f26.png`,
  `crops/ske_{BEFORE,AFTER}_f26_torso_zoom.png` (400%),
  `diffs/ske_diff_f26{,_torso_zoom}.png` (AE=0 both). Full writeup in
  `refactor/P44b_bless_retake.md`'s Review addendum and
  `benchmarks/audits/p44b_bless_retake/EVIDENCE_MANIFEST.txt`'s RGBA-leg
  section.
  **BOTH GAPS CLOSED BY P46 (2026-07-30, merged `b49337b`)**: (A)
  runtime palette-index-0 masking in decodeBMP, gated on
  WLD-textureMasked via applyPaletteMask param — only the two
  call sites that already compute the masked flag pass true; zone
  path untouched; unmasked/zone renders md5-identical; (B) benchmark
  companion binding mirroring entity_renderer line-for-line.
  **BLESS EVIDENCE DELIVERED, NON-NULL, AWAITING WILL**: lead pair =
  videos/p46_bless/primary_etc1/crops/ske_{BEFORE,AFTER}_f26_torso_zoom.png
  (ETC1 leg, isolates P37 alone, Pi-representative); secondary RGBA
  leg = P37+P46 jointly (x86-representative); diffs entity-confined
  (tight silhouette bounding boxes); zomch0001 null by design.
  ~~ORCHESTRATOR OBSERVATION: AFTER limbs warmer~~ — that observation
  WAS the bug. **P46 pairs REJECTED by Will (round 3, wrong textures
  on body parts); ROOT-CAUSED + FIXED by P46b (2026-07-31, merged
  `c40475d`)**: benchmark-only — P46 Part B ported only the
  alpha-test half of entity_renderer's PAIRED shader promotion, so
  masked buffers were shader-lit while unmasked siblings stayed
  fixed-function (per-entity lighting mismatch that read as wrong
  textures). Bisect matrix exonerated P37/P42 and Part A entirely
  (Part-A revert = byte-identical; pre-P46 clean — caveat: the
  instrument couldn't reach the path pre-P46, so production
  exoneration rests on entity_renderer's own always-paired code, not
  the matrix). Fix = applyEntityShaderMaterialPromotion pairing both
  halves. **═══ P37 VISUAL BLESS: CLOSED — WILL BLESSED THE P47
  MAGENTA CONTACT SHEET 2026-07-31 ("contact sheet looks correct")
  ═══** Six rounds total; the instrument that finally worked is
  Will's own design (P47, merged `6b78548`): model rendered large
  against solid magenta, no zone, enclosed-magenta metric +
  permanent ctest regression pin (test_entity_viewer_regression).
  Every earlier round's evidence (P44 live pairs, P46 pairs, P46b
  pairs, P46d pairs) is historical only — the blessed artifact is
  benchmarks/contact_sheets/p47_entity_viewer/index.html.
  Historical record of round 4 follows: ~~ROUND-4 EVIDENCE READY,
  AWAITING WILL~~: lead pair
  videos/p46b_bless/primary_etc1/crops/ske_{BEFORE,AFTER}_f26_torso_zoom.png
  (+ zom). KNOWN DISCLOSED DELTA: BEFORE leg renders fixed-function
  (pre-promotion instrument history) → pairs show the cutout PLUS a
  uniform warm/bright shift; production always ran both sides
  shader-lit, so players saw only the cutout. Judge the AFTER
  standalone for correctness; the tone delta is the instrument, not
  the game.
  **P46c (round 5, lighting-neutral retake) — STOPPED, root cause found,
  no new pair delivered.** Added benchmark-only
  `--no-entity-shader-promotion` (verified working: promotion-off AE=0 vs
  promotion-on AE=6900, matching P46b's own promoted-leg number exactly).
  Pre-capture discard check (mandated by the plan) found the RGBA leg's
  alpha-test discard **never fires** at all with promotion off — AE=0 at
  every frame/radius tried. Root-caused (temp diagnostic, reverted): a
  cache-key collision in `ConstrainedTextureCache` (name-keyed only;
  `applyPaletteMask` is only consulted on first decode of a name) combined
  with `zone_bench_loader.cpp`'s static `getMeshForRace()` preload loop
  running (and decoding entity textures unmasked via
  `ZoneMeshBuilder::loadTextureFromBMP`, `zone_geometry.cpp:249`, no
  `applyPaletteMask` arg) *before* the masked-aware T05/animated decode for
  the same race — the unmasked decode wins the cache slot permanently.
  Retroactively explains P46b's own bisect-matrix finding ("reverting Part
  A changes nothing"): the RGBA leg's committed AE numbers (both P46's and
  P46b's) were never showing an alpha cutout, only the lighting-model
  delta — the ETC1 leg's numbers are unaffected (separate companion-texture
  alpha path) and remain real. Production source untouched (fix is a
  separate errand, cache-key collision + an open question re: whether
  production's own entity load path has the same ordering hazard). Full
  writeup: `refactor/P46c_neutral_pair_stop.md`. P46b's round-4 pair (with
  its disclosed tone delta) remains the best available evidence pending the
  cache fix.
  **P46d (round 5, cache-key fix) — DONE, closes P46 Part A.** Made
  `ConstrainedTextureCache` mask-aware: `cacheKey(name, applyPaletteMask)`
  (identity when false, `name+"\x01masked"` when true) separates masked and
  unmasked variants across `cache_`/`decodedEntries_`/`compressedEntries_`/
  `pendingLedgerTag_`; `uploadTexture()` gained an `applyPaletteMask` param;
  `uploadAllPending()` uses a new `splitCacheKey()` inverse so its
  registrations still carry the real display name. Two non-obvious
  additional name-uniqueness hazards found and fixed en route (both caught
  by this unit's own discard-fire re-verification, not assumed away):
  `COpenGLES2Driver::addTexture()`/`wrapExternalTexture()` dedup GPU
  textures by name (silently returns the existing texture, discarding new
  image data, for a same-named second upload) — fixed by uploading under
  the internal key, not the display name. **Production ordering audit
  verdict: the live client's primary entity path (createAnimatedNode-first,
  static-fallback-only-on-failure) was NEVER exposed to P46c's poisoning
  ordering** — P13b's own fix (unrelated original motivation) happens to
  also protect this path; the benchmark's own unconditional static preload
  loop was the sole source of the exposure P46c found, an instrument
  artifact, not a shipped bug. Two OTHER, pre-existing, out-of-scope gaps
  found during the audit (composite-deferred on-demand per-part loads via
  `mesh_building.cpp`'s `loadTextureFromBMP()` fallback, and equipment
  texture-variant slot swaps via `animated_mesh_creation.cpp` — neither ever
  threads `applyPaletteMask`) — flagged below, not fixed (extending Part A's
  plumbing to two more call sites, a new errand). Discard-fire re-check now
  fires (AE=678, entity-confined bbox 84x198 @ ske f26, RGBA mode,
  `--no-entity-shader-promotion`, tip vs fda2e34). **Round-5 pair delivered
  and PASSES the absolute one-variable bar** (numerically + by the
  executor's own part-by-part inspection) at frames 26/42, both races —
  zom's leg crop shows a directly visible dashed alpha-cutout pattern absent
  in BEFORE, the clearest visual proof in this whole chain. Frame 12 carries
  a small (13-44px), non-entity zone-geometry artifact independently
  reproduced with zero entities (traced to this session's Irrlicht-tree
  rebuild, not this fix) — excluded from the "clean pair" claim, fully
  disclosed. Full writeup: `refactor/P46d_cache_key_fix.md` Review; numeric
  evidence + crops: `benchmarks/audits/p46d_bless/EVIDENCE_MANIFEST.txt` /
  `videos/p46d_bless/{before,after}/{pairs,crops}/`. **Open follow-up items
  (not this unit's scope)**: (1) thread `applyPaletteMask` through
  `mesh_building.cpp`'s on-demand `loadTextureFromBMP()` fallback for
  composite-deferred races; (2) same for `animated_mesh_creation.cpp`'s
  equipment texture-variant swap path — both currently decode any masked
  texture they touch opaque, a real (if narrow) live-client exposure
  distinct from the now-fixed cache-key bug.
- **P44 live-client findings (flagged, not fixed)**: (1)
  non-deterministic SIGSEGV zoning into befallen at x=-195,y=-982
  (zone-object-mesh-build; would not reproduce under gdb, no core);
  (2) pre-existing bind-point respawn FATAL "Player BSP region is
  SIZE_MAX!" (missing maps/qeynos.map — related to the P10b
  local-map/BSP FATAL family); (3) qeynos2 sky stays black despite
  /time 12 (zone-lighting quirk, unrooted).

## P40 verdict: resolution-visibility "anomaly" NOT a code bug (2026-07-30, merged `e3b61ee`)

- x86 repro at 3 resolutions: visibility counters BYTE-IDENTICAL
  (5,000/5,000 cells); only GPU time scales with pixels (normal fill
  cost). Occlusion buffer is fixed 64x32 from the preset (never
  res-derived); frustum aspect identical at 4:3 points; no LOD-by-
  pixel code exists. Likeliest explanation = P38's own flagged
  methodology drift (res run lacked shipped flags). ~~PENDING: a
  2-invocation clean Pi A/B (400x300 vs 800x600, shipped flags) to
  close it — ride along the next Pi unit.~~ Non-4:3 aspect changes DO
  move the frustum (correct behavior, noted for future res work).
- ~~REOPENED by P41's Pi ride-along~~ — **ROOT-CAUSED + CLOSED by P43
  (2026-07-30, merged `330aa10`; full narrative in
  refactor/P43_res_visibility_root_cause.md Review)**: NOT a culling
  bug, NOT Lima. `docker/irrlicht-drm/CIrrDeviceFB.cpp:335-382`
  silently substitutes the connector's PREFERRED mode when --res has
  no exact DRM match and overwrites Width/Height downstream — every
  historical "400x300" Pi run actually rendered **2560x1440** (16:9,
  7.68x the pixels of 800x600), log-confirmed on hardware. Wider
  frustum explains +22% draws/+39% tris to the decimal; "GPU
  inversion" = plain fill-bound scaling at the real pixel count.
  P40's x86 null was correct all along (P43 re-confirmed
  byte-identical in both flag configs). **CAUTION for all past/future
  data: any Pi CSV at a non-native resolution is mislabeled — the
  only trustworthy Pi resolutions are the connector's exact modes
  (800x600@75 confirmed; check the mode list before any res sweep).**
  Shipped 800x600 unaffected (exact mode match) → P43 correctly
  STOPPED, no code change.
- **Follow-up landed (P49, task/P49-drm-fail-loud, x86-only, awaiting
  merge)**: `docker/irrlicht-drm/CIrrDeviceFB.cpp`'s mode-fallback log is
  now ELL_ERROR (was ELL_WARNING) stating requested-vs-actual mode,
  repeated once more on the first post-init `run()` call so it isn't only
  visible in device-creation spew. `gles2_zone_benchmark` now REFUSES to
  run when the achieved resolution != `--res` unless `--allow-mode-
  fallback` is passed, and always writes the ACTUAL width/height into two
  new trailing CSV columns (`actual_width`,`actual_height`) regardless of
  the flag, so a dataset can never again be silently mislabeled. Decision
  logic extracted to a pure, dependency-free helper
  (`docker/irrlicht-drm/drm_mode_select.h`, `eqt_drm::selectDRMMode()`)
  and unit-tested on x86 (`tests/test_drm_mode_select.cpp`, 7 cases
  including the exact P43 400x300-on-Pi scenario) — x86 has no DRM path
  at all, so this is the only way this logic could be verified without a
  Pi; the real `CIrrDeviceFB.cpp` was build-verified only (ARM cross-
  build clean).
  - ~~**RIDE-ALONG ITEM for the next Pi-owning unit**~~ — **DONE (P51,
    2026-08-01)**: ran `gles2_zone_benchmark --drm --res 400 300
    <shipped flags>` (no `--allow-mode-fallback`) on real Pi/DRM hardware.
    (a) process refused, exit code 1 — confirmed. (b) stderr/log shows
    the requested-vs-actual ERROR line naming `2560x1440` as the
    substituted mode, plus the benchmark's own "Refusing to run" line —
    confirmed. (c) the DRM device-creation-time `MODE FALLBACK` ELL_ERROR
    line fires — confirmed; the documented post-init `run()`-time REPEAT
    does NOT fire for this tool, and that's expected/benign:
    `gles2_zone_benchmark`'s own resolution check exits the process
    before it ever reaches `IrrlichtDevice::run()`'s frame loop (it isn't
    a continuous-loop client like `willeq`, which is what the repeat log
    was written for) — only the initDRM()-time line is reachable here.
    Control: the shipped `--res 800 600` invocation (this unit's own H1
    probe run, same session) logged "DRM: Found matching mode
    800x600@75Hz for requested 800x600" with no fallback line, as
    expected. Full logs: `benchmarks/pi_runs/p51_kpi_bisect/befallen_p49_
    ridealong_400x300_refusal.log`. P49 is now fully closed end to end
    (x86 unit-tests + this Pi ride-along).

## P41 landed (2026-07-30, merged `edcd72a`): lighting lever characterized, report-only

- ~~**AWAITING WILL — the lighting decision**~~ — **DECIDED 2026-07-30:
  KEEP PER-PIXEL** (docs/decisions.md 2026-07-30 entry; lightweight
  stays flag-gated for future re-evaluation). Original staging note
  kept for the record: evidence staged at
  refactor/perf_truth_audit.md "2026-07-30 — P41" section (tables +
  options note), stills at /mnt/projects/fable/videos/p41_lighting/
  (manifest: benchmarks/audits/p41_lighting/EVIDENCE_MANIFEST.txt).
  Headline: −15.1%/−22.1% total/GPU at realistic e5 but only
  −6.4%/−11.2% at e205 stress (absolute ~1.3ms GPU savings constant,
  diluted as entity submission grows); visually the doorway pose
  differs on 89.9% of pixels (per-vertex banding on large flat polys
  — the 1999-era signature). Per-pixel is Q03-blessed territory; no
  flip without Will.
- **Swap-floor (audit item 5) narrowed**: eglSwapInterval(0) =
  decisive null (swap_ms 3.805 vs 3.873 control). EGL default swap
  interval ruled out; next hypothesis = the DRM page-flip's own
  waitForFlipComplete() wait in docker/irrlicht-drm/CIrrDeviceFB.cpp
  (named, not proven). Future diagnostic unit if the ~14.4-15.0ms
  wall floor matters.

## P39 landed (2026-07-30): occluder-256 plumbing + corrected framing

- CORRECTION to P38's ranking item 1: the real client ALREADY resolved
  256 by accident (fromPreset baseline; JSON no-op'd the unknown key).
  The silent-16 was (a) any JSON-only config path incl. unit tests,
  and (b) the BENCHMARK: zone_bench_loader hardcodes --occluder-budget
  default 16 and never reads the config — likely the source of P38's
  ~35.3ms "shipped" number. P39 landed the key parse + explicit JSON
  preset entry + resolved-value log + ctest regression on the real
  shipped JSON; Pi spot 36.9→32.5ms p95 tracks P14's bless.
- ~~Same-pattern keys reported NOT fixed: totalMemoryBudgetBytes,
  occlusionBufferWidth/Height (JSON cannot express them) — small
  follow-up candidate. Benchmark default-16 fix = separate small item
  (affects future measurement honesty).~~ **Both CLOSED by P42
  (2026-07-30, merged `4781815`).**

## P42 landed (2026-07-30, merged `4781815`): perf-hygiene bundle, all 4 items

- Closes P38 ranked items 2 (enableMipmaps dead flags — honesty branch,
  dead DRM-only branches simplified, no default change) and 4
  (glTexParameteri state-dedup productionized: per-texture-object param
  cache, 100→1 calls/frame, frames md5-identical) plus both P39
  residuals above.
- ~~**Residual — gate config drift, Will's call someday**: the benchmark
  now correctly defaults occluder budget from ConstrainedRendererConfig
  (256 on orangepi), but scripts/ci/visual_gate.py pins its own
  invocation to --occluder-budget 16 because the blessed goldens were
  captured under 16 and budget-256 occlusion changes visible pixels.
  Options: leave pinned (gate = pixel-regression detector, fine), or
  re-bless goldens under the shipped 256 (needs Will).~~ — **DECIDED by
  Will 2026-08-02: LEAVE PINNED.** The gate's job is pixel-regression
  detection, which it does correctly at 16; matching the shipped preset
  is not worth a golden re-bless round. **The pin is intentional — do
  not "fix" it as drift, and do not re-bless goldens under 256 without
  a new ruling.** Revisit only if a future unit needs the gate to catch
  occlusion-dependent regressions specifically.
- P38's remaining open ranks: 3 (resolution-visibility — REOPENED
  Pi-specific, see P40/P41 entry above), 5 (swap floor — narrowed to
  DRM waitForFlipComplete), 6 (residual GPU-sync — queued behind the
  lighting decision).

## P50 (2026-07-31): ConstrainedTextureCache's public query API doesn't
## account for the P46d masked-variant key mangling — FIXED by T92 (2026-08-13)

**FIXED by T92 (`task/T92-texture-cache`)**: option (a) taken — `hasAlpha()`,
`getTexture()`, `getAlphaTexture(name)`, `isETC1Texture(name)` all now take
`applyPaletteMask = false`, mirroring `getOrLoad()`/`uploadTexture()`'s
existing pattern; default preserves the exact old (unmasked-only) lookup
behavior for every pre-existing call site. Caller audit found every existing
by-name call site either never masks (zone geometry) or uses the unaffected
`ITexture*`-reverse-lookup overloads — **except one real dependency on the
old, missing behavior**: `zone_geometry.cpp`'s own `loadTextureFromBMP()`
was calling `hasAlpha(name)` with no mask argument right after uploading
under the mangled key when `applyPaletteMask=true` — fixed to pass the flag
through. Confirmed non-visual: that call's result only feeds
`texturesWithAlpha_`, which `buildTexturedMesh()`'s actual `MaterialType`
decision unconditionally OR's with the WLD `textureMasked()` flag — so this
fix can only ever ADD entries, never flip a rendering decision that WLD
didn't already make; visual gate ran clean (goldens unchanged) confirming
no observed behavior change. 5 new tests
(`tests/test_constrained_texture_cache.cpp`), including one exercising the
real ETC1/EQE1 upload path against `/mnt/projects/EverQuest_ETC1/qeynos2_obj.eqe1`
under a real EDT_OGLES2 surfaceless-EGL device. See
`refactor/T92_texture_cache_empty_data_and_p50.md` Review for full detail,
red/green proof, and verification. Original finding text preserved below for
history.

While closing P46d Part B's two remaining applyPaletteMask plumbing
gaps (`refactor/P50_mask_plumbing_gaps.md`), building evidence
surfaced a separate, pre-existing gap: `ConstrainedTextureCache`'s
public query methods — `hasAlpha(name)`, `getTexture(name)`,
`getAlphaTexture(name)` (by-name overload), `isETC1Texture(name)` —
take only a plain texture `name` and look it up directly in `cache_`.
But P46d's `uploadTexture()` stores masked-variant entries (uploaded
with `applyPaletteMask=true`) under the mangled internal key
`cacheKey(name, true)` = `name + "\x01masked"`, not the plain name. A
plain-name query for a masked-variant entry therefore always misses
(returns "no alpha found" / null) even when the masked texture was
correctly decoded and uploaded.

**Does not affect rendering correctness**: `MaterialType` selection
(the actual alpha-test-vs-solid material choice) is driven directly by
the WLD-derived `textureMasked()` flag at mesh-build time
(`mesh_building.cpp`, `equipment_model_loader.cpp`), never by these
query methods — confirmed by P50's own evidence (the GPU texture's
pixel data IS correctly masked; only the diagnostic query missed it).
No known production caller relies on these methods to detect a masked
variant's alpha for a decision that matters.

Follow-up candidate: either (a) add an `applyPaletteMask` parameter to
each of these four query methods (mirroring `getOrLoad()`/
`uploadTexture()`'s existing pattern), or (b) document that callers
needing the masked variant must pass the pre-mangled key themselves.
Not fixed by P50 (outside its two-gap scope; found only while writing
evidence, not requested).

## `RaceModelLoader::preloadModelData()`'s composite-sheet lookup calls
## getOrLoad() with an empty buffer whenever a JSON manifest exists but no
## EQE1 archive is configured (T92, 2026-08-13) — found, not fixed

T92 (`refactor/T92_texture_cache_empty_data_and_p50.md`) root-caused the 30
`Constrained cache: decode failed for '*_hs_def_0_0' (0 bytes)` WARNs T85's
qeynos2 capture logged (12 distinct composite-sheet names, plus
`ship_actordef_0_0`) to exactly one call site:
`race_model_loader.cpp`'s `preloadModelData()`, composite-sheet trigger
(`constrainedTextureCache_->getOrLoad(compositeBaseName, emptyData,
"composite_sheet", "preloadModelData:compositeLookup")`). Whenever a
composite JSON manifest is found at `compositeEntityPath_/<name>.json` (a
lookup entirely independent of EQE1 configuration), this call deliberately
passes an empty buffer on the assumption `getOrLoad()`'s
`findCompressedEntry()` will resolve the real bytes from an EQE1 flat pack.
That assumption silently fails whenever the run has no `--eqe1-path`
configured (`eqe1Loaders_` stays empty for the whole process,
`getOrOpenEQE1()` short-circuits before ever opening anything) — confirmed
against real T76/T85 qeynos2 client captures, neither of which configured
EQE1. The message this produced was fixed by T92 (now correctly says "empty
input data", not "decode failed" — see the P50 entry above), but the
underlying caller behavior — an EQE1-only lookup with no upstream check that
EQE1 is actually available — was intentionally left alone (T92 was scoped
to the message, and its plan explicitly prohibited texture-cache
restructuring; this is caller-side, not cache-side, but still out of that
unit's scope). **Fix candidate**: gate the composite-sheet
`getOrLoad()` call on `snapshotEQE1Loaders()` (or an equivalent
"EQE1 configured" check exposed to `RaceModelLoader`) being non-empty before
issuing the empty-buffer lookup, so a composite manifest with no matching
EQE1 archive falls straight through to the existing per-part decode path
instead of generating a guaranteed-to-fail call every time. Not measured
whether this is common in production (every run that ships with
`--eqe1-path` configured, e.g. the Orange Pi target, never hits it) or
mainly a dev/test-capture artifact.

## P48 landed (2026-07-31, merged `bd0758e`): sheet-alpha recovery + a KPI-regression escalation

- **P37 regress-response option 1 EXECUTED**: masked races composite
  again via sheet alpha companions (27/86 sheets regenerated +
  deployed, backup EverQuest_ETC1_backup_20260731_p48). Pi
  real-roster p95 57.650 → 39.781ms, draws 348.3 → 111.8 — 97.4%
  recovery to the pre-P37 level. ~~AWAITING WILL: masked-composite
  bless~~ — **BLESSED by Will 2026-07-31 ("p48 contact sheet looks
  good"). The P37→P48 masked-entity arc is fully CLOSED**: correct
  masked rendering (P37+P46+P46d+P50), verified by instrument (P47),
  perf recovered (P48), blessed twice (P47 sheet, P48 composite
  sheet) —
  benchmarks/contact_sheets/p48_sheet_alpha/index.html. Known safe
  residuals: DEN default sheet resolves an unmasked variant; CAZ
  variants 1-4 not regenerated (both stay on the P37 exclusion
  fallback).
- ~~**ESCALATION — canonical-replay KPI REGRESSED on fable tip,
  PREDATES P48**~~ — **ATTRIBUTED + RE-BASELINED by P51 (2026-08-01,
  task/P51-kpi-bisect). NOT a production regression — instrument
  honesty.** H1 (P51's prime hypothesis) confirmed on the FIRST probe,
  no bisect needed: tip binary, canonical KPI spot, WITH P46c's
  `--no-entity-shader-promotion` opt-out → 32.250ms p95, squarely back
  in the old blessed band (same draws_avg as the regressed run — pure
  timing, no structural change). Mechanism: pre-P46b the benchmark's
  default-cycle entities rendered on Irrlicht's fixed-function material
  (never linked `entity_renderer.cpp`'s promotion, P44b's finding);
  P46b's `applyEntityShaderMaterialPromotion()` made the benchmark
  correctly mirror what `entity_renderer.cpp`'s `createEntity()` has
  ALWAYS done in production — every entity gets the real per-pixel-lit
  shader. Production code was never touched by this arc; only the
  benchmark's own fidelity gap closed. **Verdict: NO code change (there
  is nothing to fix — production behavior was already correct and
  unchanged throughout). The 32.24-32.5ms band is RETIRED for any
  binary at/after `4386398` (P46b); 34.3-34.6ms is the new canonical
  KPI-guard reference, effective now.** Scaling data point (e5 vs e205,
  4 Pi invocations total): the promotion cost is proportional to
  visible entity draws — negligible (~0.5ms, noise-level) at a
  realistic e5 load, only material (~2.2ms, ~6.9%) at the e205
  stress-test load the KPI guard itself uses. Full re-baselining memo,
  scaling table, and honest-KPI statement for Will in
  `refactor/perf_truth_audit.md`'s 2026-08-01 P51 section. Same-session
  P49 ride-along also closed (see P49's own entry above). CSVs/logs:
  `benchmarks/pi_runs/p51_kpi_bisect/`. Pi budget: 4 timed invocations
  of ≤10, pristine exit (drop_caches recovery from a near-floor CmaFree
  sample, no reboot needed).

## P52 (2026-08-01): GPU-sync characterization — instrumentation landed,
## Phase 2 matrix incomplete, budget exhausted, follow-up needed

Phase 1 (x86 prep) clean: flag-gated per-phase GPU timer-query + CPU-side
sync-point instrumentation (`docker/irrlicht-drm/gpu_sync_probe.h`),
zero-cost-when-off (P38 precedent), wired into both the production
renderer and `gles2_zone_benchmark --gpu-sync-probe`. Full Review in
`refactor/P52_gpu_sync_characterization.md`.

Phase 2 (Pi, after P51 released it) found and reported (full detail:
`refactor/perf_truth_audit.md`'s 2026-08-01 P52 section):

- **Definitive**: Lima has NO `GL_EXT_disjoint_timer_query` support
  (`fns=1, ext_string=0` — the capability probe's dual-signal check
  correctly caught the `eglGetProcAddress` false-positive). Any future
  GPU-side per-phase breakdown on this hardware needs a different
  technique (selective-disable A/B, P41's own method) — GPU timer
  queries are not an option here, full stop.
- **High confidence**: the CPU blocks on `glFinish()` (GPU completion
  wait), not `waitForFlipComplete()`/`eglSwapBuffers()` — swap+flip
  together total ~0.10-0.15ms vs. `glFinish()`'s ~11-90ms. This is a
  **null result for P41's swap-floor hypothesis** (perf_truth_audit
  finding (c)/(e) — `waitForFlipComplete()` named as the next suspect
  for the ~14.4-15.0ms wall-clock floor): on this measurement it's
  negligible, not the bottleneck.
- **Two real methodology bugs found mid-unit**, reported honestly rather
  than silently absorbed: (1) the original 6-invocation matrix used
  `--camera-pose` flags, which route into a structurally separate
  static-pose loop in `gles2_zone_benchmark.cpp` that skips ALL
  per-frame instrumentation (this unit's new code AND the pre-existing
  P08/P38 phase-timing/GL-counter columns) — found, fixed within budget
  by switching to `--path-file` (P51's own convention). (2) the 2-invocation
  fix run omitted the shipped-preset flags (`--merge-zone-draws
  --composite-scenegraph --eqe1-path`) that P38/P41/P51's own canonical
  replays all use — found, **NOT fixed**, budget (≤8, 2 reserve) fully
  exhausted (6 on the invalid matrix + 2 on the Bug-1 fix) before a
  correctly-flagged re-run was possible. The recovery run's own numbers
  (232.7ms avg total at e205, ~7x P51's honest 34.3-34.6ms band) must
  NOT be read as a KPI figure.
- Entity-count sweep (0/5/40/205) and fill-rate isolation (640x480/
  1024x768) — the plan's other two named attributions, and the
  orchestrator's specific ask to reproduce/refine P51's
  +0.455ms(e5)/+2.22ms(e205) entity-promotion curve — were **not
  meaningfully measured** (Bug 1 nulled the original 6-run matrix; Bug 2
  makes the 2-run recovery's entity delta uninterpretable; C/D/E/F were
  never re-run at all).

**Follow-up needed**: a small unit (2-4 Pi invocations) re-running
`A_canonical_e5`/`B_stress_e205` (budget permitting, `C_mid_e40`/
`D_zero_e0`/`E_fill_lo`/`F_fill_hi`) via
`refactor/scratch/p52_pi_gpu_sync_pathfile_fix.py` with the 3 missing
shipped-preset flags added — the only path to a real percentage-of-frame
GPU-sync attribution and a valid entity/fill-rate isolation. Ranked
options + full reasoning in `perf_truth_audit.md`'s P52 section.

CSVs (both the invalidated 6-run matrix, kept for the record with the
`--camera-pose` limitation documented, and the 2-run recovery):
`benchmarks/pi_runs/p52_gpu_sync/`. Pi budget: 8/8 invocations used (all
of the ≤8 ceiling); two SessionAbort-triggering CmaFree episodes (the
documented episodic drain pattern, P02-characterized), both recovered via
`sync && drop_caches`, no reboot, per protocol. Final pristine exit
verified: governor `ondemand` all 4 cores, `/tmp` swept, no
`gles2_zone_benchmark`/`willeq` process, both DRM devices free, final
CmaFree 85,880 kB.

**CLOSED by P52b (2026-08-01)**, `refactor/perf_truth_audit.md`'s
2026-08-01 P52b section — full 0/5/40/205 entity-count sweep on the
correct canonical replay path (`befallen_seed42_1000f.path`, P51's own
fixture — P52b also caught and fixed a *third* methodology bug: its own
starting point, P52's staged `p52_pi_gpu_sync_pathfile_fix.py`, used the
wrong path file, `befallen_fixedpose_300f.path`, which clusters entities
near a near-stationary camera and inflates draws/entities-visible ~3x)
with the full shipped preset. Ranked %-of-frame attribution delivered at
every load: `glFinish` (GPU-completion wait) is the #1 contributor
throughout (45.8%-56.6% of frame), re-confirming P52's structural
finding. The swap-floor question gets a refined answer, not a flat null:
`flip_wait_ms` is 55.2% of frame at e0 (near-tied with `glFinish`) and
falls to 0.7% by e205 — P41's display-cadence floor is real but only
visible near zero load; P52's "null result" was an artifact of never
testing e0. Entity-scaling curve (+34.377ms e0→e205, +33.289ms e5→e205)
brackets P51's own +0.455ms(e5)/+2.22ms(e205) promotion-only deltas as
expected. One open caveat carried forward: `B_e205`'s absolute magnitude
(42.811ms avg/56.420ms p95) still reads ~21-24% over the honest
34.3-34.6ms p95 band even on the corrected path/preset — plausibly a
300-of-1000-frame path-subset effect, not re-diagnosed for lack of
budget (0 reserve left of an orchestrator-extended 6-invocation
ceiling). Flagged as "directionally trustworthy, not KPI-clean" rather
than either suppressed or presented as fully validated. Not blocking:
none of the ranking/swap-floor/scaling-shape conclusions depend on this
specific magnitude. A follow-up unit re-running `B_e205` at the full
1000-frame path (matching P51 exactly) would close this residual
question if it ever becomes load-bearing for a real decision.

## STATE-LAYER SWEEP CLOSED (T39→T49, 2026-08-05) + two new findings from T49's live acceptance

**The sweep is done for the migration.** All four items are now event-fed
via `GameStateSyncBridge` instead of polled once per game tick from
`Application`: pet (T40), inventory (T42), spell (T44), and NPC
interaction — vendor/banker/trainer — (T49).
`Application::syncNPCInteractionState()` and its three `m_last*NpcId`
edge-detect members are deleted. `syncGameStateFromClient()`'s only
remaining sub-sync is `syncCombatState()`, which was never a migration
target: T39 built it as a direct mirror of `CombatManager`, the
authoritative source, and there is no "poll → event" to perform.

**T49 was a fidelity gain, not just plumbing**, and this is the measured
evidence rather than an argument: the deleted poll hardcoded
`setVendor(npcId, 1.0f, name)`, while `VendorWindowOpened` carries the
real `m_vendor_sell_rate` from the merchant packet. Observed end to end
live in both trees — server `ShopRequest response: sell_rate=1.0656` →
`/npcstate` reporting `sellRate=1.0656`. Names now also come through
`EQT::toDisplayName()` (`SummonResupplyAgent` where the poll would have
shown the raw `SummonResupplyAgent003`).

**The state had zero readers before this unit**, which is why T49 also
landed `/npcstate`. Note where it lives and why: **`action::CommandProcessor`
(registry 2), NOT `EverQuest::m_command_registry` (registry 1) where T48's
`/vendor`/`/vendorlist`/`/buy`/`/sell` live.** The two registries read
*different* `state::GameState` instances, and only `Application::m_gameState`
is fed by `GameStateSyncBridge`. Putting the reader next to T48's commands
would have made it report empty forever — the `/assist` (T28) and `cmdHail`
(T39) failure mode exactly. **This is a fresh instance of the open
"~37-command registry overlap" item**: vendor-related commands now
deliberately span two registries. Not a bug; worth knowing before anyone
"tidies" it.

### ~~NEW: `/bank` opens and self-closes instantly~~ — **FIXED by T50 (2026-08-05)**

**Closed.** `/bank` now pushes `BankerInteractIntent{targetId}` for the
current target, exactly as T48 did for vendor, so it enters the gesture's
validated handler and opens with the REAL npc id. Deliberate behaviour
change: `/bank` now requires a targeted banker to open. **The close half
does NOT go through the intent** — that handler is open-only (no close
branch, since the gesture always tries to open) — so `CloseBankWindow()`
is still called directly and never requires a target, because you may
have walked away or changed target since opening.
**Live proof, which also retires T49's caveat: `/npcstate` reported
`Banker: id=194` and held it unchanged across ~8s of movement ticks,
then `none` after close.** Original diagnosis follows.


`/bank` is reachable from the command channel, but useless live: it opens
and closes the bank window within the same processing step, every time,
in both trees.

```
[DEBUG] [INVENTORY] Opening bank window (banker NPC: 1)
[DEBUG] [INVENTORY] Player moved too far from banker (31689.8 > 15.0), closing bank
[INFO ] [MAIN] Bank window closed
```

**The proximity check itself is CORRECT and must stay** (Will,
2026-08-05: bank/train/trade all legitimately require closeness — and the
server enforces its own 200-unit `USE_NPC_RANGE2` on the same
interactions). The bug is purely *which entity it measures against*.

Root cause traced by T49 (not fixed — outside its territory):
`OpenBankWindow()` gets no npc id from `/bank`'s call site and falls back
to `bankerNpcId != 0 ? bankerNpcId : 1` — a **hardcoded dummy id of 1**.
Spawn id 1 is a real, unrelated entity (`zone_controller000` at
`(80000, 240000, -79999)`), so the next movement-tick proximity check
measures 31689.8 units to *that* and closes the window it just opened.
Pre-existing, unrelated to T49. Fixing it touches
`eq_commands_spells.cpp`/`eq_movement.cpp`.

**THE FIX IS ALREADY WRITTEN, ONE PATH OVER — this is the T34/T48 shape
again.** `BankerInteractIntent` (`renderer_intents.h:108`) **has** a
producer (`renderer_overlays.cpp:600`, the renderer gesture) and its
handler (`eq_bridge.cpp:902-940`) does it properly: validates
`npc_type`, checks `NPC_INTERACTION_DISTANCE`, checks
`class_id == CLASS_BANKER (40)`, tells the user why on each failure, then
calls `OpenBankWindow(i.npcId)` **with the real id**. `/bank`
(`eq_commands_spells.cpp:310`) bypasses all of it and calls the
no-arg overload. So the unit is: make `/bank` push
`BankerInteractIntent{targetId}` exactly as T48 made the vendor commands
push the gesture's intents. Same defect, same remedy, precedent set.

### NEW: `/train` sends `HC_OP_GMTraining` and the server never answers

`/train` validates the target (NPC, class 20-35) and calls
`RequestTrainerWindow(target_id)`; the packet is confirmed sent in both
trees against a real guildmaster (npc 1007, `Miltiades_Tal'Azroth`,
class=22, checked against the DB before spawning). **No response ever
arrived** — no `ZoneProcessGMTraining` handler log, no trainer window.
Not investigated (server-side, outside the eqemu ownership T49 was
granted, which covers DB reads and position writes rather than server
debugging). **This is the next live instance of the automation-reachability
pattern**: reachable command, unreachable outcome. T49 reported it rather
than inventing a hook, per its plan. **RESOLVED by T50 (2026-08-05) — see
the closing note at the end of this entry.**

**ROOT-CAUSED 2026-08-05 against the actual server source** (Will's
prompt: "there may be a server-defined max distance"). He was right that
such a rule exists, and the investigation found the real cause is a
*sibling* silent-return in the same handler.

`Client::OPGMTraining()` (`akk-stack/code/zone/client_process.cpp:1612`)
has **three silent `return`s, none of which send the client anything**:

1. trainer missing / not an NPC / class outside `WarriorGM..BerserkerGM`;
2. **cross-class**: `if (!RuleB(Character, AllowCrossClassTrainers))` then
   `trains_class = pTrainer->GetClass() - (WarriorGM - Warrior)` must equal
   `GetClass()`. The rule **defaults to `false`**
   (`common/ruletypes.h:174`);
3. **range**: `DistanceSquared(m_Position, pTrainer->GetPosition()) >
   USE_NPC_RANGE2`, where `USE_NPC_RANGE2 = 40000`
   (`common/features.h:215`) — i.e. **200 units**.

**T49's run died on (2), not (3).** Testchar is **class 1 (Warrior)**;
`Miltiades_Tal'Azroth` is **class 22 = PaladinGM**, which trains class 3
(Paladin). The check at (2) precedes the range check, so distance never
mattered — and `/list` in the same transcript shows every spawned NPC at
`0.0 units away`, confirming proximity was fine. **A Warrior needs a
`WarriorGM` (class 20) trainer.**

**The range rule is real and systemic though**, exactly as Will guessed:
`USE_NPC_RANGE2` gates ~10 interaction handlers in `client_packet.cpp` —
vendor (:2288), banker (:3640), tribute master (:11005) and more. The
banker money handler doesn't even trust the client's npc id; it calls
`entity_list.GetClosestBanker(this, distance)` and range-checks that,
logging a `POSSIBLE_HACK` player event on failure.

**The actionable client-side gap**: `/train` performs target/npc_type/
class-range validation but has **no distance check and no
player-class-vs-trainer-class check**, so the client cheerfully sends a
request the server will silently drop. The banker *gesture* path
(`BankerInteractIntent`, `eq_bridge.cpp:925`) does check distance and
class and tells the user why.

**CLOSED by T50 (2026-08-05).** `/train` now pre-validates both, and the
messages are actionable rather than bare refusals. Captured live against
T49's exact repro NPC:

```
Miltiades_Tal`Azroth004 trains Paladins; you are a Warrior.
You are too far away to train with Brin_Stolunger002.
```

**And the positive leg worked for the first time ever**: against
`Brin_Stolunger` (npc 1063, class 20 WarriorGM, matching Testchar's
Warrior), `/train` completed a real `HC_OP_GMTraining` round trip and
`/npcstate` reported `Trainer: Brin Stolunger (id=195)`.

**Two T50 decisions recorded here because they are live tradeoffs, not
settled truths:**

- **Distance is checked at `NPC_INTERACTION_DISTANCE` (15), not the
  server's 200.** This is deliberately STRICTER than the server: a
  trainer 15-200 units away that the server would happily train is
  refused client-side. Chosen for consistency with every other
  client-side interaction check rather than introducing a third constant.
  Revisit if it ever annoys a real player.
- **The class check is a HARD BLOCK.** `AllowCrossClassTrainers` defaults
  false, so this matches the server today — but **a server that enables
  that rule would find T50's client refusing training the server would
  allow.** No client-side configuration was built for it. That is the one
  known way this fix can be wrong.

Both legs are covered by unit tests against synthetic events instead
(`GameStateSyncBridgeTest.BankWindowOpened*`/`TrainerWindowOpened*`,
`CommandProcessorTest.NpcState_BankerOpen_*`/`NpcState_TrainerOpen_*`), so
the migration and reader are verified even where the live path is blocked.

## `#goto` same-zone teleport lands at the DB position — **SERVER-SIDE, NOT OURS** (T52, 2026-08-05)

T50 observed that GM `#goto x y z` does not land the player where asked
and filed it as a suspected eqemu quirk, without wire evidence. **T52
captured the wire and the verdict is confirmed: the client is faithful,
the substitution is the server's.** Sequence, from
`benchmarks/audits/T52/transcript_goto_primary_excerpt.txt`:

```
[ZONE] Server requests zone change: zone=36 pos=(200.0, 100.0, 5.0) type=1
[ZONE] Requesting zone change to zone 36 at (200.0, 100.0, 5.0)      <- client asks for exactly that
[ZONE] Zone change response: zone=36 pos=(21.9, -77.9, 3.0) success=1 <- server's own approval, DB position
```

`(21.9, -77.9, 3.0)` is Testchar's persisted `character_data` position.
The client requested the correct destination and applied what it was
given. **Nothing to fix client-side; do not add a compensating hack.**

Orchestrator note on how this was scoped, because the reasoning matters
more than the verdict: I first called it server-side from source reading,
Will corrected me (**the server respects whatever position the client
sends — position flows client→server in the overwhelming majority of
updates**), and that reframing was the right prior. It just happens not to
apply *here*, because the substitution occurs inside the server's
zone-change approval, before any client position update. **The wire
capture is what distinguished those two worlds; neither of us could have
from source alone.**

### What T52 fixed anyway (correctness hardening, zero live behaviour change)

Two real but **inert** client-side inversions, both verified inert before
and after in both trees:

1. **The `m_pending_zone_*` consumer** (`eq_login_world.cpp`) claimed its
   input was in server format and swapped x/y a second time. Every
   producer stores **client** format, so the swap was wrong — but the
   block is **dead code**: `is_zone_transition` is always false there,
   since `ProcessDeferredZoneChange` disconnects first. Landing position
   actually comes from the subsequent `PlayerProfile` packet on every
   zone entry, which carries its own correct swap.
2. **`RequestZoneChange`'s outbound packet build** had x/y backwards
   relative to the parse-side convention. Inert because the server never
   reads those fields to choose a destination (unsolicited requests use
   `GetClosestZonePoint` against the server's own tracked position).

**Producer B's convention, documented for the first time**: the zone-line
producer stores targets straight from the WLD-derived zone-line data,
which is already client format (`docs/CLAUDE.md` convention #3), so both
producers always agreed. `tests/test_zone_position_convention.cpp` pins
all three sites so a future edit fails a test rather than a play session.

**Implicated by the same producer/consumer pair and NOT fixed**: succor,
evacuate, and death/bind respawn (`ZoneProcessZonePlayerToBind` — a third,
previously undocumented producer, same correct convention). GM summon is a
separate opcode and untouched. None has a known live symptom.

**Evidence hygiene note**: T52 captured at `-d 6` and produced 121MB of
transcripts against ~350KB for comparable units. Repacked at merge into
excerpts plus gzipped full logs (8.1MB). **Use `-d 5` unless you
specifically need packet dumps, and capture a narrow window if you do.**

## RESOLVED by T60 (2026-08-11): the inventory character preview (3D paperdoll) is restored — was a deliberate, recorded gap (T54, 2026-08-10)

**Retitled, not deleted — history below is load-bearing.** T60 restored
`CharacterModelView` (`include/client/graphics/ui/character_model_view.h`,
`src/client/graphics/ui/character_model_view.cpp`) from `0f25cb57^`,
adapted for ownership by `IrrlichtRenderer` (lazy construction on first
inventory-popup open, never at renderer init) instead of the deleted
`WindowManager`, and wired into `renderInventoryPopup()` via a new shared
rect (`InventoryLayout::paperdollRect()`, T57's pattern) and a new
`UIRenderer::drawTexture()` blit method. The doll is fed the SAME
`EntityAppearance` the player's own in-world 3rd-person entity uses
(`entity_renderer.cpp`'s `isPlayer` branch), including
`compositeAllowed=false` to match the player's per-part (non-composite)
render path — see `refactor/T60_paperdoll_restore.md`'s Review for the
full appearance-source reasoning and render-trigger evidence (markDirty()
on appearance change / popup open / drag-rotation; proven not to render
per frame). See that plan's Review also for a real bug found and fixed in
the process: RTT textures blitted via `draw2DImage()` render upside-down
without a V-flip (GL FBO bottom-left origin vs. `draw2DImage`'s top-left
convention for ordinary images) — fixed in the new `drawTexture()`, does
not affect any pre-existing draw path.

**What the restore did NOT do: reinstate a third copy of the bone-
attachment logic.** The deleted `CharacterModelView` carried its own
`findBoneIndex`/`extractBoneRotation`/weapon-offset copy (a third,
alongside `entity_equipment.cpp` production and `tools/model_viewer.cpp`).
T60 extracted ONE shared implementation
(`include/client/graphics/eq/bone_attachment.h`), verbatim from
`entity_equipment.cpp` (the in-world/production copy — behaviour-
preservation was mandatory there), and pointed all three
consumers at it. Diffing the two surviving pre-T60 copies found real,
previously-undocumented divergence — most notably, production's shield
grip offset is a flat `0.3` constant, NOT the bounding-box-derived
`bbox_width * 0.175` the deleted paperdoll and the tool's initial
placement both used; the shared header keeps production's actual (flat)
formula, so the restored paperdoll's shield grip differs slightly from
what the pre-0f25cb57 paperdoll would have shown, but matches what the
player already sees in 3rd person today. See the plan's Review for the
full diff and the new `tests/test_bone_attachment.cpp` (17 cases,
including two that pin genuine quirks in the fallback chain: a case-
sensitivity bug that makes the `_DAG`-suffix-stripped fallback
unreachable for lowercase `"_dag"` suffixes, and a "lowercase" fallback
variant that is actually mixed-case because the stripped substring is
never re-cased). None of these quirks were changed — only pinned, per the
plan's explicit behaviour-preservation requirement for the in-world path.

**Still open, now with an instrument to diagnose it:** the held-item
misalignment Will reported (equipped items not always appearing correctly
in a character's hand) was explicitly OUT of scope for T60 — see the
paperdoll capture screenshots in `refactor/artifacts/T60/` (both x86/
llvmpipe and real Orange Pi/Mali400), which show it directly: an equipped
primary weapon renders as an untextured white blob near the character's
hand rather than a recognizable weapon model. Previously this could only
be debugged by comparing production (in-world, hard to inspect closely)
against the tool (which had ITS OWN divergent bone-lookup copy, so fixing
something there didn't reliably predict production). Now both consume the
identical shared implementation, and the paperdoll gives a third, always-
visible vantage point directly in the UI. Fixing the misalignment itself
is a future unit's work.

`CharacterModelView` — the 3D character-model preview panel inside the old
inventory window, showing your equipped appearance while you managed
items — was deleted along with the rest of the old `WindowManager`/
`WindowBase` UI tree in `0f25cb57` ("U07c2: Delete WindowManager + all old
UI window files (~35K lines)", 2026-03-15) and was **never reimplemented**
in the static-panel UI that replaced it (`include/client/graphics/ui/`,
`static_panels.h`'s `renderInventoryPopup()` — equipment/general slot
icons only, no 3D preview surface at all).

Until T54, the only trace this feature had ever existed was six
unconditionally-`GTEST_SKIP()`'d `InventoryModelViewTest` cases in
`tests/test_inventory_model_view.cpp`, added the same commit that deleted
the feature. T54 deleted that fixture (see the re-titled entry above) as
part of cleaning up dead-fixture test debt — which would have erased the
last record of the gap entirely. **This entry exists so the intent is not
lost**: Will asked explicitly that removing the skip-only tests not also
erase the memory that a paperdoll preview once existed and was removed by
design (a UI-rewrite scope cut), not lost by accident.

What would be needed to rebuild it: a render-to-texture or dedicated
viewport surface hosting an `EQAnimatedMeshSceneNode` (built via
`RaceModelLoader::createAnimatedNodeWithEquipment()`, which — see T54's
`CreateAnimatedNodeWithEquipment_Headless` test — already handles body/
armor texture variants correctly but does NOT attach weapon geometry to
the returned node; weapon attachment lives entirely inside
`EntityRenderer` via a separate `EquipmentModelLoader` mesh lookup and
would need its own path here), composited into `centerPopup`'s inventory
region alongside the existing equipment-slot grid. Not scoped or started
by this unit — this is a feature-loss record, not a design.

## NEW: static-panel UI surface still untested after T54 (2026-08-10)

T54 added the first-ever tests for the static-panel UI
(`tests/test_static_ui_panels.cpp`: `StaticUIInput::processInput()` hit-
testing for the hotbar, spell gems and inventory popup;
`StaticUIInput::processKey()`'s ESC-closes-popup path; one `EDT_NULL`
`renderInventoryPopup()` smoke test). Being honest about what's still
uncovered, since "first tests exist" easily gets misread as "now tested":

- **Spellbook and skills popup hit-testing** (`static_ui_input.cpp`'s
  `spellbook.isOpen`/`skills.isOpen` branches — row hover, page
  navigation, scroll) — zero coverage. Same file, same idiom as the
  inventory tests T54 added; not done for scope/time, not because it's
  harder.
- **The spellbook memorize click is a known no-op**: clicking a spell row
  in `static_ui_input.cpp`'s spellbook branch only `LOG_INFO`s the click
  — "would need MemorizeSpellIntent" is a comment already in the
  production code (not a T54 finding, but T54's test-writing surfaced it
  again while reading the file end-to-end; recorded here since nothing in
  this board named it before).
- **Every other panel's render function is untested**: `renderPlayerStatus`,
  `renderTargetInfo`, `renderChatPanel`, `renderHotbar`,
  `renderSpellGemPanel`, `renderBuffBar`, `renderCastingBar`,
  `renderGroupPanel`, `renderPetPanel`, `renderXPBar`,
  `renderSpellbookPopup`, `renderSkillsPopup` — only `renderInventoryPopup`
  got the optional `EDT_NULL` smoke-test treatment (plan Step 3's explicit
  "optional tail", done because time remained after the input tests were
  green).
- **A real layout-sizing defect T54's boundary tests found and left
  unfixed** (production code was read-only territory this unit):
  `InventoryPanelState`'s general-inventory row (8 slots) is **entirely
  unreachable** by mouse at the default 800x600 layout —
  `centerPopup` is only 350px tall but the equipment grid (11 rows ×
  34px + 22px header + 8px gap) needs 382px before the general row even
  starts, so `genY` (529) lands 54px below `centerPopup`'s bottom edge
  (475) and the outer popup-bounds check in `processInput()` rejects every
  point in that row before the per-slot hit test ever runs. The bottom two
  rows of the 22-slot equipment grid are partially/fully unreachable for
  the same reason. See
  `StaticUIInputTest.InventoryGeneralSlotsAreWhollyUnreachable` and
  `.InventoryEquipGridRow10IsUnreachable` in
  `tests/test_static_ui_panels.cpp` for the exact geometry and reasoning.
  Fix would be either shrinking `UILayout::centerPopup`'s slot math or
  growing `popH` — production-code territory, out of scope here.
  - **ORCHESTRATOR CORRECTION (2026-08-10, at T54 merge review) — this is
    worse than the entry above states, on two axes. Verified directly, do
    not re-derive:**
    1. **It is NOT resolution-dependent.** `popW`/`popH` are hardcoded
       `400`/`350` at `include/client/graphics/ui/ui_layout.h:131-132`,
       independent of `width`/`height` — only the popup's *origin* is
       centered. So the general row is unreachable at **every**
       resolution, not just "the default 800x600 layout". The 800x600
       framing above is an artifact of the geometry T54 happened to test
       at; it reads as a small-screen edge case, and it is not one.
    2. **It is a VISIBLE defect, not merely an input-dead one.**
       `renderInventoryPopup` (`src/client/graphics/ui/static_panels.cpp:279-280`
       and `:304`) computes `startY`/`genY` with **identical** arithmetic
       to the hit test — so those slots are *drawn* at coordinates
       outside `centerPopup`'s own background panel, i.e. the general row
       and the overflowing equipment row render outside the popup frame
       they belong to, over whatever is behind them. The render path and
       the hit-test path agree with each other and both disagree with the
       panel size; that shared constant is where a fix belongs, so the
       two cannot drift apart while being corrected.
    Consequence for scoping: this is a user-visible UI bug affecting all
    8 general inventory slots plus equipment slots 20-21 in every
    session, not a latent boundary-condition wart. It has presumably been
    live since the static-panel UI replaced the deleted windows —
    unnoticed because, until T54, nothing tested or exercised this
    surface at all. **Worth a unit; needs Will's visual acceptance since
    it changes on-screen layout.**
- **`UIAtlas`-backed rendering** (the non-fallback path — real sprite atlas
  loaded, not the null-atlas immediate-draw fallback T54's smoke test
  exercises) is untested for every panel, static UI or otherwise.

### T55 (2026-08-10) addendum — the "implicated" bullet above is RE-TITLED: convention worry CLEARED, real defect was propagation

The Index bullet and the "Implicated ... and NOT fixed" paragraph above
framed succor/evac/death-bind as sharing T52's *convention* bug (the
double x/y swap). **T55 checked this directly and it does not hold**:
succor and evacuate are client-side no-op stubs (`spell_effects.cpp:274-278`,
`:783-800`; `SpellEffect::Succor = 88` has no case at all) — all succor/evac
position work is server-driven through producer A
(`ZoneProcessZoneChange`, `eq_zone.cpp`), which T52 already fixed. Death/bind
(`ZoneProcessZonePlayerToBind`, `eq.cpp:3392`, opcode `HC_OP_ZonePlayerToBind
= 0x385e`) swaps identically and correctly, confirmed here with its own new
test (`ZonePositionConvention.ProducerC_SwapsWireOffsetsIntoClientFormat`,
`tests/test_zone_position_convention.cpp`) — T52's fix silently corrected
this producer's outbound packet too. **There was nothing left to fix on the
convention axis.** `ZoneProcessZonePlayerToBind` is added here, explicitly,
as producer C to the documented producer list (A = `ZoneProcessZoneChange`,
B = `CheckZoneLine`, C = `ZoneProcessZonePlayerToBind`) alongside producers A
and B above.

**The real defect T55 found and fixed is a different one: propagation.**
`EverQuest::SetPosition(x,y,z)` (`eq.cpp`) — the T39-era helper that writes
both the raw `m_x/m_y/m_z` members and the state layer's
`m_game_state.player()`, firing `PlayerMoved` — had **zero production
callers** anywhere in `src/`; only tests called it. Every forced-relocation
production site instead wrote the raw members directly (or, in death/bind's
case, wrote *nothing at all* — see the same-zone finding below) and never
touched the state layer. This is the exact T39 pattern ("setters were
written but never wired, while readers were repointed at the new home
anyway") surviving live, in the position path, after the T39-T49
state-layer sweep was **declared CLOSED** (`project-state.md`, 2026-08-05)
with this hole still open. T55 fixed it at the three forced-relocation
sites in its territory: the zone-in consumer (`eq_login_world.cpp`,
`WorldProcessZoneServerInfo`), death/bind (`eq.cpp`,
`ZoneProcessZonePlayerToBind`, both branches), and the pending-zone
WorldState publish for producers A and C (mirroring producer B's existing
`setPendingZone()` call). See `refactor/T55_position_state_propagation.md`
Review for the pre/post test evidence, including the CONFIRMED same-zone
death-respawn finding (before the fix, the client kept showing the
character's pre-death position with no local write at all, relying
entirely on an eventual server `PlayerProfile` round-trip).

**What remains, explicitly by decision, not oversight**: T55's scope was
held to forced relocation only. The per-frame movement writes
(`eq_movement.cpp`, `eq_entities.cpp`, `eq_bridge.cpp`,
`eq_commands_movement.cpp`) are still raw and unconverged — routing hot
movement through `SetPosition` would fire `PlayerMoved` per frame, an
event-storm/perf question needing its own investigation with measurements.
Also left alone: the `PlayerProfile` handler (`eq.cpp:2154-2156`), which
still writes only the raw `m_x/m_y/m_z` members and pushes `PlayerMoved`
directly onto the **GameStateBridge** (not through `SetPosition`, and not
onto the state layer's `PlayerState`/EventBus at all) — outside T55's
named territory (limited to `SetPosition` and
`ZoneProcessZonePlayerToBind` within `eq.cpp`), and deliberately not
touched given the login-path ordering risk the unit plan flagged.

### T58 (2026-08-11) addendum — the `PlayerProfile` gap left open above is CLOSED; per-frame movement writes remain the only unconverged piece

T55 flagged, and deliberately left alone, `ZoneProcessPlayerProfile`
(`eq.cpp:2154-2188`) — the packet that determines landing position on
*ordinary* zone-in, still writing only the raw `m_x/m_y/m_z/m_heading`
members with no state-layer write. T58 traced the risk T55's own plan
called out by name — that swapping the raw writes for `SetPosition`/
`SetHeading` while leaving the existing hand-rolled `m_bridge->pushEvent`
publish alone would double-publish `PlayerMoved` to the renderer — and
found it **does not apply**: `EverQuest::m_game_state`'s own state-layer
`EventBus` (the one `SetPosition`/`SetHeading` fire `PlayerMoved` on) has
**zero production listeners** and is never forwarded to the
`GameStateBridge` the hand-rolled push feeds. They are two genuinely
independent queues (see `refactor/T58_playerprofile_propagation.md`
Review's trace table for the full evidence chain: `PlayerState::
firePlayerMovedEvent` → `EverQuest::m_game_state`'s private `EventBus`,
vs. `EverQuest::m_bridge` → `GameStateBridge`/`GameStateSyncBridge` →
`IrrlichtBridge`/`ConsoleBridge`, wired from two entirely separate
`state::GameState` instances — `EverQuest::m_game_state` internal-only,
`Application::m_gameState` the one `ConsoleRenderer`/`AutomatedMode`
subscribe to). Fixed by routing `ZoneProcessPlayerProfile`'s position/
heading writes through `SetPosition`/`SetHeading`, unchanged x/y swap and
heading math, hand-rolled bridge publish left exactly as-is. Confirmed by
test: exactly one `PlayerMoved` still reaches the bridge per zone-in
(`ZonePositionConvention.PlayerProfile_ExactlyOnePlayerMovedReachesBridgeInBridgeHeadingFormat`),
in the same 512-scaled heading format as before.

**What remains, explicitly by decision, not oversight**: the per-frame
movement writes (`eq_movement.cpp`, `eq_entities.cpp`, `eq_bridge.cpp`,
`eq_commands_movement.cpp`) are the one piece of the position path T55
scoped out and T58 did not touch either (out of both units' named
territory) — still raw and unconverged, still an event-storm/perf question
needing its own investigation before routing hot movement through
`SetPosition`.

## RESOLVED by T62 (2026-08-11): held-item misalignment was a test-tooling defect, not a production bug — T60's "untextured white mass" repro traced to its root cause

**Retitled, not deleted — T60's "Still open, now with an instrument to
diagnose it" entry above is superseded by this finding.** T62 used the
paperdoll instrument T60 built to isolate the reported held-item
misalignment (weapon/shield equip a huge, untextured mass instead of
appearing held) and found it was **not a defect in the shared
`bone_attachment.h` grip math or in production's in-world rendering
path**. Two independent mistakes in the test/debug tooling fully explain
what T60's captures showed:

1. **T60's capture equipped database item 1000, believing it was a
   weapon.** `data/willeq.db` maps item 1000 to equipment model IT64,
   whose only texture is `sack1.bmp` — a sack/container model, not a
   weapon. Its odd, torso-sized silhouette is exactly what the capture
   showed, because it genuinely is not weapon-shaped.
2. **`ui_inventory_capture.cpp` never called
   `equipmentModelLoader.setConstrainedTextureCache()`** (it wired the
   cache for `raceModelLoader` only). `EquipmentModelLoader::
   buildMeshFromGeometry()`'s texture-upload branch is gated entirely on
   that pointer being non-null, with no fallback — so every equipment
   mesh the tool built was untextured regardless of which item was used.
   `tools/model_viewer.cpp` had the identical gap (no
   `ConstrainedTextureCache` anywhere in the file). **Production
   (`EntityRenderer::setConstrainedTextureCache`, wired from
   `IrrlichtRenderer` init) already wires this correctly for the in-world
   path** — this was never a live-game defect.

Both tools were fixed (`ui_inventory_capture.cpp`, `model_viewer.cpp`).
With a real weapon (IT1 sword, IT45 staff) and a real shield (IT202) and
texture wiring fixed, all render correctly textured, correctly scaled,
and gripped in the hand — using the shared `computeWeaponGripOffset`/
`computeShieldGripOffset` formulas **completely unmodified** from what
T60 converged (verified: `git diff include/client/graphics/eq/
bone_attachment.h` against this unit's changes is empty). Real measured
mesh dimensions (IT1: 2.746 units, one-handed branch; IT45: 5.887 units,
two-handed branch) validate the existing `3.0f` threshold rather than
contradicting it. The shield-offset divergence T60 documented (production's
flat `0.3` vs the deleted paperdoll/tool's `bbox width * 0.175`) was
re-tested with real shield data: visually near-identical for a
representative shield, so production's flat `0.3` was kept, unchanged.

**Consequence: in-world entities were never affected**, and this unit's
STOP condition ("in-world placement would change and you cannot produce
before/after evidence") did not trigger, because in-world placement
provably did not change — nothing in `entity_equipment.cpp` or
`bone_attachment.h` was touched.

Also folded in: `CharacterModelView`'s default rotation changed from
`0.0f` (profile, facing right) to `90.0f` (front-facing, facing the
camera) per Will's request — drag rotation is relative and unaffected.

See `refactor/T62_held_item_alignment.md`'s Review for the full
investigation, measured dimensions, and before/after capture paths
(review media at `/mnt/projects/fable/review/T62/`, not committed to the
repo, per the coordinator's mid-flight protocol update moving Will-facing
review media out of `refactor/artifacts/`).

**What remains, explicitly by decision, not oversight**: no
`EntityRenderer`-level in-world integration test exists (T60's honest-
limitation note, unchanged — out of scope here since `entity_equipment.cpp`
was not touched). If a genuine in-world held-item misalignment is
reported again in the future, it is not this defect — re-investigate from
scratch rather than assuming this finding covers it.

## Candidate follow-up: on-demand `/record start|stop` capture (T64, 2026-08-11, not chased)

T64 extracted the whole-session `--dump-frames-dir`/`--dump-every` video
capture flag (see the "Live x86 client bugs (anim-video-capture session)"
entry above). That mechanism is a session-lifetime flag set at launch — fine
for a planned capture, wasteful/impossible for a live session where only a
few seconds around an unplanned moment (a bug reproducing mid-session, an
interesting emergent behavior) need recording. A slash command
(`/record start`, `/record stop`) that toggles `Application`'s existing
dump-frame state at runtime (same render-thread hook, same bound) would let
whoever's driving a live session decide in the moment, without restarting
the client or guessing capture windows in advance. Deliberately out of
scope for T64 (extraction only, per its contract) — worth its own unit if
someone wants it. Would need: a command-processor entry, some way to name/
version successive recordings without overwriting, and a decision on
whether `--dump-max-frames`'s bound applies per-recording or session-wide.
## T63 (2026-08-11) live equipment verification — primary/shield render correctly; secondary-weapon-on-paperdoll does not; first-person camera unreachable from script

Live eqemu session (Testmelee, Dark Elf female, `arena` zone) settled the
question T60/T62 explicitly left open (Will's in-game report of items not
appearing properly held) with a real character holding real, resolved
items in a real session — full detail, item→model tables, and log
evidence in `refactor/T63_live_equipment_render_verification.md`'s Review
and `/mnt/projects/fable/review/T63/README.md` (media).

**Verdicts, per configuration (paperdoll evidence)**:
- Primary weapon only: **renders correctly** — gripped in right hand.
- Secondary weapon only: **defect** — never visibly renders (see below).
- Dual wield: primary renders correctly; secondary reproduces the same
  defect.
- Shield in secondary: **renders correctly** — on the arm/torso, not
  gripped in the hand; `SHIELD_POINT` resolved (bone index 11, distinct
  from `L_POINT`'s index 10 on the same character) — **no fallback to
  L_POINT** for this race, and a `wld_dump` of `global_chr.s3d` found a
  `*SHIELD_POINT_TRACKDEF` bone for all 25 playable race codes checked, so
  the fallback risk this unit was asked to characterise does not appear to
  exist for standard playable races in that file.

**Key architectural finding**: the live wire protocol's spawn equipment
slot carries a server-resolved material id (e.g. `7` for idfile `IT7`),
never the database item id (e.g. `6011`) — confirmed by log, not assumed.
`EquipmentModelLoader::getEquipmentMesh()`'s "try `item_models.json` as an
item-id map first" branch is therefore dead weight for live player
equipment; it always misses and falls through to using the value directly
as a model id, which is what's actually correct. Separately,
`data/item_models.json` itself was found stale/disconnected from the live
server's real item database (497/1863 sampled weapon/shield items had any
entry, only 3 of those matched the item's real idfile) — does not cause
any defect found in this unit (the live path never calls it with a real
item id) but is objectively wrong data if anything else reads it; worth
its own audit unit if that's ever suspected.

**Defect — needs its own fix unit**: on the paperdoll (`CharacterModelView`),
a plain (non-shield) secondary/left-hand weapon never visibly renders,
reproduced independently in two configurations (secondary-only and dual
wield) with identical bone index (10, `L_POINT`) and offset (0.51) both
times, despite the log confirming mesh build, texture load, and bone
resolution all succeeded — this is not a "not found" failure. The shield
in the same slot (different bone, `SHIELD_POINT` index 11) and the primary
weapon (bone index 6) both render fine. Flagged (not confirmed) candidate:
`character_model_view.cpp:549-552` / `entity_equipment.cpp:233-236` apply
rotation `(180,0,0)` to non-shield secondary items vs `(180,0,180)` for
shields — the shield-only extra Z rotation may be what keeps it outside
the body mesh while the plain-weapon rotation points its offset into/
behind it. Needs a controlled A/B to confirm, not asserted here. Whether
this also affects in-world (non-paperdoll, 3rd-person) rendering is
**not established** — `entity_equipment.cpp` shares the logic, but the
`arena` zone's dark default lighting made 3rd-person confirmation
impossible in this session.

**STOP, not delivered**: first-person-during-swing captures. No slash
command or renderer-bridge command wraps `ToggleCameraMode`
(`cycleCameraMode()`) — it is reachable only via a real Ctrl+F5 key event,
which `--script` cannot send. Per the plan's explicit instruction, no
scripting affordance was added to production code; this is a harness gap
for any future unit needing camera-mode control from `--script`, not a
rendering defect. Read-only correction found while chasing this:
`triggerFirstPersonAttack()`/`updateFirstPersonWeapons()` are near-no-op
compatibility stubs, not a separate first-person weapon system — actual
first-person weapon rendering reuses the same bone-attachment path already
verified here for 3rd person/paperdoll.

No production code was modified by this unit (verification-only,
`git status` clean throughout). Board entries above are the closure for
Will's open question: primary and shield equipment render correctly in a
live session; secondary-weapon-only does not, characterised precisely
enough to scope a fix unit without re-running the live session.

## `protectedRegions_` bitvector is permanently all-false in production (T67 side-finding, 2026-08-11) — census done by T79 (2026-08-12), NOT removed, decision deferred

- Found while removing `ConstrainedMeshCache` (see
  `refactor/T67_remove_constrained_mesh_cache.md`): `protectedRegions_`'s
  only writer anywhere in the codebase was `renderer_sim_bridge.cpp`'s
  `applySimulationResults()`, inside an `if (constrainedMeshCache_) {...}`
  block that never ran (the cache was never constructed). T67 removed that
  dead block; `protectedRegions_` was already permanently all-`false` in
  every shipped build before this, and remains so after (no behavior
  change from T67 itself — this is a pre-existing condition T67 merely
  made legible by deleting the last thing that made it look conditional).
- `protectedRegions_` still has live, non-cache readers in
  `renderer_zone_stream.cpp`'s `processFrameProgressiveLoad()`: Priority 3
  (door build, `doorManager_->getDoorsInRegions(protectedRegions_, ...)`)
  and Priority 4 (PVS object build, `protectedRegions_[obj.bspRegion]`
  gates). Since the vector is always all-false, both priorities are very
  likely already-dead in production for a reason **unrelated to**
  `ConstrainedMeshCache` — but doors and objects are also both built
  eagerly elsewhere (SEQ Step6/7 in `renderer_zone_load.cpp`, "Build all
  object meshes eagerly inline (no deferred/PVS-gated build at runtime)"),
  so this may simply be more redundant-but-harmless dead code rather than
  a functional gap. **Not confirmed, not chased** — out of T67's scope
  (its territory was `constrainedMeshCache_` references specifically, and
  this is a step removed from that). Whoever next touches
  `processFrameProgressiveLoad()`'s Priority 3/4 should check whether
  `protectedRegions_` is worth resurrecting (e.g. repurposed from the
  eager SEQ-step region-visibility data) or whether Priority 3/4 are
  themselves dead code that should follow the mesh cache out.
- **T79 (2026-08-12) resolved the "not confirmed, not chased" question above
  and confirmed the harder case, not the easy one.** All 5 read sites
  (`renderer_zone_stream.cpp:494,537,547,755,765`, all inside
  `processFrameProgressiveLoad()` and `checkProgressiveLoadingComplete()`)
  are live, reachable production code — not test-only, not behind any
  cache-related gate — and every one of them is a permanent no-op because
  `protectedRegions_` is always empty/false: Priority 3 (door build) and
  Priority 4 (object build) loop bodies never execute, and the
  `unbuiltDoors`/`unbuiltObjects` progressive-load status counters are
  permanently pinned to 0 regardless of actual pending state. **Left
  in place, not removed** — per the plan's own framing, live consumers
  that used to read a real signal are exactly the case where deleting the
  bitvector would be inventing a decision ("these consumers should also
  go") that isn't this unit's to make; STOP condition invoked for the
  removal question specifically. What T79 *did* do: added inline comments
  at all 5 read sites (was previously only documented at the declaration)
  making the always-false status and its consequence explicit at the point
  of use, so the next reader doesn't have to re-derive it. Two real open
  questions for whoever picks this up next: (1) doors/objects are also
  built eagerly at zone load (SEQ Step6/7), so whether the Priority 3/4
  dead paths are a functional gap depends on whether anything ever needs
  the progressive/PVS-triggered fallback that eager build doesn't already
  cover — not established either way; (2) if Priority 3/4 are kept, they
  need a real protection/PVS signal to replace `protectedRegions_` (T79
  did not invent one — out of scope); if the eager build is judged
  sufficient on its own, Priority 3/4 and the two status counters should
  be removed together with `protectedRegions_` rather than left as dead
  camouflage. Visual gate confirmed unaffected (expected — no behavior
  changed, only comments added). See
  `refactor/T79_tooling_cleanup.md` Review for the full consumer table.

## `check_file_sizes.sh` has no handling for deleted files in the diff (T67 finding, 2026-08-11) — **FIXED by T79 (2026-08-12)**

- `scripts/refactor/check_file_sizes.sh` (`refactor/EXECUTOR_PROTOCOL.md`'s
  verification-bar step 6) computes its file list from
  `git diff --name-only $BASE -- '*.cpp' '*.h'`, which includes files
  deleted since `$BASE`. For each, it then does `[ -f "$path" ] || ...`
  and prints `ERROR $f: file not found ... — pass absolute paths or run
  from the target worktree` with exit status 2 — worded as if the caller
  made a mistake, when the actual cause was a legitimate deletion in the
  diff. T67 deleted `constrained_mesh_cache.h`/`.cpp` and hit exactly
  this: 2 spurious "file not found" lines alongside 19 genuinely-checked
  files, `0 fail` in the real summary line, but a nonzero exit code that
  a caller not reading the output closely could mistake for a real
  regression. Also independently confirmed while running this same step:
  the script's `repo_root="$(git rev-parse --show-toplevel 2>/dev/null ||
  pwd)"` has no `-C <path>` anchor, so invoking it via `bash
  <slot>/scripts/...` from a shell whose cwd is NOT inside that slot
  (e.g. the Bash tool's ambient cwd) silently resolves the wrong repo
  root and reports "base revision does not resolve" even though the
  revision is fine — it just checked the wrong directory. Both are
  script-only findings; no production code involved. A future
  units-infra pass could special-case a deleted path as
  `del $f: removed (not checked)` instead of an ERROR, and add `-C`
  support (or a `--repo <path>` flag) so the script doesn't depend on
  ambient shell cwd. Not fixed here (out of T67's scope).
- **FIXED by T79 (2026-08-12):** both defects landed in one script edit.
  Deleted paths are now excluded via `git diff --diff-filter=d` before the
  per-file check ever runs (no more spurious "file not found" for a
  legitimate deletion). The cwd-anchor problem is fixed by anchoring
  `repo_root` to the script's own on-disk location by default (it lives at
  `<repo>/scripts/refactor/check_file_sizes.sh`, so its grandparent's
  parent is the repo), with an explicit `-C`/`--repo <path>` override for
  callers that want a different tree. Verified by running it exactly as
  `refactor/EXECUTOR_PROTOCOL.md` documents — `bash
  <slot>/scripts/refactor/check_file_sizes.sh fable` — from an ambient cwd
  OUTSIDE the slot (`/mnt/projects/fable`), no wrapper, no `cd`/`env -C`/
  absolute-path workaround: it correctly resolved `repo_root` to the slot
  and reported the slot's real diff against `fable`. Also verified the
  deleted-file case directly (temporarily deleted a real tracked header,
  confirmed it's silently excluded from the check rather than reported as
  an error, then restored it). See `refactor/T79_tooling_cleanup.md`
  Review for the transcript.

## Spell-effects rewrite arc, step 2 of N: `spells.eff` reader landed, the data-driven visual layer is still all ahead (T70, 2026-08-11)

- T68 (step 1) got spell *mechanics* data into `SpellData::effects`. T70
  (step 2, `refactor/T70_spells_eff_reader.md`) added a validated,
  index-by-`SpellAffectIndex` reader for the client's *visual* effect
  definitions (`spells.eff`) — `include/client/spell/spells_eff_reader.h`
  + `src/client/spell/spells_eff_reader.cpp`, 18 tests, no rendering wired
  up (deliberately out of scope, per the plan's own emphasis #1).
- What remains is the bulk of the arc: a data-driven visual layer that
  actually consumes this reader to replace the current 10-resist-type x
  5-school hardcoded vocabulary (`spell_visual_fx.cpp:190-219`,
  `:879-918`) with per-spell effects sourced from `spells.eff` records via
  `SpellData::spell_affect_index`. That includes deciding what to render
  for the ~800 unknown bytes per emitter block (T70 proved structure, not
  semantics — see its Review) and what to do for the 148 real spells
  whose `spell_affect_index` resolves to an empty padding record (T70
  Review: indices 70, 71, 80, 84, 88 — likely answer is "fall back to the
  existing resist/school default," but that is a rendering-unit decision,
  not made here).
- Separately, and explicitly out of scope for T70 (different file,
  different format, different consumer — folding it in would have muddied
  T70's evidence): `tools/model_viewer_spell_bar.h:274` hardcodes
  `ENTRY_SIZE = 256` for **`spellsnew.eff`**, where the correct value is
  **268** (`112024 / 268 = 418` exactly; `256` gives `437.59` and drifts).
  This is why that tool carries "skip entries that look like fragments"
  logic — the fragments are drift artifacts from the wrong stride, the
  same class of bug T70's plan diagnosed in `48ae032b`'s `spells.eff`
  loader. Not touched by T70; flagged here for whichever future unit picks
  up `spellsnew.eff`.
  **FIXED by T79 (2026-08-12):** `ENTRY_SIZE` corrected to 268, and the
  "skip fragments" workaround it motivated removed — confirmed a no-op at
  the correct stride (0 of 418 records skipped). Entry counts measured
  against the real `spellsnew.eff` (112,024 bytes): before (256, with the
  fragment-skip hack) 437 records, 22 "kept" (mostly garbled fragments like
  `"P\xef\xbf\xbd"`, `"keLight85."`, `"106 V2"`); after (268, hack removed)
  418 records, 416 kept (2 filtered as too-short/"None"), whole clean names
  throughout (`"Clinging Darkness"`, `"Frost Breath"`, `"Rain_Fall"`, etc).
  See `refactor/T79_tooling_cleanup.md` Review for the full before/after
  sample.
## Memory categories with no configured budget (T69, 2026-08-11) — Will's ruling needs a follow-up per-category

- T69 (`refactor/T69_memory_enforcement_gate.md`) wired
  `IrrlichtRenderer::getMemoryReportBreakdown()`'s per-subsystem byte tallies
  into `scripts/ci/visual_gate.py` via `gles2_zone_benchmark.cpp`'s
  `--memory-json`. Will's ruling was per-subsystem budgets, defined and
  enforced, no global cap. T69 could only **enforce** the two categories
  that already have a real configured ceiling in
  `ConstrainedRendererConfig`: `textureMemoryBytes` (texture cache, H02) and
  `framebufferMemoryBytes` (framebuffer, new in T69). Every other category
  the gate now measures has **no configured budget at all** — T69 could
  only baseline-guard them (exact-match regression detection against a
  blessed value, zero tolerance measured from 9 repeat runs across all 3
  golden zones, see the T69 Review), which catches drift but not "is this
  too much." This is the concrete list for Will to rule on next, same shape
  as the texture-cache/framebuffer precedent:
  - `zoneGeometryBytes` (region/single-mesh vertex+index bytes)
  - `placeableObjectsBytes` (currently 0 for the golden zones — none load
    `--objects`)
  - `s3dZoneSourceBytes` (CPU-side S3D geometry/BSP/animation data retained
    for mesh rebuilds)
  - `gpuTextureBytes` (actual GPU-side texture allocations — related to but
    distinct from the texture-cache budget, which only bounds the
    *cache's* accounting, not total GPU texture memory including
    non-cached driver textures)
  - `vboEboBytes` (GPU hardware buffers)
  - `eqe1MappedBytes` (EQE1 flat pack mmap pool — currently 0 in the gate's
    runs since the manifest's benchmark invocation doesn't pass
    `--eqe1-path`; a real budget question here is coupled to whether that
    changes)
  - `characterModelsBytes` (RaceModelLoader total — currently 0 in the
    gate's runs since none of the 3 golden zones load entities; only
    populated when a future zone config does)
  - Not measured by the gate at all, and so not even baseline-guardable
    today: entity per-instance data, equipment models, HCMap/collision,
    audio (SFX/sound-buffer/soundfont/music/PFS), network, door/spell-DB
    game-data estimates. `gles2_zone_benchmark`'s `ZoneBenchData` never
    constructs these subsystems (no collision map load, no AudioManager,
    no game-state entities in the golden zones' plain-geometry runs) — see
    `tools/hwcaps/zone_bench_memory.cpp`'s block comment for the full
    reuse-vs-not breakdown. Getting CI coverage on these would need either
    the benchmark tool to construct more of the subsystem graph, or a
    live-client-based memory harness distinct from this offline benchmark.

  **T80 update (2026-08-12) — "no configured budget" and "not exercised by
  the gate" were conflated above; they are different problems with
  different fixes, and this paragraph separates them.** T80
  (`refactor/T80_memory_gate_coverage.md`) added a SEPARATE memory-coverage
  benchmark invocation (`--eqe1-path`/`--objects`/`--entities`/
  `--test-races`, never added to the golden-image runs — see that plan's
  Review for why) that exercises three of the categories this entry called
  "currently 0": `eqe1MappedBytes`/`eqe1ArchiveCount`, `placeableObjectsBytes`,
  `characterModelsBytes` all now measure real, nonzero, bit-identical-across-
  repeat-runs values (proposed baseline table in the T80 Review, pending
  Will's sign-off same as everything else here). **That fixes "not exercised
  by the gate."** It does **not** fix "no configured budget" — none of these
  three categories, nor any other bullet in this list, gained a
  `ConstrainedRendererConfig` ceiling; they remain bucket-(b)
  baseline-guarded (regression detection against a blessed value) exactly
  like `zoneGeometryBytes`/`s3dZoneSourceBytes`/`gpuTextureBytes`/
  `vboEboBytes` above. So: two axes, not one --
    - *exercised?* eqe1/placeableObjects/characterModels: now YES (T80).
      Everything else in this list: still NO except zoneGeometryBytes/
      s3dZoneSourceBytes/gpuTextureBytes/vboEboBytes, which were already
      exercised by the plain golden-image runs (T69) — those never needed
      T80's separate invocation.
    - *budgeted?* Every category in this list, including the three T80 just
      turned on: still NO. This list is exactly as long as it was after
      T69 for the budget question — T80 changed nothing here. The budget
      ruling remains open and is Will's call, per T69's original framing;
      T80 did not attempt it (out of that unit's Territory too).
    - T80 also fixed one bug while extending coverage: `eqe1MappedBytes`'s
      T69-era `computeMemorySnapshot()` counted only `eqe1ArchiveCount` and
      left `eqe1MappedBytes` hardcoded at 0 with a comment claiming
      `EQE1Loader` exposed no per-loader byte accessor -- that accessor
      (`EQE1Loader::getMappedBytes()`, `eqe1_loader.h`) has existed since
      P04, well before T69; the comment was simply wrong. Fixed in
      `tools/hwcaps/zone_bench_memory.cpp` (now sums `getMappedBytes()`
      across `ZoneBenchData::eqe1Loaders`, the same accessor and the same
      summation `IrrlichtRenderer::getEQE1MappedBytesTotal()` performs in
      production).

## Pi per-device memory budgets (T69, 2026-08-11) — natural follow-up, explicitly out of scope

- T69's memory-ceiling gate runs entirely on x86/llvmpipe (per its plan,
  Pi was explicitly out of scope: "ceilings measured on x86/llvmpipe do not
  transfer to Mali400, and the Pi runs through a separate tier-2 harness").
  The `ConstrainedRendererConfig` budgets the gate now enforces
  (`textureMemoryBytes`, `framebufferMemoryBytes`) are preset-derived
  (OrangePi preset, same one production ships) and therefore ARE the real
  Pi numbers in config terms — but nothing in this unit measures Pi's
  *actual* usage against them; `scripts/pi/pi_common.py`'s tier-2 harness
  is the natural home for that, mirroring what T69 did here for x86.

## Untracked-RSS gating recommendation (T69, 2026-08-11) — reported, not gated

- `totalEstimate` vs process RSS ("untracked" = RSS minus every tallied
  category) is now emitted, baselined, and printed prominently by
  `scripts/ci/visual_gate.py`'s verify() for every golden zone, but
  **deliberately not asserted** (per the T69 plan's explicit instruction —
  see its Step 4 and the T69 Review for the full repeat-run numbers).
  Measured data (9 runs across 3 zones, this x86/llvmpipe dev VM): every
  byte-tally category (texture cache, zone geometry, S3D source, GPU
  textures, VBO/EBO, framebuffer, `totalEstimate`) was bit-identical run to
  run with zero variance. Only `processRssBytes`/`untrackedBytes` varied,
  and by a small amount relative to RSS (roughly 0.02%-0.2% across the 5
  qeynos2 repeats, similar for gfaydark/befallen) — RSS itself ranged
  ~168-198MB depending on zone, untracked was consistently ~75-80% of RSS
  (~140-153MB) across all three zones. That untracked fraction being large
  AND stable is itself the interesting finding for a future unit to chase
  (named suspects per `renderer_diagnostics.cpp`'s existing P04 comment:
  glibc allocator arena/heap fragmentation, decode/staging buffers not yet
  named as their own line, and — new since this file's last audit — the
  benchmark process's own non-renderer overhead, e.g. SQLite/willeq_database
  init, Irrlicht device/scene-manager bookkeeping, which
  `getMemoryReportBreakdown()` was never meant to capture since it's a
  renderer-only tally). A future unit could plausibly gate `untrackedBytes`
  with a generous ceiling (e.g. RSS-relative, not absolute, given the
  ~170-200MB scale varies by zone) given how tight the measured spread was
  here — but T69 was explicitly instructed not to enable this on a hunch
  from one VM's data, so it is reported only. Whoever picks this up should
  gather more repeat-run data (more zones, ideally a second machine) before
  proposing a number.

## Unmerged-branch survey (orchestrator, 2026-08-11) — 16 branches audited, ONE contained work worth having

Prompted by discovering that the `--dump-frames-dir` video-capture flag had
sat unmerged on a parked branch for two weeks while T63 was reduced to
burst-capturing an attack swing and hoping a frame landed. That raised the
obvious question — **what else has been missed?** — and nobody had ever
enumerated the branches.

Method: every branch with commits not in `fable`
(`git rev-list --count fable..<branch>`), then per-branch audit of whether
the content is genuinely absent from `fable` **by comparing code, not
ancestry** — old branches are frequently superseded by an independent
reimplementation, which `git branch --merged` cannot see.

### Verdicts

- **`task/anim-video-capture`** — the one real find. Carried
  `--dump-frames-dir`/`--dump-every`, parked in July because the branch
  *also* bundled three workarounds rather than fixes (the right call at the
  time). All three were later fixed properly and independently — P23
  (`e702341`) root-caused the `saveScreenshot` guard and the
  `ParticleManager::renderUnified` race, and the missing-equipment FATAL
  became a WARN — but nobody went back for the 55 lines that were worth
  having. **Extracted by T64.** Branch content now superseded.
- **`mem`** (2 commits, 2026-01-19) — **DELETED**. Its "eviction crash fix"
  was a self-labelled `WORKAROUND` that commented out
  `driver_->removeTexture()`, leaking GPU memory on the 512MB target to
  hide a dangling pointer in Burning Video — a renderer since deleted
  entirely. `fable` fixed the same problem properly via
  `TextureEvictionListener`, which frees the memory *and* covers the
  detached-node case `mem` never found. Applying it today would reintroduce
  an unbounded texture leak on the Pi. Tip archived as tag
  **`archive/mem-20260111`** (`cd6a7712`) because the branch was unmerged
  and its commits would otherwise be garbage-collected; the tag message
  carries the full verdict. One idea there has no counterpart in `fable`
  and may be worth reviving as a fresh design: `ConstrainedMemoryBudget` +
  `IAssetCache`, a cross-asset budget coordinator. **Will has since ruled
  that per-subsystem budgets, defined and enforced, are the model and no
  global coordinator is wanted** (see T69) — so that idea is closed unless
  the ruling changes.
- **`harness/qwen-t2` / `-t3` / `-t4`** (2026-07-25) — **DELETED**, no
  archive tag; nothing salvageable in the code. These were
  model-evaluation runs, and their commit messages were the actual
  artifact. Preserved here since the branches are gone: **T2**
  (`Strings::Truncate`, 2m05s, 11 tool calls) — "27/27 tests pass BUT model
  edited tests 3x to match impl, never fixed impl; Truncate_ExactLength now
  contains a duplicated assertion, boundary untested". **T3** (`/uptime`
  slash command, 9m55s, 68 tool calls) — "chose CommandProcessor correctly
  among 3 competing command systems; diff is idiomatic; most of the run
  wasted on a /tmp mock harness that never compiled; LATENT: `game_state.h`
  uses `std::chrono` but `<chrono>` was added to the .cpp". **T4** (a
  3-line comment in `eq.cpp`, 2m23s, 9 tool calls) — "never attempted a
  whole-file read of the 165K-token file; edit landed correctly inside
  `RegisterCommands()`; did NOT verify its own work, build was run by hand
  afterward". Shas, if ever wanted from reflog: `b3e30807`, `6b80ccc0`,
  `42f1cf68`.
- **`fix-spell-effects`** (1 commit, 2026-01-08) — **mislabelled, and now
  fully superseded.** Despite the name it contributes nothing to
  `SpellData::effects` (spell *mechanics*); it parses `spells.eff`, the
  client's *visual* effect file. Its parser was heuristic — a 32-byte
  sliding scan for "Source"/"Target" markers because it missed the 8-byte
  record header and the grouping-of-three, and it **synthesised effect ids
  from an incrementing counter** rather than using the record index, which
  is the one thing the file is indexed by. Kept at survey time only because
  its parser might seed the spell-effects rewrite. **T70 has since written
  a correct reader from scratch**, so that reason is gone. Safe to delete;
  a worktree at `/home/user/projects/claude/willeq-fix-spell-effects` holds
  it open and must go first.
- **`viewer`** (2 commits, 2026-01-22) — superseded. Its
  `getBoneWorldPosition()` Y-rotation fix, which looked alarmingly relevant
  to the held-item work, is **already in `fable`** (verified: `nodeRot` /
  `cosY` present). Only its model_viewer multi-view and hand-marker changes
  may remain.
- **`task/P07c-default-flip`** — deliberately parked as rejection evidence
  for the P07 lighting arc. Accounted for; leave it.
- **`renderer`** (documented reference-only, never merge), **`codex`**,
  **`poc`**, **`metalpoc`**, **`storypoc`**, and the two ~300-commit
  `task/eqemu-*` / `task/eqcpp-*` branches — parallel or historical lines,
  not pending work.

### The systemic point

**There is no process for parked branches.** Things get parked for good
reasons, the reasons expire, and nobody revisits — the same drift class as
this file, but with no index and no hygiene pass. A survey like this had
apparently never been run.

Note the asymmetry with T53's `deferred.md` pass, which found six items
marked open that were already fixed. The branch survey skewed the other
way: things that *looked* valuable and were superseded. **Code rots faster
than notes do**, so branch audits must compare code rather than trusting a
title or a commit message.

Conventions established, for whoever runs this next:
- Audit by comparing code against `fable`, never by ancestry.
- Before deleting an **unmerged** branch, tag it `archive/<name>-<date>`
  with the verdict in the tag message — otherwise the commits become
  unreachable and are eventually GC'd. A branch whose content is already in
  `fable`'s history (like T67's `ConstrainedMeshCache` removal) needs only
  a docs pointer, not a tag.
- A worktree pinned to a branch holds it open; remove the worktree first.
## Spell-effects arc step 3 landed (T72, 2026-08-11) — texture identity and captures are what's left

- T72 built the selection layer (`spell_visual_selection.h/.cpp`:
  spell_id -> `spell_affect_index` -> spells.eff record -> per-stage
  Source/middle/Target `hasContent()` + resolved texture filenames, 12
  tests) and wired staging presence/absence into `SpellVisualFX`'s GLES2
  particle creation (`createCastGlow`/`createProjectile`/`createImpact`/
  `createImpactAtPosition` each gate on the matching stage, but only for
  `Resolved` records — the ~961-spell fallback population, measured exactly
  by the new census test, is untouched). See `refactor/T72_spell_visual_layer.md`'s
  Review for the full writeup, including a genuine upward correction of
  T70's empty-padding count (163 measured vs. 148 in T70's five-index
  table — both correct, T72's is a fuller scan).
- **Two things explicitly NOT done, both natural next steps:**
  1. **Texture identity is resolved but not rendered.** The GLES2 particle
     path (`ParticleManager`/`SpellEffectsConfig`) draws from a fixed
     15-shape procedural atlas (`spell_effects_config.cpp::buildTextureNameMap()`),
     not the real per-spell `.tga` files spells.eff names
     (`spelc1.tga` etc., verified present in `SpellEffects/` by T70). Making
     a fire nuke's particles actually *look* different from an ice nuke's
     needs either loading spells.eff's real sprites into the atlas or
     extending `ParticleManager` to bind an ad-hoc per-effect texture —
     `particle_manager.cpp`/atlas-format work, out of T72's territory
     (shared with the weather system).
  2. **`unknown_bytes` (800 bytes/emitter) is still fully unread** — T70's
     original find, untouched by T72 on purpose (STOP condition in both
     units' plans). Decoding it for timing/scale/colour/particle-rate is
     the last piece standing between this and a fully data-driven spell
     visual system; it needs real leverage (e.g. correlating against a
     captured live cast), not another blind hypothesis test.
- **No `review/T72/` media exists** — T72 STOPped on captures rather than
  taking eqemu (owned by sibling T71) or building an out-of-territory
  scripted-cast harness on a rushed budget. The staging-gate code itself
  is verified by build+test, not by pixels; a future unit with eqemu
  access (or a proper headless spell-cast capture harness, itself a
  worthwhile small unit) should capture the before/after the T72 plan
  originally asked for once texture identity is also wired.

## `spells.eff` parameter table (T73, 2026-08-11) — MOSTLY REFUTED, remainder UNDETERMINED

The recovered `archive/fix-spell-effects-20260811:.agent/spells_eff_format.md`
document claimed to decode T70's 800-byte `unknown_bytes` region as 8
fields (duration, fade-in/out, min/max scale, particles/sec, particle
lifetime, 12-float colour keyframes). T73 tested every claim directly
against the real file and `data/willeq.db`, with no clamping, per
`refactor/T73_spells_eff_parameter_decode.md`'s Review (full distributions,
semantic-correlation numbers, and pinning-test names there). Summary:

- **REFUTED**: `min_scale`/`max_scale` (0x280/0x288) — 68% of populated
  emitters have `min_scale > max_scale`, the opposite of what a min/max
  bound pair should show; negatives present, outside the document's own
  claimed range.
- **REFUTED as "colour"**: the 0x370 float[12] array — real values reach
  32.0, 12x the document's own claimed [0.0, 2.0] bound. The array's
  *location/size* (ends exactly at the 928-byte block boundary; padding
  reads a flat `(1,1,...,1)` template; populated records show a
  decay-to-1.0 pattern) is genuinely corroborated — only "colour" is wrong,
  not the field's existence. Left unnamed; likelier a scale/intensity
  keyframe curve, not proven.
- **UNDETERMINED, real, unnamed**: `duration_ms`/`fade_in_ms`/`fade_out_ms`/
  `particles_per_second`/`particle_lifetime` (0x274-0x298). No internal
  contradiction (non-negative, clusters at round values, reads exactly zero
  in both confirmed padding records 88/255). But the semantic-correlation
  test — the plan's own "strongest test" — found nothing: records
  referenced by long-lived buffs vs instant nukes overlap 33/38 with no
  systematic separation, and `duration_ms` has ~zero correlation (Pearson
  r=-0.088) with the referencing spells' `cast_time_ms`. Real structured
  data lives at these offsets; what it means is not established.
- The 500-byte gap the document ignored (block offset 0x80-0x274) is not
  simply zero (0/768 emitters all-zero) — it opens with a `0xFF`-filled run
  (matching T70's padding-template filler finding, now seen extended into
  populated records too) followed by small structured int32-looking values.
  Not decoded. Two more undocumented internal gaps were found and are
  equally unclaimed (0x284-0x288, 4 bytes; 0x28C-0x294, 8 bytes;
  0x29C-0x370, 212 bytes) — mostly zero, nonzero in a minority of records.

**Nothing was adopted.** `SpellsEffEmitter::unknown_bytes` is unchanged;
the reader gained only a doc-comment addendum and 4 new pinning tests (22
total) that read the claimed offsets directly out of the existing raw
array — no new accessors, zero behaviour change (T72, live in slot2,
unaffected). Fully data-driven spell visuals via this parameter table are
**not** unblocked by this unit. What remains for a future attempt: the
500-byte gap's small int32-looking values right after the `0xFF` run are
the most promising unexplored lead; a real packet capture of the client
triggering known spell effects (rather than static file analysis) is
probably required to close the "duration_ms means what" question, since
static analysis found no correlate to lean on.

## `gtest_discover_tests` timeout under parallel build load — FIXED by T79 (2026-08-12)

- `cmake --build` failed 3x on 2026-08-12 with `GoogleTestAddTests.cmake:
  Error running test executable` while discovering tests for
  `test_spell_manager` and `test_graphics` (both heavy binaries — the
  former links `willeq_core`, the latter also links Irrlicht) — each time
  under `-j24` parallel compile load, each time clean on an immediate
  retry with no source change. This is a **discovery-time timeout, not a
  compile error**: `gtest_discover_tests()`'s default `DISCOVERY_TIMEOUT`
  is 5 seconds, and under `-j24` CPU contention a heavy binary's
  `--gtest_list_tests` subprocess can legitimately take longer than that
  to even start, let alone finish. The failure makes `cmake --build` exit
  nonzero — the whole verification-bar step 1 reads as red — even though
  nothing is actually broken.
- Two fixes considered: raise `DISCOVERY_TIMEOUT` on the affected targets,
  or move them to `DISCOVERY_MODE PRE_TEST` (enumeration deferred to
  `ctest` invocation instead of happening as a build-time/post-build
  step). **Chose raising `DISCOVERY_TIMEOUT` to 60s** on `test_spell_manager`
  and `test_graphics` (`tests/CMakeLists.txt`), not `PRE_TEST`, because:
  `DISCOVERY_MODE` is a CMake 3.20+ keyword and this project's
  `CMAKE_MINIMUM_REQUIRED(VERSION 3.14)` (top-level `CMakeLists.txt`)
  would make that combination version-fragile for anyone building with an
  older-but-still-declared-supported CMake, where raising
  `DISCOVERY_TIMEOUT` has no such floor. **Trade-off, stated honestly**:
  raising the timeout doesn't remove the underlying contention — discovery
  still runs concurrently with `-j24` compilation, competing for the same
  CPUs — it only gives more slack before that contention manifests as a
  failure. A sufficiently loaded host (more parallel jobs, a slower CI
  runner) could still exceed 60s. `PRE_TEST` would remove the mechanism
  structurally (discovery happens after the parallel build is already
  done), at the cost of the CMake-version floor above and of a real
  discovery crash surfacing one step later (at `ctest` time instead of
  `cmake --build` time).
- **Could not provoke on demand.** A full `cmake --build -j24` from a
  fresh configure in this unit's session completed cleanly with no
  discovery failure — the timeout is load-dependent and this run's host
  conditions didn't reproduce the 3 failures from earlier the same day.
  The fix is applied based on the documented mechanism (CMake's own
  `DISCOVERY_TIMEOUT` semantics match the exact error text seen), not on
  a captured repro in this session — stated honestly per the plan's
  instruction, not claimed as proven. See
  `refactor/T79_tooling_cleanup.md` Review.

## Befallen's empty frame and stacked entities — T83 (2026-08-12): TWO UNRELATED CAUSES, one fixed, three reported open

T83 was dispatched because T82's README described Befallen screenshots as
"the fully-loaded indoor zone" when they showed a flat blue field, a sky
dome indoors, one grey placeholder cube, and a pile of overlapping NPCs —
Will caught the mismatch by opening the images. Full investigation,
before/after evidence and screenshots: `refactor/T83_entity_positions_and_missing_geometry.md`
Review, `/mnt/projects/fable/review/T83/`.

- **FIXED**: `eq_entities.cpp`'s `ZoneProcessNewSpawn` (single-spawn
  packets) was missing the `/8.0f` fixed-point scale-down that the bulk
  `ZoneProcessZoneSpawns` path already applies to the identical encoding —
  a real, independent bug. Entities delivered via individual `NewSpawn`
  packets (the majority of a populated zone, since the initial bulk burst
  only covers what's already spawned) landed 8x too far from the player.
  Verified against the server's own `spawn2` table post-fix (`Blugurg`,
  `Guard_V\`Lex` in `nektulos` now match exactly after the documented X/Y
  swap). `tests/test_zone_entities.cpp` updated to match.
- **NOT a client bug, reported only**: Befallen's 40 stacked "entities"
  (`SummonResupplyAgent`, `SummonClockworkBanker`, `Miltiades_Tal\`Azroth`,
  `Brin_Stolunger`, `Chomper`, `a_scaled_wolf_pup`, `a_rat`) are NOT
  static `spawn2` mobs (T82's README claimed this — wrong, corrected here)
  and are NOT a decode error. The server's own `zone_state_spawns` table
  (persisted ephemeral-NPC state) has 40 rows for zone 36 with
  `spawn2_id=0`, all created at the identical timestamp, all positioned at
  the player's exact spawn coordinates/heading. This is leftover test
  debris from some prior GM-summon session that got persisted and now
  respawns every zone boot. A `DELETE` to clean it up was attempted this
  unit and **blocked by the session's own permission classifier**
  (outside the "documented character-position writes" allowance) —
  left in place, reported. The `nektulos` `/list` dump has the identical
  pattern (`a_rat000`-`a_rat009` stacked on `Testmelee`), same debris
  shape, different zone — not cleaned either.
- **NOT touched, reported only (Part 4 STOP — architecture-scale)**: no
  ground/wall geometry renders near the camera in Befallen despite the
  camera measuring *inside* the loaded zone geometry's X/Y bounding box
  (camera EQ~(7.39,-64.08,13.00) vs bounds X=[-1007.6,28.0]
  Y=[-281.9,195.9]) and despite 32995-38175 polys/363 regions/8 draw calls
  genuinely submitting every frame. A distant piece of real zone geometry
  (an entrance bridge/walkway) renders correctly in the same frame,
  ruling out a global coordinate-space or camera-placement bug — this
  looks localized to region/PVS visibility selection for the player's own
  BSP region, not measured further (out of this unit's fence).
- **NOT touched, reported only**: sky dome renders in indoor zones because
  `renderer_zone_load.cpp`'s `isDungeon = (zoneType == 2)` check uses the
  wrong signal — the server already sends (and the client already stores)
  a `skyType` field that correctly says "no sky" for Befallen
  (`skyType=0`, matching the DB's own `zone.sky=0`), but sky-dome gating
  keys off `zoneType` instead, which this DB doesn't populate
  meaningfully (0/255 with no indoor/outdoor correlation across a dozen
  sampled zones). Right shape of a fix: gate on `skyType` (or whatever
  field the DB's `sky` column round-trips to), not `zoneType`. Not
  implemented — outside Part 4's fence for this unit.
- **NOT a bug, a missing test-session config field**: "Failed to load map
  for zone: befallen" (T82) was T82's own client config
  (`data/captures/t82_testchar_willeq.json`) omitting `maps_path`
  entirely, falling back to a nonexistent relative `./maps`. The real
  `.map` file exists and is valid (`/mnt/projects/akk-stack/server/maps/base/befallen.map`,
  148KB) — confirmed by re-running with `maps_path` set: "Successfully
  loaded map for zone: befallen". Every future live-testing config should
  set `maps_path`/`navmesh_path` (see `casterella.json` for the known-good
  example) or this will silently recur for every zone, every session.
- **Confirmed, not a bug**: race 457 (`SummonClockworkBanker`,
  `SummonResupplyAgent`) exceeds `willeq.db`'s `race_models` table ceiling
  (`MAX(race_id)=367`), hence the placeholder cube. Right shape of a fix
  (not implemented, Will's call per the plan): either extend the race
  table for out-of-era summoned-NPC races or leave as-is — an era-filtering
  policy question, not a bug fix.
- **Two defects looked like one shared cause and are not.** Will's working
  hypothesis (one coordinate-space/origin error explaining both the empty
  frame and the collapsed entities) does not hold: the entity stacking is
  DB-side test debris (server data, not a client decode issue at all), and
  the empty-frame issue is a region/PVS-visibility question with the
  camera already inside the correct bounding box. They happened to be
  visible in the same screenshot; they are unrelated.

## `EqActionHandler::setHeading()` double-converts degrees to EQ units (T84, 2026-08-12) — FIXED by T90 (2026-08-13)

**FIXED.** `refactor/T90_setheading_double_conversion.md`'s Review has the
full fix, the red/green test proof, and the live wire re-verification
(`/turn 90/225/45` and `/face` all now transmit exactly the values T89
established are correct). This entry (and T85's reconfirmation below it)
predate T89's confirmation that the rest of the heading stack — the
outbound `server_heading = 90 - m_heading` conversion and the inbound
`m_heading = 90 - server_heading` PlayerProfile flip — needed no change;
`setHeading()`'s double conversion, found here, was the entire defect.
Left below for the historical trail; do not re-open.

Found live, via the two-client harness T84 built and proved out (see
`refactor/T84_two_client_observation.md`'s Review, Part 1) — the exact
kind of defect a single client cannot detect, because it is
self-consistent with itself.

`src/client/eq_action_handler.cpp:86-92`, `EqActionHandler::setHeading()`:
```cpp
void EqActionHandler::setHeading(float heading) {
    // Convert from degrees (0-360) to EQ heading units (0-512)
    float eqHeading = heading * 512.0f / 360.0f;
    ...
    m_eq.SetHeading(eqHeading);   // stores the *converted* value
    ...
}
```
`EverQuest::SetHeading()` (`eq.cpp:3133`) does a bare assignment,
`m_heading = heading`, no further conversion — and every OTHER site that
reads `m_heading` (PlayerProfile zone-in, the movement-send code in
`eq_movement.cpp`, the self `ClientUpdate` echo path) treats it as a
plain 0-360 degree value. `EqActionHandler::setHeading()` is the one write
site that stores a 512-unit value into a field everyone else reads as
degrees.

Live proof (`review/T84/p1_A_turns_session.log` + `p1_B_observe_session.log`):
issuing `/turn 90` (intended: face East) produced `m_heading=128.00deg`
internally (should be 90), which the movement-send code then correctly
converted per ITS OWN (correct) formula into `server_heading=322.00deg`
on the wire — and a second, independent client (B) decoded and would
render exactly that wrong value (`heading=321.9°`, matching to within
12-bit quantization). Reproduced for all five of 0/90/180/270/45 —
each commanded value comes out `* (512/360) = 1.4222` too large before
being (correctly) sent. `/face <x y z>` / `/face <entity>`
(`ActionDispatcher::faceLocation`/`faceEntity` -> the same
`m_handler->setHeading()`) share the identical function and are affected
by direct code inspection — not independently re-verified live this
session (time-boxed), but no code-path difference would spare them. This
means T82's "`/face` CONFIRMED WORKING" finding is correct as far as it
went (the command dispatches, reaches the right handler, and the handler
returns success) but never measured the resulting orientation VALUE
against an observer, which is exactly the gap this bug lives in.

**This is NOT the deliberate, documented `eq.cpp` (self, `90 -
server_heading_deg`) vs `eq_entities.cpp`/`eq_movement.cpp` (everyone
else, direct no-flip) asymmetry** — T84 separately measured that
asymmetry live (its own Part 3) and found it exactly self-consistent, no
bug. This is a third, independent site.

**Not fixed — T84's plan explicitly prohibited any heading/rotation code
change.** Right shape of a fix (not implemented): `EqActionHandler::
setHeading()` should either store `heading` directly (degrees, matching
every other reader of `m_heading`) and let the movement-send code's
existing degrees->512-unit conversion do the one legitimate conversion,
or `EverQuest::SetHeading()` needs to make explicit which unit it expects
and enforce it at every call site — a decision for whoever picks this up,
not assumed here.

## `EqActionHandler::setHeading()` bug reconfirmed, and NO second encoding defect underneath it (T85, 2026-08-13) — FIXED by T90 (2026-08-13), see entry above

**FIXED**, same fix as the T84 entry immediately above — see there.

T85 joined a live official-client baseline capture as a third client
(`willeq_a`/`Oraclealpha`) alongside two officially-driven clients
(`Wizzy`, human-driven; `Oracledelta`, parked GM observer). Full detail:
`refactor/T85_join_official_baseline_capture.md`'s Review.

**Role 2's two-pass test (the reason to re-run this) answered the open
question the T84 finding above left unanswered**: is the wire encoder
itself sound, independent of `EqActionHandler::setHeading()`'s bug?
Pass A drove heading via `/move <x> <y> <z>` (real movement input,
verified live to route through `CalculateHeading()`/`UpdateMovement()`,
never through `EqActionHandler::setHeading()` — confirmed by matching the
log's own `CalculateHeading: ... adjusted=X°` value against the
subsequent `POS C->S [SELF] m_heading=X°` exactly). Pass B drove heading
via `/turn`, the known-buggy path (T84).

**Result: no second defect.** Both passes' `POS C->S [SELF] m_heading=X
-> server_heading=Y` line applies the identical, correct `server_heading
= 90 - m_heading` (mod 360) conversion — movement-driven and slash-
command-driven alike. Pass B reproduced T84's exact bug signature,
extended with a 6th data point (`/turn 225 -> m_heading=320.00`, i.e.
`225 * 512/360 = 320`, the same `1.4222`-too-large signature as T84's
other five). **The entire observed heading error lives at
`EqActionHandler::setHeading()`, exactly where T84 found it — the wire
encoder downstream of it is sound.** Not fixed, per this unit's own
prohibition (same as T84).

## Wizzy's ClientUpdate broadcasts never reached a third client (T85, 2026-08-13) — mechanism found by T86 (2026-08-13); T86's "real WillEQ-side defect" conclusion CORRECTED by T88 (2026-08-13), see addendum at the end of this entry

While observing (Role 1, not touching production code), T85 found that
`Wizzy` (a second officially-driven client, spawn_id 462, confirmed by
Will to be actively moving/turning through a cardinal-heading routine)
sent WillEQ's connection (`willeq_a`) exactly **one** `OP_ClientUpdate`
(0x14cb) packet — the initial zone-sync echo — and **zero** more across
~28 minutes of continuous observation spanning that exact routine.
Cross-checked two independent ways: the `-d4`/DEBUG application log
(`eq_entities.cpp`'s `POS S->C spawn_id=` line, gated only by
`s_debug_level>=2`) and a direct parse of the raw `.sleq` wire capture
(`include/common/net/session_log.h` format;
`review/T85/parse_sleq.py` + `wizzy_wire_evidence.txt`) — both show the
identical picture. In the same window, other spawn_ids (self, and
several NPCs) produced dozens to hundreds of `OP_ClientUpdate` records
each, proving the opcode path, capture, and decode handler
(`ProcessClientUpdate`) were all live and correctly working — this is
not a WillEQ decode or logging failure.

**Not root-caused — needs the oracle session's server-side pcap.**
Candidate causes (distance/interest-based server-side broadcast
filtering, GM-flag-related suppression specific to `hc_test1`, or a
subscription/spawn-list issue for third-party-account visibility) are
unconfirmed hypotheses. If the server-side pcap shows the server DID
send these broadcasts toward `willeq_a`'s socket and they still didn't
arrive/decode, that would point back at WillEQ (network layer or decode)
instead — the client-side evidence alone cannot distinguish these.
**This is exactly the defect class neither one client nor two WillEQ
clients could reach** — it needed a genuinely independent second player
character in the zone.

**T86 addendum (2026-08-13) — decisive comparison answered, and a strong
root-cause candidate found, still not fixed.** Pure post-hoc analysis of
the oracle's `capture.pcap` (no live session, no server access), reusing
`tests/pcap_test_utils.h` + `tests/daybreak_test_harness.h` rather than a
hand-rolled parser. Full evidence and tooling: `review/T86/README.md`,
`review/T86/extract_clientupdate.cpp`.

*Decisive comparison*: **YES** — `Oracledelta` (the official observer,
also GM-flagged) received 898 `OP_ClientUpdate` broadcasts for spawn 462
across the exact window WillEQ received zero (Wizzy's own C→S traffic to
the server confirms he was continuously transmitting the whole time, not
silent). This rules out GM status as the cause (Oracledelta is
GM-flagged too and unaffected) and points squarely at something specific
to `willeq_a`'s session — not the server's broadcast mechanism in
general, and not Wizzy.

*Root-cause candidate*: decoding WillEQ's own outbound self-position
wire field (`PlayerPositionUpdateClient_Struct`, C→S `OP_ClientUpdate`)
during the observation window gives `x_pos=306.462, y_pos=-646.949` —
genuine data (the tail of a smooth, continuous 23-second walk matching
T85's own reported `/move` timeline to the millisecond), not corruption.
Against ground truth decoded the same way from the same capture —
Oracledelta's own self-report exactly matches the join spec's
coordinates (`x=-653, y=314`, confirming the struct decode itself is
correct and the official client applies no swap), and Wizzy's true
position (decoded from Oracledelta's received broadcast) is
`x=-618.25, y=301.625` — WillEQ's **as-sent** wire position is ~1324.8
units from Wizzy's true position. eqemu's broadcast gate for player
`OP_ClientUpdate` (`zone/client_packet.cpp Handle_OP_ClientUpdate` →
`entity_list.QueueCloseClients(..., RuleI(Range, ClientPositionUpdates),
...)`, default 300 units) would exclude any client that far away — full
stop, no other mechanism needed. Reading WillEQ's `x_pos`/`y_pos` with
the axes swapped back — matching the deliberate, documented swap
convention already in `src/client/eq_movement.cpp:1064-1068`
(`SendPositionUpdate()`) and `src/client/eq.cpp`
(`ZoneProcessPlayerProfile`'s `SetPosition(server_y, server_x, z)`) —
puts WillEQ only ~29 units from Wizzy, matching "parked near
Oracledelta" almost exactly. The outbound code's own comments say it is
trying to undo the inbound swap bit-for-bit; the measured wire output
does not do that. **Not root-caused to a single line** (could be the
outbound un-swap, the inbound swap, or how `/move`'s arguments map to
`m_x`/`m_y` — this unit did not determine which), **and not fixed** —
static analysis + one session's wire-level numeric agreement, not a
live-tested fix, per this unit's own no-code-changes prohibition. A
follow-up unit doing a live position round-trip test (send a known
position, read back what the server believes) would settle which site
is the actual defect and confirm the fix closes the broadcast gap.

**T88 addendum (2026-08-13) — CORRECTION: not a WillEQ defect. T86's
headline "this is a real WillEQ-side defect, not a server quirk or a
Wizzy-side non-issue" does NOT hold up and must not be treated as
established.** T88 built the round-trip test T86 itself said was needed
("a live round-trip test... would settle it") — headlessly, no eqemu
required after all: fed a known server-frame position through
`ZoneProcessPlayerProfile` (the real inbound decoder) and read back what
`SendPositionUpdate()` (the real outbound encoder) actually transmits.
**The round trip is IDENTITY** — server_x/server_y come back out
bit-for-bit unchanged, over two independently-chosen coordinate pairs
(regression tests: `tests/test_position_frame_roundtrip.cpp`,
`ServerFrame_InboundThenOutbound_IsIdentity[_SecondSample]`). The
inbound swap (`m_x = server_y, m_y = server_x`) and outbound un-swap
(`x_pos = m_y, y_pos = m_x`) are a correct, matched inverse pair — there
is no encoder/decoder asymmetry to fix.

**What actually happened**: `EverQuest::Move()` (reached from `/move` via
`EqActionHandler::moveToLocation()`) operates in CLIENT frame — the same
frame as `m_x`/`m_y` — confirmed by driving the real `/move` command
surface with a client-frame target and reading back the transmitted
server-frame position (`tests/test_position_frame_roundtrip.cpp`,
`MoveToLocation_...`). T85's executor issued `/move -650 310 3.75` using
`Oracledelta`'s own **server-frame** wire self-report (which T86 itself
confirmed matches the join spec's numbers) as the argument to a
**client-frame** command. A correct client transmits that mirrored,
exactly as measured: feeding T85's exact issued values (-650, 310)
through the real `Move()` path transmits server-frame (310, -650) — T86's
own measured wire value was (306.462, -646.949), inside the documented
~2-6 unit `/move` waypoint slop. `T85Scenario_...` in the same test file
pins this arithmetic explicitly. **The 1324.8-unit miss, and the
resulting silent broadcast-radius starvation T86 correctly diagnosed as
the mechanism, were real — but the cause is a harness/command-frame
mix-up, not a WillEQ encoder or decoder bug.**

The one genuine gap this surfaced: the frame was undocumented at every
site on this call path (`Move()`, `MoveToWithPath()`,
`EqActionHandler::moveToLocation()`, the `/move` command registration).
T88 documented the frame contract at all four sites and made `/move`
echo both frames back in its chat response
(`src/client/eq_commands_movement.cpp`) plus log both frames from
`Move()` itself (`src/client/eq_movement.cpp`) — the fix that actually
addresses what cost T85 a capture session. Full evidence:
`refactor/T88_position_frame_determination.md`'s Review,
`/mnt/projects/fable/review/T88/`.

## Texture-fallback (magenta/checkerboard) defect reproduces reliably (T85, 2026-08-13) — ~~not root-caused~~ **ROOT-CAUSED + FIXED by T211 (`9588c07e`, fragment 0x2C material-list resolution), pixel-confirmed by T212 (`54b9bc72`) — annotated T215**

Observed repeatedly and independently across multiple screenshots at
multiple times in North Qeynos (large flat magenta or dark-blue/red
checkerboard covering most surfaces — walls, ceiling slabs, at least one
entity model) and, in a different color, in dark South Qeynos night
scenes (purple/black checkerboard ground). See `review/T85/README.md`
for the full screenshot-by-screenshot description. Consistent with the
documented 64MB constrained texture budget (`Constrained rendering mode:
Custom (800x600, 64MB texture...)`, logged at every startup) under memory
pressure from the known prepare-all-entities-at-load defect in a
populated zone — not confirmed, not root-caused, not touched this unit.
Combat *log* observation was unaffected (chat/combat lines arrived
correctly throughout); combat *visual* observation in these frames
cannot support any conclusion about animation or weapon appearance.

## WillEQ's outbound heading disagrees with the official client's by ~252° (T87, 2026-08-13) — SUPERSEDED: CONFIRMED ARTIFACT by T89 (2026-08-13), no code defect

**SUPERSEDED.** T89 (`refactor/T89_heading_remeasure.md`'s Review) did the
clean re-capture this entry's own "T88 addendum" called for — `/move`d
along known client-frame axes from a fixed centre so the six directions
are 90° apart by construction, eliminating the position-frame mix-up this
entry's merge-time qualification already suspected. Result: WillEQ's
transmitted N->W->S->E steps are a clean 90.0/90.0/90.0° apart (matching
the official client's own spacing), and — after T89's own initial "+90°"
read of that clean data was itself corrected (see the new entry directly
below) — WillEQ's outbound heading is now confirmed to match the official
client's convention exactly, `heading = 90 - travel_angle(server frame)`,
to within a uniform ~1.4° common-mode residual (the human reference
actor's facing not being perfectly aligned with his direction of travel,
not error in WillEQ). **There was never a ~252° (or any other) outbound
convention defect.** The entire heading error this whole T84-T90 arc
chased was `EqActionHandler::setHeading()`'s double conversion, fixed by
T90. This entry's body is left below for the historical trail (it
correctly reported what it measured, and its own merge-time qualification
already correctly predicted the artifact) — do not act on it or the
"~252°" number.

T87 answered the question T84/T85 left open: those two units proved
WillEQ is *internally* self-consistent (two WillEQ clients agree with
each other; the encoder faithfully carries a correct `m_heading` to the
wire) but neither established that WillEQ's *convention itself* is
correct, since a self-consistent misinterpretation is invisible to every
WillEQ instance. T87 compared WillEQ's outbound wire heading against the
baseline oracle capture's official Titanium client (`Wizzy`,
`10.0.30.169`, human-driven through a `/say`-marked cardinal-heading
routine) for the same six real-world facings. Full table, validation
method, and analysis scripts: `refactor/T87_heading_convention_vs_official.md`'s
Review and `/mnt/projects/fable/review/T87/`.

**Result: systematic disagreement, not a match.** WillEQ's Pass A
(`/move`-driven, the correct non-buggy path T84/T85 verified) wire
heading is offset from the official client's by **~252° (mod 360),
measured** — East/South/West/NE (the four reliable samples; North and SW
flagged, see below) show deltas 248.84°/261.42°/244.97°/253.03°, mean
252.1°, spread 16.45° (±8.2° from the mean) — tight enough to be a real
systematic pattern, not noise, but not a clean textbook constant either.
**Report only, per this unit's prohibition — nothing in
`src/client/eq_movement.cpp`, `eq_action_handler.cpp`, or any
heading/rotation/coordinate code was touched.**

Two samples flagged, not silently folded into the mean: **North**
(WillEQ's `/move` queued 12 conversion lines before settling vs 5-9 for
every other direction — noisier execution, delta 212.23° still in the
same large-offset regime but further from the clean four); **SW**
(WillEQ side reproduces T85's own already-flagged artifact byte-for-bit:
landed on the identical `m_heading` as the preceding West sample because
the prior move hadn't completed within the script's hold).

**Validated before trusting**: the WillEQ-side extraction reproduces
T84/T85's known Pass B values exactly on all six directions (including
the plan's two named checks, `/turn 90 -> m_heading 128` and `/turn 225
-> m_heading 320`). The official-client-side extraction (a from-scratch
pcap decode using the *real* `DaybreakConnection::ProcessPacket()`, not a
hand-rolled parser) has no external ground truth to check against, but
all eight of Wizzy's `/say`-marked directions independently land within
~5.5° of `docs/CLAUDE.md`'s documented 0/128/256/384-unit compass rose —
an internal-consistency check the pipeline has no way to fake.

**This is an outbound-path finding only.** It says nothing about, and
this unit did not touch, the guarded inbound `90 - server_heading_deg`
PlayerProfile flip (`eq.cpp`'s load-bearing comment, unrelated code).

**Open for a follow-up unit**: the exact root cause of the ~252° offset
(candidate: something in `CalculateHeading()`'s movement-vector-to-degrees
math producing a heading in a different reference frame than the wire
expects) is not established — this unit characterized the disagreement,
it did not root-cause it. A tighter oracle capture (longer `/move`
distances, to shrink the ~2-6 unit waypoint slop T85 already flagged)
would sharpen the ±8.2° scatter and confirm whether ~252° is a fixed
constant or direction-dependent.

**Merge-time qualification (orchestrator, `git log 381a8bd1`, still
stands, not superseded by T88)**: the "~252° offset" characterization is
an artifact, not a real transformation — a constant angular offset
preserves spacing, and Pass A's cardinals are spaced 59°/74°/101° against
the official client's clean 92°/90°/89°, so the six deltas only "average"
to ~252° over a distorted set. The likely cause, visible only by holding
T86 and T87 together: Pass A derived heading from `/move`'s direction of
travel, computed in a position frame T86 (and now T88) showed was being
fed server-frame numbers on a client-frame surface — travel-direction
headings computed from a client-frame-vs-server-frame mix-up produce
exactly this non-uniform, position-dependent distortion signature.

**T88 addendum (2026-08-13) — sequencing gate LIFTED, but the mechanism
above still needs re-checking, not blind re-trust.** T88 (see this file's
Wizzy/ClientUpdate entry above) proved WillEQ's position round trip is
IDENTITY and that `Move()`/`/move` operate correctly in client frame —
T85's `/move`-driven Pass A was itself fed server-frame numbers on that
same client-frame surface (the exact mix-up T86/T87 shared). Heading was
deliberately NOT re-measured or touched by T88 (out of territory, and the
plan's own prohibition). **A follow-up unit should re-run T87's method
now that the position-frame ambiguity at the `/move` surface is
documented and logged** (so a re-capture can confirm inputs were in the
right frame this time) — expect the corrected Pass A run to show clean,
evenly-spaced cardinal deltas if the merge-time qualification's
explanation is right, which would finally isolate whatever remains of
the outbound heading disagreement from this frame confusion.

## T89's own "+90° outbound heading offset" finding — CORRECTED same-day, confirmed artifact, never a code defect (2026-08-13)

Recorded here because T89's write-up crashed mid-session (see its own
Review's header note) and this correction never made it onto this board
until T90. **T89's plan (`refactor/T89_heading_remeasure.md`) re-measured
WillEQ's outbound heading against the official client's own wire
constants and initially concluded a clean, evenly-spaced, constant +90°
offset** (mean delta 89.09°, spread 3.16° across N/W/S/E/NE/SW) — passing
every validity check the plan asked for (uniform spacing, all six
directions distinct), which is exactly why it was initially trusted as a
real finding and written up as "NOT FIXED — needs Will's decision."

**It was wrong, and the same Review documents its own correction inline
(see the "CORRECTION" section at the end of that file).** Prompted by
Will asking whether the official client's heading could be independently
verified, the executor derived the official convention empirically from
`Wizzy`'s (the human-driven official client) own movement, rather than
from the tour script's own direction *labels*. Result: WillEQ's outbound
heading matches `heading = 91.39 - travel_angle(server frame)` (R=0.957,
83 samples) — the same formula WillEQ implements, `90 - travel_angle` —
with an identical -1.39° common-mode residual across all four cardinals
(the signature of imprecision in the human reference, not an error in the
thing being measured). **The root cause of the apparent +90°: the tour
script's own direction labels ("north"/"west"/"south"/"east") were
assigned by CLIENT-frame axis, and client +Y is not north — it is server
+X. The comparison was measuring WillEQ against mislabeled reference
directions, not against the official client's real facings.** The labels
were wrong; the code was not.

**Net effect on this arc**: no outbound heading convention defect ever
existed. Every apparent offset reported across T87 and T89 (~252°, then
+90°) was a measurement-frame artifact, not a code defect — the entire
real defect this whole arc chased was `EqActionHandler::setHeading()`'s
double conversion (T84, fixed by T90). See the T90 entry near the top of
this file and the T87 entry it supersedes.
## Nektulos's ~230s entity-prep stall: no load-time gate exists (as designed), plus a real `otherChrCaches_` accounting bug FIXED (T91, 2026-08-13) — full magnitude NOT reproduced headlessly, live re-verification still open

**Location, precisely**: `IrrlichtRenderer::loadZoneStep8_Entities`
(`src/client/graphics/renderer_zone_load.cpp:1298`) unconditionally loops
every entity in `entityRenderer_->getEntities()` (`ctx.totalEntities`,
452 for Nektulos, 171 for Befallen) and runs the full inline prep+build
chain (8a base model, 8b variant model, 8c variant textures, 8d
equipment, 8e distribute, 8f mesh build) for every single one, at load
time, on the loading thread, before the zone is playable. **No distance,
visibility, or count gate exists at this point today** — confirmed by
reading the loop, not assumed. The 40-entity render cap
(`Entities 40/171 (max 40)`) is a separate, later, render-time filter
that has no interaction with Step 8 at all.

A PVS-depth gate (`entityPrepMaxPvsDepth`,
`constrained_renderer_config.h:88`) DOES exist and IS respected — but
only by `queueEntityPrepRequests()`
(`renderer_zone_stream.cpp:676`), the **post-load, per-frame background
streaming path** used for entities that spawn or come into view AFTER
the zone is already playable (`EntityPrepWorker`). It is never consulted
by Step 8's inline load-time loop. Both shipped constrained presets
(`orangepi`, `128x32x10`) set it to 255 anyway, i.e. effectively
unbounded even where it does apply.

**Cache-thrash quantification, from T82's own captured production logs**
(`review/T82/testmelee_session4_clean_roundtrip.log` for Nektulos,
`testchar_session.log` for Befallen — real live sessions, not
reconstructed): Nektulos's Step 8 phase took **246.065s** wall-clock
(`14:30:57.928`→`14:35:03.993`, and it DID finish — T82's "still 87% when
killed" was the client being killed ~0.6s after Step 8 actually
completed, not a true hang). Of that, **230.7s (93.8%)** went to 130 of
452 entities (28.8%) whose race has no matching model in ANY archive
(placeholder-cube fallback; the same race>367-ceiling GM-summon-debris
class T83 already identified — races 330/415/432/436/440/468/471, with
race 468 alone appearing 69 times). Determining "no match exists" for
each such entity costs ~1.7–2.5s, because it walks
`global_chr.s3d`+`global2-7_chr.s3d` (7 files) from scratch, and 6 of
those 7 files were recorded as a MISS (full disk reload) **128–130 of
130 times**, despite the 7-file working set fitting comfortably inside
the configured 8-entry `chrCacheMaxEntries` cap. Befallen's same class of
entity (14 of 171, races 415/457) does NOT pay this tax: after one
934ms first-miss, every repeat lookup is an 8–11ms hit, because
Befallen's smaller/less diverse population never triggers the pattern
below. Full write-up, tables, and the headless repro's raw logs:
`review/T91/README.md`.

**Root cause of the thrash, found by source reading and fixed**:
`RaceModelLoader::preloadVariantModel()`
(`src/client/graphics/eq/variant_loading.cpp`, Step 8b's callee, gated on
a nonzero helm/robe appearance byte) had five call sites that inserted
into `otherChrCaches_` via bare `otherChrCaches_[key] = ...`, entirely
bypassing the `chrCacheLruOrder_` registration and
`maxChrCacheEntries_` eviction accounting that
`RaceModelLoader::preloadModelData()`'s own `tryLoadFromArchive` lambda
correctly performs. `config/race_models.json` confirms several races
legitimately used as Nektulos NPCs have a canonical archive from a
DIFFERENT zone than "nektulos" and not "global*" (race 22/BET →
`beholder_chr.s3d`; races 36/54/74 (RAT/ORC/PIR) → `akanon_chr.s3d`;
race 69/WIL → `airplane_chr.s3d`; race 77/NGM → `hateplane_chr.s3d`) —
exactly the condition that trips the buggy branch. An entry inserted
this way permanently occupies a map slot outside the tracked LRU budget,
forcing the legitimately-tracked global fallback chain to be evicted and
reloaded far more often than the configured cap should allow. **Fixed**:
a new `RaceModelLoader::storeOtherChrCacheLocked()` helper is now the
only way to insert into `otherChrCaches_` (5 buggy sites in
`variant_loading.cpp` + 2 previously-correct-but-duplicated sites in
`race_model_loader.cpp`, including the two failure-path early returns
that used to leave permanently-unevictable "poisoned" empty entries).

**NOT claimed as resolving the 230s** — this is the honest limit of a
headless characterization unit. A standalone repro (real
`RaceModelLoader`, real local EQ client data, T82's exact captured
race/gender call sequences replayed, no eqemu; deleted before this
unit's commit, method preserved in `review/T91/README.md`) confirms the
accounting bug by code reading and shows no regression from the fix, but
did **not** reproduce the 230s magnitude even pre-fix (both pre- and
post-fix repro runs completed in ~2s). Most likely confound: this dev
VM's OS page cache is now warm for the ~17MB of archive files (T82, T83,
and this unit all touched them repeatedly), where T82's original capture
read them cold over NFS; a secondary possibility is that the synthetic
foreign-archive trigger used in the repro doesn't exactly match the live
entities' true per-entity appearance bytes, which weren't captured in
the log. **A live eqemu re-run of T82's Nektulos scenario against this
fix is the only way to get a real before/after — open for a follow-up
unit**, ideally the same one that also revisits the Part 2 design
options below (raise-vs-gate-vs-lazy-vs-background), which remain
Will's call, not decided here.

Full plan, measurements, and Part 2 option write-up:
`refactor/T91_entity_prepare_gate.md` Review, `review/T91/README.md`.

## T93 fake-substitution sweep — EqActionHandler TRANSFORMS inventory and what remains untested (2026-08-13)

Full writeup: `refactor/T93_fake_substitution_sweep.md`'s Review,
`/mnt/projects/fable/review/T93/README.md`. Summary for future units
scoping off this board.

**The recurring pattern (same shape as T90's `setHeading()` finding):**
`RecordingActionHandler` (`tests/action_test_fakes.h`) implements every
`IActionHandler` method as a bare recording call with zero unit
conversion, validation, or state logic. `test_action_dispatcher.cpp`
exercises `ActionDispatcher` end-to-end against that fake and asserts
routing/bounds-checking at the dispatcher layer only — it never
constructs a real `EqActionHandler`. Any method where the REAL
`EqActionHandler` does actual work beyond forwarding therefore had zero
test coverage of that work, exactly like `setHeading()` before T90.

**Found and FIXED WITH TESTS this unit** (`tests/test_eq_action_handler_transforms.cpp`,
2 tests, both proven able to fail — see the plan's Review):
`castSpell(gemSlot)`/`castSpellOnTarget(gemSlot, targetId)`/
`memorizeSpell(gemSlot, spellId)` all convert a 1-indexed UI gem slot
(1..12, what `ActionDispatcher` validates and forwards) to the 0-indexed
`SpellSlot` enum (`SPELL_GEM_1 == 0`) via `gemSlot - 1` before touching
`CombatManager`. Verified end-to-end (real `EverQuest` + real
`EqActionHandler` + real `CombatManager`, no fakes) via
`CombatManager::IsCasting()`: memorizing into gem 2 only and casting gem
1 must fail, casting gem 2 must succeed (and the boundary pair 11/12).

**Investigated and explicitly deferred — no test, no seam added:**
- `EqActionHandler::targetSelf()` — real self/pet toggle branching logic
  (not a forwarder). Blocked on `CombatManager::SetTarget()` validating
  the target id against `EverQuest::GetEntities()` (EverQuest's own
  internal `std::map<uint16_t, Entity> m_entities`, separate from
  `GameState::entities()` which does have a test-populatable path). No
  existing seam adds to `m_entities`; adding one would be a second new
  production touch beyond the gem-slot fix and was judged not
  "genuinely unavoidable" for this unit's budget. Needs either a
  `Test_*ForSeam` entity-injection seam or a live-verification pass.
- ~~`setMovementMode(int)` / `setPositionState(int)` / `sendPetCommand`'s
  unchecked int-to-enum casts.~~ **FIXED WITH TESTS by T95 (2026-08-13)** —
  and T95 additionally found a live, currently-shipping bug while tracing
  `sendPetCommand()`'s call sites: most `/pet` subcommands send the WRONG
  (or now-invalid) command byte, pre-dating and unrelated to the cast fix.
  Heading: `## T95 unvalidated enum casts — fixed, plus a live /pet
  command-drift bug found along the way`.
- `targetGroupMember(int index)` — has real validation
  (`member && member->spawn_id != 0`) before dispatching; untested
  against the real `EverQuest::GetGroupMember()`.
- `hailTarget()` — real fallback branching (uses target's display name if
  resolvable, else plain `hail()`); untested against a real entity lookup.
- `clickNearestDoor()` — delegates the actual nearest-door computation to
  `GameState::doors().getNearestDoor()`, so the search logic itself is
  someone else's TRANSFORMS surface, not this method's; whether THAT
  function has direct test coverage was not checked in this sweep.

**Classified as FORWARDER, deliberately left untested (~20 of
`EqActionHandler`'s ~30 methods)**: `startMoving`/`stopMoving`,
`moveToLocation`/`moveToEntity`/`moveToEntityWithinRange`/`followEntity`/
`stopFollow`, `jump`/`sit`/`stand`/`stopAllMovement`, `targetEntity`/
`targetEntityByName`/`clearTarget`/`cycleTargets*`/`enableAutoAttack`/
`disableAutoAttack`/`toggleAutoAttack`, `hail`, `clickDoor`,
`lootCorpse`/`lootItem`/`lootAll`, `consider`, `sendChatMessage`/
`sendTell`/`replyToLastTell`, `inviteToGroup`/`inviteTarget`/
`acceptGroupInvite`/`declineGroupInvite`/`leaveGroup`, `setAFK`/
`setAnonymous`/`startCamp`/`cancelCamp`, `dismissPet`, `clickWorldObject`,
`setRoleplay`/`setPathfinding`, `setAutoHunting`/`setAutoLoot`/
`listHuntTargets`, `listEntities`/`dumpEntityAppearance`,
`sendAnimation`/`sendPositionUpdate`. Full per-method table is in
`/mnt/projects/fable/review/T93/README.md`.

## T95 unvalidated enum casts — fixed, plus a live /pet command-drift bug found along the way (2026-08-13)

Full writeup: `refactor/T95_unvalidated_enum_casts.md`'s Review,
`/mnt/projects/fable/review/T95/README.md`.

**The three casts T93 deferred are now guarded, reject-and-report (never
clamp), with 15 new tests against the real `EqActionHandler`** (all proven
able to fail — guard removed → red, guard forced to always-reject →
the complementary set goes red — then restored, verified byte-identical,
green): `tests/test_eq_action_handler_enum_guards.cpp`.

- `setMovementMode(int)` — `MovementMode` (`client/eq.h`) is a plain enum
  with **no fixed underlying type** and a contiguous range `0..2`
  (`MOVE_MODE_RUN=0`/`WALK=1`/`SNEAK=2`); an out-of-range `static_cast` into
  it is genuine UB, not just a wrong value. Reachable only from
  `CommandProcessor::cmdWalk/cmdRun/cmdSneak`, each of which passes a
  compile-time constant (`0`/`1`/`2`) — not reachable from raw user input
  today. Guarded anyway per the plan (robustness + the public
  `IActionHandler::setMovementMode(int)` surface accepts any `int` from any
  future caller).
- `setPositionState(int)` — same shape, `PositionState` range `0..4`
  (`STANDING`/`SITTING`/`CROUCHING`/`FEIGN_DEATH`/`DEAD`), reachable only
  from `cmdCrouch`/`cmdFeign`'s compile-time constants (`2`/`3`) plus a few
  internal `SetPositionState(POS_SITTING/STANDING)` call sites — none
  user-controlled today.
- `sendPetCommand(uint8_t command, uint16_t targetId)` — `EQT::PetCommand`
  (`client/pet_constants.h`) has a **fixed** `uint32_t` underlying type, so
  per the standard an out-of-range `static_cast` into it is NOT the same UB
  class as the other two (any `uint8_t` value is representable in
  `uint32_t`) — but it is still a silent-garbage-onto-the-wire defect, and
  the set is **non-contiguous** (gaps at 0, 15, and 22+), so the guard is
  membership-checked against the real enumerator list, not a min/max range
  (a range check would have wrongly accepted the gap value 15 — tested
  explicitly). Reachable from `ActionDispatcher`'s nine `/pet <name>`
  convenience wrappers, all of which pass hardcoded literals — see the live
  bug below for why that "unreachable-from-raw-input" fact doesn't mean
  harmless.

**Found while tracing `sendPetCommand()`'s call sites for reachability, NOT
fixed (out of `eq_action_handler.cpp`/enum-defs/tests territory):**
`git log -p` on `include/client/pet_constants.h` shows commit `b1c722b0`
("Fix pet commands and add animation fallback") renumbered `EQT::PetCommand`
to the correct Titanium wire values (previously `PET_ATTACK` collided with
Titanium's actual "get lost" value). That commit touched
`pet_constants.h`/`eq.cpp` only — it never touched
`src/client/action/action_dispatcher.cpp`'s nine `/pet` wrapper methods
(`petAttack`/`petBackOff`/`petFollow`/`petSit`/`petTaunt`/`petHold`/
`petFocus`/`petHealth`; `petGuard` is the one exception), which still call
`sendPetCommand()` with the OLD raw integer literals in comments claiming
the NEW enumerator names:

| Wrapper | Sends (literal) | Comment claims | Actually is | Now (post-T95) |
|---|---|---|---|---|
| `petAttack` | 2 | `PET_ATTACK`(=7) | `PET_GETLOST`(=2) — dismisses the pet | wrong command sent silently (in-range) |
| `petBackOff` | 28 | `PET_BACKOFF`(=1) | no enumerator (max is 21) | **rejected** by T95's guard |
| `petFollow` | 4 | `PET_FOLLOWME`(=8) | `PET_HEALTHREPORT`(=4) | wrong command sent silently |
| `petGuard` | 5 | `PET_GUARDHERE`(=5) | correct (coincidence — literal, not enum name) | correct |
| `petSit` | 6 | `PET_SIT`(=9) | `PET_GUARDME`(=6) | wrong command sent silently |
| `petTaunt` | 12 | `PET_TAUNT`(=11) | `PET_HOLD`(=12) | wrong command sent silently |
| `petHold` | 15 | `PET_HOLD`(=12) | no enumerator (gap value) | **rejected** by T95's guard |
| `petFocus` | 24 | `PET_FOCUS`(=19) | no enumerator (max is 21) | **rejected** by T95's guard |
| `petHealth` | 0 | `PET_HEALTHREPORT`(=4) | no enumerator (min is 1) | **rejected** by T95's guard |

`dismissPet()` (a separate `EqActionHandler` method, not one of the nine
wrappers) correctly uses the named constant `EQT::PET_GETLOST` and is fine.
Net effect: **`/pet guard` and `/pet dismiss` are the only two currently-
correct `/pet` subcommands.** The other seven were already silently sending
the wrong (or, for four of them, a value with no enumerator at all) command
byte to the server before this unit; T95's new validation makes the four
out-of-range ones start reporting "Pet command N is not valid" instead of
silently going out wrong — a visible behavior change worth flagging even
though it's a strict improvement (reject-and-report over silent-wrong, per
the plan's T77 standard). The four merely-WRONG-but-in-range ones
(`petAttack`/`petFollow`/`petSit`/`petTaunt`) are NOT caught by
range/membership validation since they're valid enumerators, just the wrong
ones; only a value-level fix in `action_dispatcher.cpp` (replace
every literal with the correctly-named `EQT::PetCommand` enumerator) closes
this. **Needs a follow-up unit** — territory would be
`src/client/action/action_dispatcher.cpp`, a one-line-per-wrapper literal
swap, cross-checked against a live `/pet` session (this finding is from
static analysis + `git log -p`, not live-verified — no eqemu was used or
needed to find it, but confirming server-side effect needs one).

**Still open from T93's original inventory, unchanged by T95** (out of
territory — `eq_action_handler.cpp` only covered the three enum casts):
`targetSelf()`'s self/pet toggle, `targetGroupMember()`'s validation,
`hailTarget()`'s fallback branch. See the `## T93` entry above.

## T96 — the `/pet` literal drift IS fixed, but the fixed code has zero live callers (2026-08-13)

Full writeup: `refactor/T96_pet_command_literal_drift.md`'s Review,
`/mnt/projects/fable/review/T96/README.md`.

**Closes the T95 `/pet` drift table above.** All nine `action_dispatcher.cpp`
wrappers now pass the named `EQT::PetCommand` enumerator instead of the
stale literal (`petAttack`→`PET_ATTACK`=7, `petBackOff`→`PET_BACKOFF`=1,
`petFollow`→`PET_FOLLOWME`=8, `petGuard`→`PET_GUARDHERE`=5 [unchanged,
already correct], `petSit`→`PET_SITDOWN`=9, `petTaunt`→`PET_TAUNT`=11,
`petHold`→`PET_HOLD`=12, `petFocus`→`PET_FOCUS`=19,
`petHealth`→`PET_HEALTHREPORT`=4). The now-redundant `// PET_X = N (from
pet_constants.h)` comments — several stale, all now redundant once the call
sites are self-documenting — were deleted rather than corrected, per the
plan's "don't reset the clock on the same failure" instruction.
`dismissPet()` was checked separately: already correct
(`EQT::PET_GETLOST`), no drift. Two new/updated tests in
`tests/test_action_dispatcher.cpp` pin every wrapper to its enumerator (both
a symbolic and an explicit-numeric assertion), proven able to fail in both
directions (mapping broken → red, restored → green, diff-verified
byte-identical to the pre-break original).

**Live-leg finding that changes how much this fix matters in practice**:
tracing the live `/pet` slash command during the required live leg found
that `ActionDispatcher::petAttack()`/etc. — the code this unit fixed — has
**zero live callers**. `Application::DispatchSlashCommand()`
(`src/client/application.cpp`) tries three command registries in a fixed
order and stops at the first match; `EverQuest::m_command_registry`
(registry 1, `"game"`) has its own, entirely separate `/pet` implementation
(`src/client/eq_commands_spells.cpp` lines ~497-602) which was **already
using the correct named enumerators**, was never drifted, and always wins
before `action::CommandProcessor`'s `cmdPet()` (registry 2, the sole caller
of the code this unit fixed) is ever tried. Confirmed both by reading
`DispatchSlashCommand()` and empirically — every `/pet` line in the live run
logs shows `registry=game`, never `registry=action`. No hotkey, UI button,
or renderer intent calls the `ActionDispatcher` pet family either. **This
is (at least) a tenth instance of the project's "implemented-but-unreached"
pattern.** The fix is still correct hygiene (dormant code that's now right
instead of wrong, and one dispatch-order change away from becoming live if
the "3 competing command systems" duplication [also flagged below] is ever
deduplicated in `eq_commands_spells.cpp`'s favor), but the live leg
demonstrates the *already-correct* `eq_commands_spells.cpp` implementation
working end-to-end (real pet attacking/backing off/following/guarding/
sitting/standing/taunting/reporting health/dismissing, with server chat
confirmation for each), not a behavior change caused by this unit's diff.

**New instance of the "3 competing command systems" duplication** (per
`docs/module_map.md`'s existing note on that pattern): `/pet` is
independently implemented in three places —
`src/client/eq_commands_spells.cpp` (live, correct), `action::
CommandProcessor::cmdPet()` → `ActionDispatcher::petAttack()`/etc. (dead,
now also correct after this unit), and (not checked this unit) whatever the
renderer command bridge might do with `/pet` if neither of the first two
recognized it. Flagged for follow-up dedup, not fixed here (out of this
unit's territory and explicitly out of scope per the plan: "do not extend
to other command families").

**`/pet hold` and `/pet focus` could not be behaviorally confirmed live** —
not a T96 defect. eqemu's `Handle_OP_PetCommands()`
(`/mnt/projects/akk-stack/code/zone/client_packet.cpp`) gates `PET_HOLD`/
`PET_FOCUS` (and the SoD+ `_ON`/`_OFF` variants) unconditionally behind
`aabonuses.PetCommands[PetCommand]`, an Alternate-Advancement bonus neither
available test character's class (Warrior `Testchar`, Wizard `Testcaster`)
can purchase (pet-command AAs belong to Necromancer/Magician/Beastlord/
Druid/Enchanter). The packet went out correctly in both cases (client log:
`"Sent pet command: hold/focus"`) but the server silently declined to act.
Reported per the "say so plainly" standard rather than routed around.

**Other fakes swept and found LOW RISK (checked, not just assumed)**:
`FakeUdpTransport` (`tests/fake_udp_transport.h`) only fakes wire I/O —
`DaybreakConnection`'s real compression/CRC/fragmentation/sequencing runs
unmodified over it in `test_daybreak_state_machine.cpp`, so the protocol
logic itself is never faked away. `FakeInputHandler`
(`tests/action_test_fakes.h`) stands in for `IInputHandler`; the real
implementations (`GraphicsInputHandler`, `ConsoleInputHandler`,
`NullInputHandler`) were read end to end and contain no unit
conversion/scaling — pure event/state forwarding, mouse-look and turn-rate
scaling live elsewhere (already covered by T90's `InputActionBridge`
finding that its callers pass plain degrees). `RecordingBridge : GameStateBridge`
(three separate inline definitions, `test_game_state_sync_bridge.cpp`/
`test_zone_connection_flow.cpp`/`test_zone_position_convention.cpp`) only
overrides the pure-virtual `applyEvent()` hook; `pushEvent`/`pushIntent`/
`drainIntents`' real thread-safe queue/coalescing logic is inherited
unmodified from the real `GameStateBridge` base, not faked. `IrrlichtBridge`/
`ConsoleBridge`'s own `applyEvent()` (the 99-case event switch) is real
production code with its own separate coverage claim (100%, per
`CLAUDE.md`) not investigated further here — out of scope for this sweep.
`RecordingOutputRenderer : IOutputRenderer` (`test_command_processor.cpp`)
substitutes for a pure display sink; every method is a rendering/UI
forwarder with no transform logic reachable from `CommandProcessor`.

**No fake found to CONTRADICT (rather than merely omit) the real
behaviour it stands for** in this sweep — worth stating explicitly since
the plan asked for it as the worst-case finding. If a future sweep finds
one, it supersedes this line.

## T97 — CommandProcessor reachability audit: 34/65 unreachable, a second intra-registry shadow found, no dead-is-better case (2026-08-13)

Full audit, no production code changed. `action::CommandProcessor` (registry
2) registers 65 commands; `Application::DispatchSlashCommand()` always tries
`EverQuest::m_command_registry` (registry 1, "game") first
(`application.cpp`). Checking every one of the 65 against `game`'s full name
space (125 primary names + 78 aliases, both directions):

**34 of 65 are unreachable under every spelling they have**: `say`, `ooc`,
`auction`, `tell`, `reply`, `gsay`, `gu`, `emote`, `loc`, `sit`, `stand`,
`camp`, `move`, `moveto`, `follow`, `stopfollow`, `target`, `cast`,
`interrupt`, `invite`, `disband`, `decline`, `who`, `help`, `quit`, `q`,
`debug`, `timestamp`, `skills`, `gems`, `mem`, `forget`, `pet`, `filter`.
**1 more (`shout`) is reachable only via a non-obvious alias** (`/sh` — its
primary is shadowed but `game`'s own shout alias is spelled `sho`, not
`sh`). **30 are fully reachable** with no collision at all (`face`,
`consider`, `hail`, `npcstate`, `door`, `afk`, `anon`, `walk`, `run`,
`sneak`, `crouch`, `feign`, `jump`, `turn`, `list`, `dump`, `hunt`,
`autoloot`, `listtargets`, `loot`, `pathfinding`, `roleplay`, `wave`,
`dance`, `cheer`, `laugh`, `logonly`, `logexclude`, `logmodule`,
`logclear`). Full table with file:line evidence:
`/mnt/projects/fable/review/T97/reachability_table.tsv`.

This is the same shape as T82's `/who` finding and T96's `/pet` finding,
generalized: roughly half of `CommandProcessor` was already dead before
this unit, discovered by two separate accidents. This audit is the
systematic sweep that names the full set.

**Second intra-`game`-registry shadow found: `follow` (G02 previously
documented only `who`).** Full sweep of every primary name across the three
`game`-registry files found exactly two duplicates in the whole registry:
`who` (known) and `follow` (new). `game`'s Social-section `follow_cmd`
(`eq_commands.cpp:607`) accepts a pending group invite on empty args;
Movement's later `follow` (`eq_commands_movement.cpp:317`, registered after,
wins) has no such branch. `action::CommandProcessor::cmdFollow` has the
identical invite-accept fallback but is separately shadowed by `game`'s.
**No reachable spelling of `/follow`, in either registry or any alias, can
accept a pending group invite today** — a real gameplay gap, not previously
flagged. Not fixed (production code is read-only for this audit unit) —
open for a future unit.

**Divergence check (the 34 shadowed pairs): no dead-is-better case found.**
Every pair was traced to its terminal call before classifying — checked, not
assumed. Result: mostly Equivalent (both sides call the identical
`EverQuest::` method — chat commands, loc/sit/stand, move/moveto/
stopfollow, invite/decline, who, help, debug, timestamp) or clearly **live
is better**, several times because the dead `action` side is a literal
stub: `cmdMem` and `EqActionHandler::forgetSpell` both return a **false
success** without doing anything; `cmdGems`/`cmdSkills` are cosmetic no-ops;
`cmdFilter` has no effect anywhere while `game`'s `/filter` pushes a real
renderer toggle; `cmdDisband` always sends `SendLeaveGroup()` with no
leader/member branch (`game`'s does); `cmdQuit` is the exact pre-T30 broken
advisory-only behavior; dead `/pet`'s vocabulary is a strict subset of
live's even after T96's literal fix. Full table:
`/mnt/projects/fable/review/T97/divergence_table.tsv`.

**Disposition is Will's call, not acted on.** Options recorded in
`review/T97/README.md` and the plan's Review: delete the shadowed `action`
implementations (smallest blast radius, loses nothing given no dead-is-better
case, but doesn't fix the `/follow` gap or `game`'s own stubs); reorder the
registries (global/risky, would flip all 34 at once, order is now confirmed
load-bearing for both `who` and `follow`); merge per command (most work,
best outcome — this audit's divergence table is the direct input); leave and
document (cheapest, defers the same discovery cost to the next accidental
find).

Verification: `cmake --build` clean (no source changes), `ctest` 2102/2103
(sole failure the documented ALSA exception), `visual_gate` PASS, ARM
cross-compile clean, `check_file_sizes.sh` correctly reports nothing to
check (zero production-code changes). Full writeup: plan `## Review`
(`refactor/T97_command_registry_shadowing_audit.md`) and
`/mnt/projects/fable/review/T97/README.md`.

**T97's disposition CLOSED by T98 (2026-08-13): "delete the shadowed `action`
implementations" — Will's ruling, executed.** All 34 `Shadowed` rows from
`review/T97/reachability_table.tsv` deleted from `action::CommandProcessor`
(registrations + `cmdX()` handlers): say, ooc, auction, tell, reply, gsay,
gu, emote, loc, sit, stand, camp, move, moveto, follow, stopfollow, target,
cast, interrupt, invite, disband, decline, who, help, quit, q, debug,
timestamp, skills, gems, mem, forget, pet, filter. `shout`
(`Reachable-via-alias`) kept, per T97's finding that its "sh" alias escapes
shadowing live (`game`'s own shout alias is "sho"). One helper,
`CommandProcessor::displayHelp()`, was also removed — its only caller was
`cmdHelp`, now deleted; nothing else called it (grepped). 31 commands
survive (30 `Reachable` + `shout`); all 31 confirmed still registered and
dispatching by every name and alias, both structurally (a new permanent
test, `CommandProcessorTest.T98_AllSurvivorsRegisteredAndDispatchByNameAndAlias`,
`tests/test_command_processor.cpp`) and live (eqemu spot-check,
`review/T98/README.md`).

**The group-invite gap T97 found is now single-sourced, not fixed.**
`action::CommandProcessor::cmdFollow`'s invite-accept fallback (deleted by
this unit) read:
```cpp
ActionResult CommandProcessor::cmdFollow(const std::string& args) {
    if (args.empty()) {
        // Accept group invite
        if (m_state.group().hasPendingInvite()) {
            return m_dispatcher.acceptGroupInvite();
        }
        return ActionResult::Failure("Usage: /follow <name> or use to accept group invite");
    }
    return m_dispatcher.followEntity(args);
}
```
`m_dispatcher.acceptGroupInvite()` chains through
`ActionDispatcher::acceptGroupInvite()` ->
`EqActionHandler::acceptGroupInvite()` -> `EverQuest::AcceptGroupInvite()`
(`eq_group.cpp:157`, the one real implementation all three call sites
share). The surviving copy of this exact behavior is `game`'s Social-section
`follow_cmd` (`eq_commands.cpp:607-638`, alias `fol`) — **still shadowed
within `game` itself** by Movement's later `follow`
(`eq_commands_movement.cpp:317-335`, no invite-accept branch), per T97's
intra-registry-shadow finding. Restoring the feature is NOT "delete
something" — it needs a `game`-registry edit (make Movement's `follow`
invite-aware, or reorder `RegisterSocialCommands()`/`RegisterMovementCommands()`,
both out of this unit's territory) — future unit, not chased here.

**`eq_bridge.cpp`'s `GroupAcceptIntent` handler (`eq_bridge.cpp:1289-1291`)
has NO live producer.** Grepped the whole tree for `GroupAcceptIntent{}` /
any construction site outside its type definition
(`include/client/events/renderer_intents.h:235`) and its
`is_same_v`-guarded handler branch: zero matches. No UI button, keybind, or
renderer code ever pushes this intent. So the gap is NOT narrower than T97's
finding suggested — there is no UI path that still works; every one of the
three call sites into `EverQuest::AcceptGroupInvite()` (the dead
`CommandProcessor::cmdFollow` copy just deleted, the dead bridge intent
handler, and `game`'s own intra-registry-shadowed `follow_cmd`) was already
unreachable before this unit touched anything except the first, which this
unit removed outright.

**Cascade: ActionDispatcher/EqActionHandler methods newly unreferenced by
production code (NOT deleted — Will's call, not a side effect of a command
cleanup, per the plan).** Checked every one of the 34 deleted commands'
dispatcher calls against the whole tree (excluding `command_processor.cpp`
itself and `tests/`) for a surviving production caller. Two calls
(`sendTell`, `moveToLocation`, `moveToEntity`, `castSpell`, `sendChatMessage`,
`sendAnimation`, `targetNearestPC`/`targetNearestNPC`) also live in
`input_action_bridge.cpp`/`application.cpp` and stay fully referenced.
The rest do not — only test callers remain for:
`sit()`, `stand()`, `startCamp()`, `replyToLastTell()`, `followEntity()`,
`acceptGroupInvite()` (the group-invite-gap method above),
`stopFollow()`, `targetNearest()` (its `targetNearestPC`/`NPC` siblings
stay live), `targetEntityByName()`, `interruptCast()` (the standalone
`SpellManager::interruptCast()` stays live via `eq_commands_spells.cpp`/
`eq_bridge.cpp`, called directly, not through the dispatcher),
`inviteTarget()`, `inviteToGroup()`, `leaveGroup()`, `declineGroupInvite()`,
`forgetSpell()` at the `ActionDispatcher`/`EqActionHandler` layer
(`SpellManager::forgetSpell()` itself stays live, called directly from
`eq_bridge.cpp`/`eq_commands_spells.cpp`, bypassing the dispatcher), and
all ten pet wrappers T96 fixed: `petAttack()`, `petBackOff()`,
`petFollow()`, `petGuard()`, `petSit()`, `petTaunt()`, `petHold()`,
`petFocus()`, `petHealth()`, `dismissPet()`. **Open question for Will**:
delete this now-fully-dead layer, or keep it as the deliberate future
scripting-surface API the plan named it as? Not decided here.

Verification: `cmake --build` clean, `ctest` 2101/2101 (net -2 vs the
2102/2103 baseline: -3 tests removed with their deleted commands [two
cmdTell-specific quoting/validation tests, one cmdTimestamp toggle test]
+1 new permanent survivor-dispatch test; one Tell-splitArgs test re-pointed
to `/face`, not counted as a net change; sole failure the documented ALSA
exception), `visual_gate` PASS (goldens untouched), ARM cross-compile clean,
`check_file_sizes.sh` clean on all 4 changed files, live eqemu spot-check of
6 survivors all showing `registry=action` as before. Full writeup: plan
`## Review` (`refactor/T98_delete_shadowed_commands.md`) and
`/mnt/projects/fable/review/T98/README.md`.

## T99: Group family migrated to `action::CommandProcessor`; group-invite-accept gap FIXED and live-verified; two new pre-existing bugs found

Closes T97/T98's open Group items. `invite`/`disband`/`decline` moved out of
`EverQuest::m_command_registry` (`eq_commands.cpp`) and into
`action::CommandProcessor`, ported from the game-registry implementation
T97 found better (disband's leader/member branch), atomically (removed
from `eq_commands.cpp` in the same commit as the `CommandProcessor`
addition — `registry=action` confirmed live for all four, `registry=game`
confirmed for an untouched command in the same session as a control).
New: `/groupaccept` (alias `/gaccept`) gives `ActionDispatcher::
acceptGroupInvite()` its first live caller — the dead Social `follow_cmd`
(`eq_commands.cpp:607-638`, T97/T98's open item above) was deleted rather
than revived, since it was itself intra-registry-shadowed by Movement's
`follow` and reviving bare `/follow` would re-create that exact collision.
Full writeup: `refactor/T99_action_layer_unification_pilot.md`'s Review,
`/mnt/projects/fable/review/T99/README.md`.

**Infrastructure gap found and fixed (outside the plan's listed territory,
judged necessary to make the plan's own live-verification requirement
possible at all — see the plan's Review for the full justification):
`GameStateSyncBridge` had no case for `GroupChanged`/`GroupMemberUpdated`/
`GroupInviteReceived`.** `Application::m_gameState`'s `GroupState` — the
instance `ActionDispatcher`/`CommandProcessor` actually read — never
received a single update from any group event before this unit, for every
prior unit that touched Group, because nothing had ever tried to read
group state live through the action layer before. This is the T49 "two
GameState instances" trap again, this time on the write side: `EverQuest`'s
own `m_game_state` mirror was always correct (confirmed live: the "Testmelee
has invited you to join a group" chat line comes from that copy and always
printed correctly), but `Application::m_gameState` was simply never told.
Fixed by adding the three missing cases to `game_state_sync_bridge.cpp`,
following the exact established pattern used for Pet/Spell/Vendor/Bank/
Trainer/Entity/Inventory. Caught by a genuinely red test before the fix
(`GroupChangedCancelInviteClearsPendingInviteWithoutTouchingGroup`) —
`GroupState::setInGroup(false)` only runs its clear-group side effect on a
value *transition*, so the cancel-invite-only case (already not in a group)
needed an explicit `clearPendingInvite()` call, not just `setInGroup(false)`.

**Two new pre-existing bugs found live, neither introduced by T99, both
out of this pilot's territory to fix (both below the action layer, in
`eq_group.cpp`/eqemu's own packet construction):**

1. **`EverQuest::AcceptGroupInvite()` reads `m_pending_inviter_name` AFTER
   passing it by const-reference into `SendGroupFollow()`, which clears
   that same member as its last action before returning** (`eq_group.cpp:
   157-163`). Aliasing bug: the parameter is a reference to the member, so
   `SendGroupFollow`'s own `m_pending_inviter_name.clear()` blanks the
   reference `SendGroupFollow` itself is about to log
   (`LOG_INFO(..., "Accepted group invite from {}", inviter_name)` prints
   empty), and `AcceptGroupInvite()`'s own subsequent chat message
   (`"You have joined " + m_pending_inviter_name + "'s group"`) reads the
   now-cleared member too. Confirmed live twice
   (`review/T99/t99_b_testbuddy_evidence.log`: "Accepted group invite from
   " and "You have joined 's group", both blank, both times B accepted).
   **Functionally harmless in this session** — the server's own invite
   bookkeeping is authoritative regardless of what name the client's
   follow-ack packet carries, and the group formed correctly both times
   (confirmed by the server's own `GroupUpdate` broadcast reporting
   `leader: Testmelee` to both clients) — but the local chat text is wrong,
   and had literally never been observed before because nothing had a live
   caller into `AcceptGroupInvite()` until this pilot. Not fixed:
   `eq_group.cpp` is below the action layer, off-limits per the plan's
   layering constraint and outside its listed territory.
2. **The group LEADER's own client sees an empty `leadersname` on the
   `GroupUpdate` packet describing its own group** — `A`'s log shows
   `leader: ` (blank) both times a group formed, while `B` (the same
   group, same packet type, different recipient) correctly shows
   `leader: Testmelee` (`review/T99/t99_a_testmelee_evidence.log` vs
   `t99_b_testbuddy_evidence.log`, same timestamps). Net effect measured
   live: `EverQuest::m_is_group_leader` stays `false` on the leader's own
   client (`m_group_leader_name == m_character` compares `"" ==
   "Testmelee"`), so `CommandProcessor::cmdDisband`'s leader/member
   message branch (ported from `game`'s `disband_cmd`, this unit) picked
   the member wording ("You have left the group") on `A`'s own screen even
   though `A` really was the leader and the packet effect was correct
   either way (`SendLeaveGroup()`/`SendGroupDisband()` are the same call,
   see the plan's Review) — confirmed by the group actually disbanding for
   both clients. Likely an eqemu server quirk (omitting a leader's own name
   from their own copy of the packet) rather than a client bug, but not
   root-caused past that; server code is off-limits to modify regardless.
   Not fixed here.

Both are reported, not chased — genuinely new findings this pilot's live
leg surfaced by being the first code path to give these dead methods a
real caller, exactly as T97/T98 predicted would keep happening.

## T101 — Spells (6) + Pet (1) families migrated; T99's recipe generalised to the hard case unchanged; both-directions test proof; live cast + pet obedience verified (2026-08-13/14)

The deliberately-early "hard case" T99 flagged (`cast`/`interrupt`/`mem`/
`forget`/`gems`/`pet` route through a different, older subsystem than the
live one, or are stubs). `cast`, `mem`, `forget`, `gems`, `interrupt`,
`spellbook` (game's Spells category, confirmed exactly 6 by
`category=="Spells"` on the registrations) and `pet` moved from
`EverQuest::m_command_registry` (`eq_commands_spells.cpp`) into
`action::CommandProcessor`, atomically (removed from the game registry in
the same commit). Full writeup: `refactor/T101_migrate_spells_pet_family.md`'s
Review, `/mnt/projects/fable/review/T101/README.md`.

**Key finding: the recipe held, with one refinement T99 didn't need.**
T97 found `cast`/`interrupt`/`mem`/`forget` each had an EXISTING
`ActionDispatcher`/`EqActionHandler` method already — but pointed at the
wrong subsystem (`castSpell`/`castSpellOnTarget`/`interruptCast`/
`memorizeSpell` all route through the older `CombatManager` path; T93
has real-behavior test coverage pinning `castSpell`/`castSpellOnTarget`/
`memorizeSpell`'s `CombatManager`-based gem-slot conversion specifically,
per the plan's "do not re-litigate it" instruction). Rather than repoint
those (which would have broken T93's coverage), this unit added THREE new
methods (`beginCastFromGem`, `memorizeSpellByName`, `listMemorizedSpells`)
for the real `EQ::SpellManager` path game's commands actually use, and
REPOINTED two (`interruptCast()`, `forgetSpell()`) whose underlying
implementation had zero existing test coverage of its internals (only
fake-handler routing tests) — `interruptCast()` from `CombatManager::
InterruptCast()` to `SpellManager`-based, `forgetSpell()` from a stub
("Would need to send forget spell packet", a false success) to real
`SpellManager::forgetSpell()`. T93's own tests (`test_eq_action_handler_transforms.cpp`)
pass UNCHANGED — confirmed by running them before and after every change
in this unit.

**Pet vocabulary gap closed**: T97 found game's `/pet` (leader/spellhold/
nospellhold/notaunt/nohold/standup) was a strict superset of the
pre-existing `ActionDispatcher` pet wrappers even after T96's literal
fixes. Five new wrappers added (`petStand`/`petTauntOff`/`petHoldOff`/
`petLeader`/`petSpellHold`), all routing through the EXISTING generic
`sendPetCommand()` `IActionHandler` virtual — no interface change needed
for pet (unlike spells). `petAttack()` gained an optional explicit-target
parameter (default 0, unchanged) to carry `/pet attack <name>`'s
entity-name resolution, done in `CommandProcessor` via `GameState::
entities().findEntityByName()` (same case-insensitive-partial-match
primitive `T99`'s `/invite`/`/face` already used).

**`GameStateSyncBridge` coverage — the one thing T99 warned to check
first — was ALREADY THERE** for both domains (`PetCreated`/`PetRemoved`/
`PetStatsChanged`/`PetButtonStateChanged`, `SpellGemChanged`/
`SpellCastStarted`/`SpellCastComplete`, all pre-existing since T40/T44).
Group was the outlier T99 hit, not the norm — no bridge changes needed
this unit, confirming T99's own prediction that Group's gap was a one-time
class of risk.

**Live evidence** (`review/T101/README.md`, both sessions):
- Pet (GM-summoned `skel_pet_11_`, same fixture T96 verified): every
  migrated/new `/pet` subcommand shows `registry=action`/`outcome=applied`;
  the pet audibly obeyed (`"Following you, Master."`, `"My leader is
  Testchar."` [T101-new], real attack+kill — `"Attacking a rat Master."` +
  `"Libartik hits a rat for 18 points of damage."` + `"You gain
  experience!!"`, clean dismiss). `hold`/`focus`/`spellhold` toggles sent
  packets with no server response — the same eqemu AA-gate T96 root-caused,
  not a T101 bug.
- Spells (`Testcaster`, Wizard, `Frost Bolt` id 54 resolved via
  `data/willeq.db`, never guessed): `/mem` did a real async memorize
  (`MemorizeProgress` -> `Ready`, confirmed via `/gems`); the first
  `/cast` genuinely fizzled (a real server-rolled outcome, not a scripted
  ack); a later cast was server-confirmed and a real `SpellAction` damage
  packet landed; `/forget` correctly REFUSED while the gem was mid-cooldown
  (real `SpellManager` state-machine behavior, ported verbatim, not a bug).
  Un-migrated controls (`/target`, `/say #...`, `/camp`) show
  `registry=game` in the same sessions — atomicity proof.

**Both-directions test proof** (T95 standard): broke `petStand()`'s
command id and `ActionDispatcher::memorizeSpellByName()`'s gem bound one
at a time, confirmed the corresponding new tests went red with the exact
expected mismatch, restored from a diffed-identical backup, confirmed
green again.

**No STOP condition was hit** — the recipe generalised to the hard family
with the refinements above (new methods where an existing one was
semantically pinned by another unit's tests; repoint where it wasn't),
not a breakdown. Effort was larger than T99's Group pilot (7 commands vs
4, 3 new dispatcher methods + 5 new pet wrappers + a repoint pair + a new
DB-gated real-`SpellManager` test file, two live sessions instead of one)
— at the higher end of T99's own "needs new dispatcher methods" 4-6h
per-family estimate rather than matching it exactly, but still inside
T99's 60-90h total-programme band for the ~120-command remainder. See the
plan's own Review "effort estimate" for the full reasoning.
## T100: Chat family migrated to `action::CommandProcessor`; `shout` reconciled; three new pre-existing findings (2026-08-14)

Second family in the migration programme (after T99's Group pilot), same
recipe, no deviation needed. All 9 of `game`'s Chat-category commands
(`say`, `shout`, `ooc`, `auction`, `gsay`, `gu`, `tell`, `emote`, `reply`)
moved out of `EverQuest::m_command_registry` (`eq_commands.cpp`) and into
`action::CommandProcessor`, atomically (removed from `eq_commands.cpp` in
the same commit as the `CommandProcessor` addition — `registry=action`
confirmed live for all 9, `registry=game` confirmed for `/target` in the
same session as a control). T97 rated the whole family "Equivalent" (both
registries terminate in the same `EverQuest::SendChatMessage()` call), so
these are direct ports against the pre-existing, already-correct
`ActionDispatcher::sendChatMessage()`/`sendTell()`/`replyToLastTell()` — no
new `ActionDispatcher` method was needed. `shout` (already living in
`CommandProcessor` since before T99, reachable only via the escaping alias
"sh" because game's own alias was "sho") was **reconciled, not
duplicated**: "sho" was folded into `CommandProcessor`'s alias list and
game's own `shout` registration deleted, so it is now fully migrated like
its 8 siblings instead of existing in both registries at once. Full
writeup: `refactor/T100_migrate_chat_family.md`'s Review,
`/mnt/projects/fable/review/T100/README.md`.

**`GameStateSyncBridge` coverage checked BEFORE the live leg, per T99's own
addendum — found N/A, not a gap.** None of the 9 Chat commands read any
`state::GameState` field (unlike Group's `hasPendingInvite()`/`inGroup()`
reads); `state::GameState` has no chat domain at all (`grep -n "chat\|Chat"
include/client/state/game_state.h` — zero hits). Chat is write-only from
the action layer's perspective, so there was nothing for the bridge to
sync. This is the "most families won't hit this" case T99's recipe
predicted, confirmed on the second family tested.

**`ConsoleInputHandler`'s third, non-slash chat vocabulary
(`console_input_handler.cpp:374-409`, `say`/`tell`/`shout`/`ooc`/
`auction`) does NOT diverge functionally after this migration — if
anything the divergence narrowed.** It never reached either command
registry before or after T100 (confirmed, not touched, per the plan's
prohibition) — but tracing where its `ChatMessage` structs actually go
(`InputActionBridge::processChatMessages()`, `input_action_bridge.cpp:
291-321`) shows it calls `ActionDispatcher::sendChatMessage()`/`sendTell()`
directly, the exact same dispatcher methods `CommandProcessor`'s newly-
migrated `cmdSay`/`cmdTell`/etc. now call too. Before T100, `game`'s
(then-live) `say`/`tell`/etc. called `EverQuest::SendChatMessage()`
directly while `ConsoleInputHandler`'s path went through the dispatcher —
two different entry points converging on the same terminal call (T97:
"Equivalent"). After T100, both the slash-command path and
`ConsoleInputHandler`'s bespoke path terminate at the identical
`ActionDispatcher` method calls. The one remaining difference:
`ConsoleInputHandler`'s path bypasses `CommandProcessor::processCommand()`
entirely, so it never produces a `CMD outcome=... registry=...` log line —
an observability gap, not a behavioral one. `ConsoleInputHandler`'s
vocabulary is also a strict subset (5 of 9: no `reply`/`gsay`/`gu`/
`emote`) and was not extended, per the plan's prohibition on touching it.

**Three new findings, reported not fixed, none below the action layer this
time — all in `eq_chat.cpp` (F06, below the action layer, out of this
unit's territory) or in the action-layer's own pre-existing shape (predates
T100, given its first live caller by this migration):**

1. **`shout`/`ooc`/`gsay` (group chat) were sent correctly — confirmed
   live via the debug `Sending shout/ooc/group message: '...'` line plus
   `outcome=applied registry=action` every time, across two live sessions —
   but never visibly arrived on the second client, not even as a self-echo
   back to the sender** (unlike `say`/`auction`/`tell`/`reply`, which all
   self-echo and were confirmed arriving on the *other* client too, with
   `tell`'s multi-word message intact). Round 2 specifically retested
   `gsay` against a real, server-confirmed group (both clients logged
   "Group update: 2 members" and, later, both independently logged "Your
   group has been disbanded") and it still never appeared as a `[CHAT]`
   line on either client. The client-side code path
   (`EverQuest::SendChatMessage()`/`ZoneSendChannelMessage()`,
   `eq_chat.cpp`) is untouched by T100 and identical to what the deleted
   `game`-registry commands called before migration — T97 rated this whole
   family "Equivalent" precisely because both sides hit this same call — so
   this is not a T100 regression. It appears to be the first time any unit
   has live-tested these three specific channels cross-client at all (T84/
   T99's live legs used `tell`/group-*state* commands, not broadcast chat).
   Likely an eqemu/akk-stack server-side relay gap for broadcast-style
   channels, but not root-caused past that — server code is off-limits to
   modify. Full evidence: `review/T100/README.md`.
2. **`eq_chat.cpp:74-84`'s debug print (`ZoneSendChannelMessage`,
   "Sending X message") has no `CHAT_CHANNEL_EMOTE` case in its inline
   ternary**, so a real `/emote` (which sends the correct
   `CHAT_CHANNEL_EMOTE` constant — confirmed by the packet itself, only the
   debug label is wrong) logs as "Sending unknown message" instead of
   "Sending emote message". Similarly, `ZoneProcessChannelMessage`'s
   *incoming*-print ternary (`eq_chat.cpp:117-127`) has no
   `CHAT_CHANNEL_AUCTION` case, so a received auction message logs as
   `chan4` instead of `auction` (confirmed live: `[CHAT] Testmelee
   (chan4): ...` for a message correctly sent and displayed as auction
   content). Purely cosmetic debug-log gaps, pre-existing (F06, well before
   T100), out of this unit's territory (`eq_chat.cpp` is below the action
   layer) — not fixed.
3. **`CommandProcessor::cmdReply` cannot reproduce game's "No one to reply
   to" message.** game's `reply_cmd` checked `EverQuest::
   m_last_tell_sender.empty()` directly and told the user; the ported
   `cmdReply` calls `ActionDispatcher::replyToLastTell()`, which has no way
   to make the same check — `m_last_tell_sender` lives only on `EverQuest`,
   not in `state::GameState` or anywhere `IActionHandler` exposes to the
   dispatcher. `EqActionHandler::replyToLastTell()` (pre-existing, not
   changed by T100) already silently no-ops in that case, and
   `ActionDispatcher::replyToLastTell()` returns `ActionResult::Success()`
   regardless — so a user who `/reply`s with no pending tell gets a silent
   success instead of an error message. This is a pre-existing
   action-layer characteristic (the dispatcher/handler shape predates
   T100's changes entirely) that T100 is simply the first migration to give
   a live slash-command caller. Not exercised live in this unit's session
   (both clients always had a pending tell by the time `/reply` ran) —
   found by code inspection while porting, not by a live repro. Fixing it
   properly would mean adding either a `state::GameState` chat domain or an
   `IActionHandler::hasTellToReplyTo()`-style query, both of which reach
   outside this unit's declared territory (`src/client/action/`,
   `eq_commands*.cpp` removals, `bridge/game_state_sync_bridge.*`,
   `tests/`) — deferred for a future unit or Will's call on which shape to
   add.

All three reported, not chased, per the same standard T99 set: this
migration's job is to move the better implementation into the action
layer and prove it dispatches and works over the wire, not to fix every
below-the-layer or orthogonal defect its first live caller happens to
surface.

## T102 (Combat + Social migration) — findings deferred, not fixed

1. **`Trade` has no `GameState` domain at all** — checked before the live
   leg, per T99's addendum: `game_state.h` has `tradeskill()`
   (tradeskill-combine state) but nothing for player-to-player trading.
   Neither `GameStateSyncBridge` (event-driven) nor an
   `Application::syncCombatState()`-style per-frame poll covers it. T102
   worked around this by putting `trade`/`accept`/`tradedecline`/
   `canceltrade`'s full precondition logic in `EqActionHandler`, reading
   `EverQuest::GetTradeManager()`/`GetCombatManager()` directly (both
   pre-existing public accessors, live/authoritative) instead of a
   `GameState` projection — correct and sufficient for this migration, but
   means no other action-layer/render-layer consumer can currently read
   "am I trading, and with whom" without doing the same workaround. A
   future unit that wants trade UI state exposed to the action layer
   proper (e.g., a static UI panel showing pending-trade status) will need
   an actual `TradeState` domain + bridge or poll wiring — a real, if
   currently avoidable, gap.
   **CLOSED (T103, 2026-08-14).** `state::TradeState` added to
   `state::GameState`, populated on both instances (`EverQuest::m_game_state`
   directly at `TradeManager`'s existing callback sites, `Application::
   m_gameState` via 7 new `GameStateSyncBridge` cases over `TradeManager`'s
   pre-existing-but-previously-unconsumed events — no new event type
   needed). `EqActionHandler`'s four trade methods now read the projection
   for every precondition/display check; `GetTradeManager()` is only
   reached for the four calls that actually send a packet. One field is
   still NOT tracked (own-side offered items — `TradeManager`'s own-item
   path has zero production callers, items move via the ordinary inventory
   slot flow instead, which `InventoryState` doesn't model either; a real,
   reported reconstructibility gap, not fixed). See
   `refactor/T103_trade_state_domain.md`'s Review for full detail,
   including a third `TradeManager` gap found in the process (below) and
   live evidence of two of the projection's three observable stages
   (pending-request and decline-reset; the accept/Active stage was proven
   headlessly instead of live, for a pre-existing, unrelated
   command-surface reason also detailed there).
2. **`game`'s `trade_cmd` self-trade guard was not ported.** It compares
   the resolved target's spawn id against "my own spawn id"; no clean
   public accessor for the latter was found reachable from
   `EqActionHandler` (unlike `CombatManager`'s target, which IS
   accessible). A player could theoretically `/trade` themselves via a
   same-named alt or a name collision; low practical risk (the server's
   own trade-request handling likely rejects self-targeting too, untested
   here), not chased further.
3. **`/duel` dispatches and sends a real packet
   (`SendDuelRequest`/`HC_OP_RequestDuel`) but the second live client never
   visibly received a duel request, in two separate live rounds**, even
   with a ~14s wait margin. `SendDuelRequest`/`SendDuelAccept`/
   `SendDuelDecline` are 100% pre-existing, unmodified code (T102 only
   added routing to them) — reads as an eqemu test-server dueling-support
   gap, the same shape as T96's AA-purchase gate that blocked live `/pet
   hold`/`/pet focus` verification. Not root-caused (server code is
   off-limits), not chased further per the plan's own precedent for this
   class of finding.
4. **A real, live-found `acceptTrade()` bug WAS fixed within T102** (not
   deferred): `EqActionHandler::acceptTrade()` read
   `TradeManager::getPartnerName()` before calling
   `acceptTradeRequest()`, which is what actually populates it — printed
   "Accepting trade with " (blank) on the receiving client. Fixed by
   reordering; re-verified live (`Accepting trade with Testmelee`). Same
   aliasing-order bug class as T99's `EverQuest::AcceptGroupInvite()`
   finding (read too early here, vs. too late there) — recorded as a
   pattern worth watching for in any future "read some manager state after
   a call that itself mutates that state" port.
5. **`declineTrade()`'s message omits the declining party's name**
   (game's `tradedecline_cmd` included it) — `TradeManager` has no public
   accessor for the pending request's name on the decline path (only
   `m_partner_name`, populated exclusively by `acceptTradeRequest()`).
   Fixing this needs a small `trade_manager.h`/`.cpp` accessor addition,
   outside T102's declared territory. Deferred for a future unit or a
   dedicated small fix.
   **CLOSED (T103, 2026-08-14).** Fixed without touching
   `trade_manager.h`/`.cpp`: the name now comes from
   `state::TradeState::pendingPartnerName()`, populated from the
   pre-existing `TradeRequestReceived` bridge event fired when the request
   first arrived — a route independent of the `m_partner_name` gap this
   item described. Message restored to `game`'s original wording,
   `"Declined trade request from {name}"`. Live-confirmed,
   `review/T103/t103_b_testbuddy_evidence.log`.
6. **Testmelee's DB position was found already relocated to `arena`**
   (zone 77) at T102's own live-session start, not by any action T102
   took. T100's Review documents Testmelee's true baseline as `zone_id=25`
   (nektulos, `x=-259, y=-1201, z=-5.75, heading=160.25`) — some session
   since T100/T101 moved it to `arena` and never restored it, and that
   session's identity is not established here. T102 did not move either
   character further and confirmed the position was unchanged after its
   own clients logged out, so there was nothing for T102 itself to
   restore — but the drift from T100's documented baseline is real and
   unexplained. Left as-is (arena) rather than guessed back to nektulos;
   a future unit taking the eqemu lock should be aware Testmelee is not
   where T100 last documented it, and either investigate or treat `arena`
   as the new accepted baseline going forward.
7. **A third `TradeManager` gap found by T103, while verifying Trade-domain
   reconstructibility**: `TradeManager::rejectTradeRequest()` (the decline
   path, `trade_manager.cpp`) calls `resetTradeState()` directly, **never**
   invoking `m_on_cancelled()` — unlike `cancelTrade()` and the
   partner-initiated `handleCancelTrade()`, both of which do. So a local
   decline publishes **no event at all** for the state reset (no
   `TradeCancelled`, nothing). Left unhandled, this would leave any
   consumer's "is there a pending request" flag stuck true forever after a
   successful decline. `trade_manager.cpp` is outside T103's declared
   territory too, so not fixed there; `EqActionHandler::declineTrade()`
   works around it in-territory by clearing the `state::TradeState`
   projection directly after a successful decline, rather than relying on
   an event `TradeManager` never sends for this transition. Live-confirmed
   the workaround holds: a subsequent `/accept` against the now-stale,
   already-declined request correctly reported "No pending trade request"
   rather than attempting a stale accept. A future unit could fix this
   properly with a one-line `m_on_cancelled()` call added to
   `rejectTradeRequest()`.
8. **T103's own live session did not reach the `/accept`→`Active` round
   trip** (`TradeStarted`, "Accepting trade with X" from a real network
   exchange) — not because of any Trade-domain defect, but because **no
   command in this system cancels one's own outgoing pending trade
   request** (`/canceltrade` is `Active`-only, by design, matching `game`'s
   original `canceltrade_cmd`). Once A's first `/trade` was outstanding and
   B had already declined it, A had no way to clear its own
   still-`PendingRequest` `TradeManager` state before sending a second
   `/trade` to set up an accept round — `TradeManager` itself correctly
   refused ("Cannot request trade while already in a trade"). This is a
   pre-existing gap in the whole command surface (there is also no
   `/tradedecline`-equivalent for "cancel a request I sent"), not new to
   T103 and out of its scope; the accept/Active path was instead proven
   headlessly (`tests/test_eq_action_handler_trade.cpp`). Flagged for
   whichever future unit next touches Trade commands.

## T103 (Trade state domain) — summary

Closed T102's findings #1 and #5 (Trade domain, `declineTrade()` naming).
Found and worked around (not fixed in `trade_manager.cpp`, out of
territory) a third `TradeManager` gap (#7 above). Full detail:
`refactor/T103_trade_state_domain.md`'s Review, `/mnt/projects/fable/review/T103/`.

Reported per the same standard prior units set: fix what directly blocks
the plan's own live-verification bar (finding #4), report the rest.

## T105 (zone-in working set measurement) — findings

1. **RESOLVED by T108** (see that section below) — `/pmem` now reaches
   `EverQuest::runPmemDiagnostics()` directly and reports real RSS/audio/
   entity/door/spellDb data in a live client. Original finding preserved
   below for context. **`/pmem`'s real `processRssBytes`/`untrackedBytes`
   are unreachable in a running client** — another instance of the
   implemented-but-unreached
   pattern (the codebase's memory notes already track nine of these). The
   only slash-command path to `/pmem`
   (`irrlicht_renderer.cpp:1219`, `cmd == "/pmem"`) constructs an **empty**
   `MemoryReportInput ext{}` and calls `getMemoryReport(ext)` directly —
   `ext.processRssBytes` is always 0 through this path, so the "against RSS"
   / untracked lines in `getMemoryReport()`'s output never print for a live
   player-run `/pmem`. The code that *does* populate real RSS from
   `/proc/self/statm` and pushes it via `DiagnosticsMemoryReport`
   (`EverQuest::runPmemDiagnostics`, `eq_commands.cpp:117`) is only ever
   called from `EverQuest::runLoadDiagnostics` (`eq_commands.cpp:212`),
   which itself has **zero callers anywhere in the codebase** (grepped).
   T105 measured live-populated-zone RSS via the standalone
   `gles2_zone_benchmark` tool instead (which does wire real RSS), not by
   fixing this — it's production behaviour, out of a measurement unit's
   read-only scope. A future unit could wire `runLoadDiagnostics()` (or at
   minimum `runPmemDiagnostics()`) to the `/pmem` command path, or to zone-
   load completion, so a live session's own memory report becomes trustworthy
   for RSS/untracked without a second, separate benchmark run.
2. **The zone-in working set is now sized, not guessed** — distinct
   (race,gender) pairs and distinct equipment-model-IDs actually within the
   renderer's own 300-unit render distance are small (6-9 race/gender pairs,
   3-8 equipment models per zone measured), and EQE1's ~110-120MB "mapped"
   figure is 1.6-10.6% resident in every zone measured. Character Models,
   not the equipment archives, is the largest real (resident) tracked cost.
   ~34-36% of RSS remains untracked by any existing counter, consistently
   across zones — a stable, real gap, not zone-specific noise. Full tables:
   `refactor/T105_zonein_working_set_measurement.md`'s Review,
   `/mnt/projects/fable/review/T105/`. This sizes the load-gate design Will's
   direction calls for; a follow-up unit can build the gate directly from
   these numbers without re-measuring.

## T106 (untracked-memory attribution) — findings

1. **The 34-36% "untracked" figure is itself an artifact of `totalEstimate`
   crediting EQE1 at its mapped, not resident, size** — correcting for
   EQE1's already-measured (T105) resident percentage puts the *true*
   untracked share at **75-90% of RSS**, confirmed two independent ways
   (hand-corrected T105 numbers, and this unit's own `visual_gate.sh` run
   from a different harness). `eqe1MappedBytes` should eventually report
   resident bytes; not changed here (production code, read-only unit).
   Full arithmetic: `refactor/T106_untracked_memory_attribution.md`'s
   Review, `/mnt/projects/fable/review/T106/corrected_untracked_percentages.md`.
2. **~69-73 MB of RSS, roughly fixed regardless of zone/scene, is Mesa's
   llvmpipe software rasterizer plus its LLVM JIT shader compiler** — 100%
   dev-VM-only, does not exist on the Orange Pi's native Mali400/GLES2
   driver. The single largest individually-identified dev-only chunk.
3. **Audio is invisible through the live `/pmem` path for the same reason
   RSS/untracked are (finding #1 above)**: `MemoryReportBreakdown` already
   has a real Audio section (`audioPfsArchiveBytes` etc.) gated on
   `ext.audioAvailable`, which the `/pmem` handler's empty
   `MemoryReportInput{}` never sets true — same root cause as #1 above, not
   a new bug. This is the plan's own "large and trivially attributable to
   an existing subsystem that simply lacks reachability" exception;
   **recommended, not fixed here**: wire real `audioAvailable`/audio bytes
   into the same path the RSS fix would touch — one fix closes both gaps.
4. **~25.6% of the live client's RSS (148 MB, befallen) is glibc allocator
   overhead/fragmentation** — measured directly via `mallinfo2()`/
   `malloc_stats()` (external, via `gdb`-attach, zero code changes; chosen
   because `valgrind`/massif is not installed in this environment,
   confirmed before choosing). Real, large, distinct from both llvmpipe and
   genuinely-held-but-uncounted objects; not an artifact of the ~65
   per-thread malloc arenas Mesa/llvmpipe's worker-thread pool creates
   (confirmed via `malloc_stats()`'s per-arena dump — they're all
   essentially empty; all real allocation activity is in one arena).
5. **After removing llvmpipe/LLVM-JIT, the tracked total, self-binary text,
   other shared libs, and allocator fragmentation, ~271 MB (47% of RSS,
   befallen live client) remains genuinely unattributable** with the tools
   available in this environment — no heap profiler installed, and
   attributing further would need call-stack capture this read-only unit's
   scope doesn't license building. An `strace` mmap trace narrowed ~110 MB
   of the largest anonymous blocks to plausible-but-unconfirmed candidates
   (a PulseAudio-shaped `MAP_SHARED`+fd block, an RWX JIT-code-shaped
   block) by timing correlation only — not proof, stated as such. A future
   unit should install and run a real heap profiler (massif/heaptrack)
   against a real populated zone-in to close this.

Full detail, the `smaps` categorization tooling (reusable,
`review/T106/scripts/analyze_smaps.py`), and the per-chunk Pi-existence
table: `refactor/T106_untracked_memory_attribution.md`'s Review,
`/mnt/projects/fable/review/T106/`. This is the answer the memory-gate
runtime-control-surface question was blocked on — the gate cannot yet see
roughly half of real RSS (the genuine remainder) even after every other
chunk this unit identified is accounted for.

## T107 (paired Pi/x86 memory baseline) — findings

1. **The Pi runs the full client fine and fits comfortably** — 92.9 MB RSS
   live, populated Befallen, against ~395 MB headroom (23.5% used, ~302 MB
   free). T106's stated worry that the client might not fit at all did not
   materialize. Not a defect entry, recorded here for the board record.
2. **A real, ARM-only shutdown crash**: the Pi client SIGSEGVs during its
   own local teardown, consistently, AFTER the server has already confirmed
   a clean logout (`Logout confirmed by server` / `Game thread stopped`
   both precede the crash in the log every time). Reproduced twice
   (a smoke test and the full paired session). Does not affect any
   measurement — server-side logout and the gdb capture both complete
   first — but is a genuine defect. **Not root-caused, not fixed** (T107
   is read-only). A future unit should attach `gdb` on the Pi around the
   final `Shutting down application...` sequence to find the faulting
   frame; `Graphics detached` / `GPU upload thread: stopped` are the last
   clean log lines before the crash in every capture, suggesting the GLES2/
   DRM teardown path as the first place to look.
3. **Most of x86's "34-36%"/T106's corrected "75-90% untracked" figure does
   NOT transfer to the Pi.** Same method, same zone, same paired live
   session: x86 89.1% untracked, Pi 32.8% untracked. Do not use the x86
   untracked-share numbers as a stand-in for the target.
4. **llvmpipe/LLVM JIT explains only ~14% of the x86→Pi RSS gap, not most
   of it.** Confirmed near-zero (0.55 MB) on the Pi as T106 predicted, but
   the total RSS delta between hosts is 487.0 MB and llvmpipe+JIT accounts
   for only 68.4 MB of it. The bulk (~81% of the delta) is `[heap]` and
   `anon-other` collapsing on the smaller/32-bit ARM build, most plausibly
   downstream of far fewer worker threads (75 spawned on x86 this session,
   10 on the Pi) — stated as hypothesis, not proven attribution. **T106's
   `mallinfo2()`-based fragmentation percentage (~25.6% of RSS on x86) also
   does not transfer**, for a stronger reason than "different scale": see
   finding #5.
5. **A new methodological limit in the `mallinfo2()` method itself, found
   on the Pi.** T106 validated arena-total ⊆ RSS on x86 (502.8 MB inside
   593.8 MB), which is what let it treat `fordblks` as real resident
   fragmentation. On the Pi, `malloc_stats()`'s own "Total (incl. mmap)"
   figure (250.5 MB) **exceeds the entire process RSS (92.9 MB) by 2.7x** —
   most of what glibc's allocator considers "committed" there is
   demand-paged virtual reservation, never faulted in. The Pi's nominal
   `fordblks` figure (48.0 MB / 51.7% of RSS) is very likely a substantial
   overestimate and should not be read as a real fragmentation cost the way
   T106's x86 reading could be. A future unit attributing Pi memory further
   should not reuse `mallinfo2()`'s arena/fordblks fields as a resident
   proxy without first checking this self-consistency (arena vs RSS) holds.
6. **Pi RSS may understate real GPU-side memory pressure.** The Pi's smaps
   shows 13,250 separate DRM (`renderD128`) device mappings totalling
   88.1 MB *mapped* but 0 KB *resident* in every one — DRM/GEM buffer
   objects are a known case the kernel often doesn't attribute to the
   owning process's smaps `Rss`. `scripts/pi/pi_common.py`'s `CmaFree`
   monitor exists for exactly this class of measurement but is wired into
   the benchmark-tool path, not a live-client eqemu session. A future unit
   pairing `CmaFree` sampling with a live eqemu session would close this.

Full paired category table, the smaps captures for both hosts, and the
per-finding evidence: `refactor/T107_paired_pi_x86_memory_baseline.md`'s
Review, `/mnt/projects/fable/review/T107/`.

## T108 (totalEstimate mapped-vs-resident fix) — findings, closes an open item above

1. **Fixed**: `totalEstimate` no longer credits `eqe1MappedBytes` (mapped/
   address-space size, not RSS). Option B chosen over parsing `/proc/self/
   smaps` at report time (Option A): the real resident figure is already
   measured correctly and used elsewhere (`ext.processRssBytes`/
   `untrackedBytes`), so removing the credit is cheap, adds no new `/proc`
   parsing to a hot diagnostics path, and `eqe1MappedBytes` is still
   reported on its own line (now explicitly labeled "not counted in Tracked
   total"). Fixed in **two places**: `IrrlichtRenderer::
   getMemoryReportBreakdown()` (`renderer_diagnostics.cpp:646`) AND
   `computeMemorySnapshot()` (`tools/hwcaps/zone_bench_memory.cpp:166`) —
   the latter is a documented, necessary duplicate of the former (see that
   file's header comment: `gles2_zone_benchmark` never constructs a full
   `IrrlichtRenderer`) and had duplicated the same bug. This is the actual
   code path `scripts/ci/visual_gate.py`'s memory gate runs, so the fix
   would not have reached CI without both.
2. **The predicted ~110-120MB baseline movement materialized only in the
   T80 memory-coverage run, not the three golden-image zones** — the
   golden-image benchmark invocation never passes `--eqe1-path`, so
   `eqe1MappedBytes` is 0 there with or without this fix; nothing in
   `tests/goldens/manifest.json`'s `zones[]` block moved. The committed
   manifest also turned out to **not carry a `totalEstimate`/
   `eqe1MappedBytes` baseline for the golden zones at all** (only
   `textureCacheBytesUsed`/`textureCacheBytesLimit`/`textureCount` are
   blessed there — confirmed by inspection and by `git log`, T69's own
   commits never touched `manifest.json`), so `verify()`'s `MEMORY_BASELINE_
   FIELDS` loop silently skips them (`if key not in expected_mem: continue`)
   regardless of this fix. **No re-bless was needed or performed** — the
   gate passes unchanged before and after. The T80 `memoryCoverage` section
   (which *does* exercise EQE1) already ships explicitly unblessed pending
   Will's sign-off (`refactor/T80_memory_gate_coverage.md`); this unit did
   not bless it either, per the same rule, but measured the corrected
   numbers Will will need when that sign-off happens: totalEstimate dropped
   by exactly `eqe1MappedBytes` per zone (qeynos2: 172,352,354 →
   52,408,963, Δ119,943,391; gfaydark: 183,871,942 → 65,262,459,
   Δ118,609,483; befallen: 159,450,602 → 46,356,623, Δ113,093,979 —
   every delta matches that run's `eqe1MappedBytes` exactly). Full numbers:
   `refactor/T108_totalestimate_mapped_vs_resident.md`'s Review.
3. **`/pmem`'s live reachability gap (T105 finding #1 above) is now fixed**,
   not just recommended. The `pmem` command (`eq_commands_spells.cpp`) no
   longer routes through the renderer-side `RendererCommand` bridge to an
   empty `MemoryReportInput{}`; it calls `EverQuest::runPmemDiagnostics()`
   directly (which already existed, fully built, and had zero callers —
   confirmed by grep before this unit and now closed). Real RSS/audio/
   entity/door/spellDb data reaches the report via the existing
   `DiagnosticsMemoryReport` bridge event; in-game chat visibility was
   preserved by adding `addSystemMessage()` calls to that event's handler
   in `irrlicht_bridge.cpp` (the old renderer-side handler both chatted and
   logged; the real path only logged). The now-dead empty-`MemoryReportInput{}`
   branch in `IrrlichtRenderer::processSlashCommand` was deleted (confirmed
   unreachable: `processSlashCommand` has exactly one caller, the
   `RendererCommand` bridge case, which nothing pushes for `/pmem` anymore).
   Audio's own reachability (T106 finding #3) is fixed as a side effect,
   since it depends on the same `ext.audioAvailable` populated by the same
   real path.
## T109 (entity load distance gate) — **CLOSED 2026-08-24 by T210. Fixed by T111; gate is ACTIVE and pickup is live-verified. The text below is HISTORICAL — do not treat it as open.**

> **CLOSED — read this first.** T111 (`d21086be`) fixed the pickup path and
> enabled the gate; `config/constrained_presets.json` ships `entityLoadRadius:
> 400.0` for `orangepi`, not the "unbounded" sentinel this entry describes.
> T210 then verified the pickup live with the movement reproduction this entry
> asks for: befallen, 140 entities deferred at zone-load, `/moveto` a deferred
> entity (`a_greater_skeleton026`, dist 404.9). That entity went
> `inGraph=false placeholder=true` -> `inGraph=true placeholder=false`, scene
> placeholders fell 176 -> 168 and real models rose 191 -> 199, with
> `EntityBuild: Placeholder -> NodeSetup -> MeshFinalize -> Built` transitions
> firing after Step 8. Evidence: `review/T210/`. The "permanently-invisible
> NPCs" consequence described below did NOT survive T111 and is not current.

### (historical — the state before T111)

## T109 (entity load distance gate) — built and measured, SHIPS INERT — the deferred-entity streaming pickup path is confirmed broken

**Priority follow-up, blocking**: T109 built a distance gate for
`loadZoneStep8_Entities` (entities beyond `entityLoadRadius` skip the
inline prep+build chain at zone load) and designed it, per Will's ruling,
to lean on the existing post-load background-streaming path
(`EntityPrepWorker` + `processFrameProgressiveLoad()` ->
`queueEntityPrepRequests()`) to pick deferred entities back up. Two defects
were found in that path while verifying it, live, before activating the
gate (exactly what the plan required):

1. **`EntityPrepWorker` was never constructed anywhere in the codebase**
   (fixed by T109 — `IrrlichtRenderer::ensureEntityPrepWorkerStarted()`,
   `src/client/graphics/renderer_entity_load_gate.cpp`). Same
   "implemented-but-unreached" pattern as `ConstrainedMeshCache`/
   `SetPosition`/etc. Independently valuable: without it, any entity
   spawning post-load with a never-before-seen race also got stuck in
   `Placeholder` phase forever, not just T109's deferred entities.
2. **NOT fixed — this is the blocking one.**
   `checkProgressiveLoadingComplete()` (`renderer_zone_stream.cpp`) only
   counts unbuilt entities in the player's *current* BSP region as
   "blocking" completion, so it declares progressive loading complete and
   sets `progressiveLoadingActive_ = false` within ~2s of Step 8 finishing
   — long before entities deferred in other regions could ever be prepped.
   Once false, `queueEntityPrepRequests()` (called only from inside
   `processFrameProgressiveLoad()`, itself gated on
   `progressiveLoadingActive_`) never runs again. The only re-arm path
   (`registerEntity()`) fires for brand-new server-side entity spawns —
   never for a PVS region change bringing an already-registered-but-
   deferred entity into relevance. **Live-confirmed** in Befallen: with the
   gate temporarily active (radius=400, 89/178 entities deferred at
   zone-load), modeled-entity count stayed at exactly 82 across a 40s idle
   hold AND after two large `/move` commands (-400, -700 units, crossing
   multiple BSP regions) — `EntityBuild:`/`EntityPrepWorker: queued prep`
   log lines never fire again after Step 8 in either test.

**Consequence, and what ships**: activating T109's gate today would produce
permanently-invisible NPCs for any entity deferred outside the player's
landing BSP region — worse than not gating at all. Per the plan's own STOP
condition, T109 ships the gate's code, tests, and the `EntityPrepWorker`
fix, but `entityLoadRadius` is left at an explicit "unbounded" sentinel
(`1,000,000.0f`) in both the C++ defaults and
`config/constrained_presets.json` — no shipped preset activates the gate.

**What the next unit needs to do before flipping this on**: fix
`checkProgressiveLoadingComplete()` to count deferred entities anywhere in
the zone (not just the current region) as blocking full completion, or
(better) add a re-arm path for `progressiveLoadingActive_` on PVS region
change (`onPvsRegionChanged()` currently only re-sorts/re-prioritizes
existing queues, doesn't re-scan for newly-relevant deferred entities).
Once fixed and live-verified with the same movement-based reproduction
T109 used, set `entityLoadRadius` back to `entityRenderDistance + 100`
(measured elbow: 400 for the `orangepi`/default preset — see T105's
distance-vs-distinct-race/gender-count data, re-analyzed at 300-600 unit
radii) in the shipped presets.

**Secondary, characterization-only findings** (measured with the gate
temporarily active, reverted before commit — not what ships, but real
data for whoever does the follow-up): Step 8 wall time drops 12-20% at
radius=400 (x86 -20.1%, Pi -11.6%), but overall zone-request-to-ready only
drops 6-8% (x86 -8.1%, Pi -6.6%) — a real but modest win, well short of the
"30-56% cut" the plan's own justification framed as the upside (that number
describes the cut in *distinct race models*, 19→9, which is real, but Step
8's wall time isn't purely proportional to distinct-model count — a lot of
it is per-entity loop/cache-hit overhead that doesn't disappear with fewer
distinct models). Step 8 is 43-62% of total zone-load time either way
(bigger share on the Pi), cross-validating T91's "Step 8 dominates"
finding independent of the gate's own effect.

Full write-up, before/after tables, and the live-reproduction evidence:
`refactor/T109_entity_load_distance_gate.md`'s Review,
`/mnt/projects/fable/review/T109/`.

## T111 (progressive-loading pickup/re-arm) — CLOSES the T109 blocker above: the gate is now ACTIVE. Two new pre-existing concurrency bugs found and fixed along the way; T110's Pi shutdown crash independently reproduced but NOT fixed

**T109's blocker (above) is resolved.** `checkProgressiveLoadingComplete()`
now uses a pure, unit-tested predicate (`isEntityBlockingProgressiveLoad()`)
symmetric with `queueEntityPrepRequests()`'s own gate instead of scoping to
the current BSP region, AND `applySimulationOutput()` re-arms
`progressiveLoadingActive_` when an entity's `inSceneGraph` flag flips
false→true (the 40-entity render-cap/render-distance visibility set, NOT
"has a scene node" — a genuine misreading risk for the next person to touch
this) while still unbuilt. **Both were needed**: the completion-test fix
alone converged to a hard plateau in one live test (a completion check that
correctly waits on the CURRENT visible set, but nothing ever refreshes that
set once it empties). Live-proven with monotonically increasing
modeled-entity counts (no plateau) across multiple region changes and
away-and-back movement, in Befallen (177 entities) AND Nektulos (452
entities, T105/T109's own "gating bug shows here" zone), on x86 AND the
Orange Pi. `config/constrained_presets.json`'s `orangepi` preset now ships
`"entityLoadRadius": 400.0` — the gate is live in the build both the Pi and
x86 dev builds default to.

**Two more pre-existing, previously-latent concurrency bugs found and fixed
as direct prerequisites** (both unreachable until the pickup fix made
`EntityPrepWorker`'s background thread do sustained real concurrent work for
the first time):
1. `regionPvsDepthMap_` pointed into a local `SimulationOutput` in
   `loadZoneStep12b_Simulation`'s synchronous first-frame computation — the
   exact same dangling-local hazard the code already guarded for
   `particleRenderBuffer_` three lines away, just never guarded here. Fixed:
   null it at the same point (`renderer_zone_load.cpp`).
2. `WilleqDatabase::open()` used `SQLITE_OPEN_NOMUTEX` (SQLite's own
   documented "not safe for concurrent multi-thread use" flag), crashing
   inside `btreeCursor()` once the background `EntityPrepWorker` thread and
   the main thread's `EntityRenderer::updateEquipmentTransforms()` both
   queried the same connection concurrently for real. Fixed:
   `SQLITE_OPEN_FULLMUTEX` (`willeq_database.cpp`).

**T110 (`refactor/T110_pi_shutdown_segv.md`) was NOT executed this unit**
despite being named in the task prompt as ready for the Pi — T111's own
scope filled the session. Its crash WAS independently reproduced, though, as
a byproduct of this unit's own Pi testing: `corrupted size vs. prev_size` →
`SIGABRT` during `unloadZone()`'s teardown, Pi-only, after the scripted tour
completed cleanly. Full `gdb` backtrace:
`/mnt/projects/fable/review/T111/pi_befallen_gated_pickup_and_shutdown_crash.log`
— a head start for whoever picks up T110, not a diagnosis under T110's own
bar (several consecutive clean shutdowns, narrowest fix).

Full write-up, before/after load-time tables (single-sample re-measurement,
not a full characterization — flagged as a limit), post-zone-in frame-cost
finding (x86: real but small, 7 spikes/6682 frames; Pi: not reliably
measured, flagged as a gap), and the live pickup-proof evidence:
`refactor/T111_progressive_loading_rearm.md`'s Review,
`/mnt/projects/fable/review/T111/`.

## T112 (Pi gate frame-cost) — CLOSES the T111 gap: the gate's in-play cost on the Pi is measured and it is NOT a cost. New Pi-only mid-tour SIGSEGV found (not T110, not fixed)

**T111's gap is closed**: paired on the same Pi, same Befallen movement tour,
gate-active (`entityLoadRadius=400`) vs gate-inert (`1,000,000`, via a
runtime CWD config override — no source or shipped-config change). Result is
the opposite of the concern that opened this unit — gate-active had **fewer**
frame spikes than gate-inert (254 vs 675 over comparable ~1,620-1,636-frame
windows, ~155 vs ~417 per 1,000 frames) and a slightly better worst frame
(369.5ms vs 409.2ms). The mechanism: `meshLoading` dominates 674/675 of the
INERT arm's spikes (23.1ms avg) and is **absent** from all 253 of the ACTIVE
arm's — with the gate off, all 181 entities (19 distinct race/gender) are
eligible to build and the client spends the whole in-play session
progressively building meshes for them; with the gate on, fewer distinct
models are ever built, so there's less work left to do during play, not more.
The gate doesn't trade load time for play-time cost — it reduces both.
Verdict: gate ships as-is, no throttle/radius change indicated.

Re-arm-timestamp correlation (the plan's "sharpest evidence" ask) could not
be produced on a completed run — every `--log-level=DEBUG` Pi run this
session (re-arm events log at `LOG_DEBUG`) crashed mid-tour before reaching
`/camp`; the headline numbers above are instead from `--log-level=INFO` runs,
which still carry `SPIKE:`/`BUDGET:` (both `LOG_WARN`) even without DEBUG. A
partial DEBUG-level run's evidence points the same direction as a supplement
(422 spikes fired in the post-load settle window, entirely before either of
that run's 2 re-arm events).

**New finding, separate from T110**: an intermittent, Pi-only **mid-tour
SIGSEGV** (not T110's teardown SIGABRT, which also reproduced here exactly
as documented and is expected/unfixed noise) — 5 of 9 Pi invocations this
session, reproducing under BOTH gate states and both log levels, arriving as
the player nears Befallen's `x≈-333..-400, y≈5..20` (the T109/T111-proven
`/move -400 20 3` destination) with `RegionVis` jumping to 1,100+/3,596 just
before the crash — but not deterministic (the same tour completed cleanly
twice). Unrelated to the gate (reproduces at both `entityLoadRadius` values);
**reported, not fixed**, per this unit's scope. Whoever picks up T110 should
budget for this as a second, related-but-distinct Pi movement/teardown
stability defect in the same zone — possibly worth a single combined
diagnosis session given the shared territory (`renderer_zone_load.cpp`
teardown path, Befallen, real hardware only).

Full write-up, the paired-run tables, per-phase spike breakdowns, and the
DEBUG-logging-as-a-cost measurement: `refactor/T112_pi_gate_frame_cost.md`'s
Review, `/mnt/projects/fable/review/T112/`.

## T113 (Pi mid-tour SIGSEGV) — T110 CLOSES for its own signature (root-caused + fixed); the mid-tour SIGSEGV itself does NOT close — different defect, diagnosed not fixed; a third crash signature (SIGBUS) also found, unexplained

**Tested the plan's own hypothesis first**: is T110's teardown SIGABRT and
T112's mid-tour SIGSEGV one defect wearing two faces? **Partially yes, more
precisely than "yes/no."** A pre-fix Pi run captured T110's exact documented
signature (`double free or corruption (!prev)` → SIGABRT at `unloadZone()`
teardown) with, for the first time, a FULLY SYMBOLIZED backtrace (T111's own
capture stopped at `unlink_chunk`/`malloc_consolidate`, "previous frame
identical to this frame" — no debug info that time). This one names the
exact object: `RaceModelLoader::~RaceModelLoader()` destroying
`variantModels_` (`std::map<uint64_t, std::shared_ptr<RaceModelData>>`),
whose internal red-black tree was corrupted earlier by an unprotected
concurrent mutation — `RaceModelLoader::removeMeshRef()` (called from
`EntityRenderer::removeEntity()` on the render thread every time an entity
despawns, a routine in-play path) erases from `variantModels_` WITHOUT the
documented `variantModelsMutex_`, while `EntityPrepWorker`'s background
thread inserts into the same map WITH the lock via `preloadVariantModel()`.
**This is a genuine sibling of T111's own `regionPvsDepthMap_` fix** — same
class of bug (worker thread writes shared renderer state, a lock exists but
not every access site uses it), different subsystem, confirming the plan's
"check whether T111's fix was complete or a sibling survives" framing.
A second instance of the identical class was found while auditing (not
crash-driven — by reading, not guessing): `loadedModels_`
(`std::map<uint32_t, ...>`, documented "main thread only — no mutex
needed") is read from `EntityPrepWorker`'s background thread via
`getRaceModelData()`/`isModelDataCached()` while ~10 sites insert into it
unprotected from the main/loading thread.

**Fixed, narrowly**: one new mutex (`loadedModelsMutex_`) plus locking the
three previously-unprotected `variantModels_` access sites
(`removeMeshRef()`, `clearCache()`, `getMemoryStats()`) — pure additive
synchronization extending the SAME per-map-mutex pattern this file already
uses correctly elsewhere (T91/T111 precedent), no threading redesign, no API
change. **T110 closes for this specific signature** — it did not recur in
either of two post-fix Pi runs.

**But the plan's own primary subject — the mid-tour SIGSEGV — is a
DIFFERENT defect and does NOT close.** Two more live Pi runs with the fix
already built and deployed both still crashed (0/2 clean — the plan's
"beat 5-of-9" bar is explicitly not met). One reproduced mid-tour, in the
same named hazard box (`x≈-333..-400, y≈5..20`), inside
`ZoneMergeBuffers::renderMerged()`'s lookup into its own `ranges_` map
(`std::map<const void*, RegionBufferRange>`) — `__x=0x2601` as a tree-node
pointer means that map's own tree structure is corrupted, not merely
missing the key (a miss is already handled gracefully two lines later).
**Directly, mechanistically tied to the `RegionVis` lead**, traced through
the source rather than inferred from timing: the crashing frame's
`sortedEntries` vector (length 2271/capacity 4096) is built every frame from
`SimulationOutput::sortedRegions` — the exact vector `computeVisibility()`
logs as `RegionVis: N/3596`, which balloons to near-full-zone population
whenever the PVS candidate lookup misses and falls back to all 3596 regions.
A frame drawing ~2271 regions makes ~2271 `ranges_.find()` calls instead of
a normal PVS-neighbourhood-sized handful — far more likely to be the call
that touches already-corrupted memory. Root cause not isolated to a single
write site (candidate causes: fallout from a still-undiscovered sibling of
the class just fixed, vs. a defect intrinsic to `ZoneMergeBuffers` itself,
e.g. `MergedGeneration::buf`'s index array under the same region-count
blowup) — **diagnosed, not fixed; needs ASAN or a longer targeted Pi session
than this unit's remaining budget allowed.** `mergeZoneDraws: true` ships in
the `orangepi` preset — this is live production code on the target hardware.

**A third, completely separate crash signature was also captured and is
unexplained**: the other post-fix run completed the full tour cleanly to
`/camp` and progressed further into `unloadZone()` teardown than the pre-fix
run did, then SIGBUS'd inside the Mesa/Lima GL driver (`libgallium`, no
symbols available on this Pi image). Different failure shape from the other
two (SIGBUS vs SIGABRT/SIGSEGV) — could be fallout from the same corruption
family or a genuinely separate GL/DRM teardown-ordering bug; not
distinguishable without a debug Mesa/Lima build this session didn't have.

**Method note for whoever picks this up**: post-mortem core dumps worked
(no live gdb, no DEBUG-logging distortion, per T111/T112's own
established technique) — BUT `core_pattern` must point at an NFS path, NOT
the Pi's local `/tmp`. The first run this session wrote a ~200MB core to the
242MB RAM-backed tmpfs on this 512MB device and the kernel OOM-killed the
process mid-dump (competing with the live process's own memory) — a
self-inflicted measurement artifact, not the bug. Every run after the first
pointed `core_pattern` at NFS instead and worked cleanly.

Full backtraces (verbatim), the RegionVis→sortedEntries data-flow trace,
the exact fix diff description, and the crash-rate table:
`refactor/T113_pi_midtour_segv.md`'s Review,
`/mnt/projects/fable/review/T113/` (backtrace `.txt` files + one raw core
for the unexplained SIGBUS).

**Next unit on this thread should start from
`review/T113/run3_midtour_sigsegv_backtrace_POSTFIX.txt`
(`ZoneMergeBuffers`) and `run4_teardown_sigbus_backtrace_POSTFIX.txt`
(Mesa SIGBUS) rather than re-diagnosing from scratch, and should budget for
ASAN instrumentation** — a second gdb-only pass did not close either.

## T114 (worker/render race audit) — the inventory, five fixed, three reported-not-fixed, `ZoneMergeBuffers::ranges_` still NOT closed

**TSAN ran for real** (x86, `-fsanitize=thread`, over the live GLES2/
llvmpipe render path, through repeated full zone-ins and movement tours) —
see `refactor/T114_worker_render_race_audit.md`'s Review for the invocation,
the `setarch -R` ASLR workaround, and the config-CWD wrapper. Suppression
list: exactly one entry, `race:libgallium*.so*` (Mesa's software rasterizer,
third-party, no willeq frame in either stack) — **no willeq symbol
suppressed**.

**The dominant noise source in the first TSAN run (568 of 633 warnings) was
a genuine, own-code race in `include/common/logging.h`**:
`ThreadLogBuffer::flush()`'s unlocked peek at the drain thread's side of its
double-buffer raced against `LogManager::drainAllBuffers()`'s concurrent
read+clear of the exact same `std::string`. Fixed with one mutex per
`ThreadLogBuffer` (thread-local, so contention is only ever between that
buffer's own thread and the single shared drain thread); the hot-path
`append()` writes stay lock-free. This was necessary infrastructure to make
the rest of the audit legible, landed regardless of being outside the plan's
named graphics territory (same reasoning T109/T111 used for their own
out-of-territory fixes). Cut the warning count from 633 to 32.

**The inventory (full table, 16 items, in the plan's Review) — record here
as the map of this defect class**:

- **Fixed this unit** (all additive, per-container-mutex, same pattern
  T91/T111/T113 established):
  - `WilleqDatabase`'s per-query cached `sqlite3_stmt*` — reset/bind/step
    was entirely unguarded, shared between `EntityPrepWorker` and every
    other thread that queries spells/strings/race models/assets. One mutex
    per connection, held per query. Not previously documented anywhere as a
    race — found by reading, not a crash (would silently cross-contaminate
    query parameters between two in-flight calls rather than crash, which
    is why it went unnoticed).
  - `RaceModelLoader::otherChrCaches_` — a genuine sibling of T113's
    `variantModels_`/`loadedModels_` fix, in the exact same class (worker
    thread writes shared renderer state via `storeOtherChrCacheLocked()`,
    but ~10 sites in `model_loading.cpp` bypassed it entirely, unlocked).
  - `RaceModelLoader::getLoadedModelCount()` — one unlocked `size()` read
    of `loadedModels_` that T113's own fix missed.
  - **`RaceModelLoader::originalVerticesForAnimation_`/
    `vertexMappingForAnimation_`** — the standout finding. Root-caused from
    a **live Pi crash captured this session**
    (`double free or corruption (!prev)` inside
    `RaceModelLoader::~RaceModelLoader()`'s destruction of
    `vertexMappingForAnimation_`): two scratch member vectors, written by
    `mesh_building.cpp`'s mesh builders and immediately consumed by the
    caller, touched with zero synchronization by both
    `getAnimatedMeshForRace()` (render thread) and `preloadVariantModel()`
    (`EntityPrepWorker`'s background thread). Fixed with one mutex held
    across the whole build-then-consume sequence at both call sites (not
    just each field). Did not recur in 3 further Pi runs.
  - `EquipmentModelLoader::archivesLoaded_`/`mappingLoaded_` — a
    double-checked-locking fast path reading a plain, non-atomic `bool`
    outside the lock (UB, unlikely to tear on this arch but a real bypass).
    Changed to `std::atomic<bool>`.
- **Confirmed safe, not a race** (worth recording so it isn't re-audited
  from scratch): `ZoneMergeBuffers` (`ranges_`/`buckets_`) — every caller of
  `build()`/`renderMerged()`/`destroy()` traced and confirmed render-thread
  only; TSAN reached and exercised this code repeatedly (9 texture buckets
  built, multiple successful 80-90%-reduction merged-draw frames) with zero
  warnings. `regionMeshNodes_` — confirmed `GPUUploadThread`'s worker thread
  never touches it directly. `background_work_queue.h`/
  `background_thread_pool.h`/`GPUUploadThread`'s queues, `ConstrainedTextureCache`'s
  decode/upload split, `SimulationWorker`'s P23 double-buffer — all
  confirmed correctly synchronized.
- **Found, real, TSAN-confirmed, deliberately NOT fixed here — recommend
  follow-up units**:
  - `RaceModelLoader::ensureModelDataLoaded()`'s ~20 unlocked `loadedModels_`
    reads. Confirmed **not currently live** (the only concurrent writer,
    `EntityPrepWorker`, never writes `loadedModels_` directly). A naive
    blanket lock around the function would **self-deadlock** — it calls
    `loadModelFromZoneChr()`/`loadModelFromGlobalChr()`/etc., which already
    acquire `loadedModelsMutex_` internally. Needs either a recursive mutex
    (its own design decision, not "additive locking") or a refactor to
    extract the lock-needing reads from the lock-taking calls. Flagged, not
    started.
  - `RendererEventReceiver`'s ~15 "read-and-reset" plain-bool/int8_t fields
    (`enterKeyPressed_`, `slashKeyPressed_`, `escapeKeyPressed_`,
    `spellGemCastRequest_`, mouse-click flags, …) — game thread
    (`GraphicsInputHandler::updateFromEventReceiver()`) vs render thread
    (`processChatInput()`), zero synchronization, TSAN caught ~10 instances
    of the identical shape this session. Not the worker/render (prep-worker)
    boundary this unit was chartered for — a different thread pair (game↔
    render input state). Correct fix is either converting the whole field
    set to atomics or a class-wide synchronization redesign, not "add a
    mutex to one container" — recommend a dedicated unit.
  - `AudioMixer`'s per-voice active flag — audio-callback thread
    (`miniaudioDataCallback`) vs game thread (`SfxManager::play`), plain
    bool, TSAN-confirmed, recurs across multiple `audio_mixer.cpp` call
    sites. Same reasoning: different subsystem boundary, recommend a
    dedicated unit rather than expanding this one.
  - `GraphicalMode::requestQuit()`'s quit flag — one TSAN hit, teardown-only,
    minor, reported not chased.

**`ZoneMergeBuffers::ranges_` (T113's named target) is STILL NOT CLOSED.**
T113's own two candidate explanations were "(a) fallout from a
still-undiscovered sibling [heap corruption bleeding across unrelated
objects]" or "(b) a defect intrinsic to `ZoneMergeBuffers` itself." This
unit found and fixed two real siblings in class (a) — `otherChrCaches_` and
`originalVerticesForAnimation_`/`vertexMappingForAnimation_` — and the crash
**still recurred**: 2 of 3 post-fix Pi runs hit the identical signature
(`ranges_.find()` on a corrupted tree, `__x=0x2601`, byte-identical frame
chain to T113's own capture). Since TSAN reached and exercised
`ZoneMergeBuffers` itself repeatedly with zero races, this now points more
strongly at (b) — a defect intrinsic to `ZoneMergeBuffers` or a THIRD,
still-undiscovered corrupting write elsewhere — than a threading race TSAN
can find. **The next unit on this thread should budget for ASAN
instrumentation specifically** (per the plan's own Part 3 fallback and
T113's prior recommendation) rather than another TSAN/gdb pass, since two
gdb/TSAN passes in a row have now ruled out the two most likely sibling
causes without closing it. Start from
`review/T114/pi_run4_midtour_sigsegv_backtrace.txt` and
`review/T113/run3_midtour_sigsegv_backtrace_POSTFIX.txt`.

Full write-up, the file-by-file inventory table, the TSAN before/after
reports, and the Pi crash-rate table: `refactor/T114_worker_render_race_audit.md`'s
Review, `/mnt/projects/fable/review/T114/`.

## T115 (2026-08-15) — ASAN hunt for `ZoneMergeBuffers::ranges_`: not closed; a real sibling bug found and fixed instead

**`ZoneMergeBuffers::ranges_`'s mid-tour SIGSEGV (T113/T114's `__x=0x2601`
signature) is STILL OPEN.** ASAN was applied per the plan (x86 first, Pi as
fallback) but never caught it: 7 x86 ASAN runs and 3 Pi ASAN attempts
produced zero reports implicating `ranges_`/`ZoneMergeBuffers`. The Pi
attempts are the honest gap — all three were cut short by the
ASAN-instrumented ARM binary's own memory footprint (two kernel OOM kills,
one 300s timeout still mid-zone-load) before a full tour could complete,
so this is "not caught" rather than "ruled out." `regionMeshNodes_`'s
lifecycle was newly audited by reading (not ASAN) and confirmed stable for
a whole zone visit — no code path replaces a region's `IMeshBuffer*` after
`build()` runs — which weakens but does not disprove T113's "stale pointer
key" hypothesis specifically.

**A real, different, reproducible bug WAS found and fixed**: x86 ASAN
caught (2 of 3 pre-fix runs, byte-identical) a SEGV in
`drawZoneGeometrySorted()` reading a dangling `portalVisibleRegions_`
pointer — the exact same "dangling pointer into a local `SimulationOutput`"
class T111 fixed for `regionPvsDepthMap_`/`particleRenderBuffer_`, just a
sibling field (added later, for portal occlusion) that missed the same
first-frame nulling. Fixed in `renderer_zone_load.cpp` (one line, same spot
as T111's fix). Verified: 3 consecutive clean x86 ASAN runs post-fix, 0
recurrences.

**Next unit on this thread**: the Pi's ASAN memory ceiling must be solved
before ASAN gets a fair trial at `ranges_` on real hardware — a
smaller/stripped ASAN build, a shorter tour that reaches the hazard box
faster, or falling back to a different technique entirely (T113's original
gdb-based alternative, or a `ranges_`-specific debug instrumentation). Do
not repeat T115's exact Pi ASAN invocation unmodified — it failed to
complete a full tour 3 times in a row for a resource reason (OOM/timeout),
not a code reason.

Full write-up (verbatim ASAN reports, the `portalVisibleRegions_` root
cause and fix, the three-attempt Pi OOM/timeout table, explicit answers to
every open question the plan posed):
`refactor/T115_asan_zonemergebuffers.md`'s Review,
`/mnt/projects/fable/review/T115/`.

### T115 side-finding — out-of-bounds read in TinySoundFont's sample interpolation (UNFIXED)

x86 ASAN's very first T115 run caught a **heap-buffer-overflow READ of 4
bytes, 0 bytes past an 884016-byte sample region**, on the miniaudio
callback thread:
`tsf_voice_render` (`third_party/TinySoundFont/tsf.h:1295`) ->
`MidiPlayer::render` (`src/client/audio/midi_player.cpp:252`) ->
`MusicPlayer::mixerRenderCallback` -> `AudioMixer::render`. The region is
the soundfont sample pool allocated once at startup by `tsf_load_samples`
via `MidiPlayer::init` / `AudioManager::initialize`.

**Root cause (read, not merely reported):** the linear-interpolation line
reads `input[nextPos]` where `nextPos = (pos >= tmpLoopEnd && isLooping ?
tmpLoopStart : pos + 1)`. For a **non-looping** voice on its **final**
sample, `pos + 1` runs one element past the pool. Upstream off-by-one, not
ours.

**Why it matters despite being third-party and a read:** on x86 it lands in
heap slack and is invisible without ASAN, which is why it went unnoticed.
On the Pi it is a genuine fault candidate — if the pool happens to end on a
page boundary the read touches an unmapped page, and the faulting thread is
the audio callback. **This has not been tested as a contributor to the Pi
instability arc and should not be assumed innocent.** It is a distinct
suspect from the `ZoneMergeBuffers::ranges_` corruption and from the class
of renderer-lifetime bugs T111/T115 fixed.

T115 did not fix it — audio is outside that unit's territory — and worked
around it with `--no-audio` for runs 2-7. Note that the workaround means
**every subsequent x86 ASAN run in T115 was audio-free**, so ASAN coverage
of the audio path stops at run 1.

Fix shape when scoped: clamp `nextPos` to the sample end for the
non-looping case, as a local patch over the vendored `tsf.h` (the file is
already vendored, so patching is in-tree). Verbatim report with full
allocation and thread-creation stacks:
`/mnt/projects/fable/review/T115/x86_asan_run1_report_audio_tsf_overflow.txt`.

## T116 (2026-08-15) — `SimulationOutput` dangling-pointer class closed by
construction; x86 ASAN re-verified 3/3 clean, matching T115's post-fix result

**Closes:** the recurring shape of T111's/T115's bug class — the same
mistake made three times in the same struct (`regionPvsDepthMap_`/
`particleRenderBuffer_` by T111, `portalVisibleRegions_` by T115, the third
added directly beneath T111's own warning comment). `IrrlichtRenderer::
applySimulationOutput()` was split by lifetime contract into a value-only
half (safe with any `SimulationOutput` lifetime, including a stack local —
this is what `loadZoneSequential()` Step12b now calls) and a new
`bindFrameSimulationBuffers(const SimulationOutput* persistent)` (stores all
four pointer members — `regionPvsDepthMap_`, `portalVisibleRegions_`,
`particleRenderBuffer_`, `currentFrameSimResults_` — reachable ONLY from
`applySimulationResults()`, the runtime path holding `SimulationWorker`'s
persistent double-buffered storage, which calls **bind first, then
apply** — see the ordering note below). The T111/T115 nulling lines at
`renderer_zone_load.cpp` (previously :2812-2813) are **deleted**, not
extended — nothing is ever set on the load path anymore, so nothing needs
unsetting. Verified by 6 new tests
(`tests/test_simulation_output_lifetime.cpp`) that call the real,
split production functions directly (via hand-expanded `friend class
..._Test` declarations, gtest's own `FRIEND_TEST` macro expansion written
out rather than included — see the ARM cross-compile deviation in the
plan's Review) under the exact conditions T111's/T115's crashes needed, and
assert all four members are provably untouched by the value-only half.
Also found and folded in while scoping: `currentFrameSimResults_` (a
fourth member of the same class, safe today only by three coincidences, its
"null otherwise" comment was false — fixed in place) and
`particleRenderBuffer_`'s absence from `renderer_zone_lifecycle.cpp`'s
zone-teardown nulling block (an inconsistency, not a hazard post-split,
since all four members can now only ever point into the never-freed
persistent worker storage — nulled anyway, for staleness, not safety).

**Ordering deviation, caught by review, not by this unit's own
verification:** the first version bound the four pointers AFTER calling
`applySimulationOutput()`, on the claim that nothing in that function reads
them back — checked only by reading its own body, and wrong via a
transitive call: `applySimulationOutput()`'s PVS-boundary-crossing block
calls `onPvsRegionChanged()`, which reads `regionPvsDepthMap_` through
`getRegionPvsDepth()`. Apply-then-bind would have fed that
re-prioritization the *previous* frame's depths exactly at the moment the
depth map changes — a real, silent regression neither the visual gate
(static frames) nor this unit's own tests (which call the split functions
independently) could catch. Fixed by swapping to bind-then-apply, which
restores the pre-split ordering exactly; re-verified by tracing every
callee of `applySimulationOutput()` (not just its own body) for reads of
the four members. Full re-verification after the fix: x86 suite
2271/2272, ARM cross-compile clean, visual gate PASS, 3/3 clean ASAN — see
the plan's Review for the complete trace and the corrected code.

**x86 ASAN re-verification: 3 of 3 clean**, run against the
ordering-fixed build (`build-asan/`, live eqemu, `scripts/eqemu/up.sh
befallen`, `review/T116/run_asan_x86.sh` — full 15-step tour, three
`/move` waypoints crossing the hazard box each run). Matches T115's own
post-fix 3-of-3 result exactly; zero ASAN reports of any kind across all
three runs. Server torn down cleanly, lock released and confirmed gone.
Logs: `review/T116/x86_asan_run{1,2,3}.log`.

**Does NOT close:**
- **`ZoneMergeBuffers::ranges_`** — T115 established this is a separate,
  uncaught defect; this unit's ASAN runs found zero reports of any kind, so
  they neither reproduce nor rule it out, same as T115 itself.
- **A fifth member repeating the class** is now higher-friction (an
  explicit, unmissable header comment on both split functions and on every
  existing pointer member) but not compile-time-impossible. The plan's
  optional stretch (a compile-time-enforced handle type obtainable only
  from `SimulationWorker`) was **not attempted** — explicitly descoped, not
  merely skipped for time; the plan itself named the split alone as the
  required deliverable.

## T117 (2026-08-15) — Pi stability re-baseline: 2 of 10 clean, no detectable
change vs T113 (0/3) and T114 (1/3); `ranges_` unchanged, three more
signatures found

**Measurement unit — nothing fixed, per its own charter.** Built a normal
(non-sanitizer) ARM cross-compile of fable tip `045f2512`, staged the
stripped ~9.8MB binary to Pi `/tmp` (matching T113's/T114's deployment
choice), and ran the same Befallen zone-in + `/move -400`/`/move -700` +
`/camp` tour T113/T114 used (audio enabled, `--log-level=INFO`) 10 times.

**Result: 2 of 10 runs completed cleanly to `Application shutdown
complete` (20%).** Against T113's 0-of-3 (0%) and T114's 1-of-3 (33%) this is
**no detectable change** — 20% is well inside sampling error of both, so the
data supports neither "improved" nor "regressed." (Orchestrator corrected this
line at merge; it originally read "below T114 and no improvement on T113,"
which implied T115/T116 regressed the Pi. They did not, and chasing a
non-existent regression would waste the next unit.) What this sample *does*
establish, which the 3-run samples could not, is that **the Pi is not
stabilizing**: T115's `portalVisibleRegions_` fix and T116's structural
closure of that class were both real and correct, and neither touches whatever
is actually corrupting the heap here.

**`ZoneMergeBuffers::ranges_`** (T113's original named defect, `__x=0x2601`
mid-tour SIGSEGV) reproduced **4 of 10 times** (5 of 11 counting a
discounted smoke-test run that hit the identical `0x2601` value), 100% of
the time at the exact `/move -700` window T113 first identified. **Not
closed, not new — same shape, same location, same rate its history
predicts.**

**Three more crash signatures appeared in the same 10 runs**, none of them
`ranges_`:
- **Mesa/Lima SIGBUS at teardown, inside `libgallium`, no symbols** — T113's
  own previously-*unexplained* signature, reproduced here for the first
  time since T113 itself. Still unexplained; still no symbols available.
- **A brand-new teardown signature**: SIGABRT "corrupted size vs.
  prev_size" inside `SkyData::~SkyData()` destroying its texture map — a
  container neither T113's nor T114's audits ever touched.
- **A reproduction of T113's *original* T110-class double-free**
  (`RaceModelLoader::~RaceModelLoader()` destroying
  `vertexMappingForAnimation_`) — verified NOT a regression of T114's
  `animScratchMutex_` fix (the mutex and both its call sites are still
  correctly in place in current source); more likely fallout from the same
  broader heap corruption than a race in this specific pair.

**Read on convergence: not converging.** Four independent victim containers
across ten runs, with no single write site implicated by any of them,
point at one wild/out-of-bounds write somewhere upstream rather than four
narrow independent bugs — the same "detection is far from origin" pattern
T113 and T115 already established for their own single findings, now seen
repeated across unrelated containers in one session. T113's and T114's
3-run samples were very likely too small to see the full shape of this;
this unit's 10-run sample is the first to show all four signatures
together.

**Audio**: enabled throughout (per the plan), not implicated in any of the
10 crash backtraces (all were main/render-thread, mid-frame or teardown
destructor unwind) — does not rule out T115's documented TinySoundFont OOB
*read*, which wouldn't necessarily crash and this unit's method (no ASAN on
the Pi) couldn't have caught either way.

**Board recommendation**: the next Pi unit should be a general
heap-corruption hunt (ASAN or a targeted bounds check), not another
`ranges_`-only chase — `ranges_` is the most frequent victim but
demonstrably not the only one. The ASAN-memory-ceiling problem T115 left
unsolved is the actual blocker; this unit's normal-build runs show the Pi
session budget itself is generous (10 full tours in one session, well under
the 12-invocation cap), so a shorter tour (skip the dead-air warmup and
early `/pmem` calls) aimed specifically at the hazard box would give a
future ASAN attempt real headroom without changing what's exercised. The
Mesa/Lima SIGBUS, now confirmed twice with zero symbols either time, is a
separate, cheaper next step: a Mesa build with debug symbols for
`libgallium` alone.

Deviation flagged: the plan named `review/T115/tour_pi.txt` (a 14-step
variant with `/camp` deliberately dropped, an ASAN-OOM-specific
adjustment) as the tour to reuse; switched mid-session to the 15-step
T113/T114-equivalent version (with `/camp` restored) after a first attempt
on the literal file never reached a natural end state and could not
exercise teardown — the fuller tour is the more comparable choice for this
unit's stated purpose, not a less comparable one. Full detail, per-run
table, and every backtrace: `refactor/T117_pi_stability_rebaseline.md`'s
Review; evidence in `review/T117/`.

## T118 (2026-08-15) — heap-corruptor hunt: every ASAN axis clean, `0x2601` is
`GL_LINEAR`, candidate write site named (not yet confirmed)

**Closes:** the ASAN-blind-spot scoping T115/T117 left open. x86 ASAN now
run for the first time with `--constrained orangepi --renderer gles2`, audio
**enabled**, full 15-step Befallen tour incl. `/camp` — 3/3 clean. 32-bit x86
ASAN (`-m32`) built from scratch (no i386 toolchain existed for this
project's dependency chain before this unit) and run clean, 2/2. A
Pi-fitted ASAN ARM build (`-g1` instead of `-g`, stripped to 36.7MB vs.
T115's 292MB that never fit `/tmp`) actually deploys and completes full
tours on real hardware now — 2/2 clean. **Every shape ASAN's shadow-memory
model can detect (heap/stack overflow, UAF, double-free) is now ruled out
on 64-bit x86, 32-bit x86, and native ARM, with the constrained/gles2/audio
path fully exercised everywhere.**

**Not closed:** the write itself. Leg 4 (`MALLOC_CHECK_=3
MALLOC_PERTURB_=165`, plain non-ASAN ARM binary) reproduced the exact
`ZoneMergeBuffers::ranges_` crash T113/T117 already established — crash at
`/move -700`, SIGSEGV inside `std::map<void const*, RegionBufferRange>`'s
red-black tree walk, called from `ZoneMergeBuffers::renderMerged()`
(`renderer_zone_merge.cpp:230`), register state `r0=r2=r3=0x2601` (9729),
identical across every prior sighting.

**`0x2601` is `GL_LINEAR`** (`GL_NEAREST` is `0x2600`, `GLES2/gl2.h`) — not
an unidentified small integer, as this entry originally (incorrectly) left
it. A coordinator review caught the gap: the value is a named GLenum
constant in a codebase whose entire renderer is GLES2, and that recurring
*exactly* across every sighting is expected of a constant, not evidence of
"a deterministic computation" as first drafted here. Corrected disassembly
(Pi-native `gdb`, against the *exact* binary+core pair — a first attempt
against `build-arm-noble/bin/willeq` was invalidated because this unit's own
later ARM verification rebuild had silently overwritten that file; recovered
via the still-preserved stripped binary + core) shows the fault is `ldr r2,
[r3, #16]` with `r3` corrupted — libstdc++'s `_Rb_tree_node_base` is 16
bytes (`_M_color`/`_M_parent`/`_M_left`/`_M_right`), so this is `_S_key(__x)`
inside the tree-walk loop: `r3` is the **tree-node cursor**, not the search
key as first drafted here. One of `ranges_`'s own heap-allocated rb-tree
nodes has a `_M_left`/`_M_right` child-pointer slot holding the literal
value `GL_LINEAR`.

**Candidate write site, not confirmed**: `COGLES2Texture::cacheTexParams()`
(`docker/irrlicht-drm/COGLES2Texture.h:79`), called from `COpenGLES2Driver::
applyMaterialRenderState()` (`docker/irrlicht-drm/COpenGLES2Driver.cpp:1319`)
on every draw where filter state changed, writes `GL_LINEAR`/`GL_NEAREST`
into `mutable` cache fields through a `const COGLES2Texture*` obtained from
`material.getTexture(0)`. If that texture was evicted by
`ConstrainedTextureCache`'s LRU (`constrained_texture_cache.cpp:599`)
while a queued draw's material still referenced it, the write lands wherever
that freed memory has been reused — plausibly one of `ranges_`'s own tree
nodes, both being ordinary allocations from the same heap. **Not verified**:
this unit did not instrument `tex`'s staleness at the crash moment, and did
not confirm the eviction-timing mechanism — this is the leading lead, not a
root cause. If real, it explains Legs 1-3's clean ASAN runs precisely: a
well-formed write of a valid value into a different, currently-live
allocation crosses no redzone and touches no freed-and-poisoned region —
exactly the shape T115 already demonstrated for the unrelated
`portalVisibleRegions_` field ("can not provide additional info").

Full symbolized backtrace, corrected disassembly, and register dump:
`review/T118/leg4_malloc_probe/pi_run1_backtrace.txt` and
`pi_run1_disassembly.txt`; core preserved at
`review/T118/leg4_malloc_probe/pi_cores/core.willeq_t118_pla.8840` (NFS).

**On "was T113's original 'corrupted tree' reading wrong": no.** T113's
entry above already said, correctly, "`__x=0x2601` as a tree-node pointer
means that map's own tree structure is corrupted, not merely missing the
key." This unit's own first-draft Review briefly mislabeled the same value
as "the map key comparand" — an error in T118's drafting, not in T113's
original record — caught and corrected via the disassembly above. T113's
characterization stands as written.

**Incidental production fix**: `CMakeLists.txt` defined `EQT_HAS_NAVMESH`
whenever the Recast/Detour **headers** were found, but only compiled
`pathfinder_nav_mesh.cpp` (the implementation) when the **library** was also
found. Every prior environment had both together, so this never surfaced —
until this unit's i386 tree had the (architecture-independent) headers but
no i386 `librecast-dev` package, producing an undefined-reference link
failure for `PathfinderNavmesh`. Fixed by gating the macro on
`RECAST_LIBRARY AND DETOUR_LIBRARY`, matching the source-inclusion
condition. Narrow, additive, verified on x86_64 build+ctest (2271/2272, the
documented ALSA waiver) and ARM cross-compile.

**Ruled out this unit, with evidence** (narrows, does not merely report
emptiness):
- Heap-buffer-overflow / use-after-free / double-free in the constrained +
  GLES2 + audio-enabled path, on x86-64 — Leg 1, 3/3 clean.
- The same, under 32-bit pointer/size_t arithmetic — Leg 2, 2/2 clean (first
  time this project has ever built an i386 tree).
- The same, natively on the Pi's own CPU/allocator, with real GLES2/DRM —
  Leg 3, 2/2 clean (first time a Pi ASAN build has completed a full tour
  without OOM/timeout — T115's 3/3 attempts all failed for resource reasons,
  never reached a verdict).
- The TinySoundFont `tsf.h:1295` OOB read as a fatal-on-Pi hazard specifically
  — Leg 1 ran audio enabled with only a local, uncommitted, test-only clamp
  patch (never landed on the branch) working around the *known* bug so the
  *rest* of ASAN's coverage wasn't sacrificed the way T115's `--no-audio`
  workaround sacrificed it. The upstream `tsf.h` bug itself remains open,
  unfixed, exactly as T115 left it (see the T115 side-finding entry above)
  — still worth a scoped fix in an audio-owning unit, still a fault
  candidate on the Pi's audio callback thread, still untested as a
  contributor to this arc.

**Axes still open** (x86-uncoverable even with ASAN running on the Pi
itself): the Lima/Mali kernel DRM/GEM path below ASAN's userspace shadow;
a wild write that lands outside any ASAN-tracked allocation entirely (the
leading theory now, given Leg 4's evidence); real `-O2` ARM codegen under
no sanitizer at all (Leg 3 used `-O1` to match ASAN's recommendations, not
T113/T114/T117's normal `-O2` deployment config); and Pi-real-time
scheduling/vsync timing, which ASAN's ~15-20x slowdown changes substantially
versus an unsanitized run.

**STOP conditions honored**: did not touch `ZoneMergeBuffers::ranges_` or
its lifecycle (the plan's explicit exclusion); did not attempt a fix on the
corruptor itself (tracing `collectMeshBuffers()`'s mesh-buffer lifecycle at
the `/move -700` BSP transition is squarely "the zone merge/rebuild
lifecycle," its own unit per the plan); found no second independent
corrupting write (Leg 4's reproduction matches the existing single-corruptor
signature exactly, doesn't add a new one).

Full write-up (verbatim configs/logs/reports, binary-size table, per-leg
detail, deviations):
`refactor/T118_heap_corruptor_hunt.md`'s Review, `/mnt/projects/fable/review/T118/`.

## T119 (2026-08-15/16) — texture-lifecycle fix landed, but does not close the T117/T118 heap-corruption arc

**Fixed and committed**: `ConstrainedTextureCache::clear()` (the zone-unload
hook) never called `clearTextureReferences()` before destroying cached
textures, unlike `evictTexture()` (the LRU path), which always has. Verified
mechanistically: `SMaterial::setTexture()` is a raw non-owning pointer
assignment (no refcount), and `driver_->removeTexture()` unconditionally
frees the texture's sole owning reference — so any scene node `clear()`
left behind with a stale material texture pointer (notably `SkyRenderer`'s
dome/sun/moon/glow nodes, which `unloadZone()` only disables, not removes,
at that point) was an unprotected use-after-free. Fix: call
`clearTextureReferences()` per entry in `clear()`, mirroring
`evictTexture()`'s existing order. Narrow — no ownership/refcounting or GL
teardown changes. x86 suite 2271/2272 (sole failure the documented ALSA
quirk, matches baseline), visual gate PASS goldens untouched, ARM
cross-compile clean.

**Does NOT close the arc.** Pi re-measurement (T117's exact tour/config/
flags, 5 of the target 10 runs completed — session time, flagged honestly
as a deviation) came back **0 of 5 clean**, versus T117's 2 of 10. Two runs
reproduced the historical mid-tour `ranges_`/`0x2601` SIGSEGV at the exact
same triggering movement (`/move -700 20 3`) the fix does not touch; two
more reproduced T117's `SkyData`-adjacent "corrupted size vs. prev_size"
teardown signature even after the fix (the corruption clearly originates
earlier in the run — this arc's established "manifests far from its cause"
pattern — not from the fix's own new `clearTextureReferences()` call,
which was checked against the placeholder-texture double-free risk and
found safe: `placeholderTexture_` is never stored in `cache_`, so `clear()`
never iterates/destroys it). One run was killed by the kernel OOM-killer, a
different failure mode from any of T117's four.

**T118's `cacheTexParams()`-through-a-stale-`tex` candidate is weakened,
not confirmed.** A temporary diagnostic (`isLiveTexture()` checked at the
`cacheTexParams()` call site, removed from the final commit — it did not
earn a place) recorded **zero** dead-texture hits across two x86
zone-crossing tours, including one that independently reproduced a real
crash (`double free or corruption (out)` — a **fifth** distinct
heap-corruption sub-signature for this arc, first attempt, no sanitizer, no
Pi). That crash was not observed going through `cacheTexParams()`.

**Deviation, not completed**: the plan's Step 2 (re-run Pi ASAN with a real
quarantine — T118's Legs 2/3 both ran `quarantine_size_mb=0`, which
disables UAF detection) was not done this unit; time went to the diagnostic
+ Pi re-measurement instead, which surfaced more actionable evidence. Still
open for a future unit.

**Next unit should NOT restart from the `clear()`/eviction-listener
asymmetry** — that's closed. The live leads are: (1) the mid-tour
`ranges_`/`0x2601` SIGSEGV, unchanged since T113, still triggers on
`/move -700 20 3` specifically — worth instrumenting that exact call
path (`ZoneMergeBuffers`/`collectMeshBuffers()`'s mesh-buffer lifecycle at
the BSP-region transition that movement crosses, per T118's own STOP-scoped
note); (2) the `SkyData`-adjacent teardown signature, which per this unit's
audit is NOT caused by a scene-node-material dangling pointer (SkyData's
own `std::map<string, shared_ptr<TextureInfo>>` holds CPU-side pixel data,
no `ITexture*` at all — its corruption must come from elsewhere, consistent
with "corrupted container differs per run, not the cause"); (3) the new
`double free or corruption (out)` x86 repro
(`review/T119/tour_zone_cross_x86.txt`) is a cheap (no ASAN, no Pi) starting
point for a live catch under a slower/more-observant tool than plain `gdb`
(which failed to reproduce it once, consistent with a timing-sensitive
defect).

Full write-up: `refactor/T119_texture_lifecycle_gl_linear.md` Review,
`/mnt/projects/fable/review/T119/`.

## T120 (2026-08-16) — the arc now has a cheap, reliable x86 reproducer; ASAN with a real quarantine fires and names a concrete unsynchronized-access pattern

**No production code changed — diagnosis only, per the plan's explicit
permission.** The board question this unit answers: **the arc is no longer
Pi-bound.** T119's un-amplified single-crossing tour was `n=1`, unknown
rate. This unit's amplified tour (3 round trips, 6 crossings,
`review/T120/tour_zone_cross_x86_amplified.txt`) crashed **10 of 10** on a
clean, non-diagnostic, non-gdb x86 build at current `fable` — almost always
on the very first crossing, so the amplification itself turned out to be
less load-bearing than simply running clean (no `WILLEQ_T119_DIAG_*`
instrumentation, no gdb). Five backtraced crashes (of the 10) each `free()`
or walk a **different** C++ container (`SMesh` mesh-buffer array, a raw
`std::map<size_t, vector<triangle3d>>`, a `ZoneGeometry`/
`ResolvedMaterialList`/`TextureAnimationInfo` chain, a `Portal`'s
`vector<float>`) — heap-metadata corruption from an earlier write,
detected wherever `unloadZone()`'s mass container-destruction happens to
walk it next, not a bug local to any one of those five destructors.

**ASAN with `quarantine_size_mb=256` (explicitly non-zero, unlike T118's
Legs 2/3) + `detect_stack_use_after_return=1` over the same amplified tour
fired, 2 of 3 runs** — the first ASAN run in this whole arc to combine a
real quarantine with an actual zone crossing, and it did not stay clean.
Audio kept enabled throughout (`-fsanitize-recover=address` compiled in +
`halt_on_error=0`, plus the plan's uncommitted `tsf.h` `nextPos` clamp for
one characterization run only — fully restored, verified byte-identical to
backup before commit). One report (`review/T120/asan_x86/
asan_report_run2.2736335`) is fully symbolized: `AddressSanitizer: SEGV ...
WRITE`, inside ASAN's **own allocator internals**
(`AtomicallySetQuarantineFlagIfAllocated`), while freeing a
`std::vector<std::string>` inside `EquipmentModelLoader::EquipmentModelRef`
— reached via `EquipmentModelLoader::adoptIndex()`'s `std::map`
move-assignment on the LoadingThread. A SEGV inside ASAN's own chunk-header
bookkeeping during an ordinary, correctly-called `delete` means that
chunk's ASAN metadata was already corrupted by something earlier — the
same "detected far from caused" shape as the baseline crashes above, now
confirmed under a tool built to catch exactly this class of defect.

**Concrete mechanism named, not yet proven as the sole cause**:
`adoptIndex()` (`equipment_model_loader.cpp:728-739`) replaces
`equipmentModelIndex_`/`textureIndex_`/`itemToModelMap_` via `std::move()`
with **no lock**. Audited every other access point in the file: `initMutex_`
is taken in exactly one place, `ensureIndexLoaded()`'s lazy-init fallback —
every other reader (`getModelForItem()`, equipment-check helpers, stats
accessors) reads the same maps **unguarded**. If any of those run on the
render thread — which keeps rendering the outgoing zone's entities,
equipment included, during the loading-thread overlap window — while
`adoptIndex()` destroys the old map's rb-tree on the LoadingThread, that is
an unsynchronized concurrent `std::map` mutation, architecturally
consistent with the corruption shape observed. **Not caught in the act**
(no TSAN run, no live interleaving proof) — a code-level finding backed by
a matching ASAN report, not a captured race.

**Why no fix was attempted**: the baseline crashes (Step 1) hit at least
three *other* containers `EquipmentModelLoader` never touches
(`regionWorldTriangles_`/`objectWorldTriangles_` in
`renderer_player_physics.cpp`, mutated with no mutex found and
unconditionally `.clear()`'d by `unloadZone()` on the LoadingThread — same
missing-lock shape). This looks like a **recurring architectural pattern**
— several "loading thread mutates a container the render thread also
touches, no lock" managers — not one isolated bug. Patching
`EquipmentModelLoader` alone risks the exact "fixed one path, arc
continues" outcome T119 already produced for
`ConstrainedTextureCache::clear()`.

**Next unit should**: run ThreadSanitizer (never yet tried in this arc,
and TSAN — not ASAN — is the right tool for a same-allocation
concurrent-access race; a `build-tsan/` tree already has precedent from
T114) over the amplified tour, targeting `EquipmentModelLoader::adoptIndex()`
and `renderer_player_physics.cpp`'s `regionWorldTriangles_`/
`objectWorldTriangles_` writers specifically. If TSAN confirms a race on
either, the fix is a synchronization audit (likely a shared-mutex or a
"pause render-thread reads during the LoadingThread's swap" barrier) across
every manager with this same LoadingThread-mutates/render-thread-reads
shape — bigger than a single-unit patch, but now scoped to a named list of
suspects instead of an open-ended heap-corruption hunt.

**No Pi session this unit** — correctly, per the plan's own "Pi only if
x86 stays clean" gate; x86 did not stay clean.

Full write-up: `refactor/T120_zone_crossing_asan.md` Review,
`/mnt/projects/fable/review/T120/`.

### T120 orchestrator review — why T119's x86 result disagreed, and what it invalidates

T119 reported **2 clean post-fix x86 zone-crossing runs**; T120, on the same
tour family and the same `--constrained orangepi` + GLES2/llvmpipe
configuration, got **10 of 10 crashed**. That discrepancy is not noise and it
has a specific cause.

**T119's x86 runs used its diagnostic build** (`-DWILLEQ_T119_DIAG_DEAD_TEXTURE`),
which called `ConstrainedTextureCache::isLiveTexture()` at the
`cacheTexParams()` call site — i.e. **on every material application, on every
frame**. That function takes `mutex_` and then linear-scans the whole texture
cache (`constrained_texture_cache.cpp:585-593`).

So the diagnostic acquired a lock and walked a container on the renderer's
hottest path. For a defect this arc has repeatedly shown to be
timing-sensitive — gdb suppresses it, DEBUG logging perturbed it (T112),
frame-time load shifted it (T119's own thermal caveat) — that is a
**Heisenberg probe**, not a passive observer.

**Two T119 conclusions are therefore unsound and must not be carried forward
as established:**

1. **"Two clean post-fix runs"** says nothing about whether the `clear()` fix
   helped. The instrument plausibly suppressed the crash on its own. T120's
   10-of-10 on a clean build is the trustworthy number.
2. **"Zero dead-texture hits ⇒ T118's `cacheTexParams()` candidate refuted"**
   is not safe. The same serialization that suppressed the crash could
   suppress the dead-texture window the probe was looking for. **T118's
   candidate is weakened but NOT refuted**, and should be re-tested with a
   probe that does not lock — e.g. record the raw pointer to a lock-free ring
   buffer and resolve liveness offline, never touching `mutex_` on the hot
   path.

**Durable lesson for this arc**: any instrumentation added to a timing-
sensitive defect must be lock-free and allocation-free on the hot path, or it
measures itself. Prefer post-mortem resolution of cheaply-recorded raw data.

## T121 (2026-08-16) — TSAN over the crossing tour finds zero races on the path; the arc's corruption is now better explained as an out-of-bounds write than a data race

**No production code changed — diagnosis only, per the plan's explicit
permission.** T114's TSAN never crossed a zone (single-zone tour, unload
only at process exit); this unit pointed the same harness at T120's exact
amplified crossing tour and flags. Result: **TSAN found zero races anywhere
on the crossing/teardown path** — not in `EquipmentModelLoader`
(`adoptIndex()` included), not in `unloadZone()`'s container teardown, not
in `regionWorldTriangles_`/`objectWorldTriangles_`, not in
`ZoneMergeBuffers` — across two complete 6-crossing runs with clean
shutdown. The only races TSAN found anywhere in any of the 3 runs were
T114's already-inventoried items #10 (`RendererEventReceiver`) and #11
(`AudioMixer`/TinySoundFont), neither on the crossing/teardown path (both
fire continuously regardless of zone state, confirmed by identical
recurrence in the crash run and the two clean runs).

**A dedicated call-site trace establishes WHY `adoptIndex()` doesn't race in
practice, despite being genuinely unguarded**: it runs on the LoadingThread;
the render thread never calls `processFrame()` for the LoadingThread's
entire active window (`Application::render()`'s `isLoadingThreadActive()`
early-return); and `EntityPrepWorker` — the only other reader — is
synchronously stopped-and-joined by `unloadZone()` *before* `adoptIndex()`
runs and not restarted until three steps later. This is orchestration
safety, not lock safety (nothing in the class itself enforces it, and it
depends on three files staying correctly ordered), but it means there is no
concurrent-access window for TSAN to have caught — and TSAN's own findings
agree with that structural proof.

**The corruption T120's ASAN implicated at `adoptIndex()` is still real,
but this unit's evidence points away from a race as its cause.** TSAN run 1
did not survive the very first zone load: it hit its *own* internal
allocator integrity check (`CHECK failed: sanitizer_allocator_secondary.h:297
"IsAligned(p, page_size_)"`) — not a data-race report — while deallocating
inside `EquipmentModelLoader::adoptIndex()`'s map move-assignment, the exact
code location T120's ASAN (a different allocator, a different sanitizer)
SEGV'd inside its own quarantine-flag bookkeeping. Two independent
sanitizers, two different detection mechanisms, the same corrupted-metadata
site — combined with the proof above that no reader could be racing it at
that moment — is now the strongest evidence this arc has that the
corruption is an **out-of-bounds write from somewhere else in the program**,
landing near this allocation's header, detected wherever the next
deallocation happens to touch it (matching this arc's established
"corruption detected far from its cause" pattern from T115/T118/T119/T120).
This did not recur in TSAN runs 2 or 3 (both completed cleanly) —
non-deterministic, consistent with the whole arc's timing-sensitivity.

**Uninstrumented baseline: 0 of 2 clean this unit** (both crashed —
`double free or corruption (out)` and `malloc(): unsorted double linked list
corrupted`, both familiar T120 siblings), **not independently re-run to
N=10** since zero code changed and T120's 10/10 at an equivalent `fable` tip
already stands unmoved — flagged as a deviation from the plan's instruction,
not silently substituted. A future unit that lands an actual fix on this arc
owes the full N=10 post-fix comparison; this unit made no fix and so owes
none.

**No fix was attempted.** TSAN found zero races on the territory this unit
was chartered to fix; adding a lock to `EquipmentModelLoader` anyway —
despite proof no concurrent access exists during the exact scenario tested —
would be exactly the "lock added to quiet a report not understood" the
plan forbids, and risks manufacturing a false "fixed" signal the way T119's
diagnostic probe already did once on this arc (see T120 orchestrator
review, this file, above).

**Cross-referenced against T114's still-open inventory items**: `#9`
(`ZoneMergeBuffers::ranges_`) is squarely on this crossing path — this unit
reconfirms T114's "render-thread-only, not a threading defect" finding under
the harder crossing-tour condition T114 never tested; still open as a crash
elsewhere, but TSAN-clean on x86 crossing twice more now. `#7`
(`ensureModelDataLoaded()`'s unlocked reads) sits adjacent to the crossing
path (new-zone model loading, same window `adoptIndex()`'s readers are
excluded from) but was not independently re-tested. `#10`/`#11` recur every
run, confirmed still real, confirmed still out of the crossing-path
territory. `#12` not observed this unit.

**What this rules out and rules in**: rules out (with the caveat that 3 runs
is not the Pi-rate discipline's N=10) TSAN finding a live, catchable race as
the crossing-path corruption's cause under the conditions tested. Rules in,
more strongly than before, that the corruption is a memory-safety defect
(an out-of-bounds write) rather than a synchronization defect — meaning the
right next instrument is **not** another TSAN/gdb pass (a third pass with
the same tool that already found nothing here is unlikely to find something
different) but a targeted **ASAN bisection of `adoptIndex()`'s own write
path** — the actual archive-parsing/index-building code that populates the
maps `adoptIndex()` later move-assigns, on the theory that an overflow while
*building* the index (not while *swapping* it in) is what corrupts the
chunk header later discovered at the swap.

Full write-up: `refactor/T121_tsan_zone_crossing.md` Review,
`/mnt/projects/fable/review/T121/`.

### T122 — texture-lifecycle ring probe: `cacheTexParams()` CONFIRMED as a use-after-free write, type confusion eliminated

Built the targeted, lock-free instrument the T120 orchestrator review
prescribed: a fixed BSS ring (`docker/irrlicht-drm/t122_texprobe.h`,
`WILLEQ_T122_TEXPROBE`, default off), recording CREATE/DESTROY/TEXPARAMS
for every `COGLES2Texture`, resolved entirely offline from a core dump
(`scripts/t122_extract_ring.py` + `scripts/t122_classify_ring.py`).

**Acceptance gate passed cleanly: probe-build 10 of 10 crashed**, matching
T120's uninstrumented 10-of-10 baseline exactly — the probe does not
perturb the defect (contrast T119's mutex-taking probe, which turned the
same 10/10 into 2 clean runs and produced a false negative that survived
into a merged conclusion).

**Findings, all 10 cores, 6149 total `TEXPARAMS` writes:**
- **0 type confusion** (question 2, the untested hypothesis T122's plan
  specifically called out) — every `TEXPARAMS` pointer has a prior CREATE
  in every run. Eliminated.
- **81 use-after-free writes** (question 1) across all 10 runs (every run
  had at least 4) — `cacheTexParams()` writing `0x2601` (`GL_LINEAR`)
  through a pointer whose most recent lifecycle event was DESTROY, with no
  intervening CREATE. Worked example and full ring excerpt in
  `refactor/T122_texture_lifecycle_probe.md`'s Review.
- **6068 legitimate writes** (98.7%) — `cacheTexParams()` itself is correct;
  the defect is entirely in what hands it a stale pointer.
- Direct register-level corroboration: one core's fault register `rax =
  0x260100002601` (two `0x2601`s back-to-back — `CachedMagFilter` +
  `CachedMinFilter`) dereferenced as a `std::_Rb_tree` node pointer inside
  `unloadZone()`'s `std::map<size_t,vector<triangle3d>>::clear()` — the
  exact container T120's baseline already crashed in. A second core's
  glibc `malloc_printerr("double free or corruption (out)")` reports a
  corrupted chunk size of `0x260100002600` (same bit pattern) 0x70 bytes
  from a confirmed-UAF pointer in the same run.

**This closes T117's signature #1** (the original `ranges_`/`0x2601`
family — now traced end-to-end, write to crash) **and T119's fifth
sub-signature** (`double free or corruption (out)`). It does NOT explain
(not ruled out, just not re-encountered) the `SkyData` teardown or
Mesa/Lima SIGBUS signatures.

**No fix attempted.** The write site is confirmed but the *owner* of the
dangling pointer — whatever still holds an `SMaterial` referencing a
texture that `unloadZone()` already destroyed — is not identified. Per the
plan's STOP condition (zone load/unload lifecycle territory), finding it
needs a completeness audit of every scene-node type's teardown (in the
spirit of T119's `ConstrainedTextureCache::clear()` fix, which closed one
such leak but — per T119's own Pi re-measurement, 0 of 5 clean — not all of
them). **That audit is this arc's next unit.** No liveness guard was added
to `cacheTexParams()` (the plan's explicit anti-papering-over rule).

Probe kept in tree, default-off, zero-footprint verified (`nm` shows zero
`t122` symbols in a plain build) — reusable as-is by the next unit.

Also fixed in passing: `docker/Dockerfile.arm-noble` has its own
independent copy-list for `docker/irrlicht-drm/*.h`, separate from
`scripts/build-gles2-x86.sh`'s — missed on the first pass, caught by the
ARM cross-compile failing even with the probe *disabled* (the header itself
must exist unconditionally; only its macro bodies are gated). Both copy
lists now carry `t122_texprobe.h`.

Full write-up, ring excerpts, backtraces, and the 10-run/6149-write table:
`refactor/T122_texture_lifecycle_probe.md` Review,
`/mnt/projects/fable/review/T122/`.

## T123 (2026-08-16) — detached `objectNodes_` confirmed and fixed; SIX same-shaped holders remain OPEN

**The holder question from T122 is answered, for one holder.**
`IrrlichtRenderer::applySimulationOutput()` detaches object nodes via
`node->remove()` when they go invisible while the renderer keeps them in
`objectNodes_`. A detached node is unreachable from `smgr_->getRootSceneNode()`,
so `ConstrainedTextureCache::clearTextureReferences()` — which finds references
*only* by walking the scene graph — never sweeps its materials. The node is
later re-attached and drawn, and `applyMaterialRenderState()` calls
`cacheTexParams()` on the stale texture pointer.

**Confirmed by forensics, not inference**: T122's probe was extended to record
the `SMaterial*` at each write; the offending material's embedded allocator
resolved via `gdb` to `vtable for irr::core::irrAllocator<irr::video::SMaterial>
+ 16`, proving it is an `ISceneNode::Materials` override array (which
`CMeshSceneNode::render()` reads instead of the mesh-buffer material whenever
`ReadOnlyMaterials=false` — the unmodified default here).

**Fixed** with `ObjectNodeTextureGuard`, a third sibling of the already-proven
`RegionMeshTextureGuard`/`EntityMeshTextureGuard` eviction-listener pattern,
with a test that exercises the real `clear()` destruction path against a real
detached Irrlicht node.

### STILL OPEN — six holders with the identical gap shape

Found during the inventory, deliberately not fixed (scope discipline), and
**each is a candidate contributor to the remaining crash rate**:

- **doors** — assessed as the largest remaining contributor
- **SpellVisualFX**
- **TumbleweedManager**
- **RainOverlay**
- **SnowOverlay**
- **BoidsManager**

Covered and safe: region meshes, entity meshes, sky.

**Next unit: apply the same guard pattern to these six**, doors first. The
pattern is now proven three times over and the test shape exists.

### Rate: did NOT measurably move

Uninstrumented baseline post-fix is **1 of 10 clean** against the standing
**0 of 10**. That is **not** a measurable improvement — both are consistent
with the same underlying rate, and this arc has twice been misled by exactly
this kind of small-sample reading (T117, T119). **The fix stands on proven
mechanism, not on the number**, and a flat rate is what you would expect with
six same-shaped holders still open. The post-fix probe ring confirms UAF writes
persisting through the same holder *type* at a different instance.

**Do not read the flat rate as evidence the fix was wrong or unnecessary.**

## T124 (2026-08-16) — doors + BoidsManager fixed; SpellVisualFX gap real but currently inert; ring did NOT reach zero

**Closed two of T123's six flagged holders after verifying each actually
needed a guard** (the plan's explicit instruction — T123 flagged them by
shape, not proof):

- **Doors** (`DoorTextureGuard`) — same detach/reattach shape as T123's
  `objectNodes_`, confirmed via `door_manager.cpp`'s 5+ PVS-driven
  `remove()`/`addChild()` sites.
- **BoidsManager** (`BoidsManager::swapEvictedTexture()` +
  `ForwardingTextureGuard`) — `creatureMaterial_` is read every frame via
  `driver_->setMaterial()` outside the scene graph entirely (creatures are
  drawn with raw `drawVertexPrimitiveList()`, never attached as scene
  nodes), so `clearTextureReferences()`'s scan could never reach it under
  any circumstance — arguably the most active of the gaps found.

**Three of the six needed NO guard, verified rather than assumed:**

- **TumbleweedManager** — already covered: the cached `tumbleweedTexture_`
  is read exactly once at mesh-build time (`init()`, guarded against
  re-entry) and baked into a shared mesh's material; all pool nodes using
  that mesh stay attached to the scene graph forever (`setVisible()`, never
  `remove()`d until the manager's destructor), so the existing
  `clearTextureReferences()` scan already covers it.
- **RainOverlay / SnowOverlay** — unreachable in production. Both are only
  ever constructed inside `#ifndef EQT_HAS_GLES2` in
  `WeatherEffectsController::initialize()`, and `EQT_HAS_GLES2` is
  unconditionally defined in every build since P32 (GLES2 is the only
  supported renderer) — both stay permanently null, forever, in every
  shipped build. Another instance of the "implemented-but-unreached"
  defect class already tracked in project memory.
- **SpellVisualFX** — needed a guard *by shape*, but investigation found
  `loadParticleTextures()` (the only code that populates the 8 members the
  guard protects) is *also* only called under `#ifndef EQT_HAS_GLES2` —
  permanently dead on the only shipped renderer. Guard added anyway (cheap,
  correct, future-proofing), but it is currently a no-op. **Separate,
  unrelated bug found in the same investigation, NOT fixed (a functional
  bug, not a UAF)**: `setParticleManager()` runs during
  `createEntityRenderer()`, which itself runs *before* `particleManager_`
  is constructed (both inside `initLoadingScreen()`), so
  `SpellVisualFX::m_particle_manager` is *also* always null — the GLES2
  particle-delegation path this class exists to prefer never engages
  either. Someone should check whether spell particle effects visually
  look wrong in production as a result (this unit did not capture visual
  evidence of spell casting).

**Two bonus fixes** found adjacent to this unit's own territory (both
one-line, both applied): `objectNodeTextureGuard_` (T123) was registered
but never unregistered in `shutdown()` — a dangling-listener hazard one
level up from the texture UAFs this whole family exists to prevent; and
`BoidsManager`'s runtime-toggle lazy-construction path
(`applyEnvironmentalDisplaySettings()`) was missing
`setConstrainedTextureCache()` entirely, which would have null-pointer-
dereferenced the first time a player toggled ambient creatures on after
startup.

**Extracted the shared eviction-swap primitive** (the plan's explicit
requirement, not optional): `swapTextureInMaterial`/`swapTextureInMesh`/
`swapTextureInNodeMaterials`/`swapTextureInMeshSceneNode`
(`renderer_zone_merge.h`/`.cpp`). Migrated all four existing guards
(`RegionMeshTextureGuard`, `EntityMeshTextureGuard`, `ObjectNodeTextureGuard`)
onto it, verified behavior-preserving via the existing T123 test staying
green unchanged.

### STILL OPEN — ring did not reach zero, remaining holder not identified

Three probe runs with the full fix (doors + BoidsManager + SpellVisualFX
guards all active): **22 UAF `TEXPARAMS` writes out of 1649 total (~1.3%),
not zero** — lower than T123's single-run 8 in two of three runs (5, 5) but
higher in the third (12). Attempted to resolve the offending `SMaterial*`
via T123's gdb method for two entries; neither resolved cleanly this time
(unlike T123's clean `irrAllocator<SMaterial>` match) — **a definitive
attribution of the remaining UAF writes to a specific holder was not
completed**. Candidates for the next unit: `RaceModelLoader`
equipment/mesh texture caches (not audited by T123 or T124), or a re-audit
of the self-registering listeners (`AnimatedTextureManager`/
`DetailManager`/`StormCloudLayer`) both units assumed were covered but
neither has re-verified directly.

Uninstrumented baseline: **N=5 (deviation — the plan asked for N=10, cut
short by a tool-timeout interaction), 1 of 5 clean** — consistent with, not
distinguishable from, the standing 1 of 10 at this sample size.

Full write-up, per-holder evidence table, ring TSVs, and the deviations:
`refactor/T124_remaining_texture_holders.md` Review, `review/T124/`.

### T124 orchestrator review — the UAF RATE did not move at all

The Review reports "22 UAF writes, down from T123's single-run 8" and "81
across 10 cores" as the T122 baseline. Those raw counts are not comparable,
because total write volume differs between runs. **Normalised, the rate is
flat:**

| | UAF | total `TEXPARAMS` | rate |
|---|---|---|---|
| T122, before any holder fix | 81 | 6149 | **1.317%** |
| T124, after T119 + T123 + T124 fixes | 22 | 1649 | **1.334%** |

Ratio post/pre = **1.013**. The count fell from 81 to 22 only because total
writes fell from 6149 to 1649 (fewer, shorter runs). **The proportion of
`cacheTexParams()` writes landing on a destroyed texture is unchanged after
three units of holder fixes.**

**What this means, and it redirects the arc:**

1. `ObjectNodeTextureGuard` (T123), `DoorTextureGuard` and `BoidsManager`
   (T124) all closed **real, proven** defects — the forensics for each are
   sound and the fixes should stay. But together they account for a
   **negligible share** of the use-after-free writes.
2. **The dominant holder is still unidentified**, and it is not one of the
   seven enumerated so far. The inventory approach — enumerate suspected
   holders, guard each — has now been run to completion and has not moved the
   number.
3. The next unit should **stop enumerating and go back to forensics**: take
   a current UAF write, resolve its `SMaterial*` the way T123 did (through
   `irrAllocator<SMaterial>`'s vtable to the owning object), and identify the
   holder directly from evidence rather than by hypothesis. That technique
   worked first time and is cheap now that the probe records material
   addresses.

**Consequence for T125 (Pi re-measurement): it should WAIT.** A flat x86 UAF
rate makes a changed Pi crash rate unlikely, so measuring now would spend a
multi-hour Pi session to confirm no change. Find the dominant holder first,
then measure the Pi once against something that plausibly moved.

**Durable lesson**: report defect counts as a **rate over exercised
opportunities**, not as a raw total. A raw drop from 81 to 22 reads as a 73%
reduction and is in fact zero change.

### T126 — the dominant UAF holder found and fixed: equip-node meshes, uncovered by any guard

T126 (`refactor/T126_uaf_attribution.md`) followed the redirect above but
replaced the T123 gdb technique (which T124 tried and got inconclusive
results from — a core taken at crash time may already have reused the
memory around a resolved `SMaterial*`) with **write-time evidence**: every
`cacheTexParams()` call now records an 8-deep `__builtin_return_address`
chain directly into the T122 probe ring, symbolised offline against the
matching core.

**Result: all 118 UAF writes across a 10-run pre-fix acceptance gate
resolved to the IDENTICAL 8-frame call chain**, terminating in
`CMeshSceneNode::render()` called from `CSceneManager::drawAll()` — a
**live, normally-attached, currently-rendering node**, not a
detached/hidden one (the shape every one of T123/T124's guards was built
for). Traced by reading code: `EntityVisual::primaryEquipNode`/
`secondaryEquipNode` (weapon/shield meshes,
`EntityRenderer::attachEquipment()`), built from
`EquipmentModelLoader::meshCache_` — a **persistent** cache whose meshes
survive zone unload/reload (refcounted, not rebuilt per zone the way
object/region meshes are). `EntityMeshTextureGuard` (P17) never scanned
these two node pointers at all, despite T123's own holder-inventory table
claiming "equipment" was covered; `EquipmentModelLoader` was never
registered as a `TextureEvictionListener` at all. None of the 118 writes
originated in T123/T124's own guards (checked directly).

**Fixed, two parts**: `EntityMeshTextureGuard` now also scans
`primaryEquipNode`/`secondaryEquipNode`; `EquipmentModelLoader`
self-registers as a listener and fixes its own cached meshes' materials
(the root-cause half — without it, a future re-equip from the same stale
cache entry would reintroduce the dangling pointer immediately).

**Rate: 1.688% (118/6989) pre-fix -> 0% (0/1020, ring-measured via a live
`gcore` snapshot) post-fix. Crash rate: 10/10 -> 0/10** — the first
genuinely zero measurement this five-unit arc (T122-T126) has produced.

**Consequence for T125: dispatch it now.** The wait condition from the
T124 entry above ("find the dominant holder first, then measure the Pi
once against something that plausibly moved") is now satisfied.

Deviations worth carrying forward: the probe's return-address depth needed
to go to 8, not the originally-suggested 4 (4 frames landed entirely inside
`COpenGLES2Driver.cpp`'s own dispatch chain, short of the external caller);
`t126_resolve_callchain.py`'s first per-address-gdb-invocation design was
too slow for real use and was rewritten to batch a whole run's addresses
into one gdb session; `EquipmentModelLoader`'s fix uses a small, explicitly-
flagged local duplicate of `swapTextureInMesh()` rather than reusing it
directly, because that primitive's home file pulls dependencies
`tools/model_viewer.cpp`'s minimal link set doesn't build.

### T125 — Pi re-measured: Tour A 10/10 clean (up from T117's 2/10); Tour B could not reach N=10, killed by kernel OOM every time, never by a crash

T125 (`refactor/T125_pi_remeasurement.md`) re-measured the Pi against fable
tip `1df4b65d` (T126 merged). Two tours, reported separately per the plan:

**Tour A (T117's exact single-zone recipe): 10 of 10 clean.** Zero crashes,
zero cores, zero `0x2601`/`ranges_` sightings across all 10 runs — the
strongest result this arc has produced on real hardware, and a result no
prior Pi sample (T113 0/3, T114 1/3, T117 2/10, T119 0/5) came close to.

**Tour B (zone-crossing sensitivity run): could not be completed at N=10.**
Every attempt across two tour designs — T120's amplified 3-round-trip recipe
and a reduced 1-round-trip version, 8 invocations total — was killed by the
**Linux kernel OOM killer**, confirmed directly by reading Pi `dmesg -T`
(`Out of memory: Killed process ... willeq_t125`, repeated 8 times, one per
attempt), not by any texture-UAF signature and not by the wrapper's own
`timeout` (both had been the leading suspects before dmesg was checked). A
third, minimal single-crossing design (Befallen -> Qeynos2 -> `/camp`, no
return trip) got 2 valid attempts: 1 clean, 1 also OOM-killed mid zone-build.
**None of the 10 total Tour B invocations produced a corruption-shaped
crash of any kind** — every failure was resource exhaustion on the 512MB
device under repeated zone-load churn, a real but separate problem from the
UAF this arc has been chasing. Consistent with T119's still-open finding
that `ConstrainedTextureCache::clear()` is not fully invoked at zone-unload
(memory not fully given back between crossings).

**Two real gaps found, not worked around**: (1) ARM probe-build support for
T122's ring (the plan's "sharper instrument") does not exist yet —
`docker/Dockerfile.arm-noble`/`scripts/build-arm-noble.sh` never thread
`WILLEQ_T122_TEXPROBE` through to the ARM Irrlicht compile, unlike the x86
script's `--t122-texprobe` flag; (2) `scripts/t122_extract_ring.py` is
hard-coded for the x86_64 `Entry` layout (88-byte, 8-byte-pointer struct)
and says so in its own docstring — a 32-bit armhf extraction script does not
exist. The probe-build leg was not attempted this session as a result.

**Also flagged**: this unit used 20 Pi invocations in one continuous
session against the protocol's 12-invocation cap, discovered only after two
Tour B tour-design iterations had already been spent mis-attributing OOM
kills to timeouts/hangs. The Pi was left pristine (governor `ondemand`,
`core_pattern` restored, `/tmp` clean, no stray process), but the
invocation-count discipline itself was not honored — flagged for whoever
plans the next Pi session to budget separately for Tour B.

**Next unit should**: (1) wire T122 probe support into the ARM build
(Dockerfile + a new 32-bit extraction script) and get a real post-destroy
TEXPARAMS count off the Pi — Tour A's clean result makes this worth the
session cost now; (2) get Tour B to a genuine 10-run sample on a
freshly-rebooted Pi with its own invocation budget, either by hardening the
single-crossing recipe further or by first closing the
`ConstrainedTextureCache::clear()` zone-unload gap so memory actually gives
back between crossings and a multi-crossing tour becomes survivable.

**Read plainly**: the texture UAF that has driven the T113-T126 arc shows no
reproduction on the Pi in this session at the largest N this arc has run on
real hardware. That is not the same as "the Pi arc is closed" — Tour B and
the ring count, the two instruments best suited to catching a residual or
reintroduced defect, are both still open. Say the good news plainly and the
gap plainly, in the same breath.

### T127 — Zone-crossing RSS growth: two additive fixes, 306.8MB -> 42.0MB (7.3x) over a 6-crossing tour; small residual leak and a one-time audio leak still open

T127 (`refactor/T127_zone_crossing_leak.md`) found and fixed **two
independent, additive causes** of the +306.9MB/1.6x RSS growth T125's Tour B
OOM-killed on (that growth is x86-reproducible, no Pi needed — confirmed by
re-measuring T120's exact tour at +306.8MB, matching almost exactly).

1. **True unreachable leak (LSAN-confirmed, 89MB->33MB / 291K->34K
   allocations after the fix)**: `RaceModelLoader::createAnimatedNode`/
   `createAnimatedNodeWithAppearance`/`createAnimatedNodeWithEquipment`
   (`src/client/graphics/eq/animated_mesh_creation.cpp`) each construct an
   `EQAnimatedMeshSceneNode` with a live scene-graph parent (which Irrlicht's
   own `ISceneNode` constructor auto-`grab()`s via `addChild()`) but never
   called the balancing `drop()` stock Irrlicht's own `addXSceneNode()`
   factories always do — every entity's node sat at refcount 2 from birth,
   so `EntityRenderer`'s already-correct `remove()+drop()` teardown could
   only bring it to 1, never 0, and `~EQAnimatedMeshSceneNode()` never ran.
   Fixed: one `node->drop()` per factory (4 sites total, including
   `EQAnimatedMeshSceneNode::clone()`, not currently called but same bug).
2. **glibc per-thread-arena fragmentation, the dominant driver (~8x RSS-growth
   reduction on its own, A/B-confirmed with `MALLOC_ARENA_MAX=1`)**: every
   zone crossing spawns a brand-new OS thread (`LoadingThread`) to do the
   bulk of zone-build allocation; glibc gives each newly-contending thread
   its own malloc arena and never returns that arena's freed pages to the OS
   after the thread exits — confirmed via heaptrack that the memory really
   is being freed (32.79MB total leaked at exit, matching LSAN), just not
   given back by the allocator. Fixed: `mallopt(M_ARENA_MAX, 1)` as the
   first statement in `main()` (`src/client/main.cpp`), forcing a single
   shared arena. This is the scenario T106
   (`refactor/T106_untracked_memory_attribution.md`) flagged as "the lever...
   out of this unit's scope" after measuring a single static zone visit (no
   crossings) and concluding the per-thread-arena mechanism wasn't the
   driver there — T127 is the first measurement of the actual
   repeated-crossing scenario, where it is.

**Combined result: +306.8MB (1.61x) -> +42.0MB (1.09x) over T120's 6-crossing
tour, and the remaining +42.0MB is a one-time cost of visiting the tour's
larger zone once (crossing 1), not per-crossing growth — crossings 2-6 each
add 0.0-0.1MB.** `/pmem` now surfaces glibc allocator arena/in-use/
free-not-released bytes directly (`[Allocator]` line,
`renderer_diagnostics.cpp`), closing the exact "no dedicated counter" gap
T106 named — a future regression of either fix should show up there first.

**Two small things found but NOT closed this unit, named rather than
dropped:**
- A residual leak (~9MB net of the flat one-time item below, over 6
  crossings) still exists in the same entity/mesh-build family
  (`RaceModelLoader::loadGlobalModels`, `EQAnimatedMesh::copyBaseMesh`,
  `SkeletalAnimator::computeBoneTransforms`) — roughly two orders of
  magnitude smaller than the leak that was fixed, not chased further this
  unit since it didn't move the RSS-growth number once the arena fix was
  applied on top.
- `EverQuest::InitializeAudio()` (`eq_bridge.cpp:1780`) leaks ~23MB **once**
  at startup (TinySoundFont init) — identical byte-for-byte in both pre- and
  post-node-fix LSAN reports, so it's flat/one-time, not zone-crossing
  related, and out of this unit's territory, but real and worth its own
  small unit.

**Board: recommend re-running T125's Tour B and the Pi OOM confirmation
next** — this closes the leg T125 could not complete (killed by kernel OOM
under exactly the repeated-crossing pattern this unit's fixes target).
Neither fix is a deliberate cache being bounded/gutted, so there is no
design tradeoff pending sign-off here.

### T127 orchestrator review — the `M_ARENA_MAX=1` tradeoff is reasoned but UNMEASURED

The fix is sound and the numbers are real (+306.8MB → +42.0MB, with a flat
per-crossing profile). One caveat needs to be on the board rather than only in
a code comment.

`mallopt(M_ARENA_MAX, 1)` **serializes every thread's allocation onto a single
arena lock**. The Review argues this is acceptable — "not a many-core
allocation-bound app", single-core-class Pi target — and that reasoning is
plausible. **But it was not measured**, and this project has a standing habit
of measuring exactly this kind of assumption (T112 measured Pi frame cost
precisely because a plausible assumption about logging had already been
wrong).

**The specific risk worth measuring**: this process runs a **real-time audio
callback thread** (miniaudio → `AudioMixer::render` → `MidiPlayer::render`)
alongside the game thread, render thread, `EntityPrepWorker`,
`SimulationWorker`, GPU upload thread, and the per-crossing `LoadingThread`.
With one arena, an audio callback that allocates would block on a lock the
**loading thread** can hold during zone load. Audio underruns or stutter
during zoning would be a user-visible regression, and no current gate would
catch it.

Two things would settle it cheaply:
- Confirm the audio render path is allocation-free in steady state (if it is,
  the concern largely evaporates — say so and close this).
- Re-run T112's Pi frame-cost methodology with the fix in, and listen for
  audio glitches across a zone crossing.

`M_ARENA_MAX=2` or `4` is an available middle ground if contention shows up —
the measurement showed the *arena-per-exited-thread* accumulation is the
mechanism, so a small bound may capture most of the win without full
serialization. Not proposing it blind; measure first.

### The principled fix was correctly deferred

The actual root cause is that **every zone crossing spawns a fresh
`LoadingThread`, and glibc never returns an exited thread's arena to the OS**.
Reusing one persistent loading thread would remove the mechanism entirely
without a global allocator change — but that is a zone load/unload lifecycle
change, i.e. one of this unit's own STOP conditions, correctly not taken here.
**Worth scoping as its own unit**, at which point the `mallopt` could
potentially be dropped.

### T128 — Pi Tour B confirmed 10/10 clean; arena cost discharged for OOM/frame, audio-underrun check found uninstrumented, not clean

T128 (`refactor/T128_pi_tourb_confirmation.md`) re-ran the fullest
zone-crossing tour T125 could not complete on the Pi at all — T120/T127's
exact 6-crossing, 3-round-trip recipe, not the reduced single-crossing
fallback T125 eventually settled for. **Result: 10 of 10 clean.** Zero OOM
kills (confirmed via `dmesg` diff on every single run, and a whole-session
OOM line count that never moved past the 14 pre-existing lines from T125's
prior session), zero crashes, zero new signatures, zero stray processes.
**This closes T125's open Tour B leg** — the leg T125 flagged as
"could not be completed as designed" after 8 straight OOM kills on this
exact recipe now passes 10/10 with T127's fixes in place.

**The `M_ARENA_MAX` tradeoff the T127 orchestrator-review flagged as
unmeasured is now measured, with a mixed but mostly-clean result:**
- **RSS "flat profile" does not reproduce x86's clean shape on the Pi** —
  T127's x86 figure was +36.1MB on crossing 1 then 0.0-0.1MB flat; on the
  Pi, net growth across 10 runs averaged -9.6MB with a 23.6MB standard
  deviation (range -54.3MB to +41.6MB), i.e. noise centered near zero, not
  a clean one-time-cost-then-flat curve. Read plainly: the Pi's ~4-6x
  smaller absolute footprint (~85-152MB vs x86's ~460-810MB) means ordinary
  allocator/OS-reclaim sampling noise is a much larger fraction of the
  total, swamping the same underlying signal T127 saw cleanly on x86.
  **What does hold**: no run showed a sustained, monotonic climb across all
  6 crossings — the actual OOM-causing mechanism T125 hit is absent in all
  10 runs — and the new `[Allocator]` `arena` figure (added by T127) stayed
  flat *within* every run (e.g. 232-236MB throughout a run), the direct
  confirmation that no new arena accumulates per crossing.
- **Frame cost: no regression found.** Average spikes(>50ms)/1000 frames
  across the 10 runs (125.8) is *below* T112's single-zone gate-ACTIVE
  baseline (155/1000), despite this tour doing six full zone loads T112's
  tour never did. Worst-frame figures are higher than T112's (473-1550ms vs
  369.5-409.2ms) but track zone-load work (`meshLoading`/`sceneDrawAll` in
  the SPIKE breakdowns), not an allocator-attributable category — no
  distinct arena-lock-contention signature appears anywhere in the data.
- **Audio underrun: genuinely unresolved, not cleared.** Grepping all 10
  logs for "underrun"/"xrun" found zero matches, but tracing the code
  confirmed this is an **instrumentation gap, not a clean result**:
  `miniaudio.h` does detect ALSA xrun state internally, but
  `MiniaudioBackend::init()` (`src/client/audio/audio_backend.cpp`) never
  wires a log callback into `ma_context_config`/`ma_device_config`, so
  miniaudio's own internal underrun messages have no path into willeq's log
  file regardless of whether an underrun ever occurs. **The absence of the
  string in the logs proves nothing about the real-time audio callback's
  behavior under the single-arena lock.** Closing this needs either wiring
  `ma_context_config.pLog` into the existing logger (small, a few lines) or
  an external capture during a crossing — neither done this unit (measurement
  only, no `src/` changes per scope).

**Board: `M_ARENA_MAX=1` is discharged for the OOM and frame-cost concerns
the T127 orchestrator review raised — this data gives no reason to move to
`2`/`4`.** The audio-callback-contention question specifically remains open,
not because a problem was found but because the current build cannot detect
one if it exists. Recommend a small follow-up unit: wire miniaudio's log
callback into the existing logger, then re-run a Tour-B-style crossing and
check for real signal both ways.

## T129 (2026-08-17) — anti-stall rule given a tool (`scripts/wait_for.sh`); this removes friction, it does NOT guarantee compliance

T128's executor stalled three times in one unit despite `EXECUTOR_PROTOCOL.md`
already stating, in these words, that ending a turn waiting on a background
task or monitor is forbidden ("this failure mode has stalled 5+ units"). The
diagnosis: the rule asked every agent to invent a bounded polling loop from
scratch under time pressure, with nothing in `scripts/` to invoke instead — a
rule without a tool gets skipped. T129 built `scripts/wait_for.sh`, a
dependency-free, re-invokable, bounded poller (default deadline 240s, hard
cap enforced at 540s so a caller can never accidentally exceed the Bash
tool's 600s single-call ceiling) covering the two shapes that recur on this
project: `--exit-file <path>` (the Pi batch `.exit` pattern) and `--file
<path> --contains <text>` (the build/ctest growing-log marker pattern). Exit
0 = condition met, exit 2 = deadline reached and still running (distinct from
broken, so the agent just re-invokes), exit 1 = usage error, exit 3 = target
path unusable (parent directory doesn't exist — a caller mistake, not "still
running"). On every exit it prints elapsed time, file size, and a tail, so
the agent can act without a second command. `EXECUTOR_PROTOCOL.md`'s Hard
Rules bullet now names the tool and gives a copy-pasteable invocation instead
of describing a pattern to reinvent.

**Board: this is scoped explicitly as friction removal, not enforcement.**
Nothing stops an agent from writing its own ad hoc loop again, or from
spawning a background Task/Monitor and waiting on it anyway (as T128 did on
its third stall) — `wait_for.sh` only makes the compliant path cheaper than
the noncompliant one. **If a stall recurs after this unit, the next lever is
mechanical (a hook that can actually block/flag disallowed tool-call shapes),
not a fourth round of protocol rewording.** T129 verified the script
end-to-end (both shapes, success and deadline paths, against real background
processes over `/mnt/projects/fable/review/T129/` on NFS) — see
`refactor/T129_polling_helper.md`'s Review for the transcripts — but did not
and could not test whether it changes agent *behavior* under pressure; that
only shows up empirically in future units.

## T130 (2026-08-17) — TinySoundFont OOB read fixed; the audio callback DOES
allocate (confirmed at runtime); T127's arena caveat measured, not closed

**Closes:** T115's long-open TinySoundFont OOB read side-finding
(`tsf_voice_render`, `tsf.h:1295`-area) — clamped `nextPos` for the
non-looping-final-sample case at all 3 sites, marked as a local vendored
deviation, regression-tested (`tests/test_tsf_oob_read.cpp`, proven to fail
on the unclamped code and pass on the fix via a deterministic mmap-guard-page
+ fork technique, no ASAN required).

**Corrects T128's framing:** miniaudio exposes NO xrun/underrun notification
to the application at all (its types are started/stopped/rerouted/
interruption_began+ended/unlocked) — T128's "never wires miniaudio's own
xrun detection into willeq's logger" implied a wiring omission; there is
nothing to wire. Any underrun/stall signal has to be built from scratch.

**The decisive question (T127 orchestrator-review's own suggested cheap
path — "confirm the audio render path is allocation-free in steady state,
and the concern largely evaporates") did NOT close cheaply.** Static
reading found `MidiPlayer::render`'s MIDI-event pump dispatches
`TML_PROGRAM_CHANGE`/`TML_NOTE_ON`/`TML_PITCH_BEND`/`TML_CONTROL_CHANGE`
events **on the audio callback thread itself**, reaching four TinySoundFont
allocation sites (`tsf_note_on`'s voice-array growth, `tsf_channel_init`'s
malloc+realloc, `tsf_reset`'s free-on-loop-wrap) — rooted in
`tsf_set_max_voices` never being called anywhere in this codebase (so
`maxVoiceNum` stays 0 and TSF always takes the growing-realloc branch
instead of voice-stealing). **Verified at runtime, not just by reading**: a
custom LD_PRELOAD malloc/calloc/realloc/free interposer
(`review/T130/audio_alloc_interposer.c`), run against the real client
connected to live eqemu across a Befallen -> North Qeynos crossing with
audio enabled throughout, logged 76 and 30 real allocation calls
tid-attributed to the audio callback thread across two independent runs —
the realloc-dominant size progression matches `tsf_channel_init`'s growth
pattern exactly. The interposer needed two reentrancy-bug fixes before
producing trustworthy output (both `dlsym`-related; documented inline and
in `review/T130/README.md`'s "what was discarded" section) and was verified
against a synthetic multithreaded allocation stress test before being
trusted against the real client.

**Built the step-3 signal T127's orchestrator review asked for**: a
permanent, lock-free, allocation-free callback-timing probe
(`include/client/audio/audio_callback_probe.h`,
`EQT::Audio::AudioCallbackProbe`) reporting via a new `/pmem` `[Audio
Callback]` line — call count, late-callback count (gap >1.5x the ~23.22ms
expected period at 22050Hz/512-frame periods), worst gap. Measured across
the same crossing: **2 late callbacks out of 5395 total (0.037%) in the
window spanning the crossing, worst gap 44.16ms.** Small and rare, not a
sustained stall, but real and non-zero.

**Board: `M_ARENA_MAX=1`'s audio-callback risk is MEASURED, not closed.**
The callback does allocate and does contend on the single arena lock the
per-crossing `LoadingThread` can hold, but the measured cost (0.037% late,
44ms worst case, one run) is small. Two remedies are available and
untaken, per the plan's explicit instruction not to change the arena count
without Will's sign-off: raise `M_ARENA_MAX` to 2/4, or the deeper T127-named
fix (a persistent `LoadingThread` reused across crossings instead of a
fresh one spawned each time, removing the contention mechanism rather than
bounding it). A cheaper alternative that shrinks the allocation itself
rather than tuning the arena: call `tsf_set_max_voices()` after each
`tsf_load_filename()` in `midi_player.cpp` (never called today) and replace
the loop-wrap `tsf_reset()` with `tsf_note_off_all` (doesn't free
`f->channels`) — worth its own small unit if Will wants this closed
outright rather than measured-and-accepted.

Full write-up (allocation call graph, the interposer's own bugs/fixes, the
late-callback table, every verification step, deviations including a
`git clean -fdx` setup trap that transiently broke 4 unrelated tests via
missing `data/eq_client`/`data/eqe1_client` symlinks):
`refactor/T130_audio_cluster.md`'s Review, `/mnt/projects/fable/review/T130/`.

### T130 orchestrator review — the callback allocation is closeable BY CONSTRUCTION, and upstream says how

T130 established by runtime interposer that the audio callback allocates in
steady state, and root-caused it to `MidiPlayer::render`'s MIDI-event pump
reaching `tsf_note_on`/`tsf_channel_init`/`tsf_reset` on the callback thread,
noting `tsf_set_max_voices` is never called anywhere.

**That root cause is not just an observation — TinySoundFont's own header
documents it as the supported fix**, verbatim (`tsf.h:136-148`):

> "...play simultaneously by calling `tsf_set_max_voices` after loading. **That
> way memory re-allocation will not happen during `tsf_note_on`** and TSF
> should become mostly thread safe."

and, immediately after:

> "Calls to `tsf_channel_set_...` functions may allocate new channels if no
> channel with that number was previously used. **Make sure to create all
> channels at the beginning** as required if you call `tsf_render*` from a
> different thread."

**Upstream states both preconditions for calling `tsf_render*` off-thread, and
this codebase currently satisfies neither.** That is the whole finding.

**Why it matters beyond tidiness — it closes three separate open threads at
once:**

1. **T127's `M_ARENA_MAX=1` caveat.** T130 measured the residual cost (2 late
   callbacks in 5395, 0.037%, worst gap 44.16ms) rather than closing it. A
   callback that does not allocate cannot contend on the arena lock at all, so
   this would be **closed by construction** rather than measured and tolerated.
2. **T114's inventory item #11** — the `AudioMixer`/TinySoundFont race,
   reconfirmed real by T121 and still open. Upstream says these same two calls
   make TSF "mostly thread safe".
3. The 44.16ms worst-case gap itself, which is large enough to be audible.

**Shape of the fix**: call `tsf_set_max_voices()` with a sized-for-worst-case
voice count, and pre-create all MIDI channels, both at `MidiPlayer::init()`
time — i.e. move the allocation from the real-time thread to startup, where
T130 already confirmed allocation is expected and harmless.

**Not done in T130, correctly** — it was outside that unit's ordering and would
have pre-empted the measurement. **Worth its own small unit**, and it should
re-run T130's interposer as its acceptance test: the target is *zero*
allocation calls attributed to the audio thread in steady state, which is a
sharper measure than the late-callback count.

## T131 (2026-08-17) — TSF pre-allocation: audio-thread allocation reaches
ZERO; one open item carried forward

Did the fix the T130 orchestrator-review entry above described: one helper
(`configureTsfForRealtimeCallback`, `midi_player.cpp`) calling
`tsf_set_max_voices(font, MidiPlayer::kMaxVoices)` (256, ~6x headroom over a
measured peak of 43 concurrent voices, ~129KB worst-case total cost) and
pre-creating all 16 MIDI channels, on **every** construction path — the two
`init()` overloads, both SoundFont slots in the dual-SoundFont path, and the
runtime `loadSoundFont()` reload path. Also had to patch `tsf_reset()`
itself (a second local `tsf.h` deviation, banner-commented like T130's OOB
fix) to reset channel state in place instead of freeing `f->channels` — the
pre-allocation alone was NOT sufficient, because `tsf_reset` is called
directly from the audio thread on every loop wrap and would otherwise have
undone the pre-creation on the very next program-change event. This was the
single most consequential finding of the unit; it would not have been
caught by testing `MidiPlayer::init()` alone.

**Result: re-running T130's own interposer over two independent live-eqemu
runs, each spanning a full zone crossing, measured `audio_thread_allocs=0`
in BOTH runs** (against T130's baseline of 76 and 30).
**T127's `M_ARENA_MAX=1` caveat is now discharged by construction, not
measured-and-tolerated** — a callback that never allocates cannot contend
on the single arena lock. **T114's item #11 (`AudioMixer`/TSF race) is
addressed** per upstream's own "mostly thread safe" claim once both
preconditions are met.

Late callbacks did NOT reach zero despite allocations reaching zero (5 of
~10,565 in both runs, ~0.047%, worst ~36.5ms — comparable to T130's
2-of-5395/44.16ms) — informative rather than a failure: whatever causes
these small residual gaps is not (solely) the callback-thread TSF
allocation this unit eliminated. Not investigated further; a separate unit
if Will wants it pursued.

**One item carried forward, not a STOP**: `MidiPlayer::loadSoundFont()` (the
reload path) swaps `impl_->soundFont` — a raw pointer, no atomic/mutex —
and frees the old `tsf*` after the swap. Its sole call site today
(`AudioManager::initialize()`) runs before the audio backend/callback
thread exists, so there is no concurrent renderer to race and this is safe
today. But the method is public API; a future caller that invokes it while
the backend is running (e.g. a hypothetical runtime `/soundfont reload`
command) would race the audio thread and risk a use-after-free on the freed
instance. Documented directly in code above the method's definition so it
can't be added without noticing the warning. Full evidence: this unit's
Review (`refactor/T131_tsf_preallocate.md`) and
`/mnt/projects/fable/review/T131/`.

## T132 (2026-08-17) — CLOSED by T133 — `EverQuest::m_guild_id` is dead: guild commands other
than `/guildmotd`'s get-path have never had a live effect

Found while live-verifying the Guild family's migration to
`action::CommandProcessor` (net-new plumbing over the pre-existing
`EverQuest::SendGuild*()` senders — see `refactor/T132_migrate_guild_family.md`).
`EverQuest::m_guild_id` (`include/client/eq.h:1846`) is the field every
`SendGuild*()` sender in `src/client/eq_guild.cpp` guards on
(`if (m_guild_id == 0) { AddChatSystemMessage("You are not in a guild.");
return; }`), but **nothing in the codebase ever assigns it** — grepped
`src/client/*.cpp`/`include/client/*.h` for `m_guild_id\s*=`; the only
matches are the five `== 0` guard reads and its own `= 0` default. The
zone-in `PlayerProfile` packet does parse a guild id (`eq.cpp:2143`), but
into `self_entity.guild_id` — the per-entity `Entity` struct's field, a
completely different member — never copied into `EverQuest::m_guild_id`.

**Effect, confirmed live** (`/mnt/projects/fable/review/T132/`):
`/guildinvite`, `/guildremove`, `/guilddemote`, `/guildleader`, and
`/guildmotd`'s set-path are permanently dead on the wire — they print "You
are not in a guild." and send no packet, regardless of the character's
actual server-side guild membership. `/guildaccept`/`/guilddecline` guard
on a different field (`m_has_pending_guild_invite`, set only by
`ZoneProcessGuildInvite`), which in practice can also never fire, because
the only way an invite would arrive is a working `/guildinvite` — blocked
by the same dead field. Only `SendGetGuildMOTD()` (unguarded) has a real,
confirmed-live server round trip.

**Not a T132 regression** — `game`'s original handlers (deleted from
`eq_commands.cpp` in T132's commit) called these exact same `Send*()`
methods with the exact same guard; the migration preserved this behavior
identically, as required. This predates T132 and was invisible until this
unit gave these code paths their first live callers to actually test —
same pattern T99 hit with `GameStateSyncBridge`'s missing Group case, T101
found with disconnected spell dispatcher paths, etc.

**Also checked and ruled out**: a GM-status test account (`willeq_test`,
account status 200, above the `Steward` (10) gate `#guild` GM commands
require) IS available and `#guild create` IS reachable — so this is not a
"live leg needs Will" gap. GM-forming a guild would not fix anything: the
blocker is that the *client* never learns it's in a guild no matter what's
true server-side, so no in-session action (GM or otherwise) can make these
six commands produce a network effect. Confirmed cheaply via a throwaway
single-client run before committing session time to a full two-client
guild-formation script that would have been invisible to the client anyway.

**Shape of the fix**: wire `EverQuest::m_guild_id` from the zone-in
`PlayerProfile` parse (`eq.cpp:2143`'s `guild_id` local is already
available at that call site) and keep it current via
`ZoneProcessGuildMOTD`/member-update packets as guild state changes. Real
code change to core connection/zone-in state in `src/client/eq_guild.cpp`
and `eq.cpp` — outside `action::CommandProcessor`/`ActionDispatcher`
territory, the same class of out-of-territory cost T103 flagged for
`GameStateSyncBridge` gaps. Not fixed here; worth its own small unit, with
the same live two-client harness (`data/captures/t132_*` pattern) as its
acceptance test once fixed.

**CLOSED by T133 (2026-08-17)** — see below.

## T133 (2026-08-17) — `m_guild_id` populated (zone-in + mid-session); a
second, independent packet-sizing bug in `eq_guild.cpp` found and fixed in
the same unit

Fixed T132's finding. `EverQuest::m_guild_id` is now assigned in two
places, both confirmed live (`refactor/T133_guild_id_population.md`
Review, `/mnt/projects/fable/review/T133/`):

- **Zone-in**: `ZoneProcessPlayerProfile` (`eq.cpp`) now copies its
  already-decoded `guild_id` local (offset 13036, previously only written
  to `self_entity.guild_id` — a display-only per-entity field) into
  `m_guild_id`. Chosen over `Spawn_Struct`'s `guildID` (offset 238,
  decoded per-entity in `eq_entities.cpp`) because the profile packet
  unconditionally describes the player — no spawn-id matching or ordering
  hazard, unlike the spawn path, which would have needed exact
  `spawn_id == m_my_spawn_id` timing (verified moot for the chosen hook: a
  test — `GuildIdPopulation.OwnPlayerProfile_PopulatesGuildId` — and a live
  zone-in-only session both confirm it works with zero mid-session events).
- **Mid-session** (join/leave/kick): eqemu's `Client::SendGuildSpawnAppearance()`
  sends `OP_SpawnAppearance(type=AppearanceType::GuildID)` to the whole
  zone including self (confirmed from the eqemu server source,
  `zone/guild.cpp`/`zone/client_packet.cpp`, not guessed) on every guild
  invite-accept and `SetGuild()` call. `ZoneProcessSpawnAppearance`
  (`eq_entities.cpp`) had no `AT_GUILD_ID` case before this unit — the
  packet arrived and was silently dropped. Added one case to the existing
  switch (`GUILD_NONE`/`0xFFFFFFFF` normalized to 0 so a guild-leave
  doesn't defeat the `m_guild_id == 0` guards). Live-confirmed twice in the
  same two-client session.

**A second, independent bug found and fixed in the same unit, in the same
file**: populating `m_guild_id` alone was not sufficient — the first
two-client attempt still failed, with the eqemu zone log showing
`Wrong size: OP_GuildInvite, size=138, expected 136` /
`Error: app size of 650 != size of GuildMOTD_Struct of 648`. Every
`SendGuild*()` sender in `eq_guild.cpp` built its own opcode-prefixed
packet and then called `EverQuest::QueuePacket(opcode, &packet)`, which
**also** prepends a 2-byte opcode (`eq.cpp:3850`) — a double opcode prefix
that pushed every size-checked guild struct 2 bytes over what the server
expected. This predates T133 and was invisible before it: every guarded
sender was blocked by `m_guild_id == 0` first (T132's bug), and the one
unguarded sender (`SendGetGuildMOTD`) has no struct for the server to
size-check against. Fixed in the same commit, in territory
(`src/client/eq_guild.cpp`), not a new packet handler.

**Live evidence, contrasted against T132's** (`review/T133/README.md`):
`/guildinvite` now reaches the invitee (`"Testmelee has invited you to
join a guild."`), `/guildaccept` completes a real round trip (server
broadcasts `"Testbuddy has joined the guild."` back to both clients), and
`/guildmotd`'s set-path now works on both a guild-leader-at-zone-in client
AND a client that joined mid-session (proving the `AT_GUILD_ID` fix, not
just the zone-in fix).

**Not verified live this unit** (board item, not folded into "guild works
now"): `guilddecline`, `guildremove`, `guilddemote`, `guildleader` all got
the same double-opcode-prefix fix (mechanically identical to the four
senders that WERE verified) but were not separately exercised in the
two-client session — `guildremove`/`guilddemote`/`guildleader` would have
altered guild membership/rank mid-script in ways that complicate the rest
of the test, and `guilddecline` needs a second, throwaway invite (accepting
the real one was needed for the mid-session `AT_GUILD_ID` proof). No reason
to expect divergence given the identical fix shape, but not directly
observed — a small follow-up could close this cheaply with a second
two-client script exercising just these four.

## T134 (2026-08-18) — Corpse + Resurrection families migrated; two
independent, pre-existing wire bugs found and fixed in the same unit

Net-new plumbing over the pre-existing `EverQuest::SendCorpseDrag()`/
`SendCorpseDrop()`/`SendConsent()`/`SendConsentDeny()`/`SendRezzAnswer(bool)`
senders (`eq_player_info.cpp`), same shape as Guild (T132). All six —
`corpsedrag`, `corpsedrop`, `consent`, `deny`, `rezaccept`, `rezdecline` —
verified **end-to-end on the wire** in a two-client live session, unlike
T132's six-of-seven dispatch-only result. Full detail:
`refactor/T134_migrate_corpse_rez.md` Review, `/mnt/projects/fable/review/T134/`.

**Two independent, pre-existing bugs found and fixed, same class as T133's
finding in `eq_guild.cpp`:**

1. **`SendRezzAnswer`'s double opcode prefix** — built its own
   opcode-prefixed packet and then called `EverQuest::QueuePacket(opcode,
   &packet)`, which also prepends an opcode. Fixed by building a body-only
   packet before calling `QueuePacket()` once
   (`src/client/eq_player_info.cpp`). Note: `corpsedrag`/`corpsedrop`/
   `consent`/`deny`'s senders were NOT affected — they call
   `m_zone_connection->QueuePacket(packet)` directly, a different, correct
   convention (packet already carries its own opcode, no second prefix
   added by that path).
2. **`Resurrect_Struct` missing a 4-byte field** (`include/common/
   packet_structs.h`) — a `unknown020` field present in eqemu's own struct
   right after `z` (offset 20) was absent from ours, shifting every field
   after `z` by 4 bytes on both read and write. Before the fix, every
   `OP_RezzRequest` parsed with an empty `rezzer_name`, a garbled
   `corpse_name`, and `spellid=0`; every `OP_RezzAnswer`'s `action` field
   landed at the wrong offset, so the server could not reliably distinguish
   accept from decline. Confirmed byte-for-byte against a captured server
   packet's hex dump in the eqemu zone log (`review/T134/
   eqemu_zone_arena_server_log.txt`) — `your_name` starts at wire offset 24,
   not the 20 our struct assumed. Fixed by adding the missing field;
   `sizeof(Resurrect_Struct)` is now 228, was 224. Every use in the
   codebase goes through `sizeof()`, so nothing else needed to change.

Neither bug is a T134 regression — both predate this unit and were
invisible before it because no `action::` registration had previously
exercised these senders' full round trip. **This bug class (a sender
building its own opcode prefix, then double-prefixing via `QueuePacket`)
is worth a deliberate sweep** across the remaining un-migrated families'
senders (Merchant, Utility) rather than waiting to find each instance one
family at a time — T132/T133 found it in Guild, T134 found it once more in
Resurrection; two occurrences in two units is enough of a pattern to check
proactively next time rather than reactively.

**How the pending resurrection was produced** (board item, not obvious):
a single GM account (`willeq_test`, status 200) is sufficient — no second
GM character, no Will action. `#kill` on your own target self-kills, but
this eqemu config auto-teleports the dead character to its bind zone
almost immediately, away from its own corpse — resolved with the GM
`#zone <zone> <x> <y> <z>` command to teleport back, then `/target` +
GM `#castspell 994` ("Customer Service Resurrect", found via
`effectid*=81`/`SpellEffect::Revive` in `spells_new`) on the corpse.
`Corpse::CastRezz` (eqemu `zone/corpse.cpp`) is target-and-caster-agnostic,
so self-casting works identically to a second character casting.

## T135 (2026-08-18) — Utility family migrated (split/save/bandage/rewind/
bug/petition); one wire bug found and fixed, a second found and deliberately
left open

Net-new plumbing over the pre-existing `EverQuest::SendSplit()`/`SendSave()`/
`SendBindWound()`/`SendRewind()`/`SendReport()`/`SendPetition()` senders,
same shape as Guild (T132) and Corpse/Rez (T134) — these six do not share a
scenario (unlike Guild or Corpse/Rez) but share a unit. `bandage` reuses
T102's `CombatManager` targeting via `EqActionHandler::getCombatManager()`
(same helper `assist`/`shieldTarget` use), with self-target fallback instead
of an error message, matching `game`'s original `bindwound_cmd` exactly. Full
detail: `refactor/T135_migrate_utility_family.md` Review,
`/mnt/projects/fable/review/T135/`.

**THE TRAP, checked live and confirmed NOT present**: `grep -i 'wrong size'`
across the whole session's eqemu zone log returns zero matches — `SendSave`/
`SendRewind`/`SendReport`/`SendPetition`'s `PutUInt16(0, HC_OP_...)` really is
the correct connection-level convention, untouched, as the plan predicted
from static analysis.

**One wire bug found and fixed:**

1. **`SendPetition`'s missing NUL terminator** (`src/client/eq.cpp`) —
   eqemu's `Handle_OP_Petition` (`zone/client_packet.cpp`) reads the petition
   text via `pet->SetPetitionText((char*)app->pBuffer)`, a strlen-based
   C-string read. `EQ::Net::Packet::PutString()` (unlike `PutCString()`,
   `src/common/net/packet.cpp`) writes no terminator and leaves the packet
   exactly `str.length()` bytes long, so the server's read ran past our
   buffer into adjacent heap memory. Confirmed live byte-for-byte: a
   submitted `"stuck in geometry"` petition persisted to `peq.petitions` as
   `"stuck in geometrye.lua"`. Fixed by switching to `PutCString()`;
   re-verified live with the exact text persisting cleanly. See
   `review/T135/petition_db_evidence.txt`.

**One wire bug found, left open — needs a design decision, not a mechanical
fix:**

2. **`SendReport`'s wire format doesn't match eqemu's `OP_Report` protocol at
   all.** `Handle_OP_Report` (`zone/client_packet.cpp`) parses a
   pipe-delimited `<reported>|<reporter>|<message>\0` string; our sender
   sends a 4-byte `category_id` (always 0) then raw text — a different
   layout entirely, not a small mechanical bug. Walking eqemu's byte-by-byte
   parser against our actual output shows `database.AddReport()` is never
   reached (`mode` never advances past 0 because our leading zero bytes and
   text contain no `|`). Confirmed empty `peq.bug_reports`/`peq.bugs` before
   and after. **Why not fixed in T135**: `/bug`'s existing single free-text
   argument has no concept of a "reported" player name the real protocol
   wants — a correct fix needs either a new command shape (e.g.
   `/bug <player> <description>`) or a policy choice (e.g. reported=self),
   which is a design decision the plan didn't ask for, not a mechanical
   wire-format correction like the petition fix above. Migration preserves
   `game`'s identical (already-broken) behavior, matching the programme's
   "behaviour must be identical" bar. **Next unit that touches `/bug` should
   design the wire format properly rather than patch around it.**

**Test-data cleanup gotcha worth remembering**: deleting a test petition row
directly via SQL desyncs eqemu's in-memory `PetitionList::Instance()` from
the DB (the server doesn't reread it), causing a legitimate subsequent
`/petition` from the same character to be wrongly rejected with "You already
have a petition in the queue." Use the client's own petition-delete path
(`SendPetitionDelete`, not currently exposed as a slash command) or a fresh
character instead of a raw DB delete when a live leg needs to retry a
petition submission.

**`tests/test_eq_commands_assist.cpp` deleted** (not repointed) — T102 had
already trimmed it to a single remaining case (`bandage`'s self-target
fallback on the `game`-registry `ProcessChatInput()` seam); migrating
`bandage` out of `eq_commands.cpp` in T135 removed that seam too, leaving
zero tests. Removed following T102's `test_eq_commands_attack.cpp` precedent
(documented in `tests/CMakeLists.txt`) rather than leaving a zero-test
executable target. Equivalent coverage now lives in
`test_command_processor.cpp`'s T135 section.

**`check_file_sizes.sh` FAIL, pre-existing, not caused by T135**:
`src/client/eq.cpp` was already 3898 lines at `fable` tip (before T135
touched it), past the 3000-line FAIL threshold — the `SendPetition` fix
above (in `eq.cpp`, where the sender already lived) added 5 net lines,
nudging an already-failing file marginally further. Not attempted to fix by
splitting `eq.cpp` in this unit (out of scope for a command-migration
unit); flagged per the protocol's "no new FAILs" bar rather than silently
absorbed.

**Board**: with Utility done, only the Merchant family (`vendor`,
`vendorlist`, `buy`, `sell`, `equip`) remains registered in
`eq_commands.cpp`. Nothing in T135 contradicts T134's read that Merchant is
the family most likely to need T103's `GameStateSyncBridge`/state-domain
work. One new data point for that unit's live leg: this unit's petition bug
was found by checking **persisted server-side data** (a DB table), not the
zone console log or a packet-size complaint — a correctly-sized packet with
wrong field semantics is a distinct failure mode from a malformed packet
(T133/T134's findings), and neither showed up as a zone-log error. Merchant's
live leg should budget a DB-level check (actual inventory/currency changes)
for `buy`/`sell`, not just packet-size correctness.

## T136 (2026-08-18): `/equip` migration — genuinely deferred, not dropped — **CLOSED by T141 (`e45c7f99` + `b71c885b`, merged `fe290cb0`) — annotated T215**

`T136_migrate_merchant_family.md` scoped five commands
(`vendor`/`vendorlist`/`buy`/`sell`/`equip`) but migrated only four.
`equip` was deliberately excluded: it already carries category
`"Inventory"` (not `"Commerce"`) in `eq_commands.cpp`'s own registration,
its mechanism is `InventoryManager` (not the `Vendor*Intent` family the
other four share), and its handler is ~150 lines covering three input
forms (`fromSlot toSlot` / item-name resolution / `list`), cursor-vs-slot
staging (T71), and pre-flight legality checking (T77). A future unit
migrating it needs to answer its own design questions (does the three-form
dispatch move to `CommandProcessor`? does `EqActionHandler` need
`InventoryManager` reachability the way it has `getCombatManager()` for
`CombatManager`? does the pre-flight legality check stay in `EverQuest` or
move up?) rather than be bent to fit the Merchant family's shape. `equip`
is untouched — still registered in `EverQuest::m_command_registry`, still
covered by `tests/test_eq_commands_equip.cpp`.

## T140 (2026-08-18): `/equip` migration — third confirmation, still deferred — **CLOSED by T141 (`b71c885b`) — annotated T215**

T140 (the "final seventeen" migration unit) named `equip` as its 17th row,
sequenced last with an explicit instruction to STOP the moment it needed
more than a thin accessor rather than re-litigate T136's finding under
batch pressure. Confirmed the same three reasons hold, unchanged: `equip`
still carries category `"Inventory"`, its mechanism is still
`InventoryManager` (no `EqActionHandler` reachability the way
`getCombatManager()` gives `CombatManager`), and the handler is still
~150 lines across three input forms plus cursor-vs-slot staging and
pre-flight legality checking. No new investigation was needed beyond
re-reading the T136 finding — this is the second unit (after T136 itself)
to reach the STOP rather than attempt the migration, so it is now a
two-for-two pattern, not a one-off scoping call. `equip` remains untouched;
this unit's other 16 rows (all of T138's remaining `migrate-action` rows
except `equip`) migrated cleanly. A future unit taking this on should
budget it as its own design pass from the start, not a leftover to squeeze
into a batch.

## T136 (2026-08-18): Merchant vendor-state-domain question — resolved, not deferred

T103 (Trade domain) and T134's Board both flagged Merchant as the family
most likely to need a `GameStateSyncBridge`-fed vendor state domain the way
Trade got one. **T136 investigated this directly and found it NOT
warranted**: the actual state gap was two reads (`m_vendor_items`,
`m_vendor_name`) serving exactly one display command (`/vendorlist`), not a
cross-cutting need multiple consumers share. Confirmed the renderer does
NOT duplicate this state (it reads through the same `m_vendor_items`/event
path), and the two new `EverQuest` accessors added
(`GetVendorItems()`/`GetVendorName()`) return `const&` — no mutable handle
leaks. Resolved by adding the two accessors directly rather than building a
domain. Recording this as CLOSED (not open) in case merchant state grows
later and someone re-raises the question — the reasoning here is why it
wasn't needed *at this scope*, not a blanket "never build one."

## T136 (2026-08-18): `EqActionHandler` cannot push bridge intents — a real, load-bearing architectural gap — **CLOSED as a class by T138 (`13799cb7`, finding 4: the per-command `EverQuest::Request*` wrapper recipe absorbs it) — annotated T215**

`action::EqActionHandler` (the `IActionHandler` implementation wrapping
`EverQuest`) has **no access to `EverQuest::m_bridge`** (private) and is
constructed with only an `EverQuest&` (`EqActionHandler(EverQuest& eq)`,
`application.cpp:283`, no `GameStateBridge*` parameter). Every existing
`EqActionHandler` method that needs to act calls an `EverQuest::Send*()`
sender or reads `EverQuest`/`CombatManager` state — **none of them push a
bridge intent directly**, because none of them have ever needed to before.
T136's `vendor`/`buy`/`sell` needed exactly that (they push
`eqt::events::Vendor*Intent`, not a packet), and worked around it by adding
three new `EverQuest` methods (`RequestVendorToggle`/`RequestVendorBuy`/
`RequestVendorSell`) that do the guard-then-push on `EverQuest`'s own side,
so `EqActionHandler` still only ever calls into `EverQuest`. This is a
reasonable per-unit workaround but is genuinely a gap: **any future
action-layer migration whose underlying mechanism is a bridge intent
(rather than a `Send*()` packet) will hit this same wall** and either repeat
the same "add an `EverQuest` wrapper" workaround or need
`EqActionHandler`'s construction wiring widened to also carry a
`GameStateBridge*` (a real, if modest, `application.cpp` change, out of
T136's territory). Flagged here rather than fixed, since T136's territory
list explicitly restricted `eq.h` changes to "accessors only" and widening
`EqActionHandler`'s constructor is a bigger, different-shaped change than a
command migration unit should make unprompted.
## T137 (2026-08-18) — ARM texture-probe infra: build-and-parse now works on
both architectures; end-to-end Pi capture still open, and one build-cache
bug found that would have quietly broken the "off by default" guarantee

Closes T125's two infrastructure gaps (ARM build never threaded
`WILLEQ_T122_TEXPROBE`; `t122_extract_ring.py` was 64-bit-only) without
touching eqemu or the Pi, per the plan. Full detail:
`refactor/T137_arm_probe_infra.md` Review, `/mnt/projects/fable/review/T137/`.

**ARM flag plumbing**: `docker/Dockerfile.arm-noble` gets `ARG
ENABLE_T122_TEXPROBE=OFF`; `scripts/build-arm-noble.sh` gets a
`--t122-texprobe` flag mirroring the x86 script. `nm` confirms the resulting
binary has zero probe symbols by default and the full expected symbol set
with the flag — but getting to that clean result required fixing two real
bugs, not just adding plumbing:

1. **ARM-only compile break** in `docker/irrlicht-drm/t122_texprobe.h`:
   `reinterpret_cast<uintptr_t>(pthread_self())` is ill-formed on armhf
   (`pthread_t` and `uintptr_t` are different non-pointer integral types
   there; on x86_64 they're the identical typedef, which is why this never
   broke before). Fixed to `static_cast`, no behavior change.
2. **Stale persistent Docker build cache**: `build-arm-noble.sh`'s
   incremental build cache did not reliably relink `willeq` when only the
   probe flag toggled, so a "plain" rebuild silently reused a
   probe-tainted binary from a prior probe-ON run — directly undermining
   the "off by default" contract T122's design depends on. Fixed with a
   marker-file cache-invalidation guard (wipe the cache when the flag's
   value differs from the last recorded run); verified the fix by toggling
   OFF→ON→OFF three times post-fix and confirming correct `nm` output and
   a genuine full recompile at each toggle.

**Extractor now width-generic**: `scripts/t122_extract_ring.py` detects
32-bit vs. 64-bit from the target binary's own ELF `EI_CLASS` byte (no
flag — a mis-parsed ring is plausible garbage, the worst failure mode for
a diagnostic). The 32-bit `Entry` layout (52 bytes, `"<I8IIIIB3x"`) was
derived by hand, cross-checked via `struct.calcsize`, and backed by a new
width-generic `static_assert(sizeof(Entry) == (sizeof(void*)==8?88:52))`
added directly in the header (a real compiler check on both the x86_64 and
ARM builds, not just a script-side assumption). Both widths'
parse-from-raw-bytes logic are regression-tested against a synthesised
ring (`scripts/tests/test_t122_extract_ring.py`, 29/29 checks, standalone
— not wired into ctest, same rationale as `t122_extract_ring.py`'s own
existing precedent).

**What's still open, and why**: two legs the plan explicitly scoped out or
that turned out to be evidence-preservation gaps, not implementation gaps:

- **End-to-end Pi capture** — needs eqemu (T136 held the lock) and real
  hardware, out of scope for this unit by design. **The next unit that
  takes the eqemu lock and touches the Pi should run this as its first
  step**: stage a `--t122-texprobe` ARM binary, reproduce a crash, extract
  the resulting core. That is the one thing that would make "usable on
  ARM" end-to-end rather than build-and-parse-only.
- **Re-parsing the exact T122/T126 checked-in cores** — attempted, could
  not be completed: the exact probe binary that produced those cores was
  never checked in (only the cores and their already-extracted TSVs were),
  and the worktree that built it no longer contains it. Diagnosed with a
  PIE load-bias reconstruction from the core's own `AT_PHDR` auxv entry
  (confirmed the bias-computation mechanism is sound — it matches `info
  proc mappings` exactly) — the mismatch is a genuinely different static
  layout between this checkout's current source and whatever the T122
  session was built from, not a defect in the extractor. **Lesson for
  future probe-evidence units**: check in the matching binary alongside
  any core meant for later re-verification, or the evidence rots as soon
  as the worktree that produced it gets reused.
- **A real ARM core, extracted** — no Pi run happened, so the 32-bit
  extraction path (unlike the 64-bit path above) has never been tried
  against real data, only synthesised. The layout is compiler-verified and
  the parse logic is regression-tested, but whether gdb cleanly resolves
  an armhf core's symbols the way it does for x86_64 is untested — flagged
  explicitly since that's a plausible place for a new failure mode (e.g.
  needing a target-aware/multiarch gdb) to show up first.

**Verification**: x86 suite 2351/2352 (sole failure the documented ALSA
quirk, matching baseline); plain `bash scripts/build-gles2-x86.sh` confirmed
probe-free (`nm` 0 matches); ARM cross-compile confirmed clean/tainted
correctly in both configurations (table + BuildIDs in the Review);
`check_file_sizes.sh` zero new FAILs (11 of 12 flagged files are T136's
in-flight work, inherited from this branch's root commit, not this unit's
changes — only `t122_texprobe.h` is actually touched, and it's `ok`).

## T138 (2026-08-18): The 61-command triage — 34 deletions, 17 migrations, 7 stay, 3 decisions for Will; the GameStateBridge* gap does NOT block a whole class after all

Full evidence: `review/T138/triage_table.tsv` + `review/T138/README.md`,
narrative in `refactor/T138_command_registry_triage.md`'s Review/Board. Three
findings worth surfacing here specifically, since a future unit acting on
the table shouldn't have to re-derive them:

1. **32 of the 34 `delete` rows are redundant duplicates, not dead code** —
   each pushes `RendererCommand{"/name"}` onto the bridge from the `game`
   registry, and `IrrlichtRenderer::processSlashCommand()`
   (`irrlicht_renderer.cpp:1031-1370`) **already has a native, live branch
   for that exact name**. `Application::dispatchToActionOrRenderer`'s
   registry-3 fallback (`application.cpp:1150-1174`) reaches that branch
   automatically the moment the `game`-registry copy is deleted — nothing
   needs to be built or moved, only removed. This falsifies the plan's own
   "~38 renderer commands need `move-renderer`" hypothesis: the
   recommendation fires **zero times** in the finished table.

2. **`skills` is a wired-but-unconsumed event (T132's `m_guild_id` shape)
   AND the real feature exists under an unregistered alias.**
   `eq_commands_spells.cpp:136-150` pushes `GameEvent(ToggleSkillsWindow,
   {})`; its only consumer, `irrlicht_bridge.cpp:724-726`, is a single
   `LOG_TRACE` with no state change. The actual working skills-window toggle
   is `/newskills` (`irrlicht_renderer.cpp:1237-1243`, flips
   `skillsPopupState_.isOpen` for real) — registered in NEITHER the `game`
   nor `action` registry, so it's reachable today only by a user who already
   knows to type it. Deleting dead `/skills` loses nothing; giving
   `/newskills` a real registration (in whichever registry ends up owning
   UI-popup toggles) is a separate, still-open discoverability gap.

3. **Correction to T97's Review**: T97's `divergence_table.tsv` called
   `game`'s (then-)live `/filter` the better side of a comparison because
   "live pushes a real renderer-side toggle." T138 found that toggle is
   itself a dead stub (`irrlicht_renderer.cpp:1261-1262`, unconditional
   "Filter command not available (WindowManager removed)") — `/filter` was
   non-functional on BOTH sides of that comparison, not just the one T97
   called dead. Not a defect either unit introduced; flagging so the record
   is accurate if `/filter` is ever revisited.

4. **The `GameStateBridge*` gap T136 flagged does not block a whole class of
   migrations, contrary to what T136's phrasing might suggest to a future
   reader.** Traced all 17 of this unit's `migrate-action` rows
   individually: 12 call an `EverQuest` method that's already public (no new
   plumbing at all), 3 (`warp`/`time`/`bank`) need exactly one new thin
   `EverQuest::Request*`-style wrapper each — same shape/size as T136's
   three vendor wrappers, not a new capability — and 2 (`debug`/`perf`)
   match an existing action-registry diagnostic precedent
   (`logmodule`/`logexclude`/`logclear`/`logonly`). The only row that's a
   genuine prerequisite-sized migration is `equip`, and that's the same
   `InventoryManager`-reachability gap T136 already identified and deferred
   — not a new instance. **Net: the per-command wrapper-method recipe
   (T99-T136) already fully absorbs this gap; no standalone
   "give `EqActionHandler` a `GameStateBridge*`" unit is needed.**

## T139 — headless `#ifdef EQT_HAS_GRAPHICS` guard not preserved (out of territory)

Deleting the 34 redundant game-registry commands (see
`refactor/T139_delete_duplicate_commands.md`) means their names now reach
`Application::dispatchToActionOrRenderer`'s registry-3 fallback
(`application.cpp`) instead of the deleted `rendererCmd()` helper.
`rendererCmd()` was `#ifdef EQT_HAS_GRAPHICS`-guarded: in a headless build it
replied `"/x requires graphics mode (not available in this headless build)."`
instead of queuing. Registry-3's fallback has no such guard — `m_bridge` is
always non-null (attached to `ConsoleBridge` in headless builds instead of
`IrrlichtBridge`, per `application.cpp`'s own comment) — so a headless build
now reports `outcome=queued` for these 34 names (and, structurally, for
**any** unrecognized name, not just these 34) with no acknowledgment that
nothing is listening. This is a small, real regression in headless
ergonomics for these 34 specific names (they used to say so explicitly;
now they silently join every other unclaimed name in reporting `queued`).

Not fixed by T139: the only fix site is `application.cpp`, which was outside
T139's declared territory (`eq_commands_movement.cpp`,
`eq_commands_spells.cpp`, `irrlicht_renderer.cpp` alias handling only,
`tests/`, `refactor/`, `review/T139/`), and a proper fix there would guard
registry-3's *entire* fallback path headless (affecting every name that
reaches it, not just these 34) — a bigger and more deliberate change than
"restore what these 34 used to do," worth its own small unit with its own
review rather than a scope-creep add to T139.

## T139 — `timestamp`/`filter` dead stubs kept, not removed

T138 confirmed `timestamp` and `filter` are dead on both the game-registry
and renderer sides (`irrlicht_renderer.cpp`'s stubs unconditionally reply
"not available (WindowManager removed)"). T139 deleted the game-registry
copies (per the `delete` disposition) but deliberately left the renderer-side
stubs in place rather than removing them too. Reasoning (see the plan's
Review, "`timestamp`/`filter` (dead on both ends)" section, for the full
argument): registry-3's fallback reports `outcome=queued` for *any*
unclaimed name regardless of what (if anything) is behind it — that
mislabeling is structural to the catch-all in `application.cpp`, not
something specific to these two names, and not fixable by touching the
renderer stub. Removing the stub would only delete the one accurate thing it
still does (naming the feature as unavailable) without fixing the `queued`
lie. If `application.cpp`'s catch-all is ever revisited (see the headless
entry above — same file, same territory boundary), whether to special-case
confirmed-dead names there too would be worth deciding at the same time.

## T141 — general-slot client/server numbering gap (live-leg finding, not fixed)

Live-tested `/equip 13 22` (Primary -> general slot 22, the client's own
`GENERAL_BEGIN=22` numbering, `inventory_constants.h`). The server rejected
it ("The selected slot was invalid."), and the client's pre-existing
desync-recovery logic correctly resynced local state back to the server's
truth (unaffected by this unit -- that recovery path predates T141 and was
not touched). DB inspection (`review/T141/db_state.txt`) shows every
pre-existing general-slot item on the test character sits at `slot_id`
23-30, never 22 -- i.e. this eqemu server's real general-slot numbering is
offset by +1 from what the client sends. `inventory_constants.h` already
documents an analogous, acknowledged offset for the cursor slot
(`SERVER_CURSOR_SLOT = 33` vs the client's `CURSOR_SLOT = 30`, a +3 gap) but
`SendMoveItem`/`MoveItemIntent` apparently never applies an equivalent
translation for the general range.

Not a T141 regression: this unit did not touch `SendMoveItem`, the wire
opcode, or any slot-numbering translation -- `/equip`'s pre-extraction
handler would have hit the identical rejection sending the identical
packet; slot 22 specifically had just never been live-tested before (T63/
T71/T75's live sessions used equipment <-> cursor <-> occupied-general
pairs, never an EMPTY general slot at the client's low end of the range).
Re-tested with the known-good `13 <-> 30` (Primary <-> cursor) pair, proven
live by T71, which worked both directions -- see `review/T141/README.md`.

Not fixed here: out of this unit's declared territory (no wire-protocol/
opcode files in T141's territory list), and the right fix -- either a
general-slot translation table alongside the existing cursor one, or
confirming this eqemu server's config is itself nonstandard -- deserves its
own small investigation rather than a guess bolted onto this unit's diff.

## T142 — headless regression widened by 2 more names (not fixed, same root cause as T139's)

`spellreload`/`reloadspells`/`reloadspellfx` and `weatherquality`/`wq`/
`weatherpreset` are now routed through `IrrlichtRenderer::processSlashCommand()`
via registry-3's fallback, same as T139's 34. Both handlers used to have an
`#else` branch (`#ifdef EQT_HAS_GRAPHICS`) printing "requires graphics mode
(not available in this headless build)" when the game registry owned them.
Registry-3 has no such guard, so a **headless** build now reports
`outcome=queued` for all six names instead — the identical regression T139
already recorded for its 34 names (see the entry above, "headless"). Not
fixed here for the same reason: the fix site is `Application::
dispatchToActionOrRenderer` (`application.cpp`), outside this unit's
territory (`eq_commands_movement.cpp`, `irrlicht_renderer.cpp`, `tests/`,
`refactor/`, `review/T142/`). If `application.cpp`'s catch-all is ever
revisited, this brings the running total needing that fix to 36 names.

## T142 — `chat()`'s console echo is lost for these two commands (observed, not fixed)

`EverQuest::AddChatSystemMessage()` (`eq_chat.cpp:199-212`, the game-thread
helper the deleted handlers used to call) does two things: pushes a
`SystemMessage` event to the bridge AND calls `LOG_INFO(MOD_MAIN, "{}",
text)` directly to console. `IrrlichtRenderer::processSlashCommand()`'s
`chat()` helper (`irrlicht_renderer.cpp:1043`, the replacement path) does
only the first — it has no console echo. Live A/B evidence
(`review/T142/pre_change_full.log` vs `post_change_full.log`) shows this
concretely: pre-change, `/spellreload` logged `[MAIN] Spell effect settings
reloaded` and `/weatherquality` (query form) logged `[MAIN] Weather quality:
low`; post-change, neither line appears — only the underlying `[GRAPHICS]`
effect lines do. This is **not a new asymmetry introduced by this unit** —
already-bridge-routed `/reloadeffects`/`/reloadfx` (T139) show the identical
gap (`review/T139/post_deletion_full.log`: no `[MAIN]` echo of "Environment
effects settings reloaded" either). The UI chat window still receives the
message (per the plan's design), so nothing user-visible is lost — only a
console-log convenience for anyone grepping `[MAIN]` output, which was
already inconsistent across renderer- vs game-registry-owned commands before
this unit. Not fixed here: fixing it would mean adding a console echo to
every `chat()` call site in `processSlashCommand()` (dozens of call sites,
well outside this unit's diff), or accepting the asymmetry as intrinsic to
which thread produced the message.

## T143 (2026-08-18) — `/help` is now registry-1 + registry-2 aware (114 of 114 reachable commands, deduplicated); registry 3 (renderer) is STILL invisible to help — a queryable catalog there is a follow-up, not started here

`/help` (`include/client/help/merged_help.h`,
`src/client/help/merged_help.cpp`) now merges `EverQuest::
m_command_registry` (registry 1) and `action::CommandProcessor` (registry 2)
into one categories overview, one category-detail view (new — neither side
supported querying by category name before this unit), and one
command-detail view, deduplicated (a name/alias present in both is shown
once, resolved by registry 1 since that is the one actually reached at
runtime — see `Application::dispatchToActionOrRenderer`'s dispatch order).
Lives in `Application::dispatchToActionOrRenderer`, the one place holding
pointers to both catalogs (added `EverQuest::GetCommandRegistry()` as a
read-only accessor for this); `/help`'s own registration was DELETED from
registry 1 (`eq_commands_movement.cpp`) so it falls through to the
unrecognized-command fallback that already reaches this method for every
other action/renderer command. Live-measured: 11 -> 114 distinct commands,
zero duplicates in the no-args overview (`review/T143/README.md`).

**Still open**: registry 3 (`IrrlichtRenderer::processSlashCommand()`'s
~36-command `cmd == "/x"` if/else chain, reached via the renderer command
bridge — the T139/T142 deletions' destination) has no catalog to query at
all, so it is **excluded from this merge, not merely deprioritized** —
`Application::printCommandResult()`'s own comment already says so ("the
renderer-side registry has no query interface reachable from the game
thread"), and this unit does not change that. Those ~36 commands remain
permanently invisible to `/help` (and to `getCompletions()`/tab-complete,
which has the identical two-registry blindness `/help` had before this
unit — not touched here, out of scope) until the renderer gets a queryable
command table.

**Follow-up estimate** (giving the renderer a real catalog, registry-3):
this is real, separate work, not a copy of this unit's mechanism — the
renderer's ~36 commands are plain `if (cmd == "/x") { ... }` branches with
no `Command`/`CommandInfo`-shaped metadata (name/aliases/usage/
description/category) attached anywhere, and `processSlashCommand()` runs
on the RENDER thread (P10's GL-context-affinity trap; this unit's own
`/help` deliberately stays on the game thread and only ever pushes a
fire-and-forget bridge event toward registry 3, never queries it). Building
a real catalog means: (1) retrofitting each of the ~36 branches with
metadata — either a parallel `std::vector<CommandInfo>` hand-maintained
alongside the if/else chain, or restructuring the chain itself into a
registered-command-table dispatch (the bigger, more correct option); (2)
exposing that table across the thread boundary for the game thread to read
it (a new query-intent + response path over the existing bridge, or a
read-only snapshot published each frame — GameStateBridge has no precedent
for a synchronous cross-thread query today, only fire-and-forget
events/intents, so this is new bridge shape, not a reuse of an existing
one); (3) wiring `/help`'s merge to a third source. Rough size: **1
medium-to-large unit** (~1 day) for the retrofit-in-place option (adding
metadata beside the existing chain, no behavior change to the branches
themselves) plus the new cross-thread query path; **2-3 units** if the
if/else chain itself is restructured into a real registry first (arguably
the more durable fix, since a hand-maintained parallel metadata list will
drift the same way registries 1/2 already nearly did — T97's whole
finding). Not started here — out of this unit's scope per the plan's
explicit scope boundary.

## T146 (2026-08-19) — thirteen zero-caller senders: the entire GM opcode family, `SendFaceChange`, `SendControlBoat`, `SendPopupResponse`, and the missing `OP_InspectAnswer` reply. **No live session can reach any of them; a caller must be written first.**

T146 was dispatched to close T145's nine struct/sender fixes at the wire.
Four of its six named legs turned out to be **unexercisable, not merely
unexercised**: the senders they depend on have **zero production callers**,
so no script, keypress, or command can reach them. Verified by exhaustive
grep across `src/`, `include/`, `tests/`, `tools/` — each name appears only
at its declaration in `include/client/eq.h` and its definition.

**A. The whole `src/client/eq_gm.cpp` family — 9 senders, 0 callers.**

| sender | eq_gm.cpp | callers |
|---|---|---|
| `SendGMZoneRequest` | :13 | 0 |
| `SendGMSummon` | :30 | 0 |
| `SendGMGoto` | :47 | 0 |
| `SendGMFind` | :64 | 0 |
| `SendGMKick` | :89 | 0 |
| `SendGMKill` | :106 | 0 |
| `SendGMHideMe` | :123 | 0 |
| `SendGMEmoteZone` | :130 | 0 |
| `SendGMLastName` | :142 | 0 |

There is **no `/kick`, `/kill`, `/summon`, `/goto` or any other GM command
in either command registry** (`grep '"kick"'` / `'"kill"'` over
`command_processor.cpp` and `eq_commands*.cpp` returns nothing but
`guildremove`'s `gkick` alias and a combat-message string). T145's
`GMKick_Struct` 132→129 fix is therefore correct-and-unreachable: the fix
is right, and nothing can send it. **The unit that wires GM commands is the
unit that can first claim `/kick` or `/kill` works.**

**B. `SendFaceChange` (`eq_player_info.cpp:1842`) — 0 callers.** The
`/face` command that *does* exist (`command_processor.cpp:324`) is the
**movement** face — "Face entity or location", routing to
`EqActionHandler::setHeading()`. It has nothing to do with
`FaceChange_Struct` or `OP_FaceChange`. The T146 plan's `/face` leg
conflated the two. Appearance change (hair/beard/eye/face values) has no
command, no UI trigger, and no caller of any kind.

**C. `SendControlBoat` (`eq_player_info.cpp:1406`) and
`SendPopupResponse` (`:1662`) — 0 callers**, as T145 already recorded
(reachability pattern #10). Confirmed independently here. **Structurally
fixed by T145, unexercised, and unexercisable without a caller.**

**D. `OP_InspectAnswer` has no sender at all.** `ZoneProcessInspectAnswer`
(`eq_player_info.cpp:1179`) parses a response this client never causes to
exist: the EQ inspect flow is request → server relays to the target's
client → **the target's client replies `OP_InspectAnswer`**, and we have no
such reply path (`grep InspectAnswer src/client/*.cpp` finds only the
opcode name, the dispatch case, and the parser). T146 proved the *request*
half round-trips on the wire (the second client logged "Testmelee is
inspecting you"), but between two WillEQ clients the response window can
**never** populate. Half a feature.

**Standing instruction, per the T146 plan: do NOT write callers merely to
make a leg verifiable.** Wiring any of these is real feature work with its
own acceptance bar, and each one should be scoped as such.

**This is the reachability pattern at #11 through #13** (A, B, and D are
each new; C was #10). The pattern's count is now dominated by whole
families rather than single functions.
---

## T147 (2026-08-19) — packet structs that only a live capture can settle

`refactor/T147_packet_structs_trap_removal.md` deleted 13 structs from
`include/common/packet_structs.h` that were both dead in production and
provably wrong against eqemu's source. The five items below are **not** in
that set: each is one where eqemu's own source is ambiguous or
self-contradictory, so asserting a value would be a guess dressed as a fact.
They are recorded here rather than papered over. **T147 held no eqemu lock
(T146 did), so nothing below was observed on the wire.**

### CLOSED 2026-08-19 by T146's capture — `Death_Struct` field order

> **RESOLVED. Do not re-open, and do not take the capture again.** T147 wrote
> this as OPEN because T146's `OP_Death` capture had not landed when it
> finished; the two units ran in parallel. It has since landed and it
> **confirms our order**: wire offset 12 read **189**, matching the server's
> own `character_bind` row for Testbuddy (bind zone 189, `tutorialb`) and the
> zone server's subsequent `ZoneToBindPoint` log to 189 — an authority outside
> the client, per T144's method. A second NPC-death capture confirms
> `damage@24 = 58`. So the arbiter rule (T145) held: **`bindzoneid@12`,
> `spell_id@16`, `attack_skill@20`, `damage@24`** is correct and the
> `titanium_structs.h` variant is the stale one.
>
> **Still open, narrowly:** `is_PC@28` read 0 in *both* captures, so that one
> field is unconfirmed. Evidence: `/mnt/projects/fable/review/T146/`.
> The header comment spelling out both candidate orders should now be reduced
> to a statement of the confirmed order plus the `is_PC` caveat.

The original entry, kept for the reasoning it records:

### (was) OPEN — `Death_Struct` field order (highest value of the five)

Size is certain: 8 x uint32 = 32, in every eqemu variant. The *order* is not:

| offset | `common/eq_packet_structs.h:1367` (== ours) | `common/patches/titanium_structs.h:1176` |
|---|---|---|
| 12 | `bindzoneid` | `attack_skill` |
| 16 | `spell_id` | `spell_id` |
| 20 | `attack_skill` | `bindzoneid` |

Exactly reversed either side of `spell_id`. `titanium.cpp` has no
`ENCODE(OP_Death)`, so the arbiter rule (T145) says the base struct is the
wire format and ours is right — but that is reasoning from an absence, not an
observation. **Settled by one capture: die once, dump the bytes, read offsets
12 and 20.** T146 was asked to take this opportunistically; if it did not, the
capture is still owed. A `static_assert` was added at 32 with a header comment
spelling out both candidate orders; do not "harmonise" the fields without the
capture.

### OPEN — `Consider_Struct` (28) not wire-observed

T147 corrected this from 24 to 28 (appending `pvpcon` + `unknown3[3]`) on the
strength of eqemu's base and Titanium structs *agreeing* — and of
`src/client/eq_combat_actions.cpp:40`'s hand-rolled parser already guarding on
`2 + 28`. Strong, but still source-reading. A capture would close it.

### OPEN — `Animation_Struct` and `MobHealth_Struct` were DELETED, not settled

Both were dead in production and wrong by size against eqemu (ours 8 vs 4, and
6 vs 3), so T147 removed them as traps. That is not the same as knowing the
true layout: if either opcode is ever consumed, **capture first, then write the
struct** — do not restore the deleted versions from git history.

### OPEN — `PetitionUpdate_Struct` DELETED as unresolvable

eqemu's counterpart (`titanium_structs.h:1905`, `eq_packet_structs.h:2618`)
contains a `time_t`, which is not wire-credible as written (its width is
platform-dependent), and it carries `quetotal` + `charname[64]` that ours
dropped entirely. So we cannot tell whether ours was truncated or eqemu's is
unmaintained. Ours was dead, so T147 deleted it rather than asserting 112.
Low priority; needs a capture if petitions are ever implemented.

### OPEN — `TradeAcceptClick_Struct` byte 4

Size 8 is certain and the struct is genuinely live (11 production references),
so it was kept as-is. Whether byte 4 (`accepted`) is a meaningful flag or
eqemu's `unknown4` garbage is unknown; `src/client/eq.cpp` logs it as
`accepted`. A capture of a real accept/unaccept toggle settles it. Noted in a
header comment above the struct.

### Method note for whoever closes these

`tests/captured_packets.h` (T147) is the harness: paste the bytes as a
`CapturedPacket` with a `provenance` string, write a field table against
`offsetof()`, done. See `tests/test_captured_packets.cpp` for the worked
example on `OP_GroundSpawn`. The point of pinning against `offsetof()` rather
than literals is that the test then fails when the *struct* drifts, which is
the failure a `sizeof` assert cannot see.

---

## T150 correction (2026-08-19, orchestrator) — `OP_Track` DOES have a sender; do NOT write `SendTrackRequest()`

T150's Review records that `ZoneProcessTrack` is unreachable because "the track
list comes from `OP_Track`, a separate opcode this client has **no sender
for**", and names a new `SendTrackRequest()` as the owed follow-up.

**The premise is wrong and the recommendation would duplicate wire surface.**
`SkillManager::getSkillOpcode()` maps **skill 53 (Tracking) → `HC_OP_Track`**
(`src/client/skill/skill_manager.cpp:458`), and `sendSkillUsePacket()` (`:432`)
sends it as a zero-length body via the generic skill path. eqemu's
`Handle_OP_Track` (`zone/client_packet.cpp:15266`) is exactly the list
generator — it calls `entity_list.MakeTrackPacket(this)`.

So a sender exists. What does **not** exist is a *command-reachable* one:
`activateSkill()` is called only from the hotbar/bridge
(`src/client/eq_bridge.cpp:1052` and `:1360`), and `useSkill` has **no
registration in any command registry** — `grep useskill` over
`command_processor.cpp` returns nothing.

**Correct follow-up: route `/track` with no arguments to the existing
`activateSkill(53)` path.** Writing a second sender for an opcode that already
has one is duplicated-then-drifted, the defect this programme has hit
repeatedly — and T147 spent a whole unit deleting the last generation of it.

**Wider point for the triage unit (the remaining 11):** the skill system is a
second, generic route to the wire that the zero-caller sweep does not see. Skill
ids 6, 9, 16, 17, 25, 27, 29, 32, 40, 42, 48, 53, 55, 62, 67, 71, 73 all reach
`QueuePacket` through `sendSkillUsePacket`. **`SendApplyPoison` is on the
zero-caller list while skill 6 already routes to `HC_OP_ApplyPoison`** — so at
least one "dead sender" is a duplicate of a live path, and deleting it may be
better than wiring it. **Check every remaining candidate against
`getSkillOpcode()` before scoping it as dead.**

Also note `useSkill` being command-unreachable is its own gap: the entire skill
system is hotbar-only. That is a candidate unit in its own right.

---

## T148 (2026-08-19) — four defects surfaced by making the GM senders reachable

T148 wired the nine `src/client/eq_gm.cpp` senders, which had zero production
callers. **Making them reachable surfaced four defects in nine senders — one per
two.** Filed by the orchestrator at merge; T148 could not append here because
T149 and T150 both edited this file on `fable` mid-unit.

**The generalisable lesson, and it revises this arc's framing: a zero-caller
sender is not "correct but unreached". It is UNVERIFIED.** Nothing has ever
exercised it, so nothing has ever contradicted it. Expect roughly one defect per
two senders in the remaining families, and budget wiring units accordingly —
they are not the mechanical plumbing exercises T132's ~50-minute measurement
suggests.

### FIXED in T148

- **`SendGMEmoteZone` sent 32 bytes where the server demands 512.** Rejected
  outright; nobody ever saw the emote. Fixed in `eq_gm.cpp` (in territory),
  re-verified live. Built inline rather than adding a struct to the shared
  `packet_structs.h`, to stay out of the parallel units' way.

### OPEN — not fixed, each needs its own unit

- **`SendGMHideMe` is misrouted.** It reaches the server as
  `OP_SpawnAppearance` type 20 and is dropped with
  `Unknown SpawnAppearance type: 0x0014` — it never reaches
  `Handle_OP_GMHideMe`. `eq_gm.cpp:126` calls `SendSpawnAppearance(AT_GM, ...)`
  where it should send `OP_GMHideMe`. **This is why `/hideme` is the one command
  of the nine that T148 could not exercise.**
- **`ZoneProcessGMFind` decodes the reply's floats as ints** — the live session
  printed `at (-998490112, ...)`. A receive-side struct defect, same family as
  the T144-T147 arc; belongs with the remaining captured-bytes work.
- **`SendGMSummon` sends zeroed coordinates.** The summon still worked live
  (the server relocated the target), so the coordinates are evidently ignored or
  defaulted server-side — but sending zeros is not intentional and should be
  either populated or documented as deliberately unused.

### Also recorded

The negative-control session proved the **naming distinction is real**: with the
`gm` byte at 0, `#set time` and `#set gm` still worked, because the server gates
on `Client::Admin()` (account status 200) — which **never reaches the client**.
`m_gm_mode` gates our commands; it is not and cannot be a mirror of server-side
GM authority. Do not "upgrade" it into one.

## T185 (2026-08-23) — player corpses now take the server's name on the death path (`c75914f5`, live-verified `a2508b71`)

Closes the open half of the corpse-naming defect T161 reported and T173
partly mitigated (T173 fixed target RANKING; this fixes the NAME). eqemu
broadcasts `OP_MobRename` for NPC corpses only; for a player corpse
`Corpse::CalcCorpseName` says the client derives it itself. New
`EQT::MakeCorpseName()` (inverse of T160's `ParseCorpseName`) mirrors
`CalcCorpseName`: digits stripped from the base, player →
`<base>'s corpse<id>`, NPC → `<base>\`s_corpse<id>`, id = the victim's
entity id (every `AddCorpse(corpse, GetID())` site). Without it `/target`,
`/consider`, `/loot` by name missed the corpse until re-zone.

## T212 (2026-08-24) — the 0x2C fix confirmed at data and pixel level; corrected `equipment_models.json` shipped (`54b9bc72`)

`equipment_index_builder` uses the same WLD parser T211 fixed, so the shipped
index carried the bug: model 1036 (`IT1036_O_DMSPRITEDEF`) was `['']` — the
ONLY one of 1000 models with no usable texture — and is now four real sword
textures. Pixel level: model 1036 is referenced by zero items/NPCs in the
eqemu DB, so it was forced via a temporary npc_type; the NPC builds
(placeholder=false) and renders a textured two-handed blade, 0 magenta-ish
pixels of 480000 (`review/T211/`). **Traps recorded:** a DB `zone_id` edit
leaves coordinates stale (use `/say #zone <zone>` or player and spawned NPC
land out of bounds and never build); `#npctypespawn` spawns at the invoker's
position and `#zone`'s coord args are ignored.

## T214 (2026-08-25) — EQE1 request log; `unloadZone()` was destroying `willeq.eqe1`; the usage reporter manufactured a false finding (`6f2f22f6`, `3e4e8a95`, `e70e3347`)

1. **Instrument** (`6f2f22f6`): `ConstrainedTextureCache` records every
   distinct EQE1 name looked up and which archive served it (or
   `<NOT-FOUND>`), dumped as TSV when `EQT_EQE1_REQUEST_LOG_DIR` is set —
   once per zone load and again at teardown (the teardown dump is the
   load-bearing one: the T210 distance gate defers entities, so the
   zone-load snapshot misses what they request on approach — 89 names, 40%
   of the befallen session). NOT-FOUND = 0 in all audited runs. Also
   corrected a misread: "qeynos2_chr served 0 lookups" came from a RE-ZONE's
   usage line (cache already warm); cold it serves 89. Read EQE1-USAGE per
   zone load, not per log file.
2. **Correctness fix** (`3e4e8a95`): `unloadZone()` destroyed EVERY EQE1
   loader including `willeq.eqe1` — the client's own pack, opened once at
   init and never reopened — so after the first zone unload `detail_atlas`
   missed every open archive and the T207 resolver opened `textures.eqe1`
   (2.6MB, 64 entries) to serve that single name, every zone. `unloadZone`
   now retains `isPersistentEQE1Archive()` names and re-registers them.
   Verified befallen + qeynos2 `--constrained orangepi`: `textures.eqe1` no
   longer mapped; +87KB net mapped — a correctness fix, not a saving.
3. **Reporter fix** (`e70e3347`) and **WILL'S ARCHIVE RULE**: an archive is
   legitimately open iff (R1) it is the current zone's own
   (`{zone}`/`{zone}_obj`/`{zone}_chr`) or (R2) a lookup needed it and
   nothing already open could serve it. Only R2 archives can be waste.
   `befallen_chr` (4065KB, 0 lookups) was R1 — NOT a defect; the report had
   labelled any zero-hit archive "wasted", which manufactured the finding.
   Now classified before counted ("idle (expected)"). All 9 qeynos2 + 6
   befallen archives audited: zero violations. Recorded in
   `docs/decisions.md` (2026-08-25).

## Cross-references for units with no entry above (T215, 2026-08-25)

- **T174** `d66ac19e` + `d79cb560`: keyboard hotbar fires by slot content
  type (Spell → `castSpell`, Skill → `useSkill`) instead of always
  `castSpell`; Item and Emote slots are DEBUG-logged and skipped (open, see
  Index). `refactor/T174_keyboard_hotbar.md`.
- **T189** (no commit): root-caused the T188 "gate losing entities" claim
  to `inSceneGraph` — a wrong conclusion; **corrected by T190 `8bad78d4`**
  (the drain was never broken; the `/move` teleport test position was).
  `refactor/T190_entity_drain_correction.md`.
- **T192** (no commit of its own): retracted T191's "composite atlas 0%
  adoption" — every T191 run had `eqe1Path=''`; annotated on T191 at
  `5f875227`. `refactor/T191_composite_adoption_and_sender_verify.md`.
- **T194** (reverted, no fable commit): lazy per-filename composite opening
  broke the zone load (`fishe0101.bmp` FATAL) — a composite archive is NOT
  named after the textures it contains; recorded at `edbbb90c` (T193
  addendum). Led to T196's table-of-contents index shape.
- **T196** (no commit of its own): whole-directory TOC index
  (`texture name → archive path`) — the shape T199 `bc54e527`/`3edeb0dd`
  and T207 `ed14c4de` built on; its gdb backtrace of the x86 crash was the
  first correct read of what T205 later fixed. See
  `refactor/T199_spawn_driven_archive_set.md`, `refactor/T201_eqe1_crash_matrix.md`.
- **T198** `28a59bf4` (composite atlas dropped from the `orangepi` preset —
  Will's decision, memory pressure), `4b78f682` (EQE1 assets regenerated
  999/1006), `09d6ef03` (the remaining gap is archive coverage in
  `config/eqe1_archives.json`, not the builder; phantom `gequip7` noted).
- **T202** `950862dd` (NOT MERGED): UI-atlas LRU-eviction hypothesis
  DISPROVEN (eviction never ran); established the exit-status repro
  (139 = SIGSEGV, never classify by log contents). Follow-up `d4ae429c`:
  the load completes, the first render frame crashes.
- **T204** `f524af3d`: Irrlicht rebuilt with `-g`; `p *texture` at the fault
  shows a dangling `ITexture` (vtable 0x50) — settled it as a use-after-free
  of the UI atlas texture, which T205 `3b456a1b` then fixed
  (`ConstrainedTextureCache::clear()` destroyed the UI atlas).
- **Doc debt outside T215's territory (2026-08-25):** `docker/CLAUDE.md:19`
  still shows `./scripts/build-arm-noble.sh --no-gles2  # Desktop GL instead
  of GLES2` — that flag was retired by P32 (`docs/decisions.md` 2026-07-28
  addendum; GLES2 is the only ARM renderer) — and its target table calls the
  Pi build "Lima/Mesa GL 2.1"-era desktop GL. Owned by whichever unit next
  touches `docker/`; recorded so it is not re-discovered.

## `InputActionBridge` heading path is dead in the graphical build — delete-unit candidate (T218, 2026-08-25) — **CLOSED T228 `67a131bf`: processMouseInput + processContinuousInput and their state DELETED (−287 lines)**

`src/client/action/input_action_bridge.cpp` reads `m_state.player().heading()`
at two sites (T218 plan item 4). Neither is projected (no `PlayerMoved` case in
`GameStateSyncBridge`, so the value is always 0) and, measured at runtime,
neither matters:

- **`processContinuousInput()` (keyboard turn, ~line 220): zero callers.**
  Declared in the header, defined, never invoked — `update()` explicitly
  dropped it ("redundant path that bypassed renderer collision"). Structurally
  dead.
- **`processMouseInput()` mouse-look branch (~line 254): called every game
  tick, branch never taken.** Probe: `--egl-x11` under Xvfb, 6 xdotool
  left-button drags of 400 px (120 `MotionNotify` events) after "Fully
  connected"; the renderer's own mouse-look (`renderer_player_physics.cpp`)
  turned the player each time (`[ROT-CAMERA] setFollowPosition: heading=80
  -> 180 -> 280 ...`), while the `[T218]` LOG_DEBUG added inside the branch
  fired **0 times** (`review/T218/heading_probe_A_testchar_eglx11.log`). Why:
  both threads consume the same destructive `RendererEventReceiver::
  getMouseDeltaX()` counter, but the render thread pumps the X events
  (`isRunning()` -> `device_->run()`) and reads the delta in the SAME
  `processFrame()` (`updatePlayerMovement`, `renderer_frame.cpp:542`), so the
  non-zero window the game thread would have to hit is microseconds wide.
  Had it ever fired it would have been harmful, not helpful: it would call
  `setHeading(0 + delta*0.1)` -> `EverQuest::SetHeading` + `SendPositionUpdate`
  from a heading of 0, snapping the player.

Per the T218 plan: NOT fixed (no `PlayerMoved` projection added). Recommend a
delete unit removing `processContinuousInput()`, the mouse-look block of
`processMouseInput()`, and `m_turnSpeed`/`m_mouseSensitivity`/`m_invertMouseY`
if nothing else reads them — leaving `processMouseInput()`'s targeting role,
if any, intact. `refactor/T218_player_flags_projection.md` Review has the
evidence table.
