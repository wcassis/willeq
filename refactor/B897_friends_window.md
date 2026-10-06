# B897 — the Friends window (friends + ignore lists), GitLab #213

Offline pass. No server, no eqemu lock, no Pi. Live legs are owed (below).

## What the official client does — from its files, not inferred

| fact | source |
|---|---|
| One window, a TabBox of `FW_FriendsPage` + `FW_IgnorePage`; the Ignored page has no Contact/Who | `uifiles/default/EQUI_FriendsWnd.xml` (quoted in `include/client/graphics/ui/friends_layout.h`) |
| 168x288, `Style_Sizable false`, XML `<Location>` (196,50); per-resolution defaults | same XML; `defaults.ini` `[FriendsWindow]` XPos/YPos (no `Show=`) |
| Toggle key **Alt+F**, Alternate empty | `review/T377/oracle/T377_oracle_options_keys_UI_page1.png` ("Toggle friend window", eqstr 6599) |
| Both lists CLIENT-side; only `OP_FriendsWho` on the wire, no ignore opcode | `akk-stack/code/utils/patches/patch_Titanium.conf:542` |
| Persisted in `<Char>_<server>.ini` `[Friends] Friend0..99`, `[Ignored] Ignored0..99`, empty = `*NULL*`, CRLF | `Obian_project1999.ini`, `Logjam_project1999.ini`, … |
| Names stored AS TYPED (mixed case in one file); occupied slots always the LAST n | every `*_<server>.ini` in the install |
| Strings 12307/12308, 12977-12984, 13240-13244 | `eqstr_us.txt` (pinned byte for byte by the test) |
| World's friends-who reply carries `playerineqstring = 0xFFFFFFFF` (its only writer) | `world/clientlist.cpp` `ClientList::SendFriendsWho` |

## What was built

- `state::SocialListsModel` on `GameState::social()` (game thread), oracle strings and the command bodies:
  `include/client/state/social_lists.h`, `src/client/state/social_lists.cpp`.
- Persistence: `<cache dir>/ui/<Char>_<server>.ini` in the oracle's exact shape (all 100 keys, tail-packed,
  CRLF, foreign sections kept), via `state::SocialListsSync` once per `BridgeComponent::GameTick`
  (`social_lists_io.{h,cpp}`, `bridge_component_social.cpp`). Headless too. NOT through `UiPreferences`:
  that file is renderer-owned and absent headless, and the oracle keeps these lists in a different file.
  The window's open state IS UI state: `[FriendsWindow] Show=` in `UiPreferences`.
- Commands (registry 2, `command_processor_friends.cpp`): `/friends [name]` (`/friend`) toggle/list,
  `/ignore [name]` toggle/list, `/friendswho [names]` (no argument = stored list). `/friends` no longer
  aliases `/friendswho`. `/friendswindow` (registry 3) toggles the window.
- Ignore filter: `ChatComponent::ZoneProcessChannelMessage` drops an ignored sender's message before the
  `/reply` bookkeeping.
- Friends-who reply heading 12307 printed on the `0xFFFFFFFF` reply.
- Window: `friends_layout.h`, `friends_panel.{h,cpp}`, `renderer_frame_friends.cpp`; `SocialListIntent`
  (Add / Remove / Who) runs the same bodies as the commands; `GameEventType::SocialListsChanged` feeds it.
- Test: `tests/test_friends_window.cpp` (binary `test_friends_window`).

## Reconciled with the official-client capture

`review/UIGAP-ORACLE/213/friends.tsv` (two official Titanium clients, 2026-10-05) MEASURED, and fable now
does, each pinned by `tests/test_friends_window.cpp` citing the row:

| row | measured | fable |
|---|---|---|
| 1, 4, 10 | `/friend <name>` and `/ignore <name>` toggle; add/remove send nothing | same; no opcode on edit |
| 2 | `/friends`: "List of Friends", 17-dash rule, names, "You have N friend(s)." | same (`kFriendsListRule`) |
| 3, 5 | no existence check; stored as typed | same |
| 8 | self-ignore -> 13242 | same |
| 9 | ignore hides tell, say, shout, ooc, auction, emote CLIENT-side | exactly those (channel set + `OP_Emote` by its first word) |
| 1, 11, 17 | lists SORTED, filled from the END, written at once, `*NULL*` | same; the writer reproduces `ini_after_window.ini` byte for byte |
| 12 | Contact/Delete disabled without a selection, Add disabled while empty | drawn greyed and inert |
| 13 | Who, no selection: `OP_FriendsWho` "a,b", heading 12307 + 33-dash rule | same (heading on the reply) |
| 14 | Who with a selected friend: `OP_WhoAllRequest` for that name | same (`SendWhoAllRequest(name)`) |
| 15 | Contact: `/tell <name> ` into the chat input, unsent | same (T319 prefill) |

## Assumptions still awaiting oracle

1. The 100 cap and messages 12983 / 13244 (row 18: NOT captured) -- the issue's default.
2. Any friend login/logout notice (row 18: NOT captured) -- none implemented.
3. Matching is case-insensitive (no row adds a name differing only in case while present); the sort is
   case-insensitive (both measured pairs agree with it).
4. "Too long" (12981) = more than 63 characters; 13242 also for a too-long ignore name.
5. Add of a present name / Delete of an absent one: silent, no change.
6. `/ignore` with an empty list prints only the 13240 heading; argless `/friendswho` / Who with an empty list
   prints 12308 and sends nothing.
7. Group and guild channels are NOT filtered (row 9 measured six other channels).
8. Name Editbox: Return = Add; Add clears the box.
9. List row 14px, VScroll strip 12px (click upper/lower half = page); the mouse wheel does not scroll it.
10. `Style_Minimizebox` not drawn; the "Friends List Tip" window shown on first open (row 12) is not built;
    the window is opened by Alt+F / `/friendswindow` -- fable has no EQ-button > Players menu.
11. Click pass-through follows the existing static-UI pattern (a click is offered to every window).

## Live legs owed (#213 Leg O + Leg V)

- Leg O remainder: the 100 cap (12983/13244), login/logout notices, group/guild suppression, case-only
  duplicate adds.
- Leg V: scenarios `friends_who` (A stores B, B logs in, A presses Who → B in A's chat under 12307) and
  `ignore_tell` (A ignores B, B tells A, A's chat lacks the line). Not written: they need two new
  `IScenarioWorld` methods and a partner scenario; nothing was run against a server.
