# T388 — documentation truth sweep 2 (T371–T387) + the FX API decision (B161)

Class P + B, slot1, no server. T371 swept the docs after T352–T370 and
built `DocClaims`; T373 built `DocCounts`. Fourteen units since changed
things the two nets do not see: every client send goes through
`PacketSender` (T376/T378/T379 — `QueueLogin`/`QueueWorld`,
`SafeQueueZone`, the census pin), the pending-click set lives on
`DoorStateManager` (T376), `/help` command-then-category and Tab over
three registries (T365), `/inventory` (T365), window keys `Alt+P` /
`Alt+F1` / `Alt+B` (T382), corpse coin banked (T382), spell visuals keyed
on `SpellVisualVocabulary` with `m_spell_db` gone (T381), willeq.db v7
with `target_anim`/`travel_type` (T384), the ceiling covering `tools/`
and `benchmarks/` with `model_viewer` split into `tools/model_viewer/`
(T385), vision floors per channel (T383), the light-culling rule (T367),
`AudioMixer` atomics (T370).

## Leg A — sweep

1. Same method as T371: for each orientation doc (root `CLAUDE.md`, every
   `*/CLAUDE.md` under `include/`, `src/`, `tools/`, `README.md`,
   `docs/module_map.md`, the orientation parts of `docs/decisions.md`),
   read it against the tree and table every sentence now FALSE or naming
   a thing that no longer exists (`m_spell_db`, `setSpellDatabase`,
   `m_pending_door_clicks`, `ClearPendingDoorClicks`, `zone_connection->QueuePacket`
   as a pattern, the old key bindings, "12 direct sends", "tools/ is not
   checked", `kWilleqDbExpectedVersion 6`, "35x darker", "data-only"
   lights, …). Grep is the start; reading is the proof.
2. Fix in place, citing the unit. Add the retired names to `DocClaims`'
   table and any new count to `DocCounts` (T373 shape: planted fault
   shown).
3. Review: the per-file table.

## Leg B — B161

The seven "dead" `SpellVisualFX` methods: T381 corrected the claim —
`tools/model_viewer` calls six of them (`createProjectile` is the one
with no caller anywhere). After T385's split, re-census with the B166
rule (name the directories). Decision by the board's own rule: a method
whose only caller is a developer tool is not production-reached; but
deleting it deletes the tool's feature. Keep the six (the tool is the
manual FX harness — say so in a comment at the class), and for
`createProjectile`: B165 says the travel gate guards it and 6,250 spells
populate that gate; do NOT delete it — annotate B161 as "six kept for
the harness, one awaits B165/B162" and strike nothing unless the census
contradicts T381.

## Proof

`ctest -j6` (the three hygiene tests + any new pin), file sizes.

STOP: none expected.

## Review

Executed 2026-09-05 in `/mnt/projects/fable/wt/slot1`, branch
`task/T388-doc-truth-sweep-2`, base `fable` tip **`65cbed39`** (verified at
checkout). No server (the eqemu lock is held by T386 and was not touched), no
graphics change, no golden touched, no gate.

### Method

Same as T371: each orientation doc read against the tree, every sentence
checked by grep + a look at the thing it names. The measured truths came from
the `## Review` sections of T376-T385 plus T367/T370/T375/T377.

**Deviation 1: T386 and T387 have no `## Review` at this tip** — both are in
flight (T386 holds the lock, T387 owns `tools/hwcaps`). The plan said
"T371-T387"; this sweep is against T371-T385 plus the earlier units whose
truths the docs had not learned. T387's subject is nevertheless recorded,
because its two files are why `FileSizeCeiling` is RED here (**B185**).

### Per-file table of false sentences — 21 across 7 files

