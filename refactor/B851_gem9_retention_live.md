# B851: gem 9 follow-up — read RequireMnemonicRetention live; ninth memorised spell survives zoning

Class A, lane vm. Issue #97. Follow-up to T483 (B770, option (a) — store 9,
show/offer 8, parse no AA) and its Leg A, which had to STOP before two live
checks because T478 held the lock. This card performs both.

No C++ change. No defect found — option (a) holds live.

## AC1 — the live rule row

Under the lock (`kb-b851-gem9-retention-live`), `mysql_exec` against the live
mariadb container:

```
ruleset_id   rule_name                       rule_value   notes
1            Spells:RequireMnemonicRetention true         Enabling will require spell slots 9-12 to have the appropriate Mnemonic Retention AA learned.
```

`rule_sets` table: `ruleset_id 1 = "default"`. So the live server's default
ruleset has the Mnemonic-Retention gate ON, exactly as the eqemu source default
(`common/ruletypes.h:534`, `true`) says — the value T483 Leg A could not read.
Testcaster (level 1, `aa_exp` 0, no `character_altadv` rank for 1071) has
rank 0 of AA 1071, so slot 8 (0-based) is cast-gated for it: it may HOLD a
spell there, the server refuses to CAST from it. Memorising is not gated
(T483 §1), which is the state this card exercises.

## AC2 — slot 9 survives a zone change, read from both the client log and the DB

