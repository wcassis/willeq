# T224 — /who prints entries, not just a count; delete dead WldLoader::resolveTextureName

Slot: slot3. x86. NO eqemu at start (T220 holds it); code+tests first. Class A.
Do NOT touch include/client/bridge/*, src/client/bridge/*, eq_bridge.cpp
(T220), or eq_entities.cpp / eq_merchant.cpp / spell_manager.cpp / combat.cpp
(T221 stage-2 territory, about to dispatch).

## Defect 1 — /who (T218 side-finding)
`ZoneProcessWhoAllResponse` (src/client/eq_player_info.cpp) prints only the
player count and never parses the entries, so `/who` shows nothing useful.
eqemu's WhoAllReturnStruct / WhoAllPlayerPart layout: check eqemu's
common/eq_packet_structs.h if present under /mnt/projects, else the T178
write-up (`refactor/T178_*.md`, which fixed the `%1` count substitution) and
any capture in tests/captured_packets.h or data/captures. Parse each entry
(name, level, class, race, zone/guild as the struct provides, GM/anon/LFG
flags) and print lines in the official client's format using eq_strings
where a string id exists (T178 found the count string; the per-line
formatting strings are adjacent — look at 5xxx range near it). Test with a
constructed multi-entry packet (fixed bytes in the test) and, if a real
capture exists, with that too. Red/green measured.

## Defect 2 — dead resolver
`WldLoader::resolveTextureName` has exactly two hits (decl + def), zero
callers (testing_progress.md finding). Delete it. If per-mesh bbox header
fields in the same file are read by nothing (same finding), delete those
too — verify with grep first, keep the parse offset arithmetic correct.

## Verification
Full ctest; visual gate (defect 2 touches the WLD loader — goldens must be
byte-identical); Review in this file; evidence review/T224/. Live leg for
/who: only if the eqemu lock is free when you get there; otherwise report
pending. Commit: `T224: /who lists the returned players; dead WLD resolver deleted`.
