# K12 — `CommandsComponent` (family: commands + combat_actions)

Batch: `refactor/K_component_extraction.md` §Mechanism (REVISED through
K10). **Wave 4; runs alongside K11 AFTER K09 merges.**

The batch table said each `/command` body "becomes an `IActionHandler`
method or is deleted if the registry already has it". Same reasoning as
K11: extract first, mechanically; the registry-merge (T139/T223 shape) is a
separate, measured unit afterwards.

## Measured at `dfd36b06` (2026-09-03) — re-measure at your tip

Four command TUs, 26 bodies: `eq_commands.cpp`, `eq_commands_movement.cpp`,
`eq_commands_spells.cpp`, `eq_commands_train.cpp`. Plus `eq_combat_actions.cpp`
(6 bodies: `IsStunned`, `ZoneProcessAction/Consider/Damage/MoneyOnCorpse/Stun`,
member `m_stunned_until`) — small enough to ride along as the same
component's "combat actions" section; K09 already retargeted its dispatch
arms to `owner_.X()`, which this unit turns into `owner_.Commands().X()`.

Callers: `eq_action_handler*.cpp` (`Request*` — ~15), `action/command_processor_*.cpp`
and `action/action_dispatcher*.cpp` (`Request*`, `Register*Commands`),
`eq.cpp` (`RegisterCommands`), `chat_component.cpp` (`RegisterCommands` —
read it; likely a comment), `graphics/renderer_slash_commands.cpp`
(`runLoadDiagnostics`, `runPmemDiagnostics` — if these are real calls the
graphics tree changes and the visual gate is not skippable), `merchant*.cpp`
(`TrainingCost`, `SendMoveItem` — these are MERCHANT's now; the hits here
are comments or the reverse direction — verify), `eq_zone_dispatch.cpp` →
after K09, `zone/…` (the combat handlers), `spell_cast_checks.cpp`
(`IsStunned`), `spell_manager.h` / `combat.h` (declarations or comments —
read).

`ListTrainableSkills`, `ResolveCarriedItem`, `RegisterSpellCommands`: no
external callers — in-family or dead (§5 with the in-family rule).

