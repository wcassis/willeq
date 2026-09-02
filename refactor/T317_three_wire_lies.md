# T317 — Three things the client asserts that are not true: guild rank, skill gates, and caps lock

**Slot: slot3. Branch `task/T317` cut from `fable`. x86 only. No Pi.
eqemu is held by slot1 — headless acceptance; live leg only if the lock frees,
and label live vs headless evidence.**

Closes **tier 2 items 6, 7 and 18** of `refactor/deferred.md`'s class-A list.
The first two are one defect class — **the client tells the server something it
has not checked**. The third is unrelated and tiny; it rides along because it is
four lines and would otherwise never be worth a dispatch.

## 1. Guild rank is HARDCODED on the wire (item 6)

`src/client/eq_guild.cpp:54` — `invite.officer = 2;  // TODO: Use actual rank`
(reached from `/guildinvite`), and `:105` — `remove.leaderstatus = 2;`
(from `/guildremove`).

*Impact:* both commands claim officer/leader status the player may not have.
The server's answer is whatever it makes of the lie.

**Find where the player's real guild rank lives** before writing anything —
`GuildState` / the player profile / the guild-member roster are the candidates.
If the client genuinely does not know its own rank, that is the finding: say so,
say what packet would carry it, and **send the honest unknown rather than a
confident 2**. Do not invent a rank.

⚠ Read eqemu's handler for both opcodes and say what it does with these fields
— if the server ignores them entirely, the fix is still right but the impact
line on the board is wrong and should be corrected (T314 set that precedent).

## 2. Skill combat and position gates are unenforced (item 7)

Four sites in `src/client/skill/skill_manager.cpp`:

| site | current state |
|---|---|
| `:272-274` | `if (skill.requires_combat) { /* TODO: Check if in combat */ }` — empty body |
| `:283-285` | `if (skill.requires_behind) { /* TODO */ }` — empty body |
| `:734-740` | `checkCombatRequirement()` substitutes **"has a target"** for "in combat" |
| `:758-761` | `checkPositionRequirement()` unconditionally `return true` |

*Impact:* Backstab's behind-the-target requirement is never checked client-side,
so the client sends attempts the server refuses.

**The comment at `:276-283` is load-bearing — read it before you touch this.**
T159 removed a refusal message that *lied* (it blamed facing for what was
actually an endurance failure), and left a written instruction that **the check
and the message go back in together.** Honour that: if you implement the facing
check, restore the message; if you cannot, leave both out and say why. Do not
restore a message without the check.

For the combat gate: "has a target" is not "in combat". `CombatManager` is right
there — find whether it exposes a real in-combat state and use it; if it only
exposes a target, that is the finding.

For facing: it is a heading comparison between the player and the target.
⚠ **`docs/CLAUDE.md`'s Heading/Rotation Conventions warning applies** — players
and NPCs do not share default rotations, and that asymmetry is load-bearing and
must NOT be harmonised (see the memory `heading-90-offset-load-bearing`). Derive
the behind-test from the server's own definition if eqemu has one, and say which
convention you used.

## 3. RDP remote input ignores caps lock (item 18)

`src/client/graphics/renderer_rdp.cpp:159` —
`bool capsLock = false;  // TODO: track caps lock state`, feeding
`rdpScancodeToChar(scancode, shift, capsLock)`.

*Impact:* typing over RDP produces the wrong case whenever caps lock is on.

Track the state from the RDP key stream (the sibling `shift` two lines above is
the template). Note caps lock is a **toggle**, not a modifier — a key-down
flips it; it is not held. FreeRDP also carries the client's lock state in its
sync/`KBD_SYNC` message; prefer that if it is reachable.

## Acceptance

- Guild: a test asserting the sent packet carries the player's real rank, for at
  least two different ranks — or, if the client cannot know it, a test pinning
  the honest-unknown value with the reason in the test name.
- Skills: a test per gate, each asserting the **negative** (the gate refuses when
  it should) — a test that only proves the allowed case passes on today's code.
  Include the T159 case: a Backstab failing for endurance must not blame facing.
- RDP: a table test over caps-lock on/off × shift on/off (four cases; caps+shift
  is the one that catches a naive OR).
- Full `ctest -j6` (**NOT -j24**), zero new failures vs `refactor/test_baseline.txt`;
  visual gate PASS, goldens untouched; `check_file_sizes.sh --files` no new FAILs.

⚠ **If the facing check turns out to need real work, STOP that piece, deliver
the other two, and report** — do not let it swallow the unit.

## Territory

`src/client/eq_guild.cpp`, `src/client/skill/skill_manager.cpp`,
`src/client/graphics/renderer_rdp.cpp`, plus your tests.
⚠ Concurrent: slot1 T311 Leg B (`eq_movement.cpp`, `eq_bridge.cpp`,
`eq_entities.cpp`, `spell_cast_checks.cpp`), slot2 T316
(`eq_action_handler.cpp`, `action_dispatcher.cpp`, `spell/spell_database.cpp`,
`willeq_database`). **`src/client/spell/` is slot2's — `skill/` is yours; do not
stray between them.**
