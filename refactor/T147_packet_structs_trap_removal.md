# T147 — Delete the traps in `packet_structs.h`

**Slot: whichever is free. x86. No eqemu. No Pi.**

**DISPATCH AFTER T144 AND T145 ARE BOTH MERGED** — both edit this file and this
unit rewrites it wholesale.

**Territory: `include/common/packet_structs.h`, `tests/`, `refactor/`, plus
whatever consumers a deletion forces.**

## Why

The sweep that produced T144–T146 found that the file's real problem is not the
handful of live bugs — it is that **~28 structs are wrong AND dead**, several
named after the hottest packets in the game (`ChannelMessage_Struct`,
`CombatDamage_Struct`, `Consider_Struct`, both `PlayerPositionUpdate*_Struct`),
carrying authoritative-looking `/*offset*/` comments and "Size: N bytes"
headers, and pointed at by comments elsewhere in the codebase. The next person
who does the natural `reinterpret_cast` gets the T134 failure mode.

Two of them are **actively contradicted by correct code sitting next to them**:
`eq_chat.cpp:31-32` documents the right `unknown(8)` layout while
`ChannelMessage_Struct` declares a 4-byte `cm_unknown4`; `eq_combat_actions.cpp`
and `combat.cpp` each carry private, correct copies of `Consider_Struct`,
`Action_Struct` (as `ActionPacket_Struct`) and `CombatDamage_Struct`. That is
duplicated-then-drifted, and the shared copy is the one that rotted.

## The work

1. **Delete, do not repair, the dead-and-wrong structs.** A wrong dead struct
   is a trap; a deleted one cannot be miscast. 23 have zero references in
   `src/` or `include/`; 5 more are referenced only from comments. Re-derive
   both lists yourself — do not trust a count from a report. **Do not delete
   `Tint_Struct`, `Texture_Struct`, `BandolierItem_Struct`,
   `PotionBeltItem_Struct`** — dead by name but structurally live inside
   `TintProfile`/`TextureProfile`/`Bandolier_Struct`/`PotionBelt_Struct`.
2. **Fix and keep** `ChannelMessage_Struct` (144 → 148; `cm_unknown4` becomes
   `uint32_t[2]`) and `Consider_Struct` (24 → 28; append `pvpcon` +
   `unknown3[3]`), since both have live parsing code that should eventually use
   them instead of hand-rolled offsets.
3. **`Action_Struct` needs a decision, not a fix.** It is used purely as an
   in-memory DTO for `SpellManager::handleAction()` — `eq_combat_actions.cpp:149`
   parses the wire with a local 31-byte copy and hand-copies fields in. Its
   layout is harmless today, but it looks like a wire struct while being
   neither. Either rename it (`SpellActionInfo`) and strip its offset comments,
   or make it wire-accurate at 31 and use it directly. **Recommend one in the
   Review; do not leave it ambiguous.**
4. **Move `#pragma pack(pop)`.** It sits at line 1361 while struct definitions
   run to ~1986 — roughly 40 structs compile at default alignment. Verified
   latent, not live (`-fpack-struct=1` shows no current layout differs), but the
   next `uint16` followed by a `uint32` added in that region silently gains
   padding and the assert gets updated to match the padded value, which is the
   exact failure mode this whole arc exists to catch. Move the `pop` to just
   before the closing `#endif`. **Re-run the layout tests after moving it** —
   if anything shifts, that is a live bug, not a cleanup.
5. **Add the verified `static_assert`s** for structs that lack them, at the
   eqemu-verified values. Re-derive the values; do not copy a list.
6. **Generalize T144's captured-bytes test** into a small harness so future
   structs can be pinned to real observed bytes rather than to `sizeof`. This
   is the missing gate — no captured-bytes test existed for any packet struct
   before T144, which is why every bug in this arc survived its assert.

## Unsettled — do not paper over

- **`Death_Struct`**: our field order matches `eq_packet_structs.h:1366` but is
  reversed relative to `titanium_structs.h:1175`. The call that ours is correct
  rests on `titanium.cpp` having no `ENCODE(OP_Death)` — reasoning, not
  observation. Same caveat for `Animation_Struct`, `Consider_Struct`,
  `MobHealth_Struct`. Only a live capture settles these. If T146 leaves the
  eqemu lock free and a capture is cheap, take it; otherwise record them as
  open in `deferred.md` rather than asserting a value you have not seen.
