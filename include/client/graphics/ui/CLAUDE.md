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
| 1-8 | Hotbar slots 1-8 |
| 9-0 | Hotbar slots 9-10 |
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

**Chat:**
- `/say`, `/shout`, `/ooc`, `/auction`, `/gsay`, `/gu` - Channel messages
- `/tell <name> <msg>` - Private messages
- `/emote <text>` - Emotes
- `/filter [channel]` - Toggle channel display (say, tell, group, guild, shout, auction, ooc, emote, combat, exp, loot, npc, all)

**Movement:**
- `/loc` - Show current location
- `/sit`, `/stand` - Sit/stand
- `/camp` - Sit down and logout after 30 seconds (stand to cancel)
- `/move <x> <y> <z>` - Move to coordinates
- `/moveto <name>` - Move to entity
- `/follow <name>`, `/stopfollow` - Follow entity

**Combat:**
- `/target <name>` - Target entity
- `/attack`, `/stopattack` - Toggle attack
- `/aa` - Toggle auto-attack

**Group:**
- `/invite [name]` - Invite target or named player to group
- `/follow [name]` - Accept group invite from player
- `/disband` - Leave current group
- `/decline` - Decline pending group invite

**Spells:**
- `/cast <gem#>` - Cast spell from gem slot (1-8)
- `/mem <gem#> <spell_name>` - Memorize spell to gem slot
- `/forget <gem#>` - Forget spell from gem slot
- `/gems` - Show memorized spells
- `/interrupt` - Interrupt current cast
- `/spellbook` - Open spellbook window

**Skills:**
- `/skills` - Toggle skills window

**Pet:**
- `/pet <command>` - Issue commands to your pet (attack, back, follow, guard, sit, taunt, hold, focus, health, dismiss)

**Trading:**
- `/trade` - Request trade with target
- Trade window opens when accepting trade requests

**Audio:**
- `/music [on|off]` - Toggle or set music playback
- `/sound [on|off]` - Toggle or set sound effects
- `/volume [0-100]` - Show or set master volume (alias: `/vol`)
- `/musicvolume [0-100]` - Show or set music volume (alias: `/mvol`)
- `/effectsvolume [0-100]` - Show or set effects volume (aliases: `/evol`, `/sfxvol`)

**Utility:**
- `/help [command]` - Show help
- `/who` - List nearby entities
- `/quit` - Show exit options
- `/q` - Exit client immediately
- `/debug <level>` - Set debug level (0-6)
- `/timestamp` - Toggle chat timestamps
- `/pmem` - Show memory usage breakdown across all subsystems (aliases: `/memory`, `/mem_report`)
