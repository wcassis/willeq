# EverQuest Titanium Audio Assets Catalog

## Overview

This document catalogs all audio assets in the EverQuest Titanium client (P1999 edition), focusing on Classic EQ, Kunark, and Velious content. The goal is to map these assets to game events for implementation in WillEQ.

## Audio File Summary

| Category | Count | Format | Location |
|----------|-------|--------|----------|
| Zone Music (Classic/Kunark/Velious) | 82 | XMI (MIDI variant) | Root directory |
| Zone Music (Later Expansions) | 78 | MP3 | Root directory |
| Sound Effects | 2,128 | WAV | snd1-17.pfs archives |
| Zone Sound Configs | 329 | EFF (binary) | Root directory |
| Sound ID Mappings | 2,166+ entries | Text | SoundAssets.txt |

---

## 1. Zone Music Files (XMI Format)

XMI files are EverQuest's MIDI variant format used for zone music in Classic, Kunark, and Velious.

### Classic EverQuest Zones

| File | Zone Name | Era |
|------|-----------|-----|
| akanon.xmi | Ak'Anon (Gnome city) | Classic |
| befallen.xmi | Befallen | Classic |
| blackburrow.xmi | Blackburrow | Classic |
| butcher.xmi | Butcherblock Mountains | Classic |
| cauldron.xmi | Dagnor's Cauldron | Classic |
| eastkarana.xmi | East Karana | Classic |
| erudnext.xmi | Erudin | Classic |
| erudnint.xmi | Erudin Palace | Classic |
| erudsxing.xmi | Erud's Crossing | Classic |
| felwithea.xmi | Felwithe | Classic |
| freporte.xmi | East Freeport | Classic |
| freportn.xmi | North Freeport | Classic |
| freportw.xmi | West Freeport | Classic |
| gfaydark.xmi | Greater Faydark | Classic |
| grobb.xmi | Grobb | Classic |
| gukbottom.xmi | Lower Guk | Classic |
| guktop.xmi | Upper Guk | Classic |
| halas.xmi | Halas | Classic |
| highkeep.xmi | High Keep | Classic |
| highpass.xmi | Highpass Hold | Classic |
| innothule.xmi | Innothule Swamp | Classic |
| kaladima.xmi | North Kaladim | Classic |
| kaladimb.xmi | South Kaladim | Classic |
| kithicor.xmi | Kithicor Forest | Classic |
| lavastorm.xmi | Lavastorm Mountains | Classic |
| lfaydark.xmi | Lesser Faydark | Classic |
| mistmoore.xmi | Castle Mistmoore | Classic |
| najena.xmi | Najena | Classic |
| nektulos.xmi | Nektulos Forest | Classic |
| neriaka.xmi | Neriak Foreign Quarter | Classic |
| neriakb.xmi | Neriak Commons | Classic |
| neriakc.xmi | Neriak Third Gate | Classic |
| northkarana.xmi | North Karana | Classic |
| oggok.xmi | Oggok | Classic |
| paineel.xmi | Paineel | Classic |
| paw.xmi | Infected Paw | Classic |
| permafrost.xmi | Permafrost Keep | Classic |
| qcat.xmi | Qeynos Catacombs | Classic |
| qey2hh1.xmi | Qeynos Hills | Classic |
| qeynos.xmi | Qeynos | Classic |
| qeynos2.xmi | South Qeynos | Classic |
| qeytoqrg.xmi | Qeynos-Surefall route | Classic |
| qrg.xmi | Surefall Glade | Classic |
| rathemtn.xmi | Rathe Mountains | Classic |
| rivervale.xmi | Rivervale | Classic |
| runnyeye.xmi | Runnyeye Citadel | Classic |
| soldunga.xmi | Solusek's Eye (A) | Classic |
| soldungb.xmi | Nagafen's Lair | Classic |
| southkarana.xmi | South Karana | Classic |
| steamfont.xmi | Steamfont Mountains | Classic |
| unrest.xmi | Estate of Unrest | Classic |

### Kunark Expansion Zones

