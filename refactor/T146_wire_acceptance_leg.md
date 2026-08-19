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

(executor fills)
