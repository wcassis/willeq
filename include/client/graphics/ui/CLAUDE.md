# Static UI System

The old WindowManager/WindowBase draggable window system was completely removed (Batch U07c, ~35K lines deleted). The new UI uses static screen-region panels rendered via batched quads.

## Components

- `UIRenderer` - Batched quad drawing API. Accumulates `SpriteQuad` structs per frame, flushes in `endFrame()`. Supports atlas-backed sprites or solid color fallback. Methods: `drawRect()`, `drawSprite()`, `drawBar()`, `drawPanel()`.
- `UILayout` - Anchor-based screen region definitions. `computeLayout(width, height)` positions all panels for any resolution. Constants: MARGIN=4, BAR_HEIGHT=14, SLOT_SIZE=32.
- `StaticPanels` - Free render functions (not a class hierarchy) for each panel. Each takes a `UIRenderer`, `UILayout`, and a plain data struct. No state ownership.
- `StaticUIInput` - Mouse/keyboard input handler for static UI. Communicates with game thread via `GameStateBridge` intents. Hit-tests slot grids for inventory, hotbar, spell gems.
- `UIAtlas` - Loads pre-generated sprite atlas (`ui_atlas.png` + `ui_atlas.json`). Provides `UISprite` enum (SlotBackground, BarHP, PanelBorder, etc.) with source rects.
- `CommandRegistry` - Slash command registration and dispatch. Case-insensitive lookup, alias support, tab completion, category-based help.

## Panels

| Panel | Location | Data Struct | Render Function |
|-------|----------|-------------|-----------------|
| Player status | Top-left | `PlayerStatsData` | `renderPlayerStatus()` |
| Target info | Top-right | `TargetInfoData` | `renderTargetInfo()` |
| Chat | Bottom-left | `ChatPanelState` | `renderChatPanel()` |
| Hotbar | Bottom-center | `HotbarPanelState` (10 slots) | `renderHotbar()` |
| Spell gems | Bottom-right | `SpellGemPanelState` (8 gems) | `renderSpellGemPanel()` |
| Buff bar | Above chat | `BuffBarState` (max 25) | `renderBuffBar()` |
| Casting bar | Center-bottom | `CastingBarState` | `renderCastingBar()` |
| Group | Left below player | `GroupPanelState` (5 members) | `renderGroupPanel()` |
| Pet | Below group | `PetPanelState` | `renderPetPanel()` |
| XP bar | Full width bottom | `XPBarState` | `renderXPBar()` |
| Inventory | Center popup | `InventoryPanelState` | `renderInventoryPopup()` |
| Spellbook | Center popup | `SpellbookPopupState` | `renderSpellbookPopup()` |
| Skills | Center popup | `SkillsPopupState` | `renderSkillsPopup()` |

Center popups (inventory, spellbook, skills, vendor, bank, loot, trade) share the same screen region and are mutually exclusive.

## Design Patterns

- **Data-driven rendering**: All panel data is cached in plain structs, updated via bridge events, passed to stateless render functions
- **No class hierarchy**: Free functions replace the old WindowBase inheritance tree
- **Atlas fallback**: UIRenderer draws atlas sprites when loaded, solid color rectangles otherwise
- **Bridge-only input**: StaticUIInput sends intents to GameStateBridge, never accesses game state directly

## Key Bindings

**Global:**
| Key | Action |
|-----|--------|
| F12 | Screenshot |
| LMB+drag | Look around (camera) |
| RMB+drag | Look around (camera) |
| Ctrl+LMB+drag | Look around (single-button mouse) |
| Shift+ESC | Quit |
| Ctrl+F1 | Toggle wireframe |
| Ctrl+F2 | Toggle HUD |
| Ctrl+F3 | Toggle name tags |
| Ctrl+F4 | Toggle zone lights |
| Ctrl+F5 | Cycle camera mode |
| Ctrl+F6 | Toggle Classic/Luclin models |

