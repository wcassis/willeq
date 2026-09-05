# T395 — spell impact draws nothing (B204); the eqstr hex blob (B205); race_data deleted (B216)

Class B ×3, slot2, no server. Read the three board entries and T386 §3.

## Leg A — B204

An observing client 15 units away logged both `SpellAction` packets for a
Fire and a Cold cast and its pixels did not move. T381 keyed colours on
the vocabulary; B204 says nothing reaches a frame. Trace the consumer
chain from `SpellCastVisualComplete` in `irrlicht_bridge.cpp` to pixels:
`createSpellComplete` / `createImpact` → which scene node / particle
emitter; is the emitter attached to the ENTITY node of the caster (which
for the observer is a remote entity — does the lookup by spawn id find
it?), is it created with zero lifetime / zero particles, is it culled by
the entity render distance, is the particle manager's render pass
reached on the observer? Instrument nothing permanent: a headless test
that publishes the two events for a REMOTE entity through the bridge and
asserts a node/emitter exists with non-zero particles and a lifetime
(pre-fix failing), and a frame-render assertion that the pass draws it
(G05 frame order golden if a statement changes — learn in place). Fix
the first broken link you can prove; if the chain is intact and the gap
is the atlas (B162/B165 territory), say so with the evidence and annotate
instead of striking. Live proof owed to T389.

## Leg B — B205

`Your spell 0FFFFF000000004F00000000000000000000000000000Burst of Flame
has failed to land…` — a 45-char blob before the spell name. Find the
eqstr (grep `has failed to land` in `eqstr_us.txt`), the assembler that
substitutes `%1`, and what fills the argument: the blob looks like a
link-encoded item/spell name (Titanium's `\x12` link format encodes a
hex header before the name). Read how the official client renders the
message (T386's oracle screenshot of the same refusal if captured;
otherwise T345's) — it shows the bare spell name. Fix the consumer to
strip/decode the link header (there may be an existing link parser for
item links — reuse it). Test, pre-fix failing, over the two measured
strings. Strike B205.

## Leg C — B216

`race_data`: 475 rows, zero consumers in six directories. Delete the
loader, the table from the schema and any builder docs; willeq.db
rebuilt (content-only — no version bump unless the schema check
requires it; read how the loader validates tables and say which). T392's
identity proof over the untouched tables. Strike B216.

## Proof

Build (all trees if the db changes), `ctest -j6` — the suite has NO
expected failures since T392 (a red is real), the tests with
pre-fix/post-fix runs, file sizes; graphics change in Leg A → visual gate
standalone + ARM.

STOP: Leg A's chain is intact end to end (annotate, do not invent a
draw); the blob is not a link header (report the bytes).
