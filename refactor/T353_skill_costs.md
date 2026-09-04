# T353 — `endurance_cost` / `mana_cost` are 0 for every skill (B007): delete or load, by what the server enforces

Class B, slot3 after T350 (or slot1 after T352). No eqemu needed for the
code; a read of the server source decides.

1. Measure what eqemu enforces for skill use: in
   `/mnt/projects/akk-stack/code/zone/` grep the handlers for the skills
   fable's `SkillManager` gates (`Handle_OP_Taunt`, `Handle_OP_Kick`/
   `Bash`/`Backstab` via `OP_CombatAbility`, `Handle_OP_Hide`, `Sneak`,
   `Forage`, `Mend`, `BindWound`, `FeignDeath`, `Track`, `SenseHeading`,
   `ApplyPoison`, `Disarm`…): does the server check endurance/mana and
   refuse with a message, or does it charge and proceed? Cite each as an
   absolute path. Then the oracle: does the OFFICIAL client refuse
   locally before sending (T328/T342's rig can answer for one skill:
   with 0 endurance, does `/kick` produce an outbound `OP_CombatAbility`
   at all?) — if a session is cheap, do it; otherwise reason from the
   server source and say so.
2. Rule (the T324/T327 precedent: the server is authoritative and reports
   every outcome): if the server enforces, DELETE `endurance_cost`,
   `mana_cost` and both refusal branches in `SkillManager::getSkillUseError()`
   and `:859` (§5 shape, K12 ff check); the server's refusal message
   already reaches the chat. If the server does NOT enforce for some
   skill and the official client does, load the values from wherever
   the client gets them (there is no client-side skill-cost table in
   Titanium's files that we know of — measure; do not invent).
3. Tests: `test_skill_*` with assertions untouched except the ones that
   asserted the unreachable refusal (delete those with the branch, cite
   which).

Territory: `include/client/skill/skill_data.h`, `src/client/skill/skill_manager.cpp`,
tests, `refactor/BOARD.md`. No graphics files.

STOP: a skill the server does not enforce AND no data source exists —
board it as a class C (invent nothing).
