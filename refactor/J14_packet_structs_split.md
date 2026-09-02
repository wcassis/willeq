# J14 — Batch J Wave 7: split `include/common/packet_structs.h` (2,215 → umbrella + 11 domain headers, largest ~400)

**Slot: assigned at dispatch. Branch `task/J14-packet-structs-split` cut from `fable`.
x86 only. No Pi hardware. No eqemu.**

Wave 7 of `refactor/J_file_size_reclamation.md`. Read, before editing anything:
**`refactor/J_file_size_reclamation.md` §Process per unit AND every correction
section (eighty numbered lessons from thirty-odd executed units; newer text wins
over older)**, then **`refactor/J28_detail_texture_atlas_split.md` including its
Review** — the batch's best-instrumented correction-21/correction-60 evidence —
and `refactor/J34_spell_visual_fx_split.md` §Review. Then
`refactor/EXECUTOR_PROTOCOL.md`.

**Everything this plan needs is stated concretely below. You should not have to
chase a reference to find a command line, a range, or a rule.**

## MOVE-ONLY. No behaviour changes, no interface changes.

Struct, enum and constant definitions move **verbatim**. **No struct is
edited, renamed, reordered, deleted or re-asserted.** No drive-by fixes. No
deletion of dead content: a J unit **records** it and **moves it verbatim**.
**No `static_assert` value may change for any reason.**

## Why this unit is different from every .cpp unit in the batch

This is a **header**, and the whole point of the split is that **all ~62
includers keep compiling unchanged**. `packet_structs.h` stays where it is and
becomes an **umbrella** that includes the eleven domain headers. Nothing outside
`include/common/` changes.

The proof bar is therefore stronger than a line count: **109 `static_assert`
declarations must all survive and still fire.** A split that silently stopped
compiling one of them — because a struct drifted out of a `#pragma pack` region
— would pass every other check in the batch's bar. §Acceptance turns that into
a measurement.

## Measured at `09c99b0b` (2026-09-02, `fable` tip after T325 merged)

`include/common/packet_structs.h` — **2,215 lines** (the batch survey's
figure, unchanged). ⚠ **Re-verify at your own branch tip before you cut**
(correction 9) and report the drift in your Review rather than silently
adjusting.

| property | measured value |
|---|---|
| total lines | **2,215** |
| file-header comment block | **1-37** — the T147 "RULES FOR THIS FILE" block, four numbered rules. **Stays in the umbrella.** |
| include guard | `EQT_PACKET_STRUCTS_H`, `#ifndef`/`#define` at **39-40**, `#endif` at **2215** |
| includes | **three**, at 42-44: `<cstddef>`, `<cstdint>`, `<cstring>` |
| namespace | `EQT` only, opened at **46**, closed at **2211**. Plus a nested `namespace Position {` at **2192**, closed at **2209** |
| pack region | `#pragma pack(push, 1)` at **64**, `#pragma pack(pop)` at **2213** — ⚠ **the pop is AFTER the namespace close**, not inside it |
| top-level `struct` definitions | **120** (`grep -c '^struct '`) |
| top-level `enum` definitions | **5** (`grep -c '^enum '`) — `AppearanceType`, `GroupAction`, `MerchantAction`, `ItemPacketType`, `ServerFilterSlot` |
| `static_assert` declarations | **109** — **108** `sizeof`, **1** `offsetof` (line 494). ⚠ `grep -c 'static_assert'` returns **111**: two of those are prose mentions inside the 1-37 rules block. Count with `grep -c '^static_assert'`. |
| `// ====` banner sections | **21**, first at **805**, last at **2187** |
| conditional preprocessor directives | ⚠ **the include guard and nothing else.** `awk '/^[ \t]*#(if\|ifdef\|ifndef\|else\|elif\|endif\|define)/'` returns exactly three lines: 39, 40, 2215. **Zero conditional compilation anywhere in the file.** |
| file-scope `static` constants | **27** — 13 at 49-61, `PET_BUFF_COUNT` at 402, 9 `COINTYPE_*`/`COINSLOT_*` at 1247-1257, and the rest are `static_assert` (not symbols). See §Correction 21. |
| namespace-scope free functions | **zero at column 0.** The only free functions are the three `Position::Extract*` **inline** helpers at 2192-2209, indented inside `namespace Position`. |
| multi-line comment regions | **many** — this file documents nearly every struct with a `/* … */` block, and uses inline `/*0197*/` field-offset comments. **Comment depth at every cut must be COMPUTED, not asserted.** |

## The split — 11 domain headers under `include/common/packets/`

New directory `include/common/packets/`. Each file is included as
`"common/packets/<name>.h"` (the `include/` root is already on the include path
— that is how `"common/packet_structs.h"` resolves today).

| # | new header | verbatim range(s) | ≈ body lines | contents |
|---|---|---|---|---|
| 1 | `common/packets/common.h` | **47-61** and **66-134** | **84** | the 13 shared constants (`BUFF_COUNT` … `TEXTURE_COUNT`), then `Tint_Struct`, `TintProfile`, `Texture_Struct`, `TextureProfile` |
| 2 | `common/packets/spawn.h` | **135-381** | **247** | `LoginInfo_Struct`, `EnterWorld_Struct`, `EntityId_Struct`, `Spawn_Struct`, `NewSpawn_Struct`, `ClientZoneEntry_Struct`, `ServerZoneEntry_Struct`, `NewZone_Struct`, `SpawnAppearance_Struct`, `enum AppearanceType`, `Illusion_Struct` |
| 3 | `common/packets/combat.h` | **382-535** | **154** | `SpellBuff_Struct`, `PET_BUFF_COUNT`, `PetBuff_Struct`, `Consider_Struct`, `CombatAbility_Struct`, `MoneyOnCorpse_Struct`, `ChannelMessage_Struct`, `Death_Struct`, `DeleteSpawn_Struct` |
| 4 | `common/packets/character.h` | **536-681** | **146** | `BindStruct`, `AA_Array`, `Disciplines_Struct`, `Tribute_Struct`, `BandolierItem_Struct`, `Bandolier_Struct`, `PotionBeltItem_Struct`, `PotionBelt_Struct`, `Target_Struct`, `BeginCast_Struct`, `CastSpell_Struct`, `MemorizeSpell_Struct`, `ManaChange_Struct` |
| 5 | `common/packets/zone.h` | **682-804** | **123** | `ZoneChange_Struct`, `RequestClientZoneChange_Struct`, `LootRequest_Struct`, `ZonePoint_Entry`, `ZonePoints_Header`, `Door_Struct`, `MoveDoor_Struct`, `ClickDoor_Struct` |
| 6 | `common/packets/world_object.h` | **805-1051** | **247** | the *World Object / Tradeskill* banner section: `Object_Struct`, `ClickObject_Struct`, `ClickObjectAction_Struct`, `NewCombine_Struct`, `ApplyPoison_Struct`, `Track_Struct`, `Resurrect_Struct`, `Who_All_Struct`, `WhoAllReturnHeader`, `Inspect_Struct`, `InspectResponse_Struct` |
| 7 | `common/packets/group.h` | **1052-1127** | **76** | the *Group* banner section: `enum GroupAction` and the seven `Group*_Struct` |
| 8 | `common/packets/merchant.h` | **1128-1397** | **270** | the *Vendor/Merchant* and *Pet Command* banner sections: merchant, money, `enum MerchantAction`, `enum ItemPacketType`, trade (incl. the `COINTYPE_*`/`COINSLOT_*` constants at 1247-1257), book, GM training, pet commands |
| 9 | `common/packets/guild.h` | **1398-1632** | **235** | the *Guild*, *Corpse Management* and *Consent System* banner sections, incl. `Emote_Struct`, `SetServerFilter_Struct`, `enum ServerFilterSlot` |
| 10 | `common/packets/travel_misc.h` | **1633-1828** | **196** | the *Travel*, *Group Split*, *LFG*, *Combat Abilities*, *Discipline*, *Banking* and *Phase 3 Misc* banner sections, incl. `EnvDamage2_Struct` (the T145 struct) |
| 11 | `common/packets/gm.h` | **1829-2209** | **381** | the *Dueling*, *Skills*, *Tradeskill Recipe*, *Cosmetic*, *Phase 4 Misc*, *GM Command* and *Petition* banner sections, and the `namespace Position` inline extractors at 2192-2209 |

**Arithmetic that must hold:**
`(61 - 47 + 1) + (2209 - 66 + 1) = 15 + 2144 = 2159` movable lines, and
`84 + 247 + 154 + 146 + 123 + 247 + 76 + 235 + 270 + 196 + 381 = 2159`.
**If your own numbers do not sum to 2,159, you have a gap or an overlap — find
it before you build.**

The lines NOT moved are **1-46** (rules block, guard, three includes,
`namespace EQT {`), **62-65** (the blank, the `// Compile-time struct packing`
comment, the `#pragma pack(push, 1)`, the blank) and **2210-2215** (blank,
`} // namespace EQT`, blank, `#pragma pack(pop)`, blank, `#endif`). Lines 62-65
and 2210-2215 are **structural** — each new header writes its own equivalents,
and the umbrella keeps none of them.

⚠ **Correction 71: the "≈ body lines" column above is RANGE LENGTH, not
finished file size.** Each finished header is `(your prescribed header block) +
range`; §Header shape fixes that block, and §Acceptance asks you to compute the
totals rather than quote these.

