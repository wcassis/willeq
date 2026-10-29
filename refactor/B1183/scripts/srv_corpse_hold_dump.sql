-- B1183 (#502) legH dump: the one row the change touches, as a diffable TSV.
SELECT ruleset_id, rule_name, rule_value FROM rule_values WHERE rule_name='NPC:EmptyNPCCorpseDecayTime' ORDER BY ruleset_id;
