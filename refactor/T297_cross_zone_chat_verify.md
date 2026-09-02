# T297 — is zone-wide and cross-zone chat actually working? (Will's UCS deferral is conditional on it)

Slot: first free. x86. OWNS eqemu (noclobber) — needs TWO clients in TWO
DIFFERENT zones. Class B, verification. **No client code change expected**;
if a channel is broken, report it as a defect with evidence and STOP — the
fix is a separate unit.

## Why this unit exists
Will deferred the UCS/chat-server work *"as long as zone-wide and cross-zone
communication is working."* **That condition has never been tested.** Every
two-client chat probe in the record put both clients in the SAME zone:
T222 (`/reply`, arena), T243 (`/emote`, befallen), T239 (hotbar macro `/say`,
befallen), T256 (`/who`, befallen). Cross-zone delivery is unverified.

Context that matters: `ConnectToUCS(...)` is commented out
(`eq_login_world.cpp:834`), so anything eqemu routes through UCS cannot work.
In eqemu, zone-local channels (say/shout/ooc/auction/emote) are handled by the
zone server, and cross-zone tells are routed by WORLD — but which channels
depend on UCS on THIS server build is a question to answer from the source
(`/mnt/projects/akk-stack/code`: `zone/client_packet.cpp`
Handle_OP_ChannelMessage, `world/`, and the UCS service), not from assumption.

Also relevant: **T243 fixed `ZoneSendSetServerFilter`, which had been telling
the server to HIDE every filtered message class** — so any pre-T243 chat
observation is void. Re-establish from scratch.

## Scope
1. **Read the server first.** For each channel — say, shout, ooc, auction,
   emote, tell, guildsay, group — determine from eqemu's source who routes it
   (zone / world / UCS) and therefore which SHOULD work cross-zone with UCS
   absent. Write that table before running anything; it is the prediction the
   run tests.
2. **Same-zone control leg.** Both clients in one zone, every channel, confirm
   each is received. Anything failing here is a plain defect, not a UCS issue.
