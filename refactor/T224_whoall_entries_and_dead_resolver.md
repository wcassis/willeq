# T224 — /who prints entries, not just a count; delete dead WldLoader::resolveTextureName

Slot: slot3. x86. NO eqemu at start (T220 holds it); code+tests first. Class A.
Do NOT touch include/client/bridge/*, src/client/bridge/*, eq_bridge.cpp
(T220), or eq_entities.cpp / eq_merchant.cpp / spell_manager.cpp / combat.cpp
(T221 stage-2 territory, about to dispatch).

## Defect 1 — /who (T218 side-finding)
`ZoneProcessWhoAllResponse` (src/client/eq_player_info.cpp) prints only the
player count and never parses the entries, so `/who` shows nothing useful.
eqemu's WhoAllReturnStruct / WhoAllPlayerPart layout: check eqemu's
common/eq_packet_structs.h if present under /mnt/projects, else the T178
write-up (`refactor/T178_*.md`, which fixed the `%1` count substitution) and
any capture in tests/captured_packets.h or data/captures. Parse each entry
(name, level, class, race, zone/guild as the struct provides, GM/anon/LFG
flags) and print lines in the official client's format using eq_strings
where a string id exists (T178 found the count string; the per-line
formatting strings are adjacent — look at 5xxx range near it). Test with a
constructed multi-entry packet (fixed bytes in the test) and, if a real
capture exists, with that too. Red/green measured.

## Defect 2 — dead resolver
`WldLoader::resolveTextureName` has exactly two hits (decl + def), zero
callers (testing_progress.md finding). Delete it. If per-mesh bbox header
fields in the same file are read by nothing (same finding), delete those
too — verify with grep first, keep the parse offset arithmetic correct.

## Verification
Full ctest; visual gate (defect 2 touches the WLD loader — goldens must be
byte-identical); Review in this file; evidence review/T224/. Live leg for
/who: only if the eqemu lock is free when you get there; otherwise report
pending. Commit: `T224: /who lists the returned players; dead WLD resolver deleted`.

## Review (executor, slot3, 2026-08-25)

### Defect 1 — /who entries — FIXED, red/green measured

**Struct layout used** (source: `/mnt/projects/akk-stack/code/common/eq_packet_structs.h`
`WhoAllReturnStruct` + `WhoAllPlayerPart1..4` under `#pragma pack(1)`, and the
two senders `zone/entity.cpp EntityList::ZoneWho` and
`world/clientlist.cpp ClientList::SendWhoAll/SendFriendsWho`, which size the
packet as `64 + 47*entries + strlen(name) + strlen(guild)`):

| offset | field | values eqemu writes |
|---|---|---|
| header 0..63 | `WhoAllReturnHeader` (already in `packet_structs.h`) | `playerineqstring`=5001, `line`="---…", `playersinzonestring`=5028/5036/5029, `playercount` |
| +0 | u32 formatstring | 5025 normal / 5024 anon / 5023 roleplay |
| +4 | u32 pidstring | 0xFFFFFFFF |
| +8 | cstr name | |
| | u32 rankstring | 12312 GM / 12315 trader / 6056 buyer / 0xFFFFFFFF |
| | cstr guild | `<Name>` or "" |
| | u32 unknown80[2] | [0]=0xFFFFFFFF, [1]=12313 linkdead or 0xFFFFFFFF |
| | u32 zonestring | 5006 (world) / 0xFFFFFFFF (zone-local) |
| | u32 zone, class, level, race | zeroed for anon |
| | cstr account | "" |
| | u32 unknown100 | 0 zone / 207 world |

47 = eleven u32s + three NUL terminators. The eqemu header's
`WhoAllPlayer` (with `char*` members) is the logical view, not the wire.

**Strings** (`/mnt/projects/EverQuestP1999/eqstr_us.txt`, all present in the
5xxx block T178 found): 5001 "Players in EverQuest:", 5006 "ZONE: %1",
5023 "%T1[ANONYMOUS] %2 %3 %4", 5024 "%T1[ANONYMOUS] %2 %3",
5025 "%T1[%2 %3] %4 (%5) %6 %7 %8 %9", 5028/5036/5029 count lines,
12312 "* GM * ", 12313 " <LINKDEAD>", 12315 " TRADER", 6056 "BUYER".
Argument positions per format are INFERRED from the template shapes and
eqemu's field comments (no client source exists to cite): 5025 → rank,
level, class, name, race, guild, zone, linkdead, account; 5024 → rank,
name, guild; 5023 → rank, name, guild, zone. Blank runs left by empty
arguments are collapsed. Flagged as a deviation-by-necessity: if the live
leg (pending) shows the official ordering differs, only the `args` switch in
`formatWhoAllEntry` changes.

**Code**: new pure TU `src/client/who_response.cpp` +
`include/client/who_response.h` (parser with byte-by-byte bounds checks,
`%Tn`/`%n` template formatter, built-in copies of the 12 strings for an
unloaded db); `ZoneProcessWhoAllResponse` rewritten to print title, rule,
entries, count line (T178's `substituteWhoCount` kept; the hardcoded
"No players found…" replaced by eqstr 5029). `ZoneLines::zoneShortNameFromId`
added (reverse of the existing name→id table) for "ZONE: %1".
`GetClassName`/`GetRaceName` reused.

**Tests** `tests/test_who_response.cpp` (18): 5 parse (47-byte fixed part,
every field of two entries, truncation keeps complete entries, unterminated
string cannot read past the buffer, zero count), 8 format, 5 end-to-end via
`ReplayZonePacket` observed as SystemMessage bridge events. **RED**: 4/18
fail on the old handler (`review/T224/red_test_who_response.txt`); **GREEN**
18/18 (`green_test_who_response.txt`). No real capture exists:
`data/captures/session.sleq` and `.pcap` contain zero
`{5023,5024,5025}+0xFFFFFFFF` byte pairs (checked).

### Defect 2 — dead resolver — DELETED; bbox fields KEPT (finding refuted)

`WldLoader::resolveTextureName` (decl `wld_loader.h`, def `wld_loader.cpp`,
body `return "";`) removed; grep confirms 0 remaining hits.

The per-mesh header bbox fields are **not** dead: `parseFragment36/2C` copy
`header.minX..maxZ` into `ZoneGeometry::minX..maxZ`, and those are READ by
`renderer_zone_load_objects.cpp:92-108` (`geom->maxX - geom->minX` for
object culling radius and `zoneBoundsMinX_ = currentZone_->geometry->minX`),
`renderer_zone_load.cpp:364-366`, `renderer_zone_stream.cpp:53-54` and
`s3d_loader.cpp:461-466,810-811`. testing_progress.md's "read by nothing in
src/" was wrong at the consumer level (it may be right that no TEST reads
them). Not deleted; parse offsets untouched.

### Verification

| step | result |
|---|---|
| 1 `cmake --build build -j24` | clean |
| 2 ctest | **2794/2795**, sole failure `AudioManagerTest.PreloadCommonSounds` (ALSA, documented) |
| 3 `build-gles2-x86.sh` | clean |
| 4 visual gate | **PASS**, goldens untouched (`git status`: only the 10 unit files) |
| 5 `build-arm-noble.sh` | clean, `build-arm-noble/bin/willeq` produced |
| 6 `check_file_sizes.sh fable` | 0 fail (new code in a new TU) |

### Live leg — PENDING, with an incident

Lock was absent at the first check; ~90s later this unit wrote its lock
file, and `ls` in the same command showed the file already existed —
another session (T222/slot2 per the board) had taken it and its containers
were 28s old. T224 overwrote that lock's contents. Corrective action: the
lock was rewritten with an incident note so the owner retains it; T224 did
NOT start, use, or stop the server. Lesson: the check-then-create is not
atomic — take the lock with `set -o noclobber` (`> file` fails if it
exists) so the check and the take are one operation. Live `/who` + `/who all`
capture is owed; script/config are staged in the scratchpad (see
`review/T224/README.md`).

## Live leg (follow-up commit, slot3, 2026-08-25 19:00-19:19 UTC)

Lock taken with `set -o noclobber` (exit 0), `up.sh befallen`, headless
Testchar (`--no-graphics --log-level=INFO --script`: wait 55, /who, /who all,
/quit), then Testchar restored (zone 36, -77.9232/21.9086/3, h190), `down.sh`,
lock deleted. Logs: `review/T224/live_who_{before,after}_followup.log`.

**What the server actually sent** (this eqemu routes every Titanium /who to
world: `titanium.cpp DECODE(OP_WhoAllRequest)` forces `type=3`, so
`world/clientlist.cpp ClientList::SendWhoAll` is the sender, not the zone's
`ZoneWho`). The parser decoded it field-for-field on the first run -- the
47-byte packed layout is confirmed live. Two things the first commit got
wrong, both found by the run:

1. **`%9` is the user/PID trailer, not a bare account.** For a GM viewer
   world sends `pidstring=5003` "(USER %1: PID %2)" with the account name
   and `unknown100`(=207) as its arguments. First run printed
   `... ZONE: befallen willeq_test`; now `... ZONE: befallen (USER willeq_test: PID 207)`.
   Also learned from the world sender: `rankstring` is 5007..5021 (GM rank
   strings) or **0**; `formatstring` 5022 "%T1[ANON (%2 %3)] %4 (%5) %6 %7 %8"
   for anon players as seen by a GM; `unknown80[0]` is the **numeric admin
   level** for GM viewers (never looked up as a string -- and never was);
   `playersinzonestring` 5033 when the list is cut at 20. Defaults for
   5003/5007-5022/5033 added to `whoDefaultString`. `%1..%8` order
   (rank, level, class, name, race, guild, zone, linkdead) is CONSISTENT with
   the live line; the linkdead slot could not be exercised (no LD player).
2. **`/who all` sent whom="all" as a name filter** -- server replied
   "There are no players in EverQuest that match those who filters." (an
   OP_FormattedMessage, `[CHAT]` in the log, not a who response).
   `cmdWho` now drops a leading case-insensitive `all` token. Second run:
   `/who all` returns the same 4 lines as `/who`.

Red/green: `Who_All_*` 2/2 red → green (`red/green_live_followup_command_processor.txt`);
`test_who_response` 2/21 red → 21/21 green (`*_followup_who_response.txt`).
One expectation was corrected during green: eqstr 5015 "* GM-Admin *" has
no trailing blank so the faithful render is `* GM-Admin *[ANON (...` -- the
strings are rendered as shipped, not prettified.

Final live output (`/who` and `/who all` identical):

    Players in EverQuest:
    ---------------------------
    [2 Warrior] Testchar (Human) ZONE: befallen (USER willeq_test: PID 207)
    There is 1 player in EverQuest.

Verification after the follow-up: ctest **2799/2800** (ALSA only), visual gate
PASS, goldens untouched, gles2 + ARM builds clean. `check_file_sizes.sh fable`
reports 1 FAIL `irrlicht_renderer.cpp` 3054 lines -- **not this branch**
(`git diff b9e38936 HEAD` on that file is empty; it grew on fable at
f3502cb3/T225 after this branch's base). Single-player only: a second
character to exercise anon/LD/guild lines live is still owed.
