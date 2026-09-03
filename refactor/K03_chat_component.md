# K03 — `ChatComponent` (family: chat)

Batch: `refactor/K_component_extraction.md` — §Mechanism (REVISED). Runs
alongside K02/K04 (Wave 1).

## Measured at `7ffc4522` (2026-09-03)

`src/client/eq_chat.cpp`, 645 lines, 13 `EverQuest::` bodies, one root
`CMakeLists.txt` site. `eq_strings.cpp` (16 bodies, string-table lookups)
is NOT in this unit — it is a candidate for free functions and gets its own
decision later; do not move it.

**Decision taken for this unit:** `AddChatSystemMessage`,
`AddChatCombatMessage`, `AddChatMissMessage` are OUTPUT PRIMITIVES with 30+
callers across every family (the same shape as `QueuePacket`). They STAY in
`EverQuest` for now; retargeting 30 call sites would collide with K02/K04's
territory. Record them in the Review as "core-candidates"; K-final decides
whether they become `ClientContext` services. Their bodies stay in
`eq_chat.cpp` → so `eq_chat.cpp` is NOT `git mv`'d whole: move the other
bodies out into `src/client/chat/chat_component.cpp` and leave the three
primitives (and nothing else) in `eq_chat.cpp`. Say in the Review how many
lines stayed.

| method | callers outside the family | tests |
|---|---|---|
| `DispatchSlashCommand` | `application_commands.cpp`, `application.cpp`, `eq_commands.cpp`, `action/command_processor_registration.cpp`, `help/merged_help.cpp`, `application.h` | 3 files |
| `ProcessChatInput` | `application_commands.cpp`, `eq_bridge_intents.cpp`, `eq_bridge_ui_audio.cpp`, `application.h` | 7 files |
| `SendChatMessage` | `ucs.cpp` (dead — BOARD B005), `eq_action_handler.cpp`, `action/action_dispatcher_social.cpp`, `action/command_processor_social.cpp`, `ucs.h`, `action/command_processor.h` | 1 |
| `ZoneProcessChannelMessage` | `eq_zone_dispatch.cpp`, `eq_enums.h` (a comment — check) | 2 |
| `ZoneProcessColoredText`, `ZoneProcessFormattedMessage`, `ZoneProcessSimpleMessage` | `eq_zone_dispatch.cpp` | 0 |
| `ZoneProcessEmote` | `eq_zone_dispatch.cpp`, `eq_zone_entry.cpp` | 0 |
| `ZoneSendChannelMessage`, `ZoneSendEmote` | none in src/include | 2 / 0 |

`ZoneSendEmote` has zero callers anywhere → delete (§5). `ZoneSendChannelMessage`
has test callers only → keep on the component, retarget the tests.
Re-measure everything at your tip.

| member | refs | elsewhere | decision |
|---|---|---|---|
| `m_unrecognized_command_fallback` | 2 | none | **move** |
| `m_last_tell_sender` | 2 | `action/action_dispatcher_social.cpp`, `action/command_processor_social.cpp` (readers) | shared non-core: stays as `owner_.m_last_tell_sender`; note the two readers for K12 |
| `m_string_db` | 1 | `eq_strings.cpp`, `eq_player_social.cpp` | stays, `owner_.` |
| `m_structs` | 0 in family | 0 tree-wide? measure | if zero: board as dead, do not touch |
| `m_bridge`, `m_character`, `m_combat_manager`, `m_command_registry`, `m_entities`, `m_game_state`, `m_zone_connection` | core | | `ctx_.` |

Predicted `eq.h` delta: −10 declarations (13 − 3 primitives) −1 member
+4 scaffolding ≈ **−7 or better**.

## Steps

Per §Mechanism. New files `include/client/chat/chat_component.h`,
`src/client/chat/chat_component.cpp` (moved bodies), `tests/test_chat_component.cpp`
(register next to `test_chat_channel_names`). Note `src/client/chat/` already
exists with one file — read it first; `include/client/chat/` likewise.

## Territory

