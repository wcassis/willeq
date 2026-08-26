# T256 — /who anon / linkdead / guild lines verified live (T224 owed)

Slot: slot1. x86. OWNS eqemu (noclobber) — two clients. Class B (verify;
fix only what the live lines refute). Territory: src/client/who_response.*
+ its test, `ZoneProcessWhoAllResponse` in eq_player_info.cpp (formatting
only), tests. Do NOT touch inventory (T255 slot3), combat/eq_movement
(T254 slot2).

## Owed (T224 Review)
T224 verified the 47-byte WhoAllPlayerPart layout and the plain-player
line live, but anon (format 5024 / 5022 for GM viewers), linkdead
(`unknown80[1]=12313`), guild (rankstring/guild cstr) and roleplay lines
were exercised by unit tests only. The `%n` argument order for those
formats was inferred from template shapes.

## Scope
1. Stage: Oraclerogue `/anon` on; a guild for Testchar via DB (guilds +
   guild_members rows, documented + restored) or `#guildcreate` if the GM
   command exists; linkdead by SIGKILL-ing the observer client mid-session
   (eqemu marks LD for ~90 s).
2. Testchar (GM, status 200) `/who all` after each stage; a NON-GM viewer
   too if a third account exists (willeq_rogue is status 250 — check; if
   no non-GM account, note it). Compare each printed line to the official
   template with the %n slots; fix `formatWhoAllEntry`'s args switch where
   the live line proves the order wrong (red/green on the constructed
   packet).
3. Restore everything (anon off, guild rows, positions).

## Verification
Full ctest if code changed; Review with the live lines verbatim; evidence
review/T256/. Commit: `T256: /who anon, linkdead and guild lines verified live` (+ fixes if any).

## Review (executor, slot1, 2026-08-26 04:15-04:50 UTC)

Lock taken with noclobber (exit 0), `up.sh befallen`, two headless x86
clients (`--no-graphics --log-level=INFO --script`), `down.sh`, lock
deleted. Evidence: `/mnt/projects/fable/review/T256/` (README there).

### Viewers and stages actually used
- **GM viewer**: Testchar (willeq_test, status 200, `gm` flag 0 so no rank
  string is ever sent for it).
- **Non-GM viewer: AVAILABLE** -- Testbuddy on `willeq_buddy` (status **0**,
  char 68, already in zone 36 and already a member of guild 1
  `<TestGuild133>` rank 3). No Oraclerogue needed (willeq_rogue is 250, a
  GM; and a 250 viewer would only ever get 5022 anyway).
- Guild for Testchar: `/say #guild set Testchar 1` in-game (restored with
  `#guild set Testchar 0` + a DB DELETE, see Restores).
- Anon / roleplay: `/anon`, `/roleplay on|off` from each client.
- Linkdead: SIGKILL of the Testbuddy client at 04:33:08, `/who all` from
  Testchar 47 s later.

### Live lines, verbatim (run 2, after the fixes; run 1 lines identical
except for the two defects below)

Non-GM (Testbuddy) viewing Testchar, roleplay + guild -- format **5023**
`%T1[ANONYMOUS] %2 %3 %4`, filter `/who all Testchar`:

    [ANONYMOUS] Testchar <TestGuild133>

Non-GM viewing Testchar, anon + guild -- format **5024** `%T1[ANONYMOUS] %2 %3`:

    [ANONYMOUS] Testchar <TestGuild133>

Non-GM viewing Testchar anon, no guild (run 1): `[ANONYMOUS] Testchar`.
Non-GM, unfiltered `/who all` while Testchar is anon: the server SKIPS him
(clientlist.cpp:782, `Anon()==1 && Admin()>=admin && whomlen==0`) and answers
`There are no players in EverQuest that match those who filters.`; with the
name filter he appears. Both the way eqemu intends.

Non-GM viewing itself, normal -- **5025**, no pid trailer (pidstring only for
admin >= GMAdmin):

    [1 Magician] Testbuddy (Dark Elf) <TestGuild133> ZONE: befallen

GM (Testchar) viewing Testbuddy anon (run 1) -- **5022**
`%T1[ANON (%2 %3)] %4 (%5) %6 %7 %8`:

    [ANON (1 Magician)] Testbuddy (Dark Elf) <TestGuild133> ZONE: befallen

GM viewing Testbuddy normal (run 2) -- 5025 with the 5003 trailer:

    [1 Magician] Testbuddy (Dark Elf) <TestGuild133> ZONE: befallen (USER willeq_buddy: PID 207)

