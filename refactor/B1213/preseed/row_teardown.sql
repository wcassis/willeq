-- B1213 (#532) worn-FX row TEARDOWN.
-- Removes every account/character/inventory row the preseed created, restoring
-- the DB to its pre-session state. Idempotent; safe to run more than once.
-- The id ranges are the generator's (account >= 10000, login_accounts >= 20000,
-- character >= 5000, inventory character_id >= 5000) -- disjoint from the
-- pre-existing lane data (accounts 1..80, chars 1..104).
START TRANSACTION;
DELETE FROM inventory           WHERE character_id >= 5000;
DELETE FROM character_currency  WHERE id           >= 5000;
DELETE FROM character_data      WHERE id           >= 5000;
DELETE FROM login_accounts  WHERE id           >= 20000 OR account_name LIKE 'fxrow\_%';
DELETE FROM account         WHERE id           >= 10000 OR name       LIKE 'fxrow\_%';
COMMIT;
