-- B630 (issue #48) seed: put the caster and the observer in the SAME zone at
-- the SAME coordinates, with the caster GM, so it can #interrupt and
-- #memspell its own kit and every leg's targeted cast is IN RANGE of the
-- peer.
--
-- Applied by scripts/scenario.sh AFTER the snapshot and BEFORE the clients log
-- in, so the snapshot restore takes every row back out again (the rig leaves
-- the database exactly as it found it).
--
-- Testcaster (67) is the wizard caster; Testbuddy (68) the non-GM observer.
-- Both are co-located in zone 36 (befallen on this server), a public,
-- non-instanced zone, so the two clients share one zone instance and each
-- sees the other as a live peer.
--
-- The coordinates are seedable and MUST be: the server zones each character
-- at its STORED x/y/z (character_data), so setting only zone_id leaves the
-- two characters at whatever spots they were last saved -- in the first two
-- runs they were in the same zone but ~2,500 units apart, so every targeted
-- leg failed "Your target is out of range, get closer!" and no cast ever
-- began (the analyzer's declared-spell cells could not pass). restore_selftest.sh
-- uses the same UPDATE shape (x/y/z/heading), so this is known-good and is
-- restored by the snapshot.
SET @co_x = -82;
SET @co_y = 35;
SET @co_z = 3;

-- Caster: GM, level 50 (enough to cast the leg spells without class-min
-- fizzle), mana refilled, co-located with the observer.
UPDATE character_data SET
  gm = 1,
  level = 50,
  zone_id = 36,
  x = @co_x,
  y = @co_y,
  z = @co_z,
  heading = 0,
  cur_hp = 1,
  mana = 999,
  endurance = 100
WHERE id = 67;

-- Observer / fizzle caster: NOT GM (the fizzle leg needs a non-GM caster),
-- level 1 (low-skill, so the fizzle roll clamps high if the B leg is used),
-- same zone AND same coordinates.
UPDATE character_data SET
  gm = 0,
  level = 1,
  zone_id = 36,
  x = @co_x,
  y = @co_y,
  z = @co_z,
  heading = 0,
  cur_hp = 1,
  mana = 50
WHERE id = 68;