| # | File | The false sentence (abridged) | The measured truth | Proof |
|---|---|---|---|---|
| 1 | `CLAUDE.md` | "Doors tracked in **`m_doors`** map keyed by door_id" | There is no `m_doors` anywhere in the tree — **zero occurrences** under `src/` + `include/`. Doors are `DoorStateManager::doors_`, reached as `GameState::doors()`, and since T376 the same manager owns `pendingClicks_` | `grep -rn '\bm_doors\b' src/client include/client` = 0; `include/client/door_state_manager.h:43-44`; T376 (B141) |
| 2 | same | "every **source file** is under 1,500 lines and ctest enforces it" | `FileSizeCeiling` is **RED at this tip**: `tools/hwcaps/zone_bench_loader.cpp` 3,280 and `tools/hwcaps/gles2_zone_benchmark.cpp` 3,060 | `check_file_sizes.sh --all`: 990 checked, 988 ok, **2 fail**; T385 (B167/B185) |
| 3 | same | "a comment-filtered count **at this tip** reads 165 (T371)" | 174 at this tip | comment-stripped `\bowner_\b` over `src/client/graphics` + `include/client/graphics`, excluding `zone_shader_callbacks.h` |
| 4 | `docs/module_map.md` | "File-size ceilings: **warn >1,500, fail >3,000**" (Conventions) | 1,500 is a FAIL; `--all` scans five directories since T385; the allowlist is empty | `scripts/refactor/check_file_sizes.sh`; M00 + `docs/decisions.md` 2026-09-04 and 2026-09-05. **T371 corrected the header paragraph and left this bullet 300 lines down saying the pre-M00 rule** |
| 5 | same | "**Every file in the tree is under 1,500**" | as #2 | as #2 |
| 6 | same | "The **151** that remain are 81 uses … and 70 in those stays" (stated as current) | as #3. T371 fixed this shape in `CLAUDE.md` and `src/client/graphics/CLAUDE.md` and missed the third site | as #3 |
| 7 | same | "the same **142** `registerCommand({…})` calls" | 143 | `grep -c` = 143; T347 inserted `/cleartarget`. T371/T373 corrected the two bolded sites; this unbolded one was outside `DocCounts`' regex |
| 8 | same | "`include()`s **18** per-domain fragments … `graphics`, `midi`, …" | **19**, and the list omits `hygiene` — the fragment T371/T373 added to hold `FileSizeCeiling`/`DocCitations`/`DocClaims`/`DocCounts` | `ls tests/cmake/*.cmake` = 19. Two errors in one sentence: the count and the enumeration |
| 9 | `docs/decisions.md` | "The residual is bounded by B080: fable's night is **~35x darker** than the oracle's on near geometry" | **REFUTED.** At a lamp-free spot at forced 22:00 fable reads 0.005502 linear grey against the oracle's 0.004245 — fable is **1.30x brighter**. The 35x came from cropping two frames that did not show the same geometry | T367 Review, Leg A table; B080's *lead* (the missing lamp term, now B110) stands |
| 10 | same | vision gains "solved against the oracle" (one scalar) | T383 re-solved them **per channel**: UV gain 0.1601 x (0.695, 0.698, 1.607), IR gain 0.1248 x (0.823, 0.759, 1.419), zone-light IR shift 0.3 -> 0.0508; six residuals inside 0.11%, and the live re-measure is still owed (B171) | T377 §2, T383 §2-3 |
| 11 | `README.md` | "# Run tests (**2746** registered tests)" | The **third** site of the count T371 replaced with its measuring command (T371 fixed the prose, T373 fixed the directory tree, both missed this one, inside the Building code block) | `ctest -N`; now netted by a new `DocCounts` assertion, below |
| 12 | same | "Visual spell effects incomplete (cast glow, projectile, impact, **aura exist**…)" | Only `createCastGlow`/`removeCastGlow`, `createSpellComplete` and `createImpact` are reached from the game. Projectile, aura, rain and ground circle have **no production caller** | `src/client/bridge/irrlicht_bridge.cpp`; T388 census below; B161 |
| 13 | same | "Spell visual effects \| Partial \| Cast glow, **projectile**, impact, **aura, rain, ground circle**" | as #12 | as #12 |
| 14 | same | "Spell effects \| Partial \| Cast glow, **projectile**, impact, **aura** particles" | as #12 | as #12 |
| 15 | same | "First-person \| Partial \| … + **clamped down-pitch (T300); clamp angle inferred**" | The clamp was **REFUTED live and no longer exists**; the rule is that the model + weapon draw for the duration of an attack animation and at no other time. **The same README says so 66 lines earlier**, so the file contradicted itself | T307 (measured against the official client), T309 (`entity_first_person_draw.cpp`); README:49 |
| 16 | same | "Bug / petition \| Partial \| **/bug body does not match the eqemu report format** (T135)" | Fixed and live-proven: `reports` row 3 carries `name=Testchar`, `reported_text='test T377'` | T375 (the byte-parsed `reporter|reported|text` shape), T377 item 4 (B023 PASS) |
| 17 | same | "**EverQuest** (`src/client/eq.cpp` + 18 `eq_*.cpp` domain TUs)" | `src/client/eq*.cpp` **does not exist** — K14 moved it to `src/client/core/everquest.cpp` and Batch K extracted twelve components | `ls src/client/eq*.cpp` -> no such file |
| 18 | same | File-structure tree: "`eq.cpp, eq_*.cpp`" under `src/client/` | as #17 | as #17 |
| 19 | same | File-structure tree: "`graphics/ irrlicht_renderer.cpp` … `renderer_*.cpp`" | Neither path exists — L12 moved the class's four TUs into `src/client/graphics/renderer/` and the rest into eleven component directories | `ls src/client/graphics/irrlicht_renderer.cpp src/client/graphics/renderer_*.cpp` -> no such file |
| 20 | `include/client/graphics/ui/CLAUDE.md` | "Registry 1 — game registry (…, **`src/client/eq_commands*.cpp`**)" | That path does not exist; K12 moved registry 1 to `eqt::commands::CommandsComponent::RegisterCommands()` under `src/client/commands/` | `ls src/client/eq_commands*.cpp` -> no such file |
| 21 | `include/client/bridge/CLAUDE.md` | "direct `CombatManager` calls from **`eq_commands_movement.cpp`**" | `src/client/commands/commands_movement.cpp` (K12) | as #20. Not caught by `DocCitations` because the citation is a bare basename, not a repo-relative path |