| File | Zone Name | Era |
|------|-----------|-----|
| burningwood.xmi | Burning Woods | Kunark |
| cabeast.xmi | Cabilis East | Kunark |
| cabwest.xmi | Cabilis West | Kunark |
| charasis.xmi | Howling Stones | Kunark |
| chardok.xmi | Chardok | Kunark |
| citymist.xmi | City of Mist | Kunark |
| dalnir.xmi | Crypt of Dalnir | Kunark |
| dreadlands.xmi | Dreadlands | Kunark |
| droga.xmi | Temple of Droga | Kunark |
| emeraldjungle.xmi | Emerald Jungle | Kunark |
| fieldofbone.xmi | Field of Bone | Kunark |
| firiona.xmi | Firiona Vie | Kunark |
| frontiermtns.xmi | Frontier Mountains | Kunark |
| kaesora.xmi | Kaesora | Kunark |
| karnor.xmi | Karnor's Castle | Kunark |
| kurn.xmi | Kurn's Tower | Kunark |
| lakeofillomen.xmi | Lake of Ill Omen | Kunark |
| nurga.xmi | Mines of Nurga | Kunark |
| overthere.xmi | Overthere | Kunark |
| sebilis.xmi | Old Sebilis | Kunark |
| skyfire.xmi | Skyfire Mountains | Kunark |
| swampofnohope.xmi | Swamp of No Hope | Kunark |
| timorous.xmi | Timorous Deep | Kunark |
| trakanon.xmi | Trakanon's Teeth | Kunark |
| veeshan.xmi | Veeshan's Peak | Kunark |
| warslikswood.xmi | Warsliks Woods | Kunark |

### Velious Expansion Zones

| File | Zone Name | Era |
|------|-----------|-----|
| cobaltscar.xmi | Cobalt Scar | Velious |
| crystal.xmi | Crystal Caverns | Velious |
| eastwastes.xmi | Eastern Wastes | Velious |
| everfrost.xmi | Everfrost Peaks | Classic/Velious |
| frozenshadow.xmi | Tower of Frozen Shadow | Velious |
| greatdivide.xmi | Great Divide | Velious |
| growthplane.xmi | Plane of Growth | Velious |
| iceclad.xmi | Iceclad Ocean | Velious |
| kael.xmi | Kael Drakkel | Velious |
| mischiefplane.xmi | Plane of Mischief | Velious |
| necropolis.xmi | Dragon Necropolis | Velious |
| sirens.xmi | Siren's Grotto | Velious |
| skyshrine.xmi | Skyshrine | Velious |
| sleeper.xmi | Sleeper's Tomb | Velious |
| templeveeshan.xmi | Temple of Veeshan | Velious |
| thurgadina.xmi | Thurgadin | Velious |
| thurgadinb.xmi | Icewell Keep | Velious |
| velketor.xmi | Velketor's Labyrinth | Velious |
| wakening.xmi | Wakening Land | Velious |
| westwastes.xmi | Western Wastes | Velious |

### Special/UI Music

| File | Purpose |
|------|---------|
| opener.xmi | Login screen music 1 |
| opener2.xmi | Login screen music 2 |
| opener3.xmi | Login screen music 3 |
| opener4.xmi | Login screen music 4 |
| pickchar.xmi | Character select screen |
| damage1.xmi | Combat/damage stinger 1 |
| damage2.xmi | Combat/damage stinger 2 |
| eerie.xmi | Generic eerie/dungeon |
| echo.xmi | Echo Caverns |

---

## 2. Sound Effect Archives (snd*.pfs)

Sound effects are stored in 17 PFS archives containing 2,128 WAV files total.

### Archive Contents Summary

