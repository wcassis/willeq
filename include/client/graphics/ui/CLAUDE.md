# Static UI System

The old WindowManager/WindowBase draggable window system was completely removed (Batch U07c, ~35K lines deleted). The new UI uses static screen-region panels rendered via batched quads.

## Components

- `UIRenderer` - Batched quad drawing API. Accumulates `SpriteQuad` structs per frame, flushes in `endFrame()`. Supports atlas-backed sprites or solid color fallback. Methods: `drawRect()`, `drawSprite()`, `drawBar()`, `drawPanel()`.
- `UILayout` - Anchor-based screen region definitions. `computeLayout(width, height)` positions all panels for any resolution. Constants: MARGIN=4, BAR_HEIGHT=14, SLOT_SIZE=32.
- `StaticPanels` - Free render functions (not a class hierarchy) for each panel. Each takes a `UIRenderer`, `UILayout`, and a plain data struct. No state ownership.
- `StaticUIInput` - Mouse/keyboard input handler for static UI. Communicates with game thread via `GameStateBridge` intents. Hit-tests slot grids for inventory, hotbar, spell gems. Its `processKey()` (ESC dismisses the top popup) had NO production caller until T290 wired it into `renderer_frame.cpp`'s escape handling, ahead of clear-target. **Scrolling (T313):** `processWheel()` / `processScrollKey()` / `runScrollCommand()` all funnel into ONE router — `scrollTargetAt()` picks the panel under the cursor (loot > skills > chat, the draw order), `scrollPanel()` moves it. Before T313 there was no scroll writer of any kind: chat scrollback, the skills list and the loot list all honoured a `scrollOffset` in both their render body and their hit test that nothing ever wrote, so a corpse with more than 8 items was partly UNLOOTABLE. **The offset conventions differ and are not harmonised** — chat counts lines up from the BOTTOM (0 = newest), skills and loot count rows down from the TOP; `scrollPanel()`'s `lines > 0` means "towards older content" either way. The clamps stay with the panels: `renderChatPanel()` owns chat's upper bound (it is the only code that knows the wrapped-line count), `SkillsPopupState::maxScrollOffset(visibleRows)` and `LootPanelState::maxScrollOffset()` own the other two.
- `UIAtlas` - Loads pre-generated sprite atlas (`ui_atlas.png` + `ui_atlas.json`). Provides `UISprite` enum (SlotBackground, BarHP, PanelBorder, etc.) with source rects.
- `CommandRegistry` - Slash command registration and dispatch. Case-insensitive lookup, alias support, tab completion, category-based help. **Tab completion is reached from the chat box** (T357/B057): `FrameComponent::applyCommandCompletion()` in `src/client/graphics/frame/renderer_frame_input.cpp` is the Tab arm of `processChatInput()`'s key drain, and it asks the provider `Application::initRendererAndGraphics()` installs, which merges `CommandRegistry::getCompletions()`, `action::CommandProcessor::getCompletions()` and (T365/B091) the static renderer catalog's `eqt::help::RendererCommandCompletions()` (`Application::commandCompletions()`) -- **all three** registries, the same three `/help` lists, so the two cannot list different commands. Until T365 this said "the same pair", and it was wrong: `/help`'s `BuildCategoryOverview` has always appended registry 3's whole Graphics category, so Tab and `/help` differed by ~70 commands and none of the renderer's own slash set was completable. T362 (B069) removed the last drift between the pair: both `getCompletions()` bodies (and both `toLower`s) now call the one shared helper in `include/client/command_completion.h`, which sorts AND de-duplicates -- `CommandRegistry` used to sort only, so a command registered with its own name as an alias was offered twice.

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
| Loot | Center popup | `LootPanelState` | `renderLootPopup()` (T290) |
| Tradeskill container | Center popup | `TradeskillPanelState` | `renderTradeskillPopup()` (T415, board B321) |

Center popups (inventory, spellbook, skills, vendor, bank, loot, trade, tradeskill container) share the same screen region and are mutually exclusive.

**The tradeskill panel takes its geometry from the oracle's XML**, the way the purse and the paperdoll do:
`include/client/graphics/ui/tradeskill_layout.h` derives `EQUI_TradeskillWnd.xml`'s anchor-positioned
container half (`COMBW_ComponentArea`'s ten `COMBW_RequiredIcon` 42x42 cells, `COMBW_ContainerName`,
`COMBW_CombineButton`, `COMBW_CloseButton`) into absolute rects in the oracle's 400x525 window and maps
them proportionally onto `centerPopup`. The panel's `.cpp` computes no rect of its own.

## Design Patterns

- **Data-driven rendering**: All panel data is cached in plain structs, updated via bridge events, passed to stateless render functions
- **No class hierarchy**: Free functions replace the old WindowBase inheritance tree
- **Atlas fallback**: UIRenderer draws atlas sprites when loaded, solid color rectangles otherwise. **The two paths are not interchangeable and the client only ever runs the atlas one** — every capture harness before T275 ran the fallback (`ui.init(driver, nullptr, ...)`), which is why the atlas path shipped for years with the whole UI painting opaque black (T275: an incomplete ETC1 mip chain sampled black, and `endFrame()` discarded every per-quad tint). `tests/test_ui_atlas_pixels.cpp` pins the atlas path against a real GLES2 device + the real `ui_atlas`; `ui_inventory_capture --eqe1-path <dir>` captures it
- **Bridge-only input**: StaticUIInput sends intents to GameStateBridge, never accesses game state directly

