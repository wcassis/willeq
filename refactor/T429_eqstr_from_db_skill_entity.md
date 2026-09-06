# T429 — the display composers read eqstr from willeq.db (B390); the skill and entity halves of B298

Class B ×2, slot3, no server. Read B390 (both composers hardcode format
strings the client ships in `willeq.db`'s `eqstrings` table, reachable via
`EQT::StringDatabase::getString()`), B298's remaining halves (`SkillData`
and `EntityManager` fields parsed and never shown), T421's and T425's
§Reviews (the composers, `spell_display_text.cpp`, `item_display_*`),
`/mnt/projects/EverQuestP1999/eqstr_us.txt` for the skill and entity
(consider/target) vocabulary, and the skills window (T363) / target
panel.

## Leg A — B390

Both composers fetch each format string by id through `getString()`
(with the id in the code and a one-line comment naming the string, so
the intent stays readable) and substitute `%1..%n` through one shared
formatter (find the existing eqstr substitution in `chat_component.cpp`
/ `eq_strings.cpp` — reuse, never a second). A test pins that every id
the composers use exists in the shipped db (pre-fix: the strings were
literals — the test fails to find the ids referenced). Strike B390.

## Leg B — B298 skill + entity halves

Table: each `SkillData` / entity field the census names → the eqstr line
that shows it (skill: the `/skills` window rows — T363 draws name +
value; what else does the official window show? `EQUI_SkillsWindow.xml`
quote) and for entity fields the consider/target text (`/con` lines are
eqstr; the target panel's HP/level/class). Draw what the oracle shows,
in its place, through Leg A's fetch; fields with no line stay hidden
(say which). Tests pre-fix failing. Strike the halves; B298 fully closed
if nothing remains, else annotate.

## Proof

Build (background), `ctest -j6` (no expected failures), pre-fix/post-fix,
G05 in place if a pass changes, visual gate standalone + ARM, file sizes;
`ui_panel_capture` for the touched panels into `review/T429/`.

STOP: none expected.
