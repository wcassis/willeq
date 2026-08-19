# T148 — GM status gate, then the nine GM commands

**Slot: slot1. x86. eqemu lock REQUIRED (the gate needs a capture). No Pi.**

**Territory: `src/client/eq.cpp` (`ZoneProcessPlayerProfile` only),
`include/client/eq.h`, `src/client/eq_gm.cpp`, `src/client/action/`,
`include/client/action/`, `src/client/eq_action_handler.{h,cpp}`, `tests/`,
`refactor/`, `/mnt/projects/fable/review/T148/`.**

**T149 (slot2) and T150 (slot3) run in parallel.** T149 owns
`src/client/eq_merchant.cpp`; T150 owns `src/client/eq_player_info.cpp` and
`src/client/eq_group.cpp`. All three of you edit `src/client/action/*` and
`eq_action_handler.*` — **append your registrations, do not reorder or reflow
existing ones**, and keep `tests/CMakeLists.txt` changes to a single appended
block. Those files are shared by design; the merge stays trivial only if nobody
tidies.

Read T132's Review (the net-new action-stack recipe and its ~50-minute measured
cost) and T133's (the `PlayerProfile` parse-site precedent this unit copies).

## Part A — the gate. Do this first; the commands are worthless without it.

**Will's decision, 2026-08-19: gate the GM commands behind a status check
performed ONCE AT LOGIN. A GM who changes state mid-session must log out and
back in to see it reflected. This staleness is accepted and is not a defect —
do not add a mid-session refresh path.** `AT_GM` live-update was explicitly
considered and declined; **do not implement it**, and do not add an `AT_GM`
case to `ZoneProcessSpawnAppearance`.

### What the field actually is — read this before writing the gate

Account status (the `200` on `willeq_test`) **never reaches the client**. There
is no account-status field in the Titanium protocol. What exists is
`PlayerProfile.gm` at **offset 13054** (eqemu
`common/patches/titanium_structs.h:894`), 18 bytes past the `guild_id` at 13036
that T133 already parses in `ZoneProcessPlayerProfile`.

**Its only writer in the entire server is `Client::SetGM(bool)`
(`zone/client.cpp:2661`)** — the `#gm on/off` toggle, which calls `Save()` so
the value persists in the character profile. So the flag means **"GM mode was ON
at login"**, NOT "this account has GM privileges". Name the client-side member
and any user-facing string accordingly — `m_gm_mode` reads truer than
`m_is_gm`. A GM who was `#gm off` at login will not see the commands until they
toggle and relog; that is the accepted consequence of the chosen design and
should be stated in a header comment so the next reader does not "fix" it.

### Capture before you trust it

eqemu's own header comments this field `// 0=no, 1=yes (guessing!)`. **That is
their uncertainty, and this arc has been burned three times by reasoning from
source instead of bytes.** Verify by capture, T144's method:

1. Log in with GM mode ON, dump the `PlayerProfile` bytes at the parse site,
   read offset 13054.
2. Log in with GM mode OFF, same dump, same offset.
3. **Cross-check the neighbours** `pvp@13052` and `anon@13053`, whose values you
   know independently — that confirms the whole region is aligned, not merely
   that one byte reads plausibly.

Annotated dump into `review/T148/`. **If offset 13054 does not track the toggle,
STOP** — the gate has no field to stand on and the rest of this unit is void.
Report; do not hunt for a substitute field on inference.

### The gate itself

Parse it in `ZoneProcessPlayerProfile` alongside T133's `guild_id`, store it,
expose a `GetGMMode()`-shaped accessor. Every GM command registration checks it
and returns a clear refusal when false. **The refusal must be distinguishable
from "no such command"** — a player typing `/kick` should learn the command
exists and is unavailable to them, not that it does not exist.

## Part B — the nine commands

All nine senders in `src/client/eq_gm.cpp` currently have **zero production
callers** and no GM command is registered in either registry (verified by sweep,
`refactor/deferred.md` T146 section). Signatures, `include/client/eq.h:1104-1112`:

| sender | signature |
|---|---|
| `SendGMZoneRequest` | `(const std::string& charname, uint16_t zone_id)` |
| `SendGMSummon` | `(const std::string& charname)` |
| `SendGMGoto` | `(const std::string& charname)` |
| `SendGMFind` | `(const std::string& charname)` |
| `SendGMKick` | `(const std::string& charname)` |
| `SendGMKill` | `(const std::string& charname)` |
| `SendGMHideMe` | `(bool hide)` |
| `SendGMEmoteZone` | `(const std::string& text)` |
| `SendGMLastName` | `(const std::string& charname, const std::string& lastname)` |

Full action stack per the T132 recipe: `CommandProcessor` registration →
`ActionDispatcher` method → `EqActionHandler` → existing sender. **Register in
`action::CommandProcessor` only** — do not add to the legacy `game` registry;
T97's shadowing bug is why. Atomicity is not a concern here since none of these
exist in either registry today, but say so in the Review rather than leaving it
unaddressed.

`SendGMHideMe` already calls `SendSpawnAppearance(AT_GM, ...)`
(`eq_gm.cpp:126`) — that is the *sending* side and is unrelated to the declined
receive-side live-update. Leave it as it is.

**T145 fixed `GMKick_Struct` 132 → 129, correct-and-unreachable until now.**
This unit is the first that can put it on the wire, so `/kick` and `/kill` are
also the first real wire test of that fix.

## Acceptance — AT THE WIRE

**`Handle_OP_GMKick` bare-returns on a size mismatch with NO log line at all**
(`zone/client_packet.cpp:6729`), so "no error in the server log" proves nothing.
Only observed effects count. Two-client, per T146's method — and T146's negative
control is the standard now: where a command was previously unreachable, show
the observed effect, not just an absence of complaint.

Verify as many of the nine as the server lets you. **A command you could not
exercise is a valid outcome — list it as unexercised rather than folding it into
"the GM family works."** T133's precedent: four guild commands were flagged as
"same fix applied, not separately exercised" rather than claimed.

`SendGMKill` on a live character is destructive; use your own test characters
and restore per the protocol (Testchar → zone_id=36, x=-77.9232, y=21.9086, z=3,
h=190).

## Verification

Full protocol bar. Suite baseline 2457 (tip `359f3647`), sole expected failure
the documented ALSA `AudioManagerTest.PreloadCommonSounds`.

## STOP conditions

- Offset 13054 does not track the GM toggle (see Part A).
- The gate needs state that does not exist at login — report, do not invent a
  mid-session path Will declined.
- A command needs server-side privileges the test account lacks — report which.

## Review

(executor fills)
