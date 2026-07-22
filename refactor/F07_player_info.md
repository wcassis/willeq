# F07 — Split player-info getters and low-coupling misc clusters into eq_player_info.cpp

Sweep the many small low-entanglement clusters out of `src/client/eq.cpp`.
Pure move — no behavior change, no interface change, no reformatting.
This unit has the most scattered scope so far: TWELVE small clusters. Work
cluster-by-cluster; when in doubt about a boundary method, LEAVE IT and
document — a smaller clean unit beats a bigger doubtful one.

## Scope (original refs @ a0c02cd; enumerate by NAME)

Move to new `src/client/eq_player_info.cpp`:
1. Tribute/AA stubs (orig 6610–6679): TributeUpdate/Timer/Info, SendAATable,
   RespondAA, GuildTributes, AAStats, ExpZonein, WorldObjectsSent.
2. Player stat getters (orig 10628–10828): the ~40 one-line Get* accessors
   (GetSTR/GetHP/GetMana/GetPlatinum family).
3. GameState sync + group accessors (orig 10829–10977).
4. Tasks/DZ/BeginCast/ManaChange/Buff/PetBuff handlers (orig 12150–12431).
5. ApplyPoison / Track (orig 14252–14338).
6. Emote/AFK/Anon/RP/Camp/Logout (orig 15395–15553).
7. Rezz / WhoAll / Inspect (orig 15554–15800).
8. Corpse / Consent (orig 16027–16165).
9. Assist/Boat/Split/LFG/Shielding/EnvDamage/Discipline/Banker (orig 16166–16449).
10. Save/Popup/ClearObject/Duel/BindWound/Track (orig 16450–16633).
11. Surname/FaceChange/Random/FindPerson + social misc (orig 16740–16899).
12. CameraEffect / PlayMP3 / Sound (orig 16900–16976).

## Steps

1. For each cluster: grep/read boundaries, list every `EverQuest::` definition
   found, move the cluster verbatim, note anything left behind and why.
2. Create `src/client/eq_player_info.cpp`: header comment + pruned includes.
3. File-local static helpers: usual rule.
4. Add to `CLIENT_CORE_SOURCES` in CMakeLists.txt.
5. In the review, give a per-cluster table: methods moved / methods left /
   reason.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo the documented exceptions in
  refactor/test_baseline.txt (ALSA expected-fail; RDP flake → re-run isolated).
- `python3 scripts/refactor/verify_move_only.py BASE..HEAD` — PASS or
  documented de-static-ing (BASE = merge-base with fable if it advanced).
- `bash scripts/refactor/check_file_sizes.sh --files <abs>/eq_player_info.cpp`
  — ~1,600 lines: WARN accepted, must not FAIL.
- eq.cpp shrinks by ~1,400+ lines.

## Review

(executor fills in)
