# K14 — Directory move: the last flat `eq_*.cpp` files, and `eq_strings` → free functions

Batch: `refactor/K_component_extraction.md`. **Runs after K13 merges.** Can
run alongside K15 (disjoint territory).

## Measured at `a89314e0` (2026-09-03) — re-measure at your tip

Flat files left in `src/client/` from the original 38: `eq.cpp`,
`eq_action_handler.cpp`, `eq_action_handler_extended.cpp`,
`eq_action_handler_spells_trade.cpp`, `eq_chat.cpp` (may be empty after
K13 — delete if so), `eq_player_info.cpp` (same), `eq_strings.cpp` (14
bodies, all pure lookups: `GetClassName`, `GetRaceName`, `GetDeityName`,
`GetOpcodeName`, `GetBodyTypeName`, `GetChatTypeName`, `GetEquipSlotName`,
`GetNpcTypeName`, `FormatPrice`, `GetStringMessage`,
`GetFormattedStringMessage`, `LoadStringFiles`, `DumpPacket`,
`DumpEntityAppearance`).

## Steps

1. `git mv src/client/eq.cpp src/client/core/everquest.cpp`;
   `git mv src/client/eq_action_handler*.cpp src/client/action/` (the action
   layer already lives there — check for name clashes with
   `action_handler.cpp` and prefix `eq_` stays if there is one). CMake lines
   follow; `docs/module_map.md` rows follow; `tests/` and `scripts/` text
   readers of any moved filename are repointed (grep first — K03 c).
2. `eq_strings.cpp`: the lookups that read no `EverQuest` state become free
   functions in `include/client/eq_strings.h` / `src/client/core/eq_strings.cpp`
   (namespace `eqt::strings`); the ones that read `m_string_db`
   (`GetStringMessage`, `GetFormattedStringMessage`, `LoadStringFiles`) move
   into a small `StringDb` class owned by `EverQuest` and exposed on
   `ClientContext` as `ctx_.strings`. Callers by compiler. `DumpPacket` /
   `DumpEntityAppearance` are debug helpers with `s_debug_level` reads —
   free functions taking the level as a parameter.
3. `include/client/eq.h`: delete the moved declarations; ratchet.
4. Multiset proof per moved body set; zero-code-residual.

## Territory

The files above, `src/client/core/` + `src/client/action/` (moves in), root
`CMakeLists.txt`, `include/client/eq.h`, `include/client/client_context.h`
(one member), `include/client/eq_strings.h` (new), `docs/module_map.md`,
tests/scripts text readers, CALL-SITE-ONLY edits in callers. NOT the
component directories except call sites.

## Verification / Review

