# T146 — Live wire acceptance for T145's nine fixes

**Slot: whichever is free. x86. eqemu lock REQUIRED. No Pi.**

**DISPATCH AFTER T144 RELEASES THE EQEMU LOCK AND T145 IS MERGED.**

## AMENDED 2026-08-19 after T144/T145 merged (`797d93e4`) — READ THIS FIRST

T145's execution corrected the orchestrator's scoping on four points. **The leg
list below is amended accordingly; where this section and the original list
disagree, THIS SECTION WINS.**

1. **Leg 3 (environmental damage) is CANCELLED — do not attempt it.**
   `OP_EnvDamage` is **client→server** (`ConnectedOpcodes[OP_EnvDamage]`,
   `zone/client_packet.cpp:198`); the server never sends it. Our receive path is
   unreachable dead code regardless of struct size, and T145 did **not** restore
   fall-damage messages. The real gap is a sender we do not have. Record it as
   out of scope; do not manufacture a scenario.
2. **Leg 5 (`/camp`) is downgraded to observation only.** `Handle_OP_Camp` has
   **no size check at all**, so `/camp` has been working the whole time and the
   malformed packet was silently tolerated. The question the original plan asked
   is already settled. Confirm `/camp` still works post-fix (a regression check,
   not a repair proof) and move on.
3. **Leg 7 (popup response) and leg 8 (boat control) CANNOT BE VERIFIED and are
   removed from the acceptance set.** `SendControlBoat` and `SendPopupResponse`
   have **zero production callers** — reachability pattern #10. There is no way
   to trigger either from a live session without first writing a caller, which
   is not this unit's job. Record both as **structurally fixed, unexercised**,
   and add them to `refactor/deferred.md` as needing a caller before any wire
   claim is possible. **Do not write callers to make a leg verifiable.**
4. **The real acceptance set is therefore four legs: `/face`, `/kick`, `/kill`,
   `/who`**, plus the `/camp` regression check and the inspect leg. These are
   the ones where a fixed struct meets an existing, reachable call path.

**You hold the eqemu lock; T147 runs in parallel and does not.** T147 has
unsettled structs (`Death_Struct`, `Animation_Struct`, `Consider_Struct`,
`MobHealth_Struct`) that only a capture can settle. **If you have spare session
budget after the four legs above, capture `OP_Death`** — die once, dump the
bytes, and record whether `bindzoneid`/`spell_id`/`attack_skill` appear in our
order or the reverse. That single capture closes T147's largest open question.
Put it in `review/T146/` even if you cannot fully decode it; raw annotated bytes
are worth more than a reasoned guess.

**Method precedent:** T144 produced the repo's first captured-bytes evidence and
its approach is the standard now — dump at the parse site, decode field-by-field,
and cross-check against an authority outside the client (T144 used the server's
own `object` DB table). See `review/T144/groundspawn_hexdump_annotated.txt`.


**Territory: `tests/`, `refactor/`, `/mnt/projects/fable/review/T146/`. This is
a verification unit — it should need no production-code changes. If it does,
that is a finding: report it, and fix only what the evidence forces.**

## Why this unit exists separately

T145 fixed nine things without an eqemu lock (T144 held it), so its acceptance
stopped at eqemu-source reading and `offsetof` tests. **That is exactly the bar
that certified three dead features in a row** (T132 guild, T145's own `/who` and
`/face`, both of which earlier units recorded as working). This unit closes it
at the wire, which is where the programme's standing rule says acceptance
belongs.

## The legs

Each must produce an **observed server-side effect**, not a locally-echoed
success log. Two-client where a second observer is what proves it
(`willeq_test/Testmelee`, `willeq_buddy/Testbuddy`) — see the two-client
precedent in T102's and T134's Reviews.

1. **`/face`** — change a face value; the observing second client must see the
   appearance change. Previously `DECODE_LENGTH_EXACT` discarded the packet.
2. **`/kick` and `/kill`** (GM) — target must actually be kicked/killed. The
   old failure left **no server log at all** (bare `return`), so "no error in
   the log" is not evidence of anything; only the effect counts.
3. **Environmental damage** — take fall or lava damage; the damage message must
   appear client-side. Previously every such packet was rejected by our own
   length guard.
4. **`/who`** — a real server-populated who-list, contrasted against what the
   malformed 154-byte packet produced.