| Archive | Files | Size | Primary Content |
|---------|-------|------|-----------------|
| snd1.pfs | 73 | 915 KB | Player sounds (death, jump, gasp, gethit), doors, UI |
| snd2.pfs | 69 | 2.2 MB | Combat sounds (weapons, impacts, spells) |
| snd3.pfs | 162 | 3.8 MB | Creature sounds (A-D: amadamnator, bat, bear, etc.) |
| snd4.pfs | 130 | 3.0 MB | Creature sounds (I-M: insects, kobolds, lions, etc.) |
| snd5.pfs | 115 | 3.4 MB | Creature idle/movement, bat, bear, beetle loops |
| snd6.pfs | 63 | 3.7 MB | Environment (boat, bells, water, wind loops) |
| snd7.pfs | 43 | 540 KB | Additional player/door sounds (variant set) |
| snd8.pfs | 51 | 3.0 MB | Ambient loops (arena, bubbles, caves, fire) |
| snd9.pfs | 139 | 4.8 MB | More creature sounds (bear, beetle, beholder, etc.) |
| snd10.pfs | 244 | 7.2 MB | Extended creature library (ael, akh, alligator, etc.) |
| snd11.pfs | 221 | 13 MB | Ape, aqua goblin, bird, bug distant sounds |
| snd12.pfs | 19 | 341 KB | Kef (Kedge) creature sounds |
| snd13.pfs | 559 | 44 MB | Massive creature library (aam through various races) |
| snd14.pfs | 127 | 25 MB | Animal, ambient, environmental sounds |
| snd15.pfs | 21 | 491 KB | Frf (Froglok) creature sounds |
| snd16.pfs | 83 | 3.1 MB | Dpf creatures, dragon variants |
| snd17.pfs | 9 | 2.4 MB | Blacksmith loop, fly buzz, misc ambient |

### Sound Categories in Archives

#### Player Character Sounds (snd1.pfs, snd7.pfs)

Gender and race variants with suffixes:
- `_f` = Female
- `_m` = Male
- `_fb` = Female Barbarian
- `_mb` = Male Barbarian
- `_fl` = Female Light (Elf/Halfling)
- `_ml` = Male Light (Elf/Halfling)

| Sound Type | Files | Example |
|------------|-------|---------|
| Death | 12 | death_f.wav, death_m.wav, death_fb.wav, etc. |
| Drowning | 12 | drown_f.wav, drown_m.wav, etc. |
| Jump/Land | 12 | jumpf_1.wav, jumpm_1.wav, etc. |
| Get Hit | 48 | gethit1f.wav through gethit4ml.wav |
| Gasp (breath) | 12 | gasp1f.wav, gasp2m.wav, etc. |

#### Combat Sounds (snd2.pfs)

| Sound | File | Description |
|-------|------|-------------|
| Arrow Hit | arrowhit.wav | Projectile impact |
| Shield Bash | bashshld.wav | Shield attack |
| Bow Draw | bowdraw.wav | Bow string pull |
| Club Hit | club.wav | Blunt weapon impact |
| Kick | kick1.wav, kickhit.wav | Kick attacks |
| Punch | punch1.wav | Unarmed attack |
| Sword Block | swordblk.wav | Parry/block |
| Swing | swing.wav | Weapon swing miss |
| Stab | stab.wav | Piercing attack |
| Impale | impale.wav | Critical pierce |

#### Spell Sounds (snd2.pfs)

| Sound | File | Description |
|-------|------|-------------|
| Spell Cast | spelcast.wav | Generic casting |
| Spell 1-5 | spell_1.wav through spell_5.wav | Spell effect variants |

#### Door/Object Sounds (snd1.pfs)

| Sound | File | Description |
|-------|------|-------------|
| Metal Door Close | doormt_c.wav | Metal door closing |
| Metal Door Open | doormt_o.wav | Metal door opening |
| Stone Door Close | doorst_c.wav | Stone door closing |
| Stone Door Open | doorst_o.wav | Stone door opening |
| Secret Door | doorsecr.wav | Hidden passage |
| Sliding Door Close | sldorstc.wav | Sliding mechanism |
| Sliding Door Open | sldorsto.wav | Sliding mechanism |
| Lever | lever.wav | Lever activation |
| Spear Trap Down | speardn.wav | Trap mechanism |
| Spear Trap Up | spearup.wav | Trap mechanism |
| Trap Door | trapdoor.wav | Trap door sound |

#### Environment Sounds (snd6.pfs, snd8.pfs, snd14.pfs, snd17.pfs)