## The umbrella — what `packet_structs.h` becomes

```
<lines 1-37 verbatim: the T147 RULES block, unchanged>

#ifndef EQT_PACKET_STRUCTS_H
#define EQT_PACKET_STRUCTS_H

// J14: this file was 2,215 lines of wire layouts. It is now an umbrella over
// include/common/packets/. Every struct, enum, constant and static_assert
// moved verbatim; nothing was edited, and the ~62 files that include this
// header see exactly the same declarations they saw before.
//
// Each domain header opens and closes its OWN #pragma pack(push, 1) /
// #pragma pack(pop) pair. This file must NOT wrap them in one — see rule 1
// above, and refactor/J14_packet_structs_split.md.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "common/packets/common.h"
#include "common/packets/spawn.h"
#include "common/packets/combat.h"
#include "common/packets/character.h"
#include "common/packets/zone.h"
#include "common/packets/world_object.h"
#include "common/packets/group.h"
#include "common/packets/merchant.h"
#include "common/packets/guild.h"
#include "common/packets/travel_misc.h"
#include "common/packets/gm.h"

#endif // EQT_PACKET_STRUCTS_H
```

The three standard includes stay in the umbrella **even though every domain
header repeats them**: some includer may be relying on getting `<cstring>`
transitively today, and removing that is a behaviour change wearing a
move-only commit's clothes.

**Compute the umbrella's final length yourself and report it** (correction 58 —
never quote a headroom figure a plan derived; derive it from what the plan
prescribes). It is `37 + <the block above>`.

## Header shape — every domain header, exactly this

```
// <one or two lines naming the sections this header holds>
// Wire layouts, extracted from packet_structs.h by
// refactor/J14_packet_structs_split.md. The RULES for this file family live
// in the umbrella, include/common/packet_structs.h -- read them before
// touching a struct or a static_assert here.

#ifndef EQT_PACKETS_<DOMAIN>_H
#define EQT_PACKETS_<DOMAIN>_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "common/packets/common.h"      // omitted in common.h itself

namespace EQT {

#pragma pack(push, 1)

<the verbatim range>

#pragma pack(pop)

} // namespace EQT

#endif // EQT_PACKETS_<DOMAIN>_H
```

## ⚠⚠ THE PACK HAZARD — this file's whole defect history, and it has bitten twice

`#pragma pack` is **lexical, not scoped**. It is not undone by a namespace
close, a struct close, or the end of a header — only by a matching `pop`. T145
and T147 are both instances of getting this wrong: the file's `pop` used to sit
~700 lines early, ~40 structs compiled at natural alignment, and someone
"fixed" a `static_assert` to the padded value — producing a permanently-passing
assert on a wrong wire layout (`EnvDamage2_Struct`, 36 instead of 31).

**Three rules, all load-bearing:**

1. **Every new header opens its own `#pragma pack(push, 1)` and closes its own
   `#pragma pack(pop)`, and the pop is in the same file as the push.** Eleven
   pushes, eleven pops.
2. **The umbrella must NOT wrap the includes in a push/pop.** If it did, and
   any domain header's own pop ran first, the nesting would be wrong for
   everything after it. The umbrella carries **no pack directive at all** —
   grep it and prove that.
3. ⚠ **No `#include` may appear inside a push/pop region.** An include pulled
   in while packing is active repacks the included header's types — the T147
   mechanism applied to a whole file. In the shape above every include sits
   **above** `namespace EQT {`, which is above the push. `common.h` itself pops
   before anyone includes it, so including it is safe.

**Note the deliberate deviation from the original's ordering:** today the pop
sits *after* `} // namespace EQT`. The prescribed shape pops *before* the
namespace close, so the pack region is exactly the struct region. This is a
strictly tighter arrangement, it is identical in effect, and it is the only
structural change this unit makes. **State it in your Review.**

**Prove the pairing mechanically, in every created file and in the umbrella.**
⚠ Correction 52: `grep -c '<TOKEN>'` is **not** a matched-pair check — it counts
lines, and both directives could be pushes. Count each direction separately:

```bash
for f in include/common/packet_structs.h include/common/packets/*.h; do
  printf '%-52s push=%s pop=%s\n' "$f" \
    "$(grep -c 'pragma pack(push' "$f")" "$(grep -c 'pragma pack(pop' "$f")"
done
```
Expect `push=0 pop=0` for the umbrella and `push=1 pop=1` for each of the
eleven. **Paste the whole table.**

## ⚠ Correction 21 — the only plan defect class that has produced a non-compiling plan

**The single-consumer grep proves a symbol CAN stay file-local. It does NOT
prove the plan's ranges KEEP it with its consumers.** J08's plan assigned three
statics to one range and their only consumer to another; it surfaced only at
the BUILD step.

**Run BOTH greps (correction 68 — the narrow one and the widened one are not
interchangeable, and neither alone is sufficient), and run each with a positive
control before believing any empty result (correction 60):**

```bash
# narrow — file-scope constants, which the widened pattern structurally cannot match
grep -nE '^static |^namespace|^const |^constexpr |^extern ' include/common/packet_structs.h
# widened — namespace-scope FREE FUNCTIONS, which the narrow pattern cannot see
grep -nE '^[A-Za-z_][A-Za-z_0-9:<>,&\* ]*\(' include/common/packet_structs.h
```

⚠ **Positive control: `src/client/position.cpp`** — correction 78. It returns
`11:constexpr float position_eps` for the narrow pattern and roughly thirty free
functions (`ValueWithin`, `GetReciprocalHeading`, `IsHeadingEqual`, …) for the
widened one. **The controls named in corrections 51 and 61 —
`zone_geometry.cpp`'s `heightToColor`, `combat.cpp`'s `IsCorpseEntity` — are
STALE; J32 and J24 split those files and the symbols are gone.** Use
`position.cpp` and paste both control results.

**At `09c99b0b` the widened pattern returns 109 lines and every one of them is
a `static_assert(` at column 0** — the pattern is live, and there are **zero
namespace-scope free functions** in this file. The three `Position::Extract*`
helpers are `inline` and indented inside `namespace Position`, so they are
invisible to it; the narrow grep catches their enclosing namespace at 2192.

**The orchestrator audited this plan against correction 21. Result, symbol by
symbol:**

| symbol(s) | defined at | tile | in-header consumers | consumers' tile | same range? |
|---|---|---|---|---|---|
| `TEXTURE_COUNT` | 61 | common | 100, 131 | common | ✅ yes |
| `MAX_PP_DISCIPLINES` | 53 | common | 566, and a comment at 1756 | character, travel_misc | ✅ via `common.h` include |
| `BANDOLIER_ITEM_COUNT` | 60 | common | 596 | character | ✅ via `common.h` include |
| `POTION_BELT_SIZE` | 60 | common | 616 | character | ✅ via `common.h` include |
| `MAX_PP_SKILL` | 50 | common | 1340 | merchant | ✅ via `common.h` include |
| `BUFF_COUNT`, `MAX_PP_LANGUAGE`, `MAX_PP_AA_ARRAY`, `MAX_PLAYER_TRIBUTES`, `MAX_RECAST_TYPES`, `SPELLBOOK_SIZE`, `SPELL_GEM_COUNT`, `BANDOLIERS_SIZE` | 49-60 | common | **none in this header** | — | ✅ external only, reached via the umbrella |
| `PET_BUFF_COUNT` | 402 | combat | 406, 407 | combat | ✅ **same tile** |
| `COINTYPE_*`, `COINSLOT_*` | 1247-1257 | merchant | **none in this header** | — | ✅ external only (`src/client/trade_manager.cpp`), reached via the umbrella |
| `Tint_Struct`, `TintProfile`, `Texture_Struct`, `TextureProfile` | 70-134 | common | **`Spawn_Struct` at 217 and 241 — and NOWHERE else in this file** | spawn | ✅ via `common.h` include |

⚠ **Two corrections to the dispatch brief, both verified:** the shared
tint/texture types are used by **`Spawn_Struct` only** — `Illusion_Struct` does
**not** reference them — and **there is no `WearChange_Struct` anywhere in this
tree** (`grep -rn 'WearChange_Struct' include/ src/` returns nothing). The
common header is still right, for `Spawn_Struct` and for external includers.

**The rule that discharges every cross-tile row: every domain header includes
`common/packets/common.h`.** That is not symmetry — it is the enumerated
consequence of the table above (correction 10's corollary: an exception list is
signed off item by item).

**Re-run both greps at your tip, with the control, and paste them.** Do not
cite this section — if a file-scope symbol has appeared or moved since
`09c99b0b`, **STOP and report**; the unit changes shape.

## Every cut boundary — verify each at your tip

The rule is **J03's** (correction 11): **cut at a blank line whose predecessor
closes a definition, and verify comment depth 0 at the cut.** The banner rule
survives only as a tie-breaker: **a `// ====` banner travels with the section it
introduces.**

