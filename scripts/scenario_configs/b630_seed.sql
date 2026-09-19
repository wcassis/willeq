-- B630 (issue #48) seed: put the caster and the observer in the SAME zone with
-- the caster GM, so it can #interrupt and #memspell its own kit.
--
-- Applied by scripts/scenario.sh AFTER the snapshot and BEFORE the clients log
-- in, so the snapshot restore takes every row back out again (the rig leaves
-- the database exactly as it found it).
--
-- Testcaster (67) is the wizard caster; Testbuddy (68) the non-GM observer.
-- Both are co-located in cazicthule (36), a public, non-instanced zone, so the
-- two clients share one zone instance and each sees the other as a live peer.
-- The character x/y/z are not seedable here (the zone's default spawn is used),
-- which is fine: co-presence in the same public instance is what the lane needs.

-- Caster: GM, level 50 (enough to cast the leg spells without class-min
-- fizzle), mana refilled.
UPDATE character_data SET
  gm = 1,
  level = 50,
  zone_id = 36,
  cur_hp = 1,
  mana = 999,
  endurance = 100
WHERE id = 67;

-- Observer / fizzle caster: NOT GM (the fizzle leg needs a non-GM caster),
-- level 1 (low-skill, so the fizzle roll clamps high if the B leg is used),
-- same zone.
UPDATE character_data SET
  gm = 0,
  level = 1,
  zone_id = 36,
  cur_hp = 1,
  mana = 50
WHERE id = 68;