Executed alone in `wt/slot1` on `task/K14-directory-move`, branched from
`fable` at **`7c125b28`**. Re-measured at that tip: the flat files were
exactly the six named in the plan, and (contrary to the plan's "may be
empty after K13 — delete if so") `eq_chat.cpp` (35 lines, three forwarders)
and `eq_player_info.cpp` (253 lines, 46 forwarders) were **not** empty — K13
left them as the sanctioned forwarder set, explicitly deferring "whether the
surface goes away entirely" to this unit. That question is a design call
beyond a directory move's contract (§Mechanism 5's forwarder exception, not
this plan's step list), so both moved verbatim and the question is left
open, noted below.

Neither STOP condition fired.

### Files moved

`git mv`, byte-identical except where noted:

| old path | new path | content change |
|---|---|---|
| `src/client/eq.cpp` | `src/client/core/everquest.cpp` | yes — see below |
| `src/client/eq_action_handler.cpp` | `src/client/action/eq_action_handler.cpp` | none |
| `src/client/eq_action_handler_extended.cpp` | `src/client/action/eq_action_handler_extended.cpp` | 2 call-site retargets (`FormatVendorPrice`/`GetClassName`) |
| `src/client/eq_action_handler_spells_trade.cpp` | `src/client/action/eq_action_handler_spells_trade.cpp` | none |
| `src/client/eq_chat.cpp` | `src/client/core/eq_chat.cpp` | none |
| `src/client/eq_player_info.cpp` | `src/client/core/eq_player_info.cpp` | none |
| `src/client/eq_strings.cpp` | `src/client/core/eq_strings.cpp` | split — see below |

No name clash with `src/client/action/action_dispatcher.cpp` etc.; the
`eq_` prefix on the three action-handler files was kept as the plan allowed.

### `eq_strings.cpp` split

Fourteen bodies, three destinations:

| destination | bodies | shape |
|---|---|---|
| `eqt::strings` free functions (`include/client/eq_strings.h`, `src/client/core/eq_strings.cpp`) | `GetClassName`, `GetRaceName`, `GetDeityName`, `GetOpcodeName`, `GetBodyTypeName`, `GetChatTypeName`, `GetEquipSlotName`, `GetNpcTypeName`, `FormatPrice`, `DumpPacket` (both overloads) | pure lookups, no `EverQuest` state |
| `eqt::StringDb`, the fourth context service (same header/TU) | `GetStringMessage`, `GetFormattedStringMessage`, `LoadStringFiles` | holds `EQT::StringDatabase& m_string_db`, spelled the same as `EverQuest::m_string_db` (K13 correction (kk)); exposed as `ctx_.strings` and `EverQuest::Strings()` |
| stays on `EverQuest`, moved into `everquest.cpp` | `DumpEntityAppearance` (both overloads) | **deviation 1** |

**Deviation 1 — `DumpEntityAppearance` is not a pure lookup and does not
belong in `eqt::strings`.** The plan's measured-bodies list groups it with
`DumpPacket` as "debug helpers with `s_debug_level` reads". Reading the body:
it has **zero** `s_debug_level` reads. It reads `m_entities` — `EverQuest`'s
own spawn-id map — iterates it, and recurses into itself. That is
`EverQuest` state, not a lookup table, so it stayed on `EverQuest` and moved
with the rest of `eq.cpp` into `everquest.cpp`, at the end of the file next
to the state it dumps. Its five internal name-lookup calls
(`GetNpcTypeName`/`GetRaceName`/`GetClassName`/`GetBodyTypeName`/
`GetEquipSlotName`) now reach `eqt::strings::` — the only substitution the
move needed. Proved byte-for-byte against the original body below.

**Deviation 2 — `DumpPacket`'s `s_debug_level` read became
`EverQuest::GetDebugLevel()`, not "a free function taking the level as a
parameter" as the plan's step 2 says.** `GetDebugLevel()` already existed
as `EverQuest`'s public accessor returning that exact static and nothing
else — the identical substitution K13 correction (jj)/3 used for
`eqt::PacketSender::SafeQueueZonePacket`, the prior case of a body moving
into a destination that is not a friend of `EverQuest`. This is
behaviour-preserving and changes no caller's argument list (~30 call sites
across the tree quality for `DumpPacket`, all just re-qualified from
`EverQuest::DumpPacket(` to `eqt::strings::DumpPacket(`, none touched
beyond that). Changing the signature to take an explicit level parameter, as
the plan's wording suggests, would have forced an edit to every one of those
30 call sites for no behavioural gain; recorded here rather than followed
literally.

