# J02 — Batch J Wave 1: split eq.cpp (3,923 → 6 TUs, largest ~1,065)

**Slot: assigned at dispatch. Branch `task/J02-eq-split` cut from `fable`.
x86 only. No Pi hardware. No eqemu.**

Wave 1 of `refactor/J_file_size_reclamation.md`, running alongside **J03** and
**J04**. Read, before editing anything: **`refactor/J_file_size_reclamation.md`
§Process per unit** (it carries six corrections made after the J01 pilot and it
**wins over anything older**), `refactor/J01_inventory_manager_split.md`
(the proven template, and its Review), `refactor/F_file_split.md`,
`refactor/T166_split_renderer_zone_load.md` and `docs/module_map.md`.

`eq.cpp` is the largest file in the tree and Batch F already split it once
(3,629 → grew back to 3,923). This is a re-split, not a first cut.

## MOVE-ONLY. No behavior changes, no interface changes.

Method definitions move **verbatim**. No header changes: every `EverQuest`
declaration stays exactly where it is in `include/client/eq.h`. **No method
decomposition** — that is Batch G's shape and is out of scope, `DispatchZonePacket`
emphatically included (see §Hazards). No drive-by fixes.

## The split

Measured at `2eb34f3d`, re-confirmed byte-for-byte at `7e0c7823` after J01 merged (2026-09-02). **Re-verify every range against the tree
at your own branch tip before you cut** — `eq.cpp` grew 10 lines in the days
between the survey and this plan, and J03/J04 are landing in parallel. Report
drift in your Review rather than silently adjusting.

Preamble is **lines 1-79** (78 lines of includes plus the `M_PI` guard). There
is **no namespace** in this file — `EverQuest::` methods are defined at file
scope, so there is no namespace open/close to replicate.

Ranges below start at the **first line of the cluster's leading comment**, per
§Process. Where a cluster has no banner, they start at the first blank line
above the first statement.

| Range | Cluster | Target TU | ≈ lines |
|---|---|---|---|
| 81-799 | **stays.** `s_debug_level` definition, ctor/dtor, `StartSessionRecording`, `OpenDatabase`, `InitializeSpellSystem`, `ConnectToLogin`, `TickNetwork`, loading phase + progress + status text, `OnGameStateComplete`, `OnGraphicsComplete`, `PublishFullStateSnapshot` (197), `ConnectToWorld`, `ConnectToZone`, the four `ZoneOn*` connection callbacks, `ReplayZonePacket` | **stays** `eq.cpp` | — |
| 800-1681 | `DispatchZonePacket` — **one method, 882 lines** | `eq_zone_dispatch.cpp` | **~965** |
| 1682-1953 | the `ZoneSend*` handshake family: `SessionReady`, `ZoneEntry`, `ReqClientSpawn`, `ClientReady`, `SetServerFilter`, `StreamIdentify`, `AckPacket`, `ReqNewZone`, `SendAATable`, `UpdateAA`, `SendTributes`, `RequestGuildTributes`, `SpawnAppearance`, `SendExpZonein` | `eq_zone_handshake.cpp` | ~355 |
| 1954-2933 | `ZoneProcessNewZone` (160), `ZoneProcessPlayerProfile` (467), `ZoneProcessAnimation` (295), `ZoneProcessGuildMOTD`, `CheckZoneRequestPhaseComplete` | `eq_zone_entry.cpp` | **~1,065** |
| 2934-3173 | the UCS family: `ConnectToUCS`, `UCSOnNewConnection`, `UCSOnConnectionLost`, `UCSOnPacketRecv`, `UCSSendMailLogin`, `UCSSendTell`, `UCSSendJoinChannel`, `UCSSendLeaveChannel`, `UCSProcessChatMessage`, `UCSProcessMailNew`, `UCSProcessBuddy` | `eq_ucs.cpp` | ~325 |
| 3174-3700 | the `// ====` accessor banner block (movement / stat / entity-sync / combat-sync / behavioural-flag accessors, 3174-3272) then `ZoneProcessPlayerStateAdd`, `ZoneProcessDeath` (168), `ZoneProcessPlayerStateRemove`, `ZoneProcessStamina`, `ZoneProcessZonePlayerToBind`, `ZoneProcessLevelUpdate`, `SendTradeSkillCombine`, `ZoneProcessTradeSkillCombine` | `eq_player_state.cpp` | ~610 |
| 3701-3923 | **stays.** `SafeQueueZonePacket`, `StartUpdateLoop`, `StopUpdateLoop`, the Phase 3/4 banner block, `SendPetition`, `SetSneak`, `QueuePacket`, `SetEQClientPath`, `SetConstrainedConfig` | **stays** `eq.cpp` | — |

**Result: 6 TUs** — `eq.cpp` ≈ 1,025 (79 preamble + 719 + 223),
`eq_zone_dispatch.cpp` ≈ 965, `eq_zone_entry.cpp` ≈ 1,065,
`eq_player_state.cpp` ≈ 610, `eq_zone_handshake.cpp` ≈ 355,
`eq_ucs.cpp` ≈ 325. Every one under 1,500.

## Hazards

Three, all verified directly against the tree — not taken from the survey.

1. **`int EverQuest::s_debug_level = 0;` (`eq.cpp:82-83`) is a static member
   DEFINITION.** It must appear in **exactly one** TU or the link fails
   (duplicate symbol) or does not resolve. It stays in `eq.cpp` with the
   preamble. Do not let the "full include block" rule tempt you into copying
   the two lines above it — copy the **includes**, not the definition.