| Sound | File | Description |
|-------|------|-------------|
| Boat Bell | boatbell.wav | Ship bell |
| Boat Loop | boatloop.wav | Sailing ambient |
| Big Bell | bigbell.wav | Town bell |
| Wind Loop | wind_lp1.wav | Wind ambient |
| Rain Loop | rainloop.wav | Rain ambient |
| Water In | waterin.wav | Enter water |
| Water Tread | wattrd_1.wav, wattrd_2.wav | Swimming |
| Drawbridge Loop | dbrdg_lp.wav | Drawbridge mechanism |
| Elevator Loop | elevloop.wav | Lift/elevator |
| Portcullis Loop | portc_lp.wav | Gate mechanism |
| Blacksmith Loop | blacksmith_lp.wav | Forge ambient |
| Fly Buzz | fly_buzz_nearby.wav | Insect ambient |
| Arena Background | arena_bg_lp.wav | Arena ambient |
| Bubble Brew | bubble_brew.wav | Cauldron/potion |

#### Creature Sound Naming Convention

Creature sounds follow a consistent pattern:
- `{creature}_atk.wav` or `{creature}_att.wav` - Attack sound
- `{creature}_dam.wav` - Taking damage
- `{creature}_dth.wav` or `{creature}_die.wav` - Death sound
- `{creature}_idl.wav` - Idle/ambient
- `{creature}_spl.wav` - Special attack/spell
- `{creature}_sat.wav` - Special attack variant
- `{creature}_run.wav` - Running/movement
- `{creature}_wlk.wav` - Walking

---

## 3. SoundAssets.txt Mapping

The `SoundAssets.txt` file maps numeric Sound IDs to WAV filenames. Format:
```
SoundID^Volume^Filename.wav
```

### Key Sound ID Ranges

| ID Range | Category | Notes |
|----------|----------|-------|
| 1-99 | Reserved/Unknown | Minimal usage |
| 100-170 | Core Gameplay | Water, spells, UI, combat basics |
| 170-189 | Objects/Mechanisms | Doors, levers, traps |
| 190-999 | Classic Creatures | Original EQ monsters |
| 1000-1299 | Humanoid Races | Player race attack/damage/death |
| 1300-1999 | Extended NPCs | Expansion creatures |
| 2000-2999 | Additional Creatures | More expansion content |
| 3000-3584 | Movement/Idle | Walk loops, run loops, idle variants |

### Important Sound IDs

| ID | File | Purpose |
|----|------|---------|
| 100 | WaterIn.WAV | Enter water |
| 101-102 | WatTrd_1/2.WAV | Swimming |
| 103-107 | Spell_1-5.wav | Spell effects |
| 108 | SpelCast.WAV | Spell casting |
| 114 | jumpland.WAV | Landing |
| 116 | BowDraw.WAV | Bow draw |
| 118 | Swing.WAV | Weapon swing |
| 120 | SwordBlk.WAV | Block/parry |
| 121-123 | Impale/Stab/Club.WAV | Weapon hits |
| 124 | BashShld.WAV | Shield bash |
| 126-127 | Kick1/KickHit.WAV | Kicks |
| 131 | Punch1.WAV | Punch |
| 139 | LevelUp.WAV | Level gained |
| 151 | StepWlk2.WAV | Footstep |
| 158 | wind_lp1.wav | Wind ambient |
| 159 | rainloop.wav | Rain ambient |
| 170 | BoatBell.WAV | Ship bell |
| 175-176 | DoorMt_C/O.WAV | Metal doors |
| 177 | DoorSecr.WAV | Secret door |
| 180 | Lever.WAV | Lever |
| 187-188 | SpearDn/Up.WAV | Spear trap |
| 189 | TrapDoor.WAV | Trap door |

---

## 4. Zone Sound Effect Files (EFF Format)

EFF files define sound emitters/regions within zones. Two types exist:
- `{zone}_sounds.eff` - Full sound configuration (420 bytes to 8.4 KB)
- `{zone}_sndbnk.eff` - Sound bank references (90-400 bytes, present for some zones)