- **`PetitionUpdate_Struct`**: eqemu's counterpart contains a `time_t`, which is
  not wire-credible as written, so we cannot tell whether ours is wrong or
  eqemu's is unmaintained. Low priority — record, do not guess.
- **`TradeAcceptClick_Struct`** byte 4: size 8 is certain; whether it is a
  meaningful `accepted` flag or eqemu's `unknown4` garbage is not.
- **`FinishTrade_Struct`**: eqemu sends `OP_FinishTrade` with a **zero-length**
  payload (`client_packet.cpp:15358`); our 2 "unknown" bytes are fictional.
  `eq.cpp:1286` guards on length and falls back to a zeroed struct so nothing
  breaks. Delete the struct.

## Verification

Full protocol bar. This unit deletes a lot; the suite and both builds are the
gate that nothing deleted was load-bearing.

## STOP conditions

- Moving `pack(pop)` changes any struct's layout — stop, that is a live bug
  needing its own unit.
- A "dead" struct turns out to have a consumer you did not find by grep.

## Review

Executed in `/mnt/projects/fable/wt/slot2`, task branch
`task/T147-trap-removal`, off `fable` tip `b5970ef5`.
**NO eqemu lock (T146 held it). Nothing in this unit was observed on the wire.**
Every claim below is verified against eqemu source at `/mnt/projects/claude/EQEmu/`,
against `offsetof`/`sizeof` experiments, or against the build + suite.

### Item 4 first: the `pack(pop)` move — STOP condition did NOT fire

Re-run from scratch here rather than taken on trust from T145. Method: generate
a program that prints `sizeof` for all 130 structs **and `offsetof` for all 612
named members**, compile it twice — once against the header as it stood, once
against a copy with the file-level `pop` moved to EOF and T145's local
`push`/`pop` around `EnvDamage2_Struct` removed — and diff the two outputs.

```
742 lines compared (130 sizeof + 612 offsetof)
diff sizes_orig.txt sizes_moved.txt   -> identical
diff off_orig.txt   off_moved.txt     -> identical    "NO OFFSET CHANGE ANYWHERE"
```

**Not one byte moves.** `sizeof`-identity alone would not have been enough
(that is exactly the trap this arc exists to catch — `Object_Struct` was 100
bytes and self-consistent), so the member-offset sweep is the load-bearing
half. The plan's STOP condition ("moving `pack(pop)` changes any struct's
layout") did not trigger.

Applied: the mid-file `pop` (was line 1390) is gone, replaced by a comment
saying where it used to be and why; a single `pop` now sits immediately before
the closing `#endif`; T145's local `push`/`pop` pair around `EnvDamage2_Struct`
is removed as now-redundant, with its comment rewritten to record that T147
did the file-wide fix. `EnvDamage2_Struct` still asserts 31.

**The plan's "verified latent, not live" wording was wrong and the prompt was
right to correct it.** T145 proved it live: writing `EnvDamage2_Struct`
correctly against eqemu made it measure 36, not 31, precisely because it sat
below the pop. The reason nothing moves *now* is that T145 already paid that
cost locally; the remaining ~40 structs in the region are alignment-neutral by
luck, not by design. The header's new banner comment says so, so the next
person does not re-add a pop.

### Every list re-derived — and the plan's counts are wrong in both directions

The plan quotes "23 with zero references", "5 comment-only", "~28 wrong AND
dead". I did not use those. I swept `src/`, `include/` and `tests/` for every
one of the 130 struct names, separately counting **`EQT::`-qualified**
references (the only way an outside TU can name these — there is no
`using namespace EQT` outside `EQT::Audio`/`EQT::Graphics`) and comment-only
mentions.

| measure | plan | re-derived |
|---|---|---|
| structs in the header | — | **130** (117 after) |
| dead in production (`EQT::` refs in `src/`+`include/` == 0) | 28 | **54** |
| ...of which **wrong** against eqemu | ~28 | **13** |
| ...of which dead **and correct** (kept, given asserts) | — | **41** |
| names removed from the header | ~28 | **13** |