**Argument order verdict**: every line matches the T224-inferred order
(5025/5022: rank, level, class, name, race, guild, zone, linkdead[, pid];
5024: rank, name, guild; 5023: rank, name, guild, zone). Nothing in the args
switch was wrong. Caveat: 5023's `%4` (zone) is never populated for the
viewer that gets 5023 (world zeroes `zonestring` unless the viewer outranks
the anon player, and a viewer who outranks gets 5022 instead), so `%3`/`%4`
order for 5023 is confirmed only as "guild lands in a printed slot".

### Two defects the live lines DID refute (both fixed, red/green)

1. **Phantom `[0 Class0] (Race0)` line** when a GM `/who all`s while itself
   anon or roleplay (run 1, 4 occurrences). world's SendWhoAll COUNTS an anon
   player whose admin equals the viewer's (line 676) but SKIPS writing the
   entry (line 782/786) -- the packet says "2 players" and carries 49 zeroed
   bytes where the entry would be. `ZoneProcessWhoAllResponse` now skips
   entries with `formatString == 0`; the server's count line is kept as sent
   ("There are 2 players in EverQuest." with one line above it -- that is
   what eqemu sends, and a real Titanium client sees the same packet).
   Test `WhoResponseHandlerTest.ZeroPaddedEntryFromSkippedPlayerIsNotPrinted`
   (red 21/22 -> green 22/22). Live after fix: run 2 04:46:38, one entry line
   + "There are 2 players".

2. **Every string-database line ended in a carriage return** (`cat -A`:
   `ZONE: befallen^M (USER willeq_test: PID 207)^M^M`, `There are 2 players in
   EverQuest.^M`). ALL 5901 eqstrings, 8305 dbstrings and 470 login_strings
   rows in `data/willeq.db` end in `\r` (CRLF source, builder keeps it); the
   legacy text-file loader strips it, `WilleqDatabase::getString/
   getDbString/getLoginString` did not. **Deviation (out of the plan's file
   territory, flagged)**: fixed at the source in
   `src/client/willeq_database.cpp` (`colTextNoCR`) rather than in the who
   formatter, because it is every SQLite-served message, not a /who thing.
   Test `WilleqDatabaseStringTest.TrailingCarriageReturnIsStripped` (red ->
   green, 6/6). Live after fix: no `^M` anywhere in run 2's `cat -A`.

### Linkdead: NOT REACHABLE live on this server
`world/clientlist.cpp:854` hardcodes `unknown80[1]=0xFFFFFFFF`; only the
zone's `EntityList::ZoneWho` (entity.cpp:5011) ever writes 12313, and
Titanium never reaches it (titanium.cpp forces type=3 -> world). Observed:
47 s after SIGKILL world still listed Testbuddy as a normal online entry
(run 1, 04:33:55). The ` <LINKDEAD>` slot stays unit-test-only
(`GmRankPrefixesAndLinkdeadSuffixes`); its `%8` position is the same slot
5022/5025 fill and cannot be exercised through this eqemu.

### Incidents / side findings
- Two willeq processes started in the same second: one failed to open
  `data/willeq.db` with "missing metadata table" (sqlite prepare failed
  while the other process held the file open). Staggering the launches by
  3 s avoided it. Not investigated further (not this unit).
- Run 1's non-GM client was SIGKILLed with its stdout fully buffered (file,
  not tty), so its last `/who all Testchar` output was lost (log mtime
  04:32:30, kill 04:33:08). Re-captured in run 2 with `stdbuf -oL` + `/quit`.
- `#guild set Testchar 0` printed "is now Guildless" but the `guild_members`
  row stayed in the DB; run 2's `#guild set Testchar 1` then reported
  "failed to be put into" while world still treated him as a member. Server
  side; the row was deleted by hand at restore.

### Restores (all verified, `review/T256/db_restore.txt`)
- `guild_members` char 65 row DELETED (did not exist before).
- Testbuddy (68): x=0 y=0 z=3 heading=0 anon=0 (staged to Testchar's
  coordinates for the run).
- Testchar (65): zone 36, -77.9232/21.9086/3, h190, anon=0.
- Server down.sh, lock removed.

### Verification
| step | result |
|---|---|
| `cmake --build build -j24` | clean |
| ctest | **2941/2942**, sole failure `AudioManagerTest.PreloadCommonSounds` (ALSA, documented) -- `ctest_full.txt` |
| `check_file_sizes.sh fable` | 5 files, 0 fail (1 pre-existing warn) |
| gles2 / ARM / visual gate | not run -- the plan narrows to "full ctest if code changed"; no renderer or loader files touched |
