# B1048 evidence

The implementation is under `scripts/eqemu/oracle/`; the raw captures live
under `/home/user/kb-evidence/kb-b1048-oracle-driver/`, never the stale NFS
review copy on this host. These runs used the official Titanium client.

## Fix-leg verification

The review-fix runs below all exited 0; each `driver.log:61` says
`cleanup exit=0` and `lock=free`. Their DB, inventory, account, and login-account
before/after TSVs compare byte-identically, and each `ledger_check.txt:1`
reports `BROKEN REFERENCES: 0`.

| Plan | Evidence |
| --- | --- |
| Hotbutton placement, lane 3 | `hotbutton-fix4/summary.md:7-16,25`; the frame stepper records the real click as video coordinate `26,585`, not `640,400` (`hotbutton-fix4/step_frames.tsv:2-3`); `video.calibration.tsv:2` records marker-calibrated t0. |
| Untouched Human-male Warrior, lane 1 | `charcreate-fix4/summary.md:7-9`; `02_untouched_created.opcodes.tsv:5` is the client-to-server `OP_CharacterCreate`, decoded class/race/gender = 1/1/0 (Warrior/Human/Male) by `oracle_create.validate_create`. |
| Untouched Barbarian-male Warrior, lane 4 | **Pending a green live rerun.** The original `barbarian-fix4` capture decoded class=16 (Berserker) for a requested Warrior and is NOT acceptance evidence. `barbarian-fix2h` verified the requested selection (Barbarian/Warrior/Male all yellow in `02_barbarian_male_untouched-before-submit.png`) and was correctly **rejected at submit** (`expected one OP_CharacterCreate, got 0`, exit 65) -- a wrong or incomplete capture can no longer be blessed. |

Offline regressions under `scripts/eqemu/oracle/tests/` cover existing-name
rejection and held-lock status, the Barbarian coordinate table and assertion
failure still, marker calibration and action-target coordinates, and vendored
provenance. The safety probes fail on the original `02a0b9d1` driver with
`held lock returned 0` and `existing character deleted`. The create-identity
regression (`create_identity_test.py`) accepts a Barbarian/Warrior packet and
rejects a Berserker packet, and the remote selftest now rejects an unselected
(white-text) row in the real `selected_at` (the old negate-mean detector read
0.99 in both selected and unselected states and is what let the wrong captures
through).

| Run | Observation | Source |
| --- | --- | --- |
| `hotbutton-r726g`, lane 2 | The planned placement generated a final `Page3Button1=E12` INI addition. The step summary cites its own packet TSV and INI diff for each action. | `hotbutton-r726g/character-final.ini.diff:7`; `hotbutton-r726g/summary.md:7-16,25` |
| `charcreate-r726f`, lane 3 | The untouched creation sent `OP_CharacterCreate` from client to server. | `charcreate-r726f/02_untouched_created.opcodes.tsv:5`; `charcreate-r726f/summary.md:8` |
| `disconnect-r726g`, lane 3 | Killing the client during the plan's wait returned exit 67 and appended world and zone log lines. | `disconnect-r726g/timeline.tsv:5-6`; `disconnect-r726g/driver.log:61`; `disconnect-r726g/disconnect-logs.txt:1-42` |

Each run's `db`, `inventory`, `accounts`, and `login-accounts` before/after TSV
files compare byte-identically; each `driver.log` ends with `lock=free`
(`hotbutton-r726g/driver.log`, `charcreate-r726f/driver.log`, and
`disconnect-r726g/driver.log:61`). The two completed plans' ledger checks say
`BROKEN REFERENCES: 0` (`hotbutton-r726g/ledger_check.txt:1` and
`charcreate-r726f/ledger_check.txt:1`).

## Not verified / left open

The official client writes the hotbutton INI value after the individual step
captures, including the plan's flush wait. The final INI diff is real, but its
write time cannot be attributed to the placement step by a per-step INI diff
(`hotbutton-r726g/summary.md:12-16,25`). The intentional disconnect is a
partial run and has no completed-plan `ledger_check.txt`; the snapshot and
lock checks above still apply. Earlier failed login/zone probes remain in their
own evidence directories and are not counted as successful runs. Lane 4 is
demonstrated by the fix-leg Barbarian run above. MR-head CI is reported in the
kanban handoff.