The 13 removals break down as: **10** dead-and-wrong traps deleted, **1**
(`PetitionUpdate_Struct`) dead and unresolvable, deleted; **1**
(`FinishTrade_Struct`) live but describing a payload that does not exist,
deleted; **1** (`Action_Struct`) live but not a wire struct at all, relocated
and renamed. Two more dead-and-wrong structs — `ChannelMessage_Struct` and
`Consider_Struct` — were **fixed and kept** per the plan, which is why 13
wrong-among-the-dead produces only 11 removals from that set.

Two corrections worth stating plainly:

1. **The dead set is nearly twice the plan's estimate (54, not 28).** 41% of
   this header has no production consumer at all. The reason is structural:
   `src/client/` parses essentially every packet with hand-rolled byte offsets
   (`p.GetUInt32(14)`), not by casting these structs. `Spawn_Struct` — 385
   bytes, the biggest and most-cited struct in the file — has **zero**
   `EQT::Spawn_Struct` references in production; `eq_entities.cpp` mentions it
   only in comments while reading offsets by hand.
2. **The wrong-and-dead set is far *smaller* than the plan's estimate (13, not
   ~28) — and only 11 of those needed removing.** Most dead structs are dead
   but *correct*. I checked all 54 against
   eqemu field-by-field. Deleting a correct one destroys documentation for no
   safety gain, so the rule I applied is the plan's own: **delete iff dead AND
   wrong**; if dead and correct, keep it and give it a verified
   `static_assert` (item 5). That is where the 33 new asserts came from.

