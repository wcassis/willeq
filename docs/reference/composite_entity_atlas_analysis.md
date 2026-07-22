> **HISTORICAL SNAPSHOT** — point-in-time document from a completed batch or past audit; findings inside may be superseded and are NOT kept current. Current truth lives in `docs/decisions.md`, `docs/module_map.md`, and `refactor/deferred.md`.

# Entity Texture Atlas — Exhaustive Analysis

Generated 2026-04-06 by scanning all 1006 S3D archives in the EQ Titanium client.
Raw data: `docs/reference/exhaustive_texture_scan.json`

## Summary

- **215 unique race prefixes** found across 1006 S3D archives
- **106 races** have multiple texture variants
- **109 races** have only the base variant (v00)
- **78 shared equipment textures** (helms, chain, cloaks, etc.)
- **Variant IDs found**: 00, 01, 02, 03, 04, 05, 06, 07, 17, 18, 19, 20, 21, 22, 23

## Texture Naming Convention

Format: `{race3}{part2}{variant2}{page2}.bmp`

Example: `humch0101.bmp` = race `hum`, part `ch` (chest), variant `01` (cloth), page `01`

### Body Part Codes

| Code | Slot |
|------|------|
| he | Head/face |
| ch | Chest/torso |
| ua | Upper arms |
| fa | Forearms |
| hn | Hands |
| lg | Legs |
| ft | Feet |
| ta | Tail (some races) |
| sk | Skirt (hag only) |

## Variant Definitions

### Base Game (Original EQ)
| ID | Name | Notes |
|----|------|-------|
| 00 | Naked | Default skin texture |
| 01 | Cloth | Cloth armor |
| 02 | Chain | Chain mail |
| 03 | Plate | Plate armor |

### Kunark Expansion
| ID | Name | Notes |
|----|------|-------|
| 04 | Leather | Only ERM/ERF, HUM/HUF, IKM/IKF |
| 05-07 | Special | Only dragons (dra), qcm/qcf, spi, wlm |

### Velious+ Expansion (global17-23_amr.s3d archives)
| ID | Name | Notes |
|----|------|-------|
| 17 | Velious Leather 1 | All 12 playable races + IKM/IKF |
| 18 | Velious Chain 1 | All 12 playable races + IKM/IKF |
| 19 | Velious Plate 1 | All 12 playable races + IKM/IKF |
| 20 | Velious Leather 2 | All 12 playable races + IKM/IKF |
| 21 | Velious Chain 2 | All 12 playable races + IKM/IKF |
| 22 | Velious Plate 2 | All 12 playable races + IKM/IKF |
| 23 | Velious Monk | Only HUM/HUF, IKM/IKF |

### Robe Textures (Kunark, shared across races)
Robes use `clk{NN}{page}.bmp` naming, not race-specific. Applied via body mesh variant swap.

| Robe | Files |
|------|-------|
| clk04 | clk0401-clk0406.bmp |
| clk05 | clk0501-clk0506.bmp |
| clk06 | clk0601-clk0606.bmp |
| clk07 | clk0701-clk0706.bmp |
| clk08 | clk0801-clk0806.bmp |
| clk09 | clk0901-clk0906.bmp |
| clk10 | clk1001-clk1006.bmp |
| clkerm06 | Erudite male race-specific robe |
| clkerf06 | Erudite female race-specific robe |

## Playable Races (IDs 1-12 + Iksar 128)

All have 7 body parts (ch, fa, ft, he, hn, lg, ua) and variants 00-03 + 17-22.

| ID | Race | Male | Female | Extra Variants | Source Archives |
|----|------|------|--------|----------------|-----------------|
| 1 | Human | HUM | HUF | 04, 23 | global_chr, global17-23_amr |
| 2 | Barbarian | BAM | BAF | — | global4_chr, global17-22_amr |
| 3 | Erudite | ERM | ERF | 04 | global_chr, global17-22_amr |
| 4 | Wood Elf | ELM | ELF | — | global_chr, global17-22_amr |
| 5 | High Elf | HIM | HIF | — | global_chr, global17-22_amr |
| 6 | Dark Elf | DAM | DAF | — | global_chr, global17-22_amr |
| 7 | Half Elf | HAM | HAF | — | global_chr, global17-22_amr |
| 8 | Dwarf | DWM | DWF | — | global_chr, global17-22_amr |
| 9 | Troll | TRM | TRF | — | global_chr, global17-22_amr |
| 10 | Ogre | OGM | OGF | — | global_chr, global17-22_amr |
| 11 | Halfling | HOM | HOF | — | global_chr, global17-22_amr |
| 12 | Gnome | GNM | GNF | — | global_chr, global17-22_amr |
| 128 | Iksar | IKM | IKF | 04, 23 | global4_chr, global17-23_amr |

**Texture count per playable race/gender**: 122-166 textures across all variants.

## NPC/Monster Races with Multiple Variants

