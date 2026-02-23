# EQ Zone Texture Categories for Detail System

Generated: 2026-01-20

This document categorizes ground textures from Classic, Kunark, and Velious zones
for the procedural detail object system.

## Surface Types for Detail Objects

The detail system uses these surface types to determine what objects to spawn:

| Surface Type | Detail Objects | Description |
|-------------|----------------|-------------|
| Grass | Grass blades, flowers, small plants | Green vegetated areas |
| Dirt | Rocks, pebbles, dead grass | Bare earth, paths |
| Sand | Shells, driftwood, beach grass | Beaches, deserts |
| Rock | Small stones, gravel, moss patches | Rocky terrain |
| Snow | Snowdrifts, ice crystals, frozen plants | Cold/winter areas |
| Swamp | Mushrooms, reeds, murky plants | Wetlands |
| Jungle | Ferns, tropical flowers, vines | Tropical forests |
| Lava | Embers, ash, charred debris | Volcanic areas |
| Water | (none - no detail on water) | Lakes, rivers, ocean |
| Stone | (none - artificial surfaces) | Floors, walls, buildings |

## Texture Pattern Analysis

### GRASS Textures (spawn grass detail)

**Common Patterns:**
- `*grass*` - grass.bmp, grass1.bmp, xgrass1.bmp, pgrass.bmp
- `*gras*` - kwgras1.bmp, nwgras1.bmp
- Classic: drockgrass*, hhdrockgrass* (grass on rocks)
- Kunark: firgrass01a.bmp, dredgrass.bmp, bwgrass.bmp
- Velious: (minimal grass - frozen zones)

**Texture List:**
```
grass.bmp, grass1.bmp, grass01.bmp, grass01a.bmp
xgrass1.bmp, xgrasdir.bmp
pgrass.bmp, pgrass2.bmp, pgrasstrans.bmp
sgrass.bmp, rgrass.bmp, fgrass.bmp
kwgras1.bmp, nwgras1.bmp, bwgrass.bmp
hhdrockgrass*.bmp (1-7)
drockgrass*.bmp (1-7)
grobbgrass.bmp
dirtygrass.bmp
firgrass01a.bmp
dredgrass.bmp
overgrass01a.bmp
```

### DIRT Textures (spawn rocks, pebbles)

**Common Patterns:**
- `*dirt*` - dirt.bmp, dirt01.bmp, dirt3und.bmp
- `*ground*` - topground2.bmp, ground.bmp
- `*mud*` - mud.bmp, mudmuck.bmp, drymud*.bmp
- Cave floors: cavedirt.bmp, cavedirt02.bmp

**Texture List:**
```
dirt.bmp, dirt01.bmp, dirt02.bmp, dirt03.bmp, dirt04.bmp
dirt1.bmp, dirt2.bmp, dirt3.bmp, dirt3und.bmp
1dirt.bmp, darkdirt.bmp, justdirt.bmp
topground2.bmp, ground.bmp, ground01a.bmp
mud.bmp, mudmuck.bmp, bloodmud.bmp
drymud1.bmp - drymud9.bmp
cavedirt.bmp, cavedirt02.bmp, cavedirt1.bmp, cavedirt2.bmp
dirtwood.bmp, dirtwood2.bmp, 1dirtwood.bmp, 1dirtwood2.bmp
dirtbones.bmp, dirtslm*.bmp
burrowdirt.bmp, burrowdirtfloor.bmp
dreddirt.bmp, dredmud.bmp
bwground1-4.bmp (Kunark)
```

### SAND Textures (spawn shells, beach debris)

**Common Patterns:**
- `*sand*` - sand.bmp, sand3.bmp, sandtrans.bmp
- Beach specific: cbsand.bmp, kerrasand02.bmp
- Stonebrunt: sb*sand*.bmp

**Texture List:**
```
sand.bmp, sand3.bmp, sandtrans.bmp
cbsand.bmp, kerrasand02.bmp
rocksand02.bmp
sandbewall.bmp, sandbefloor.bmp
sbsand01.bmp, sbrivsand*.bmp, sbjungsand*.bmp
```

### ROCK Textures (spawn small stones, gravel)

**Common Patterns:**
- `*rock*` - rock.bmp, rockwall.bmp, hhdrock.bmp
- Mountain: permrock3.bmp, frnmtrock*.bmp
- Cave: gukroundrock1.bmp, cavewall1.bmp

**Texture List:**
```
rock.bmp, rockw2.bmp, rockwall.bmp
hhdrock.bmp, bhdrock.bmp, bhdrock2.bmp
drock.bmp, rrock.bmp
permrock3.bmp
frnmtrockb.bmp, frnmtrockd.bmp
newrock02.bmp
ejcliff1.bmp
dreadrock01a.bmp
gukstone1.bmp, gukroundrock1.bmp
brownrock.bmp, browndarkrock.bmp
verock2a.bmp
```

### SNOW Textures (spawn ice crystals, frozen debris)