3. **Cross-zone leg.** Testchar and a second character in DIFFERENT zones
   (use `/say #zone <zone>` per the standing trap — a DB `zone_id` edit leaves
   COORDINATES stale). Every channel again. Record received / not received per
   channel per leg, verbatim from the OBSERVER's log — never from the sender's
   self-echo (T172's lesson: an ack line is not delivery).
4. **Guild and group cross-zone**: a guild needs `guild_members` rows (T256
   staged some — document and restore); a group needs both characters grouped
   before they separate.
5. Report the matrix against the step-1 prediction. Where they disagree, say
   so — a channel that works when the source says it should not is as
   interesting as the reverse.

## Deliverable
A table: channel | routed by | same-zone | cross-zone | matches prediction.
Plus a plain verdict on **Will's condition**: is zone-wide and cross-zone
communication working? If any channel fails, that failure is the finding —
do not fix it here.

## Verification
No source change expected, so no ctest/gate unless something is fixed. Review
in this file; evidence review/T297/ with both clients' logs. Commit:
`T297: cross-zone chat matrix`.

---

## Step 1 — the source-based prediction (COMPLETE, no server needed)

Read from `/mnt/projects/akk-stack/code` at the checked-out revision, plus the
live `rule_values` table (read-only query; no server state touched).

### Where each channel is routed

`Client::Handle_OP_ChannelMessage` (`zone/client_packet.cpp:4558`) does nothing
but forward to `Client::ChannelMessageReceived` (`zone/client.cpp:1201`), whose
`switch(chan_num)` is the whole routing decision. Channel numbers are
`common/eq_constants.h:950`.

| chan_num | channel | what the switch does | leaves the zone? |
|---|---|---|---|
| 8 `Say` | say | `entity_list.ChannelMessage(...)` | no |
| 3 `Shout` | shout | `entity_list.ChannelMessage(...)` | no |
| 5 `OOC` | ooc | `RuleB(Chat, ServerWideOOC)` ? `worldserver.SendChannelMessage` : `entity_list.ChannelMessage` | **yes**, rule is `true` |
| 4 `Auction` | auction | `RuleB(Chat, ServerWideAuction)` ? `worldserver.SendChannelMessage` : `entity_list.ChannelMessage` | **yes**, rule is `true` |
| 7 `Tell` | tell | `worldserver.SendChannelMessage(this, target_name, ...)` | **yes** |
| 0 `Guild` | guild | `worldserver.SendChannelMessage(..., GuildID(), ...)` | **yes** |
| 2 `Group` | group | `Group::GroupMessage` — local members directly, **plus** `ServerOP_OOZGroupMessage` to world (`zone/groups.cpp:880`) | **yes** |
| 22 `Emotes` | (not used by Titanium) | rebroadcast as `OP_Emote` to close clients | no |
| 20 `UCSRelay` | (not used by Titanium) | `UCSConnection::SendMessage` | UCS |

`entity_list.ChannelMessage` (`zone/entity.cpp:1305`) walks only that zone's
`client_list`. **Only `Say` is range-limited** there (`Distance < 200`); shout
and non-server-wide ooc/auction are zone-wide but zone-bounded.

**/emote does not use OP_ChannelMessage at all on Titanium** (T243): the client
sends `OP_Emote`, and `Client::Handle_OP_Emote`
(`zone/client_packet.cpp:6211`) rebroadcasts via `QueueCloseClients(...,
RuleI(Range, Emote) = 135, ..., FilterSocials)`. So emote is not merely
zone-local, it is *radius-135* local.

### Does anything a Titanium client can send depend on UCS?

**No.** In `world/zoneserver.cpp:423` (`ServerOP_ChannelMessage`) the *only*
branch that reaches `UCSConnection::Instance()->SendMessage` is
`scm->chan_num == ChatChannel_UCSRelay` (20), the Underfoot+ relay channel.
Everything else is dispatched by world itself:

- `Tell` / `TellEcho` → `ClientList::FindCharacter(scm->deliverto)` → that
  character's zone server (`cle->Server()->SendPacket(pack)`), with offline /
  zoning-queue / not-contactable fallbacks.
- `Guild` → `ZSList::SendPacketToZonesWithGuild(scm->guilddbid, pack)`.
- `OOC` / `Auction` / `Broadcast` → `ZSList::SendPacket(pack)` — every zone.
- `GMSAY` → `ZSList::SendPacketToZonesWithGMs(pack)`.

Zones re-emit these through `EntityList::ChannelMessageFromWorld`
(`zone/entity.cpp:2275`), which re-checks guild membership and the
`FilterGuildChat` / `FilterOOC` client filters — the filters T243 fixed.

So `ConnectToUCS(...)` being commented out at `eq_login_world.cpp:834` should
cost this client **nothing** on any channel it can actually speak. UCS on a
Titanium server covers only player-created chat channels (`/join`, `/1`),
mail, and the Underfoot+ relay — none of which the client implements.

### Gates that could produce a false negative

Checked against the live `rule_values` so a blocked message is not mistaken
for a routing failure:

- `Chat:GlobalChatLevelLimit` = **1** (default is 8). The karma gate in the
  ooc / auction / tell cases (`TotalKarma < KarmaGlobalChatLimit` *and*
  `GetLevel() < GlobalChatLevelLimit`) therefore cannot fire — every character
  in `character_data` is level >= 1. Had the default stood, Testchar (level 2)
  and Oraclerogue (level 9) would have split on it.
- `Chat:ServerWideOOC` = `true`, `Chat:ServerWideAuction` = `true`.
- `Chat:EnableAntiSpam` = `true`, `MinimumMessagesPerInterval` = 4 over a
  60 s interval, `MinStatusToBypassAntiSpam` = 80/100. **This is the real
  trap**: shout / auction / ooc / tell share one 4-messages-per-60 s budget
  for a non-GM. Testchar is a GM (bypasses); the second character must either
  be given status or the run must pace those four channels.
- `worldserver.IsOOCMuted()` — must be confirmed off in the run.
- Guild chat needs a `guild_members` row *and*
  `guild_mgr.CheckPermission(..., GUILD_ACTION_GUILD_CHAT_SPEAK_IN)`. Live
  `guild_members` today: char 66 (Testmelee, rank 1) and char 68 (Testbuddy,
  rank 3) in guild 1 `TestGuild133`. Neither test client character (65
  Testchar, 81 Oraclerogue) is a member — rows must be staged and restored.

### The prediction

| channel | routed by | same-zone predicted | cross-zone predicted |
|---|---|---|---|
| say | zone (`entity_list`, range 200) | RECEIVED (if within 200 units) | NOT RECEIVED — *by design* |
| shout | zone (`entity_list`, zone-wide) | RECEIVED | NOT RECEIVED — *by design* |
| emote | zone (`OP_Emote`, range 135) | RECEIVED (if within 135 units) | NOT RECEIVED — *by design* |
| ooc | world (`ZSList::SendPacket`, all zones) | RECEIVED | **RECEIVED** |
| auction | world (`ZSList::SendPacket`, all zones) | RECEIVED | **RECEIVED** |
| tell | world (`ClientList::FindCharacter` -> target zone) | RECEIVED | **RECEIVED** |
| guild | world (`SendPacketToZonesWithGuild`) | RECEIVED | **RECEIVED** |
| group | zone locally + world (`ServerOP_OOZGroupMessage`) | RECEIVED | **RECEIVED** |

"NOT RECEIVED cross-zone" for say/shout/emote is the *correct* EverQuest
behavior, not a defect — those are zone-local channels in the real game too.
Will's condition ("zone-wide and cross-zone communication is working") should
therefore be read as: say/shout/emote work **zone-wide**, and
ooc/auction/tell/guild/group work **cross-zone**. That is the pass criterion
the live legs test.

### Status of the live legs

**RUN 2026-09-02** (the 2026-08-27 attempt was blocked on T296's lock; that
paragraph is superseded). Lock taken with `noclobber`, exit 0. Evidence:
`/mnt/projects/fable/review/T297/` (README there).

---

## Review (executor, slot1, 2026-09-02 04:45-05:10 UTC)

Branch `task/T297` off `fable` `2eb34f3d` ("Merge branch 'task/T311-legB'").
**No client code changed** — this is a verification unit and the plan's STOP
condition ("if a channel is broken, report it as a defect and STOP") was hit.

### Setup and deviations

- **The server was already running with NO lock held** (launcher fleet up
  since 02:52 UTC: `world`, `ucs`, 14 static zones + ~12 dynamic slots). This
  is the trap recorded this week. Per the trap's own note, `up.sh` was NOT
  run on top of it; the running fleet was used as found, having first
  confirmed exactly one `loginserver` and one `world`. `down.sh` at the end,
  lock deleted.
- Characters: **A = Testchar** (willeq_test, acct status 200, char 65) is the
  SENDER; **B = Oraclerogue** (willeq_rogue, acct status **250**, char 81) is
  the OBSERVER. Every assertion below is read off B's log except the two
  B->A probes, which are read off A's.
- **Anti-spam is a non-issue here**, correcting this plan's own step-1 note
  and the old `review/T297/README.md`: `MinStatusToBypassAntiSpam` is 80
  (default ruleset) / 100 (the active override), and *both* accounts are
  200/250. Neither character is rate-limited. `Chat:GlobalChatLevelLimit` = 1,
  `ServerWideOOC` / `ServerWideAuction` both `true`, confirmed by live query.
- Movement was by **`/say #zone <shortname>`** and `#summon`, never a DB
  `zone_id` edit (the standing trap). `#gm on` was issued on Testchar first.
- Guild staged in-game with `/say #guild set <name> 1` (T256's method), rows
  deleted at restore.
- Group formed in-zone with `/invite` + `/groupaccept` **before** anyone
  separated, exactly as the plan required.
- Same-zone leg was run at **distance 0** — `#summon` put both at
  `(-82, 35, 3)` in befallen (`/loc` on both clients) — so neither the
  say-200 nor the emote-135 radius can be confused with a routing result.

### The measured matrix

Cross-zone leg: A in `qeynos`, B in `befallen` (run 1); and the mirror, B in
`qeynos`, A in `befallen` (run 2). Verbatim observer lines in
`review/T297/README.md`.

| channel | routed by | same-zone | cross-zone | matches prediction |
|---|---|---|---|---|
| say | zone (`entity_list`, range 200) | **RECEIVED** | not received *(by design)* | YES |
| shout | zone (`entity_list`, zone-wide) | **RECEIVED** | not received *(by design)* | YES |
| emote | zone (`OP_Emote`, range 135) | **RECEIVED** | not received *(by design)* | YES |
| ooc | world (`ZSList::SendPacket`) | **RECEIVED** | **RECEIVED** | YES |
| auction | world (`ZSList::SendPacket`) | **RECEIVED** | **RECEIVED** | YES |
| tell | world (`ClientList::FindCharacter`) | **RECEIVED** | **RECEIVED** | YES |
| guild | world (`SendPacketToZonesWithGuild`) | **RECEIVED** | **RECEIVED** | YES |
| group | zone + world (`ServerOP_OOZGroupMessage`) | **RECEIVED** | **NOT RECEIVED** | **NO** |

Both directions were exercised: A->B on every row, plus B->A `tell` and `ooc`
cross-zone (run 1, seen on A at 04:55:46 / 04:55:54) and B->A `gsay`
cross-zone (run 2, not received). The tell round trip therefore proves
world's `FindCharacter` -> destination-zone route in both directions.

**The step-1 prediction was right on 7 of 8 rows, and right about UCS.**
`ConnectToUCS(...)` being commented out at `eq_login_world.cpp:833-834` cost
this client **nothing**: ooc, auction, tell and guild — the four channels
world dispatches — all crossed zones with the client's UCS connection absent,
and the server's `bin/ucs` process was running the whole time and was never
in the path.

### DEFECT 1 (blocking) — the group is DISBANDED when either member zones

Group chat did not fail because of chat routing. It failed because there was
no group left to route to.

- Run 1: Testchar (the **leader**) `#zone`d befallen -> qeynos at 04:53:49.
  B's log, same second: `Your group has been disbanded` / `[GROUP] Not in
  group`. A's subsequent `/gsay T297-GROUP-CROSSZONE` produced no delivery
  and no server echo.
- Run 2 (the isolating leg): the **non-leader** zoned instead. Same result,
  mirrored — A (the leader, who never moved) logged `Your group has been
  disbanded` at 05:04:21, the instant B `#zone`d. So it is **not** about who
  the leader is.

Server-side proof, `logs/zone/befallen_..._353.log` (container clock is UTC-5;
23:53:49 CDT == 04:53:49 UTC). Immediately after the ZoneToZone approval the
**source zone** emits two `OP_GroupUpdate` packets, one per member, whose
first dword is `06 00 00 00` — `groupActDisband`
(`common/eq_packet_structs.h:2478`):

    [Zone] [Packet S->C] [OP_GroupUpdate] [0x2dd6] Size [454]
       0: 06 00 00 00 54 65 73 74 - 63 68 61 72 00 00 00 00  | ....Testchar....
    [Zone] [Packet S->C] [OP_GroupUpdate] [0x2dd6] Size [454]
       0: 06 00 00 00 4F 72 61 63 - 6C 65 72 6F 67 75 65 00  | ....Oraclerogue.

**There is no client->server group packet anywhere near the transition** —
the zone's full `OP_Group*` packet list for the session shows only the
`OP_GroupInvite` / `OP_GroupFollow` of group formation, and then these
server-originated disbands. The client did not ask for this.

How far the mechanism was narrowed (from `/mnt/projects/akk-stack/code`):

- `Group::MemberZoned` (`zone/groups.cpp:547`) — the intended zone-out path —
  does **not** disband; it just nulls the member out of `members[]`.
- The only eqemu path that turns a two-person group into a `DisbandGroup()`
  is `Client::LeaveGroup()` (`zone/groups.cpp:1258`): `if (MemberCount < 3)
  g->DisbandGroup();`. That fits both runs exactly (the group was 2 members
  in both).
- On a zone-out, `LeaveGroup()` is reachable only where `bZoning` is false —
  `client_process.cpp:639` (`if (!bZoning) { ... LeaveGroup(); }`) and
  `~Client()` (`client.cpp:766`, `if (isgrouped && !bZoning && is_zone_loaded)`).
  `Handle_OP_ZoneChange` sets `bZoning = true` and only `SendZoneCancel` /
  `SendZoneError` clear it; neither ran (the zone log shows
  `response [1]`, `success [1]`).

So the source zone behaved as though the client had logged out rather than
zoned, even though the OP_ZoneChange handshake visibly succeeded. **Which
side is at fault is NOT settled by this unit.** Two things kept it open:
neither observer log carried the `"%s logged out."` / `"%s left the zone."`
`MessageGroup` line that would discriminate the two branches
(`client_process.cpp:636/655`), and the zone log shows the
`ServerOP_ZoneToZoneRequest` response handler running **twice** for a single
client OP_ZoneChange (two `ZoneToZone ... response [1]` lines, two
`OP_ZoneChange` S->C) — unexplained, and worth a look by whoever fixes this.
The clean way to settle it is the eq-oracle official client doing the
identical `#zone` while grouped: if the official client's group survives, it
is ours. **Not fixed here — separate unit.**

### DEFECT 2 (client, non-blocking) — phantom group state survives the disband

The client does not end up agreeing with the server about the group.

- Run 1: A was mid-zone-transition when the disband was queued and **never
  processed it** (`grep -c disbanded run1_A_testchar.log` = 0). On arriving in
  qeynos it reported `[GROUP] In group (2 members), leader: ` — inGroup true,
  member count 2, **leader name empty** — and then happily sent `/gsay` into
  a group that no longer existed, with no error shown to the player.
- Run 2: the mirror. B, the client that zoned, still reported `[GROUP] In
  group (2 members), leader: Testchar` at 05:04:24, three seconds after the
  server had disbanded the group.

Either way the client shows a group that is not there. There is no group-state
re-sync on zone-in; whatever `GroupState` held before the transition is
carried across unverified. Fixing Defect 1 does not by itself fix this — a
disband delivered while the client is between zone servers will always be
lost, so zone-in needs to re-establish group state from the server rather
than trust the pre-zone copy.

### DEFECT 3 (cosmetic) — the two chat-channel name tables have drifted apart

`src/client/eq_chat.cpp` builds the channel name twice in one function,
40 lines apart, from two hand-written ternary chains:

- the console `[CHAT]` line (line ~119) omits `CHAT_CHANNEL_AUCTION`, so every
  auction message printed as `[CHAT] Testchar (chan4): ...` in both runs;
- the bridge `ChatMessageData::channelName` (line ~141) **does** include
  auction, so the UI and the console disagree on the same packet;
- neither knows channel **14** (`ChatChannel_TellEcho`, the server's
  confirmation of an outgoing tell), which printed as `(chan14)`.

This is the duplicated-then-drifted pattern, not a routing problem — the
messages all arrived. One shared lookup would close it.

### Verdict on Will's condition

Will deferred UCS *"as long as zone-wide and cross-zone communication is
working."* Read as the plan defines it (say/shout/emote work **zone-wide**;
ooc/auction/tell/guild/group work **cross-zone**):

> **The condition holds for 7 of the 8 channels, and it holds for the reason
> Will cares about: nothing a Titanium client can say needs UCS.** Zone-wide
> chat is fully working. Cross-zone ooc, auction, tell and guild are fully
> working in both directions with the UCS connection commented out.
>
> **It does not hold for group chat** — and the cause is not chat at all. The
> group itself is destroyed by the server the moment anybody zones, so *no*
> group feature survives a zone transition, chat included. **Leaving UCS
> deferred is still the right call; this is a grouping defect, not a UCS
> one.** It needs its own unit, and that unit should start by deciding
> whether the zone-out handshake or eqemu is at fault (see Defect 1).

### Verification

No source change, so the plan's narrowed bar applies: no build, no ctest, no
visual gate, no `check_file_sizes.sh` (nothing to check).

| step | result |
|---|---|
| `cmake --build build -j24 --target willeq` | EXIT=0 (needed to run the branch's client) |
| live run 1 (leader zones) | A exit=0, B exit=0 |
| live run 2 (non-leader zones) | A exit=0, B exit=0 |
| DB restore | `diff db_before.txt db_restored.txt` **empty** |
| eqemu `down.sh` | EXIT=0, 0 zone processes remain, lock deleted |

Restores: `guild_members` rows for chars 65 and 81 deleted (staged this unit;
the before-state was chars 66 and 68 only). Testchar -> zone 2,
`-74 / 428 / 3.751`, h160. Oraclerogue -> zone 36, `-259 / -1201 / -5.75`, h0.
Testchar `gm` flag reset to 0 (this unit set it with `#gm on`).
**Deviation, flagged:** Oraclerogue's persisted `gm` flag was found at 1 and
left at 1 — no command this unit issued could have set it, and there is no
recorded prior value to restore to. `Oracledwarf` (char 82) untouched.