`include/client/chat/chat_component.h` (new), `src/client/chat/chat_component.cpp`
(new), `src/client/eq_chat.cpp` (shrinks), `include/client/eq.h`,
`src/client/eq.cpp` (ctor), root `CMakeLists.txt`, `tests/test_chat_component.cpp`
+ its cmake, allowlist (one number), `docs/module_map.md` (chat row), and
CALL-SITE-ONLY edits in the caller files listed above plus the tests that
call retargeted methods. NOT `eq_strings.cpp`, NOT `ucs.cpp` (B005 deletes it
later; leave its dead call as-is — it does not compile against anything
that moves? verify: if `ucs.cpp` calls `SendChatMessage` on an `EverQuest*`,
retarget that one line too).

⚠ Same collision note as K02: K02/K04 edit the shared files at other lines;
the allowlist number will conflict at merge.

## Verification / Review

As K02.

---

## Review

Executed in `wt/slot2` on `task/K03-chat-component`, branched from `fable` at
`feb6b10b`. §Mechanism REVISED followed as written; no STOP condition hit.

### The three `eq.h` numbers

| | lines |
|---|---|
| at dispatch (`fable` `feb6b10b`) | **2,024** |
| predicted | **1,994** (−30) |
| measured after | **1,998** (−26) |

The prediction was built before writing anything, from the measured deletion
set: −26 (the public chat block, lines 353–378: six declarations plus the 20
comment lines documenting them), −6 (the six private `ZoneSend*`/`ZoneProcess*`
declarations), −2 (`m_unrecognized_command_fallback` and its one-line T29
note), +4 (`#include`, `Chat()`, `friend`, `m_chat`).

**The 4-line miss is entirely mine and entirely comment.** Two lines went to
explaining the surviving "Public chat interface" banner (it now heads only the
AddChat* trio, and a bare banner over three lines would have read as a
leftover), and two to the `Chat()` accessor's transition note, matching K01's
`Guild()`. No code line differs from the prediction. `file_size_allowlist.txt`
lowered 2,024 → **1,998**; `FileSizeCeiling` passes.

### Measured vs the plan's leads

**Methods.** 13 `EverQuest::` bodies in `eq_chat.cpp` confirmed. 10 moved, 3
stayed. Differences from the lead table, all found by `grep -rw` at the tip:

| lead | measured |
|---|---|
| `ZoneSendEmote` "has zero callers anywhere → delete (§5)" | **WRONG — it has one caller**: `SendChatMessage`'s T243 emote arm (`eq_chat.cpp:230`), inside the family. It is not dead; it MOVED with the family and is public on the component. Deleting it would have deleted `/emote`. This is the one lead that would have caused a behaviour change if followed. |
| `ZoneSendChannelMessage` "test callers only → keep on the component, retarget the tests" | **no test callers exist.** `test_eq_action_handler_transforms_real.cpp:218` and `test_chat_channel_names.cpp:70` are both COMMENTS naming it. Its only caller is `SendChatMessage` (`eq_chat.cpp:236`), in-family. Moved, zero retargets. |
| `SendChatMessage` callers include "`ucs.cpp` (dead — BOARD B005)" and `ucs.h` | **NOT this method.** `ucs.cpp:307` / `ucs.h:34` are `UCSConnection::SendChatMessage(const std::string&, uint8_t)` — a different class, a different signature. The plan's own "verify: if `ucs.cpp` calls `SendChatMessage` on an `EverQuest*`, retarget that one line too" resolves to **no**. `ucs.cpp` untouched. Real callers: `eq_action_handler.cpp` ×5 (570, 580, 670, 674, 683). `action_dispatcher_social.cpp:65` and `command_processor_social.cpp:31` are comments; `action/command_processor.h:82` is a comment. |
| `DispatchSlashCommand` callers include `application.cpp`, `application_commands.cpp` ×2, `eq_commands.cpp`, `command_processor_registration.cpp`, `merged_help.cpp`, `application.h`, "3 test files" | **only ONE is EverQuest's**: `application_commands.cpp:540` (`m_eqClient->DispatchSlashCommand`). Every other hit is `Application::DispatchSlashCommand` — its own same-named method (`application.h:331`) — or prose. The three test files call `app.DispatchSlashCommand`, i.e. Application's. **One retarget, zero test edits.** |
| `ProcessChatInput` callers "`application_commands.cpp`, `eq_bridge_intents.cpp`, `eq_bridge_ui_audio.cpp`, `application.h`; 7 test files" | `application_commands.cpp` and `application.h` hits are all COMMENTS. Real: `eq_bridge_intents.cpp:360`, `eq_bridge_ui_audio.cpp:727`, and **`test_command_output_routing.cpp` lines 104 and 149** — the only test call sites in the unit. The other six test files mention it in comments. |
| `ZoneProcessChannelMessage` "`eq_enums.h` (a comment — check)" | confirmed a comment (`eq_enums.h:112`). Left alone. |
| `ZoneProcessEmote` "`eq_zone_dispatch.cpp`, `eq_zone_entry.cpp`" | `eq_zone_entry.cpp:710` is a comment (a T243 note about the OP_Animation rename). One real call site. |

