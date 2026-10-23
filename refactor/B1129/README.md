# B1129 (#449): ORACLE-RIG, class-gated gem casts and the per-lane gem pitch

Process card from B1118 (§2 defect 1, §6 item 11). Docs + scripts only. No lane leg
was run.

## 0. Result

- **Plan check.** `scripts/eqemu/oracle/oracle_plan_check.py check` replays a plan's
  `#memspell <spell_id> <slot>` rows (slot 0 = gem 1) and `#level N` rows. For each gem
  cast it reads the spell's class level from `spells_us.txt` and REFUSES the cast when the
  level is 255 or above the character's level. A gem cast is either a `command /cast N`
  or a `click` on a gem of the lane's measured bar. The refusal names the plan line, the
  spell, the class and the classes that can cast it. `oracle_session.sh` runs the check
  while it holds the lock, after the snapshot and any pre-SQL. It uses the login
  character's real DB class, level and memorised gems, and exits 64 before Wine starts.
- **Replay of B1118's evidence.** B1118's 7-gem tour (`review/B1118/plans/oracle_tour.tsv`)
  as a wizard on lane 5: 41, 253 and 93 are refused, and 458, 38, 54 and 656 pass. Those
  are the gems the official client refused in `oracle_leg/`. Its druid plan is refused for
  class 12 and passes for class 6.
- **Gem pitch per lane.** `scripts/eqemu/oracle/gem_pitch.tsv`. On lane 5 (1280x800) the
  pitch is 30 px and gem i (**1-based**) is centred at root (25, 213+30i). Source: B1118
  `oracle_calib/11_gems_ready.png`. Lanes 1-4 are `-` ("measure on first use"). A
  gem-column click on lanes 1-4 is reported as unchecked; the script does not guess its
  gem. `oracle_plan_check.py gem-xy --lane 5 --gem N` prints a click target.
- **Class legs.** `scripts/eqemu/oracle/oracle_class_leg.sh <lane> <class> <plan> <out>
  [--char-id 67] [--home-class 12] [--gems 93,38,54,41,253,458,656] [--post-sql-file F]`
  generalises B1118's `class_run.sh` and B1130's `leg.sh`. It does five things:
  1. Checks the plan offline.
  2. Waits for the lease without breaking it.
  3. Switches the class, only from the home class. This happens inside the driver's
     locked session. `class_run.sh` ran its SQL outside the lock, and this helper does
     not repeat that.
  4. After the world-down snapshot restore, puts back the home class, the `--gems` list
     and no buffs. A `--post-sql-file` can add more, for example B1130's
     `skills_restore_t2.sql`. The driver then deletes any spellbook rows the leg added.
  5. Exits 66 when the after-census differs from the home class plus exactly `--gems`.
- **Driver hooks.** `oracle_session.sh` now takes the hooks B1130 used, made generic and
  run only inside its lock: `ORACLE_STATE_CHAR`, `ORACLE_PRE_SQL`, `ORACLE_EXPECT_CLASS`
  and `ORACLE_POST_SQL`. A plan that sets none of them behaves as before, except for the
  gem check.
- **Driver defect found and fixed: a `set -u` abort exited 0.** The new safety case
  stopped on an unbound `EQEMU_ORACLE_HOST`. The `EXIT` trap then ran `cleanup`, which
  exits `$STATUS`, and `STATUS` was still 0. `cleanup` now maps a non-zero `$?` with
  `STATUS=0` to 65. The case failed before this fix and passes after it
  (`session_safety.sh`, last case).

## 1. Card vs evidence (two corrections)

- **The class-level columns are 16, not 15.** Fields 104..119 hold classes 1..16, Berserker
  included. This is `EQ::SpellField::ClassLevel1 = 104` with `NUM_CLASSES = 16`
  (`tools/willeq_db/db_loaders_text.cpp`, `include/client/spell/spell_constants.h`), and
  B1130's `plan_gen.py` maps BER to 16.
- **The gem index is 1-based.** B1118 says "gem *i* (1-based) is centred at root
  (25, 213+30i)". The card says 0-based. B1118's plans click (25, 243) for `#memspell <id> 0`,
  and the DB has slot 0 = 93, so gem 1 sits at 243. The table and the checker use 1-based
  numbering.

## 2. B1107 reconciled (not settled)

B1107 (lane 1) used the INI-derived clicks (21, 209+30i), with gem i 0-based. It reported
41, 54 and 656 as confirmed and 93, 38, 253 and 458 as missed. B1118 says the misses were
class refusals. That does not fully fit B1107's table:

- 41 is ENC-only, so a wizard's click on it is refused, yet B1107 lists 41 as confirmed.
- 38 and 458 are wizard spells, yet B1107 lists them as missed.

One reading fits both reports. Suppose lane 1's bar matches lane 5's (34 px below B1107's
coordinates). Then each B1107 click hit the gem one ABOVE the intended one:

- The click meant for gem 0 hit nothing.
- The clicks meant for 38, 253 and 458 hit 93, 41 and 253, which a wizard cannot cast.
- The clicks meant for 54, 41 and 656 cast 38, 54 and 458.

That gives exactly B1107's 4 misses. It is a hypothesis. B1107's pcap
(`oracle_gemcast/`) is not on this host, so the spell ids in its `OP_CastSpell` packets
were not checked, and lane 1's bar is unmeasured. Measuring lane 1 on first use
(`gem_pitch.tsv`) settles it.

## 3. Strikes

- `refactor/B1107/README.md`: these are struck, each with a pointer here:
  - the ~35 px "measured" pitch in §4;
  - "click-calibration misses, not client defects" in the summary and in acceptance A;
  - gap G1, whose text is kept under a SUPERSEDED banner;
  - follow-up 3.
- `refactor/B1118/README.md`: §6 item 11 is marked done, and the "why B1107 got 3/7"
  sentence points to §2 above.
- `docs/oracle_driver.md`: the new section "Gem casts and class legs (B1129)", and exit
  code 64 now covers a refused gem cast. This is the oracle driver's README.
  `scripts/eqemu/oracle/` has no README of its own, and none was added.

## 4. Tests (hygiene label, device-free)

The hygiene count in `docs/module_map.md` is now 48, as `DocCounts` measures it.


- `OracleGemCastPlanCheck` (`scripts/eqemu/oracle/tests/oracle_plan_check_test.py`). It
  covers:
  - a druid-only spell on a wizard, refused with the spell and class named;
  - the same spell castable by a druid;
  - a castable wizard spell;
  - level refusal, and a `#level 60` row lifting it;
  - `/cast N` with `--memmed`;
  - unmeasured-lane clicks, noted and not guessed;
  - the lane table itself.

  Its fixture rows are cross-checked against the real `spells_us.txt` when the EQ files
  are present.
- `OracleClassLegSafety` (`tests/class_leg_safety.sh`). The helper uses a stub driver. It
  checks the offline refusal, the exact pre- and post-SQL, and that an unrestored census
  exits 66.
- `OracleSessionSafety` (`tests/session_safety.sh`, extended). The real driver runs over
  stubs. It checks a refused gem cast (exit 64, never reaches ssh) and a castable one. It
  checks that the pre-SQL runs and that the post-SQL runs after the world stop. It checks
  the spellbook delete and that a `set -u` abort is never exit 0.

## 5. Not done

No live leg was run, and lanes 1-4 stay unmeasured. The other `oracle_remote.sh` plan
verbs are unchanged.
