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

**NOT RUN — blocked on the eqemu lock.** `/mnt/projects/fable/.eqemu-lock`
was held by `T296 2026-08-27` (written 06:02:55Z, one minute before this unit
tried to take it) with `./bin/world`, `./bin/zone arena` and the zone fleet
live. Per `refactor/EXECUTOR_PROTOCOL.md` a non-zero `noclobber` take means
another session owns the server: STOP, never overwrite. Steps 2-5 and the
verdict on Will's condition remain owed and need a re-dispatch once the lock
is free.