Also corrected, not counted as false: `include/client/state/CLAUDE.md`'s
`DoorStateManager` row named no header ("(in game state layer)") and did not
mention the pending-click set it has owned since T376;
`src/client/graphics/CLAUDE.md`'s `SpellVisualFX` line gained the
`SpellVisualVocabulary` route (T381) and `travelType` (T384).

### The three that matter most

1. **#1, `m_doors`.** The root `CLAUDE.md`'s Key Patterns section — the
   section a new agent reads to learn where state lives — pointed at a member
   with **zero occurrences in the tree**. Every other Key-Patterns bullet
   around it (`m_entities`, `m_bridge`, `m_command_registry`) resolves, so
   there was nothing to warn a reader that this one did not. It is now a
   `DocClaims` row, and it is the row that fired first on the planted fault.
2. **#9, "35x darker".** A *refuted measurement* standing in
   `docs/decisions.md` — the file whose banner says "Settled; do not
   re-litigate". T367 disproved it (fable is 1.30x brighter, not 35x darker)
   and boarded the real residual as B110, but never struck the decision entry
   that quoted it. This is the most expensive shape in the sweep: a unit
   scoping night-lighting work off `decisions.md` would have started from a
   number that is wrong by a factor of ~45 and in the wrong direction.
3. **#15, the first-person pitch clamp.** `README.md` describes the hiding
   mechanism T307 refuted, 66 lines below the paragraph that says it was
   refuted. `docs/reference/`-class staleness is expected; a file
   contradicting itself in two adjacent sections is what makes a reader trust
   the wrong half. The memory note `eq-firstperson-draws-player-geometry`
   exists precisely because this was re-derived once already.