| Race | Parts | Variants | Total Textures | Description |
|------|-------|----------|----------------|-------------|
| dra | ch,ft,he,lg,ua | 00-07 | 186 | Dragons (8 color variants) |
| qcm | ch,fa,ft,he,hn,lg,ua | 00-06 | 91 | Qeynos citizen male |
| qcf | ch,fa,ft,he,hn,lg,ua | 00-05 | 72 | Qeynos citizen female |
| gnn | ch,fa,ft,he,hn,lg,ua | 00-04 | 90 | Gnoll |
| gia | ch,fa,ft,he,hn,lg,ua | 00-04 | 75 | Giant |
| kob | ch,fa,ft,he,hn,lg,ua | 00-04 | 64 | Kobold |
| wlm | ch,ft,he,hn,lg,ta,ua | 00-04 | 63 | Wurm |
| orc | ch,fa,ft,he,hn,lg,ua | 00-03 | 40 | Orc |
| gol | ch,fa,ft,he,hn,lg,ua | 00-03 | 50 | Golem |
| gob | ch,ft,he,hn,lg,ua | 00-03 | 40 | Goblin |
| wol | ch,ft,he,lg | 00-03 | 59 | Wolf |
| drk | ch,ft,he,hn,lg | 00-03 | 51 | Drake |
| spi | ch,ft,he,lg | 00-05 | 36 | Spider |
| sna | fa,he,lg | 00-03 | 36 | Snake |
| pum | ch,ft,he,lg | 00-03 | 36 | Puma/lion |
| ele | ch,fa,hn,lg,ua | 00-03 | 28 | Elemental |
| stg | ch,fa,ft,he,hn,lg,ua | 00-02 | 54 | Storm giant |
| fsg | ch,ft,he,hn,lg,ua | 00-02 | 45 | Frost giant |
| lyc | ch,fa,ft,he,hn,lg,ua | 00-01 | 50 | Lycanthrope |
| avi | ch,fa,ft,he,hn,lg,ua | 00-02 | 61 | Aviak |
| com | ch,fa,ft,he,hn,lg,ua | 00-02 | 45 | Combine soldier |
| cof | ch,ft,he,hn,lg,ua | 00-02 | 46 | Coldain |

## Shared Equipment Textures

These are NOT race-specific — they're referenced by model geometry for helmet/armor meshes.

### Chain Textures
chain01.bmp, chain02.bmp, chain04.bmp, chain05.bmp (in global_chr.s3d, global4_chr.s3d)

### Helm Textures
helm1, helm3, helm4, helm7, helm9, helm11-21, helm24 (in global_chr.s3d)
helmleather02-07 (in global_chr.s3d)
magecap1.bmp (in global_chr.s3d)

## Unified Atlas Feasibility (Option A)

For a unified atlas containing ALL body variants per race/gender:

### Playable Races
| Race | Body Textures (all variants, RGBA) | ETC1 Estimate | Fits In |
|------|-------------------------------------|---------------|---------|
| HUM | 618KB | 309KB | 1024x1024 |
| HUF | 690KB | 345KB | 1024x1024 |
| BAM | 1129KB | 565KB | 2048x1024 |
| DAM | 998KB | 499KB | 2048x512 |
| HIM | 700KB | 350KB | 1024x1024 |
| Most others | 400-600KB | 200-300KB | 1024x512 or 1024x1024 |
| Smallest (GNM) | 331KB | 166KB | 1024x512 |

### Key Constraints
- **Max sheet size**: 2048x2048 (4MB RGBA, 2MB ETC1) — all playable races fit
- **Orange Pi memory**: ~7.3MB ETC1 for all 24 playable race/genders
- **NPC races**: Most are small (single variant, few parts) — easily fit in 512x256 or smaller
- **Dragons** are the largest NPC (186 textures, 8 variants) — may need 2048x2048

### Current Builder Issues
1. Includes shared equipment textures (helm*, chain*, clk*) in every variant — wastes 30-50KB per sheet
2. Equipment textures are rendered via separate model add-on draw calls, not body texture swap
3. Robes need special body mesh variant handling, not just texture swap
4. Velious variants (17-23) stored in separate `global{N}_amr.s3d` archives — builder only scans `_chr.s3d`

## Source Archive Index

### Global Archives (always loaded)
| Archive | Contents |
|---------|----------|
| global_chr.s3d | 31 models, 1616 textures — all 12 playable races + common NPCs |
| global2_chr.s3d | Additional NPC models |
| global3_chr.s3d | More NPC models |
| global4_chr.s3d | BAM/BAF, IKM/IKF + additional models |
| global5_chr.s3d | Additional models |
| global6_chr.s3d | Additional models |
| global7_chr.s3d | Additional models |
| global17_amr.s3d | Velious Leather 1 (v17) for all playable races |
| global18_amr.s3d | Velious Chain 1 (v18) for all playable races |
| global19_amr.s3d | Velious Plate 1 (v19) for all playable races |
| global20_amr.s3d | Velious Leather 2 (v20) for all playable races |
| global21_amr.s3d | Velious Chain 2 (v21) for all playable races |
| global22_amr.s3d | Velious Plate 2 (v22) for all playable races |
| global23_amr.s3d | Velious Monk (v23) — HUM/HUF, IKM/IKF only |

### Zone Archives
NPC textures are scattered across zone-specific `{zone}_chr.s3d` archives.
A single NPC race may appear in 50+ zone archives (e.g., `gol` in 86+ archives).
