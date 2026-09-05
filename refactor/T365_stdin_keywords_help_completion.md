# T365 — dead stdin keywords (B090), /help name-vs-category (B092), Tab completes registry 3 (B091)

Class A + B + P, slot2. Three commits. Read the B090/B092/B091 board text
and T363's §Review (the measurement that found them).

## Leg A — B090

`inventory`/`inv` and `group` on piped stdin set `InputAction::ToggleInventory`
/ `ToggleGroup`, and `InputActionBridge::processDiscreteActions` has no arm
for either — they do nothing. Same defect as `skills` (T363), one keyword
over.
1. `inventory`/`inv`: fall through to `m_rawCommands` the way `skills` now
   does. Registry 3's catalog name is `newinv` — the same `new*` naming
   Will ruled against for skills. Apply the same shape: rename `/newinv` to
   `/inventory` in the catalog (remove `/newinv`, no alias), fix every text
   mention, and let `inventory` fall through as a catalog name; `inv` is an
   explicit rewrite to `inventory`. Flag the rename in the Review so Will
   can veto it (it mirrors his B058 ruling; it is not a new call).
2. `group`: find whether ANY consumer for a group-window toggle exists
   (a slash command, a hotkey). If one exists, route the word to it; if
   none, delete the keyword and the dead `InputAction` values (all three:
   ToggleInventory, ToggleSkills, ToggleGroup, once nothing produces them —
   the compiler finds any remaining reader).
3. Tests, pre-fix failing: `inventory`, `inv`, `group` on the console
   handler reach the renderer registry / the consumer (T363's
   `HeadlessCommandRefusalTest.BareSkillsKeywordIsClaimedByTheRendererRegistry`
   is the template).
4. Strike B090.

## Leg B — B092

`/help <arg>` resolves `<arg>` as a category before a command name, so
`/help skills` prints the Skills category and the `/skills` command's own
detail is unreachable. Make it try both: print the command detail first,
then the category (with a one-line "also a category" note), or the
reverse — pick the order that matches how the official client's `/help`
behaves if it has one (it does not have merged categories; so choose
command-first and say so). Update
`MergedHelpTest.T363_HelpSkillsResolvesToTheSkillsCategoryNotTheCommand`
to the new truth (rename it), pre-fix failing. Strike B092.

## Leg C — B091

`Application::commandCompletions` unions registries 1+2; `/help` lists
three. Give registry 3 (`AllRendererCommands()` / the static catalog) a
completion source through T362's shared helper
(`include/client/command_completion.h`) and union it in, so `/ski<Tab>`
completes to `/skills`. Test, pre-fix failing: a renderer catalog name
completes. Then correct the three claims (T357 Review, the two CLAUDE.md
sentences) to the new truth: Tab and /help share all three registries.
Strike B091.

## Proof

Build, `ctest -j6`, the new tests with pre-fix/post-fix runs, file sizes;
renderer catalog/graphics files change → visual gate standalone + ARM
(slot2's trees are at willeq.db v6 from T363).

STOP: `/newinv` has semantics beyond toggling the inventory that make the
rename non-mechanical (then leave the name, route `inventory`→`newinv`,
board the naming as class C for Will).