Character: Testcaster, `character_data.id` 67, account `willeq_test`, Wizard
level 1. Its `character_memmed_spells` had one pre-existing row (slot 0 = 288,
Minor Shielding — the B484 lane's leftover). Seeded by SQL, AFTER the
`restore_lib.sh` snapshot of all character rows:

```
INSERT INTO character_memmed_spells (id, slot_id, spell_id) VALUES (67, 8, 54);
```

spell 54 = Frost Bolt, `spells_new.classes12 = 1` (castable at level 1 by the
config's class).

The client (the tree's own build, `/home/user/src/willeq/build/bin/willeq`,
tree at `11ee81ab` — identical C++ to this tip `ac34acab`, which adds only
B484's board/log files) ran headless against the VM stack, three times:

| run | log | what happened | the load-bearing line |
|---|---|---|---|
| 1 | `refactor/B851/run1.txt` | zoned into nro (its stored zone), full session, `/camp` out | `Set 9 spell gems` |
| 2 | `refactor/B851/run2.txt` | re-login, nro, `-d 1` so the per-gem parse prints | `Gem 9: Frost Bolt (ID 54)` |
| 3 | `refactor/B851/run3.txt` | character moved to a DIFFERENT zone (zone_id 303 = causeway; nro is 34) while OFFLINE, fresh login — a genuine cross-zone transition | `Zone change:  -> causeway` … `Gem 9: Frost Bolt (ID 54)` |
| 5 | `refactor/B851/run5.txt` | re-seed, fresh login in nro; the DB is read **while the client is online** | `Gem 9: Frost Bolt (ID 54)` + online DB read |

`-d 1` matters: the per-gem "Memorized spells:" block in
`src/client/zone/zone_entry.cpp:665-674` is gated on
`EverQuest::GetDebugLevel() >= 1` (the `-d` flag), which `--log-level=DEBUG`
alone does not set — run 1 (no `-d`) shows `Set 9 spell gems` (the count, from
`SpellManager::setSpellGems`) but not the per-gem lines.

So across a real zone change, with the spell written only by SQL:
- **client log**: every run's PlayerProfile parse reads all 9 slots and names
  the ninth (`Gem 9: Frost Bolt (ID 54)`), including run 3's fresh login into a
  different zone than the one it was seeded in.
- **DB, while the client is ONLINE** (run 5, the decisive read):
  `character_memmed_spells` still holds `67 / 8 / 54` **with the character in
  world** — that is the world process's in-memory copy, not merely the row it
  loaded at login. A server that dropped gem 9 would show `67 / 0 / 288` only
  here. It is also still present after run 5's `/camp` + client exit, and after
  runs 1–3 (incl. run 1's full in-world session and camp).

Restore: the seeded row was deleted and the whole character+currency snapshot
(`restore_lib.sh`'s `eqemu_restore_snapshot`, world container stopped) came
back **byte-identical** to the first pre-test snapshot;
`character_memmed_spells` for id 67 is back to its single pre-existing row
(slot 0 = 288) and Testcaster to its original nro coordinates. See AC5.

## AC3 — pinned as a scenario? No, and here is why

Not cheap, and the pin would be weaker than the measurement it would
automate:

1. **The proof is not client-observable from inside a scenario.** The scenario
   lane asserts on what the client process can see (`IScenarioWorld`: zone
   name, buffs, entities, equipment materials). What AC2 proves is that a
   spell the CLIENT NEVER SENT lands in the client's stored gems — the
   client's own belief. A scenario whose pass condition is "my own log says
   the ninth gem is there" is the card's own "single-client self-observation
   is circular" rule applied to itself; the non-circular half is the SQL read
   of `character_memmed_spells`, which no scenario can do.
2. **The lane cannot seed it.** `scripts/scenario.sh`'s `--seed-sql` runs
   AFTER the character snapshot, so the restore takes it out again — good —
   but the character must be OFFLINE for the row to be authoritative, and the
   lane starts the client straight after seeding. Seeding into an online
   character's row races the world's write-back (B784's whole reason for
   stopping the world before restoring). The AC2 recipe (seed while offline,
   fresh login) is exactly the sequence a scenario lane does not own.
3. **The guard that DOES exist is already a ctest.** T483's
   `test_ninth_spell_gem.cpp` pins the parse and storage at unit level
   (`NinthSpellGem.PlayerProfileKeepsTheNinthMemorisedSpell`): a 9-spell
   PlayerProfile keeps spell 9. What this card adds is the LIVE half — the
   real server's `PlayerProfile` actually carries the seeded slot, and the
   live rule that gates casting it — which is what issue #97 asked for. A
   third copy of the parse logic as a scenario would be the duplication
   `test_ninth_spell_gem` was written to prevent.

The evidence is committed under `refactor/B851/` instead (the B484 precedent
for committing lane run logs).

## AC4 — does fable lose or mis-draw the ninth gem?

No, and no, per option (a):
- **not lost**: 9 slots are parsed and stored (`Set 9 spell gems`;
  `Gem 9: Frost Bolt (ID 54)`), across zones.
- **not mis-drawn**: the offer path is 8 everywhere it is visible. `/gems`
  prints exactly `[1] Minor Shielding Ready` and `[2]`–`[8] <empty>` (run 2,
  client log lines 13168-13176; identically in run 5, lines 12789-12797). The
  panel, hotkeys and `/cast` remain at 8 (T483's guards:
  `NinthSpellGem.ThePanelHoldsEightGems`,
  `TheNinthGemIsNeverCastFromTheClient`). No gem-9 FATAL/ERROR/WARN in any
  run. A level-1 character cannot have the AA anyway, so the server gate is
  moot for it — and the client would refuse the cast client-side regardless.

Nothing to file as a NEW defect.

## AC5 — stack down, lock released

After the restore (byte-identical), `down.sh` brought the stack down; the
lock was released. `eqemu_lock_show` printed nothing (free).

## Working-rule notes

- **Board strike**: B851 has no `refactor/BOARD.md` entry to strike — the
  orchestrator filed it straight to GitLab (#97, 2026-09-18;
  `refactor/BOARD.md` line "B849 (issue #91), B850 (#92) and B851 (#97) were
  filed straight to GitLab issues … have no board entry"). Nothing to edit.
- **RED-first**: no production code changed, so there is no red→green; the
  card is the live check itself. The pre-existing "owed" state is Leg A's STOP
  (T483 doc: "Still owed live, under a lock: the rule_values read, and a
  character whose profile holds a ninth spell keeping it across a zone") —
  this doc is the closure of that line.
- **The GM zone path needs a GM login.** `/gmzone` from a non-GM character is
  refused at the login-time GM-mode gate (`ActionDispatcher::checkGMMode`),
  so the zone change was done the lane-standard way: edit `character_data`
  while the character is offline, then fresh login (the same offline-edit
  discipline the equipment-capture rig documents). Testcaster is
  `gm = 0` / account `status = 200`.
- **`character_data.zone_id` is `zone.zoneidnumber`**, not `zone.id`:
  zone_id 34 = nro, 303 = causeway, 2 = qeynos2. (Run 3's "303" was causeway,
  not qeynos2 — still a genuine cross-zone change from nro.)
