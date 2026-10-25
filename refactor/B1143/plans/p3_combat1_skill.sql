-- B1143 p3_combat1 pre-SQL (inside the lock, after the driver's save + class
-- switch): Intimidation (skill_id 71; the Ti eqemu's skills.h comment
-- /*13922*/ is its eqstr_us.txt string id, "Intimidation") to 5000 so the
-- success roll is certain: special_attacks.cpp InstillDoubt rolls
-- GetSkill(Intimidation) + CHA/4 - (target level*4 + target WIS/4) and
-- random.h Roll(int) = Int(0,99) < required, i.e. >= 100 is 100%.
-- The driver's oracle_save_character_skills_67 restore returns it to 0
-- (lane_before: 67,71,0) after the leg.
UPDATE character_skills SET value=5000 WHERE id=67 AND skill_id=71;
