# T269 — bank coin deposit / withdraw / convert (T264 F3)

Slot: slot2. x86. OWNS eqemu (noclobber) for a live deposit/withdraw with
DB proof. Class A. Territory: the bank-coin intents/handlers T264 KEPT
(its Review names them), CommandProcessor `/bank` family, eq_merchant.cpp
bank senders (OP_MoveCoin), tests. Do NOT touch loot (T268 slot3), race
loader (T267 slot1).

## Gap
`/bank` opens the banker window (T136); the coin move handler exists with
no producer. Titanium: OP_MoveCoin {from_slot, to_slot, cointype1,
cointype2, amount} — verify in eqemu common/eq_packet_structs.h
MoveCoin_Struct and zone/client_packet.cpp Handle_OP_MoveCoin (slot ids
for inventory vs bank coin pools, and the convert rule: 10:1 across
denominations).

## Scope
1. Commands (T156 rule): `/bank deposit <amount> <pp|gp|sp|cp>`,
   `/bank withdraw <amount> <denom>`, `/bank convert <amount> <from> <to>`
   (if convert is a client-side MoveCoin with different cointypes — verify).
   Refuse locally when the pool lacks the coins (official string if one
   exists) — send nothing.
2. Route through the kept handler hop; update `test_reachability_sweep`'s
   expected set.
3. Tests: MoveCoin bytes per command; refusal sends nothing; the local
   currency projection (GameState player().currency + bank currency)
   updates from the server's echo, not optimistically. Red/green.
4. Live: `/bank deposit 5 pp` with the banker open → DB `character_currency`
   platinum -5 / platinum_bank +5; withdraw back; `/bank` closed → refused.
   Restore rows.

## Verification
Full ctest; Review; evidence review/T269/. Commit:
`T269: bank coin deposit, withdraw and convert reachable`.

---

# Review

Executed in `/mnt/projects/fable/wt/slot2`, branch `task/T269-bank-coins`,
base fable `4d61ea70`. Evidence: `/mnt/projects/fable/review/T269/`
(README.md there indexes every file).

## 1. MoveCoin truth (verified in eqemu source, not assumed)

`zone/client_packet.cpp` `Handle_OP_MoveCoin` (drops any size != 20) ->
`zone/client_process.cpp` `Client::OPMoveCoin`:

| field | values |
|---|---|
| `from_slot`/`to_slot` | -1 destroy, 0 cursor, **1 inventory**, **2 bank**, 3 trade, 4 shared bank |
| `cointype1`/`cointype2` | **0 cp, 1 sp, 2 gp, 3 pp** (`CoinTypeCoppers`: 1/10/100/1000) |
| `amount` | int32, negative dropped as malicious |

Three findings that changed the plan as written:

1. **The plan's "convert is 10:1 across denominations" is only half the
   rule.** The server computes
   `amount_to_add = take * CoinTypeCoppers(from) / CoinTypeCoppers(to)`,
   truncating, then recomputes `amount_to_take` from that. Any pair works, in
   either direction: cp->gp is 100:1, pp->sp is 1:100. The pre-existing
   `BankCurrencyConvertIntent` hard-coded `toCoinType = fromCoinType + 1` and
   refused anything not a multiple of 10 -- correct only for the single-step
   upward case. The intent grew an explicit `toCoinType` field and the arm
   now uses the server's ratio. `/bank convert 200 cp gp` works and is pinned.
2. **Nothing comes back on success.** `OPMoveCoin` ends in `SaveCurrency()`,
   a DB write; there is no OP_MoneyUpdate/OP_BankerChange echo for a
   MoveCoin (OP_BankerChange is a *client-initiated* refresh, and its handler
   requires a banker in range). So the plan's item 3 -- "the local projection
   updates from the server's echo, not optimistically" -- is **not
   implementable as written; there is no echo to update from.** What the
   tests pin instead: the arm applies the SAME arithmetic the server applies,
   and refuses everything the server would clamp or partially apply, so the
   projection and the wire can never disagree. This is called out rather than
   silently absorbed.
3. **Either bank slot needs a banker within `USE_NPC_RANGE2`**, or the server
   returns in total silence (a POSSIBLE_HACK player-event log). The client's
   proxy for that is "the bank window is open" -- it was opened by
   `BankerInteractIntent`, which does its own distance/class check.

## 2. What was added