## Key Bindings

**Global:**
| Key | Action |
|-----|--------|
| F11 | Toggle ALL UI (T301: also hides the static panels -- chat, hotbar, gems, popups, XP bar; before T301 it gated only the casting bars, the legacy `guienv_` draw and the FPS counter, so with the static UI on it hid nothing. Scriptable as `/hideui`.) |
| F12 | Screenshot |
| PageUp / PageDown | Scroll the static-UI panel under the cursor one page (chat scrollback, the skills list, the loot list). T313; scriptable as `/scroll`. PageDown used to be a second StrafeRight binding — that moved to Home, pairing with StrafeLeft's End, so paging a loot list does not also strafe. |
| Mouse wheel | Scroll that same panel 3 lines per notch. T313: `EMIE_MOUSE_WHEEL` had no case in `RendererEventReceiver::OnEvent()` at all before this, so the wheel was discarded client-wide — camera zoom is the `CameraZoomIn/Out` hotkeys, never the wheel, and the static UI is the wheel's sole consumer. |
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
| Ctrl+D / Home | Strafe right (T313 moved this off PageDown, which now scrolls the UI) |
| Q | Toggle auto-attack |
| Ctrl+Q | Attack (initiate combat) |
| ` / NumLock / Numpad+ | Toggle autorun. Live since T343 and live-proven by T345 (board B002 CLOSED): the route is RENDERER-side — the receiver queues `RA::ToggleAutorun` on `actionQueue_`, `processCommonInput()` flips `PlayerCameraComponent::toggleAutorun()`, and movement phase 1 cancels it on a translational key (turning does not cancel). The game-side `EqActionHandler::toggleAutorun()` survives for the console handler alone |
| Space | Jump |
| 1-8 | Hotbar slots 1-8 (routed by slot content type: Spell → cast, Skill → useSkill, Item → useItemById (T230), Emote/Macro/Combat → activateHotbarCommandSlot (T239)) |
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
| Alt+P | Toggle group window |
| Alt+F1 | Toggle pet window |
| Ctrl+B | Toggle spellbook |
| Alt+B | Toggle buff window |
| U | Interact (nearest door/object) |
| H | Hail (say "Hail" or "Hail, <target>") |
| L | Cycle object lights |
| ESC | Clear target. `/cleartarget` (no alias, category Combat) reaches the same `ActionDispatcher::clearTarget()` since T347 (board B012 CLOSED), so a `--script` run can clear a target |
| +/- | Camera zoom in/out |
| PageUp / PageDown | Scroll the UI panel under the cursor (T313) |
| Ctrl+Alt+C | Toggle collision |
| Ctrl+Z | Toggle zone line visualization |
| Enter | Open chat input |
| / | Open chat with slash |

Key bindings are configurable via `config/hotkeys.json` and managed by `HotkeyManager` (see `src/client/input/CLAUDE.md`).

**T368 (B101): the group, pet and buff rows above were STALE until T368.** All
three reached empty `case` arms in `renderer_frame_input.cpp` -- "visibility is
driven by inGroup state", and so on -- so the group, pet and buff keys did
nothing whatsoever. Each now flips a `bool isOpen` on its panel state
(`FrameComponent::toggleGroupWindow()` / `togglePetWindow()` /
`toggleBuffWindow()`, the `toggleSkillsWindow()` shape), and each panel's
render function early-returns on a free predicate
(`groupPanelIsVisible()` / `petPanelIsVisible()` / `buffBarIsVisible()` in
`static_panels.h`) that composes the toggle with the auto-driven state:

| Panel | Draws when |
|---|---|
| Group | `isOpen` alone -- an open window with no group draws its bare title bar (T382); joining a group never reopens a closed one |
| Pet | `isOpen && hasPet` |
| Buff bar | `isOpen && at least one buff` (the bar has no frame of its own) |

`isOpen` defaults to **true** for all three, which is the official client's
default: its `defaults.ini` writes a `Show=0` key only under a section whose
window ships closed (`[SelectorWindow]`, `[HotButtonWnd3]`, `[HotButtonWnd4]`)
and writes none under `[GroupWindow]`, `[PetInfoWindow]` or `[BuffWindow]`. The
official ACTION names are `TOGGLE_PARTYWIN`, `TOGGLE_PETINFOWIN` and
`TOGGLE_BUFFWIN` (`KEYMAPPING_` suffixes in `eqgame.exe`).

**T382 (B170/B115): the three KEYS are now the oracle's own.** They are
recorded in no file on disk, so T377 screenshotted the official client's
Options > Keys > **UI** page instead
(`review/T377/oracle/T377_oracle_options_keys_UI_page1..4.png`): party window
**`Alt P`**, buff window **`Alt B`**, pet info window **`Alt F1`**, each with an
EMPTY Alternate column. fable's `G` (group) was an invention and its `P` (pet)
was the official key for **Toggle Customer Service Win**; both are gone.
`Alt+B` was already right. The empty group window's SHAPE was measured in the
same session -- the oracle paints only its title bar, which is what
`groupPanelFrameRect()` now returns with no group.

## Slash Commands

Three vocabularies exist (`docs/decisions.md` 2026-08-03 ruling; T97–T143
migration); `Application::DispatchSlashCommand` tries them in order:

1. **Registry 1 — game registry** (`EverQuest::m_command_registry`,
   registered by `eqt::commands::CommandsComponent::RegisterCommands()` in
   `src/client/commands/` — K12 moved it off the old flat `eq_commands*.cpp`):
   after T139 deleted 34 redundant copies
   and T141 migrated `/equip`, only seven remain: `/hotkeys`, `/pmem`
   (aliases `/memory`, `/mem_report`), `/music`, `/sound`, `/volume`
   (`/vol`), `/musicvolume` (`/mvol`), `/effectsvolume` (`/evol`, `/sfxvol`).
2. **Registry 2 — `action::CommandProcessor`**
   (`src/client/action/command_processor_registration.cpp`, **143**
   `registerCommand({...})` calls — T347 inserted `/cleartarget` at entry 022
   and `tests/goldens/command_registration_order_g03.txt` pins both the count
   and the position, so re-read the golden header rather than this line; J04
   split the handlers into
   `command_processor_{social,spells_gm,world,bank,loot,train}.cpp`): the real
   slash vocabulary, listed below by its own category strings. Derived from
   the `registerCommand({...})` calls — regenerate from the source, do not
   hand-edit.
3. **Registry 3 — renderer if-chain** (`SlashCommandsComponent::processSlashCommand()`,
   reached through the one-line `IrrlichtRenderer::processSlashCommand()`
   forwarder, in `src/client/graphics/slash/renderer_slash_commands.cpp` (L06),
   reached via the `RendererCommand` bridge queue): ~70 debug/graphics
   toggles (`/sort`, `/portal`, `/plight`, `/detail`, `/screenshot`,
   `/dumpscene`, `/hideui` (aliases `/toggleui`, `/ui` — the scriptable half
   of F11, T301), `/scroll [chat|skills|loot] [up|down|top|bottom] [lines]`
   (the scriptable half of the wheel and PageUp/PageDown, T313 — a `--script`
   run cannot generate either), `/skills`, `/inventory`, `/newbook`,
   `/weatherpreset`, …)
   — see `src/client/graphics/CLAUDE.md`. It IS queryable by `/help`: T223
   added registry 3's static catalog (`src/client/help/renderer_command_catalog.cpp`)
   and `BuildCategoryOverview` appends the whole Graphics category, tagged
   `(graphics)` because a `--no-graphics` run refuses these. T365 (B091) gave
   the catalog a `RendererCommandCompletions()` too, so registry 3 is now
   tab-completable as well.

`/help [command]` (registry-aware since T143 `e979a821`) lists **all three**
registries: 1 and 2 merged by category and deduplicated, then registry 3's
catalog appended as one `Graphics (graphics):` line (T223). Since T365 (B090)
`/help <word>` resolves the word as a COMMAND NAME first and as a category
second; when both exist it prints the command detail, then a one-line
`"<Word>" is also a category:` note, then the category.

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
(T268). **T290 restored the loot POPUP** (the feature T283 scoped): the
four loot game events (`LootWindowOpened/Closed`, `LootItemAdded/Removed`)
now have real `IrrlichtBridge` arms instead of payload-discarding
`LOG_TRACE`s, feeding `LootPanelState` (`src/client/graphics/ui/loot_panel.cpp`,
ported from the `LootWindow` U07c2 `0f25cb57` deleted); `renderLootPopup()`
draws it last of the center popups, and `hitTestLootRow()` /
`hitTestLootButton()` turn a click into `LootItemIntent` / `LootAllIntent` /
`CloseLootIntent` -- the SAME intents and the same consumer bodies the
`/loot` family uses. Two rules carried over from the old window: the item
list is PACKED so on-screen row != list index != corpse slot (the intent
carries `items[idx].slot`), and the panel never closes itself -- a click
pushes `CloseLootIntent` and the panel stays up until the server's
`LootWindowClosed`. `/autoloot` is ON by default and will empty a corpse
before you can click it (T268).

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
`/train`, `/track [name]`. The skills window toggle is `/skills`
(registry 3), renamed from `/newskills` with NO alias on Will's ruling <!-- RETIRED-OK: tombstone for the old name -->
(T363, B058); the ORIGINAL `/skills` was a dead stub deleted by T138/T139, so
the name was free. Its body is `FrameComponent::toggleSkillsWindow()` --
the K keybind (`HotkeyAction::ToggleSkills`) and piped stdin's bare `skills`
word reach the same one, popup mutual exclusion included.

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
