# T404 — sounds at double rate (B250); the stale self entity (B251); memspell into an occupied gem (B254)

Class A ×3, slot3 (now RelWithDebInfo — the first slot at main's build
type), no server. Read the three board entries and T401's §Review §1d,
§1e, §6.4 (`review/T401/`).

## Leg A — B250

Every PFS sound plays at k = 2.00: the assets are 11,025 Hz 16-bit mono;
the mixer/decoder treats them as 22,050 (or the WAV header's sample rate
is read from the wrong field / a default rate is assumed after the PFS
path skips the header parse). Reproduce offline: decode `BuyItem.WAV`
from the archive through the production path and assert the decoded
sample rate and frame count against the WAV header (pre-fix failing:
rate or duration off by 2). Fix at the decoder, not by halving the rate
(find WHY it is doubled — a `bytesPerSample`/`channels` mix-up or a
hard-coded 22050); a loose-file load of the same WAV must still be
correct (the pre-T392 path). Tests over three assets. Strike B250; the
audible re-check (T401's loopback rig: ncc peak at k = 1.00) is OWED.

## Leg B — B251

The local player's own `Entity` row is never updated when the server
relocates him (`#goto`, zone-in warp, death respawn): sounds are emitted
from the spawn pose and self-targeted range checks fail (`distance=1019`
caster == target). Find the ONE place the player's authoritative
position lives (`PlayerState`/movement) and the places that read the
self entity's position instead; either update the self entity row on
every position change the player component publishes (the K-mechanism
substitution: readers of the entity row for `MySpawnId()` read the
player position), or make the self entity row mirror it on
`OP_ClientUpdate`/`OP_MoveTo`-style relocations. Tests, pre-fix failing:
after a forced relocation event, a combat sound's emit position equals
the player position; a self-targeted cast's distance is 0. Strike B251.

## Leg C — B254

`OP_MemorizeSpell` with `scribing=2` (un-memorise) for slot N followed by
`scribing=1` for the same slot leaves the gem bar casting the OLD spell.
Read the handler: the order/guard bug (does `scribing=2` clear the slot
after the `scribing=1` arm ran? is the gem bar keyed by spell id and the
old id not erased?). Test, pre-fix failing: the two packets in that order
→ `/cast 1` sends the NEW id. Strike B254.

## Proof

Build, `ctest -j6` (no expected failures beyond B240's gate delta and,
until T403 merges, the atlas determinism test), pre-fix/post-fix per
leg, file sizes; no graphics change.

STOP: Leg B's authoritative position has two writers that disagree
(report both; do not pick).
