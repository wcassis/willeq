# F04 — Split UCS, guild, and GM clusters out of eq.cpp

Three small self-contained domains leave `src/client/eq.cpp` in one unit.
Pure move — no behavior change, no interface change, no reformatting.

## Scope (original refs @ a0c02cd; F01+F02 removed 3,216 lines above some of
these so positions have shifted. Enumerate by NAME.)

Three new files under `src/client/`:

**eq_ucs.cpp** (~240 lines) — the UCS chat-server connection cluster
(orig 10075–10313): `ConnectToUCS`, `UCSOn*`, `UCSSend*`, `UCSProcess*`.
Note: a commented-out `ConnectToUCS` block may sit near the old F01 seam —
moved code is the LIVE definitions; leave dead commented code where it lies.

**eq_guild.cpp** (~225 lines) — the guild cluster (orig 15801–16026):
guild invite/accept/remove/demote/leader/MOTD senders plus the guild
Process* packet handlers (~13 methods, names contain `Guild`).

**eq_gm.cpp** (~190 lines) — the GM command cluster (orig 16977–17169):
GM zone request/summon/goto/find/kick/kill/hideme/emotezone/lastname senders
plus their Process* handlers (~12 methods, names contain `GM` or are in the
contiguous GM block — enumerate by reading the block).

## Steps

1. Locate each cluster by grep; confirm contiguity; list the full method set
   per file in the review.
2. Create the three files: brief header comment + pruned include block each
   (network/state code — no graphics/audio headers expected).
3. Move definitions verbatim.
4. Shared file-local helpers: same rule as always — move if exclusive, else
   leave and document.
5. Add all three files to `CLIENT_CORE_SOURCES` in CMakeLists.txt.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo the two documented exceptions in
  refactor/test_baseline.txt (RDP flake: re-run isolated; ALSA
  AudioManagerTest.PreloadCommonSounds: expected-fail on this host).
- `python3 scripts/refactor/verify_move_only.py fable..HEAD` — PASS or
  documented de-static-ing only.
- `bash scripts/refactor/check_file_sizes.sh --files <abs>/eq_ucs.cpp <abs>/eq_guild.cpp <abs>/eq_gm.cpp` — ok.
- eq.cpp shrinks by ~600+ lines.

## Review