`FormatVendorPrice` (an eq.h-inline one-line alias for `FormatPrice`, not
named in the plan's 14-body list but sitting right next to `FormatPrice`)
had exactly one caller; inlined at that caller per §Mechanism 5, which is
the same treatment K13 gave every single-caller forwarder.

### `eq.h`

| | lines |
|---|---|
| at dispatch (`fable`, `7c125b28`) | 916 |
| measured after | **900 (-16)** |

Deleted: `FormatPrice`, `FormatVendorPrice`, `DumpPacket` ×2, `GetOpcodeName`,
`GetStringMessage`, `GetFormattedStringMessage`, `GetChatTypeName`,
`GetClassName`, `GetRaceName`, `GetDeityName`, `GetBodyTypeName`,
`GetEquipSlotName`, `GetNpcTypeName`, `LoadStringFiles` (15 declarations)
plus their comment banners (~35 lines, including the now-orphaned "K13:
PRIVATE half of the player-state facade" note, which explained why nothing
sat between two declarations that are both gone now). Added: the `m_strings`
member, the `Strings()` accessor, and K14 replacement banners (~19 lines).
The friend-class banner (line ~505) was corrected in place — it used to cite
`FormatPrice`/`DumpPacket`/`s_debug_level` together as the three private
statics forcing every component's `friend` line; `FormatPrice` and
`DumpPacket` are gone from `EverQuest` now, so the note was narrowed to
`s_debug_level` alone, which on its own still forces every line (100+ direct
reads tree-wide, unaffected by this unit).
`scripts/refactor/file_size_allowlist.txt`'s `eq.h` entry lowered
**916 → 900** in the same commit.

### Zero-code-residual multiset proof (§Mechanism 8)

Old bodies at `7c125b28`, normalised by name mapping (strip comment-only/
blank/preprocessor lines, mask nothing else needed — no string-literal
collisions with the substituted names), diffed as multisets against the new
files:

| moved body set | old code lines | new code lines | ADDED | REMOVED |
|---|---|---|---|---|
| `eq_action_handler.cpp` → `action/eq_action_handler.cpp` | — | — | **0 (byte-identical)** | **0** |
| `eq_action_handler_spells_trade.cpp` → `action/eq_action_handler_spells_trade.cpp` | — | — | **0 (byte-identical)** | **0** |
| `eq_chat.cpp` → `core/eq_chat.cpp` | — | — | **0 (byte-identical)** | **0** |
| `eq_player_info.cpp` → `core/eq_player_info.cpp` | — | — | **0 (byte-identical)** | **0** |
| `eq_action_handler_extended.cpp` → `action/eq_action_handler_extended.cpp` | diff shows exactly 2 lines changed | | 2 name-mapped | 2 name-mapped |
| `eq_strings.cpp`'s 12 bodies (`eqt::strings::*` + `eqt::StringDb::*`, `DumpEntityAppearance` excluded) → `core/eq_strings.cpp` | normalised | normalised | **0** | **0** |
| `DumpEntityAppearance` (both overloads) → `core/everquest.cpp` | normalised, 5 calls re-qualified | normalised | **0** | **0** |

Normalisation used: `std::string EverQuest::FormatPrice` →
`std::string eqt::strings::FormatPrice` (and the same shape for the other
nine `eqt::strings` functions and `void EverQuest::DumpPacket`);
`std::string EverQuest::GetStringMessage`/`GetFormattedStringMessage`/
`bool EverQuest::LoadStringFiles` → `eqt::StringDb::`; `if (s_debug_level < 6)
return;` → `if (EverQuest::GetDebugLevel() < 6) return;`; for
`DumpEntityAppearance`, `GetNpcTypeName(`/`GetRaceName(`/`GetClassName(`/
`GetBodyTypeName(`/`GetEquipSlotName(` → `eqt::strings::` + same. **Nothing
else.** Every set matched exactly.

`everquest.cpp`'s remaining diff against old `eq.cpp` (outside the
`DumpEntityAppearance` block, already proved above) is the scaffolding K13's
proof method allows to land in the moved-in TU: two `#include`s
(`client/eq_strings.h`, `<iostream>`), the `m_strings{m_string_db}`
ctor-init-list entry and its `m_ctx{...}` slot, and the four call-site
substitutions (`GetClassName`/`GetDeityName`/`DumpPacket`/`LoadStringFiles`
inside `EverQuest`'s own bodies, now qualified or routed through
`m_strings`). Nothing else changed.

### Text readers repointed

- `tests/test_chat_channel_names.cpp` (`ChatChannelNames.NoSecondCopyOfTheTableInEqChat`,
  K03(c)/K11(ee) trap): read `/src/client/eq_chat.cpp` by path — repointed to
  `/src/client/core/eq_chat.cpp`.
- `tests/cmake/packets_skills.cmake`: a comment naming the same path, updated
  for accuracy (not itself a citation the checker enforces).
- `tests/test_reachability_sweep.cpp` / `tests/test_duplicate_state_stores.cpp`:
  both walk `src/`/`include/` via `fs::recursive_directory_iterator`, not by
  named path — no edit needed; both re-ran green.
- `src/client/input/CLAUDE.md`, `refactor/BOARD.md` (two citations),
  `docs/module_map.md` (nine rows), `CLAUDE.md` (four citations/mentions):
  every `src/client/eq*.cpp`-shaped path citation repointed. `check_doc_citations.py`
  found these three by FAIL before the fix (`src/client/eq_action_handler.cpp`
  in `src/client/input/CLAUDE.md` and `refactor/BOARD.md`, `src/client/eq.cpp`
  in `refactor/BOARD.md`) and reports clean after: **285 ok, 0 fail, 1 skip.**
  `refactor/BOARD.md` is K15's territory; both edits were pure filename
  repoints forced by this unit's moves, not board-content changes.
