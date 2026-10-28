-- B1181 leg A server undo (B1174): restore 'Darkshadow' (104) to
-- Oraclewatch's account (103's — unchanged by the leg).
UPDATE character_data SET account_id=(SELECT account_id FROM character_data WHERE id=103) WHERE id=104 AND name='Darkshadow';
