# T191 — Composite atlas has 0% adoption; `/appearance` wire-verified

Two units run in parallel against one eqemu instance (the lock governs server
lifecycle, not client connections).

## A. The composite-texture atlas is never used

T187 reported "6 multi-buffer fallbacks per zone-in" and framed it as an
ordering bug — the sheet found, then not uploaded in time. **The real number is
worse and the cause is different.** Befallen, `--constrained orangepi`:

```
Composite entity adoption: 119/319 entities eligible, 0 sheet hits, 12 sheet misses
```

**Zero sheet hits.** 119 of 319 entities were eligible for the composite path
and not one of them used it. Meanwhile `preloadModelData` *does* find the sheets
and logs, per race, "composite sheet hit … deferred N per-part texture(s) …
instead of eager decode/upload" — so the optimisation pays its setup cost,
defers the per-part textures, is then not used, and the deferred textures have
to be loaded anyway.

Fallback causes, counted (22 total in this run):

| cause | count |
|---|---|
| `compositeAllowed=false` — sheet present and ready, entity not eligible | **18** |
| composite texture "not yet uploaded" | 4 |
| `buildCompositeMeshFromGeometry` returned null | 0 |

So T187's "not yet uploaded" is the minority cause. The dominant one is
eligibility:

```cpp
bool compositeEligible = !isPlayer && !hasBodyPartOverride && !hasEquipmentTint;
```
(`entity_renderer.cpp:545`) — any entity with a body-part equipment texture
(slots 0-6) or an equipment tint is excluded.

**The open question is the gap between the two numbers: 119 entities WERE
eligible, and there were still 0 sheet hits.** Eligibility is not what stopped
those. Per-race mesh caching means one miss per race can serve every later
entity of that race from the multi-buffer mesh, which would explain it —
unverified.

This matters because it sits inside `seq_entities`, which T187 measured as 64%
of the Pi's cold zone-in. A composite path with 0% adoption is the single
largest piece of unrealised work found so far.

**Not fixed here** — this unit establishes the number and the cause split.

## B. T153's `/appearance` is wire-verified

Owed since T153 (merged source-verified, never probed).

```
/appearance face 3
  -> Requesting appearance: haircolor=0 ... face=3
  -> Face change requested
  <- [SimpleMessage] string_id=12028, 'Facial features accepted.'
```

The server accepted it and said so. `SendFaceChange` works on the wire.

**T151's finding is unaffected and still stands:** `OP_SetFace`, the *broadcast*
of the result, is not mapped for Titanium, so a second client will never see the
change. That is protocol, not our bug.

Still unverified from that arc: T149 (tradeskill) and T150 (social/find), both
of which contain senders documented as impossible as written
(`SendRecipesFavorite`'s variable-length body vs an exact 2008-byte gate;
`SendFriendsWho`'s payload can never match eqemu's `strchr`) — those are
fix-then-verify, not verify.

## Method note

All runs re-zone with `#zone` first (T190's lesson): the character's saved
position is whatever the client last sent, and a bad one puts the player
outside all BSP regions, which invalidates anything measured afterwards.
