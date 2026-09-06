# T414 — inert config keys (B297); CombatManager dead tuning (B299); the census queue (B290)

Class B, slot3, no server. Read the three board entries and T407's
§Review Leg B (`scripts/refactor/check_write_only_fields.py`, the
exemptions), and T399's data-path census (what each config file feeds).

## Leg A — B297

22 JSON config keys are parsed into fields nothing reads: a user editing
them sees no effect. For each key: name the file, the field, and what it
promises (the comment/README); either wire it to the consumer the name
implies (only if that consumer exists and the wiring is mechanical) or
delete the key from the shipped JSON, the parser and the field, and
strike it from README/CLAUDE.md (DocClaims/DocCounts green). A key whose
consumer would be a feature → board it (B330+) and delete the inert
parse. Tests pre-fix failing: for each wired key, the value reaches the
consumer; for the deleted ones, the source-reader census reports zero
inert keys.

## Leg B — B299

CombatManager's dead tuning fields (T407 blocked on a test edit): read
the fields, the test that pins them, and what the official client's
combat timing does (the server enforces swing timers; the client only
animates) — delete the fields and fix the test, or wire them with a
measured reason. Strike B299.

## Leg C — B290

The census queue: re-run the script on your tip; every remaining
write-only row not exempted (wire/file structs, B296's payloads owned by
T413, B298's record fields) is classified here: delete (compiler proof),
board, or exempt with reason in the script's exemption table. Leave the
script green as a hygiene test (`WriteOnlyFields`, label `hygiene`,
planted fault shown) so a new write-only field fails ctest. Strike B290.

## Proof

Build (background + exit file), `ctest -j6` (no expected failures on
this tip), pre-fix/post-fix, file sizes; if a render path changes,
visual gate standalone + ARM (a moved image golden is a STOP).

STOP: none expected.