**Player Mode:**
| Key | Action |
|-----|--------|
| WASD/Arrows | Move (with collision) |
| Ctrl+A / End | Strafe left |
| Ctrl+D / PageDown | Strafe right |
| Q | Toggle auto-attack |
| Ctrl+Q | Attack (initiate combat) |
| ` / NumLock / Numpad+ | Toggle autorun |
| Space | Jump |
| 1-8 | Hotbar slots 1-8 (routed by slot content type since T174: Spell → cast, Skill → useSkill; Item and Emote slots are logged and skipped) |
| 9-0 | Hotbar slots 9-10 (same routing) |
| Alt+1-8 | Cast spell from gem 1-8 |
| F1 | Target self |
| F2-F6 | Target group member 1-5 |
| F7 | Target nearest PC |
| F8 | Target nearest NPC |
| Tab | Cycle targets |
| Shift+Tab | Cycle targets reverse |
| C | Consider target |
| R | Reply to last tell |
| I | Toggle inventory |
| K | Toggle skills window |
| G / Alt+P | Toggle group window |
| P | Toggle pet window |
| Ctrl+B | Toggle spellbook |
| Alt+B | Toggle buff window |
| U | Interact (nearest door/object) |
| H | Hail (say "Hail" or "Hail, <target>") |
| L | Cycle object lights |
| ESC | Clear target |
| +/- | Camera zoom in/out |
| Ctrl+Alt+C | Toggle collision |
| Ctrl+Z | Toggle zone line visualization |
| Enter | Open chat input |
| / | Open chat with slash |

Key bindings are configurable via `config/hotkeys.json` and managed by `HotkeyManager` (see `src/client/input/CLAUDE.md`).

## Slash Commands

Three vocabularies exist (`docs/decisions.md` 2026-08-03 ruling; T97–T143
migration); `Application::DispatchSlashCommand` tries them in order:

1. **Registry 1 — game registry** (`EverQuest::m_command_registry`,
   `src/client/eq_commands*.cpp`): after T139 deleted 34 redundant copies
   and T141 migrated `/equip`, only seven remain: `/hotkeys`, `/pmem`
   (aliases `/memory`, `/mem_report`), `/music`, `/sound`, `/volume`
   (`/vol`), `/musicvolume` (`/mvol`), `/effectsvolume` (`/evol`, `/sfxvol`).
2. **Registry 2 — `action::CommandProcessor`**
   (`src/client/action/command_processor.cpp`, 134 registrations at
   `e70e3347`): the real slash vocabulary, listed below by its own category
   strings. Derived from the `registerCommand({...})` calls — regenerate
   from the source, do not hand-edit.
3. **Registry 3 — renderer if-chain** (`IrrlichtRenderer::processSlashCommand()`,
   reached via the `RendererCommand` bridge queue): ~70 debug/graphics
   toggles (`/sort`, `/portal`, `/plight`, `/detail`, `/screenshot`,
   `/dumpscene`, `/newskills`, `/newinv`, `/newbook`, `/weatherpreset`, …)
   — see `src/client/graphics/CLAUDE.md`. Not queryable by `/help` (T143).

`/help [command]` (registry-aware since T143 `e979a821`) lists registries
1+2 deduplicated.

**Chat:** `/say` (`/s`), `/shout` (`/sh`, `/sho`), `/ooc` (`/o`), `/auction`
(`/auc`), `/gsay` (`/g`), `/gu` (`/guildsay`), `/tell <player> <msg>` (`/t`,
`/msg`), `/reply <msg>` (`/r`), `/emote <action>` (`/em`, `/me`), `/wave`,
`/dance`, `/cheer`, `/laugh`.

**Movement:** `/loc` (`/location`), `/sit`, `/stand`, `/move <x> <y> <z>`
(CLIENT frame — swap x/y if reading a server-frame `/loc`), `/moveto <name>`,
`/warp [x y z | name]`, `/zoning [on|off]`, `/follow <name>`, `/stopfollow`
(`/stop`), `/face <name|x y z>`, `/turn <degrees>`, `/rewind`, `/walk`, `/run`,
`/sneak`, `/crouch` (`/duck`), `/feign` (`/fd`), `/jump`.

**Combat:** `/target <name>` (`/tar`), `/attack` (`/att`), `/stopattack`,
`/aa` (`/~`), `/combat [on|off]`, `/consider` (`/con`), `/hail`, `/assist
[target]` (`/a`), `/taunt`, `/useskill <id|name>` (`/skill`; class-cap gate
T156/T159/T162), `/autofire` (`/af`), `/shield`, `/duel`, `/duelaccept`,
`/dueldecline`, `/hunt [on|off]`, `/autoloot [on|off]`, `/listtargets`,
`/loot <corpseId|corpse name>` (opens; with a loot window open `/loot
<slot|item name>` takes ONE item), `/lootall`, `/lootclose` (`/closeloot`)
(T268). There is no loot popup in the static UI; the loot window is
command-driven (its intents are the same ones a popup would push).

**Group:** `/invite [name]` (`/inv`), `/groupaccept` (`/gaccept`), `/decline`,
`/disband`, `/split <pp> [gp] [sp] [cp]`, `/groupassist [name]` (`/ga`),
`/raid status|list|invite|accept|leave|remove|leader|loot|looter|lock|unlock|group`
(T179–T184; `remove` gated client-side on leadership — Will's ruling, T183).

**Guild:** `/guildinvite` (`/ginvite`), `/guildaccept` (`/acceptguild`),
`/guilddecline` (`/declineguild`), `/guildremove` (`/gremove`, `/gkick`),
`/guilddemote` (`/gdemote`), `/guildleader` (`/gleader`), `/guildmotd [msg]`
(`/gmotd`).

**Social:** `/who [name] [level range]` (`/whoall`), `/inspect [name]`
(the client also answers inbound inspects with `OP_InspectAnswer`, T170),
`/inspectmessage [text|-clear]` (`/inspectmsg`, T171), `/trade [name]`,
`/accept`, `/tradedecline` (`/rejecttrade`), `/canceltrade` (`/stoptrade`),
`/lfg`, `/surname <name>`, `/random [low] [high]` (`/roll`), `/yell`, `/find
[name]` (`/findperson`), `/friendswho <names>` (`/friends`).

**Character:** `/appearance <field> <0-255> ...` (T153/T167).

**Spells:** `/cast <gem#>`, `/mem <gem#> <spell>` (`/memspell`, `/memorize`),
`/forget <gem#>`, `/gems`, `/interrupt` (`/ducking`), `/spellbook` (`/book`).

