# UI Components

## Windows

- `WindowManager` - Manages all UI windows, handles input routing
- `WindowBase` - Base class for draggable, resizable windows
- `ChatWindow` - Scrollable chat with input field, channel filtering, clickable links
- `InventoryWindow` - Player inventory grid with equipment slots
- `BagWindow` - Container bag contents display
- `LootWindow` - Corpse loot interface
- `GroupWindow` - Group member display with HP/mana bars
- `PetWindow` - Pet status display with command buttons
- `VendorWindow` - Merchant buy/sell with sorting and pricing
- `BankWindow` - Bank slots, shared bank, currency conversion
- `TradeWindow` - Player trading interface with item/money slots
- `TradeskillContainerWindow` - Tradeskill combines
- `SkillsWindow` - Player skills list with activation and cooldown indicators
- `SkillTooltip` - Skill details on hover (category, value, cooldown, requirements)
- `ItemTooltip` - Item stat display on hover
- `ItemIconLoader` - Loads item icons from EQ client files
- `CommandRegistry` - Slash command registration and dispatch
- `ChatMessageBuffer` - Ring buffer for chat history with channel support

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