### `DocClaims` — five new rows, one corrected

Table 23 -> **28** rows. New: `m_pending_door_clicks`, `ClearPendingDoorClicks`
(T376/B141), `m_doors` (T376/B141), `setSpellDatabase` (T381/B160),
`kWilleqDbExpectedVersion 6` (T384/B163). The existing
`kWilleqDbExpectedVersion 5` row's truth text was corrected 6 -> 7.

**`m_spell_db` was deliberately NOT added.** T381 deleted
`SpellVisualFX::m_spell_db`, but `SpellManager::m_spell_db` is live with 9+
uses in `src/client/spell/`, so a bare row would have been a row for a name
that still exists. Each of the five added rows was confirmed at **zero
occurrences** in `src/ include/ tools/ tests/` first.

Five doc lines are legitimate tombstones and now carry `RETIRED-OK` (2 -> 8
exempted hits): the two door lines this unit wrote in `CLAUDE.md`, the
`setSpellDatabase` mentions in `docs/decisions.md` (T385's argument for
scanning `tools/`), `src/client/graphics/CLAUDE.md` and **`refactor/BOARD.md`
B166's own live entry**, which quotes the claim that was wrong.

**Planted fault** — all five names written into one live line of
`include/client/state/CLAUDE.md`:

```
DocClaims: 13 orientation docs scanned (including refactor/BOARD.md's 21 LIVE entries; struck entries skipped), 28 retired names in the table, 8 exempted line hits (RETIRED-OK)
FAIL  include/client/state/CLAUDE.md:28 names the retired 'kWilleqDbExpectedVersion 6' -- the expected willeq.db version is 7 -- T384 appended target_anim (field 121) and travel_type (field 122) to the spells table, 99 columns -> 101, and every build tree must be rebuilt (T384 (B163)).
FAIL  include/client/state/CLAUDE.md:28 names the retired 'm_pending_door_clicks' -- ...
FAIL  include/client/state/CLAUDE.md:28 names the retired 'ClearPendingDoorClicks' -- ...
FAIL  include/client/state/CLAUDE.md:28 names the retired 'm_doors' -- ...
FAIL  include/client/state/CLAUDE.md:28 names the retired 'setSpellDatabase' -- ...
DocClaims: 5 failure(s).
```

Reverted from a `cp -p` backup (never `git stash`), `cmp` clean, test green.

### `DocCounts` — one new netted count, one new assertion KIND

**New row: `tests/cmake` per-domain fragments**, measured as `*.cmake` under
`tests/cmake/` (**19**), netted at `docs/module_map.md`. Planted fault
19 -> 18:

```
FAIL  docs/module_map.md:316 states 18 tests/cmake per-domain fragments; the tree has 19. (T371/T373 added hygiene.cmake; a new test domain adds another.) Correct the doc -- the measurement is authoritative.
```

