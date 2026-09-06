# T422 — spell effects matched live against the oracle: burst size, lifetime, animation rate (B356, B370, B226)

Class A; eqemu lock (atomic; must be ABSENT); slot1. Read B356/B370/B226,
T389's oracle table and videos (`review/T389/oracle/`), T417/T420's
§Reviews (the sprite atlas, per-particle frame by age, the oracle capture
was only ~5 renders/s — capture at a higher rate this time: raise the
x11grab fps and confirm the client's own render rate from consecutive-
frame diffs), T405's rig (`#goto x y z h`, `zone.rain_chance` zeroed,
`#set time` re-pinned, `#weather 3 1 0`), T401's two-client `--script`
driver, and `config/spell_effects.json`.

1. Capture the oracle at ≥ 20 fps (verify the render rate) for Frost
   Bolt (54), Burst of Flame (93), Weaken (41), Whirling Wind (253):
   gem casts (T395 — GM casts send no BeginCast), caster in frame at a
   fixed distance, day. Per spell per stage: particle count (peak),
   sprite screen size, duration, and the animation rate (cells/s from
   consecutive renders — the rotating sword `spelg1` if a spell uses it;
   else `spela2`'s stars).
2. Fable, same spot/distance, same four spells, same capture rate.
3. A/B one parameter at a time in `spell_effects.json` (count, size,
   lifetime, emission rate) and the frame-rate rule (per-lifetime vs a
   fixed cells/s — T420 chose per-lifetime because the oracle refuted an
   effect-wide clock; re-test with the better capture) until each stage
   matches the oracle within the noise floor on count/size/duration;
   every value in the commit carries its probe.
4. The observer pair (B160's original owed proof) with the real sprites:
   both colours AND shapes on the second client.
5. Tests pin the solved config (pre-fix failing). Strike B356/B370/B226.
   Restore server values (world DOWN); `down.sh`; delete the lock.

STOP: lock present; the oracle's rendering rate is still too low to
measure the animation rate (report the ceiling and settle the rest).