Members: `m_has_pending_invite`, `m_report` (verify they exist and are
`EverQuest`'s), `m_stunned_until` → move. `m_id`/`m_instance` are other
classes'.

Predicted `eq.h` delta: −32 declarations −3 members +4 ≈ **−30 or better**.

## Territory

`include/client/commands/` + `src/client/commands/` (new), the four
`eq_commands*.cpp` and `eq_combat_actions.cpp` (move), `eq.h`, `eq.cpp`
(`RegisterCommands` call), root `CMakeLists.txt`, `tests/test_commands_component.cpp`
+ cmake, allowlist, `docs/module_map.md`, CALL-SITE-ONLY edits in the callers
the compiler names. K11 runs alongside and edits `application*.cpp`,
`eq_bridge*.cpp` and the components at its own lines; if K11 has moved
`eq_bridge_intents.cpp` before you retarget a call in it, write the call
against the new path — check `git log fable` at dispatch.

## Verification / Review

Executed in `wt/slot2` on `task/K12-commands-component`, branched from
`fable` at `233ff30b`.

### Measured vs the plan's leads

**Methods.** Re-measured at this unit's tip (`233ff30b`), not the plan's
`dfd36b06` -- the plan's "26 bodies" is now **22** (`eq_commands.cpp` 13,
`eq_commands_movement.cpp` 3, `eq_commands_spells.cpp` 3,
`eq_commands_train.cpp` 3), plus combat's 6 (`ZoneProcessConsider/Action/
Damage/MoneyOnCorpse/Stun`, `IsStunned`) = **28 total**, all moved:

| method | callers outside the family, re-measured | tests, re-measured |
|---|---|---|
| `RegisterCommands` | `eq.cpp` (1, ctor) | 0 |
| `RegisterSocialCommands`/`RegisterMovementCommands`/`RegisterSpellCommands` | 0 (in-family, called only from `RegisterCommands`) | 0 |
| `runPmemDiagnostics` | 0 external | 0 -- but 1 IN-FAMILY caller (`commands_spells.cpp`'s `/pmem` handler): **not** zero-caller, see Corrections |
| `runLoadDiagnostics` | 0 anywhere, in-family included | 0 -- but see Corrections: kept anyway |
| `RequestVendorToggle`/`Buy`/`Sell` | `eq_action_handler_extended.cpp` (1 site each) | 0 |
| `RequestEquip` | `eq_action_handler.cpp` (1) | `test_eq_commands_equip.cpp` (**44** direct `eq.RequestEquip(...)` calls -- a real caller table miss, see Corrections) |
| `ResolveCarriedItem` | 0 (in-family only, called by `RequestReadItem`/`RequestScribeItem`) | 0 |
| `RequestReadItem` | `eq_action_handler.cpp` (1) | 0 direct (reached via `EqActionHandler` in `test_read_book.cpp`) |
| `RequestScribeItem` | `eq_action_handler.cpp` (1) | 0 direct (`test_scribe_scroll.cpp`, ditto) |
| `RequestTradeskillCombine`/`RequestCloseTradeskillContainer` | `eq_action_handler_spells_trade.cpp` (1 each) | 0 direct (`test_tradeskill_combine.cpp`, ditto) |
| `RequestWarp`/`RequestTimeOfDay` | `eq_action_handler_extended.cpp` (1 each) | 0 direct |
| `RequestBankToggle`/`RequestTraining` | `eq_action_handler_extended.cpp` (1 each) | `test_eq_commands_bank_train.cpp` (**10** direct calls -- same miss as `RequestEquip`) |
| `RequestTrainSkill`/`RequestCloseTrainer` | `eq_action_handler_extended.cpp` (1 each) | 0 direct (`test_train_skill.cpp`, ditto) |
| `ListTrainableSkills` | 0 external -- 1 in-family caller (`RequestTrainSkill`'s `list` branch) | 0 |
| `GetCommandRegistry` (inline `eq.h` accessor -- K04 correction f) | `application_commands.cpp` (2 sites, 1 real call + 1 comment) | 0 |
| `IsStunned` | `spell_cast_checks.cpp` (1) | `test_eq_action_handler_cast.cpp` (**6** direct `h.eq.IsStunned()` calls, retargeted) |
| `ZoneProcessConsider/Action/Damage/MoneyOnCorpse/Stun` | `zone_dispatch.cpp` (1 each, `owner_.ZoneProcessX(p)` -- K09 had already retargeted these to `owner_.X()` exactly as the plan predicted) | 0 |

Every caller retargeted to `eq.Commands().Method(...)`. All declarations
(and the `GetVendorItems()`/`GameVendorItem`/`m_vendor_items` cluster, see
below) deleted from `eq.h` per §Mechanism 5 -- no forwarders.

**The plan's caller leads that were FALSE or need correction, re-verified at
this tip:**
- `chat_component.cpp`'s `RegisterCommands` hit is a comment only (confirmed
  by reading it) -- no edit.
- `renderer_slash_commands.cpp`'s `runLoadDiagnostics`/`runPmemDiagnostics`
  hits are comments only -- **no graphics file changed** in this unit
  (`git diff --name-only fable...HEAD -- src/client/graphics
  include/client/graphics` is empty), so the visual gate and ARM steps were
  correctly skippable per the plan's own caveat.
- `merchant*.cpp`'s `TrainingCost`/`SendMoveItem` hits are comments quoting
  the already-migrated names (`eqt::merchant::MerchantComponent::TrainingCost`,
  already fully qualified) -- confirmed MERCHANT's, no edit needed there.
- `m_has_pending_invite`, `m_report`: **both are false leads.**
  `m_has_pending_invite` exists only in one comment in `eq_commands.cpp`
  (prose about a dead pending-invite-accept path); `m_report` does not exist
  anywhere in the tree. Neither is a real `EverQuest` member -- same class of
  false lead K10 correction (t) named ("a member lead is only real if the
  receiver is `EverQuest`"), here the lead was never a real member at all.
- `spell_manager.h`/`combat.h`: comment-only prose references to
  `EverQuest::ZoneProcessAction`/`ZoneProcessDamage` -- read, confirmed not
  live code, no edit (not `DocCitations`-checked; that script only scans
  live docs, not `.h`/`.cpp` prose).

**A caller class the plan's table did not name at all: direct test calls.**
`test_eq_commands_equip.cpp` (44 sites) and `test_eq_commands_bank_train.cpp`
(10 sites) call `eq.RequestEquip(...)`/`eq.RequestBankToggle()`/
`eq.RequestTraining()` directly against a real `EverQuest` instance, not
through `EqActionHandler` -- the plan's caller table only listed the
handler-mediated path. Found by grepping `tests/` for the method names
(§Mechanism 7's "grep tests/ once for stragglers" step) after the first
build, not by the compiler (these are the SAME class as `m_eq.Method()`
call sites -- `eq.Method()` on a bare instance -- so a name grep, not a
build error, is what catches them). All 54 retargeted to
`eq.Commands().Method(...)`.

**Members.** `m_stunned_until` confirmed exactly as the plan predicted:
single-family, moved with `IsStunned`/`ZoneProcessStun`. **One member the
plan's table did not name at all: `m_vendor_items` (+ its `GameVendorItem`
struct).** K05's `merchant_component.h` file banner explicitly named this
family ("`m_vendor_items` (read by `eq_commands.cpp`, K12)") as the shared
member K12 would be the last family to use -- §Mechanism 3's rule. Moved
here; `GameVendorItem` is now `eqt::commands::CommandsComponent::GameVendorItem`
(public), and Merchant's three writers (`merchant.cpp`, `merchant_trade.cpp`,
`merchant_vendor_bank.cpp`) reach it through a new non-const reference
accessor, `VendorItems()`, matching K09's `ZoneComponent::MovementHistory()`
shape exactly (§Mechanism 3's "member shared with another family" case, not
a stayed-on-`EverQuest` transition-handle read). `GetVendorItems()`
(an inline `eq.h` accessor -- K04 correction f) moved with it; its one
external caller (`eq_action_handler_extended.cpp`) retargeted to
`eq.Commands().VendorItems()`.

**Dependencies not in the plan's tables**, found by reading the bodies (same
class as K02's `PosX`/`AddChatSystemMessage` surprises): `PosX`/`PosY`/
`PosZ`/`HeadingDeg`/`SetPosLocal`/`MySpawnId` (stayed-behind `EverQuest`
private accessors, `owner_.Foo()`), `GetPracticePoints`/
`IsVendorWindowOpen`/`GetVendorNpcId`/`IsBankWindowOpen` (stayed-behind
public `EverQuest` accessors), `EverQuest::s_debug_level` (third
substitution, `combat_actions.cpp` -- 4 call sites), `EverQuest::GetClassName`/
`EverQuest::FormatPrice` (third substitution, static methods), and four
already-extracted-family calls needing the `owner_.<Family>().Foo()` form
(K06/K07 correction r): `owner_.LoginWorld().WorldConnection()`/
`LoginConnection()`, `owner_.Entities().FindEntityByName()`,
`owner_.Movement().SendPositionUpdate()`/`CalculateDistance2D()`,
`owner_.Merchant().CloseBankWindow()`/`RequestTrainerWindow()`/
`ScribeRefusalMessage()`/`GetTrainerSkillCap()`.

### The three `eq.h` numbers

| | lines |
|---|---|
| at dispatch (`fable`, `233ff30b`) | **1,226** |
| after this unit | **1,138** |

Predicted: "-32 declarations -3 members +4 ~ -30 or better." Measured from
`git diff --stat fable -- include/client/eq.h`: **131 deletions, 43
insertions = net -88.** Much better than predicted, same shape K02 found:
the shrinkage is driven by struct/comment-banner scaffolding riding on the
moved declarations (the `GameVendorItem` struct + `m_vendor_items` +
surrounding comments, the `RegisterCommands`/`Register*Commands`/diagnostics
comment blocks, the `Request*` per-method doc comments), not just the raw
declaration count.

### Multiset residual (§Mechanism 8)

`scripts/refactor/verify_move_only.py` takes committed revisions only, and
this unit's substitutions (`m_x`->`ctx_.x`/`owner_.m_x`,
`Foo()`->`owner_.Foo()`/`owner_.<Family>().Foo()`, `EverQuest::Static`) are
exactly the class of edit it cannot itself normalize -- same as K02's
manual approach. Reversed the substitutions by name-mapping on a COPY of
each new file (mapping `ctx_.foo` back to `m_foo` by the `ClientContext`
field name, `owner_.Foo()`/`owner_.<Family>().Foo()` back to the original
bare/member-pointer call, `EverQuest::Static` back to bare), then diffed the
sorted, blank-stripped line multisets against `git show fable:<old path>`
for all five files:

| file | added | removed |
|---|---|---|
| `commands_component.cpp` (was `eq_commands.cpp`) | 16 | 3 |
| `commands_movement.cpp` | 1 | 0 |
| `commands_spells.cpp` | 1 | 0 |
| `commands_train.cpp` | 1 | 0 |
| `combat_actions.cpp` | 11 | 3 |

Every line in every diff is the new `#include "client/commands/
commands_component.h"`, the K12 file-banner prose (interleaved by the
sorted diff), or two pre-existing comment-prose edits (a stale filename --
"`eq_commands_movement.cpp`" -> "`commands_movement.cpp`" -- and a
singular/plural tweak, "diagnostics helpers" -> "diagnostics helper").
**Zero code lines added or removed in any of the five files** -- a pure
move plus the allowed substitutions, confirmed by inspection of every
diff line (pasted in full during the session; none start with a bare
non-comment, non-include token).

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **First attempt: exit 2**, exactly 4 errors -- all in `tests/test_eq_action_handler_cast.cpp`, `'class EverQuest' has no member named 'm_stunned_until'` (see Corrections: a `#define private public` test-hack site the grep-for-declarations pass could not find). Fixed in-unit. **Second attempt: exit 0.** Third attempt (after restoring `runLoadDiagnostics`, see Corrections) and fourth (after adding `test_commands_component`): both **exit 0** |
| `ctest --test-dir build -j6 --output-on-failure` | Final run: **3,578 / 3,579 passed**, 47 skipped, 124 s. **1 failure: `AudioManagerTest.PreloadCommonSounds` only** (the known ALSA quirk). `FileSizeCeiling`, `DocCitations`, all 5 `ReachabilitySweep.*`, `DuplicateStateStores.*` and `visual_gate` (Test #3567, 21.16s, inside this same run) all **PASS**. An intermediate run (before the `runLoadDiagnostics` fix, see Corrections) had a second failure, `ReachabilitySweep.EveryEventIsPublishedAndConsumed` -- fixed in-unit, re-run green |
| `test_commands_component` (new) | **7/7 pass**: `ComponentExistsAndIsStable`, `ContextPointsAtTheOwningEverQuestsOwnMembers`, `RegisterCommandsRanAtConstructionAndPopulatedTheRegistry`, `GetCommandRegistryReturnsTheSameEveryTime`, `FreshClientIsNotStunned`, `VendorItemsStartsEmpty`, `VendorItemsRoundTripsThroughTheAccessor` |
| retargeted suites | `test_eq_commands_equip.cpp` (44 calls), `test_eq_commands_bank_train.cpp` (10 calls), `test_eq_action_handler_cast.cpp` (6 `IsStunned` calls + 4 `m_stunned_until` test-hack accesses) -- **all pass, every assertion untouched** |
| `test_duplicate_state_stores`/`test_reachability_sweep` | **pass**, no file-list edit needed -- `m_stunned_until`/`m_vendor_items` are not state-module names, and both text-scanning tests already walk `include/client/*/*_component.h` since K05's fix (correction k) |
| `bash scripts/build-gles2-x86.sh` / visual gate (standalone) / `bash scripts/build-arm-noble.sh` | **SKIPPED, with evidence**: `git diff --name-only fable...HEAD -- src/client/graphics include/client/graphics` is **empty** -- no graphics file changed in this unit's 24 touched/added files. `visual_gate` nonetheless ran and PASSED inside the full `ctest` run above |
| `scripts/refactor/check_file_sizes.sh --all` | **821 ok, 0 warn, 0 FAIL**, `eq.h` allowlist entry lowered **1,226 -> 1,138** |

### Corrections to §Mechanism / the plan

1. **§Mechanism 5's "zero callers -> delete" has no rule for a dead method
   that is the SOLE PUBLISHER of a still-consumed event.** `runLoadDiagnostics`
   has genuinely zero callers anywhere (confirmed dead since T105/T108, exactly
   as the plan's caller-lead comments said) -- a first pass deleted it outright,
   which is what §5 says to do. That broke
   `ReachabilitySweep.EveryEventIsPublishedAndConsumed`: `runLoadDiagnostics`
   was the only site in the tree pushing `GameEventType::DiagnosticsSceneDump`,
   and `irrlicht_bridge.cpp` still has a live `case` arm consuming it. §5 is
   about callers of a MOVED METHOD -- an orthogonal axis to "this dead method
   is the last publisher of a still-live event" -- and deleting it is an
   observable behaviour change (an event stops firing) in a unit whose
   charter is explicitly "no behaviour change." **Restored it, private, moved
   verbatim, in-unit.** Future units should add a check alongside the
   zero-caller delete: before deleting a dead `Send*`/`push*Event` method,
   grep the `GameEventType`/intent enumerators it references for other
   producers; if it is the last one, keep it (or take the event deletion to
   the board as a separate, deliberate change) rather than deleting silently.
   `runPmemDiagnostics` looked like the same shape at a glance -- the plan's
   lead comment groups both names together as "if these are real calls" --
   but has a real in-family caller (the `/pmem` handler,
   `commands_spells.cpp`) and was never actually in question; conflating the
   two on a first read cost one build/test round-trip.

2. **`#define private public` test hacks are a caller class §Mechanism 7's
   grep step does not anticipate.** `test_eq_action_handler_cast.cpp` accesses
   `h.eq.m_stunned_until` directly (4 sites) via a file-level `#define private
   public`, bypassing every accessor and every declaration grep -- the
   compiler caught this instantly (`'class EverQuest' has no member named
   'm_stunned_until'`) once the member moved, exactly as §Mechanism 5
   promises ("callers are found by the COMPILER, not by grep"), but it is
   worth naming explicitly: a moved member's OLD qualified name
   (`h.eq.m_member`) can appear as a live compiled expression in test code
   through this hack, not just through an accessor call. Retargeted to
   `h.eq.Commands().m_stunned_until` (the hack reaches into the component's
   private section exactly the same way).

3. **A caller class beyond `EqActionHandler`: direct test calls on a bare
   `EverQuest` instance.** See "Methods" above -- 54 sites across two test
   files called `Request*()` directly, not through the action-handler layer
   the plan's caller table modeled. These are NOT visible to a "grep for the
   RECEIVER EXPRESSION" pass keyed on `m_eq.`/`h.eq.` alone (K03 correction
   b's guidance) -- the receiver here is a bare local variable named `eq`.
   Future plans should widen the caller-table instruction to "grep for
   `.MethodName(` across `tests/` with no receiver-prefix assumption," not
   just the `m_eq.`/`h.eq.` shapes prior units' leads used.

4. **No other STOP condition was hit.** Every moved body needed only the
   substitutions §Mechanism 4 allows (all three named forms, plus the
   already-extracted-family accessor form K06/K07 correction r describes --
   no new substitution shape). No test asserted on a member by name that
   could not be repointed (the `#define private public` case above was
   repointable, just non-obvious). The predicted delta was negative before
   dispatch and the measured delta is more negative still.

### Left for the board

- **`runLoadDiagnostics` is now a KEPT zero-caller method**, an explicit,
  reasoned exception to the reachability pattern rather than an instance of
  it -- flagged so a future dead-code sweep does not re-delete it without
  re-checking `DiagnosticsSceneDump`'s consumer.
- **The registry-merge** (T139/T223 shape: fold `CommandsComponent`'s
  `Request*()` wrappers into `action::EqActionHandler`/
  `action::CommandProcessor` directly) is explicitly deferred to a later,
  separately measured unit, per the plan's opening paragraph -- unchanged by
  this unit.