**New assertion kind: `FORBIDDEN`.** T373's `NAMED_COMMAND` assertion only
proves the measuring command is *somewhere* in the file. `README.md` carried
`ctest --test-dir build -N | tail -1` in its Tests section while a stale
`(2746 registered tests)` sat in its Building block twelve lines earlier, and
`DocCounts` was green the whole time (false sentence #11). A doc told to name
the command must also not restate the number, so `FORBIDDEN` fails on a
matched pattern rather than on a missing one. Planted fault — the literal
restored:

```
FAIL  README.md:299 restates a count that must not be pinned: '(2746 registered tests)' -- the registered-test total was replaced by its measuring command (T371); this is a second site restating it as a literal (T388). Replace it with the measuring command.
```

Both reverted byte-for-byte (`cmp` clean) and green after. Pass line:
`DocCounts: 8 counts netted over 15 doc sites in 8 docs, 4 not-a-number
assertions, 1 forbidden-restatement assertions`.

### Leg B — the B161 census, and what it contradicts

Directories grepped, per the **B166** rule: `src/`, `include/`, `tests/`,
`tools/`, `benchmarks/`, `docker/`. The last two hold no C++ caller of any of
these. `tools/model_viewer.cpp` is 155 lines since T385; the spell-test
wiring is `tools/model_viewer/model_viewer_spell_test.cpp`.

| method | `src/` (production) | `tools/` | `tests/` | verdict |
|---|---|---|---|---|
| `createProjectile` | — | **2** (`model_viewer_spell_test.cpp:235,277`) | 2 (T381 travel gate) | keep |
| `createImpactAtPosition` | — | 1 (`:245`) | — | keep |
| `createBuffAura` | — | 1 (`:273`) | — | keep |
| `createRainEffect` | — | 1 (`:264`) | — | keep |
| `createGroundCircle` | — | 1 (`:266`) | — | keep |
| `removeBuffAura` | — | **0** | **0** | **zero callers anywhere** |
| `removeAllForEntity` | — | **0** | **0** | **zero callers anywhere** |
| (`createImpact`, for contrast) | 1 (`irrlicht_bridge.cpp:811`) | 5 | — | production-reached |

**Deviation 2 — the census contradicts this plan's premise, not T381's.**
The plan says "`tools/model_viewer` calls six of them (`createProjectile` is
the one with no caller anywhere)". `createProjectile` has **two**
`model_viewer` call sites. B161's own board text was right — its "six" counts
`createImpact`, which is not one of the seven — and the plan's paraphrase
dropped that. So the split is **five kept for the harness** (not six) and
**two genuinely callerless** (not one), and the callerless pair is the
*removal* half, not `createProjectile`.

**Decision, per the plan's rule.** Nothing struck, nothing deleted. B161 is
annotated with the census above and with the corrected framing: the open
question is the PROJECTILE one, which is **B165**, waiting on **B162**'s
oracle capture; T384 has already made `travelType` available and nothing
consumes it. The five kept methods are the manual FX harness's feature and
deleting them deletes it — said out loud in a new comment block at
`SpellVisualFX`'s Effect Creation section, which names the six directories
and who calls what. `removeBuffAura` / `removeAllForEntity` are boarded as
**B195** (B), sequenced behind B165, with the note that they must not be
deleted independently: if the aura path is ever wired, an aura created and
never removed is a leak and these are the fix.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **EXIT=0** (both invocations; no exit 143, so no post-143 sweep was owed) |
| `ctest --test-dir build -j6` | **exit 8, 4,038 of 4,041 passing.** Three reds, none this unit's: `AudioManagerTest.PreloadCommonSounds` (the waived ALSA host quirk), `FileSizeCeiling` (T387's two `tools/hwcaps` files, B185) and `visual_gate` (see below) |
| `DocClaims` / `DocCounts` / `DocCitations` | all PASS (`DocCitations`: 698 citations, 684 ok, 0 fail) |
| `FileSizeCeiling` | **RED — expected, and not this unit's**: `tools/hwcaps/gles2_zone_benchmark.cpp` 3,060 and `tools/hwcaps/zone_bench_loader.cpp` 3,280, both T387's (board B185). Untouched here |
| `check_file_sizes.sh fable` | 1 file checked (`include/client/graphics/spell_visual_fx.h`, 389 lines): **1 ok, 0 warn, 0 fail**. It is the only non-doc, non-script file this branch touches |
| `scripts/build-gles2-x86.sh` | **EXIT=0** (see the `visual_gate` note) |
| `scripts/ci/visual_gate.sh` standalone | **=== VISUAL GATE: PASS ===**, all three zones + the three memory-coverage runs, goldens untouched |
| goldens | `git status --porcelain tests/goldens/` empty — no golden touched |
| ARM (`build-arm-noble.sh`) | not run: no `src/` change beyond a comment block in one header, no shader, no `willeq.db`, no CMake change |

