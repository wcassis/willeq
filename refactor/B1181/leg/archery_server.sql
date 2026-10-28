-- B1181 leg A server-side (B1174 hook; undo runs on every exit path): the
-- willeqwatch account (the observer's account) gained a second character,
-- 'Darkshadow' (id 104), after B1134. The driver's zone step selects the
-- FIRST character in the select screen (jclick 123 315); the list is
-- alphabetical, so Darkshadow shadows Oraclewatch and the observer logs in
-- as a level-1 character. For the leg, move 104 to the caster's account
-- (67's) so willeqwatch lists only Oraclewatch; the undo restores it to
-- Oraclewatch's account.
UPDATE character_data SET account_id=(SELECT account_id FROM character_data WHERE id=67) WHERE id=104 AND name='Darkshadow';