Arbiter rule applied per struct (T145's): if `titanium.cpp` has an
`ENCODE`/`DECODE` for the opcode, `Titanium::structs::` is the wire format;
otherwise the base `eq_packet_structs.h` struct goes raw. I extracted the full
`ENCODE|DECODE(OP_*)` list from `common/patches/titanium.cpp` (91 opcodes)
rather than checking case by case.

### DELETED — dead in production AND wrong (10)

| struct | ours | eqemu | eqemu source | how it was wrong |
|---|---|---|---|---|
| `CombatDamage_Struct` | 32 | **23** | `titanium_structs.h:1142` (ENCODE(OP_Damage) ⇒ Titanium) | invented `unknown05` at 5 shifts `spellid`/`damage`/`force` by one; two invented trailing uint32s |
| `HPUpdate_Struct` | 8 | **10** | `SpawnHPUpdate_Struct`, `titanium_structs.h:1250` | wrong name, wrong size, wrong order: wire is `cur_hp`/`max_hp`/`spawn_id`, ours was `spawn_id`/`cur_hp` |
| `MobHealth_Struct` | 6 | **3** | `titanium_structs.h:3312` | invented `unknown03[3]` tail |
| `Animation_Struct` | 8 | **4** | `titanium_structs.h:1110` == `eq_packet_structs.h:1300` | `action` declared uint16 (wire: uint8), fields in the wrong order (wire is `spawnid`/`speed`/`action`), invented `padding[3]` |
| `LootItem_Struct` | 12 | **16** | `LootingItem_Struct`, `titanium_structs.h:1467` | missing `lootee`, `slot_id` widened to uint32, missing `unknown3[2]` |
| `Emote_Struct` | 4 | **1028** | `titanium_structs.h:1964` (ENCODE(OP_Emote) ⇒ Titanium) | declared variable-length; the Titanium wire packet is a fixed `char[1024]` |
| `PlayerPositionUpdateClient_Struct` | 4 | **36** | `titanium_structs.h:1226` | declared 2 fields and a comment where the wire has 36 bytes of bitfields |
| `PlayerPositionUpdateServer_Struct` | 30 | **22** | `titanium_structs.h:1201` | expanded eqemu's bitfields into byte fields with invented `padding` — a completely different layout |
| `Attack_Struct` | 4 | *(none)* | — | no eqemu counterpart under any name; a byte-identical duplicate of `Target_Struct`, which *does* map to eqemu's `ClientTarget_Struct` |
| `Merchant_Open_Struct` | 16 | *(none)* | — | dead duplicate of `Merchant_Click_Struct` (eqemu has exactly one struct for `OP_ShopRequest` in both directions), drifted to different field names |

`PetitionUpdate_Struct` (112) was also deleted, as an eleventh, but on a
different footing — see "Unsettled" below: it is wrong-or-unknowable rather
than provably wrong, and deleting a dead struct is the one move that needs no
guess.

**The strongest single piece of evidence that these were the rotted copies:**
`src/client/combat.cpp:14-60` carries private, packed, **correct** copies of
`Consider_Struct` (28, with `pvpcon`), `Action_Struct` (31, uint16s),
`LootingItem_Struct` (16) and `ClientTarget_Struct` — sitting in the same
build as the shared header's wrong ones. `eq_combat_actions.cpp` does the same
for `Consider_Struct` (and guards on `2 + 28`, the correct size, while the
shared struct said 24). Duplicated-then-drifted, with the drift entirely on
the shared side. Deleting the shared copies removes the ambiguity rather than
creating it.

### DELETED — live but fictional: `FinishTrade_Struct`

Not dead (5 production references), deleted anyway because the plan is right:
eqemu sends `OP_FinishTrade` with a **zero-length** payload —
`new EQApplicationPacket(OP_FinishTrade, 0)`, `zone/client_packet.cpp:15358`
and `:15365` (verified; `parcels.cpp:594`/`:615` construct it with no size
either). Our 2 "unknown" bytes never existed.

Consumers changed: `TradeManager::handleFinishTrade(const FinishTrade_Struct&)`
→ `handleFinishTrade()`; `eq.cpp`'s `HC_OP_FinishTrade` case loses the
length guard and the zeroed-struct fallback; the forward declaration in
`eq.h` is gone. **Zero behavioural change** — the old guard was
`p.Length() >= 2 + sizeof(...)` on a 2-byte packet, so it always failed and
the handler always ran with a zeroed struct it declared `(void)` anyway.

### KEPT and FIXED (2)

| struct | before | after | evidence |
|---|---|---|---|
| `ChannelMessage_Struct` | `message` at 144 | **148**; `cm_unknown4` is `uint32[2]` | `titanium_structs.h:1020` == `eq_packet_structs.h:1189`; `ENCODE`+`DECODE(OP_ChannelMessage)` exist so Titanium arbitrates, and both agree. `src/client/eq_chat.cpp:31-32` already documented `unknown(8)` and parses at the right offsets — the struct contradicted correct code beside it |
| `Consider_Struct` | 24 | **28**; appended `pvpcon` + `unknown3[3]` | `eq_packet_structs.h:1351` == `titanium_structs.h:1160`; no `ENCODE(OP_Consider)` so the base struct arbitrates, and both agree. `eq_combat_actions.cpp:40` already guarded on `2 + 28` |

Both now carry `static_assert`s and a header comment naming the eqemu
file:line, the arbiter side, and — explicitly — that neither is wire-observed.

### KEPT deliberately, against the letter of "delete dead structs"

- **`Tint_Struct`, `Texture_Struct`, `BandolierItem_Struct`,
  `PotionBeltItem_Struct`** — the plan's carve-out. Confirmed: all four are
  nested inside `TintProfile`/`TextureProfile`/`Bandolier_Struct`/
  `PotionBelt_Struct` and deleting them would break those.
- **`Bandolier_Struct` and `PotionBelt_Struct` themselves have zero references
  anywhere** — the carve-out's premise is half-hollow. I kept them anyway
  (deleting them would strand the two the plan explicitly protects, and all
  four match eqemu exactly: 320 / 288 / 72 / 72). **Flagged as a deviation from
  the plan's stated rationale, not from its instruction.**
- **`Spawn_Struct` (385)**, dead in production but the most-cited layout in the
  codebase and byte-exact against `titanium_structs.h:256` (our
  `position_bitfield[5]` occupies the same 20 bytes as eqemu's five bitfield
  words). Correct ⇒ documentation, not a trap.
- **40 further dead-but-correct structs** (41 including `Spawn_Struct`), all
  verified: `AA_Array`,
  `BeginCast_Struct`, `BindStruct`, `BookRequest_Struct`, `BookText_Struct`,
  `ClientZoneEntry_Struct`, `CombatAbility_Struct`, `Consent_Struct`,
  `DeleteSpawn_Struct`, `EnterWorld_Struct`, `EntityId_Struct`,
  `GMToggle_Struct`, `GroupGeneric_Struct`, `GroupJoin_Struct`,
  `LoginInfo_Struct`, `LootRequest_Struct`, `ManaChange_Struct`,
  `MoneyOnCorpse_Struct`, `MoveDoor_Struct`, `NewSpawn_Struct`,
  `NewZone_Struct`, `PetCommandState_Struct`, `RequestClientZoneChange_Struct`,
  `ServerZoneEntry_Struct`, `SpawnAppearance_Struct`, `Target_Struct`,
  `TintProfile`, `TextureProfile`, `Tribute_Struct`, `ZoneChange_Struct`,
  `ZonePoint_Entry`, `ZonePoints_Header`, `Death_Struct` and the profile
  family above.

Two arbitration calls worth recording because they could have gone the other way:

- **`ManaChange_Struct`**: the base struct is 20 bytes (it has a trailing
  `int32 slot`), ours is 16. `ENCODE(OP_ManaChange)` exists in `titanium.cpp`,
  so the 16-byte Titanium struct is the wire format and **ours is right**.
  Taking the base struct as ground truth would have "fixed" a correct struct
  into a wrong one.
- **`DeleteSpawn_Struct`**: same shape — base is 5 bytes (trailing `Decay`),
  Titanium is 4, `DECODE(OP_DeleteSpawn)` exists, ours is right at 4.
- **`LoginInfo_Struct`**: eqemu's own trailing comment says `/*488*/`; the
  fields sum to **464**, which is what ours has. The comment is stale, not the
  struct. Noted next to the assert so nobody "fixes" it to 488.

### Item 3 — `Action_Struct`: RECOMMENDATION AND WHAT I DID

**Recommendation: rename it, do not make it wire-accurate.** Implemented as
`EQT::SpellActionInfo` in `include/client/spell/spell_manager.h`, offset
comments stripped, moved out of `packet_structs.h` entirely.

Three reasons, in order of weight:

1. **Making it wire-accurate would recreate the exact defect this arc exists to
   remove.** `eq_combat_actions.cpp:125-140` already declares a private,
   packed, correct 31-byte `ActionPacket_Struct` and parses with it. A second
   wire-accurate copy in the shared header gives us two copies of one layout —
   duplicated-then-drifted, which is precisely how `Consider_Struct`,
   `CombatDamage_Struct` and `Action_Struct` itself rotted in the first place.
   The correct copy is already at the parse site, where it belongs.
2. **It is not a wire struct and never was.** 44 bytes against a 31-byte
   packet, `uint32` where the wire has `uint16` for four fields, plus an
   invented `unknown_action` with no counterpart in either eqemu version. Its
   only job is to carry already-decoded values from `ZoneProcessAction` to
   `SpellManager::handleAction()`.
3. **Its presence in `packet_structs.h` was itself the trap.** The file's
   banner says every struct in it is packed to match the network protocol; the
   struct carried authoritative-looking `/*00*/ /*04*/` offset comments. Anyone
   doing the natural `reinterpret_cast<Action_Struct*>(body)` gets the T134
   failure mode from a struct that was never claiming to be right.

Blast radius: `spell_manager.h` (declaration + a 20-line comment explaining why
it must NOT be made wire-accurate), `spell_manager.cpp` (one signature),
`eq_combat_actions.cpp` (one local variable + comment), `test_spell_manager_timing.cpp`
(4 occurrences), `test_packet_structs.cpp` (one deleted size test). No
behavioural change: the field names and widths are identical, only the type
name and its home moved.

### Item 5 — 33 new `static_assert`s, derived before compiling

`static_assert` count 71 → **107**. Every new value was computed by hand from
eqemu's field list *first*, then compiled. All 33 held on the first compile —
which is the point: had I compiled first and written down what the compiler
said, the assert would agree with the code instead of with eqemu, and that is
the failure mode the whole arc is about. Each assert carries a trailing
comment naming its eqemu file:line, and where the arbiter mattered, which side
won and why (e.g. `ManaChange_Struct`, `DeleteSpawn_Struct`, `LoginInfo_Struct`).

I deliberately did **not** add asserts to production-live structs I did not
verify in this unit. An assert on an unverified struct is the trap, not the
gate.

### Item 6 — the captured-bytes harness

`tests/captured_packets.h` (new, 212 lines) + `tests/test_captured_packets.cpp`
(new, 110 lines, **7 tests, all passing**, new ctest target
`test_captured_packets`; one appended block in `tests/CMakeLists.txt` at EOF to
keep the merge with T146 trivial).

The harness is struct-agnostic:

- `CapturedPacket{name, opcode, bytes, size, provenance}` — bytes include the
  opcode; `provenance` is a required free-text field saying where the capture
  came from and **what outside authority corroborates the expected values**.
  T144's rule, made structural.
- `ExpectOpcode()` — a capture that is not the opcode it claims was pasted from
  the wrong packet.
- `ReadAt<T>()` / `ReadCStr()` — `memcpy`-based (unaligned reads through a cast
  are UB on packed captures), bounds-checked, and NUL-terminated-not-fixed-width
  for strings, which is T144's other finding baked in.
- `ExpectFields(capture, {U32("drop_id", offsetof(...), 199), ...})` — a
  declarative field table.

The one design decision worth defending: **the field tables are written against
`offsetof(EQT::Object_Struct, field)`, not against literal offsets.** T144's
tests drive `ParseGroundSpawn()`, so they guard the *parser*; these guard the
*struct*. If a field moves in the header, T144's tests keep passing (the parser
has its own literals) and these fail. That is the gate that was missing.

