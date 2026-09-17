# T498 — B755: `item_models.json` vs the server's `items.idfile`

Class A (B755, re-classed by T485). Lane: **LOCAL eqemu** (`export EQEMU_HOST=local`), the lock
only for the export. Do NOT touch the VM at 172.16.200.99. Board entries start at **B823**.
Planned read-only on fable `ec1e9d77`.

## The facts this plan rests on

- [measured] `data/item_models.json` is a flat `{"<itemId>": <modelId>}` map: 3,661 rows, keys
  **500..8456**, 156 distinct model values, no key ≥ 10000; `"2894": 10009`. No provenance field.
- [measured] `EquipmentModelLoader::getModelIdForItem()`
  (`src/client/graphics/eq/equipment_model_loader.cpp:151-155`) **ignores** the map
  `loadItemModelMapping()` builds and queries `WilleqDatabase::getItemModel()`
  (`src/client/willeq_database.cpp:524`, table `item_models`). The JSON file at runtime is a
  startup `checkFile` (`application.cpp:242`) plus an input to `willeq.db`; the db table is the
  live lookup.
- [measured] `appearance.equipment[7|8]` has **two feeders with different units**:
  - **self**: `entities_component_appearance.cpp:552-562` — weapon slots take **`item->itemId`**
    (a DB item id) → item-id lookup → JSON/db model.
  - **peers & NPCs**: spawn parse (`entities_component.cpp:217`,
    `entities_component_updates.cpp:369`) and `ZoneProcessWearChange()`
    (`entities_component_appearance.cpp:436`) — the server's **material**, which for weapons is
    `atoi(&item->IDFile[2])` (`/mnt/projects/akk-stack/code/zone/mob_appearance.cpp:174-226`).
  Weapon idfiles are ≥ 10000 and the JSON's keys stop at 8456, so the peer lookup misses and every
  render site falls back to `modelId = equipmentId` (`renderer_zone_load_entities.cpp:461-463`,
  `entity_prep_worker.cpp:405`, `entity_equipment.cpp:102,134,226`, `character_model_view.cpp:541`).
  **The JSON only decides the LOCAL player's own weapon (third person + paperdoll) and the offline
  tools; every other character is already drawn from the packet.**
- [measured] The wire carries the truth for the self path too: Titanium serialises `item->IDFile`
  as static field **3** (`titanium.cpp:3359`); fable's parser skips it —
  `inventory_item_parser.cpp:207` is the comment `// fields[3] = IDFile`. `ItemInstance` has no
  idfile.
- [measured] Collision hazard: any `items.idfile` whose numeric part lands in [500, 8456] hits a
  JSON row and silently overrides the server's material on the peer path too. Count unknown.
- [measured] `DbReproducible` (`tests/cmake/hygiene.cmake:117-128`) pins `willeq.db` against the
  JSON; nothing pins the JSON. `tools/willeq_db/db_loaders_json.cpp:56-120` skips non-numeric keys;
  `equipment_model_loader.cpp:141` does `std::stoul(key)` which **throws** on a header key.
- [measured] `scripts/eqemu/env.sh` has `mysql_exec <sql>`; `scenario.sh` has `--seed-sql` (T494).
- [assumed] 2894 is one symptom of a stale table, not a lone typo; task 1 measures the drift and
  gates task 4.

Named query: `docker exec akk-stack-mariadb-1 mysql -uroot -p<MARIADB_ROOT_PASSWORD from
/mnt/projects/akk-stack/.env> peq -e "select id,name,idfile from items where id=2894"` — expected
`2894 | Spiny Two Handed Sword | IT10004`.

## Tasks

1. **Export + diff.** `scripts/eqemu/export_item_idfile.sh` (sources env.sh; `eqemu_lock_take
   T498`, release on every path; exit 64 = held → poll every 10 min up to 3 h, T496 may hold it):
   `mysql_exec "SELECT id, name, idfile FROM items ORDER BY id"` → `data/item_idfile.tsv` with a
   `#` header (source host, db, `items` row count, UTC timestamp). `scripts/check_item_models.py
   --diff` (pure python, offline) → `review/T498/item_model_mismatches.tsv`, one row per defect,
   class ∈ {mismatch, not-on-server, server-only, non-numeric-idfile}, plus the **collision
   census** (server idfiles in [500,8456]). Report the counts.
