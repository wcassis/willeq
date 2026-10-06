# WillEQ

A standalone EverQuest client targeting the Titanium client version (2006) with 3D graphics rendering, spatial audio, and support for platforms ranging from x86_64 servers to ARM single-board computers. Connects to EverQuest servers using the Titanium protocol and renders zones, characters, and UI through a patched Irrlicht engine driving a custom native OpenGL ES 2.0 driver. GLES2 is the only 3D renderer: on x86 it runs through Mesa EGL (surfaceless llvmpipe by default, or an X11 window on request), and on ARM through DRM/KMS.

This is a mostly vibe-coded project so the codebase is a bit chaotic. Expect major changes as features are implemented.

You need an existing account. A character can be created by the client itself at character select (B888, issue #204): `willeq -c willeq.json --create-character "Name,Race,Gender,Class,Deity,City"` creates it and enters world with it in its home city (never the tutorial); `--delete-character Name` and `--list-characters` do the other two. With graphics and no `character` in the config (or with `--character-select`), the client shows the official Character Select screen (B998, issue #318): pick a character and Enter World, Delete one (with the official confirmation), or press an empty slot's "Create a New Character" to open the two-page Character Creation screen, which enters world with the new character. With a configured character the client still selects it automatically, exactly as before (every scripted and CI run).

I'm developing this on a remote Ubuntu 24 server without a GPU. The default run needs no display at all (surfaceless EGL on llvmpipe); Xvfb + x11vnc or native RDP are used when a picture is wanted. The ARM builds target an Orange Pi One (Mali 400) running Armbian Noble with the Lima open-source driver.

## Project Status

**Status, 2026-09-03.** The open-work queue of record is `refactor/BOARD.md` —
one confirmed entry per open item, classed A (live defect) / B (unreached or
hygiene) / C (needs a decision) / P (process). `docs/module_map.md` is the map
from a symbol to the translation unit that holds it. The prose below this
paragraph was last revised 2026-07-28 and is a general orientation, not a
current defect list; where the two disagree, the board wins.


**What works reliably:**
- Login, server selection, character selection, and zoning
- 3D zone rendering with textures, lighting, fog, sky dome, and day/night cycle
- Weather effects (rain, snow) with particle systems and storm clouds
- Character/NPC models with skeletal animation and equipment display
- WASD movement with collision detection and server sync
- Chat (all channels), targeting (click, Tab cycle, F-keys), auto-attack
- Spell memorization, casting, and gem management
- Inventory window with equipment slots, bags, the official client's own item icons (T430) and tooltips; items are picked up and put down click-to-cursor like the official client (left click picks up / places / swaps / stacks, a stack opens the Quantity window, Shift = whole stack, Ctrl = one, right or Alt+left click shows the item — B442, #151, rules measured on the official client), or with `/pickup`, `/putdown`, `/cursor`, `/coin` and `/equip`
- Vendor buy/sell and banking through the oracle's own merchant and bank windows (T436), or by slash command; player-to-player trading and NPC hand-ins through the oracle's trade and give windows (B896), or by slash command
- Group system with HP/mana bars, invite/accept/disband/decline
- Raid commands (invite, accept, leave, remove, leader, loot type, looters, lock, group)
- Inspect (both answering and asking) with a configurable inspect message
- Bind wound, activatable skills (`/useskill`), duels, corpse dragging, resurrection prompts
- Pet window with all 10 commands
- Hotbar with 10 slots that fires by slot content (spell, skill, item, ...)
- Spatial audio with zone emitters, music, weather sounds, and creature sounds
- Navmesh and waypoint pathfinding (optional)
- Native RDP streaming and VNC support for remote access
- Native GLES2 rendering on ARM (Orange Pi One with Mali 400 via Lima)
- ETC1 flat-pack texture assets (EQE1) for memory-constrained boards

**What doesn't work or is incomplete:**
- Character select and creation have no 3D scene: no character preview, no sample model, no Explore/Rotate (greyed), no FacePick window (B998/B888)
- Visual spell effects incomplete. Only the cast glow, the cast-complete flash and the impact are reached from the game; the projectile, buff-aura, rain and ground-circle code exists but has **no production caller** — its only callers are the `model_viewer` FX harness and a unit test (`refactor/BOARD.md` B161/B165), so no spell projectile and no buff aura is ever drawn in a real session
- Swimming mechanics incomplete (keys exist, physics don't)
- Boats/transport not implemented
- Fall damage not implemented
- First-person view draws the player's own model and weapon for the duration of an attack animation and at no other time (T307 measured the rule against the official client; T309 implemented it in `src/client/graphics/entity_first_person_draw.cpp`). The down-pitch clamp that earlier revisions of this file described as the hiding mechanism was refuted live and no longer exists. Eye placement is still open (`refactor/BOARD.md` B018)
- UI is a fixed static-panel HUD. Merchant, bank, loot, the era container window (world containers and carried bags, B899), item display, Options, trade and give all have real windows now (T290, T421, T425, T434, T436, B899, B896). Items move click-to-cursor (B442, #151) in the inventory, bank, loot, container, trade and give windows

**Architecture note:** This is a thin client that delegates game logic to the server. Combat damage, spell effects, skill checks, and item transactions are all server-calculated. The client handles rendering, input, and state tracking.

## Feature Comparison: Classic/Kunark/Velious Era (1999-2000)

This checklist targets the original EverQuest experience through the Velious expansion.

Note: Many features have working UI but rely on server-side logic. The client handles rendering, input, and state tracking while the server calculates damage, spell effects, and validates actions.

Legend: Done | Partial | Not Implemented

### Connection & Authentication
| Feature | Status | Notes |
|---------|--------|-------|
| Login server authentication | Done | |
| Server selection | Done | |
| Character selection | Done | B998 (#318). Auto-select from config by default (byte-for-byte unchanged); with no configured character or `--character-select` (graphics), the official Character Select screen (`EQUI_CharacterSelect.xml`): the list from `OP_SendCharInfo` (name; "Name [level Class]" and the zone for the selected one), Enter World, Delete (eqstr 9065 Yes/No, closing itself after 10 s, `OP_DeleteCharacter`), Create a New Character, Quit. No 3D preview |
| Character creation | Partial | B888 (#204). `--create-character "Name,Race,Gender,Class,Deity,City[,STR=n,...]"` sends `OP_ApproveName` then `OP_CharacterCreate` exactly as eqemu's Titanium world validates them (byte-pinned) and enters world in the home city, never the tutorial; `--delete-character`, `--list-characters`; scenario `create_character`. The official two-page screen (`EQUI_CharacterCreate.xml`) and its flow are transcribed with post-Velious races/classes/cities HIDDEN (`data/char_create.tsv`), opened from the character-select screen's "Create a New Character" (B998); a created character enters world straight away, as the official client does. No FacePick window and no 3D preview |

### World & Zones
| Feature | Status | Notes |
|---------|--------|-------|
| Zone loading | Done | S3D/WLD parsing, EQE1 texture packs |
| Zone geometry | Done | With BSP regions |
| Zone textures | Done | ETC1 (EQE1 packs) or DDS (DXT1/DXT3/DXT5), animated textures |
| Zone lighting | Done | Ambient, static, dynamic, per-pixel (GLSL ES) |
| Zone fog | Done | Server-driven, distance-based |
| Sky dome | Done | Sky types, day/night cycle, celestial bodies |
| Day/night cycle | Done | Smooth color transitions |
| Weather (rain/snow) | Done | Particle systems, storm clouds, lightning |
| Zone transitions | Done | |
| Zone points | Done | |
| Boats/transport | -- | Issue #206 (B890). T168 deleted the unreachable board/leave senders |

### Graphics
| Feature | Status | Notes |
|---------|--------|-------|
| Zone geometry | Done | PVS culling, frustum culling, portal occlusion, software occlusion |
| Classic character models | Done | All original races |
| NPC/mob models | Done | |
| Equipment display | Done | Weapons, shields, helms with tinting |
| Skeletal animation | Done | Animation code mapping, interpolation |
| Animated textures | Done | Water, lava, fire, flags |
| Environmental particles | Done | Rain, snow, dust, pollen, mist, fireflies |
| Ambient creatures | Done | Flocking birds, fish, insects (boids system) |
| Spell visual effects | Partial | Cast glow, cast-complete and impact only; the projectile/aura/rain/ground-circle code has no production caller (#116, B161; feature plan appended there). JSON-configurable |
| Detail objects | Partial | Grass, plants, rocks with wind animation |

### Movement
| Feature | Status | Notes |
|---------|--------|-------|
| Walking/running | Done | With server sync |
| Strafing | Done | |
| Jumping | Partial | Two unmeasured models: the headless 25-unit arc (#33, B463) and the renderer's own velocity/gravity arc. Fall damage is never sent. Issue #209 (B893) |
| Swimming | Partial | Water detection, swim movement, sinking and animations exist (`renderer_player_physics.cpp`); the speeds and skill formula are unmeasured guesses. Issue #207 (B891) |
| Drowning | -- | No breath state, no Air Remaining window, the environment-damage packet is only received. Issue #207 (B891) |
| Levitation | -- | eqemu sends the levitate appearance with skip-self, so the self check never fires; must derive from own buffs. Issue #208 (B892) |
| Auto-run | Done | |
| Collision | Done | Map-based raycasting |
| Sit/stand/crouch | Done | With animations |

### Camera
| Feature | Status | Notes |
|---------|--------|-------|
| First-person | Partial | Player model + weapon are drawn for the duration of an attack animation and at no other time (T307 measured this against the official client, T309 implemented it). The down-pitch clamp this row used to describe was REFUTED live and no longer exists. Eye placement was solved against the official client by T431 (B018 closed): the eye is derived per race from the head bone's own vertex cluster and sits 0.45 up the symmetrised skull, which puts a Human male's eye 6.02 units above its own feet — the value the oracle's own frames minimise at. A Dwarf male still solves 1.0 units lower than that global fraction gives it (B470) |
| Official camera set | Partial | T461: the official client's six cameras, cycled by F9 -- First Person, Overhead, Chase, User Defined Camera 1, User Defined Camera 2, Tether (names and order from `eqstr_us.txt` 9500-9505). Chase is the old third-person follow camera, unchanged (collision, zoom with +/- or the mouse wheel). The two user cameras orbit the player, are adjustable and are persisted per character. Overhead and Tether geometry, and the user cameras' official units, are placeholders until measured against the official client (B530). The old "Free" camera is gone: it was never an official camera and never moved |
| Exact camera placement | Done | T461: `/camera set` / `nudge` / `get` -- azimuth, pitch, distance and look-at height around the player, for matching the official client's cameras |
| Mouse look | Done | LMB or RMB drag |

### Combat
| Feature | Status | Notes |
|---------|--------|-------|
| Targeting | Done | Click, Tab cycle, F-keys, /target, /assist |
| Auto-attack (melee) | Done | Server-calculated damage |
| Damage messages | Done | |
| HP tracking | Done | |
| Consider | Done | Color-coded con system |
| Death/corpse | Done | Corpse drag/consent, resurrection accept/decline |
| Corpse looting | Done | Items and money, auto-loot option |
| Spell casting | Done | 8 gem slots, memorize, cast, interrupt |
| Combat abilities | Done | /useskill, /taunt, /shield; server validates |
| Ranged auto-fire | Partial | /autofire toggle only; a single ranged shot is impossible (`UseAbility` hard-codes the melee slot) and the arrow is never drawn. Issue #216 (B900) |
| Duels | Done | /duel, /duelaccept, /dueldecline |
| Pet commands | Done | 10 commands via pet window or /pet |
| Bind wound | Done | /bandage; server reply parsed and shown |

### Spells
| Feature | Status | Notes |
|---------|--------|-------|
| Spell book | Done | Searchable |
| Memorize spells | Done | /mem, /forget, and clicking a spellbook row (it memorises into the first free gem; with all eight full it asks you to click the gem to replace). There is no drag — T444 corrected "drag-to-gem" here |
| Cast spells | Done | Gem slots, casting bar, interrupts |
| Spell effects | Partial | Cast glow, cast-complete and impact particles; projectile and aura are unreached code (B161) |
| Buff bar | Done | Duration timers |

### Chat
| Feature | Status | Notes |
|---------|--------|-------|
| Say / Shout / OOC / Auction | Done | |
| Group / Guild | Done | |
| Tell / Reply | Done | R key or /reply |
| Yell | Done | |
| Emote | Done | /emote + shortcuts (/wave, /dance, /cheer, /laugh) |
| Chat filtering | Done | The era `/filter` set (eqstr_us.txt 1354-1360: seventeen filters) on the Options window's Filters page and `/filter <name>`, sent to the server as `OP_SetServerFilter` (eqemu filters server-side), persisted per character under the oracle's `[Options]` keys (B433) |

### UI
| Feature | Status | Notes |
|---------|--------|-------|
| Chat panel | Done | Scrollable, filterable |
| Inventory popup | Done | Equipment slots, item icons (T430), tooltips. Click-to-cursor (B442): left click picks up / places, right click shows the item display while held (Alt+left keeps it), right click on a bag toggles its window, a purse row opens the Quantity window for coin, Destroy asks before destroying the held item (B442b) |
| Player status | Done | HP/mana/stamina, stats |
| Target info | Done | |
| Spell book popup | Done | Searchable, click-to-memorise (no drag; T444) |
| Spell gem bar | Done | 8 gems with casting bar |
| Hotbar | Done | 10 slots, keys 1-0, content-typed |
| Group panel | Done | HP/mana bars |
| Pet panel | Done | Commands and status |
| Buff bar | Done | Duration timers |
| XP bar | Done | |
| Skills popup | Done | Skill values by category, class skill table from `config/skill_caps.json` |
| Merchant window | Done | T436 (board B429), the oracle's `EQUI_MerchantWnd.xml`: stock list with icons, Buy/Sell, Done. As measured (B442b, #152): clicking one of your items selects it for sale (never the cursor), the button is Buy or Sell by the last selection, Sell on a stack opens the Quantity window. `/vendorlist`, `/buy`, `/sell` still work |
| Bank window | Done | T436 (board B429), the oracle's `EQUI_BankWnd.xml`: 8 slots + 4 money rows + Change + Done. Change converts every coin in the bank and on you to the fewest coins (B441, the client's own tip text; `/bank change`). `/bank` still works |
| Cursor item | Done | T436, `EQUI_CursorAttachment.xml`: the held item's icon follows the mouse |
| Loot window | Done | T290, center popup, scrollable; left click = to the cursor, right click = straight to the inventory (measured, #151) |
| Tip windows | Done | B834 (#202): the Tip of the Day at zone-in and the context tips on the first open of a window (Inventory, Bank, Merchant, Trainer, Friends, Loot, Quantity, ...), the client's own `help/tips/` texts, `EQUI_TipWnd.xml` controls, "Don't show" persisted in the official `[Tips]` keys |
| Options window | Done | T434, the oracle's `EQUI_OptionsWindow.xml` at 400x452, seven tabs, Alt+O. Its geometry is the constexpr table in `include/client/graphics/ui/options_layout.h`, **130 control rows**; every row is classified in `src/client/graphics/ui/options_classification.cpp` (wired, or excluded with its reason -- post-Velious, Will's OUT ruling, no fable capability, or awaiting the oracle) and `OptionsClassification.*` fails on an unclassified row. B430/B433 wired General (anonymous/roleplay, autosplit, auto-consent, pet window popup, the volumes), Display (PC/NPC names, clip plane), Mouse, Keys (category, rebinding, reload defaults) and Filters. B993 (#313) wired Colors: the 86-row `[TextColors]` chat colour table (eqclient.ini's defaults, persisted per character), Reset, and the `EQUI_ColorPickerWnd` picker; every chat line is drawn in its row's colour. B994 (#314) wired Display's 3D target ring (`TargetIndicator.ini`'s con-coloured ring under the target, default off). B995 (#315) wired the Particles box's Spell and Env sub-pages (opacity, density, near clip on the particle systems, persisted as eqclient.ini's floats); the Player page and the two "On For" filters stay greyed with their reason. The remaining General/Display rows stay greyed (#144, #145). `/option` sets any wired setting by name |
| Trade | Partial | B896 (#212): `EQUI_TradeWnd.xml` (8 slots a side, ours on the right, column-major; coin rows; Trade / Cancel) and the NPC `EQUI_GiveWnd.xml` (4 slots, Give / Cancel), opened by a click on a player or NPC with an item held; the held item drops into slot 3000, an incoming request is acknowledged with no prompt, the accepting side's name turns green and any change resets it, offered items cannot be taken back, cancel prints eqstr 1450 and completion prints nothing -- all measured (`review/UIGAP-ORACLE/212`). Owed: the live two-client run, the no-drop refusal string (not captured), the tip windows |
| Item / spell display | Done | T421 + T425, one window for both (`EQUI_ItemDisplay.xml`); click an item slot, SHIFT+click a gem or spellbook row |
| Pick up / put down an item | Partial | Click-to-cursor in the inventory, bank, loot, container, trade and give windows, the Quantity window (a stack or a purse/bank coin row; its box takes typed digits) and `/pickup` `/putdown` `/cursor` `/coin` — one `CursorGestureIntent` path, rules from the official client's capture (B442, B442b, #151). Coin rides the cursor with its count; a motionless world click with an item held asks "ARE YOU SURE you wish to drop..." (Yes = `OP_GroundSpawn`) unless it lands on a player (trade) or an NPC (give, B896), and Destroy asks before `OP_MoveItem 30 -> -1`; unanswered, the two prompts close themselves after 20 s / 10 s and send nothing (measured). A loot slot's left click takes the item to the cursor and its right click straight into the inventory (measured, #151 rows 46a/46c). Owed: the live `pickup_roundtrip` scenario |
| Tradeskill container | Partial | B899 (#215): a world container (forge, oven) and a carried kit open the era container window (`EQUI_Container.xml`): its slots, Combine, Done. The post-Velious recipe window no longer opens (Will 2026-09-23); `/recipesearch` and `/recipecombine` stay as automation. The button reads "Combine" on every container (measured, `review/UIGAP-ORACLE/215`); the live combine is owed |
| Skill trainer | Partial | B898, the oracle's `EQUI_TrainWindow.xml` at 580x280: skill list (name, value, price in pp/gp/sp/cp), practices, experience, money, Train, Done. Opens with /train, T, or Ctrl+click on a guildmaster; refreshes only when the server confirms a train. The Rank column and the live train are still owed to the oracle (issue #214) |

### Inventory
| Feature | Status | Notes |
|---------|--------|-------|
| Item display | Done | Icons and names |
| Item stats | Done | Tooltip |
| Move items | Partial | Click-to-cursor (B442) plus `/equip` by slot or name and `/pickup` / `/putdown`. Not drag and drop: the official client is click-to-cursor (T444 corrected "Drag and drop" here) |
| Container bags | Partial | B899: `/bag <general slot>` opens/closes a bag's window (`EQUI_Container.xml`, several at once, stacked at one position as measured), contents drawn with icons, cells click-to-cursor (B442). Right-click on a bag in a general slot toggles its window, as the official client does (B442b, #151 rows 19-21) |
| Equipment slots | Done | With tint colors |
| Loot items | Done | |
| Currency | Done | All denominations, /split |
| Item linking | -- | Post-Velious, EXCLUDED from the baseline (#217 closed, B901) |

### Social
| Feature | Status | Notes |
|---------|--------|-------|
| /who | Done | Server who-all with count substitution |
| Friends list | Partial | B897: the official two-tab Friends window (Alt+F, `/friendswindow`, from `EQUI_FriendsWnd.xml`), 100 names, `/friends [name]` (`/friend`) toggles/lists, `/friendswho` and the Who button query the stored list, persisted in `<Char>_<server>.ini` `[Friends]` as the official client does. Live two-client proof owed (#213) |
| Ignore list | Partial | B897: the window's Ignored tab and `/ignore [name]`, 100 names, `[Ignored]` in the same ini; an ignored sender's tell, say, shout, ooc, auction and emote are hidden client-side (measured on the official client, `review/UIGAP-ORACLE/213`) and never become `/reply`'s target. Cap and group/guild behaviour unmeasured (#213) |
| Group | Done | /invite, /groupaccept, /disband, /decline, /groupassist |
| Raid | Done | /raid invite/accept/leave/remove/leader/loot/looter/lock/unlock/group/status/list |
| Guild management | Done | Invite, accept/decline, remove, demote, leader, MOTD |
| Trading | Partial | The trade and give windows (B896) and /trade, /accept, /tradedecline, /canceltrade; the live two-client round trip is owed |
| Inspect | Done | /inspect; /inspectmessage sets your own text |
| LFG / anon / roleplay / AFK | Done | Toggles |
| Surname / random | Done | |
| Bug / petition | Done | /petition and /bug both work. T375 rewrote `OP_Report`'s body to eqemu's `reporter|reported|text` byte-parsed shape; T377 proved the live row (`name=Testchar`, `reported_text` populated). `CanUseReport` allows one `/bug` per session |

### NPC Interaction
| Feature | Status | Notes |
|---------|--------|-------|
| Doors | Partial | Open/close sync, door sounds, and since T435 real ray collision against the door mesh (board B425 closed). T435 also fixed the open/closed state, which was read from the wire backwards for every non-inverted door — roughly 19,200 of the 19,249 rows. Opentype 59 lifts translate by `door_param` since T438 (B422 closed) and the ride is unit-proven but not yet live-proven (#32); opentypes 50/53/54 are skipped outright, which drops 1,310 era rows (#148). Switches, levers, traps and teleport doors are unmeasured (#149); the feature plan is on #25 |
| Hail NPC | Done | H key, /hail |
| Merchant buy/sell | Done | /vendor, /vendorlist, /buy, /sell |
| Banker | Done | /bank |
| World objects | Done | /clickobject (forge, oven, ground spawn) |
| Quest/NPC text | Done | Displayed in chat |

### Skills
| Feature | Status | Notes |
|---------|--------|-------|
| Skill display | Done | Values and categories, class caps from `config/skill_caps.json` |
| Skill activation | Done | /useskill by name or id, hotbar skill slots |
| Tradeskills | Partial | Combine from the container window or `/combine [slot]`, world and carried containers (B899); recipe search/auto-combine commands; combines server-side |
| Tracking | Partial | B895 (issue #211): `/useskill tracking` opens the Tracking Window (the oracle's `EQUI_TrackingWnd.xml`: con-coloured list, R/Y/W/B/L/G filters, sort and players comboboxes, Track, Cancel); Track or `/track` starts tracking and the official direction lines follow every 2 s (measured against the official client, `review/UIGAP-ORACLE/211`). `/trackfilter`, `/tracksort`, `/trackplayers`, `/stoptracking`, `/tracklist`. Partial until the live `track_list` scenario runs and the uncaptured controls (players combo, r* sorts) are measured |
| Forage / Fishing | -- | Requests are sent; replies only logged. Issue #210 (B894) |

### Audio
| Feature | Status | Notes |
|---------|--------|-------|
| Zone music | Done | XMI/MIDI via TinySoundFont, MP3 |
| Sound effects | Done | WAV from PFS archives |
| Spatial audio | Done | 3D positioned zone emitters |
| Day/night audio | Done | Crossfade transitions |
| Player sounds | Done | Race/gender-specific (death, hit, jump, etc.) |
| Creature sounds | Done | Per-race (attack, damage, death, idle) |
| Door sounds | Done | Type-specific (metal, stone, wood, secret) |
| Weather audio | Done | Rain loops, wind, thunder |
| Water sounds | Done | Entry, swimming, underwater ambient |
| UI sounds | Done | Level up, buy/sell, notifications |
| Combat music | Done | Stinger tracks during combat |
| Volume control | Done | /volume, /musicvolume, /effectsvolume |

### Extras (Not in Original)
| Feature | Status | Notes |
|---------|--------|-------|
| Headless mode | Done | `--no-graphics`, `--headless`, `--automated` |
| Scripted command tours | Done | `--script <file>` |
| Session recording/replay | Done | `--record-session`, replayed by `test_session_replay` |
| Video frame capture | Done | `--dump-frames-dir` + `scripts/ci/encode_capture.sh` |
| VNC support | Done | Xvfb + x11vnc |
| Native RDP streaming | Done | FreeRDP, with audio |
| Screenshots | Done | F12 |
| Navmesh pathfinding | Done | Recast/Detour |
| Waypoint pathfinding | Done | Boost A* |
| Debug overlays | Done | Map, navmesh, zone line wireframes |
| ARM/GLES2 rendering | Done | Orange Pi One (Mali 400) |
| DRM/KMS output | Done | No X11 required |
| Constrained HW presets | Done | voodoo1, voodoo2, tnt, orangepi, or `WxHxN` (renderer-independent) |
| EQE1 ETC1 texture packs | Done | `tools/eqe1_builder`, `--eqe1-path` |
| GM commands | Done | /gmzone, /summon, /goto, /gmfind, /kick, /kill, /hideme, ... |

## Requirements

### Required Dependencies
- C++17 compiler
- OpenSSL (libssl-dev)
- zlib (zlib1g-dev)
- fmt (libfmt-dev)
- GLM (libglm-dev, header-only)
- jsoncpp (libjsoncpp-dev)
- A locally-built patched GLES2 Irrlicht (see Building). This is not an apt package: the desktop-GL `libirrlicht-dev`/`libxxf86vm-dev` route was removed and there is no fallback.
- Mesa EGL + GLESv2 development headers for the x86 build

### Optional Dependencies
| Dependency | Feature |
|------------|---------|
| freerdp3-dev, libwinpr3-dev | Native RDP streaming |
| libboost-graph-dev | Waypoint pathfinding |
| librecast-dev | Navmesh pathfinding (Recast/Detour) |
| libdrm-dev, libgbm-dev, libegl-dev | DRM/KMS output (ARM) |

Audio uses bundled header-only libraries (miniaudio, TinySoundFont, dr_wav) with no external dependencies.

### EverQuest Client Files
Requires a copy of the EverQuest Titanium Edition client for zone geometry, textures, character models, and sound files. For ETC1 texture packs, the client directory is converted once with `tools/convert_all_to_eqe1.sh` (see EQE1 Asset Packs).

## Building

```bash
# One-time bootstrap: build the patched GLES2 Irrlicht for x86
# (output goes to the gitignored third_party/irrlicht-gles2-x86/)
bash scripts/build-gles2-x86.sh --irrlicht-only

# Configure (from project root)
mkdir -p build && cmake -S . -B build \
    -DEQT_GRAPHICS=ON \
    -DEQT_GLES2=ON \
    -DEQT_DRM=OFF \
    -DEQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86 \
    -DEQT_BUILD_TESTS=ON

# Build
cmake --build build -j24

# Run tests (for the count: ctest --test-dir build -N | tail -1)
ctest --test-dir build --output-on-failure
```

The executable is output to `./build/bin/willeq`. Configuring `EQT_GRAPHICS=ON` without a patched Irrlicht fails at `cmake` time with a message naming the bootstrap command.

`bash scripts/build-gles2-x86.sh` with no arguments additionally produces a second, smaller tree (`build-gles2/`, tests off) containing just `willeq` and `gles2_zone_benchmark`; that tree is what the visual gate (`scripts/ci/visual_gate.sh`) targets.

### Tests

The default suite (`ctest`) registered **3,949 tests across 232 test executables** when this line was written (T371, 2026-09-05); the suite grows every unit, so run `ctest --test-dir build -N | tail -1` for the current number rather than trusting this one. None of them need a server, a display, or EQ client files (a few skip when a packet capture is absent), and since T392 (2026-09-05, board B215) there is no expected failure at all — the old ALSA waiver on `AudioManagerTest.PreloadCommonSounds` was a misdiagnosis and the test is green. Live/integration tests that need a running eqemu server, a display, and client files are built but only registered under an opt-in flag:

```bash
cmake -S . -B build -DEQT_LIVE_TESTS=ON
ctest --test-dir build -L live      # 3 binaries: test_zoning_graphics_integration,
                                    # test_zoning_integration, test_beneficial_spell_casting
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `EQT_GRAPHICS` | ON | Enable Irrlicht graphics rendering (requires the patched GLES2 Irrlicht) |
| `EQT_GLES2` | OFF | OpenGL ES 2.0 backend (required with `EQT_GRAPHICS=ON` on x86) |
| `EQT_DRM` | OFF | DRM/KMS/GBM/EGL output (ARM, no X11) |
| `EQT_GLES2_IRRLICHT_DIR` | (empty) | Path to the locally-built patched Irrlicht |
| `EQT_BUILD_TESTS` | ON | Build the test suite |
| `EQT_LIVE_TESTS` | OFF | Register the live/integration tests with ctest |
| `EQT_DEBUG` | ON | Debug logging |
| `WITH_AUDIO` | ON | Audio (header-only: miniaudio, TinySoundFont, dr_wav) |
| `WITH_RDP` | auto | Native RDP streaming (ON when FreeRDP is found) |

Pathfinding libraries (`EQT_HAS_NAVMESH`, `EQT_HAS_WAYPOINT`) are auto-detected.

### ARM Cross-Compilation (Orange Pi One)

```bash
# Cross-compile for Armbian Noble (Lima GPU, DRM/KMS, GLES2)
bash scripts/build-arm-noble.sh            # graphics + audio
bash scripts/build-arm-noble.sh --headless # no graphics
bash scripts/build-arm-noble.sh --no-audio

# Output: build-arm-noble/bin/willeq (ELF 32-bit ARM)
```

Docker-based cross-compilation using `docker/Dockerfile.arm-noble`, targeting ARMv7-A Cortex-A7 with the same `COpenGLES2Driver` the x86 build uses. The former `--no-gles2` desktop-GL-via-Lima build was retired; GLES2 is the only ARM renderer. `scripts/build-arm64-noble.sh` / `docker/Dockerfile.arm64-noble` is a 64-bit variant for Rock64-class boards. See `docker/CLAUDE.md` for the sysroot strategy and the DRM/KMS device.

## Configuration

Create a configuration file (e.g., `willeq.json`):

```json
{
    "host": "login.server.com",
    "port": 5998,
    "user": "your_username",
    "pass": "your_password",
    "server": "Server Name",
    "character": "CharacterName",
    "eq_client_path": "/path/to/EverQuest"
}
```

The same keys may be wrapped in a `"clients": [ { ... } ]` array (the first entry is used). Optional keys: `maps_path`, `navmesh_path`, `region_maps_path`, `mode`, a `renderer` object (`width`, `height`, `fullscreen`), a `rendering` object (`constrained_mode`), and a `hotkeys` object of overrides applied on top of `config/hotkeys.json`.

Optional audio configuration:

```json
{
    "audio": {
        "enabled": true,
        "master_volume": 100,
        "music_volume": 70,
        "effects_volume": 100,
        "soundfont": "/path/to/soundfont.sf2"
    }
}
```

Optional RDP configuration:

```json
{
    "rdp": {
        "enabled": true,
        "port": 3389
    }
}
```

Other user settings live in `config/`: `hotkeys.json` (key bindings), `display_settings.json`, `constrained_presets.json` (hardware presets), `eqe1_archives.json` (EQE1 archive lists), `spell_effects.json` (particle presets, hot-reloadable), `skill_caps.json`, and the weather/detail/tree-wind configs. (`chat_settings.json` and `ui_settings.json` were listed here until T402 deleted them: they were the deleted `WindowManager`'s saved window state and no code had read them for months. The client persists no user settings at all — board B255.)

### EQE1 Asset Packs

For memory-constrained boards, textures are served from EQE1 flat packs: one mmap-able `.eqe1` file per original S3D archive holding pre-compressed ETC1 textures (with `_alpha` companions) plus raw entries such as `.wld` files (`refactor/E01_eqe1_format.md`, `include/client/graphics/eqe1_format.h`).

- Build them once from the Titanium client: `tools/convert_all_to_eqe1.sh --eq-path <EQ dir> --output <pack dir>` (drives `build/bin/eqe1_builder` and `verify_eqe1`).
- Point the client at them with `--eqe1-path <dir>` (or `eqe1Path` in a constrained preset).
- `config/eqe1_archives.json` names the per-zone (`{zone}`, `{zone}_obj`, `{zone}_chr`) and global archives; `data/eqe1_texture_index.json` (built by `eqe1_texture_index_builder`) maps texture names to the archive that holds them, so the loader opens exactly the archive a lookup needs instead of probing every global pack.
- Lookup is all-or-nothing: with an EQE1 path configured, a texture missing from every pack is a fatal error, not a fallback to the S3D copy.

Composite entity texture sheets (`--composite-entities <dir>`, `enableCompositeEntities` in a preset) are a separate, off-by-default option; the `orangepi` preset leaves them off.

## Running

```bash
# With graphics (default: surfaceless EGL, no display required)
./build/bin/willeq -c willeq.json

# With graphics in an X11 window (opt-in only, never auto-detected)
./build/bin/willeq -c willeq.json --egl-x11

# Headless (no graphics)
./build/bin/willeq -c willeq.json --no-graphics

# With VNC (Xvfb + x11vnc, for remote servers)
./scripts/start-with-vnc.sh -c willeq.json
# Connect: vnc://localhost:5950

# With VNC + audio streaming
./scripts/start-with-vnc-audio.sh -c willeq.json
# VNC: vnc://localhost:5910, Audio: vlc http://localhost:8085

# With native RDP
./build/bin/willeq -c willeq.json --rdp
./build/bin/willeq -c willeq.json --rdp --rdp-port 13389
# Connect: mstsc.exe /v:hostname:3389  or  xfreerdp /v:hostname:3389 /cert:ignore

# On Orange Pi (DRM/KMS, GLES2)
./willeq -c config.json --drm --renderer gles2 --constrained orangepi -r 800 600
```

`--renderer` accepts only `gles2`; `software` and `opengl` were removed. `--constrained` takes `voodoo1`, `voodoo2`, `tnt`, `orangepi` (the default), or a `WxHxN` memory triple. Run `willeq --help` for the full option list (`--threads`, `--zone-load`, `--frame-timing`, `--scene-profile`, `--log-level=`, `--log-module=`, and the video-capture flags).

### Audio Options

```bash
--no-audio                     # Disable audio
--audio-volume 80              # Master volume (0-100)
--music-volume 50              # Music volume (0-100)
--effects-volume 100           # Effects volume (0-100)
--soundfont /path/to/file.sf2  # SoundFont for XMI/MIDI music
```

### Linux Capabilities

For reliable zone loading, set network capabilities on the binary:

```bash
sudo setcap cap_net_raw,cap_net_admin+ep ./build/bin/willeq
```

This allows the client to request a 1MB UDP receive buffer (via `SO_RCVBUFFORCE`) to handle the server's burst of 40+ packets during zone loading. Without it, the default kernel buffer may overflow, causing zone loads to hang.

### Display Options

| Method | Use Case | Audio | Client |
|--------|----------|-------|--------|
| Surfaceless EGL (default) | No display at all; llvmpipe | Local | Native |
| X11-windowed EGL (`--egl-x11`) | Local display | Local | Native |
| Xvfb + VNC | Remote access | None or HTTP stream | VNC client |
| Native RDP | Windows remote | RDP audio channel | mstsc.exe, xfreerdp |
| DRM/KMS (`--drm`) | ARM boards, no X11 | Local | Direct framebuffer |

## Controls

### Key Bindings

Bindings are configurable in `config/hotkeys.json` (or a `hotkeys` object in the main config).

**Movement:**
| Key | Action |
|-----|--------|
| W / Up | Move forward |
| S / Down | Move backward |
| A / Left | Turn left |
| D / Right | Turn right |
| Ctrl+A / End | Strafe left |
| Ctrl+D / Home | Strafe right |
| Space | Jump |
| ` / Numpad+ / NumLock | Toggle autorun |
| [ | Swim up |
| ] | Swim down |

**Combat & Targeting:**
| Key | Action |
|-----|--------|
| Q | Toggle auto-attack |
| Ctrl+Q | Attack target (B905: the key had no dispatch arm until UIGAP-small) |
| C | Consider target |
| H | Hail target |
| Tab | Cycle targets (B905: shadowed by a dead Global `ChatAutocomplete` binding until UIGAP-small) |
| Shift+Tab | Cycle targets reverse |
| F1 | Target self |
| F2-F6 | Target group member 1-5 |
| F7 | Target nearest PC |
| F8 | Target nearest NPC |
| Escape | Close the most recently opened window, one per press (B460, measured); with none open, clear target |

**Spells & Hotbar:**
| Key | Action |
|-----|--------|
| Alt+1-8 | Cast spell from gem 1-8 |
| 1-0 | Hotbar slots 1-10 |

**Windows:**
| Key | Action |
|-----|--------|
| I | Inventory |
| K | Skills |
| Alt+P | Group |
| Alt+F1 | Pet |
| Ctrl+B | Spellbook |
| Alt+B | Buff bar |
| Enter | Open chat |
| / | Open chat with slash |
| R | Reply to last tell |
| Tab (in chat) | Command autocomplete |

**Camera:**
| Key | Action |
|-----|--------|
| F9 | Cycle the camera (the official set; replaced Ctrl+F5) |
| +/- or mouse wheel | Camera zoom in/out (the wheel zooms when the cursor is over no scrollable panel) |
| LMB/RMB drag | Look around |
| F12 | Screenshot |

**Debug:**
| Key | Action |
|-----|--------|
| U | Interact with door/object |
| Ctrl+F4 | Toggle zone lights |
| Ctrl+Alt+C | Toggle collision debug |
| Ctrl+Z | Toggle zone line visualization |
| Ctrl+M | Toggle map overlay |
| Ctrl+N | Toggle navmesh overlay |
| Ctrl+V | Toggle frustum culling |
| PageUp / PageDown | Scroll the static-UI panel under the cursor (T313) |
| Mouse wheel | Scroll that same panel (T313) |
| Shift+Escape | Quit |

### Slash Commands

Commands are dispatched through three registries in order: the game registry (audio and diagnostics), the action layer (`src/client/action/command_processor.cpp`, everything gameplay), and finally the renderer, whose commands only exist when graphics are enabled. `/help [command]` lists what is registered.

**Chat:**
```
/say (/s)                       Say to nearby
/shout (/sh, /sho)              Shout to zone
/ooc (/o)                       Out of character
/auction (/auc)                 Auction message
/tell (/t, /msg) <name> <msg>   Private message
/reply (/r) <msg>               Reply to last tell
/gsay (/g)                      Group chat
/gu (/guildsay)                 Guild chat
/yell                           Yell for help
/emote (/em, /me) <text>        Emote action
/wave, /dance, /cheer, /laugh   Animation emotes
```

**Movement:**
```
/loc (/location)                Show coordinates
/sit, /stand                    Sit/stand
/camp                           Sit and logout
/crouch (/duck)                 Crouch
/feign (/fd)                    Feign death
/jump                           Jump
/walk, /run, /sneak             Movement speed
/move <x> <y> <z>               Move to coordinates
/moveto <name>                  Move to entity
/warp <target>                  Teleport to coordinates, entity, or target
/follow [name], /stopfollow     Follow entity
/face <name|x y z>              Face direction
/turn <degrees>                 Turn to heading
/rewind                         Return to a safe position
/zoning [on|off]                Whether zone lines trigger a zone change
```

**Combat:**
```
/target (/tar) <name>           Target entity
/assist (/a) [name]             Assist target or named player
/attack (/att), /stopattack     Start/stop attack
/aa (/~)                        Toggle auto-attack
/autofire (/af)                 Toggle ranged auto-attack
/consider (/con)                Consider target
/hail                           Hail target
/taunt, /shield                 Class abilities on target
/useskill (/skill) <name|id>    Use an activatable skill
/bandage (/bindwound)           Bind wound on target
/duel, /duelaccept, /dueldecline
/loot <id>                      Loot a corpse (see /list)
/autoloot [on|off]              Toggle auto-looting
/hunt, /listtargets             Auto-hunting mode
```

**Spells:**
```
/cast <gem#>                    Cast from gem (1-8)
/mem (/memorize) <gem#> <spell> Memorize spell
/forget <gem#>                  Forget spell
/gems                           Show memorized spells
/interrupt (/ducking)           Interrupt casting
/spellbook (/book)              Open spellbook
```

**Group, raid, guild:**
```
/invite (/inv) [name]           Invite to group
/groupaccept (/gaccept)         Accept group invite
/disband, /decline              Leave group / decline invite
/groupassist (/ga) [name]       Group assist
/split <plat> [gold] [silver] [copper]  Split money with group
/raid <subcommand>              invite, accept, leave, remove, leader, loot,
                                looter, lock, unlock, group, status, list
/guildinvite (/ginvite) <name>  Invite to guild
/guildaccept, /guilddecline     Answer a guild invite
/guildremove, /guilddemote, /guildleader, /guildmotd
```

**Social:**
```
/who (/whoall) [filters]        Who-all query
/inspect [name]                 Inspect target or player
/inspectmessage (/inspectmsg)   Show or set your inspect message
/trade [name]                   Request trade
/accept, /tradedecline, /canceltrade
/lfg, /afk, /anon, /roleplay    Status toggles
/surname <name>                 Request surname
/random (/roll) [n]             Roll a random number
/friends (/friend) [name]       Add/remove a friend; no name lists them
/ignore [name]                  Ignore/unignore a player; no name lists them
/friendswho [names]             Ask which friends (or named players) are online
/find (/findperson) [name]      Ask the server to path you to a target
/consent <name>, /deny <name>   Corpse drag permission
/corpsedrag (/drag), /corpsedrop (/drop)
/rezaccept, /rezdecline         Answer a resurrection offer
/appearance                     Change face/hair/beard
/bug (/report), /petition       Send to server
```

**Commerce, banking, tradeskills:**
```
/vendor                         Open/close vendor with target
/vendorlist                     List the open vendor's inventory
/buy <slot> [qty]               Buy from the open vendor
/sell <slot> [qty]              Sell to the open vendor
/bank                           Open/close bank with target
/train                          Open the trainer window with target
/train <skill|id|list>          Train a skill at the open trainer
/trainclose (/traindone)        Close the trainer window
/equip <slot> <slot> | <name>   Move/equip an item (/equip list)
/clickobject (/clickobj) <id>   Click a world object (forge, oven, ground spawn)
/bag <general slot>             Open/close a carried container's window
/combine [general slot]         Combine in the open world container or a carried kit
/recipesearch (/recipes) <text> Search the open container's recipes
/recipedetails, /recipecombine, /recipefavorites
```

**Pet:**
```
/pet <command>                  attack, back, follow, guard, sit, taunt,
                                hold, focus, health, dismiss
```

**Audio:**
```
/music [on|off]                 Toggle music
/sound [on|off]                 Toggle sound effects
/volume (/vol) [0-100]          Master volume
/musicvolume (/mvol) [0-100]    Music volume
/effectsvolume (/evol) [0-100]  Effects volume
```

**Utility:**
```
/help (/?) [command]            Show help
/list                           List nearby entities
/dump <name>                    Dump entity appearance/equipment info
/door (/u)                      Interact with nearest door
/time (/settime) [hour]         Show or set game time
/save                           Request character save
/debug <level>                  Set debug level (0-6)
/logonly, /logexclude, /logmodule, /logclear   Module log filters
/perf (/metrics)                Performance report
/pmem (/memory)                 Memory usage breakdown
/hotkeys                        Show key bindings
/pathfinding                    Toggle pathfinding
/quit (/exit, /q)               Exit immediately
```

**GM (server permitting):**
```
/gmzone, /summon, /goto, /gmfind, /kick, /kill, /hideme, /emotezone, /gmlastname
```

**Renderer-only (graphics builds):**
```
/screenshot                     Save a screenshot
/filter <name>                  Toggle an era chat filter (no name lists them)
/option [list | <name> [value]] Show or set an Options-window setting
/option textcolor [reset | <1-86|name> [<r> <g> <b>]]  The Colors page's chat colour table
/timestamp (/ts)                Toggle chat timestamps
/camera                         Camera controls
/dumpscene (/ds)                Dump the scene graph
/sceneprofile (/sp)             Scene breakdown profiler
/frametiming (/ft)              Frame timing profiler
/budget, /governor (/gov)       Frame budget governor
/clipdist, /renderdist          Clip / view distance
/lightmode, /zlight, /olight, /plight, /stencil, /portal, /sort, /mergedraws
/sky, /skytype, /season, /fire  Sky and environment
/grass, /detail, /detailinfo, /togglegrass, /toggleplants, /togglerocks, /toggledebris
/reloadfx, /reloadspellfx, /reloadweather, /weatherpreset (/wq)
/shaderdebug (/sd), /simworker (/sw), /placeholder, /upload
/newui, /inventory, /newbook, /skills     Static-panel UI toggles
```

## Architecture

### Threading Model

The application runs two threads, decoupled via a thread-safe event/intent bridge:

```
Game Thread                         Render Thread (main)
-----------                         --------------------
Network events (TickNetwork)        Drain bridge events -> applyEvent()
Game state updates (GameTick)       processFrame() (Irrlicht render)
Intent processing (bridge intents)  Loading thread management
Audio updates                       Zone load completion detection
```

`EverQuest` never touches the renderer directly: game state changes are published as typed events through `GameStateBridge`, and user actions come back as intents. `IrrlichtBridge` translates events into renderer calls; `ConsoleBridge` logs them in headless mode. Zone loading runs on its own thread that owns the GL context, working from an immutable `ZoneLoadSnapshot` while the game thread keeps processing packets. Rendering-side simulation (animation, particles, boids, weather, visibility) runs on a `SimulationWorker` off the render thread.

### Connection Flow

The client connects through three stages, each with its own connection manager:
1. **Login Server** - Authentication and server list
2. **World Server** - Character selection and zone info
3. **Zone Server** - Gameplay

### Network Protocol

UDP-based Daybreak protocol (Titanium variant) with:
- Reliable delivery with 16-bit sequence numbers
- Packet fragmentation and reassembly (4 independent streams)
- zlib compression with marker-based detection
- XOR obfuscation
- CRC32 validation
- Exponential backoff retransmission (30ms initial, 5s max)
- 1MB socket receive buffer for zone load packet bursts

All opcodes are hard-coded for Titanium; packet layouts live in `include/common/packet_structs.h`.

### Rendering Pipeline

One rendering backend: a custom native OpenGL ES 2.0 driver (`COpenGLES2Driver`, under `docker/irrlicht-drm/`) built into a patched Irrlicht 1.8.5. It replaces Irrlicht's desktop drivers entirely, uses six built-in GLSL ES 1.0 shader programs (no fixed-function pipeline), and uploads ETC1 textures natively (`GL_ETC1_RGB8_OES`). The same driver runs on the Orange Pi (DRM/KMS, `--drm`) and on x86 (Mesa EGL: surfaceless llvmpipe, or `--egl-x11` for a window). Software and desktop-GL backends were removed (`docs/decisions.md`, 2026-07-28).

Zone visibility uses PVS (Potentially Visible Set) culling, frustum culling, portal occlusion, and a software occlusion culler on the simulation worker. Constrained hardware presets (voodoo1, voodoo2, tnt, orangepi) enforce memory budgets, texture limits, and render distances; on memory-limited devices an LRU-evicting `ConstrainedTextureCache` serves textures from mmap'd EQE1 packs under a byte budget.

### Audio Pipeline

Software-mixed spatial audio using header-only libraries (no external deps):
- **miniaudio.h** for cross-platform audio output (WASAPI/CoreAudio/ALSA)
- **TinySoundFont** (tsf.h + tml.h) for MIDI/XMI synthesis with dual SoundFont support
- **dr_wav.h** for WAV decoding from PFS/S3D archives
- Software stereo mixer at 22050 Hz (16 SFX + 2 music channels, lock-free render)
- Zone sound emitters with day/night variants and distance falloff
- Race/gender-specific player and creature sounds
- Weather audio (rain, wind, thunder)
- Combat music stingers
- Loopback mode for RDP audio streaming

### Key Components

- **Application** (`src/client/application.cpp`) - Owns renderer, bridge, game thread, loading thread; CLI and config parsing
- **EverQuest** (`src/client/core/everquest.cpp` + the twelve extracted `src/client/<family>/` components, Batch K) - Protocol handling and game state; see `docs/module_map.md`
- **GameStateBridge / IrrlichtBridge** (`include/client/bridge/`) - Thread-safe event/intent queues between game and renderer
- **ActionDispatcher / CommandProcessor** (`src/client/action/`) - Unified action layer behind slash commands, hotbar, and console input
- **IrrlichtRenderer** (`src/client/graphics/renderer/` + the eleven `EQT::Graphics::<Name>Component` directories, Batch L) - Scene management, zone load steps, visibility, lighting
- **EntityRenderer** - Character/NPC models with skeletal animation and equipment
- **RaceModelLoader / EquipmentModelLoader** (`src/client/graphics/eq/`) - Model loading from S3D/WLD by race and item model
- **EQE1Loader / EQE1TextureIndex** - mmap'd ETC1 flat packs and the name-to-archive index
- **ConstrainedTextureCache** - LRU-evicting texture cache with memory budget
- **Static UI panels** (`src/client/graphics/ui/`) - Data-driven HUD panels and popups drawn from a UI atlas
- **CombatManager**, **SpellManager**, **SkillManager**, **TradeManager**, **InventoryManager**, **HotbarModel** - Gameplay state
- **AudioManager**, **AudioMixer**, **MidiPlayer**, **SfxManager**, **MusicPlayer**, **ZoneAudioManager** - Audio subsystems
- **DaybreakConnection** - UDP reliable protocol implementation
- **HCMap** / **ZoneBsp** - Zone map collision/LOS raycasting and BSP regions
- **PathfinderNavMesh** / **PathfinderWaypoint** - Optional pathfinding
- **WillEQDatabase** (`data/willeq.db`) - SQLite built by `tools/willeq_db_builder` from client text files and `data/*.json` (spells, strings, race/item models, zone points). Rebuild it with `scripts/build_willeq_db.sh`, never by hand: since T450 the file is byte-reproducible from its inputs and pinned by `data/willeq.db.sha256`, which the `DbReproducible` ctest test checks. A row change means a rebuild plus a new pin line in the SAME commit.

## File Structure

```
src/
  client/
    main.cpp                 # Entry point
    application.cpp          # Application lifecycle, CLI/config parsing
    core/                    # everquest.cpp + eq_chat/eq_strings/eq_player_info (Batch K)
    chat/ commands/ entities/ gm/ group/ guild/ login_world/ merchant/
    movement/ player/ zone/    # the twelve extracted EverQuest components (Batch K)
    action/                  # ActionDispatcher, CommandProcessor (slash commands)
    bridge/                  # Game<->renderer event/intent bridge
    graphics/
      renderer/              # IrrlichtRenderer's own four TUs (Batch L)
      frame/ zone_load/ zone_lifecycle/ zone_stream/ player/ overlays/
      diagnostics/ sim/ slash/ eqe1/ rdp/   # the eleven renderer components (Batch L)
      sim_*.cpp              # SimulationWorker: animation, particles, boids, weather, visibility
      eqe1_loader.cpp        # EQE1 flat-pack loader + texture index
      zone_shader.cpp        # GLSL ES shaders
      eq/                    # EQ file format loaders (PFS/S3D, WLD, DDS, models, animation)
      detail/, environment/  # Detail objects, weather, particles
      rdp/                   # Native RDP server
      ui/                    # Static UI panels, UI atlas, command registry
    audio/                   # Mixer, MIDI, SFX, music, zone audio
    input/                   # Hotkeys, console and graphics input handlers
    hotbar/, inventory/, chat/, spell/, skill/, state/, mode/, output/, help/
  common/
    net/                     # Daybreak protocol, packets
include/                     # Headers matching src layout
tests/                       # gtest executables, one per suite (for the count: ctest --test-dir build -N | tail -1)
config/                      # User settings and data-driven configs
data/                        # willeq.db, eqe1_texture_index.json, model/zone JSON, UI atlases
docker/
  Dockerfile                 # Linux x86_64 build (builder/runtime/vnc stages)
  Dockerfile.arm-noble       # Orange Pi cross-compilation
  Dockerfile.arm64-noble     # 64-bit ARM variant
  irrlicht-drm/              # DRM/KMS device + COpenGLES2Driver (shared ARM/x86)
  irrlicht-drm-x86/          # x86 device fork (surfaceless/X11 EGL)
scripts/
  build-gles2-x86.sh         # Patched Irrlicht bootstrap + x86 GLES2 tree
  build-arm-noble.sh         # ARM cross-build
  start-with-vnc*.sh         # Xvfb + VNC launchers
  ci/                        # Visual gate, capture encoder, reachability checks
  pi/, eqemu/                # Orange Pi benchmarking, test-server control
tools/                       # eqe1_builder, verify_eqe1, willeq_db_builder, model_viewer,
                             # S3D/WLD dumpers, atlas builders, hwcaps/ GPU benchmarks
third_party/                 # Header-only audio libs; irrlicht-gles2-x86/ (gitignored build output)
docs/                        # decisions.md, module_map.md, standards/, reference/, plans/
refactor/                    # BOARD.md (open items), per-unit plans and reviews
```

## Project Process

Work is organised as numbered units. An orchestrator session plans each unit as a document in `refactor/` (`T###_<name>.md` / `J##_*.md` / `K##_*.md`: territory, plan, review), and executor agents carry them out in git worktree slots (`wt/slot1..3`) on task branches named `task/<unit>-<name>` cut from the `fable` branch; the orchestrator reviews and merges. Standing rules for executors are in `refactor/EXECUTOR_PROTOCOL.md`, settled architecture decisions in `docs/decisions.md`, and open items in `refactor/BOARD.md`. Per-directory `CLAUDE.md` files carry the working notes for each subsystem.

## Credits

This project builds upon the work of several open-source projects:

- **[EQEmu](https://github.com/EQEmu/Server)** - EverQuest server emulator. Network protocol implementation and packet structures derived from EQEmu's extensive reverse-engineering work.
- **[LanternExtractor](https://github.com/LanternEQ/LanternExtractor)** - EQ asset extraction tool. Referenced for S3D/WLD file format parsing and zone geometry extraction.
- **[eqsage](https://github.com/xackery/eqsage)** - EQ asset viewer. Referenced for model loading and animation handling.
- **[eqrequiem](https://github.com/jamfestemq/eqrequiem)** - EQ client project. Referenced for Titanium protocol details and client behavior.

## License

This project is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE) for details.

EverQuest is a registered trademark of Daybreak Game Company LLC.
