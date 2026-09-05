# T402 — tracked caches (B248), dead shipped configs (B245), the ignored argument (B246)

Class B ×3, slot2, no server. Read the three board entries and T399's
§Review.

## Leg A — B248

`config/snd_index_cache.json` and `gfx_index_cache.json` are git-tracked
and rewritten by every client run, so any executor who runs the client
dirties the tree. Decide by reading how they are used: they are CACHES
(a miss means rescan — T399 kept that quiet on purpose). Move the WRITE
side to a build/run-local location (the data-root rule: `$WILLEQ_DATA_ROOT`
or the executable's dir — read `ResolveDataPath`'s write-side handling
T399 added for the caches) and stop tracking the files (`git rm --cached`,
`.gitignore`), OR keep them tracked as shipped seeds and make the client
write elsewhere. Say which and why; a fresh checkout must still start
fast (measure the cold rescan cost once and record it). Test: running
the headless client leaves `git status` clean (a source-reader/ctest pin
that the write path is not under `config/`). Strike B248.

## Leg B — B245

`config/chat_settings.json` and `config/ui_settings.json` are shipped and
read by nothing (six-directory census). Read what they CONTAIN: if a
setting in them names behaviour the client has (chat channel colours,
window positions), the file is the unreached feature, not dead data —
board that as its own item with the fields listed; delete the file only
if nothing in it corresponds to anything. Strike or annotate B245.

## Leg C — B246

`loadRaceMappings(jsonPath)` ignores its argument (race data comes from
SQLite) yet S03 refuses to start without `config/race_models.json` and
`renderer_init` LOG_FATALs on a return that only means "no database".
Remove the parameter and the file requirement; the fatal names the real
condition; delete `config/race_models.json` if nothing else reads it
(census) — or if it does carry data the db lacks, say so and STOP that
leg. Tests: startup without the file succeeds; startup without the db
fails with the db message. Strike B246.

## Proof

Build, `ctest -j6` (no expected failures beyond B240's gate delta),
pre-fix/post-fix, file sizes; no graphics change expected — if the
render init path changes, visual gate standalone.

STOP: race_models.json carries data the db does not.