2. **`#ifdef EQT_HAS_GRAPHICS` at `:3918` wraps `SetConstrainedConfig` and
   closes at `:3923`, the last line of the file.** The guard must travel intact
   with the method. Every other preprocessor conditional in the body
   (`:311`, `:355`, `:424`/`:458`/`:472`, `:2060`, `:3374`) is fully contained
   inside a single method and travels for free — confirm this at your tip with
   `awk 'NR>80 && /^#(if|ifdef|ifndef|else|elif|endif)/ {print NR": "$0}'`.

3. **`DispatchZonePacket` becomes a ~965-line TU with essentially no room to
   grow** (1,500 ceiling − 79-line preamble − 882-line method ≈ 540 lines of
   slack, all of which the next opcode-handling addition eats into). **Flag
   this in your Review and do NOTHING about it in this unit.** The remedy —
   domain sub-dispatchers, i.e. carving the opcode switch into
   `DispatchZoneCombatPacket` / `DispatchZoneEntityPacket` / … — is Batch G's
   shape: it changes method bodies, is a real design question, and belongs to
   the separate follow-up already recorded in
   `refactor/J_file_size_reclamation.md` §Optional. **A J02 that decomposes
   `DispatchZonePacket` has failed its acceptance, not exceeded it.**

No anonymous namespaces, no file-local `static` functions, no file-scope
objects with construction order to preserve — re-check this yourself and say
so in the Review. If you find one, **say so loudly**: it would mean the
survey's hazard analysis is unreliable for the remaining units too.

## Includes

**Each new TU carries `eq.cpp`'s full 79-line include block, verbatim.** Not
"the includes it needs" — see §Process. Do not create a shared "everything"
header.

## CMake

**Do not trust a line number.** Find the sites yourself:

```bash
grep -n 'src/client/eq\.cpp' CMakeLists.txt tests/CMakeLists.txt
```

At `7e0c7823` there is exactly **one** site, `CMakeLists.txt:266`
(in `CORE_SOURCES`), and `tests/CMakeLists.txt` names `eq.cpp` nowhere — but
the file moves and J03/J04 are editing `CMakeLists.txt` in parallel. Add all
five new TUs beside the original in **every** list that names it.

## Acceptance (the Batch J bar — every item, reported by EXIT CODE)

- build clean; `cmake --build build -j24` classified by **EXIT CODE**, never by
  grepping the log (`grep ' error '` never matches GCC, which prints `error:`)
- full **`ctest -j6`** (NOT -j24 — the visual gate's CPU ceiling flakes above
  that), zero new failures vs `refactor/test_baseline.txt`
- ⚠ `BASE=$(git merge-base fable HEAD)` then
  `python3 scripts/refactor/verify_move_only.py "$BASE..HEAD"` **clean** — the
  unit's central proof. **NOT `fable..HEAD`:** three units are merging into
  `fable` during this wave and `fable..HEAD` will report the other two units'
  work, in reverse, as a very loud spurious FAIL. Record `$BASE` and paste the
  output in the Review.
- ⚠ the **line-multiset check** (§Process): the six files' bodies, each
  stripped of its preamble, form a multiset identical to the original's lines
  80-3923; and the seven extracted ranges re-concatenated in original order
  are byte-identical to `git show "$BASE:src/client/eq.cpp" | sed -n '80,3923p'`.
  Paste both results.
- visual gate PASS with **goldens untouched**. A re-bless would mean the split
  changed behaviour — **STOP and report** rather than re-blessing.
- `scripts/build-gles2-x86.sh` and `scripts/build-arm-noble.sh` both clean
- `bash scripts/refactor/check_file_sizes.sh --files <every changed and created
  file>` → **0 warn, 0 fail**. `CMakeLists.txt`'s pre-existing 1,899-line WARN
  is not yours (§Process) — note it and move on.
- ⚠ `chmod 644` every created file before `git add`.

## Review must answer

1. Line-range drift vs this plan at your tip, and the final line count of each
   of the six TUs.
2. The verifier's base rev you used, and whether it came back clean first try.
3. Wall clock and the slowest step, and whether running concurrently with
   J03/J04 stretched the verification tail (Wave 1 is the first parallel wave;
   the Wave 2-4 schedule depends on this number).
4. `DispatchZonePacket`'s TU: its exact final size and remaining slack, as the
   input to the sub-dispatcher decision.

## Territory

`src/client/eq.cpp` and the five new `src/client/eq_*.cpp` TUs beside it,
`CMakeLists.txt`, this file. **Nothing else** — not `include/client/eq.h`, not
the other 18 `eq_*.cpp` domain TUs, not `docs/module_map.md` (the new TUs will
need entries there; that is a follow-up, not this unit).

Concurrent at dispatch: **J03** (`graphics/entity_renderer.cpp` + new
`entity_*.cpp`, `CMakeLists.txt`), **J04** (`action/command_processor.cpp` +
new `command_*.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`), **T318**
(slot3: `graphics/entity_renderer.*`, `eq_entities.cpp`, `eq_guild.cpp`,
`spell_effects.cpp`, `event_bus.h`). **No source overlap with any of them —
`CMakeLists.txt` is the only shared file, which is why merges are serialized
one at a time with a rebase.**