T144's three capture arrays moved out of `test_packet_structs.cpp` into the
harness header so both test binaries share one copy of the evidence rather than
duplicating it; T144's 8 behavioural tests are otherwise untouched and still
pass.

### Unsettled — recorded, not papered over

**No eqemu lock, so no capture was taken and none is claimed.** T146 holds it;
I did not block on its opportunistic `OP_Death` capture and it had not landed
when this unit finished. Five items written to `refactor/deferred.md` under
"T147 — packet structs that only a live capture can settle":

| struct | disposition | why it is open |
|---|---|---|
| `Death_Struct` | **kept**, assert 32, header comment naming both candidate orders | `eq_packet_structs.h:1367` (== ours) and `titanium_structs.h:1176` are exactly reversed either side of `spell_id`. No `ENCODE(OP_Death)` ⇒ arbiter says ours is right — reasoning from an absence, not observation |
| `Consider_Struct` | **fixed to 28** | eqemu's two versions agree and our own hand-rolled parser already guarded on 28; still source-reading, not wire |
| `Animation_Struct`, `MobHealth_Struct` | **deleted** | wrong by size against both eqemu versions, which agree. Deleting needs no guess; restoring one later would. `deferred.md` says: capture first, do not `git revert` these |
| `PetitionUpdate_Struct` | **deleted** | eqemu's counterpart holds a `time_t` (platform-width, not wire-credible) plus `quetotal`+`charname[64]` ours dropped. Cannot tell whether ours is truncated or eqemu's is unmaintained. Dead, so deletion is the move that avoids asserting 112 |
| `TradeAcceptClick_Struct` byte 4 | **kept as-is**, header comment | genuinely live (11 refs); size 8 certain; whether byte 4 is a real `accepted` flag or eqemu's `unknown4` garbage is unknown |

