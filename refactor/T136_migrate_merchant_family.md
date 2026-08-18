# T136 — Migrate the Merchant commands (5): the last family, and the least like the others

**Slot: whichever is free. x86. eqemu lock required (live leg). No Pi.**

**Territory: `src/client/eq_commands.cpp` (the Commerce block),
`src/client/action/`, `include/client/action/`,
`src/client/eq_action_handler.{h,cpp}`, `include/client/eq.h` (accessors only —
see below), `tests/`, `refactor/`, `/mnt/projects/fable/review/T136/`.**

Read T135's Review first (the two-convention trap, the DB-only wire evidence),
then T132's (the net-new recipe) and T103's (the Trade state domain — the
precedent for the design question below).

## The recipe does NOT apply here. Read this before planning your work.

Every prior family (T99–T102, T132, T134, T135) was some version of *thin
wrapper over an existing `EverQuest::Send*`*. **Not one of these five is.**
Verified while scoping:

| command | mechanism |
|---|---|
| `vendor` | bridge intent `VendorToggleIntent`, plus a `CombatManager` target check |
| `vendorlist` | **pure query/display** — reads `m_vendor_items` / `m_vendor_name` and prints |
| `buy` | bridge intent `VendorBuyIntent`, validated against `m_vendor_items` |
| `sell` | bridge intent `VendorSellIntent` |
| `equip` | **`InventoryManager`** — `hasCursorItem` / `getCursorItem` / `getItem` / `placeItem` |

**Five commands, four mechanisms, zero senders.** Treat this as five
independent migrations that need design judgement, not a template application.
T134's Board flagged Merchant as the family likely to break the pattern; it
does.

## The concrete state gap — smaller than T103's, but real

`buy` and `vendorlist` validate against EverQuest's merchant state. Of what
they touch:

- `IsVendorWindowOpen()` and `GetVendorNpcId()` are already **public**
  (`eq.h:2049-2050`).
- **`m_vendor_items` and `m_vendor_name` are private with no accessor**
  (`eq.h:1956`, `:1963`).

So the gap is two reads, not a subsystem.

**Recommendation: add the two minimal accessors rather than build a vendor
state domain.** T103's Trade domain was justified by a cross-cutting need;
here it is two reads serving one display command. **If you find a reason the
domain is warranted — say, the renderer already duplicates this state, or the
accessors would leak a mutable handle — STOP and report it** rather than
building one inside a migration unit. Record the domain as a follow-up either
way, since merchant state may grow.

## `vendorlist` — decide what "migrate" even means

`vendorlist` sends nothing. It reads game state and prints formatted lines.
Moving it to the action layer means the action layer reads that state and
prints.

**Establish whether that is the right layer at all**, and say what you decided.
A defensible answer is that it belongs where the other Commerce commands are,
for discoverability and registry consistency. An equally defensible answer is
that a pure display command over game state is a poor fit for an *action*
dispatcher. **Pick one, argue it in the Review, and do not let the choice be
made implicitly by whichever is easier to type.**

## `equip` may not belong in this unit

`equip` goes through `InventoryManager`, not the vendor path at all — it is an
inventory command sitting in the Commerce block. **Say whether it should
migrate here, move to a different category, or be its own unit.** Do not
migrate it merely because it shares a block with the others.

## Threading — establish it, do not inherit the existing comment

`buy`'s handler carries a long comment asserting the push happens on the
**game thread** via the piped-command path. That is scoped to *one* entry
point. There are **two**: the live chat window routes through
`EverQuest::DispatchSlashCommand` (registry 1) with a fallback into
Application's registries 2/3, while stdin/`--script` enters via
`Application::DispatchSlashCommand`.

**Establish which thread each entry point actually dispatches on after your
change**, and whether moving these commands from registry 1 to registry 2
changes it. `GameStateBridge::pushIntent()` is mutex-protected either way, but
the *state reads* (`m_vendor_items`) are not obviously so. **If migration
introduces a cross-thread read of merchant state, that is a STOP** — report it
rather than shipping it.

## Carry forward from the last three units

- **The two-convention trap** (T135): `EverQuest::QueuePacket(opcode, body)` —
  the callee prepends the opcode. `m_zone_connection->QueuePacket(packet)` —
  the caller must write it. **Never infer a bug from shape**; prove it from a
  live server-log size complaint, as T134 did. Likely moot here since there are
  no senders, but the intent path may have its own equivalent.
- **Wire evidence may be DB-only** (T135): its petition bug was visible *only*
  in persisted state, not the zone log. **Check the eqemu DB for `buy`/`sell`**
  — item and coin changes — not just the server log.
- **Guards stay where they are** (T134): stateful guards belong in the layer
  that owns the state; only usage-string guards move up.

## Atomicity and completeness

**Remove from `eq_commands.cpp` in the same commit that adds the
`CommandProcessor` registration.** Verify via
`Application::printCommandResult()`: `registry=action` for each migrated
command, plus a `registry=game` control — **if any `game` command remains**. If
this unit empties the registry, say so, and say what that means for
`EverQuest::m_command_registry`'s continued existence (it is likely now dead
weight, which is a finding, not a cleanup to perform here).

## Verification

- x86 suite (current total **2352**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile.
- **Tests at the programme's density** — T132: 22 cases / 7 commands; T134: 18
  / 6; T135: 19 / 6.
- **Visual gate goldens must not move.** `vendor` toggles a renderer window —
  if a golden moves, **STOP and report**; that needs a human eye.
- Live evidence in `review/T136/`, including **DB state** for `buy`/`sell`.
- Note `src/client/eq.cpp` already **fails `check_file_sizes.sh`** at 3898
  lines (pre-existing, verified at T135's merge). Do not let it get worse; if
  your change would grow it, say so.

## Review

(executor fills: the five commands with their mechanism and what you did with
each; the accessor-vs-state-domain decision and its argument; the `vendorlist`
layering decision; whether `equip` belonged here; the **threading finding per
entry point**; atomic-commit confirmation and what remains in
`eq_commands.cpp`; live evidence including DB state for `buy`/`sell`;
**measured wall-clock** against T132 ~50 / T134 ~100 / T135 ~70 min;
deviations)

## Board

If `eq_commands.cpp`'s registry is now empty, say so plainly and record what
that implies for the command-migration programme — this is its last family.
Give the final programme total (commands migrated, units spent, measured cost
per family) so the effort has a closing number rather than trailing off. Then
name what remains open in the action layer, if anything.