Undocumented dependency the plan's tables did not list, and the reason the
retarget count is 3 higher than the method count: **`SetUnrecognizedCommandFallback`**
is an inline setter in `eq.h` for the member that moves. Under §Mechanism 5 it
had to move with its member and its declaration be deleted, so
`application_commands.cpp:484` and `tests/test_eq_action_handler_hotbar_command.cpp:58`
were retargeted too. It is not a `EverQuest::` body in `eq_chat.cpp`, so no
lead table would have caught it.

**Members.** 

| member | lead | measured | decision |
|---|---|---|---|
| `m_unrecognized_command_fallback` | 2 refs, none elsewhere | 1 code ref in `eq_chat.cpp` + the inline setter + the declaration | **moved** into the component |
| `m_last_tell_sender` | 2, readers in `action/` | 2 writes in `eq_chat.cpp`; the two `action/` hits are COMMENTS, but `eq.h:469`'s `GetLastTellSender()` accessor is real and `eq_action_handler.cpp` reads it 3× (678, 687, 691) | **stays**, `owner_.m_last_tell_sender`. K12 still owns the eventual move |
| `m_string_db` | 1 | 1 here, plus `eq_strings.cpp` ×6, `eq_player_social.cpp` ×1, and `GetStringDatabase()` | **stays**, `owner_.m_string_db` |
| `m_structs` | "0 in family; 0 tree-wide? measure" | **the member does not exist.** `grep -rw m_structs src include tests` returns nothing. A stale lead, not dead state — nothing to board |
| `m_bridge`, `m_character`, `m_combat_manager`, `m_command_registry`, `m_entities`, `m_game_state`, `m_zone_connection` | core | all seven confirmed used | `ctx_.` |

Non-member dependencies needing the §Mechanism 4 third substitution:
`s_debug_level` (5 sites) and **`DumpPacket()` (2 sites)** — both `static` on
`EverQuest`, both qualified `EverQuest::`. `AddChatSystemMessage` (2),
`GetStringMessage` (1) and `GetFormattedStringMessage` (2) stayed behind and
became `owner_.`.

### What stayed in `eq_chat.cpp`

**67 lines**: a rewritten 10-line file header, 8 lines of includes (the set
trimmed to what the three bodies actually need), and the three AddChat*
primitive bodies plus the T156 hotbar note that has always sat under them.
The three are recorded here as **core-candidates** for K-final: 30+ callers
across every family, the same shape as `QueuePacket`. Retargeting them was
correctly out of scope — it would have touched K02's and K04's files.

