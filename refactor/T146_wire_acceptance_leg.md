# T146 — Live wire acceptance for T145's nine fixes

**Slot: whichever is free. x86. eqemu lock REQUIRED. No Pi.**

**DISPATCH AFTER T144 RELEASES THE EQEMU LOCK AND T145 IS MERGED.**

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
