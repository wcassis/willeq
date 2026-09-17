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

Executor: `wt/t6`, branch `task/T498-item-models-vs-server`, base `ec1e9d77`
(plan `c0dfce8c`). Lane: LOCAL eqemu, `/mnt/projects/.eqemu-lock` taken as
`T498` for the export only and released; the VM at 172.16.200.99, the Pi and
hermes were never touched. Media: `/mnt/projects/fable/review/T498/`.

**Every fact the plan rested on re-verified on this tip**, with two corrections:
`getModelIdForItem()` had **nine** client call sites, not seven --
`entity_lifecycle.cpp:52,57` (the `removeMeshRef` twins of
`entity_equipment.cpp`'s `addMeshRef`) and, decisively,
`EquipmentModelLoader::getEquipmentMesh()` itself (`equipment_model_loader.cpp:479`),
which is the site that actually picks the MESH. Leaving that one would have left
the collision exactly where it was: the seven the plan named only choose the
shield bone and the ref-count key.

### Task 1 -- the drift is a stale table, not a typo [measured]

`scripts/eqemu/export_item_idfile.sh` -> `data/item_idfile.tsv`, 117,944 peq
rows with a provenance header. `scripts/check_item_models.py --diff`:

| class | count |
|---|---|
| mismatch | **1,226** (248 of them where the server idfile is >= 10000) |
| not-on-server | **902** (keys 500..8423; **497 are below peq's first item id**, so they were never item ids) |
| server-only | **115,185** |
| non-numeric-idfile | **0** |

**Collision census: 8 materials over 9 server items** (`collision_census.tsv`),
every one of which the JSON would have drawn as a different model on the peer
path: materials 536/537/540/556/566/646/655 -> 63 or 64, and **1340 -> 10627**.
Small, but non-empty, and only because weapon idfiles are >= 10000 and the
file's keys stop at 8456.

The named query reproduces exactly: `2894 | Spiny Two Handed Sword | IT10004`
against the file's `10009`.

[measured] The mismatch set is **not** mostly weapons: 978 of the 1,226 are rows
like `Cloth Choker` (json 72, server `IT63`), i.e. armour, whose appearance
comes from `material` and never from this file at all. Only 99 of the 3,661
values are >= 10000 while 248 mismatched rows have a server idfile >= 10000 --
the file is largely NOT idfile numbers. That is the evidence that it is a
different quantity from a different source, not a drifted copy of one.

### Task 2 -- the red, and why the test ships as a ratchet

`review/T498/red_item_models_match_server.txt`, captured at `c7896cfa` BEFORE
any data or code change: **2,128 of 3,661 rows disagree**, and the report names
the boarded example first -- `item 2894 (Spiny Two Handed Sword): json 10009,
server IT10004`.

**DEVIATION from the plan, and the reason.** The plan had task 4 turn this test
green. Task 4 is STOPPED (below), so a plain `--check` would ship RED. The test
therefore runs against `data/item_models_drift_baseline.txt`, the tree's own
`WriteOnlyFields`/`file_size_allowlist` idiom: it fails on a defect NOT on the
list, and equally on a listed row that has been fixed without being struck, so
the list cannot outlive the defect. `--strict` ignores the baseline and
reproduces the full red. Both directions are PROVEN, not asserted: setting item
1001 to 999999 fails with "1 NEW defect", and setting 2894 to the correct 10004
fails with "1 baseline row is no longer a defect".

### Task 3 -- the root fix is the wire

`ItemInstance::idfileModel` parses Titanium static field 3 (`IT10004` -> 10004,
0 for the 222 peq rows carrying a bare `IT` and for anything unparseable --
eqemu's own `atoi` guard), `UpdatePlayerAppearanceFromInventory()` uses it for
slots 7/8 with a `LOG_WARN` + old behaviour at 0, and the item-id lookup is gone
from all **nine** client sites. `equipment[]` is now uniformly a material/IT
number; `getModelIdForItem()` survives for `tools/model_viewer` and
`tools/hwcaps/ui_inventory_capture`, and its header says so. `loadItemModelMapping()`
no longer THROWS on a non-digit key (`std::stoul` did), which is what a
`"_provenance"` header would have done to the client at startup.

**No golden moved, and that is a measurement, not a hope.** [measured] Every
value the held-item and first-person pins put in `equipment[]` -- 10649, 10004,
202, and `{6,10,15,16,63}` -- is outside the JSON's key range 500..8456, so the
lookup they lost always missed. `tools/hwcaps/ui_inventory_capture`'s ids
(1, 45, 202, 5, 4, 157, 10) likewise, and its own comment already said it
depended on the miss. No T485 pin was re-blessed.

### Task 4 -- **STOPPED** (the plan's own condition)

"A large mismatch set (hundreds+) ... stop before regenerating -- a wholesale
swap is a visual change, not a data fix." 1,226 mismatches and 902 rows that are
not items at all. `data/item_models.json`, `data/willeq.db` and
`data/willeq.db.sha256` are UNCHANGED, so `DbReproducible` is untouched, and the
startup `checkFile` stays. `scripts/check_item_models.py --rebuild` is written
and does the swap (with the `"_provenance"` object) in one command when Will
says so; boarded as **B827**.

### Task 5 -- ran, and it took two attempts

The collision census is non-empty, so the live proof was owed. It PASSED on the
local lane (`review/T498/scenario/`, `scripts/scenario.sh` exit **0**), and both
halves are in it:

- the OBSERVER read `the peer's material slot 7 went 0 -> 10649` -- the server's
  own number, off the peer's spawn packet;
- the actor's return login read `self material slot 7 reads 10649 (expecting
  10649)` -- the number THIS client derived from its own inventory, which
  `data/item_models.json` would have made **64**.

That second half did not exist before: T494's pair could only see the server's
statement about someone else. `IScenarioWorld::SelfEquipmentMaterial()`,
`--scenario-expect-material` and `scripts/scenario.sh --expect-material` are new,
and both directions are pinned device-free in
`tests/test_scenario_state_machines.cpp`.

**The first attempt failed and the reason is a fact worth keeping** [measured]:
seeded with B755's own item 2894, fable's `/equip` refused -- *"Spiny Two Handed
Sword cannot go in Primary (13) -- your character does not meet its
class/race/level requirements."* 2894's `classes` is 32797, which has no Magician
bit (4096), and Testbuddy is a level-1 gnome Magician. The run ended "the peer
never came back" with material 0 everywhere, which is a scenario that proved
nothing rather than a red. The re-run uses **1074, Ruined Heretic Longsword**
(`IT10649`, json 64, classes 32767, races 65535, reqlevel 0, slots 24576) -- the
same defect in an item this character can hold. A live `--seed-sql` item has to
be checked against the ACTOR's class/race/level, not just against the defect.

### Suite

`ctest -j6` (visual_gate excluded, then run alone against a `build-gles2/`
rebuilt from this tip): **4,903 of 4,903 passed, 0 failed**, 125 skipped, plus
`visual_gate` PASSED standalone in 21.3 s -- **4,904 green in total**. All 31 `HeldItem*` / `FirstPersonWeaponAttachment*` /
paperdoll tests green; no golden moved and no T485 pin was re-blessed.

**`DocCitations` was RED on the base tip `ec1e9d77` and it was not mine**
[measured]: T497's B822 proposes `scripts/eqemu/selftests.sh` as a backticked
path and the checker reads a backticked path as a citation, so it failed with
"file does not exist". Fixed in place by de-citing that one line, because
BOARD.md is this unit's territory anyway. Worth knowing that a board entry
PROPOSING a file is a DocCitations failure -- there is no "proposed" exemption.

### Task 6 -- board and docs

B755 struck with the four counts and the census. New: **B823** (struck, closed
by this unit -- the two-feeder shape), **B824**, **B825**, **B826**, **B827**.
CLAUDE.md gains the rule under Key Patterns: `equipment[]` is a MATERIAL/IT
number, never a DB item id. T485's Review gained a one-line DONE marker on the
lead it left.