5. **`/camp`** — completes cleanly, and note whether the old malformed
   `OP_Camp`/`OP_Logout` were silently tolerated or rejected. The T145 sweep
   could not determine from eqemu source whether those two opcodes are
   size-checked; settle it here.
6. **Inspect** — inspect another character; the response window populates.
7. **Popup response** — a quest or GM popup button registers server-side.
8. **Boat control** — only if a boat is reachable in the test zone. **If not,
   report it unverified rather than manufacturing a scenario**; it is the least
   important leg here.

## Deliverable

`review/T146/` with a README, per-leg evidence (client logs plus the eqemu zone
server's own log where the server-side effect is only visible there), and an
explicit table: leg / verified-on-the-wire / not-verified / why. **A leg you
could not exercise is a valid outcome — say so.** T133 flagged four guild
commands as "same fix applied, not separately exercised" rather than folding
them into "guild works now"; do the same here.

Force daylight (`/say #set time 11 0`) before any capture.

## Verification

Full protocol bar. Suite baseline is whatever T144+T145 leave it at.

## STOP conditions

- A leg fails — that is the unit working. Report the failure with evidence;
  do not patch production code beyond what the evidence directly forces.
- eqemu unreachable or lock held — report blocked.

## Review

Executed in `/mnt/projects/fable/wt/slot1`, task branch
`task/T146-wire-acceptance`, off `fable` tip `b5970ef5`. eqemu lock held for
the whole live phase and deleted after clean shutdown. Full evidence:
`/mnt/projects/fable/review/T146/` (README + annotated captured bytes + four
live-session client logs + the eqemu zone server's own log).

**No production code was changed.** `git status` on the task branch shows only
`refactor/` edits; the plan's "this should need no production-code changes"
expectation held, and the temporary capture instrumentation was reverted before
commit (`git checkout --` on both touched TUs; the committed tree contains no
capture hook, T144 precedent).

### THE HEADLINE: HALF THE ACCEPTANCE SET CANNOT BE RUN AT ALL

The amendment named four legs — `/face`, `/kick`, `/kill`, `/who` — as "the
ones where a fixed struct meets an existing, reachable call path". **Three of
those four have no call path.** Verified by exhaustive grep over `src/`,
`include/`, `tests/`, `tools/`: each of `SendFaceChange`, `SendGMKick` and
`SendGMKill` appears **only** at its declaration in `include/client/eq.h` and
its definition. Zero callers.

- **The entire `src/client/eq_gm.cpp` family is dead** — all **nine** senders
  (`SendGMZoneRequest`, `SendGMSummon`, `SendGMGoto`, `SendGMFind`,
  `SendGMKick`, `SendGMKill`, `SendGMHideMe`, `SendGMEmoteZone`,
  `SendGMLastName`) have zero callers, and **no GM command is registered in
  either registry**. There is no `/kick` and no `/kill`. T145's
  `GMKick_Struct` 132→129 fix is correct and unreachable.
- **`/face` was a name collision the plan inherited.** The `/face` command
  that exists (`command_processor.cpp:324`, "Face entity or location") is the
  **movement** face and routes to `EqActionHandler::setHeading()`. It has
  nothing to do with `FaceChange_Struct`, `OP_FaceChange` or appearance.
  T145's `FaceChange_Struct` 8→7 fix is likewise correct and unreachable.
  This also means the "T82 proved /face works" claim T145 flagged was
  measuring a *different command entirely* — the corpse and the living
  command merely share a name.
- **`SendControlBoat` and `SendPopupResponse` confirmed at zero callers**, as
  T145 recorded.

All of this is written up in `refactor/deferred.md` under a T146 entry, with
the standing instruction not to write callers merely to make a leg verifiable.
**Reachability pattern #11, #12, #13.**

### Per-leg table

| leg | verified on the wire | not verified | why |
|---|---|---|---|
| `/who` | **YES** | — | 152-byte body sent; server answered `OP_WhoAllResponse` carrying both account names; **name filter honoured server-side (2 players → 1)** |
| inspect — request half | **YES** | — | 8-byte body sent; **client B logged "Testmelee is inspecting you"** |
| inspect — response half | — | **NO** | **no client anywhere sends `OP_InspectAnswer`**; between two WillEQ clients the window can never populate. Half a feature, not a wire bug |
| `/camp` regression | **YES** | — | 0-byte body; `OP_LogoutReply` received, exit 0, server logged the save. **Also verified working with the OLD malformed 2-byte packet** |
| `OP_Death` capture (bonus) | **YES** | — | field order confirmed against the server's own `character_bind` table |
| `/face` (appearance) | — | **NOT EXERCISABLE** | `SendFaceChange`, 0 callers. No command, no UI trigger |
| `/kick` | — | **NOT EXERCISABLE** | `SendGMKick`, 0 callers. No `/kick` command exists |
| `/kill` | — | **NOT EXERCISABLE** | `SendGMKill`, 0 callers. No `/kill` command exists |
| environmental damage | — | **CANCELLED** | per the plan amendment; `OP_EnvDamage` is client→server |
| boat control | — | **NOT EXERCISABLE** | `SendControlBoat`, 0 callers |
| popup response | — | **NOT EXERCISABLE** | `SendPopupResponse`, 0 callers |

**Say it plainly, T133-style: this unit did NOT prove `/face`, `/kick` or
`/kill` works. It proved they cannot be reached.** What it proved works is
`/who`, the inspect request, and `/camp`.

### The negative control — the part that makes this evidence rather than agreement

The whole reason this unit exists is that a local "sent OK" log has certified
three dead feature families. So the fixed senders were not accepted on their
own say-so: client A was **re-run with the pre-T145 senders restored** behind a
temporary `T146_LEGACY_WIRE` env gate (reverted before commit), against the
same server, in the same zone, with client B running the **fixed** binary
alongside as an in-session positive control.

| | run1 (fixed) | run2 (legacy) |
|---|---|---|
| `OP_WhoAllRequest` body | **152** | **154** (opcode duplicated into the body) |
| `OP_WhoAllResponse` received by A | **2** | **0** |
| `OP_InspectRequest` body | **8** | **10** |
| "is inspecting you" seen by B | **1 event** | **0** |
| `OP_Camp` / `OP_Logout` body | 0 / 0 | 2 / 2 |
| `/camp` completed | yes | **yes — still works** |
| B's own `/who`, same session | n/a | **239 bytes, 2 players** |

Legacy A printed `Searching for players...` twice and `Inspecting...` once and
received **nothing**; the eqemu zone log has **no line at all** for either
malformed request. Silent on both sides — exactly the failure shape that
defeated dispatch-layer acceptance. B, on the fixed binary, was answered
normally at the same moment, so the server was demonstrably healthy.

### `/who` — the server-side effect, spelled out

The 239-byte `OP_WhoAllResponse` carries both character names, the guild
`<TestGuild133>`, and **both account names, `willeq_test` and
`willeq_buddy`** (rows 70 and 71 of the server's `account` table). The client
has no other source for account names. And the count went **2 → 1** when
`whom[64]` at offset 0 carried `"Testbuddy"` — so the server read our body at
the offsets we wrote it, which a mere length match would not establish.

### `/camp` — the plan's open question, settled observationally

The plan (leg 5) asked whether the old malformed `OP_Camp`/`OP_Logout` were
silently tolerated or rejected, noting T145 could not tell from eqemu source.
**Tolerated.** The 2-byte legacy packets produced a completed camp, a real
`OP_LogoutReply` from the server, a clean exit 0, and `Save for [Testmelee]`
in the zone log. `/camp` was never broken; T145's tidy-up changed nothing
user-visible, exactly as T145 said.

### `OP_Death` — T147's largest open question, closed

Two 34-byte captures (`review/T146/wire_bytes_annotated.txt` §4).

A **player** death (Testbuddy, via the GM `#kill` chat command — note this
exercises eqemu's server-side GM command channel, **not** `OP_GMKill`, and is
not evidence for the `/kill` leg) decodes at our offsets to:

```
/*012*/ bindzoneid = 189
```

The server's own DB says `character_bind` slot 0 for character 68 is zone
**189**, `zone.short_name` = **tutorialb**, and the zone server then logged
`Client [Testbuddy] zoning to [tutorialb] (189) ... ZoneToBindPoint`. An
authority entirely outside the client, T144's method.

A second capture — an NPC death, `a cave bat` killed by `Guard Hobart` for 58
damage — reads `bindzoneid = 0` (correctly, an NPC has no bind) and
`damage @24 = 0x3a = 58`, matching the client's own decoded damage line.

**Conclusion for T147: `bindzoneid @12, spell_id @16, attack_skill @20,
damage @24` is OUR order and it is CORRECT — not the reversed order T147
feared.** `spell_id` read `0xFFFFFFFF` in both (melee/none).

**Bound stated, not glossed: `is_PC @28` read 0 for BOTH the player death and
the NPC death, so offset 28 is NOT confirmed by this capture.** Everything
through offset 27 is.

### Verification bar

| step | result |
|---|---|
| 1. `cmake --build build -j24` | **PASS**. (One transient `Error 2` at `-j24` on the first post-revert invocation; re-ran at `-j8` clean, then `-j24` clean — resource contention, not a code error, and flagged here rather than absorbed) |
| 2. `ctest --output-on-failure` | **2456/2457**, sole failure the documented ALSA `AudioManagerTest.PreloadCommonSounds` |
| 3. `scripts/build-gles2-x86.sh` | **PASS** (exit 0) |
| 4. Visual gate | **PASS**, `=== VISUAL GATE: PASS ===`; `git status tests/goldens` empty — goldens untouched |
| 5. `scripts/build-arm-noble.sh` | **PASS** (exit 0) |
| 6. `check_file_sizes.sh fable` | **N/A, and the script says so out loud**: `no C++ files to check ... nothing was checked, this is not a pass`. Correct — this unit changes no C++ file. Recorded rather than claimed as a pass |

### Method

Two concurrent `build-gles2/bin/willeq` processes, one per account, each
`-c <config>.json --headless -d 4 --log-level=DEBUG --script <file>`, launched
from a single foreground driver that `wait`s on both (no background parking).
Client A exits 0 on its own via the `/camp` → `OP_LogoutReply` path; client B
idles after its script and is ended by `timeout`, per T84's documented
never-exits-in-headless behaviour. Configs and scripts live under
`data/captures/` (gitignored, T84/T102/T135 precedent). Daylight forced first
in both runs and confirmed in the server log.

### Deviations

1. **A negative control was built, which the plan did not ask for.** Client A
   was re-run with the pre-T145 senders restored behind a temporary
   `T146_LEGACY_WIRE` env gate. Justification: the unit's entire thesis is
   that a passing "after" is not evidence when the "before" failed silently;
   without the control, `/who` working would have been one more
   self-consistent claim. The gate and the capture hooks were reverted before
   commit — the committed tree has neither.
2. **`/face`, `/kick` and `/kill` were reported unexercisable rather than
   exercised.** This contradicts the amendment's premise that these four legs
   "meet an existing, reachable call path". The amendment was right about
   `/who`; it was wrong about the other three, and the reason is the same
   reachability pattern it correctly applied to `ControlBoat`/`PopupResponse`
   — it simply had not been re-checked for the GM family or for
   `SendFaceChange`. **No callers were written**, per the plan's explicit
   instruction.
3. **The inspect leg is reported as half-verified.** The plan's leg 6 says
   "the response window populates". It does not, and cannot: no client sends
   `OP_InspectAnswer`. The request half is genuinely wire-verified by a second
   observer; the response half is a missing feature, filed in `deferred.md`.
4. **`refactor/deferred.md` was appended to** — outside the plan's stated
   territory (`tests/`, `refactor/`, `review/T146/`) only in the sense that
   the plan named it as the destination for the unexercisable senders, which
   it did. Flagged for completeness.
5. **One `character_corpses` row (id 24, `Testbuddy`, arena) left behind** by
   the death capture. The table already holds five older corpses from prior
   units; deleting corpse rows is not a sanctioned server write, so it was
   left and documented rather than cleaned up.
6. **`Testbuddy` was killed and bind-zoned to `tutorialb`**, then its
   `character_data` position was written back to arena (77, 156/-1009/51,
   h=77) and confirmed by `SELECT` — a documented, sanctioned position
   restore. `Testmelee` and `Testchar` ended at their pre-session values.

### What this unit did NOT establish

- **Nothing about `/face`, `/kick`, `/kill`, boat control or popup response.**
  Their structs are correct per T145's source reading and layout tests; their
  senders are unreachable. No wire claim is possible for any of them.
- **Nothing about `is_PC` in `Death_Struct`** (offset 28) — both captures read 0.
- **Nothing about the inspect response path**, which no client can trigger.
- The `#kill` used to produce the death capture is eqemu's **chat-channel GM
  command**, not `OP_GMKill`. It is capture plumbing, not `/kill` evidence.
