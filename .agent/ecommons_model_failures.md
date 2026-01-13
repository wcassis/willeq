# East Commonlands (ecommons) Model/Animation Failures

Log file: `output.log`
Date: 2026-01-11 20:52
Zone: ecommons (zone_id=22)

## Summary

- **12 race IDs** have missing models (entities rendered as placeholder cubes)
- **3 door mesh types** are missing (rendered as placeholders)
- **The models EXIST in the S3D files** - the issue is incorrect race ID to model code mappings

---

## Root Cause: Incorrect Race ID Mappings

The `src/client/graphics/eq/race_codes.cpp` file has **incorrect mappings** for many race IDs. The models exist in the zone chr files, but the code is looking for the wrong model codes.

### Current (WRONG) Mappings vs Required Mappings

| Race ID | Entity Names | Current Code | Correct Code | S3D Location |
|---------|--------------|--------------|--------------|--------------|
| 22 | Fire Beetle | BET | BET | ecommons_chr.s3d (textures exist) |
| 33 | Ghoul | GHO (Ghost!) | GHU | ecommons_chr.s3d, commons_chr.s3d |
| 37 | Moss Snake | SNA | SNA | ecommons_chr.s3d |
| 42 | Wolf | RAT (wrong!) | WOL | ecommons_chr.s3d, wof_chr.s3d |
| 43 | Bear | BAT (wrong!) | BEA | ecommons_chr.s3d, commons_chr.s3d |
| 44 | Guards | GNN (wrong!) | QCM/QCF or FPM | ecommons_chr.s3d |
| 47 | Griffin | COR (wrong!) | GRI | ecommons_chr.s3d, gri_chr.s3d |
| 50 | Lion | DEE (wrong!) | LIM/LIF | ecommons_chr.s3d |
| 54 | Orc | WAR (wrong!) | ORC | ecommons_chr.s3d, commons_chr.s3d |
| 69 | Will-o-wisp | ARM (wrong!) | WIL | ecommons_chr.s3d |
| 70 | Zombie | CRE (wrong!) | ZOM | nektulos_chr.s3d |
| 75 | Air Elemental | FIR (wrong!) | ELE | global_chr.s3d |
| 76 | Cat/Puma | WEP (wrong!) | PUM | ecommons_chr.s3d, pum_chr.s3d |

---

## Model Codes Found in Classic Zone CHR Files

These model codes are confirmed to exist in pre-Luclin S3D files:

### Animals
| Code | Model | Found In |
|------|-------|----------|
| BEA | Bear | ecommons_chr.s3d, commons_chr.s3d + 13 others |
| WOL | Wolf | ecommons_chr.s3d, nektulos_chr.s3d + 15 others |
| WOF | Wolf (standalone) | wof_chr.s3d, global6_chr.s3d |
| WUF | Wolf (female?) | wuf_chr.s3d standalone |
| PUM | Puma | ecommons_chr.s3d, commons_chr.s3d + 5 others |
| LIF | Lion Female | ecommons_chr.s3d + 3 others |
| LIM | Lion Male | ecommons_chr.s3d, qeynos_chr.s3d + 4 others |
| TIG | Tiger | feerrott_chr.s3d, cazicthule_chr.s3d |
| BAT | Bat | qeynos2_chr.s3d + 14 others |
| RAT | Rat | qeynos_chr.s3d + 20 others |
| SNA | Snake | ecommons_chr.s3d + 16 others |
| GOR | Gorilla | qeynos_chr.s3d + 3 others |
| ALL | Alligator | crushbone_chr.s3d + 12 others |

### Monsters
| Code | Model | Found In |
|------|-------|----------|
| ORC | Orc | ecommons_chr.s3d, commons_chr.s3d + 23 others |
| GOB | Goblin | freportw_chr.s3d + 15 others |
| GNN | Gnoll | qeynos_chr.s3d + 8 others |
| GHU | Ghoul | ecommons_chr.s3d + 12 others |
| ZOM | Zombie | nektulos_chr.s3d + 11 others |
| ZOF | Zombie (female?) | nektulos_chr.s3d + 6 others |
| SKE | Skeleton | global_chr.s3d, global6_chr.s3d |
| SPE | Spectre | unrest_chr.s3d + 5 others |
| IMP | Imp | nektulos_chr.s3d + 16 others |
| LIZ | Lizard Man | innothule_chr.s3d + 4 others |
| KOB | Kobold | crushbone_chr.s3d + 7 others |
| WER | Werewolf | global_chr.s3d |
| MIN | Minotaur | qeynos_chr.s3d + 7 others |
| FRO | Froglok | innothule_chr.s3d + 5 others |
| TRE | Treant | gfaydark_chr.s3d + 4 others |
| GIA | Giant | commons_chr.s3d + 8 others |