### EFF File Structure (Preliminary Analysis)

EFF files appear to contain:
- Sound emitter positions (X, Y, Z coordinates as floats)
- Sound ID references
- Radius/volume parameters
- Loop/trigger flags

The binary format includes:
- 4-byte header/magic
- Repeating records of ~84 bytes each containing:
  - Position data (3 floats)
  - Sound parameters
  - Trigger conditions

### Zones with Sound Effects

Classic zones with `_sounds.eff` files:
- airplane, akanon, befallen, blackburrow, butcher, cauldron
- cazicthule, commons, crushbone, erudnext, erudnint, erudsxing
- felwithea, felwitheb, freporte, freportn, freportw
- fungusgrove, gfaydark, grobb, halas, innothule, kithicor
- kaladima, kaladimb, lavastorm, lfaydark, mistmoore, misty
- najena, nektulos, neriaka, neriakb, neriakc, northkarana
- oasis, paineel, qeynos, qeynos2, qey2hh1, qeytoqrg
- rivervale, runnyeye, southkarana, steamfont, unrest

Kunark zones with `_sounds.eff` files:
- burningwood, cabeast, cabwest, charasis, chardok, citymist
- dalnir, dreadlands, droga, emeraldjungle, fieldofbone
- firiona, frontiermtns, kaesora, karnor, kurn, lakeofillomen
- nurga, overthere, sebilis, skyfire, swampofnohope
- timorous, trakanon, warslikswood

Velious zones with `_sounds.eff` files:
- cobaltscar, crystal, dawnshroud, eastwastes, everfrost
- frozenshadow, greatdivide, griegsend, growthplane, iceclad
- kael, letalis, mischiefplane, necropolis, permafrost
- shadeweaver, shadowhaven, sharvahl, sirens, skyshrine
- sleeper, stonebrunt, templeveeshan, thurgadina, thurgadinb
- velketor, wakening, westwastes

---

## 5. Implementation Priority for WillEQ

### Phase 1: Core Audio (Currently Implemented)
- [x] Zone music playback (XMI via FluidSynth)
- [x] Basic sound effects (combat, spells)
- [x] Sound ID to filename mapping

### Phase 2: Player Sounds
- [ ] Death sounds (gender/race variants)
- [ ] Jump/land sounds
- [ ] Hit reaction sounds
- [ ] Drowning sounds

### Phase 3: Environment
- [ ] Door sounds (open/close by type)
- [ ] Water enter/exit
- [ ] Ambient loops (wind, rain)
- [ ] Object interactions (levers, traps)

### Phase 4: Zone Sound Emitters
- [ ] Parse EFF files for sound positions
- [ ] Implement positional ambient sounds
- [ ] Area-based music transitions (if any)

### Phase 5: UI Sounds
- [ ] Vendor window music (needs investigation)
- [ ] Level up sound
- [ ] Other UI feedback sounds

---

## 6. Files Needing Further Investigation

1. **EFF File Format**: Need to fully reverse-engineer the binary format to understand:
   - How sound emitters are positioned
   - Trigger conditions (proximity, time, events)
   - Volume/radius parameters

2. **Vendor Music**: The original client plays specific music when vendor windows open. Need to determine:
   - Is this client-side logic?
   - Which music file is used?
   - Trigger mechanism

3. **Combat Music**: The damage1.xmi and damage2.xmi files suggest combat music stingers. Need to understand:
   - When these are triggered
   - How they layer with zone music

4. **Area-Based Music**: Some zones may have different music in different areas. Need to investigate:
   - Is this defined in EFF files?
   - Or is it purely client-side based on coordinates?

---

## Appendix: Complete File Lists