2. **Red first.** `ItemModelsMatchServer` = `ADD_TEST` in `tests/cmake/hygiene.cmake` (LABELS
   hygiene, python, modelled on `DocCounts`): `check_item_models.py --check --repo <src>` compares
   every JSON row against the committed `data/item_idfile.tsv`. Commit the export FIRST and record
   the red — it must name **item 2894: json 10009, server IT10004** — into
   `review/T498/red_item_models_match_server.txt` BEFORE task 4. No test may need a live mariadb.
3. **Root fix — use the packet.** Parse field 3: `uint32_t idfileModel` on `ItemInstance`, set in
   `parseStaticData()` by `IT(\d+)` (0 if unparseable = eqemu's own guard); in
   `UpdatePlayerAppearanceFromInventory()` set `equipment[7|8] = item->idfileModel` (WARN + old
   behaviour only if 0). Self and peers then agree by the same rule as `Mob::GetEquipmentMaterial()`.
   `equipment[]` becomes uniformly a material/IT number: delete the `getModelIdForItem()` call from
   the seven render sites (each already has the fallback), which also removes the collision. Keep
   `getModelIdForItem()` for the tools.
4. **Regenerate the JSON with provenance.** Rebuild `data/item_models.json` from the export (id →
   numeric idfile) with a `"_provenance"` object; make `loadItemModelMapping()` skip non-digit keys.
   Rebuild `data/willeq.db` + `.sha256` in the **same commit** (else `DbReproducible` goes red).
   **Do both, in this order — do not drop the file**: the packet is the only defensible runtime
   source; the tools (`model_viewer_startup.cpp:396`, `ui_inventory_capture.cpp:270`) need the
   table offline, regenerated, provenanced and ratcheted by task 2.
5. **Live proof only if** task 1's collision census is non-empty or the self path needs proving on
   the wire: two-client on the local lane, `scenario.sh --seed-sql` seeding 2894 into the actor's
   general slot, actor `/equip`s, `equip_observed` (`PeerEquipmentMaterial(name, 7)`) reads
   **10004** while the actor's own renderer picks the same number.
6. **Board + docs.** Strike B755 with the four counts. New: **B823** (B) self path computed weapon
   materials by a different rule than the server for the client's whole history — `equipment[]`
   mixed item ids and materials with no type; **B824** (C) `loadItemModelMapping()` builds a map
   nothing reads; **B825** (C) the JSON covers 3,661 of ~100k items, uncovered ones fell to
   `modelId = itemId` unremarked; **B826** (C) no derived data file carries provenance — propose the
   `"_provenance"` convention. CLAUDE.md: `equipment[]` is a MATERIAL/IT number, never a DB item id.

## STOP conditions
- **Held-item goldens may move** (2894 and any other mismatched row draw a different mesh): T485's
  `HeldItemTest.*`, `FirstPersonWeaponAttachmentTest.*`, paperdoll/contact-sheet goldens. **Do not
  re-bless — Will's call.** Report the red list with before/after model ids and stop.
- [measured] `tests/goldens/application_init_order_g04.txt:90` and `tests/test_data_paths.cpp:256`
  name `data/item_models.json`: **keep the startup `checkFile`**.
- A large mismatch set (hundreds+) or server models with no `IT*` archive in the client tree: stop
  before regenerating — a wholesale swap is a visual change, not a data fix.
- Never the VM; never re-bless a T485 pin; no edits to akk-stack server code; release the lock.

## Rules
- Private scratch `scratchpad/T498/`; no ccache; -j8; `review/` beside the checkout; a task
  worktree lacks `data/eq_client` and `data/eqe1_client` symlinks (B815) — create both.
- Commit code, tests, data (export + regenerated json + db), scripts, board, docs, plan Review.

## Review
(executor)