| Cut | preceding lines | first line of next range |
|---|---|---|
| 135 | `134: static_assert(sizeof(TextureProfile) == 36, …)` | `135` blank, `136: /*` — the *Login Info structure* comment |
| 382 | `380` blank | `381: /*`, `382: * Spell Buff structure (in profile)` — ⚠ **the block comment OPENS at 381**, so the tile starts at 381, not 382. **Re-derive this at your tip.** |
| 536 | `535: static_assert(sizeof(DeleteSpawn_Struct) == 4, …)` | `536` blank, `537: /*` — the *Bind structure* comment |
| 682 | `680: static_assert(ManaChange…)`, `681` blank | `682: /*` — the *Zone Change structure* comment |
| 805 | `803: static_assert(ClickDoor…)`, `804` blank | `805: // ============` — the *World Object / Tradeskill* banner |
| 1052 | `1050: static_assert(InspectResponse…)`, `1051` blank | `1052: // ============` — the *Group Structures* banner |
| 1128 | `1126: static_assert(GroupDisband…)`, `1127` blank | `1128: // ============` — the *Vendor/Merchant* banner |
| 1398 | `1391: static_assert(PetCommandState…)` then the T147 note comment, `1397` blank | `1398: // ============` — the *Guild Structures* banner |
| 1633 | `1631: static_assert(ConsentResponse…)`, `1632` blank | `1633: // ============` — the *Travel System* banner |
| 1829 | `1827: static_assert(PopupResponse…)`, `1828` blank | `1829: // ============` — the *Dueling* banner |

⚠ **The 381/382 row is the one boundary this plan is not certain of, and it is
called out deliberately** (correction 2: J02's plan cut inside a `/* … */` and
would have left an unterminated comment in one file and an unopened close in
the other). **Compute the depth; if the comment opens at 381, the combat tile is
381-535 and the spawn tile is 135-380, and the arithmetic above is unchanged.
Report which you used.**