**Deviation from plan (UCS cluster): nothing to move.** The entire
`ConnectToUCS` / `UCSOn*` / `UCSSend*` / `UCSProcess*` cluster in eq.cpp
(original lines 6856–7095 in this checkout) is dead code sitting inside a
single `/* ... */` comment block ("TODO: Fix UCS implementation to use
correct EQStream API"). `include/client/eq.h` declares none of these methods
— they are pure orphaned text, not compiled. The live UCS implementation is
the separate `UCSConnection` class in `src/client/ucs.cpp` (already its own
translation unit, already listed in `CLIENT_CORE_SOURCES`). Per the plan's
own note ("moved code is the LIVE definitions; leave dead commented code
where it lies"), there is no live definition to extract, so **no
`eq_ucs.cpp` was created** and eq.cpp was left untouched in that region.
This is a documentation/plan-assumption gap, not scope creep — verified by
grepping eq.cpp and eq.h for every symbol name in the cluster before
concluding.

### eq_guild.cpp (232 lines)

Moved verbatim from eq.cpp (orig region 12583–12805, comment header through
`ZoneProcessGuildMemberAdd`), contiguous, all guild-named:

- `SendGuildInvite`
- `SendGuildInviteAccept`
- `SendGuildRemove`
- `SendGuildDemote`
- `SendGuildLeader`
- `SendGetGuildMOTD`
- `SendSetGuildMOTD`
- `ZoneProcessGuildInvite`
- `ZoneProcessGuildMOTDReply`
- `ZoneProcessGuildMemberUpdate`
- `ZoneProcessGuildMemberAdd`

(11 methods — plan estimated ~13; the actual contiguous block has 11. Other
guild-named methods — `WorldProcessGuildsList`, `ZoneSendRequestGuildTributes`,
`ZoneProcessSendGuildTributes`, `ZoneProcessGuildMOTD` — live elsewhere in
eq.cpp, are not contiguous with this cluster, and were intentionally left in
place per the plan's contiguity rule.)

No file-local helpers involved; every call is to `EverQuest` members
(`m_guild_id`, `m_character`, `m_has_pending_guild_invite`,
`m_guild_invite_from`, `m_guild_invite_id`, `AddChatSystemMessage`,
`QueuePacket`, `s_debug_level`) or free functions declared in already-included
headers (`memset`/`memcpy`/`strncpy`/`strnlen` via `<cstring>`, `fmt::format`
via `common/logging.h`, `EQT::*_Struct` via `common/packet_structs.h`).
Include block pruned to `client/eq.h`, `common/logging.h`,
`common/packet_structs.h`, `<cstring>` — no graphics/audio headers needed.

### eq_gm.cpp (200 lines)

Moved verbatim from eq.cpp (orig region 13757–13948, comment header through
`ZoneProcessGMSummon`), contiguous, all GM-named:

- `SendGMZoneRequest`
- `SendGMSummon`
- `SendGMGoto`
- `SendGMFind`
- `SendGMKick`
- `SendGMKill`
- `SendGMHideMe`
- `SendGMEmoteZone`
- `SendGMLastName`
- `ZoneProcessGMZoneRequest`
- `ZoneProcessGMFind`
- `ZoneProcessGMSummon`

(12 methods, matching plan estimate. `ZoneProcessGMTraining` at a separate,
non-contiguous location in eq.cpp is a different GM-training packet handler,
not part of this contiguous block; left in place.)

No file-local helpers involved; calls are to `EverQuest` members
(`m_zone_connection`, `m_zone_connected`, `m_character`,
`AddChatSystemMessage`, `SendSpawnAppearance`) or free functions from the
same pruned include set as eq_guild.cpp (plus `AT_GM`, declared in both
`eq.h` and `packet_structs.h`).

### Line counts

- eq.cpp: 16,460 → 16,045 lines (−415)
- eq_guild.cpp: 232 lines (new)
- eq_gm.cpp: 200 lines (new)
- eq_ucs.cpp: not created (see deviation above)

### Verifier / build summary

- `cmake --build build -j24`: `willeq_core` and `willeq` build clean.
  Several unrelated standalone tool executables (`test_animation`,
  `generate_surface_map`, `analyze_hum_model`, `extract_zone_lines`,
  `uv_analysis`, `s3d_dump`, `bsp_coordinate_finder`) fail to link with
  `undefined reference to irr::video::IdentityMaterial` from
  `constrained_texture_cache.cpp` — confirmed unrelated to this unit: none
  of these targets include any `CLIENT_CORE_SOURCES` file (they link
  Irrlicht tool code directly), and the `willeq` binary itself links clean
  standalone. Pre-existing environment/library issue, out of scope for F04.
- `ctest --test-dir build`: 1191/1192 passed, 1 failed
  (`AudioManagerTest.PreloadCommonSounds`), 10 skipped (asset-dependent, no
  symlink in this worktree). The one failure exactly matches the documented
  ALSA expected-fail in `refactor/test_baseline.txt`. No RDP flake observed
  this run.
- `verify_move_only.py 70c0115..HEAD`: PASS (no non-move changes detected —
  guild/gm code moved verbatim, only include lists and CMakeLists.txt
  changed as expected for a new-TU split).
- `check_file_sizes.sh --files eq_ucs.cpp eq_guild.cpp eq_gm.cpp`: eq_ucs.cpp
  does not exist (by design, see deviation); eq_guild.cpp (232) and
  eq_gm.cpp (200) both well under the 1,500-line ceiling — ok.

### Deviations summary

1. No `eq_ucs.cpp` created — the UCS cluster in eq.cpp is entirely dead,
   commented-out code superseded by `src/client/ucs.cpp`'s live
   `UCSConnection` class. Nothing live to move.
2. Guild cluster is 11 methods, not ~13 as estimated in the plan — the
   estimate included guild-named methods that are not contiguous with the
   main cluster; those were correctly left in place per the plan's own
   contiguity rule.