| layer | file | what |
|---|---|---|
| command | `src/client/action/command_processor_bank.cpp` (new) | `cmdBank`: sub-command parse, denomination names (`cp`/`copper`/... mapped straight to the wire scale), amount validation, the LOCAL pool check against GameState, the convert-multiple rule. Refusal returns `ActionResult::Failure` and dispatches nothing. |
| dispatcher | `action_dispatcher.{h,cpp}` | `bankCoinMove` / `bankCoinConvert` -- `checkZoneConnection()` then delegate, the `bankToggle()` shape |
| handler | `eq_action_handler.{h,cpp}`, `IActionHandler` | two thin overrides |
| producer | `eq_merchant.cpp` | `EverQuest::RequestBankCoinMove/Convert` -- window-open guard, then push the KEPT intent (the `RequestVendorBuy` recipe) |
| consumer | `eq_bridge.cpp` | both Bank arms rewritten: server-truth slots/cointypes, server-ratio conversion, refuse-don't-clamp, and one shared projection helper |

The old arms had three defects beyond being unreachable: the deposit arm
**sent the packet and then silently skipped the local update** when the pool
was short (wire and projection diverging); neither arm wrote the GameState
pools at all (only the `EverQuest::m_*` counters and the bridge events), so
the new `/bank` pool check would have read stale values; and the convert arm
could only go one step up. `PublishBankCoinProjection()` now writes
GameState *and* publishes, and `CurrencyChanged` is published only when the
on-hand pool actually moved (a bank->bank convert publishes only
`BankCurrencyChanged`).

## 3. Red/green (measured)

| binary | red | green |
|---|---|---|
| `test_eq_bank_coins` (new, 13 cases) | **12 FAILED** | 13/13 |
| `test_command_processor` `BankCoinCommandTest.*` (new, 17 cases) | **16 FAILED** | 388/388 whole binary |
| `test_reachability_sweep` | **2 FAILED** (`BankCurrency*Intent` producer-less; `bankCoinMove`/`bankCoinConvert` unreachable) | 5/5 |

`tests/test_reachability_sweep.cpp`'s `kBlockedIntents` lost
`BankCurrencyMoveIntent` and `BankCurrencyConvertIntent`; F3 is off the T264
blocked list. The sweep's unreachable-handler-method test needed no list
edit -- the two new `IActionHandler` methods have callers from the day they
exist, which is what the red run proved.

`test_eq_bank_coins` asserts the OP_MoveCoin bytes on a real
`DaybreakConnection` over `FakeUdpTransport` (T234 idiom), including the raw
little-endian 20-byte body for a deposit, plus both pools of the projection
and the exact refusal paths.

## 4. Verification bar

| step | result |
|---|---|
| `cmake --build slot2/build -j24` | exit 0 (`build_full2.log`) |
| `ctest` | **3010/3011**; sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA quirk) |
| `scripts/build-gles2-x86.sh` | exit 0 |
| visual gate | PASS, goldens untouched |
| `scripts/build-arm-noble.sh` | exit 0 |
| `check_file_sizes.sh fable` | 8 ok, 7 warn, **0 fail** |

**Deviation (flagged):** `command_processor.cpp` was 2921 lines at `fable`;
the `/bank` parse took it to 3039 -- a NEW FAIL over the 3000-line ceiling.
Per the protocol, `cmdBank` + its two file-local helpers moved to the new
`src/client/action/command_processor_bank.cpp` (158 lines), registered in the
root `CMakeLists.txt` and in the four test targets that compile
`command_processor.cpp` directly. `command_processor.cpp` is back to 2922.

## 5. Live acceptance (eqemu owned; lock taken noclobber, released)

qeynos2, one headless client, banker `#npctypespawn 1107` (T227 recipe).
`live_run1.exit` = 0. Full command-by-command table in
`review/T269/README.md`. DB (`character_currency`, the server's own
`SaveCurrency` result):

    staged    20pp  0gp  0sp 40cp | bank 0/0/0/0
    after     17pp  0gp  0sp  0cp | bank 3pp 0gp 4sp 0cp
    restored 974pp 16gp 14sp 17cp | bank 0/0/0/0  (== db_before.txt, diff clean)

deposit 5 pp, deposit 40 cp, withdraw 2 pp and convert 40 cp -> 4 sp all
landed; every refusal path (window closed twice, pool short twice, bad
sub-command) printed its message and sent nothing. The DB matches the
client's projection in all eight counters -- the slot/cointype proof.

Hygiene: the one GM-spawned `zone_state_spawns` row this unit created
(`id=1704835`, zone 2, npc 1107) was deleted; pre-existing leftovers from
other units were left alone. `down.sh` clean, lock file deleted.