**Common Patterns:**
- `*snow*` - snow_flo.bmp, snowfloor.bmp, icsnow1.bmp
- `*ice*` - ice.bmp, ice3.bmp, wice*.bmp
- `*frost*` - frostedwalls.bmp, frostyramps.bmp
- Great Divide: gdrocksnow*.bmp

**Texture List (Velious-specific):**
```
snow_flo.bmp, snowfloor.bmp, snowb.bmp, snowbasic.bmp, snowbrik.bmp
icsnow1.bmp, ice3sno.bmp
ice.bmp, ice3.bmp, iced.bmp, iced3.bmp
wice1-4.bmp
icerockv1.bmp, icrock.bmp
cavesnowwal1.bmp, cavesnowwal3.bmp
dredsnow.bmp
gdrocksnow1a-d.bmp, gdrocksnow2a-d.bmp, gdrocksnow3a-d.bmp, gdrocksnow4a-d.bmp
blumarbice.bmp, brownmarbice.bmp, bubbleice.bmp
bluicetile.bmp
caveiceceil.bmp, caveicewall2.bmp
```

### SWAMP Textures (spawn mushrooms, reeds)

**Common Patterns:**
- `*swamp*` - swampgrnd.bmp
- `*muck*` - mudmuck.bmp
- `*slime*` - slime1-4.bmp, slimewall*.bmp
- Innothule/Feerrott specific textures

**Texture List:**
```
swampgrnd.bmp
mudmuck.bmp
slime1.bmp, slime2.bmp, slime3.bmp, slime4.bmp
slimewall*.bmp, slimewl*.bmp
slimetile.bmp, slimestone*.bmp
```

### JUNGLE Textures (spawn ferns, tropical plants)

**Common Patterns:**
- Kunark-specific: jungle*.bmp, fern*.bmp
- Emerald Jungle: ej*.bmp
- Burning Woods: bw*.bmp ground textures

**Texture List:**
```
junglefloor.bmp
firgrass01a.bmp (Firiona Vie)
ejcliff1.bmp (Emerald Jungle)
grassblades01.bmp
overgrass01a.bmp (Overthere)
sbjung*.bmp (Stonebrunt jungle variants)
```

### LAVA Textures (spawn embers, ash)

**Common Patterns:**
- `*lava*` - lava001-005.bmp, veelava1-3.bmp, slava.bmp
- Lavastorm, Sol A/B, Nagafen's Lair

**Texture List:**
```
lava001.bmp - lava005.bmp
veelava1.bmp, veelava2.bmp, veelava3.bmp
slava.bmp
```

### NON-SPAWNING Textures (artificial surfaces)

**Patterns to EXCLUDE from detail spawning:**
- `*wall*` - building/dungeon walls
- `*floor*` (indoor) - tile, brick, wood floors
- `*brick*`, `*tile*`, `*cobble*`, `*coble*`
- `*wood*`, `*plank*`, `*beam*`
- `*roof*`, `*ceil*`
- `*door*`, `*window*`
- Water: `agua*`, `falls*`, `water*`, `w1-4.bmp`

## Cross-Expansion Analysis

### Shared Textures (appear in 5+ zones)

| Texture | Count | Category |
|---------|-------|----------|
| grass1.bmp | 10 | Grass |
| xgrass1.bmp | 8 | Grass |
| dirt.bmp | 9 | Dirt |
| rock.bmp | 9 | Rock |
| sand.bmp | 7 | Sand |
| mud.bmp | 6 | Dirt |

### Expansion-Specific Patterns

**Classic (Antonica/Faydwer/Odus):**
- Prefix patterns: numbered (10dirt, 16wall), zone-specific (qey, free, gfay)
- Ground types: temperate grass, dirt, rock
- Unique: British-style green grasslands

**Kunark:**
- Prefix patterns: kur*, cab*, dro*, kae*, rkm*
- Ground types: jungle, volcanic ash, ruins
- Unique: jungle floors, ash/burning ground, ancient stone

**Velious:**
- Prefix patterns: ice*, snow*, gdr*, vel*, thu*
- Ground types: snow, ice, frozen rock
- Unique: ice variants, snow-rock blends, crystal floors

### Outliers (zone-specific, appear 1-2 times)

- Plane of Fear: fear-specific demonic textures
- Plane of Hate: hate-specific textures
- Kedge Keep: underwater-specific
- Sleeper's Tomb: unique crystal/ice variants
- Veeshan's Peak: dragon-themed

## Recommendations

1. **Core Categories (implement first):**
   - Grass (temperate zones)
   - Dirt (paths, caves)
   - Snow (Velious)
   - Rock (mountains, caves)

2. **Secondary Categories (add later):**
   - Sand (beaches, desert)
   - Swamp (Innothule, Feerrott, Kunark swamps)
   - Jungle (Kunark outdoor)
   - Lava (volcanic zones)

3. **Pattern Matching Strategy:**
   - Use substring matching: `*grass*`, `*dirt*`, etc.
   - Zone prefixes as hints: `kur*` = Kunark ruins style
   - Exclusion list for walls/floors/water

4. **Zone Override Files:**
   - Create per-zone override files for outliers
   - Allow explicit texture → surface type mapping
   - Handle unique textures that don't match patterns
