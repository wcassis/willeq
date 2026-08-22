# T178 — Three T155 display defects: WhoAll %1, /loot id echo, and a stale "Total points"

**Slot: orchestrator-direct on `fable`. x86.** The three receive/display defects
T155 recorded but left unfixed (its grant was sender bodies only).

## Defect 2 — OP_WhoAllResponse never substituted `%1` — FIXED

eqemu's who header is a format string with a `%1` count placeholder ("There is
%1 player in EverQuest."). The client never filled it and appended a raw `": N"`,
so `/who` rendered:

```
There is %1 player in EverQuest.: 1
```

`substituteWhoCount` (new `who_format.{h,cpp}`, pure and unit-tested) fills every
`%1` with the count; `ZoneProcessWhoAllResponse` shows the filled string, and
falls back to appending the count only when the string carried no placeholder
(so an unexpected string never silently drops the number). Now:

```
There is 1 player in EverQuest.
```

5 tests: singular/plural substitution, no-placeholder → `substituted=false`,
the don't-rescan-inserted-digits case, and other placeholders (`%2`) left alone.

## Defect 3 — /loot refusal omitted the id — FIXED (client side)

The server's own failure, `Error: OP_LootRequest: Corpse not found (ent = 0)`
(`zone/client_packet.cpp:10310`), **hardcodes the "0"** — it is a literal, not
the id we asked for, so a decayed corpse and a malformed request are
indistinguishable. The client cannot change eqemu's string, but T178 makes our
side carry the id both ways:

- `cmdLoot`'s **own** validation refusals now quote the offending token:
  `Invalid corpse id 'notanumber' - use '/list' ...` (both the format-error and
  the range-error paths).
- On a successful dispatch the client echoes **`Requesting loot from corpse
  <id>...`**, so the server's generic "Corpse not found" that may follow is
  attributable to the id we sent.

2 new tests assert the offending token appears in the refusal; both fail against
the old code (which returned a bare "Invalid corpse id") and pass with the fix.
The existing 7 loot tests are unchanged and still green.

## Defect 4 — /find "Total points 5" — ALREADY RESOLVED, no code

T155 saw `/find` print `Total points 5` where eqemu's non-pathing reply built 2
`FindPerson_Point`s. That string **no longer exists anywhere in the tree** (0
hits across `src/` and `include/`); `ZoneProcessFindPersonReply` was since
rewritten to read only the destination point — which T155 already confirmed
decodes correctly and matches the target's real server position. The count line
that was the actual complaint is gone. No code change; the board entry was stale
and is removed, the same disposition as T177's note on `getSkillUseError`.

## Acceptance

- `test_who_format` **5/5**; `test_command_processor` loot subset **9/9** (7 old
  + 2 new), the 2 new measured red against `fable` then green with the fix.
- `willeq` main binary builds clean.
- Defect 2 verified by the unit test against T155's exact observed string
  ("There is %1 player in EverQuest." + count 1). A live `/who` re-check is
  cheap but not required for a pure, tested string substitution.