`ChatChannelName()` (T320's single channel-name table) is a free function
declared in `eq_enums.h`, not an `EverQuest` member. It moved with the ten
bodies that call it, since nothing left in `eq_chat.cpp` uses it.

### Multiset residual (§Mechanism 8)

Normalisation, applied to both sides: `s/ctx_\.character/m_character/` and the
same for `zone_connection`, `bridge`, `game_state`, `command_registry`,
`combat_manager`, `entities`; then `s/owner_\.//`, `s/EverQuest:://`,
`s/eqt::chat::ChatComponent:://`, trailing whitespace.

Old side: `eq_chat.cpp` at `feb6b10b` (645 lines). New side:
`eq_chat.cpp` (67) + `chat/chat_component.cpp` (607) concatenated.

**Residual on code lines: ZERO.** With comment, blank and `#include` lines
filtered out, both sides are **393 lines and the sorted multisets are
byte-identical** (`diff` exit 0). Every residual line in the unfiltered diff is
a comment, a blank, or an `#include` — the two new file headers, the K03
transition note, and the include set split between the two TUs.

That is a stronger statement than K01 could make (its residual was 10 added
comment lines): here the moved-code multiset is provably unchanged, which is
what "verbatim except the three substitutions" is supposed to mean.

### Retarget list

| file | sites | change |
|---|---|---|
| `include/client/eq.h` | 12 declarations | **deleted**: `SendChatMessage`, `ZoneSendEmote`, `ProcessChatInput`, `DispatchSlashCommand`, `SetUnrecognizedCommandFallback`, `ZoneSendChannelMessage`, and the five `ZoneProcess*`. `Chat()` is all that was added |
| `src/client/application_commands.cpp` | 2 | `m_eqClient->DispatchSlashCommand` / `->SetUnrecognizedCommandFallback` → `->Chat().…` |
| `src/client/eq_action_handler.cpp` | 5 | `m_eq.SendChatMessage(…)` → `m_eq.Chat().SendChatMessage(…)` |
| `src/client/eq_bridge_intents.cpp` | 1 | `ProcessChatInput(i.text)` → `m_chat->ProcessChatInput(i.text)` |
| `src/client/eq_bridge_ui_audio.cpp` | 1 | same shape |
| `src/client/eq_zone_dispatch.cpp` | 5 | `ZoneProcess*(p)` → `m_chat->ZoneProcess*(p)` |
| `tests/test_command_output_routing.cpp` | 2 | `client().ProcessChatInput(…)` → `client().Chat().ProcessChatInput(…)`. **Assertions untouched** |
| `tests/test_eq_action_handler_hotbar_command.cpp` | 1 | `eq.SetUnrecognizedCommandFallback` → `eq.Chat().SetUnrecognizedCommandFallback`. **Assertions untouched** |
| `src/client/ucs.cpp`, `include/client/ucs.h` | **0** | different class, see above |

### ⚠ Deviation: a text-reading guard that the split would have silently gutted

`tests/test_chat_channel_names.cpp`'s `NoSecondCopyOfTheTableInEqChat` reads
`src/client/eq_chat.cpp` **as text** and fails if a channel is mapped to a name
inline. §Mechanism 7 names `test_duplicate_state_stores` and
`test_reachability_sweep` as the text readers to watch; this is a **third one
the batch doc does not list**, and it is family-specific — it is exactly the
guard T320 left behind after finding three drifted copies of the table.

After the split, `eq_chat.cpp` holds only the three AddChat* bodies, so the
guard would have kept passing while watching a file that no longer contains any
of the code it was written for. **Fixed by making it scan BOTH TUs** (loop over
`/src/client/eq_chat.cpp` and `/src/client/chat/chat_component.cpp`, same
needle, same assertion). The assertion and the failure semantics are unchanged;
only the input path list and one message string moved.

This is the class of thing §Mechanism 7's STOP is for. It was not stopped on
because the fix is a path-list edit inside a test whose file the unit owns and
whose assertion is untouched — but it is flagged here rather than absorbed,
because **every later K unit must grep for text readers of the TU it is
splitting, not just the two the batch doc names.** See correction 1.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0**, clean on the FIRST attempt and on every rebuild. No timeout, no exit 143, no truncation scan needed |
| `ctest --test-dir build -j6 --output-on-failure` | **3,517 / 3,519 passed**, 42 skipped, 145 s. Two failures on the first run: `AudioManagerTest.PreloadCommonSounds` (the known ALSA quirk) and `DocCitations` — the latter **caused by this unit**, a `docs/module_map.md` row citing `chat/chat_component.cpp` repo-relative instead of `src/client/chat/chat_component.cpp`. Fixed; `DocCitations` and `FileSizeCeiling` both re-run **PASS**. Final state: 1 failure, the ALSA quirk only |
| `tests/test_chat_component.cpp` (new) | **14 tests, all pass** |
| `bash scripts/build-gles2-x86.sh` | **exit 0** |
| visual gate | **SKIPPED, with evidence**: `git status --porcelain -- src/client/graphics include/client/graphics` is **empty** — this unit changed no graphics file. Goldens untouched |
| `bash scripts/build-arm-noble.sh` | **SKIPPED**, same evidence |
| `scripts/refactor/check_file_sizes.sh --all` | **802 ok, 0 warn, 0 FAIL** with the ratchet lowered to 1,998 |

One test needed rewriting before it was correct, and the reason is worth
recording: `GroupSayWithNoGroupSaysSo` asserted T320's "You must be in a group"
line, which is **unreachable from a headless fixture** — `SendChatMessage`'s
zone-connection guard returns before it. Replaced with an assertion that the
zone-connection guard itself fires silently. No production code changed; the
first version of the test was simply wrong about the order of two guards.

### Corrections to §Mechanism

1. **§Mechanism 7's list of text readers is incomplete, and per-family.**
   It names `test_duplicate_state_stores` and `test_reachability_sweep`. K03
   found a THIRD, `test_chat_channel_names.cpp`, which reads the exact TU the
   unit splits. A guard that reads a source file by path does not fail when
   that file is emptied — it silently starts passing. **Every K unit must
   `grep -rn '<its TU basename>' tests/ scripts/` before splitting**, and the
   §Mechanism 7 stop condition should cover "a test reads my TU as text",
   not only "a test asserts on a moved member".

2. **§Mechanism 5's "a method with zero callers is deleted outright" needs the
   word EXTERNAL removed from the plan's tables, not from §Mechanism.** The
   K03 plan said `ZoneSendEmote` has "zero callers anywhere → delete", derived
   from a caller table whose column heading is *callers outside the family*.
   It has an in-family caller and deleting it would have removed `/emote`.
   **A plan's zero-caller claim must be re-measured with `grep -rw` over the
   moving TU as well**, and §Mechanism 5's rule should read "zero callers
   including inside the family it is moving with".

3. **A same-named method on another class makes a caller table useless as
   written.** Four of the K03 plan's six `DispatchSlashCommand` "callers" were
   `Application::DispatchSlashCommand`, and its `SendChatMessage` list was
   half `UCSConnection::SendChatMessage`. `grep -rw <name>` cannot tell them
   apart. **Plans should record the receiver expression (`m_eq.Foo`,
   `m_eqClient->Foo`, bare `Foo(` inside an `EverQuest::` body), not the
   name**, and executors must confirm the receiver before retargeting.
   Measured here: 12 declarations deleted, but only **17 real call sites**
   across 8 files, against a lead table implying roughly twice that.

4. **An inline accessor or setter in `eq.h` that touches a moving member is
   part of the move and no lead table sees it.** `SetUnrecognizedCommandFallback`
   is not a body in the family TU, so it appears in no method table, but its
   member moved and §Mechanism 5 forces its declaration out too. **Every K plan
   should list, alongside its member table, the inline `eq.h` accessors that
   read or write each moving member.** (`GetLastTellSender()` is the mirror
   case: its member stayed, so it stayed.)

5. **§Mechanism 8's residual can and should be ZERO on code lines.** K01's
   Review reworded the prediction to "new file-header commentary and new
   includes ONLY". K03 confirms that is achievable and makes it checkable in
   one command: normalise, then filter out `^\s*//`, blank and `^#include`
   lines, sort both sides, `diff`. **Exit 0 is the proof; anything else means
   the move was not verbatim.** Recommend every K unit paste that `diff` exit
   status rather than eyeballing a comment-heavy residual.

6. **A split (rather than a whole `git mv`) needs the include set re-derived
   for BOTH sides.** `eq_chat.cpp` kept 8 of its 13 includes and the component
   took 13; had the original list simply been copied to both, the residual
   check would still pass and the file-size and dependency story would be
   wrong. Worth one line in §Mechanism 4 for the units that split.

### Left for the board

Nothing new. `m_structs` — named in the K03 plan as possible dead state —
**does not exist at this tip**; there is nothing to board.
