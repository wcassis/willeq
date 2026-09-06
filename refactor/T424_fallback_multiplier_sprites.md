# T424 — the spell fallback's falloff in RGB (B322); ambientMultiplier_ (B380); six unused UI sprites (B351)

Class B ×3, slot2, no server. Read the three board entries, T413's
§Review Leg C (the fallback is solid white 24-bit; keep additive, put
the cosine falloff in RGB — the shipped-data cascade rule), T423's §Review
(`ambientMultiplier_` one reader, zero writers), T416's §Review (the
UISprite census).

## Leg A — B322

`generateSpellParticleFallback()` writes the cosine into alpha over
constant white RGB; the 24-bit BMP drops it and the additive blend
(`GL_ONE, GL_ONE`) never samples alpha. Write the falloff into RGB
(premultiplied white × cosine) so the additive draw is a soft dome (T389's
generic dome is the oracle: soft, translucent). Regenerate → `--write-pins`
→ pack rebuild via the script (atomic; no ctest/gate/willeq running —
slot1 runs a LIVE client for T422: install only when no `willeq` process
exists; copy the old pack under `review/T424/`) → both sha pins, ONE
commit. Test pre-fix failing: the shipped BMP's centre-to-edge RGB
profile is a cosine, not flat; the pack entry matches. Report the
`eqe1MappedBytes` delta (never edit the manifest). Strike B322.

## Leg B — B380

`ambientMultiplier_` is read by `composeSceneAmbient()` and written by
nothing. Read what it was for (git log -S); if it was a zone/config
override with a lost writer, either wire the writer (the zone override
T423 named as an input) with a test, or delete the field and its read
(compiler proof). Say which. Strike B380.

## Leg C — B351

Six of 21 `UISprite` regions have no consumer. For each: is it a sprite
the oracle's window pieces draw somewhere fable does not yet (then
board its consumer under the panel item) or a leftover (delete the
region from the atlas builder and the enum; the atlas layout changes →
pack cascade in the SAME commit as Leg A's install if both change the
pack — sequence them so the pack is installed once). Strike B351.

## Proof

Build (background), `ctest -j6` (no expected failures), pre-fix/post-fix,
`TexturesReproducible`/`PackReproducible` green in every commit, visual
gate standalone + ARM (a moved image golden is a STOP), file sizes.

STOP: none expected.