`deferred.md` also records the harness as the method for closing them.

### Verification bar

| step | result |
|---|---|
| 1. `cmake --build build -j24` (GLES2 config) | **PASS**, zero errors. The only warnings are the pre-existing `-Wpointer-arith` ones on `p.Data() + 2` in `eq.cpp`/`eq_merchant.cpp` |
| 2. `ctest --test-dir build --output-on-failure` | **2457/2458**, sole failure the documented ALSA `AudioManagerTest.PreloadCommonSounds` |
| 3. `bash scripts/build-gles2-x86.sh` | **PASS** (exit 0) |
| 4. Visual gate | **PASS** — `=== VISUAL GATE: PASS ===`; `git status tests/goldens` empty, **goldens untouched** |
| 5. `bash scripts/build-arm-noble.sh` | **PASS** (exit 0) |
| 6. `check_file_sizes.sh fable` (from slot2, on task branch) | **no new FAIL**. 16 files checked: 10 ok, 5 WARN, 1 FAIL. The FAIL is `src/client/eq.cpp` at 3902 — pre-existing (3903 on `fable`) and one line *shorter*. All 5 WARNs pre-existing: `eq.h` 2182→**2181**, `packet_structs.h` 2118→**2053**, and `eq_entities.cpp` (+2), `eq_merchant.cpp` (+3), `eq_movement.cpp` (+2), which grew only by the replacement comments in deviation 4. Both new test files are `ok` (197 / 110) |

