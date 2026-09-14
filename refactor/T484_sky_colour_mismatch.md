# T484 — fable's sky is a tan/orange texture with pale white trees where the official client's is blue (B726)

Class A (visible, match the oracle), **Leg A offline (no lock)**, slot1. Found
by T478 (read its Review `refactor/T478_oracle_regrab_chase_and_held_items.md`
§4 and the stills under `review/T478/`, which show both clients at the same
qeynos2 spot at 11:00 game time, weather 0). Prior sky/lighting work to read
first: T428 (night base, vision floors), B445/B446 (ambient uploaded twice,
chromaticity squared), B231/T418 (rain darkening), B360 (fog model) —
grep `refactor/BOARD.md` and `refactor/completed/` for "sky".

## Tasks

1. **Describe the difference from the frames, precisely.** Crop the sky region
   of both clients' T478 stills, and report the mean and per-row RGB. State
   whether the whole dome is wrong, or only a layer (clouds, base colour).
   Report the same for the "pale white trees": is that tree texture colour, or
   the sky tint bleeding into foliage through fog, ambient or the alpha
   companion?
2. **Trace fable's sky for qeynos2.** Cover:
   - which sky asset (`sky.s3d` layer / dome index for the zone,
     `zoneinfo`/`sky` type from the server's zone data);
   - which textures bind, and whether they come through EQE1 with their ETC1
     `_alpha` companion;
   - the time-of-day tint;
   - fog colour;
   - the shader path.

   Compare with what the official client's zone data implies. The eqemu `zone`
   table's sky column and the sky type fable receives are in the session logs
   under `review/T478/`.
3. **Leading suspects, confirm or refute each.**
   - (a) The wrong sky type/layer index: an off-by-one or a mis-mapped sky id.
   - (b) An ETC1 alpha-companion or channel-order issue on the sky textures:
     BGR/RGB swap, or blue lost through the T411 companion path.
   - (c) The B445 double-ambient upload squaring chromaticity on sky and foliage.
   - (d) The time-of-day tint table applied at the wrong hour.

   Check each with a headless render or a unit seam and real assets. Do not
   eyeball.
4. Fix the root cause with a test that fails first. Capture before/after x86
   stills at the T478 pose, if possible through an offline path (the gate's
   `gles2_zone_benchmark` renders qeynos2). Put them in `review/T484/` with a
   README; never self-assess. If the fix needs a live A/B against the oracle,
   board it as Leg B (lock).

## Proof

Standard verification bar, builds strictly in sequence. Update the G-batch
goldens for touched TUs in place per G-correction 32, and say so. **Visual gate:
the gate zones render sky, so an image golden WILL likely move if the sky
changes. STOP and report with before/after, because Will blesses; never
self-bless.** ARM; file sizes. Board B780–B789; strike or narrow B726.

File territory: sky / environment rendering (`src/client/graphics/environment/`
sky and time-of-day, the sky shader uniforms), sky asset loading, related
tests, `review/T484/`. **Not** spell code / the gem panel (T483, slot3), and not
the object/character loaders or the T479 branch (slot2).

STOP: an image golden moves (expected if fixed — report before/after for Will's
bless); the cause is B445's ambient double-upload (report it; that re-solve is
a known lock-queue item with measured baselines, not a drive-by).

## Review

(executor fills)
