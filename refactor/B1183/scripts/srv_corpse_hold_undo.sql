-- B1183 (#502) legH undo: restore the original NPC:EmptyNPCCorpseDecayTime
-- (0) so the after-dump matches the before-dump byte for byte.
UPDATE rule_values SET rule_value='0' WHERE ruleset_id=1 AND rule_name='NPC:EmptyNPCCorpseDecayTime';