- One incidental touch reverted: the tree-wide `EverQuest::GetRaceName(` →
  `eqt::strings::GetRaceName(` sweep (below) also matched a comment in
  `include/client/graphics/eq/race_codes.h` (K15's territory). Reverted to
  keep `git diff --name-only fable -- src/client/graphics
  include/client/graphics` empty, the evidence bar for skipping the visual
  gate/ARM build standalone.

### Callers retargeted (by compiler, then confirmed by grep)

Tree-wide qualifier swap `EverQuest::<Fn>(` → `eqt::strings::<Fn>(` for the
ten free functions, applied across `src/`, `include/`, `tests/`: touched
`chat/chat_component.cpp`, `commands/combat_actions.cpp`,
`commands/commands_spells.cpp`, `commands/commands_train.cpp`,
`login_world/login_world_component.cpp`, `merchant/merchant.cpp`,
`merchant/merchant_trade.cpp`, `movement/movement_update.cpp`,
`player/player_buffs.cpp`, `player/player_component.cpp`,
`zone/zone_component.cpp`, `zone/zone_dispatch.cpp`, `zone/zone_entry.cpp`,
`zone/zone_handshake.cpp`, `packet_sender.cpp`,
`action/eq_action_handler_extended.cpp`, `include/client/eq_body_types.h`
(comment), `tests/test_race_names.cpp`, `tests/test_hunt_target_body_type.cpp`.
`GetStringMessage`/`GetFormattedStringMessage` calls (`chat_component.cpp`,
`merchant.cpp`, `combat_actions.cpp`, `spell/cast_refusal.cpp`,
`tests/test_eq_action_handler_cast.cpp`) retargeted to
`.Strings().GetStringMessage(...)`/`.Strings().GetFormattedStringMessage(...)`
on whatever receiver each site already had (`owner_`, `eq`, `h.eq`).
`LoadStringFiles`'s single internal caller (`everquest.cpp`'s
`SetEQClientPath`) retargeted to `m_strings.LoadStringFiles(path)`.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0 on the first attempt** |
| `ctest --test-dir build -j6 --output-on-failure` | **3,604 / 3,605 passed**, 17 skipped. **1 failure: `AudioManagerTest.PreloadCommonSounds` only** (known ALSA host quirk). The full run's one other apparent failure, `DocCitations`, was a timing artifact — that run started before the doc-citation fixes above landed; re-run standalone (`ctest -R "DocCitations\|FileSizeCeiling"`) after the fixes: **both pass**. `FileSizeCeiling`, all five `ReachabilitySweep.*`, `DuplicateStateStores.*` and `visual_gate` (inside the same full run) all **PASS** |
| `bash scripts/build-gles2-x86.sh` | **exit 0**, run in full (not skipped): `willeq` and `gles2_zone_benchmark` both rebuilt fresh, "=== Done. Binaries in .../build-gles2/bin/ ===" printed, no compiler error lines in the log |
| visual gate (standalone) / `bash scripts/build-arm-noble.sh` | **SKIPPED, with evidence**: `git diff --name-only fable -- src/client/graphics include/client/graphics` is **EMPTY** (after reverting the one incidental `race_codes.h` comment touch above). `visual_gate` nonetheless ran and PASSED inside the full `ctest` run |
| `bash scripts/refactor/check_file_sizes.sh --all` | **833 ok, 0 warn, 0 FAIL**; `eq.h` allowlist entry lowered **916 → 900** |
| `python3 scripts/refactor/check_doc_citations.py` | **285 ok, 0 fail, 1 skip** (after the three repoints above; 3 FAIL before) |
| multiset proof | all seven moved-body sets: **0 ADDED / 0 REMOVED** (four byte-identical, `eq_action_handler_extended.cpp`'s two lines and `eq_strings.cpp`'s twelve bodies and `DumpEntityAppearance`'s two bodies all matched exactly under the name mapping above) |

### Residuals

None. Every declaration named in the plan's 14-body list was either moved
(13) or, for `DumpEntityAppearance`, moved to a different destination than
the plan named with the reason recorded (deviation 1). `eq_chat.cpp` and
`eq_player_info.cpp` (K13's forwarder sets) moved unchanged — whether that
surface goes away entirely is still an open design question, explicitly
deferred by K13 to this unit and, on reading it, out of a directory move's
scope. Recorded below for whoever picks up K-final.

### Deviations (summary)

1. `eq_chat.cpp`/`eq_player_info.cpp` were not empty (plan's "may be empty
   — delete if so" did not hold); moved as-is, forwarder-surface question
   left open.
2. `DumpEntityAppearance` moved to `everquest.cpp`, not `eqt::strings` — it
   reads `m_entities`, not `s_debug_level`, and is not a pure lookup.
3. `DumpPacket`'s `s_debug_level` read became `EverQuest::GetDebugLevel()`
   (K13's established non-friend-destination substitution), not a new
   explicit parameter as the plan's wording suggested — avoids touching
   ~30 call sites' argument lists for no behavioural change.
4. One incidental cross-territory comment touch (`race_codes.h`) reverted
   to preserve the visual-gate-skip evidence.

### Left for the board / K-final

- Whether `eq_chat.cpp`'s 3 forwarders and `eq_player_info.cpp`'s 46
  forwarders (142 + ~262 non-component call sites respectively, per K13's
  Review) get retargeted directly and the files deleted, is still open —
  K13 named it as this unit's question; reading the code, it is a much
  larger retarget than a directory move's contract covers, and belongs with
  K-final's "`EqActionHandler` retargets to components, forwarders with a
  single caller inlined, `friend` lines removed" work instead.