**Skills:** `/bandage` (`/bindwound`; inbound reply parsed since T177),
`/train`, `/track [name]`. The skills window toggle is `/newskills`
(registry 3); the old `/skills` was a dead stub and was deleted (T138/T139).

**Pet:** `/pet <command> [target]` (attack, back, follow, guard, sit, taunt,
hold, focus, health, dismiss).

**Corpse:** `/corpsedrag <name>` (`/drag`), `/corpsedrop` (`/drop`),
`/consent <player>`, `/deny <player>`.

**Tradeskill (T149):** `/recipesearch` (`/recipes`), `/recipedetails`
(`/recipeinfo`), `/recipecombine` (`/autocombine`), `/recipefavorites`
(`/recipefav`), `/clickobject <drop_id>` (`/clickobj`).

**Commerce / Inventory:** `/vendor`, `/vendorlist`, `/buy <slot> [qty]`,
`/sell <slot> [qty]`, `/bank`, `/equip <from> <to> | <item name> | list`.

**Utility:** `/help [command]`, `/quit` (`/exit`, `/q`), `/camp`, `/save`,
`/afk`, `/anon`, `/roleplay [on|off]` (`/rp`), `/list [search]`, `/dump
<name>`, `/npcstate`, `/door` (`/u`), `/rezaccept` (`/acceptrez`),
`/rezdecline` (`/declinerez`), `/bug <text>` (`/report`), `/petition <text>`,
`/time [hour]` (`/settime`), `/pathfinding [on|off]`, `/debug <0-6>`, `/perf`
(`/performance`, `/metrics`), `/logonly`, `/logexclude`, `/logmodule
<MOD:LEVEL>`, `/logclear`.

**GM (T148; needs `m_gm_mode`, which is NOT a mirror of server-side
`Client::Admin()`):** `/gmzone <name> <zone_id>`, `/summon <name>`, `/goto
<name>`, `/gmfind <name>`, `/kick <name>`, `/kill <name>`, `/hideme [on|off]`
(misrouted — open defect), `/emotezone <msg>`, `/gmlastname <name> <last>`.

**Audio (registry 1):** `/music [on|off]`, `/sound [on|off]`, `/volume
[0-100]` (`/vol`), `/musicvolume` (`/mvol`), `/effectsvolume` (`/evol`,
`/sfxvol`). **Diagnostics (registry 1):** `/pmem` (`/memory`,
`/mem_report`), `/hotkeys`.