**Compute comment depth at every cut with a `//`-stripping counter — do not
assert it.** A `//` line containing the token `*/` makes a naive counter go
negative (the trap `eq_movement.cpp:18` set for the orchestrator's own scanner):

```bash
python3 - <<'EOF'
L=open('include/common/packet_structs.h').read().split('\n')
cuts={135,381,382,536,682,805,1052,1128,1398,1633,1829,2210}
d=0
for i,ln in enumerate(L,1):
    if i in cuts: print('depth before line',i,'=',d)
    s=ln
    if d==0:
        p=s.find('//'); q=s.find('/*')
        if p!=-1 and (q==-1 or p<q): s=s[:p]
    d+=s.count('/*')-s.count('*/')
EOF
```

⚠ **Correction 33: "no block comments" must mean "no MULTI-LINE comment
regions".** This file has many; what the cutting rule needs is **depth 0 at
every cut**, which the counter above measures.

## The preprocessor dump — paste it

```bash
awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif|define)/ {print NR": "$0}' \
    include/common/packet_structs.h
```
At `09c99b0b` this returns **exactly three lines** — 39, 40, 2215, the include
guard. ⚠ **Correction 60: an empty or tiny result is not evidence until the
pattern is proven live.** Run the same `awk` against
`include/client/graphics/irrlicht_renderer.h`, which has fourteen conditional
directives, and paste that too.

## CMake — expect ZERO add-sites, and prove it

```bash
grep -rn 'packet_structs' CMakeLists.txt cmake tests scripts benchmarks
```

⚠ **NEVER name `tests/CMakeLists.txt` in a grep (correction 26).** J20 split it
into per-domain fragments under `tests/cmake/`; the top-level file is 135 lines
and holds no test targets. **`grep -rn` across `tests/`.**

⚠ **`grep -rn --include=<pat>` SILENTLY RETURNS NOTHING on this host** (ugrep
7.8.4, correction 55). Never use `--include`; use plain `grep -rn <dir>` or
`find … -exec grep -Hn … {} +`.

⚠ **CMake commands in this project are UPPERCASE** (correction 12) —
`grep -n 'set('` and `grep -n 'add_executable('` both return nothing, silently,
which reads exactly like "no sites here". **Grep for the filename.**

⚠ **Do not cite a CMake line number.** Corrections 59 and 76 record five
consecutive units whose target names were right and whose line numbers were all
stale within a day. **Name the target, locate it yourself.**

**At `09c99b0b`, `packet_structs.h` appears in NO CMake file** — headers are not
listed in this project's source lists, and no target names it. **The expected
add-site count for this unit is ZERO, and the new headers need no add-site
either.** ⚠ That is unusual enough to be worth doubting: add-site counts in this
batch have ranged 1 to 19. **Count your own and say so loudly if you find any.**
The only CMake hit expected is `tests/cmake/data_formats.cmake`, which builds
`test_packet_structs` from `test_packet_structs.cpp` — a **.cpp** target that
already links what it needs. **No edit.**

## `tests/` and `scripts/` — correction 10, and it is NOT optional

**Run BOTH greps at your tip and paste both.** They are not interchangeable:
the second finds string-compare source-path scans that a `readFile` grep cannot
see — J13's third site was exactly that and would have let a guard pass while
guarding nothing.

```bash
grep -rn 'packet_structs' tests scripts
grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u
```

At `09c99b0b`: **no test and no script reads this header as text.** The second
grep's exhaustive list of source paths any test opens contains
`include/client/eq.h`, `include/client/action/action_dispatcher.h`,
`include/client/events/renderer_intents.h`, `include/client/state/event_bus.h`
and `include/client/state/*` — **and not `include/common/packet_structs.h`.**
The `tests/` hits are `#include`s and prose comments about *eqemu's*
`eq_packet_structs.h`, a different file in a different repository.

**Expect nothing** (correction 73/77/80: "expect comment-only hits" has been
wrong in both directions three times). **A hit is the surprise — report it.**

## The real proof: the static_asserts

`tests/test_packet_structs.cpp` plus the 109 in-header `static_assert`s are what
makes this unit safe. **Both halves must be measured, not assumed.**

```bash
# 1. every assert survived, and none changed
git show "$BASE:include/common/packet_structs.h" | grep '^static_assert' | sort > /tmp/asserts_before.txt
cat include/common/packets/*.h | grep '^static_assert' | sort > /tmp/asserts_after.txt
diff /tmp/asserts_before.txt /tmp/asserts_after.txt && echo ASSERTS-IDENTICAL
wc -l /tmp/asserts_before.txt /tmp/asserts_after.txt   # expect 109 and 109
```

```bash
# 2. they still FIRE -- i.e. they are still inside a pack region.
#    Deliberately break one, confirm the compiler rejects it, then revert.
```
⚠ **This is the step that distinguishes "the asserts are present" from "the
asserts are meaningful", and it is the one thing a line count cannot tell
you.** Pick **`EnvDamage2_Struct`** — the T145 struct, in `travel_misc.h`,
whose whole history is being silently unpacked. Temporarily change its
`static_assert` from 31 to 36 (the padded value), rebuild **just that TU**, and
confirm the build **fails**. Then `git checkout` the file and rebuild.
**Paste both outcomes.** If changing 31 to 36 *compiles*, the struct is no
longer packed and **you have reproduced T145 — STOP and report.**

Do the same for one struct in a second header — **`Spawn_Struct` in
`spawn.h`** (385 bytes, and the only in-file consumer of the shared tint and
texture profiles) — so the check covers a cross-header dependency as well as a
same-header one.

## Includes and the preamble

⚠ **This unit deliberately departs from §Process's "each new TU carries the
original's full include block verbatim" rule, and the reason is stated here so
it is not a judgement call.** That rule exists to remove per-executor
judgement from `.cpp` splits. Here the original's include block is *three*
standard headers, every one of which every domain header needs, so "verbatim"
and "enumerated" produce the same three lines. **The prescribed block in
§Header shape IS the verbatim include block, plus the one `common.h` line that
§Correction 21 derives.** Nothing is trimmed and nothing is chosen.

All four lines are `#include`, which `verify_move_only.py` allowlists as
BOILERPLATE, so the eleven copies cost **zero** substantive verifier lines
(correction 54: a promoted line costs verifier lines only if its TEXT or its
COUNT changes — and boilerplate is exempt from both).

## File-header comments

Each of the eleven new headers gets the short block prescribed in §Header
shape, naming its sections and citing
`refactor/J14_packet_structs_split.md`. The umbrella gains the J14 note shown
in §The umbrella. ⚠ **Correction 74: do not aim for a line count — carry the
required content.** That is the unit's only non-move content;
**enumerate every line of it in your Review**, exactly as J07 did. **Change no
other comment**, and do not touch a single line of the 1-37 rules block.

⚠ **Correction 45/62: never quote, in a header comment, a token that any check
in this plan greps for.** The dangerous tokens here are `static_assert`,
`#pragma pack(push`, `#pragma pack(pop)` and `packet_structs` — a header comment
that spells any of them inflates this plan's own pack-pairing table, the
assert-multiset diff, or the CMake/tests grep. **Describe them in words**
("this header opens and closes its own one-byte packing region"). ⚠ **Audit
your own written headers against every grep in this plan before you run one.**

## Acceptance (the Batch J bar — every item, classified by EXIT CODE)

- ⚠ **`scripts/build-gles2-x86.sh` FIRST, BEFORE `ctest`** (corrections 41 and
  72). `scripts/ci/visual_gate.sh` runs against the **separate** `build-gles2/`
  tree; running `ctest` first tests whatever binary was there beforehand and
  **passes while proving nothing** — J25 measured a benchmark stamped 11:01
  against edits made at 11:55, and J30 observed the same directly (14:24 vs
  14:52). **Either build gles2 first, or re-run the gate standalone afterwards,
  and say in your Review which you did and what the benchmark's timestamp was
  against your last edit's.**
- **build clean**: `cmake --build build -j24`. Classify by **EXIT CODE**, never
  by grepping the log — `grep ' error '` **never** matches GCC, which prints
  `error:`. Capture `BUILD_EXIT=$?` **immediately**.
- ⚠ **A long build can end three ways** (corrections 23 and 65): killed at the
  600 s tool ceiling with **exit 143 (SIGTERM)**; **backgrounded** by the tool
  and later reporting its own code; or **completed with no exit file recorded at
  all**. ⚠ **An absent exit file is NO RESULT, not a bad one** — it is the
  fourth outcome and it fired on J28 and J34. Relaunch detached with a
  **unit-private exit file** and read the code from inside it; that is the
  practice the whole batch should have started with.
- ⚠ **After a real exit 143, scan for valid ELF magic — not size**
  (corrections 27, 29, 32, 56). Truncated files have been 50-377 MB of zeroes;
  **no size heuristic catches them**. A killed *link* makes gmake delete its
  output (absent), a killed *compile* can leave a truncated file, and a killed
  **`ar` is a third site** — `libwilleq_core.a`, four instances — which
  `build/bin/*` alone is blind to. **Scan binaries AND `lib*.a`:**
  ```bash
  for f in build/bin/* build/*.a build/**/lib*.a; do
    [ -f "$f" ] || continue
    readelf -h "$f" >/dev/null 2>&1 || echo "BAD $f"
  done
  ```
  `rm` every hit, then rebuild once.
- **full `ctest -j6`** — **NOT `-j24`**; the visual gate's CPU ceiling flakes
  above that. Zero new failures vs `refactor/test_baseline.txt`.
  - ⚠ `HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` is a
    known **LOAD-DEPENDENT FLAKE**, not a regression, and is **not** in
    `test_baseline.txt`. **Do not re-diagnose it** — re-run it alone first.
  - ⚠ `ctest` exit 8 during `GoogleTestAddTests.cmake` discovery is **not
    necessarily** the truncated-binary trap (correction 16): check with
    `--gtest_list_tests` before concluding a mid-link truncation.
  - ⚠ `test_irrlicht_camera_command` has 8 parameterized test NAMES embedding an
    ASLR-randomised pointer; they change on every relink. If you diff
    `ctest -N`, redact `72-byte object <...>` first.
  - **`ctest -R PacketStruct` must be green and must not shrink.** Record the
    test count before and after.
- ⚠ **the verifier — COMMIT FIRST, THEN VERIFY:**
  ```bash
  git -C /mnt/projects/fable/wt/<your slot> commit ...      # FIRST
  BASE=$(git merge-base fable HEAD)
  python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" --repo /mnt/projects/fable/wt/<your slot>
  ```
  **THREE distinct ways to get a confident-looking wrong answer, two of which
  share a signature:**
  1. **Omitting `--repo`.** It defaults to `.`, and an executor agent's shell
     cwd is **not** its slot (this project forbids `cd`; agent threads reset cwd
     between calls). Omitting it silently verifies `/mnt/projects/fable/main`
     and prints a loud FAIL full of some unrelated merged unit's lines. J02 hit
     exactly this.
  2. **The wrong base rev.** `fable..HEAD` is **WRONG** — units merge during a
     wave, so `fable` moves under you and `fable..HEAD` reports the *other*
     units' merged work, in reverse, as substantive removals. J01 saw 533
     spurious removals this way. **Use the fork point.**
     ⚠ **Failures 1 and 2 look IDENTICAL** and have different causes. An
     executor who "fixes" the base and re-runs without `--repo` is still
     verifying the wrong tree. **Check both before changing a line of code.**
  3. ⚠ **Running on an UNCOMMITTED tree.** The script prints
     **`No C++ files changed` and EXITS 0.** J06 hit this. It looks like a
     definitive pass and is not one.
  Record `$BASE` in the Review and paste the full output.
- ⚠ **Expected verifier result for J14: EXIT 0, `+0/-0` substantive.**

  **Derivation, from what this plan prescribes and nothing else** (correction
  19 — J09's plan contradicted itself by predicting a number a later section
  invalidated):
  - Every moved line — 120 structs, 5 enums, 27 constants, 109 `static_assert`s
    — keeps its **exact text** and its **count of 1**, so it cancels
    (correction 54).
  - The eleven copied preambles are `#include` / `#ifndef` / `#define` /
    `#pragma` / `namespace EQT {` / `}` / `#endif` and the new header comments
    are `//` — **all BOILERPLATE**.
  - `namespace EQT {` and `} // namespace EQT` change count 1 → 11: boilerplate,
    free.
  - `#pragma pack(push, 1)` / `(pop)` change count 1 → 11: `#pragma`,
    boilerplate, free.
  - The umbrella's eleven new `#include` lines: boilerplate.
  - No internal header, no test repoint, no signature change, no `using`.
  - **Total: ADDED 0, REMOVED 0 → exit 0.**

  Paste the full output and enumerate every flagged line. **Any line beyond the
  enumerated set is a real finding — chase it.** **NEVER contort code to force a
  green exit** (J04 correctly refused to). If the verifier exits 1 with exactly
  the predicted set, that is the §Process step-4 / correction-5 case:
  **expected, not a failure** — request orchestrator sign-off in the Review.
- ⚠ **the line-multiset check, as a named second proof.** `verify_move_only.py`
  compares *distinct* normalized lines and allowlists a bare `}` and every
  comment, so a range duplicated into two headers, or dropped from both, is
  invisible to it. **Not optional.**
  ```bash
  # bodies only: each created header stripped of its comment block, guard,
  # includes, namespace open/close, both pragmas and its #endif
  cat <the 11 bodies> | sort > /tmp/after.txt
  { git show "$BASE:include/common/packet_structs.h" | sed -n '47,61p'
    git show "$BASE:include/common/packet_structs.h" | sed -n '66,2209p'; } | sort > /tmp/before.txt
  diff /tmp/before.txt /tmp/after.txt && echo MULTISET-OK
  ```
  ⚠ **Define `body(header)` STRUCTURALLY — between the file's own
  `#pragma pack(push, 1)` line and its own `#pragma pack(pop)` line — never by
  an offset constant.** J23, J27, J28, J30 and J35 all did this, and it is why
  none of them produced J08's alarming false alarm. ⚠ **Declare count variables
  `local` in your proof helpers** (correction 17): J05's non-`local` `n`
  produced a 247-line shortfall that read exactly like a dropped range.
  **When a proof fails, suspect the proof first** (correction 20) — three
  units, three proof bugs, zero real defects.
- **byte-identical re-concatenation** — stronger and also cheap. Concatenate the
  eleven bodies **in original order** — `common` contributes **two** blocks,
  `47-61` first and `66-134` in its own place — and confirm the result is
  byte-identical to the two `sed` ranges above concatenated. ⚠ **Correction 7:
  a naive concatenation of a split that reorders anything silently
  under-counts and still looks clean. State the ordering convention you used.**
  Expect `cmp` exit 0 over **2,159** lines.
- **the `static_assert` proofs of §The real proof**, both halves.
- **the pack-pairing table** of §The pack hazard.
- **visual gate PASS with goldens untouched.** `git status --porcelain` must be
  empty afterwards. A re-bless would mean the split changed behaviour — **STOP
  and report** rather than re-blessing.
  - ⚠ **A gate FAIL is not necessarily a PIXEL fail — check WHICH metric
    failed** (correction 15). J05's first run failed on `befallen` p95 CPU
    11.684 vs a 10.948 ceiling with **zero image mismatches**; the re-run
    measured 4.676. That is contention, not your change. **Re-run.**
  - ⚠ A failure at the **RENDER** step means a `gles2_zone_benchmark` CMake
    add-site was missed. **This unit has no CMake site at all**, so a
    render-step failure would be a genuine surprise — report it, do not re-run
    past it.
- `scripts/build-gles2-x86.sh` **and** `scripts/build-arm-noble.sh`, both clean.
  ⚠ Correction 14: the wave-stretch figures on record are **worst cases
  observed, not forecasts.** J06's `ctest -j6` ran in 120.7 s — faster than
  J01's solo 3.0 min on a larger suite — and its ARM build completed first
  invocation. **Do not report a clean run as an anomaly.**
- `scripts/refactor/check_file_sizes.sh --files` on **every changed AND created
  file** — `include/common/packet_structs.h` and all eleven
  `include/common/packets/*.h` → **0 warn, 0 fail.** ⚠ Every created file is
  well under 1,500 by §The split's arithmetic; **compute the totals and paste
  them** rather than quoting this plan (correction 58).
- ⚠ **The banner sanity check needs `grep -cE`, not `grep -c`** (correction 28).
  `grep -c '^// ={4,}' <file>` returns **0** whatever the file contains, because
  `{4,}` is not a POSIX BRE interval — which reads exactly like "the banners
  vanished". Use `grep -cE '^// ={4,}'`. The original has **21**; the eleven new
  headers must sum to 21 and the umbrella to 0.
- ⚠ **`chmod 644` every CREATED file before `git add`; PRESERVE the original
  mode on `packet_structs.h`** (corrections 69 and J01's inverse). A `sed >`
  pass produces mode 755 and the commit records a spurious `100644 => 100755`;
  chmod-ing the *stays* file records the opposite. **Check
  `git diff --raw` shows no mode change on `packet_structs.h`.** Its mode at
  base is `100644` — **verify that yourself with `git ls-files -s`** rather
  than trusting this line.
- **`git diff --stat` on `include/` must name exactly twelve files.** Nothing
  under `src/`, `tests/`, `scripts/`, `docs/` or `CMakeLists.txt` may appear.

## Review must answer

1. **Line count at your tip** against the **2,215** this plan measured, and any
   range drift.
2. **Every property in §Measured, re-verified with your own greps and pasted.**
   If any has changed since `09c99b0b`, **STOP and report**.
3. ⚠ **Correction 21, explicitly.** Paste **both** greps **and both positive
   controls**, print the widened grep's **exit status** and a **count of its
   unfiltered matches** to prove it was live (J28's is the template), then name
   the tile this plan assigns each file-scope symbol and the tile it assigns
   each consumer, and confirm they agree. **Do not cite §Correction 21 —
   re-derive it.**
4. **Comment depth at every cut**, computed with the `//`-stripping counter,
   not asserted — including your resolution of the **381 vs 382** boundary.
5. **The preprocessor dump**, pasted, with its positive control.
6. **The pack-pairing table** for all twelve files, and confirmation that the
   umbrella carries no pack directive and that **no `#include` sits inside any
   push/pop region**.
7. **Both `static_assert` proofs** — the 109-line multiset diff, and the
   deliberate-break test on `EnvDamage2_Struct` **and** `Spawn_Struct`, with
   the compiler's refusal pasted.
8. **Every file-header comment line you wrote**, enumerated, plus your
   correction-45 audit of them against this plan's own greps.
9. **How many CMake add-sites you actually found**, against the **ZERO** this
   plan predicts, and the output of both `tests/`/`scripts/` greps.
10. **Both content proofs**, pasted, and the verifier's exit code plus `$BASE`.
    State whether the result matched the prediction; if not, enumerate every
    flagged line before concluding anything.
11. **Final line counts of every file**, computed not quoted; wall clock;
    slowest step; and whether wave concurrency stretched the verification tail.
    ⚠ **Do not report per-header struct counts as a headline** — derived counts
    have slipped for seven consecutive units while ranges were exact
    (corrections 66, 75, 79).
12. **Findings only, not fixes.** ⚠ **Do NOT attribute any stale `tests/`
    line-number citation to this split** (correction 24) — J06, J07 and J08
    each found every citation already wrong at their base commit.
    `docs/module_map.md` needs rows for the eleven new headers — **record it,
    do not act.**

## Territory

`include/common/packet_structs.h`, the new directory
`include/common/packets/` and the eleven headers in it, and this plan file.
**Nothing else** — not `src/`, not `tests/`, not `scripts/`, not `CMakeLists.txt`,
not `docs/`.

⚠ **J15, J16 and J17 are the other Wave-7 header units.** None of them touches
`include/common/`, and this unit touches nothing of theirs. **Do not "fix"
anything in `include/client/`.**

## STOP conditions

- A `static_assert` value differs before and after, or the 109-line multiset
  diff is non-empty.
- Changing `EnvDamage2_Struct`'s assert from 31 to 36 **compiles** — that is
  T145 reproduced.
- Any header ends with unbalanced `#pragma pack` push/pop counts, or any
  `#include` is found inside a push/pop region.
- Any cut boundary does not match §Every cut boundary at your tip and the drift
  is more than a few lines of uniform offset.
- The verifier reports any substantive line beyond the predicted set.
- Either content proof fails after you have satisfied yourself the proof itself
  is correct.
- The visual gate fails on an **image mismatch** (as opposed to the CPU p95
  metric).
- You find yourself wanting to edit a struct, delete dead content, change an
  assert, or add a header this plan does not name.

---

## Review (to be written by the executor)

### Verdict: PASS. Move-only, 109/109 asserts identical, both deliberate-break tests refused by the compiler, verifier exit 0 / +0 -0 as predicted.

Branch `task/J14-packet-structs-split`, cut from `fable` at
**`40429250142c20b3adaf407af677e0495d7532d0`** ("Batch J Wave 7: plan the four
remaining oversize headers (J14-J17)"). Slot 2. No Pi, no eqemu.

---

#### 1. Line count at my tip, and range drift

`include/common/packet_structs.h` = **2,215 lines** at `40429250` -- identical
to the plan's measurement at `09c99b0b`. **Zero range drift.** Every cut
boundary, every struct/enum/assert count and every named line number in
Measured held exactly.

#### 2. Measured, re-verified

| property | plan | mine | note |
|---|---|---|---|
| total lines | 2,215 | **2,215** | agree |
| `grep -c '^struct '` | 120 | **120** | agree |
| `grep -c '^enum '` | 5 | **5** | agree |
| `grep -c '^static_assert'` | 109 | **109** | agree |
| `grep -c 'static_assert'` | 111 | **111** | agree (2 prose mentions in 1-37) |
| guard | 39/40, `#endif` 2215 | **39/40/2215** | agree |
| includes | 42-44 | **42-44** | agree |
| namespace `EQT` | 46 open, 2211 close | **46/2211** | agree |
| nested `namespace Position` | 2192-2209 | **2192/2209** | agree |
| pack region | push 64, pop 2213 (after ns close) | **64/2213** | agree |
| conditional directives | 3 (39, 40, 2215) | **3** | agree |
| banner sections | 21 | **21 sections = 42 `====` LINES** | see below |
| file-scope constants | "27" | **23** | plan arithmetic error, see below |
| namespace-scope free functions | zero at col 0 | **zero** | agree |
| file mode | -- | **`100644`** (`git ls-files -s`) | agree |

Two corrections to Measured, neither of them tree drift:

- **Banners.** `grep -cE '^// ={4,}'` returns **42**, not 21. The file's banners
  are three-line blocks (rule line / title / rule line), so 21 *sections* =
  42 matching *lines*. The plan's acceptance item says "the eleven new headers
  must sum to 21" -- read as lines that is wrong for the original too. I
  conserved the measured quantity: **original 42, eleven new headers 42,
  umbrella 0.**
- **File-scope constant count.** The plan says 27 but its own breakdown is
  13 + `PET_BUFF_COUNT` + 9 = **23**, which is what I measure. The narrow grep
  returns **25 lines** = 23 constants + 2 `namespace` lines. No symbol has
  appeared or moved since `09c99b0b`; the 27 is a plan arithmetic slip, not
  drift, so I did not STOP.

#### 3. Correction 21, re-derived (not cited)

**Narrow grep** (`grep -nE '^static |^namespace|^const |^constexpr |^extern '`),
exit **0**, 25 lines:

```
46:namespace EQT {
49:static const uint32_t BUFF_COUNT = 25;
50:static const uint32_t MAX_PP_SKILL = 100;
51:static const uint32_t MAX_PP_LANGUAGE = 28;
52:static const uint32_t MAX_PP_AA_ARRAY = 240;
53:static const uint32_t MAX_PP_DISCIPLINES = 100;
54:static const uint32_t MAX_PLAYER_TRIBUTES = 5;
55:static const uint32_t MAX_RECAST_TYPES = 20;
56:static const uint32_t SPELLBOOK_SIZE = 400;
57:static const uint32_t SPELL_GEM_COUNT = 9;
58:static const uint32_t BANDOLIERS_SIZE = 4;
59:static const uint32_t BANDOLIER_ITEM_COUNT = 4;
60:static const uint32_t POTION_BELT_SIZE = 4;
61:static const uint32_t TEXTURE_COUNT = 9;  // materialCount
402:static constexpr int PET_BUFF_COUNT = 30;
1247:static const int32_t COINTYPE_CP = 0;  // Copper
1248:static const int32_t COINTYPE_SP = 1;  // Silver
1249:static const int32_t COINTYPE_GP = 2;  // Gold
1250:static const int32_t COINTYPE_PP = 3;  // Platinum
1253:static const int32_t COINSLOT_CURSOR = 0;
1254:static const int32_t COINSLOT_INVENTORY = 1;
1255:static const int32_t COINSLOT_BANK = 2;
1256:static const int32_t COINSLOT_TRADE = 3;
1257:static const int32_t COINSLOT_SHAREDBANK = 4;
2192:namespace Position {
```

**Widened grep** (`grep -nE '^[A-Za-z_][A-Za-z_0-9:<>,&\* ]*\('`):
**exit 0**, **109 unfiltered matches** -- the pattern is live on this file.
Filtered through `grep -v 'static_assert('` it returns **no output, exit 1**:
every one of the 109 is a compile-time size check at column 0. **There are zero
namespace-scope free functions in this header**, so the set of
(free-function definition, cross-tile consumer) pairs is empty by construction.
The three `Position::Extract*` helpers are `inline` and indented inside
`namespace Position` (2192-2209), invisible to the widened pattern; the narrow
one catches their enclosing namespace at 2192, and the whole region lands
intact in `gm.h`.

**Positive controls -- `src/client/position.cpp`** (correction 78; the
correction-51/61 controls are gone from their files):

| pattern | control result |
|---|---|
| narrow | `11:constexpr float position_eps = 0.0001f;` (1 line, exit 0) |
| widened | **31 matches**, exit 0 -- `15:inline bool ValueWithin(...)`, `19/30/40:std::string to_string(...)`, `49/53:bool IsOrigin(...)`, `60:float DistanceSquared(...)`, `68:float Distance(...)`, ... |

Both patterns are proven live before any empty result was believed.

**Symbol-to-tile agreement** (computed, not cited -- word-boundary scan over
the whole file, definitions excluded):

| symbol(s) | def line | def tile | in-file consumer lines | consumer tile(s) | agree? |
|---|---|---|---|---|---|
| `TEXTURE_COUNT` | 61 | common | 100, 131 | common | yes, same tile |
| `MAX_PP_DISCIPLINES` | 53 | common | 566, 1756 | character, travel_misc | yes, via `common.h` |
| `BANDOLIER_ITEM_COUNT` | 59 | common | 596 | character | yes, via `common.h` |
| `POTION_BELT_SIZE` | 60 | common | 616 | character | yes, via `common.h` |
| `MAX_PP_SKILL` | 50 | common | 1340 | merchant | yes, via `common.h` |
| `BUFF_COUNT`, `MAX_PP_LANGUAGE`, `MAX_PP_AA_ARRAY`, `MAX_PLAYER_TRIBUTES`, `MAX_RECAST_TYPES`, `SPELLBOOK_SIZE`, `SPELL_GEM_COUNT`, `BANDOLIERS_SIZE` | 49-58 | common | **none** | -- | external only, via umbrella |
| `PET_BUFF_COUNT` | 402 | combat | 406, 407 | combat | yes, same tile |
| all nine `COINTYPE_*`/`COINSLOT_*` | 1247-1257 | merchant | **none** | -- | external only, via umbrella |
| `Tint_Struct` | 70 | common | 81, 90-98, 100 | common | yes, same tile |
| `Texture_Struct` | 109 | common | 112, 121-129, 131 | common | yes, same tile |
| `TintProfile` | 87 | common | 103, **241** | common, **spawn** | yes, via `common.h` |
| `TextureProfile` | 118 | common | 134, **217** | common, **spawn** | yes, via `common.h` |

**Both dispatch-brief corrections confirmed independently:**
`Spawn_Struct` (217, 241) is the **only** cross-tile consumer of the shared
tint/texture types; `Illusion_Struct` (line 363) references **none** of them;
and `grep -rn 'WearChange_Struct' include/ src/` returns **nothing, exit 1** --
the struct does not exist in this tree. The rule discharging every cross-tile
row is that all ten non-`common` headers include `common/packets/common.h`.

#### 4. Comment depth at every cut (computed with the `//`-stripping counter)

```
depth before line    47 = 0   | (blank)
depth before line    62 = 0   | (blank)
depth before line    66 = 0   | /*
depth before line   135 = 0   | (blank)
depth before line   381 = 0   | /*
depth before line   382 = 1   |  * Spell Buff structure (in profile)
depth before line   536 = 0   | (blank)
depth before line   682 = 0   | /*
depth before line   805 = 0   | // =========...
depth before line  1052 = 0   | // =========...
depth before line  1128 = 0   | // =========...
depth before line  1398 = 0   | // =========...
depth before line  1633 = 0   | // =========...
depth before line  1829 = 0   | // =========...
depth before line  2210 = 0   | (blank)
final whole-file depth = 0
```

**381 vs 382, resolved: the block comment OPENS at 381.** Depth before 382 is
**1**, so cutting at 382 would have left an unterminated `/*` in `spawn.h` and
an unopened `*/` in `combat.h` -- exactly the J02 defect the plan flagged.
**I used spawn = 135-380 and combat = 381-535.** The arithmetic is unchanged
(246 + 155 = 401 = 380-135+1 + 535-381+1).

Every other cut sits at depth 0. All fifteen boundaries measured, none asserted.

#### 5. Preprocessor dump, with its positive control

`awk '/^[ \t]*#(if|ifdef|ifndef|else|elif|endif|define)/'` on
`include/common/packet_structs.h`:

```
39: #ifndef EQT_PACKET_STRUCTS_H
40: #define EQT_PACKET_STRUCTS_H
2215: #endif // EQT_PACKET_STRUCTS_H
```

Exactly three lines -- the include guard and nothing else. **Zero conditional
compilation anywhere in the file**, so no cut can land inside a conditional
region.

Positive control, same `awk` on `include/client/graphics/irrlicht_renderer.h`:
**24 directives** (`1/2` guard, `53 #ifdef WITH_RDP`, `56 #endif`,
`58 #if defined(EQT_HAS_DRM) && !defined(EQT_HAS_GLES2)`, `60 #endif`,
`1067 #ifdef EQT_HAS_GLES2`, `1069 #else`, `1071 #endif`, `1326 #ifdef WITH_RDP`,
`1365 #endif`, `1493/1504`, `1627/1629`, `1994/2007`, `2138/2140/2142`,
`2493/2514`, `2520 #endif`). The pattern is live; the three-line result is a
finding, not a broken grep.

#### 6. Pack-pairing table -- and a correction to the plan's own check

⚠ **The plan's prescribed grep (`grep -c 'pragma pack(push'` unanchored) is
confounded by three PROSE lines and reports a false imbalance.** Three comment
lines in this file spell the directives out: original line **12** (rule 1 of the
T147 RULES block, which stays in the umbrella and may not be edited),
original **1393** (the T147 note, now `merchant.h:288`) and original **1737**
(the T145 note, now `travel_misc.h:127`). Both tables, pasted in full:

**(a) the plan's unanchored grep -- three prose false positives:**

```
common/packet_structs.h                        push=1 pop=0     <- prose, rules block line 12
packets/character.h                            push=1 pop=1
packets/combat.h                               push=1 pop=1
packets/common.h                               push=1 pop=1
packets/gm.h                                   push=1 pop=1
packets/group.h                                push=1 pop=1
packets/guild.h                                push=1 pop=1
packets/merchant.h                             push=1 pop=2     <- +1 prose (T147 note)
packets/spawn.h                                push=1 pop=1
packets/travel_misc.h                          push=1 pop=2     <- +1 prose (T145 note)
packets/world_object.h                         push=1 pop=1
packets/zone.h                                 push=1 pop=1
```

**(b) DIRECTIVES ONLY, anchored at column 0 -- the check that means something:**

```
common/packet_structs.h                        push=0 pop=0
packets/character.h                            push=1 pop=1
packets/combat.h                               push=1 pop=1
packets/common.h                               push=1 pop=1
packets/gm.h                                   push=1 pop=1
packets/group.h                                push=1 pop=1
packets/guild.h                                push=1 pop=1
packets/merchant.h                             push=1 pop=1
packets/spawn.h                                push=1 pop=1
packets/travel_misc.h                          push=1 pop=1
packets/world_object.h                         push=1 pop=1
packets/zone.h                                 push=1 pop=1
```

Eleven pushes, eleven pops, each pair in its own file. **The umbrella carries
no pack directive at all.** (`grep -c '^#pragma pack'` on it = 0; the single
unanchored hit is prose inside the untouched RULES block. This is the
correction-45/62/78 class firing on the plan's own acceptance step -- but the
offending text is pre-existing and unmodifiable, so the fix is the anchored
grep, not a comment edit.)

**No `#include` inside any push/pop region**, in all twelve files -- verified by
a directive-depth walker rather than by inspection:

```
packet_structs.h        final_depth=0 includes_inside=NONE
packets/character.h     final_depth=0 includes_inside=NONE
packets/combat.h        final_depth=0 includes_inside=NONE
packets/common.h        final_depth=0 includes_inside=NONE
packets/gm.h            final_depth=0 includes_inside=NONE
packets/group.h         final_depth=0 includes_inside=NONE
packets/guild.h         final_depth=0 includes_inside=NONE
packets/merchant.h      final_depth=0 includes_inside=NONE
packets/spawn.h         final_depth=0 includes_inside=NONE
packets/travel_misc.h   final_depth=0 includes_inside=NONE
packets/world_object.h  final_depth=0 includes_inside=NONE
packets/zone.h          final_depth=0 includes_inside=NONE
```

**Declared structural deviation** (the unit's only one): the original pops
*after* `} // namespace EQT`; every new header pops *before* its namespace
close, so the pack region is exactly the struct region. Identical in effect,
strictly tighter, and it is the sole structural change this unit makes.

#### 7. Both `static_assert` proofs

**(a) the 109-line multiset diff**

```
$ git show 40429250:include/common/packet_structs.h | grep '^static_assert' | sort > asserts_before.txt
$ cat include/common/packets/*.h            | grep '^static_assert' | sort > asserts_after.txt
$ diff asserts_before.txt asserts_after.txt ; echo $?
0
$ wc -l asserts_before.txt asserts_after.txt
  109 asserts_before.txt
  109 asserts_after.txt
```

**ASSERTS-IDENTICAL.** 109 before, 109 after, byte-for-byte the same multiset
(108 `sizeof`, 1 `offsetof`). Re-run after the two deliberate-break tests were
reverted: still exit 0.

**(b) the deliberate-break test -- the proof they still FIRE**

Probe TU: `#include "common/packet_structs.h"` + `int main(){return 0;}`,
compiled `g++ -std=c++17 -fsyntax-only -I include`. Unmodified baseline:
**exit 0**.

**`EnvDamage2_Struct` (the T145 struct, `travel_misc.h:143`), 31 -> 36:**

```
travel_misc.h:143:41: error: static assertion failed: EnvDamage2_Struct must be 31 bytes
  143 | static_assert(sizeof(EnvDamage2_Struct) == 36, "EnvDamage2_Struct must be 31 bytes");
      |               ~~~~~~~~~~~~~~~~~~~~~~~~~~^~~~~
travel_misc.h:143:41: note: the comparison reduces to (31 == 36)
EXIT = 1
```

**The compiler REFUSES.** Decisively: GCC prints
`the comparison reduces to (31 == 36)`, i.e. `sizeof(EnvDamage2_Struct)` is
still **31** after the split. The struct is still packed. **T145 is NOT
reproduced.** Reverted; probe recompiles at **exit 0**.

**`Spawn_Struct` (`spawn.h:134`, the cross-header case), 385 -> 392:**

```
spawn.h:134:36: error: static assertion failed: Spawn_Struct must be 385 bytes
  134 | static_assert(sizeof(Spawn_Struct) == 392, "Spawn_Struct must be 385 bytes");
      |               ~~~~~~~~~~~~~~~~~~~~~^~~~~~
spawn.h:134:36: note: the comparison reduces to (385 == 392)
EXIT = 1
```

**Refused, and `sizeof(Spawn_Struct)` is still 385** -- which is the cross-header
result that matters: `Spawn_Struct` embeds `TintProfile` and `TextureProfile`
from `common.h`, so a `common.h` that popped its packing at the wrong place, or
an include pulled in while packing was active, would show up here as a size
change. It does not. Reverted; probe recompiles at **exit 0**.

Same-header (`travel_misc.h`) and cross-header (`spawn.h` -> `common.h`) cases
both covered. Neither STOP condition fired.

#### 8. Every file-header comment line I wrote, and the correction-45 audit

Each of the eleven domain headers carries **nine** comment lines: one or two
naming its sections, then a seven-line shared block. The shared block, verbatim:

```
//
// Wire layouts, extracted verbatim from the umbrella header by
// refactor/J14_packet_structs_split.md. The RULES for this file family live
// in the umbrella, include/common/packet_structs.h -- read them before
// touching a struct or a compile-time size check here.
//
// This header opens and closes its own one-byte packing region; no include
// sits inside it.
```

The per-header section lines:

| header | section line(s) |
|---|---|
| `common.h` | `// Shared wire constants and the colour-tint / texture profile layouts that` / `// the spawn structures build on.` |
| `spawn.h` | `// Login, world-entry, zone-entry and spawn wire layouts: LoginInfo through` / `// NewZone, SpawnAppearance and Illusion.` |
| `combat.h` | `// Buff, consider, combat-ability, corpse-money, chat, death and despawn` / `// wire layouts.` |
| `character.h` | `// Character-sheet wire layouts: bind points, AAs, disciplines, tributes,` / `// bandolier and potion belt, target, and the spell cast/memorize path.` |
| `zone.h` | `// Zone change, zone points and door wire layouts.` (one line) |
| `world_object.h` | `// World object and tradeskill wire layouts, plus track, resurrect, /who and` / `// inspect.` |
| `group.h` | `// Group wire layouts: the group action constants and the seven group` / `// packet structures.` |
| `merchant.h` | `// Vendor/merchant, money, trade, book, GM-training and pet-command wire` / `// layouts.` |
| `guild.h` | `// Guild, corpse-management and consent-system wire layouts, plus emote and` / `// the server message filter.` |
| `travel_misc.h` | `// Travel, group split, LFG, combat abilities, disciplines, banking and the` / `// phase 3 miscellaneous wire layouts.` |
| `gm.h` | `// Dueling, skills, tradeskill recipe, cosmetic, phase 4 miscellaneous, GM` / `// command and petition wire layouts, plus the position bitfield helpers.` |

The umbrella's eight new comment lines (lines 42-49; the 1-37 RULES block is
**byte-identical and untouched**):

```
// J14: this file was 2,215 lines of wire layouts. It is now an umbrella over
// include/common/packets/. Every struct, enum, constant and compile-time size
// check moved verbatim; nothing was edited, and the files that include this
// header see exactly the same declarations they saw before.
//
// Each domain header opens and closes its OWN one-byte packing region. This
// file must NOT wrap them in one -- see rule 1 above, and
// refactor/J14_packet_structs_split.md.
```

**Correction-45 audit of my own text, against every grep this plan runs:**

| grepped token | present in my comments? | consequence |
|---|---|---|
| `static_assert` | **no** -- I wrote "compile-time size check" | assert multiset unaffected (109 = 109) |
| `pragma pack(push` / `pragma pack(pop)` | **no** -- I wrote "one-byte packing region" | pack table unaffected; the three false positives in table (a) are all PRE-EXISTING prose I did not write and may not edit |
| `^// ={4,}` | **no** | banner conservation 42 -> 42 exact |
| `packet_structs` | **yes**, twice per domain header (22 lines) | **audited harmless**: every `packet_structs` grep in this plan scans `CMakeLists.txt`, `cmake`, `tests`, `scripts`, `benchmarks` -- none scans `include/`. Re-ran all of them after writing the headers: identical results. The plan's own §Header shape prescribes this exact citation while §File-header comments lists `packet_structs` as a dangerous token -- a self-contradiction (correction 78's shape). Resolved in favour of the citation, since the reference is load-bearing for a reader and provably outside every grep's scope. |

I also verified `namespace` was safe to write here (J34 had to avoid it): the
plan's narrow grep is only ever run against the **original** header, never
against the produced ones.

#### 9. CMake add-sites, and the tests/scripts greps

**CMake add-sites found: ZERO**, matching the plan's prediction exactly. This
project lists no headers in any source list.

```
$ grep -rn 'packet_structs' CMakeLists.txt cmake tests scripts benchmarks
```
returns 40 lines: 24 functional `#include "common/packet_structs.h"` lines in
`tests/*.cpp`, 12 prose comments about **eqemu's** `common/eq_packet_structs.h`
(a different file in a different repository), and four lines in
`tests/cmake/data_formats.cmake` naming the target `test_packet_structs`:

```
tests/cmake/data_formats.cmake:10:ADD_EXECUTABLE(test_packet_structs
tests/cmake/data_formats.cmake:11:    test_packet_structs.cpp
tests/cmake/data_formats.cmake:14:TARGET_LINK_LIBRARIES(test_packet_structs GTest::gtest_main)
tests/cmake/data_formats.cmake:15:GTEST_DISCOVER_TESTS(test_packet_structs)
```

That is a **.cpp** target that already links what it needs. **No edit.** I did
not name `tests/CMakeLists.txt` (correction 26), did not use `--include`
(correction 55), and greped the filename rather than lowercase CMake commands
(correction 12). No line numbers cited (corrections 59/76): the target is
`test_packet_structs`, in the `data_formats` fragment; locate it yourself.

**Both text-scan greps, at my tip:**

```
$ grep -rn 'packet_structs' tests scripts
```
-> the same 24 `#include`s + 12 eqemu prose comments + the four CMake lines.
**No test and no script reads this header as text.** `scripts/` contributes
**zero** hits of any kind.

```
$ grep -hn 'kRoot / "src\|kRoot / "include' tests/*.cpp | sort -u
```
-> 17 distinct source paths, all under `include/client/...` or
`src/client/...`: `include/client/eq.h`, `include/client/state/*`,
`include/client/events/renderer_intents.h`,
`include/client/action/action_dispatcher.h`,
`src/client/graphics/renderer_event_receiver.cpp`,
`src/client/graphics/renderer_frame_{render,input}.cpp`,
`src/client/eq_bridge_intents.cpp`, `src/client/action/action_dispatcher*.cpp`,
`src/client/help/renderer_command_catalog.cpp`, plus
`kGraphicsDir = kRoot / "src" / "client" / "graphics"`.
**`include/common/packet_structs.h` appears nowhere.** No surprise hit; nothing
was added to territory.

**`tests/test_reachability_sweep.cpp` sweeps `src` and `include` recursively**,
so all eleven new headers entered it automatically. All 14
`Reachability`-matching tests **passed** (`ReachabilitySweep.*` +
`HotbarReachabilityTest.*`, 0 failed of 14) -- the new headers contribute zero
new unreachable entries and zero parse changes.

#### 10. Both content proofs, and the verifier

**Proof 1 -- line multiset.** `body(header)` defined **structurally**: the lines
strictly between that file's own `^#pragma pack(push, 1)` and its own
`^#pragma pack(pop)`, minus the one blank line the template inserts on each
side (both asserted to be blank before stripping, so a mis-slice throws rather
than silently shortening). Bodies extracted from the **produced files**, never
from the scratch slices.

```
before lines 2159   after lines 2159
$ diff before_sorted.txt after_sorted.txt ; echo $?
0
```
**MULTISET-OK.**

**Proof 2 -- byte-identical re-concatenation.** Ordering convention: the eleven
bodies concatenated in **original file order**, `common` first. `common`'s two
source blocks (47-61 and 66-134) are *contiguous* under the movable-set
definition -- lines 62-65 are structural and belong to no tile -- so `common.h`'s
single body block already reproduces `47-61` immediately followed by `66-134`,
and no interleaving is required. Compared against
`sed -n '47,61p'` + `sed -n '66,2209p'` of the base blob:

```
$ cmp orig_ranges.txt recon.txt ; echo $?
0
$ wc -l recon.txt
2159
```
**Byte-identical over 2,159 lines.** Arithmetic: 15 + 2144 = 2159, and
84 + 246 + 155 + 146 + 123 + 247 + 76 + 270 + 235 + 196 + 381 = **2159**.
(Note spawn/combat are 246/155, not the plan's 247/154, because of the 381
boundary resolution in item 4; the sum is unchanged.)

**The verifier -- committed first.**

```
$ git -C /mnt/projects/fable/wt/slot2 commit ...        # FIRST
$ BASE=$(git merge-base fable HEAD)
40429250142c20b3adaf407af677e0495d7532d0
$ python3 scripts/refactor/verify_move_only.py "$BASE..HEAD" \
      --repo /mnt/projects/fable/wt/slot2
```

`$BASE` = **`40429250142c20b3adaf407af677e0495d7532d0`** -- the fork point, not
`fable` (which has moved under this wave). `--repo` passed explicitly. The tree
was committed before the run, so the `No C++ files changed`/exit-0 no-op could
not fire. Full output:

```
Non-C++ files changed (not verified, review separately):
  refactor/J14_packet_structs_split.md

Files verified: 12
Boilerplate: +244 / -1 lines (allowed)

PASS: move-only (modulo boilerplate).
VERIFIER_EXIT = 0
```

**Every flagged line enumerated.** The verifier reports **zero substantive
lines in either direction**; the entire delta is its BOILERPLATE allowlist
(`#include`, `#pragma`, `#ifndef`, `#define`, `#endif`, any comment,
`namespace ... {`, bare `}`). The `-1` is a single line, identified by
differencing the removals against the union of all twelve produced files:

```
removed lines with NO occurrence anywhere in the 12 new files:
   '// Compile-time struct packing'
```

That is original line 63, one of the four structural lines (62-65) this plan
designates as belonging to no tile -- each new header writes its own equivalent
and the umbrella keeps none. It is a comment, hence boilerplate, hence free.
The `+244` is the eleven preambles and the umbrella's rewritten head: eleven x
(9 comment + `#ifndef` + `#define` + 3-4 `#include` + `namespace EQT {` +
`#pragma pack(push, 1)` + `#pragma pack(pop)` + `} // namespace EQT` +
`#endif` + separating blanks), plus the umbrella's 8 new comment lines and 11
new `#include`s. Nothing outside that set appears.

**Result matched the prediction exactly: exit 0, +0 added / -0 removed
substantive.** The enumerated set is empty, so there is nothing beyond it to
chase. The derivation holds as written: every moved line kept its exact text
and its count of 1 and therefore cancels; the eleven copied preambles are
`#include` / `#ifndef` / `#define` / `#pragma` / `namespace EQT {` / `}` /
`#endif` / comments, all BOILERPLATE; `namespace EQT {` and the pack directives
change count 1 -> 11, and boilerplate is exempt from count changes too. No
`inline`, no `using`, no signature change, no test repoint.

`git diff --raw` shows **exactly twelve files**, all under `include/common/`:
`M include/common/packet_structs.h` (`100644` -> `100644`, **no mode change**)
and eleven `A ... 100644`. Nothing under `src/`, `tests/`, `scripts/`, `docs/`
or `CMakeLists.txt`. Created files were `chmod 644` before `git add`; the stays
file was rewritten in place and its `100644` preserved (verified with
`git ls-files -s` at base and `git diff --raw` after).

#### 11. Final line counts (computed), timings

| file | lines |
|---|---|
| `include/common/packet_structs.h` (umbrella) | **67** |
| `include/common/packets/common.h` | **112** |
| `include/common/packets/spawn.h` | **275** |
| `include/common/packets/combat.h` | **184** |
| `include/common/packets/character.h` | **175** |
| `include/common/packets/zone.h` | **151** |
| `include/common/packets/world_object.h` | **276** |
| `include/common/packets/group.h` | **105** |
| `include/common/packets/merchant.h` | **299** |
| `include/common/packets/guild.h` | **264** |
| `include/common/packets/travel_misc.h` | **225** |
| `include/common/packets/gm.h` | **410** |
| **total** | **2,543** |

2,215 -> largest file **410** (`gm.h`), 1,090 lines of headroom under the 1,500
ceiling. Finished size is `28 + range` for the ten headers carrying
`common.h`, `27 + range` for `common.h` itself (no `common.h` include) --
`zone.h` is `28 + 123 = 151` and shows as one line shorter in the header block
because its section description is a single line. The umbrella is
`37 (untouched RULES) + 30` = **67**, derived, not quoted.

`scripts/refactor/check_file_sizes.sh --files` on all twelve:
**`checked 12 file(s): 12 ok, 0 warn, 0 fail`**, exit 0.

**Acceptance bar, by exit code:**

| step | result |
|---|---|
| `cmake --build build -j24` (detached, unit-private exit file) | **exit 0**, 210 s. No 143, no truncation scan needed. |
| `scripts/build-gles2-x86.sh` (**BEFORE** ctest, correction 41) | **exit 0**, ~940 s |
| `ctest -j6` full suite | **3,498 tests, 2 failed** -> resolved to **1**, the baseline ALSA exception |
| `ctest -R PacketStruct -N` | **43 tests**, before == after (`test_packet_structs.cpp` untouched by this unit) |
| visual gate (`visual_gate`, in-suite) | **PASSED, 21.54 s** |
| goldens | **untouched** -- `git status --porcelain` shows only `M include/common/packet_structs.h` and `?? include/common/packets/` |
| `scripts/build-arm-noble.sh` | **exit 0**, ~660 s |
| `check_file_sizes.sh --files` x12 | **12 ok, 0 warn, 0 fail** |
| verifier | **exit 0, +0/-0** |

**Correction 41, satisfied and measured:** `build-gles2/bin/gles2_zone_benchmark`
is stamped **21:18:57**; my last header edit is **21:02:38**; `ctest -j6`
launched at ~21:23. The in-suite gate tested the current tree, not a stale one.

**The two ctest failures, both dispositioned:**

1. `AudioManagerTest.PreloadCommonSounds` -- the documented ALSA host quirk in
   `refactor/test_baseline.txt`. Expected.
2. `check_build_tree_orphans` -- **pre-existing build-tree litter, not this
   unit.** `build/bin/test_spell_group_targeting` is stamped **17:38 today**,
   hours before this branch existed, and its source was deleted by **T324**
   (`tests/cmake/spells.cmake:49` records the removal). cmake never prunes
   `bin/`. I deleted the stale binary from the gitignored build tree and the
   test **passes**. No source, test or CMake file was touched.

**Zero new failures vs `refactor/test_baseline.txt`.**

One `ctest` discovery-stage failure occurred on a `-R PacketStruct -N` listing
run issued while the GLES2 build was still settling
(`GoogleTestAddTests.cmake:112`, "Process terminated due to timeout", naming
`test_progressive_load_rearm`). Diagnosed per protocol before acting:
`readelf -h` exit 0 and `--gtest_list_tests` exit 0 with a full case list, so
the binary was intact -- cold-cache contention, the J05/J07/T323 class, not
truncation. Re-ran; clean.

**Wall clock ~95 min.** Mechanical work (measure, generate, both content proofs,
both break tests) was **under 20 minutes**; the fixed bar was everything else.
**Slowest step: `build-gles2-x86.sh` at ~940 s**, followed by ARM at ~660 s.
Wave concurrency (slot1 T327, slot3 J17) did stretch the tail -- the GLES2 build
needed two `wait_for.sh` invocations and the main build ran 210 s against a
warm tree -- but nothing exceeded a second poll, and per correction 14 that is
not an anomaly worth reporting as one.

Per corrections 66/75/79 I report **no per-header struct counts**: the ranges
are exact and proven byte-identical; derived counts are the quantity that has
slipped for seven consecutive units.

#### 12. Findings (recorded, not acted on)

1. **`docs/module_map.md` needs eleven new rows** -- one per
   `include/common/packets/*.h` -- plus an amended `packet_structs.h` row
   saying it is now an umbrella. **Recorded, not acted on** (out of territory).
2. **The plan's pack-pairing check is wrong as prescribed** (item 6). The
   unanchored `grep -c 'pragma pack(push'` reports `push=1 pop=0` for the
   umbrella and `pop=2` for two headers, purely from pre-existing prose that
   cannot be edited (one line is inside the untouched T147 RULES block). **Any
   future unit auditing this file family must anchor the grep at column 0.**
   This is correction 45/62/78's shape firing on a plan's own acceptance step
   for the *fourth* time -- and it is the first instance where the offending
   text is unmodifiable, so the check must change, not the comment.
3. **The plan's banner acceptance figure conflates sections with lines** (21 vs
   42). Conserved as lines; both numbers stated.
4. **The plan's "27 file-scope constants" contradicts its own breakdown** (23).
   Fifth instance of the prose-vs-table class (corrections 19/47/63/64/80).
5. **`spawn`/`combat` are 246/155, not the plan's 247/154** -- the 381 boundary
   the plan flagged as uncertain resolved to 381, as the plan's own contingency
   predicted. Sum unchanged at 2,159.
6. **The plan's §Header shape and §File-header comments contradict each other**
   on whether `packet_structs` may appear in a new header's comment. Audited
   and resolved (item 8); a future plan should settle it rather than leave it
   to the executor.
7. Stale build-tree binary `test_spell_group_targeting` (finding 2 of the ctest
   section) will re-appear in **any** worktree whose `build/bin` predates T324.
   Not attributable to any split. **Recorded.**
8. Per correction 24, I attribute **no** stale `tests/` line-number citation to
   this split -- I found none referring to this header at all, stale or
   otherwise, because no test reads it as text.