### Creatures
| Code | Model | Found In |
|------|-------|----------|
| GRI | Griffin | ecommons_chr.s3d + 3 others, gri_chr.s3d |
| BET | Beetle | ecommons_chr.s3d + 14 others |
| SPI | Spider | ecommons_chr.s3d + 22 others |
| WIL | Will-o-wisp | ecommons_chr.s3d + 8 others |
| ELE | Elemental | global_chr.s3d |
| SCA | Scarecrow | unrest_chr.s3d, rivervale_chr.s3d, global2_chr.s3d |
| GOL | Golem | nektulos_chr.s3d + 16 others |
| FIS | Fish | qeynos_chr.s3d + 14 others |

### NPCs/Citizens
| Code | Model | Found In |
|------|-------|----------|
| QCM | Qeynos Citizen Male | ecommons_chr.s3d + 15 others |
| QCF | Qeynos Citizen Female | ecommons_chr.s3d + 15 others |
| FPM | Freeport Citizen Male | ecommons_chr.s3d + 7 others |
| HLM | Highpass Male | everfrost_chr.s3d, halas_chr.s3d |
| HLF | Highpass Female | everfrost_chr.s3d, halas_chr.s3d |
| RIM/RIF | Rivervale Citizen | rivervale_chr.s3d, misty_chr.s3d |

### Standalone Creature Files (pre-Luclin)
These are 3-letter code standalone files that may contain classic models:
- `wof_chr.s3d` - Wolf
- `wuf_chr.s3d` - Wolf (variant)
- `gri_chr.s3d` - Griffin
- `pum_chr.s3d` - Puma
- `goo_chr.s3d` - Goo/Slime
- `efe_chr.s3d` - Fire Elemental
- `bon_chr.s3d` - Bone creature
- `kob_chr.s3d` - Kobold

---

## Missing Door Meshes

These door/object mesh names were not found in zone geometry:

| Door Name | Occurrences |
|-----------|-------------|
| BUCKET1 | 2 |
| CART | 3 |
| CRATEE | 1 |

---

## Required Code Changes

### 1. Fix Race ID Mappings in `race_codes.cpp`

The following race ID mappings need to be corrected:

```cpp
// Current WRONG mappings that need fixing:
case 33:  return "GHU";  // Ghoul (NOT "GHO" which is Ghost)
case 42:  return "WOL";  // Wolf (NOT "RAT")
case 43:  return "BEA";  // Bear (NOT "BAT")
case 44:  return "QCM";  // Guards use citizen models (or GNN for gnoll guards)
case 47:  return "GRI";  // Griffin (NOT "COR")
case 50:  return "LIM";  // Lion Male (NOT "DEE")
case 54:  return "ORC";  // Orc (NOT "WAR")
case 69:  return "WIL";  // Will-o-wisp (NOT "ARM")
case 70:  return "ZOM";  // Zombie (NOT "CRE")
case 75:  return "ELE";  // Air Elemental (NOT "FIR")
case 76:  return "PUM";  // Puma/Cat (NOT "WEP")
```

### 2. Add Zone-Specific Model Loading

The loader should search:
1. Current zone's `_chr.s3d` file first (e.g., `ecommons_chr.s3d`)
2. Standalone creature files (e.g., `wof_chr.s3d` for wolves)
3. `global_chr.s3d` and `global2-7_chr.s3d` files
4. Other classic zone chr files as fallback

### 3. Add Standalone Creature File Support

Add support for loading models from standalone 3-letter creature files:
- `wof_chr.s3d`, `wuf_chr.s3d` for wolves
- `gri_chr.s3d` for griffins
- `pum_chr.s3d` for pumas
- etc.

---

## Files to Modify

1. `src/client/graphics/eq/race_codes.cpp` - Fix race ID to model code mappings
2. `src/client/graphics/eq/race_model_loader.cpp` - Add standalone creature file search
3. `src/client/graphics/eq/model_loading.cpp` - Update search order for classic models

---

## Notes

- All models use pre-Luclin (classic) format stored in `_chr.s3d` files
- Zone chr files contain zone-specific NPCs and creatures
- Standalone chr files (3-letter codes) contain specific creature types
- Textures follow pattern: `XXXch`, `XXXft`, `XXXhe`, `XXXlg`, `XXXhn`, `XXXfa`, `XXXua`
- WLD files inside S3D archives define the actual mesh/skeleton data
