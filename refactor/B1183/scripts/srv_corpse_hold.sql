-- B1183 (#502) legH: hold the NPC corpse long enough to loot it.
-- The active ruleset is "default" (ruleset_id=1; the RuleSet variable is
-- unset, so zone/main.cpp falls back to "default"). The empty-NPC-corpse
-- decay timer is RuleI(NPC, EmptyNPCCorpseDecayTime) + 1000 ms (corpse.cpp
-- constructor); the DB value 0 => the corpse despawns ~1.03 s after OP_Death
-- (B1133, measured on both clients). Bumping it to 300000 (5 min) keeps the
-- TrollF corpse alive for the whole loot measurement. Runs with the stack
-- DOWN, before up.sh (the zone reads rules at boot, main.cpp:259); the undo
-- restores the original 0 on every exit path.
UPDATE rule_values SET rule_value='300000' WHERE ruleset_id=1 AND rule_name='NPC:EmptyNPCCorpseDecayTime';
