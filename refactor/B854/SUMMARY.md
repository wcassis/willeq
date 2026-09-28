# B854: Kunark and Velious zone-in sweep on the Orange Pi

**CAVEAT (added 2026-09-28 audit-hygiene pass): this sweep ran
`--scenario zone_in`, which forces the client's headless Automated mode — no
graphics were initialised.** Every per-zone log shows `Console bridge attached
(headless mode)` at startup (e.g. `refactor/B854/logs/timorous_pi_client.log:22`),
and the memledger `point=ready` line for that same zone shows
`zone_mesh=0/0 textures_decoded=0/0 entity_models=0/0 irrlicht_scene=0/0`
(`refactor/B854/logs/timorous_pi_client.log:146`). **This sweep therefore
proves only PROTOCOL zone-in on the Pi.** The "Lane: DRM/GLES2 1280x720" label
below and the memory figures in this document are NOT rendered-client figures
and say nothing about the 64 MB rendered-client contract — a rendered Pi sweep
is still owed (issue #102).

**Date**: 2026-09-23  
**Binary**: fable ARM build sha cb8eec71877db8078c261035a54d9bc902d42e1d  
**Character**: Testcaster  
**Lane**: Pi (Orange Pi, 483 MB, DRM/GLES2, 1280x720) — **label is aspirational
only; the run was headless/Automated-mode with no graphics initialised, see
caveat above**  
**Total zones**: 46 (28 Kunark + 18 Velious)

## Results

|| Status | Count | Zones |
||--------|-------|-------|
|| **Clean** (exit=0, zoned in) | **43/46** | all except iceclad, templeveeshan, sleeper |
|| **Stalled** (exit=3, no ClientUpdate) | 1 | iceclad |
|| **Seed failed** | 1 | templeveeshan |
|| **Zone boot failed** | 1 | sleeper |

### Failures

1. **iceclad** (velious, 7594147 s3d bytes): exit=3, wall=135s, peak RSS 40.8 MB. Reached Phase 9/15 (same as #73/B751 signature), never completed zone-in. **Note on #73** — the #73 fix (MR !29) resolved stonebrunt (the #73 test case) but iceclad independently stalled. May be zone-specific network behavior.

2. **templeveeshan** (velious, 5293620 s3d bytes): seed_failed. Character could not be placed at zone's safe point (zone table query or DB constraint issue).

3. **sleeper** (velious, 5222761 s3d bytes): zone_boot_failed. Zone process would not boot within timeout.

## Memory profile

**CAVEAT: figures below are from the headless/Automated `zone_in` scenario
(see caveat at top of this document) — no graphics, no mesh/texture/entity-model
loading. They are NOT comparable to a rendered client's memory footprint and do
not speak to the 64 MB contract.**

Peak RssAnon across all zones: 60.3 MB (emeraldjungle, game_state bucket).

Top3 categories at last lifecycle point (typical for clean zones):
- game_state: 22-46 MB (dominant)
- startup: ~442 KB (willeq.db + spells + login)
- network: 136-177 KB

## Known pre-sweep fixes applied

- **B751/MR !29** (2026-09-23): UDP receive buffer fix — stonebrunt now clean (exit=0), iceclad independently stalls at Phase 9/15
- **B854 stdin drain fix** (this card): mapfile loop instead of here-string — all 46 zones processed, no swallowed iterations
- **B854 resume skip** (this card): TSV-first-zone dedup on resume — no duplicate rows

## Per-expansion breakdown

### Kunark (28 zones)

|| Zone | s3d_bytes | Status | Wall | Peak RSS |
||------|-----------|--------|------|----------|
|| stonebrunt | 7946269 | clean | 14s | 34.0 MB |
|| warslikswood | 6525667 | clean | 14s | 39.4 MB |
|| timorous | 6086601 | clean | 14s | 56.6 MB |
|| lakeofillomen | 5736341 | clean | 14s | 31.8 MB |
|| cabeast | 5159745 | clean | 14s | 15.6 MB |
|| cabwest | 5121059 | clean | 14s | 14.3 MB |
|| dreadlands | 4925937 | clean | 15s | 32.8 MB |
|| overthere | 4783737 | clean | 15s | 50.5 MB |
|| citymist | 4589515 | clean | 14s | 12.8 MB |
|| trakanon | 4565184 | clean | 15s | 37.4 MB |
|| skyfire | 4400614 | clean | 14s | 44.8 MB |
|| frontiermtns | 4125688 | clean | 14s | 37.6 MB |
|| firiona | 4119344 | clean | 14s | 31.4 MB |
|| swampofnohope | 3987542 | clean | 15s | 35.6 MB |
|| veeshan | 3830571 | clean | 14s | 16.5 MB |
|| burningwood | 3812191 | clean | 15s | 25.5 MB |
|| fieldofbone | 3761533 | clean | 14s | 25.7 MB |
|| sebilis | 3506579 | clean | 14s | 12.7 MB |
|| chardok | 3412773 | clean | 14s | 11.1 MB |
|| karnor | 3258251 | clean | 14s | 10.6 MB |
|| emeraldjungle | 3132826 | clean | 15s | 57.5 MB |
|| kurn | 2824793 | clean | 14s | 12.0 MB |
|| droga | 2774186 | clean | 14s | 15.6 MB |
|| warrens | 2242972 | clean | 14s | 10.0 MB |
|| charasis | 2159009 | clean | 14s | 8.0 MB |
|| kaesora | 1969412 | clean | 14s | 11.1 MB |
|| nurga | 1482593 | clean | 14s | 7.6 MB |
|| dalnir | 1377942 | clean | 14s | 7.8 MB |

### Velious (18 zones)

|| Zone | s3d_bytes | Status | Wall | Peak RSS |
||------|-----------|--------|------|----------|
|| iceclad | 7594147 | **stall (exit 3)** | 135s | 40.8 MB |
|| frozenshadow | 7449246 | clean | 14s | 18.2 MB |
|| thurgadina | 6948795 | clean | 14s | 16.6 MB |
|| thurgadinb | 6085837 | clean | 14s | 11.4 MB |
|| greatdivide | 5738839 | clean | 14s | 27.6 MB |
|| growthplane | 5644512 | clean | 14s | 33.4 MB |
|| skyshrine | 5509990 | clean | 14s | 22.5 MB |
|| templeveeshan | 5293620 | **seed_failed** | - | - |
|| sleeper | 5222761 | **zone_boot_failed** | - | - |
|| sirens | 4898678 | clean | 14s | 12.2 MB |
|| kael | 4856486 | clean | 14s | 16.9 MB |
|| westwastes | 4230134 | clean | 15s | 23.7 MB |
|| eastwastes | 3953177 | clean | 14s | 29.0 MB |
|| cobaltscar | 3638451 | clean | 14s | 12.6 MB |
|| velketor | 3279207 | clean | 15s | 12.0 MB |
|| crystal | 3070861 | clean | 14s | 9.9 MB |
|| necropolis | 2518376 | clean | 14s | 15.6 MB |
|| wakening | 2187786 | clean | 15s | 39.8 MB |

## Notes on failures

- **iceclad**: Same #73/B751 signature (Phase 9/15 stall after weather packet) that stonebrunt exhibited before MR !29. The #73 fix (UDP rcvbuf 8 MiB) resolved stonebrunt but iceclad independently stalls. Not an OOM (dmesg empty). May be zone-specific: iceclad has 100 spawns and 41 doors (notably high spawn count).

- **templeveeshan** + **sleeper**: Both are late-era Velious zones with different failure modes. templeveeshan fails at character seeding (zone table row exists but DB query fails). sleeper fails at zone boot (zone process itself won't start). Both are velious expansion zones; no other velious zones with 5M+ s3d_bytes failed.

## Artifacts

- TSV: `refactor/B854/sweep_results.tsv` (22 columns, 46 rows + header)
- Log: `refactor/B854/sweep.log` (full sweep output)
- Per-zone logs: `refactor/B854/logs/` (one per zone)
- Lane restore proof: `refactor/B854/lane_restore_proof.txt`
