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

(executor fills)
