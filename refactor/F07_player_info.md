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

Base for verification: `c3e926272ac9e401b474744156ca4eae74255ec7`
(`git merge-base fable HEAD` at start of this unit — `fable` had not advanced
since the plan's original refs).

All twelve clusters were located by name (grep for the named methods) rather
than by the stale line numbers in the scope section above, since prior Batch F
units already shrank `eq.cpp`. Every cluster was moved verbatim (no
reformatting), in file order, into the new `src/client/eq_player_info.cpp`.

| # | Cluster (per scope list) | Methods moved | Methods left | Reason |
|---|---|---|---|---|
| 1 | Tribute/AA stubs | `ZoneProcessTributeUpdate`, `ZoneProcessTributeTimer`, `ZoneProcessSendAATable`, `ZoneProcessRespondAA`, `ZoneProcessTributeInfo`, `ZoneProcessSendGuildTributes`, `ZoneProcessSendAAStats`, `ZoneProcessSendExpZonein`, `ZoneProcessWorldObjectsSent` | `ZoneSendSendAATable`, `ZoneSendRequestGuildTributes`, `ZoneSendSendExpZonein` (senders, elsewhere in eq.cpp near packet-send helpers) | Senders live in a different, non-adjacent block (~line 1790) mixed with unrelated zone-request senders; moving them would pull in surrounding zone-request-phase code not in scope. Left per "smaller clean unit" rule. |
| 2 | Player stat getters | `GetLevel`, `GetClass`, `GetRace`, `GetGender`, `GetSTR`, `GetSTA`, `GetDEX`, `GetAGI`, `GetINT`, `GetWIS`, `GetCHA`, `GetCurrentHP`, `GetMaxHP`, `GetCurrentMana`, `GetMaxMana`, `GetCurrentEndurance`, `GetMaxEndurance`, `GetDeity`, `GetPlatinum`, `GetGold`, `GetSilver`, `GetCopper`, `GetBankPlatinum`, `GetBankGold`, `GetBankSilver`, `GetBankCopper`, `GetPracticePoints`, `GetWeight`, `GetMaxWeight` | `GetPosition`, `GetHeading`, `IsMoving`, `SetHeading`, `SetMoveSpeed`, `SetPosition`, `SetMoving` (position/movement accessors just above, different concern); `IsAFK`/`IsAnonymous`/`IsRoleplay`/`IsCamping` (Behavioral Flag Accessors, just below) | Position/movement accessors are a distinct concern not named in the cluster list. The four `Is*` flag getters are not named in this cluster or cluster 6 (which covers the `Set*`/timer *actions*, not these read accessors) — left as a boundary case per "when in doubt, leave it". |
| 3 | GameState sync + group accessors | `SyncEntityToGameState`, `RemoveEntityFromGameState`, `IsInGroup`, `IsGroupLeader`, `GetGroupMemberCount`, `GetGroupLeaderName`, `HasPendingGroupInvite`, `GetPendingInviterName`, `SyncGroupMemberToGameState` | `SetCombatStopDistance` (Combat State Sync, immediately after) | Different named phase (7.6, Combat State Sync), not part of the group-accessors cluster; left per boundary rule. |
| 4 | Tasks/DZ/BeginCast/ManaChange/Buff/PetBuff handlers | `ZoneProcessCompletedTasks`, `ZoneProcessDzCompass`, `ZoneProcessDzExpeditionLockoutTimers`, `ZoneProcessBeginCast`, `ZoneProcessManaChange`, `ZoneProcessBuff`, `ZoneProcessPetBuffWindow` | `ZoneProcessColoredText` (immediately after) | Belongs to a different chat/text-display concern, not named in this cluster. |
| 5 | ApplyPoison / Track | `SendApplyPoison`, `ZoneProcessApplyPoison`, `ZoneProcessTrack` | — | None left; cluster is complete. |
| 6 | Emote/AFK/Anon/RP/Camp/Logout | `PerformEmote`, `SetAFK`, `SetAnonymous`, `SetRoleplay`, `StartCampTimer`, `CancelCamp`, `UpdateCampTimer`, `SendCamp`, `SendLogout`, `ZoneProcessLogoutReply` | — | None left; cluster is complete. |
| 7 | Rezz / WhoAll / Inspect | `ZoneProcessRezzRequest`, `ZoneProcessRezzComplete`, `SendRezzAnswer`, `SendWhoAllRequest`, `ZoneProcessWhoAllResponse`, `SendInspectRequest`, `ZoneProcessInspectRequest`, `ZoneProcessInspectAnswer` | — | None left; cluster is complete. |
| 8 | Corpse / Consent | `SendCorpseDrag`, `SendCorpseDrop`, `SendConsiderCorpse`, `SendConsent`, `SendConsentDeny`, `ZoneProcessConsentResponse`, `ZoneProcessDenyResponse` | `ZoneProcessMoneyOnCorpse`, `RequestLootCorpse`, `LootItemFromCorpse`, `LootAllFromCorpse`, `DestroyAllCorpseLoot` (far away, loot-window subsystem) | Loot-window methods are a separate, larger, more entangled subsystem thousands of lines away — not adjacent to this low-coupling cluster and not named in scope. |
| 9 | Assist/Boat/Split/LFG/Shielding/EnvDamage/Discipline/Banker | `SendAssist`, `SendAssistGroup`, `SendBoardBoat`, `SendLeaveBoat`, `SendControlBoat`, `SendSplit`, `SendLFGCommand`, `ZoneProcessLFGAppearance`, `SendShielding`, `ZoneProcessEnvDamage`, `ZoneProcessDisciplineUpdate`, `ZoneProcessDisciplineTimer`, `ZoneProcessBankerChange` | — | None left; cluster is complete. |
| 10 | Save/Popup/ClearObject/Duel/BindWound/Track | `SendSave`, `SendSaveOnZoneReq`, `SendPopupResponse`, `ZoneProcessClearObject`, `SendDuelRequest`, `SendDuelAccept`, `SendDuelDecline`, `ZoneProcessDuelRequest`, `SendBindWound`, `SendTrackTarget` | `SaveEntityDataToFile`, `SaveHotbarConfig` (unrelated "Save" name collision, far away) | Different subsystems (debug entity dump, hotbar persistence) that only share the "Save" name prefix, not proximity or concern. |
| 11 | Surname/FaceChange/Random/FindPerson + social misc | `SendSurname`, `SendFaceChange`, `SendRandom`, `ZoneProcessRandomReply`, `SendFindPerson`, `ZoneProcessFindPersonReply`, `SendRewind`, `SendYellForHelp`, `SendReport`, `SendFriendsWho` | — | `SendRewind`/`SendYellForHelp`/`SendReport`/`SendFriendsWho` are the adjacent "social misc" senders referenced in the cluster name; included as they are contiguous and equally low-coupling. |
| 12 | CameraEffect / PlayMP3 / Sound | `ZoneProcessCameraEffect`, `ZoneProcessPlayMP3`, `ZoneProcessSound` | `PlaySound`, `PlaySoundAt`, `PlayCombatSound`, `PlaySpellSound`, `PlayUISound` (far away, ~line 15300s, audio-helper wrappers) | Different location and concern (audio helper wrappers vs. packet handlers), not adjacent, not named in scope. |

### Results

- `cmake --build build -j24`: clean, no new warnings beyond pre-existing ones in `eq.cpp`.
- `ctest --test-dir build`: 1191/1192 passed. Sole failure is
  `AudioManagerTest.PreloadCommonSounds` (documented pre-existing ALSA
  expected-fail in `refactor/test_baseline.txt`). No RDP flake observed this run.
- `verify_move_only.py c3e926272ac9e401b474744156ca4eae74255ec7..HEAD`: **PASS**
  (move-only, modulo +63 boilerplate lines — new file header comment +
  includes; `CMakeLists.txt` flagged as non-C++ and reviewed manually: single
  line adding `src/client/eq_player_info.cpp` to `CLIENT_CORE_SOURCES`).
- `check_file_sizes.sh --files eq_player_info.cpp`: **WARN** (2006 lines, over
  the 1500-line target) — accepted per plan; not a FAIL.
- `eq.cpp`: 15373 → 13460 lines (−1913). `eq_player_info.cpp`: 2006 lines new.

### Deviations from plan

- File landed at ~2006 lines, larger than the ~1600 estimate, because several
  of the twelve clusters (4, 7, 9) turned out longer than their line-count
  estimates suggested once located by name.
- A handful of boundary methods were left behind per the "when in doubt, leave
  it" rule — see the per-cluster table above. None of these omissions block
  future units; they remain in `eq.cpp` as documented candidates for a later,
  more targeted pass if desired.
