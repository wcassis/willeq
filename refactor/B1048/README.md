# B1048 evidence

The implementation is under `scripts/eqemu/oracle/`; the raw captures live
under `/home/user/kb-evidence/kb-b1048-oracle-driver/`, never the stale NFS
review copy on this host. These runs used the official Titanium client.

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
own evidence directories and are not counted as successful runs. Lane 4 was
not demonstrated. MR-head CI is reported in the kanban handoff.