Suite arithmetic: 2457 on `fable` (T144's +13 and T145's +11 over the 2433
baseline) → **2458** = +7 new captured-packet tests − 6 size tests deleted with
their structs. Exactly accounted for; no test was silently lost.

**The suite and both builds are the gate this unit is accepted on**, and they
are load-bearing in a specific way here: 13 struct definitions and a public
method signature were removed from the header. Any of them still reached by production code
would be a compile error, not a runtime surprise — C++ makes the "a dead struct
turns out to have a consumer you did not find by grep" STOP condition
self-enforcing, and it did not fire in either the x86 or the ARM build.

### STOP conditions — both evaluated, neither fired

1. **"Moving `pack(pop)` changes any struct's layout."** Did not fire; 742
   sizeof+offsetof assertions identical across the move (evidence above).
2. **"A 'dead' struct turns out to have a consumer you did not find by grep."**
   Did not fire. Both builds and the full suite pass. The one struct whose
   removal reached beyond the header was `FinishTrade_Struct`, and that was the
   plan's own instruction, not a missed consumer.

### Deviations — flagged, not absorbed

1. **The plan's counts were not used.** Re-derived: 54 dead (plan: 28), 11 dead
   **and wrong** (plan: ~28). Both directions are corrections. Detail above.
2. **`Action_Struct` was moved out of `packet_structs.h`, not just renamed in
   place.** The plan sanctioned "rename it (`SpellActionInfo`) and strip its
   offset comments"; relocating it to `include/client/spell/spell_manager.h`
   goes one step further. Justified because leaving a non-wire DTO in a header
   whose banner promises wire layouts is itself the trap the unit exists to
   remove. Touches `spell_manager.{h,cpp}`, `eq_combat_actions.cpp` and
   `test_spell_manager_timing.cpp`.
3. **`FinishTrade_Struct`'s deletion changed a public signature** —
   `TradeManager::handleFinishTrade()` now takes no argument
   (`include/client/trade_manager.h`, `src/client/trade_manager.cpp`,
   `src/client/eq.cpp`, `include/client/eq.h`). Forced by the plan's own
   instruction; behaviourally inert (the old length guard could never pass).
4. **Dangling comments in production code were repaired.** Deleting a struct
   that six comments point at leaves the comments as the next trap ("there is a
   shared struct for this"). Fixed in `eq_merchant.cpp`, `eq_movement.cpp`,
   `eq_entities.cpp`, `eq_combat_actions.cpp`, `eq.h` and
   `tests/test_eq_action_handler_vendor.cpp`, each replaced by the eqemu
   file:line the reader actually needs. Comment-only edits.
5. **`Bandolier_Struct`/`PotionBelt_Struct` kept despite having zero references
   anywhere** — see "KEPT deliberately" above. The plan's carve-out assumed
   they were live; they are not, but deleting them would strand the two structs
   the plan explicitly protects.
6. **Files in T146's territory were touched**: `tests/test_packet_structs.cpp`
   (6 size tests deleted, 2 updated, capture arrays moved out),
   `tests/test_eq_action_handler_vendor.cpp` (2 comment lines),
   `tests/CMakeLists.txt` (**one appended block at EOF**, per the dispatch
   instruction, so the merge stays trivial), plus two new test files. Editing
   `test_packet_structs.cpp` was unavoidable — it asserted `sizeof` on six
   structs this unit deleted.
7. **The plan's "verified latent, not live" claim about `pack(pop)` is recorded
   as wrong** in the header's banner comment as well as here, because that
   sentence is the one a future reader would use to justify adding a pop back.

### What this unit did NOT establish

- **Nothing was observed on the wire.** No lock, no server, no capture, no
  packet sent. Every "correct" and "wrong" above is a claim about agreement
  with eqemu's source.
- **`Consider_Struct` at 28 and `ChannelMessage_Struct` at 148 are not
  verified, only corroborated** — by two independent eqemu structs agreeing and
  by our own hand-rolled parsers already using those numbers.
- **Deleting a struct fixed no feature.** All 11 traps were dead; nothing
  behavioural changed for any of them. The value delivered is that the next
  `reinterpret_cast` cannot reach them.
- **The 41 dead-but-correct structs are still dead.** They now have asserts,
  which pins them if someone edits them, but nothing exercises them. Whether
  this header should exist at all — given that production parses everything by
  hand offset — is a real question this unit did not answer and did not have
  scope to.

