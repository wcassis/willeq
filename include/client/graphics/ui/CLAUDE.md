# Static UI System

The old WindowManager/WindowBase draggable window system was completely removed (Batch U07c, ~35K lines deleted). The new UI uses static screen-region panels rendered via batched quads.

## Components

- `UIRenderer` - Batched quad drawing API. Accumulates `SpriteQuad` structs per frame, flushes in `endFrame()`. Supports atlas-backed sprites or solid color fallback. Methods: `drawRect()`, `drawSprite()`, `drawBar()`, `drawPanel()`.
- `UILayout` - Anchor-based screen region definitions. `computeLayout(width, height)` positions all panels for any resolution. Constants: MARGIN=4, BAR_HEIGHT=14, SLOT_SIZE=32.
- `StaticPanels` - Free render functions (not a class hierarchy) for each panel. Each takes a `UIRenderer`, `UILayout`, and a plain data struct. No state ownership.
- `StaticUIInput` - Mouse/keyboard input handler for static UI. Communicates with game thread via `GameStateBridge` intents. Hit-tests slot grids for inventory, hotbar, spell gems. Its `processKey()` (ESC dismisses the top popup) had NO production caller until T290 wired it into `renderer_frame.cpp`'s escape handling, ahead of clear-target. **Scrolling (T313):** `processWheel()` / `processScrollKey()` / `runScrollCommand()` all funnel into ONE router — `scrollTargetAt()` picks the panel under the cursor (loot > skills > chat, the draw order), `scrollPanel()` moves it. Before T313 there was no scroll writer of any kind: chat scrollback, the skills list and the loot list all honoured a `scrollOffset` in both their render body and their hit test that nothing ever wrote, so a corpse with more than 8 items was partly UNLOOTABLE. **The offset conventions differ and are not harmonised** — chat counts lines up from the BOTTOM (0 = newest), skills and loot count rows down from the TOP; `scrollPanel()`'s `lines > 0` means "towards older content" either way. The clamps stay with the panels: `renderChatPanel()` owns chat's upper bound (it is the only code that knows the wrapped-line count), `SkillsPopupState::maxScrollOffset(visibleRows)` and `LootPanelState::maxScrollOffset()` own the other two.
- `ItemIconSheets` - **T430 (board B376)**: the item icon art, lifted from the official client's own
  `uifiles/default/dragitem1..34.tga` by `tools/item_icon_atlas_builder` into two 1024x1024 pack entries
  (`item_icons0`, `item_icons1`). Held by `UIAtlas` (`atlas->icons()`) and reached as
  `UIRenderer::drawItemIcon(rect, iconId)`. The layout — 34 sheets of 6x6 40x40 cells, ids **500..1723**,
  **COLUMN-major within a sheet** because `EQUI_Animations.xml`'s `A_DragItem` carries
  `<Vertical>true</Vertical>` — lives in `include/client/graphics/ui/item_icon_atlas.h`, which the builder
  and the loader both include. The sheets load LAZILY on the first icon drawn (they are 2 x 1024x1024 and
  a session that never opens an item panel should not pay for them), and `drawItemIcon()` returns
  **false** when the pack entry or the id has no art — every caller keeps its pre-T430 text placeholder
  for that case, which is why a headless test or a pack-less client looks unchanged.
  Five draw sites use it: the 22 equipment slots and 8 general slots (`renderInventoryPopup`), the
  tradeskill container cells (`renderTradeskillPopup`), the loot rows (`renderLootPopup`, at 20px in a
  22px row) and `IDW_IconButton` (`renderItemDisplayPopup`, at the oracle's own 40x40 decal rect).
  Since T436 (board **B429** CLOSED) there are three more: the merchant window's 24px icon COLUMN
  (`renderMerchantPopup`, `MW_ItemList`'s first `<Columns>` entry, the one with an empty `<Heading>`),
  the eight bank cells (`renderBankWindow`, at `BW_BankSlot`'s own `<ItemOffsetX>` inset) and the
  cursor attachment (`renderCursorAttachment`, at `CA_Anim`'s own 40x40 -- a 1:1 blit, like
  `IDW_IconButton`).
- `UIAtlas` - Loads pre-generated sprite atlas (`ui_atlas.bmp` + `ui_atlas.json`, both packed into `willeq.eqe1` by `tools/ui_atlas_builder`).
  Provides the `UISprite` enum (SlotBackground, BarHP, PanelBackground, White, the four coin decals, ...) with source rects.
  **The roster is fifteen sprites and every one of them has a consumer** — T424 (board B351) deleted `SlotBorderSelected`,
  `ButtonNormal`, `ButtonPressed`, `ButtonDisabled`, `PanelBorder` and `ScrollIndicator`, which T416's census found unreferenced.
  They were procedural chrome for the WindowManager UI deleted in `0f25cb57`, not oracle art. A new region has to appear in the
  enum, in `UIAtlas::mapSprite`'s nameMap, in the builder AND in `tests/test_ui_sprite_roster.cpp`, and adding one rebuilds the
  shared pack (`scripts/build_willeq_pack.sh` + `data/willeq.eqe1.sha256`, same commit).
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
| Tradeskill container | Center popup | `TradeskillPanelState` | `renderTradeskillPopup()` (T415, board B321; T419 added the recipe list on the left, board B335 — `renderTradeskillRecipeList()` in `src/client/graphics/ui/tradeskill_recipe_list.cpp`) |
| Combat abilities | Top-right, under target, left of the gems | `CombatAbilityPanelState` | `renderCombatAbilityPanel()` (T415, board B320) |
| Item display | Its own absolute (0,80), 400x190 | `ItemDisplayPanelState` | `renderItemDisplayPopup()` (T421, board B298 item half) |
| Options | Centred, the oracle's own 400x452, seven tabs | `OptionsPanelState` | `renderOptionsWindow()` (T434) |
| Merchant | Center popup (the oracle's 374x480 scaled onto it) | `MerchantPanelState` | `renderMerchantPopup()` (T436, board B429) |
| Bank | Centred, the oracle's own 108x347, unscaled | `BankPanelState` | `renderBankWindow()` (T436, board B429) |
| Cursor item | At the mouse, the oracle's 50x50 box | `CursorAttachmentState` | `renderCursorAttachment()` (T436, board B429) |

Center popups (inventory, spellbook, skills, merchant, loot, trade, tradeskill container) share the same screen region and are mutually exclusive. **The BANK is not one of them** (T436): the oracle's `BankWnd` is `Style_Sizable false` at its own 108x347, so like the combat-abilities window it keeps its own size and its own anchor and draws above the centre popups. The combat abilities window is NOT one of them -- like the buff/group/pet panels it is a HUD window with its own anchor and its own Alt+C toggle. **Neither is the item display window** (T421): `EQUI_ItemDisplay.xml` gives it `RelativePosition false` and `<Location>(0,80)</Location>`, so it is drawn and hit-tested LAST, on top of every centre popup, and ESC dismisses it before them. **Nor is the Options window** (T434), for the same reason and one level higher still: `EQUI_OptionsWindow.xml` is `RelativePosition false` at `<Location>(90,47)</Location>`, so it is drawn and hit-tested after even the item display and ESC dismisses it first of all. Its geometry is `options_layout.h`, a table of every control the oracle XML declares; a control whose `OptionsLayout::Setting` is `None` has no backing setting in fable, is drawn greyed and is **not hit-testable** (the T363 rule made mechanical). Three are wired: `OGP_MusicVolumeSlider`, `OGP_SoundVolumeSlider` and `ODP_ClipPlaneSlider`.

**The three commerce windows** (T436, board **B429** CLOSED). Three producers filled an `iconId`
nothing could draw, because the merchant window, the bank window and the cursor attachment did not
exist -- the commerce vocabulary was reachable only as `/vendor`, `/vendorlist`, `/buy`, `/sell`,
`/bank`. Each takes its geometry from a header that quotes its own oracle XML and computes no rect in
its `.cpp`: `merchant_layout.h` (`EQUI_MerchantWnd.xml`), `bank_layout.h` (`EQUI_BankWnd.xml`),
`cursor_attachment_layout.h` (`EQUI_CursorAttachment.xml`).

- **Merchant** — `Style_Sizable true` and 505px tall from its own `<Location>`, which does not fit
  640x480, so it is mapped proportionally onto `centerPopup`, T415's rule. It is the LAST centre
  popup. **`MW_Buy_Button` and `MW_Sell_Button` carry byte-identical anchors in the shipped file**, so
  the official client can only ever show one; fable draws Sell when an item is on the cursor and Buy
  otherwise, which is a design choice made to fit that shared rect and is flagged as one. Buy pushes
  `VendorBuyIntent`, Sell `VendorSellIntent` (the cursor slot), Done `VendorToggleIntent` — all three
  pre-existing, the same intents `/buy`, `/sell` and `/vendor` push. `MW_DoneButton` (no underscores)
  is declared in the file and is in NO `<Pieces>` list, so the oracle never instantiates it and
  neither does fable.
- **Bank** — `Style_Sizable false` at 108x347, so it is drawn UNSCALED at the oracle's own offsets,
  T415's other rule; it is CENTRED rather than at the oracle's `<Location>` (0,25), the same flagged
  anchor deviation T434 and T415 made. **Eight slots, not sixteen**: `inventory_constants.h`'s
  `BANK_COUNT = 16` is a post-Velious bank, and the era file declares `BW_BankSlot0..7`.
  `BW_SharedBankSlot0/1`, `BW_SharedMoney0`, `BW_SharedBankLabel` and `BW_AltStorageButton` are IN the
  `<Pieces>` list and carry a `<ScreenID>` and nothing else — no `<Location>`, no `<Size>` — so the
  era file itself declares them as geometry-less stubs and fable draws none of them. `BW_ChangeButton`
  is drawn greyed and is NOT hit-testable (board **B441**). `BW_DoneButton` and the close box push
  **`CloseBankIntent`**, the unit's one new intent, whose handler is the existing `CloseBankWindow()`.
- **Cursor attachment** — `CA_Anim` at (5,5) 40x40 in a 50x50 box that follows the mouse, drawn LAST
  of everything (it is the pointer). `Style_Transparent true` / `Style_Border false`, so no plate and
  no frame is drawn under it. There is no count label anywhere in that screen, which is the same fact
  T413 recorded when it dropped `CursorItemChangedData::queueSize` (board B296) — a stack's quantity
  is simply never shown there. `CursorItemChangedData` is NOT the feed (it carries one bool and a
  `sizeof` test pins that): the feed is the enriched `InventorySlotChanged` for slot **30**, which the
  bridge's arm had been dropping along with the bank's 2000..2007.

None of the three closes itself. Each pushes the intent that ends the SERVER-side session and waits
for the server's own `VendorWindowClosed` / `BankWindowClosed`, T290's rule; ESC follows the same
top-down order the draw does (Options, item display, bank, merchant, then the centre popups). There is
still no way to MOVE an item into or out of a bank slot: that is a drag between the slot and the
cursor and fable's static UI latches no drag — board **B442**.

**Text entry** (T421, board B365). There is exactly ONE text-entry widget in the static UI: `EQT::Graphics::TextEntryState` and `applyTextEntryKey()` (`include/client/graphics/ui/text_entry.h`). `ChatPanelState` DERIVES from it -- `inputText` / `cursorPos` / `inputActive` are its members -- and the tradeskill window's three Editboxes (`COMBW_MinTrivialEdit`, `COMBW_MaxTrivialEdit`, `COMBW_SearchTextEdit`) are three more of them, selected by `TradeskillPanelState::focusedField`. The KEYBOARD GATE is `FrameComponent::processChatInput()`, still the only caller of `setChatInputFocused()`: it early-returns into `processTradeskillSearchInput()` when a search box holds the caret, so the two key drains are mutually exclusive and no keystroke is read twice (`tests/test_read_and_reset_ownership.cpp` records that as `popKeyEvent`'s two-sites-one-consumer exception). Enter in a search box, or `COMBW_SearchButton`, pushes `RecipeSearchIntent` into the existing `SendRecipesSearch()`; the favourites row pushes `RecipeFavoriteIntent` into the existing `SendRecipesFavorite()` (with board **B377** on the missing favourites set).

**The item display window** takes its geometry from `include/client/graphics/ui/item_display_layout.h` (which quotes `EQUI_ItemDisplay.xml`) and its TEXT from the GAME thread: `eqt::inventory::BuildItemDisplayText()` composes the description from format strings quoted out of the official client's own `eqstr_us.txt`, and ships the finished lines on the `ItemDisplayShown` event. That is what the official client does too -- the XML's whole body is one `<STMLbox>`. A click on an occupied inventory slot pushes `ItemDisplayRequestIntent`; the oracle uses a RIGHT click there, which fable cannot (the right button is mouselook and the static UI latches no right click).

**The same window shows SPELLS** (T425, board B298 spell half). The oracle has no second display window at all -- there is no `EQUI_SpellDisplay.xml` and no `SpellDisplay` screen anywhere in `uifiles/default`, and `IDW_IconButton`'s own `<NormalDecal>BuffIcons</NormalDecal>` is the SPELL icon sheet -- so a spell reuses `ItemDisplayPanelState`, `renderItemDisplayPopup()`, `ItemDisplayLayout` and the one `ScrollTarget::ItemDisplay`. Its lines are composed game-side by `eqt::spell::BuildSpellDisplayText()` from eqstr 3004/3005/3006/3102/3103/3105/3107/6730/9234 and arrive on the SAME `ItemDisplayShown` event. **SHIFT+left click** on a memorised gem or a spellbook row asks for it (`SpellDisplayRequestIntent`); a plain click still casts and still memorises. The oracle's gesture is a right click, which fable cannot bind, and unlike the inventory slot left click was already taken here.

**The two T415 panels take their geometry from the oracle's XML**, the way the purse and the paperdoll do:
`include/client/graphics/ui/tradeskill_layout.h` derives `EQUI_TradeskillWnd.xml`'s anchor-positioned
container half (`COMBW_ComponentArea`'s ten `COMBW_RequiredIcon` 42x42 cells, `COMBW_ContainerName`,
`COMBW_CombineButton`, `COMBW_CloseButton`) into absolute rects in the oracle's 400x525 window and maps
them proportionally onto `centerPopup`; `include/client/graphics/ui/combat_ability_layout.h` uses
`EQUI_CombatAbilityWnd.xml`'s offsets UNSCALED, because that window is `Style_Sizable false` at 174x150.
Neither panel's `.cpp` computes a rect.

**The pet panel takes its command-grid geometry from the oracle too** (T419,
board B352): `include/client/graphics/ui/pet_layout.h` quotes
`EQUI_PetInfoWindow.xml`'s 154x142 window and its 54x20 command cells, and
`UILayout::computeLayout()` sizes `petPanel` from the same two constants
(`PET_BUTTON_ROWS`, `PET_BUTTON_H`) the grid is drawn from — before this the
row was drawn 34px down a 40px panel, so the labels hung outside the frame and
the tenth ("SH", SpellHold) was silently dropped for want of width. Two
deviations from the XML are written out in that header: fable's `PetButton`
set is eqemu's TEN, not Titanium's seven, and the grid is 5x2 rather than the
XML's 2x5 because at 640x480 the panel has sixteen pixels of slack between the
group panel and the buff bar (board **B366**).

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
| Alt+O | Toggle the Options window (T434). MEASURED at the oracle's Options > Keys > UI page (`review/T377/oracle/T377_oracle_options_keys_UI_page2.png`: "Toggle options window   Alt O", Alternate column empty). fable bound a bare `O` to an arm that was `break;`; `O` is now unbound, as it is in the oracle. |
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
| Alt+C | Toggle combat abilities window (T415, board B320 -- measured at the oracle's Options > Keys > UI page, `review/T377/oracle/T377_oracle_options_keys_UI_page3.png`: "Toggle Combat Ability Window   Alt C") |
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

**T432 (B275): every one of these toggles is now PERSISTED per character.**
`<cache dir>/ui/UI_<Char>_<server>.ini` holds `[<Window>] Show=0/1` for the
seven toggleable windows -- `[InventoryWindow]`, `[SpellBookWnd]`,
`[SkillsWindow]`, `[GroupWindow]`, `[PetInfoWindow]`, `[BuffWindow]`,
`[CombatAbilityWnd]` -- plus `[Camera] Mode=`, using the official client's own
filename, section names and key. It is read at zone-in and written whenever a
toggle changes and at `/camp`. An ABSENT `Show=` key keeps the default, so
`defaults.ini`'s "a default is stated by omission" rule (above) still governs a
fresh character. Loot, tradeskill-container and item-display windows are NOT
persisted -- the server or a click opens them, the player does not. Neither is
`F11`'s all-UI hide: a client that starts with no UI gives the player nothing
to click. The format lives in `ui_preferences.h`/`.cpp`; the load/save tick is
`FrameComponent::syncUiPreferences()` in
`src/client/graphics/frame/renderer_frame_preferences.cpp`, which COMPARES the
whole value rather than hanging a save off each toggle -- add a persisted field
to `UiPreferences`, the `kWindows` table and the binding, and every path picks
it up.

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