**The `visual_gate` red was a STALE TREE, not a defect — and it is the trap
`refactor/EXECUTOR_PROTOCOL.md` step 3 names.** Inside `ctest -j6` it failed
with `characterModelsBytes mismatch (expected 25737076, got 24701259)` on
qeynos2 and `(expected 25368606, got 24332789)` on gfaydark — **an identical
1,035,817-byte deficit in both zones**, which is the documented T304/T355
signature of a `build-gles2` binary older than `data/willeq.db`. T384 bumped
`kWilleqDbExpectedVersion` 6 -> 7 and this slot's `build-gles2` tree had never
been rebuilt against it. `scripts/build-gles2-x86.sh` (EXIT=0) then
`scripts/ci/visual_gate.sh` standalone: **PASS**, mismatch gone, CPU p95
inside ceiling in all three zones. Nothing in this unit could have moved a
pixel; the gate was run only to classify the red.

### Deviations, and what surprised me

1. **T386/T387 have no `## Review` at this tip** (deviation 1, above): the
   sweep is T371-T385 plus the earlier units the docs had not learned.
2. **The census contradicts this plan's premise, not T381's** (deviation 2,
   above): five kept, two callerless, and the callerless pair is
   `removeBuffAura`/`removeAllForEntity`, not `createProjectile`.
3. **Most of T376-T385's measured truths are not in the orientation docs at
   all.** `PacketSender`, `SafeQueueZone`/`QueueLogin`/`QueueWorld`, the
   direct-sender census, the corpse-coin grant and the vision floors appear in
   `docs/module_map.md`'s `packet_sender.cpp` row and nowhere else. So the
   sweep found *no false sentence* about the eight units that did the most
   work — the docs were silent, not wrong. What was wrong was older and
   quieter: a member that does not exist, a refuted measurement in
   `decisions.md`, a pre-M00 ceiling rule 300 lines below the corrected one,
   and three stale file paths from Batches K and L.
4. **T371's own corrections drifted in a day.** Both `owner_` sites T371
   rewrote to say "at this tip reads 165" now read 174, and the third site
   (`docs/module_map.md`) still said 151. A sentence that pins a number to
   "this tip" ages exactly as badly as the number it replaced; all three now
   carry the unit that measured each reading instead.
5. **A `NAMED_COMMAND` assertion cannot catch a second site.** `README.md`
   satisfied T373's "must contain `ctest -N | tail -1`" while restating 2746
   twelve lines earlier. That is what the new `FORBIDDEN` assertion is for,
   and it is the third README site of that one count (T371 fixed the prose,
   T373 the directory tree, T388 the code block) — the count had been wrong
   in three places at once.
6. **`m_spell_db` is a trap for a `DocClaims` row.** T381's deletion was
   `SpellVisualFX::m_spell_db`; `SpellManager::m_spell_db` is live. A row for
   the bare name would have failed on correct docs.
7. **`DocCitations` does not see a bare basename.** `eq_commands_movement.cpp`
   and `eq_commands*.cpp` are both gone from the tree and both were cited in
   live docs; neither is a repo-relative path, so the citation checker was
   green. `DocClaims` cannot take them either — `docs/module_map.md` names
   them a dozen times as deliberate "(was `eq_*.cpp`)" tombstones. Recorded
   here rather than boarded: the fix would be a dozen `RETIRED-OK` markers on
   correct text.

### New board items

**B195** (B) — `SpellVisualFX::removeBuffAura` and
`SpellVisualFX::removeAllForEntity` have zero callers in `src/`, `include/`,
`tests/`, `tools/`, `benchmarks/` or `docker/`; sequenced behind B165, not to
be deleted independently of it. **B161** is annotated, not struck.