### All XMI Files (82 total)
```
acrylia.xmi, akanon.xmi, befallen.xmi, blackburrow.xmi, burningwood.xmi,
butcher.xmi, cabeast.xmi, cabwest.xmi, cauldron.xmi, charasis.xmi,
chardok.xmi, citymist.xmi, cobaltscar.xmi, crystal.xmi, damage1.xmi,
damage2.xmi, dalnir.xmi, dawnshroud.xmi, dreadlands.xmi, droga.xmi,
eastkarana.xmi, eastwastes.xmi, echo.xmi, eerie.xmi, emeraldjungle.xmi,
erudnext.xmi, erudnint.xmi, erudsxing.xmi, everfrost.xmi, felwithea.xmi,
fieldofbone.xmi, firiona.xmi, freporte.xmi, freportn.xmi, freportw.xmi,
frontiermtns.xmi, frozenshadow.xmi, fungusgrove.xmi, gfaydark.xmi,
greatdivide.xmi, grobb.xmi, growthplane.xmi, gukbottom.xmi, guktop.xmi,
halas.xmi, highkeep.xmi, highpass.xmi, iceclad.xmi, innothule.xmi,
kael.xmi, kaesora.xmi, kaladima.xmi, kaladimb.xmi, karnor.xmi,
kithicor.xmi, kurn.xmi, lakeofillomen.xmi, lavastorm.xmi, lfaydark.xmi,
mischiefplane.xmi, mistmoore.xmi, najena.xmi, necropolis.xmi, nektulos.xmi,
neriaka.xmi, neriakb.xmi, neriakc.xmi, northkarana.xmi, nurga.xmi,
oggok.xmi, opener.xmi, opener2.xmi, opener3.xmi, opener4.xmi,
overthere.xmi, paineel.xmi, paw.xmi, permafrost.xmi, pickchar.xmi,
qcat.xmi, qey2hh1.xmi, qeynos.xmi, qeynos2.xmi, qeytoqrg.xmi, qrg.xmi,
rathemtn.xmi, rivervale.xmi, runnyeye.xmi, sebilis.xmi, sirens.xmi,
skyfire.xmi, skyshrine.xmi, sleeper.xmi, soldunga.xmi, soldungb.xmi,
southkarana.xmi, steamfont.xmi, swampofnohope.xmi, templeveeshan.xmi,
thurgadina.xmi, thurgadinb.xmi, timorous.xmi, trakanon.xmi, unrest.xmi,
veeshan.xmi, velketor.xmi, wakening.xmi, warslikswood.xmi, westwastes.xmi
```

### snd1.pfs Contents (Player/UI Sounds)
```
boatbell.wav, crshwall.wav, dartshot.wav, dbrdg_lp.wav, dbrdgstp.wav,
death_f.wav, death_fb.wav, death_fl.wav, death_m.wav, death_mb.wav,
death_ml.wav, doormt_c.wav, doormt_o.wav, doorsecr.wav, doorst_c.wav,
doorst_o.wav, drown_f.wav, drown_fb.wav, drown_fl.wav, drown_m.wav,
drown_mb.wav, drown_ml.wav, elevloop.wav, gasp1f.wav, gasp1fb.wav,
gasp1fl.wav, gasp1m.wav, gasp1mb.wav, gasp1ml.wav, gasp2f.wav,
gasp2fb.wav, gasp2fl.wav, gasp2m.wav, gasp2mb.wav, gasp2ml.wav,
gethit1f.wav, gethit1fb.wav, gethit1fl.wav, gethit1m.wav, gethit1mb.wav,
gethit1ml.wav, gethit2f.wav, gethit2fb.wav, gethit2fl.wav, gethit2m.wav,
gethit2mb.wav, gethit2ml.wav, gethit3f.wav, gethit3fb.wav, gethit3fl.wav,
gethit3m.wav, gethit3mb.wav, gethit3ml.wav, gethit4f.wav, gethit4fb.wav,
gethit4fl.wav, gethit4m.wav, gethit4mb.wav, gethit4ml.wav, jumpf_1.wav,
jumpf_1b.wav, jumpf_1l.wav, jumpm_1.wav, jumpm_1b.wav, jumpm_1l.wav,
lever.wav, portc_lp.wav, portcstp.wav, sldorstc.wav, sldorsto.wav,
speardn.wav, spearup.wav, trapdoor.wav
```

---

*Document generated for WillEQ audio implementation*
*EverQuest client path: /home/user/projects/claude/EverQuestP1999*
