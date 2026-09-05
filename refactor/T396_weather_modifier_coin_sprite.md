# T396 — the weather ambient modifier (B211); the coin sprite (B218)

Class A + B, slot3, no server. Read the B211 board text and T394's
§Review (the bisect that found it), `src/client/graphics/environment/`
(the weather effects and `getAmbientLightModifier()`),
`IrrlichtRenderer::updateTimeOfDay` and `vision_ambient.cpp`; for Leg B
the B218 text, `tools/` UI atlas builder and `EQUI_Inventory.xml`'s
`A_PlatinumCoin` etc. decals.

## Leg A — B211

T377's vision session logged `weatherMod=0.460` with no weather in the
recipe; the line only fires below 0.99 so a return to 1.0 is never
logged; and the modifier multiplies the ambient BEFORE the vision floor.
1. Why 0.46 with no weather: read what sets the modifier (server weather
   packets `OP_Weather`, a zone default, a fade-in state that starts dark,
   a stale value across zone-ins). Reproduce headlessly: construct the
   weather effects object, feed it the sequence a zone-in produces (or
   none), and read the modifier over simulated time — pre-fix test
   showing 0.46 (or whatever it is) with no weather packet.
2. Match the oracle for the RULE, not the number: in the official client
   clear weather does not darken the world (T361/T367 measured the oracle
   night frame at the same spot as a stable reference — quote it). So
   with no weather the modifier must be 1.0; with rain/snow it darkens by
   the amount the official client does — if that amount is unmeasured,
   leave the weather-active value alone and board the measurement.
3. Log the modifier on every change (both directions) and once per
   zone-in, so a session log can prove weather-free.
4. Order: decide by reading whether the vision floor must clamp the
   FINAL ambient (after weather) — it is a floor on what the player sees,
   so it applies last; if the code already does that, say so; if not,
   fix and test.
5. Strike B211; annotate B203 (the re-solve can now pin weather = 1.0 and
   prove it from the log).

## Leg B — B218

The oracle's coin rows carry a coin decal per denomination
(`A_PlatinumCoin` … from the official UI atlas). Add the four decals to
fable's UI atlas via the existing atlas builder (read where the item
icons come from — the same PFS?), draw them in `coinDecalRect()` (T393),
and pin with the static-panel seam (pre-fix failing: the decal rect draws
the letter). Strike B218. If the source sprites are not in any archive
fable reads, say so and STOP that leg.

## Proof

Build (all trees if the atlas changes), `ctest -j6` (no expected
failures), the tests with pre-fix/post-fix runs, file sizes; graphics
change → visual gate standalone + ARM (weather at 1.0 in the gate's
daytime zones — if a golden MOVES, that is the confound made visible:
STOP with the contact sheet, never bless).

STOP: the 0.46 comes from a server packet the gate zones also receive
(then the goldens carry the weather and the ruling is Will's).
