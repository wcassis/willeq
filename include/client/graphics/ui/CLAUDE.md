# Static UI System

The old WindowManager/WindowBase draggable window system was completely removed (Batch U07c, ~35K lines deleted). The new UI uses static screen-region panels rendered via batched quads.

## Components

- `UIRenderer` - Batched quad drawing API. Accumulates draw commands per frame, flushes them in submission order in `endFrame()`. Supports atlas-backed sprites or solid color fallback. Methods: `drawRect()`, `drawSprite()`, `drawBar()`, `drawPanel()`. **T462 (boards B505/B506):** BOTH paths queue -- the fallback path (no `--eqe1-path`, so no atlas: this is what the Orange Pi recipe runs, board **B540**) used to draw every rectangle immediately. `endFrame()` flushes RUNS of commands that share their GL state (one texture, or solid with one blend enable) as at most two driver draws each (`UiQuadBatch`, `ui_quad_batch.h`: the first quad through the driver's own call, the rest in one draw with the driver's own vertex arithmetic), and `TextBatch::flush()` lays the glyphs of every plain text entry into the same kind of run, exactly as `CGUIFont::draw` lays them out. Pixel identity against the pre-T462 per-quad, per-entry flush is pinned on both paths by `tests/test_static_ui_frame_cost.cpp` (`UIRenderer::setBatchingEnabled()` is the seam). `takeFlushStats()` feeds the `/frametiming` breakdown's "UI Paperdoll / UI Quad Flush / UI Text Flush" sub-lines under Static UI. **Draw order is z-order, text included (B992, #312).** `renderStaticUI()` passes every window `uiRenderer_->layer()` (`UIRenderer::beginLayer()`), and `renderContainerWindows()` begins a layer per container window; a window's own text is drawn after all of its own quads, and everything a later layer draws covers it. `endFrame()` keeps adjacent layers in ONE flush group (quads, then text) unless a text entry queued in the group meets one of the next layer's quads (`TextBatch::entryIntersects()`), so windows that do not overlap -- the HUD -- cost exactly T462's draws and only a window that really covers earlier text splits the batch (`FlushStats::layers` / `layerGroups`). Before B992 every window's quads went first and every window's text after them, so a covered window's labels and counts showed through the window on top. `tests/test_ui_panel_z_order.cpp` pins it: overlapping windows assert no lower-window pixel inside the upper window exceeds what its alpha-180 panel passes through (and the same frame with `setLayeringEnabled(false)`, the test seam for the old flush, must fail it), plus the measured draw counts of the layered flush against the old one and against a flush per window on both paths. A NEW static window gets its own `layer()` in `renderStaticUI()`, or its text bleeds into the window above it. The chat panel's word-wrap is cached (`ChatWrapCache`, `chat_wrap_cache.h`): only messages that arrived since the last frame are formatted and measured.
- `UILayout` - Anchor-based screen region definitions. `computeLayout(width, height)` positions all panels for any resolution. Constants: MARGIN=4, BAR_HEIGHT=14, SLOT_SIZE=32.
- `StaticPanels` - Free render functions (not a class hierarchy) for each panel. Each takes a `UIRenderer`, `UILayout`, and a plain data struct. No state ownership.
- `StaticUIInput` - Mouse/keyboard input handler for static UI. Communicates with game thread via `GameStateBridge` intents. Hit-tests slot grids for inventory, hotbar, spell gems. Its `processKey()` (ESC dismisses the top popup) had NO production caller until T290 wired it into `renderer_frame.cpp`'s escape handling, ahead of clear-target. **B460 (#156, UIGAP-small): ESC closes the MOST RECENTLY OPENED window, one per press** -- measured (`review/UIGAP-ORACLE/156/`: tip, then bag, then inventory, then nothing). `EscWindowStack` (`esc_window_stack.h`) learns every window's open ORDER from `FrameComponent::observeEscWindows()` once a frame, whoever opened it, and `FrameComponent::handleEscapeKey()` closes its newest entry (`processKey()` takes the one popup it chose; `processContainerEscape()` the one bag); with nothing open ESC clears the target. Not in the stack: the Quantity window (ESC does not close it, measured) and the HUD windows (unmeasured). Whether the official first press ALSO clears the target while closing a window is unseparated -- fable does not (assumption awaiting oracle). `tests/test_esc_window_stack.cpp`. Each window still closes its own way: T443 (board B454): the two server-owned popups do not close themselves — the tradeskill container is asked first (it is drawn after the loot popup) and pushes `TradeskillCloseIntent`, then the loot window pushes `CloseLootIntent`; both leave their panel up until the server's `TradeskillContainerClosed` / `LootWindowClosed` comes back (T290's rule). The spellbook, skills and inventory popups are pure client views and are hidden in place. The bank and merchant windows obey the same rule one level up, in `FrameComponent`'s escape handler (T436), because neither is a centre popup. **Scrolling (T313):** `processWheel()` / `processScrollKey()` / `runScrollCommand()` all funnel into ONE router — `scrollTargetAt()` picks the panel under the cursor (loot > skills > chat, the draw order), `scrollPanel()` moves it. Before T313 there was no scroll writer of any kind: chat scrollback, the skills list and the loot list all honoured a `scrollOffset` in both their render body and their hit test that nothing ever wrote, so a corpse with more than 8 items was partly UNLOOTABLE. **The offset conventions differ and are not harmonised** — chat counts lines up from the BOTTOM (0 = newest), skills and loot count rows down from the TOP; `scrollPanel()`'s `lines > 0` means "towards older content" either way. The clamps stay with the panels: `renderChatPanel()` owns chat's upper bound (it is the only code that knows the wrapped-line count), `SkillsPopupState::maxScrollOffset(visibleRows)` and `LootPanelState::maxScrollOffset()` own the other two.
- `ItemIconSheets` - **T430 (board B376)**: the item icon art, lifted from the official client's own
  `uifiles/default/dragitem1..34.tga` by `tools/item_icon_atlas_builder` into two 1024x1024 pack entries
  (`item_icons0`, `item_icons1`). Held by `UIAtlas` (`atlas->icons()`) and reached as
  `UIRenderer::drawItemIcon(rect, iconId)`. The layout — 34 sheets of 6x6 40x40 cells, ids **500..1723**,
  **COLUMN-major within a sheet** because `EQUI_Animations.xml`'s `A_DragItem` carries
  `<Vertical>true</Vertical>` — lives in `include/client/graphics/ui/item_icon_atlas.h`, which the builder
  and the loader both include. The sheets load EAGERLY, in `UIAtlas::load()` beside the UI atlas itself:
  T430 wrote them lazily first and measured the consequence -- `ConstrainedTextureCache::getOrLoad()`
  logs FATAL and `std::exit(1)`s on a pack miss, so loading on first use converts a startup failure
  into a mid-SESSION exit the moment a player opens an inventory. The reason is written out in
  `include/client/graphics/ui/item_icon_sheets.h`; an earlier revision of this bullet said LAZILY and
  was wrong (T444). `drawItemIcon()` returns
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
| Hotbar | Stock Titanium HotButtonWnd (one visible window, paged 2x5 slots) | `HotbarPanelState` | `renderHotbar()` |
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
| Tradeskill container | Center popup | `TradeskillPanelState` | `renderTradeskillPopup()` (T415, board B321; **no longer opened since B899** — the world container opens the container window below, Will 2026-09-23; T419 added the recipe list on the left, board B335 — `renderTradeskillRecipeList()` in `src/client/graphics/ui/tradeskill_recipe_list.cpp`) |
| Container (world + carried bags) | The oracle's own (350,100) 100x366, unscaled; several at once, stacked | `ContainerWindowsState` | `renderContainerWindows()` (B899, #215 / #151 Leg B; geometry `container_layout.h` from `EQUI_Container.xml`) |
| Combat abilities | Top-right, under target, left of the gems | `CombatAbilityPanelState` | `renderCombatAbilityPanel()` (T415, board B320) |
| Item display | Its own absolute (0,80), 400x190 | `ItemDisplayPanelState` | `renderItemDisplayPopup()` (T421, board B298 item half) |
| Options | Centred, the oracle's own 400x452, seven tabs | `OptionsPanelState` | `renderOptionsWindow()` (T434) |
| Merchant | Center popup (the oracle's 374x480 scaled onto it) | `MerchantPanelState` | `renderMerchantPopup()` (T436, board B429) |
| Bank | Centred, the oracle's own 108x347, unscaled | `BankPanelState` | `renderBankWindow()` (T436, board B429) |
| Quantity | Its own absolute (300,10), 150x90 | `QuantityWindowState` (owned by `StaticUIInput`) | `renderQuantityWindow()` (B442, `EQUI_QuantityWnd.xml`) |
| Confirmation | Centred (flagged), the oracle's 274x200 | `ConfirmationDialogState` (owned by `StaticUIInput`) | `renderConfirmationDialog()` (B442b, `EQUI_ConfirmationDialog.xml`; Yes/No or Ok) |
| Cursor item | At the mouse, the oracle's 50x50 box | `CursorAttachmentState` | `renderCursorAttachment()` (T436, board B429) |
| Skill trainer | Centred, the oracle's own 580x280, unscaled | `TrainerPanelState` | `renderTrainerWindow()` (B898, #214) |
| Tracking | Centred, the oracle's own 150x460 (height clamped to the screen) | `TrackingPanelState` | `renderTrackingWindow()` (B895, GitLab #211) |
| Tip windows | The oracle's 360x300, flush right at the measured (664,234) of 1024x768 | `TipWindowState` | `renderTipWindow()` (B834, #202; `tip_layout.h` from `EQUI_TipWnd.xml`) |
| Friends | The oracle's own 168x288, at `defaults.ini`'s `[FriendsWindow]` position (XML (196,50) elsewhere), two tabs | `FriendsPanelState` | `renderFriendsWindow()` (B897) |
| Trade / Give | The oracle's own (0,25): trade 236x365, give 108x375, unscaled | `TradePanelState` (`isNpc` picks the give window) | `renderTradeWindow()` (B896, #212; geometry `trade_layout.h` / `give_layout.h`) |
| Character Select | Pre-zone: the oracle's 252x484 window, left edge, vertically centred (`EQUI_CharacterSelect.xml`) | `CharSelectScreenState` | `renderCharSelectScreen()` (B998, #318; hosted by `CharacterSelectHost`) |
| Character Creation | The whole screen, two pages (`EQUI_CharacterCreate.xml`); pre-zone, opened from Character Select | `CharCreateScreenState` (inside `CharSelectScreenState::create`) | `renderCharCreateScreen()` (B888, #204) |

Center popups (inventory, spellbook, skills, merchant, loot, tradeskill container) share the same screen region and are mutually exclusive. **The BANK is not one of them** (T436): the oracle's `BankWnd` is `Style_Sizable false` at its own 108x347, so like the combat-abilities window it keeps its own size and its own anchor and draws above the centre popups. The combat abilities window is NOT one of them -- like the buff/group/pet panels it is a HUD window with its own anchor and its own Alt+C toggle. **Neither is the item display window** (T421): `EQUI_ItemDisplay.xml` gives it `RelativePosition false` and `<Location>(0,80)</Location>`, so it is drawn and hit-tested LAST, on top of every centre popup (ESC: B460's open order). **Nor is the Options window** (T434), for the same reason and one level higher still: `EQUI_OptionsWindow.xml` is `RelativePosition false` at `<Location>(90,47)</Location>`, so it is drawn and hit-tested after even the item display (ESC: B460's open order). Its geometry is `options_layout.h`, a table of every control the oracle XML declares; a control whose `OptionsLayout::Setting` is `None` has no backing setting in fable, is drawn greyed and is **not hit-testable** (the T363 rule made mechanical).

**What is wired (B430/B431/B433).** Every control row has a decision in `src/client/graphics/ui/options_classification.cpp` — WIRED, or excluded as POST-VELIOUS, OUT (Will 2026-09-23), NO CAPABILITY (naming the issue that owns the missing capability) or AWAITING ORACLE — and `OptionsClassification.EveryControlRowIsClassifiedExactlyOnce` / `AControlIsWiredIffItHasASetting` fail on a gap; read that file, not this paragraph, for the live census. The settings themselves are one table, `options_settings.h/.cpp` (oracle ini key, scale, default, owner), and every click resolves through `resolveOptionsClick()` (`options_lists.cpp`: checkbox toggles, slider steps, combobox CYCLES — a flagged deviation from the oracle's drop-down — list rows select) into `FrameComponent::applyOptionsSetting()`. Owners: **Renderer** settings persist per character under their `eqclient.ini` key; **Game** settings (anonymous/roleplay, autosplit, the three auto-consents) are the server's and travel as `GameOptionIntent` → `OP_SpawnAppearance`, coming back on `GameOptionChanged` (also published from the player profile at zone-in and from `/anon`, `/roleplay`). Pages: **Filters** — the era `/filter` set (eqstr 1354-1360), one `OP_SetServerFilter` payload, `/filter <name>`; **General** — the anon pair, autosplit, auto-consent ×3, pet window popup, the two volumes; **Display** — PC and NPC names (two flags under the Ctrl+F3 master, `EntityRenderer::setNameTagKindsVisible`), clip plane; **Mouse** — invert Y, look spring, mouse look, wheel zoom, sensitivity (`include/client/graphics/player/mouse_options.h`); **Keys** — category combobox, the Command/Keypress/Alternate list over `HotkeyManager` (a slot click captures the next key, `options_keys.h`), Reload defaults; **Colors** (B993, #313) — `OCP_ColorChooseList` lists the 86 `[TextColors]` rows (`include/client/chat/chat_text_colors.h`: eqclient.ini's defaults, row N = server chat type 255+N, named by eqstr 6199+N), `OCP_UserColorDefault` resets them and `OCP_UserColorChange` opens the `EQUI_ColorPickerWnd` picker (`color_picker.h`, its own UIRenderer layer above the window); each chat line carries its row (`ChatMessage::userColor`) and `renderChatPanel()` resolves the colour at draw from `ChatPanelState::textColors` — the one place; `/option textcolor`; **Display** also has the 3D target ring (B994, #314: `ODP_ShowTargetRingCheckbox` -> `OverlaysComponent::drawTargetRing()`, `TargetIndicator.ini`'s ring, default off) and the Particles box (B995, #315: `ODP_ParticlesTabs` switches the Spell / Env / Player sub-pages, `Control::subPage`; the Spell and Env pages' opacity / density / near clip drive `particle_options.h`'s knobs, the Player page and the "On For" filters stay greyed); **Mail** — all OUT. `/option [list | <name> [value]]` (registry 3) sets any wired setting by name.

**The Friends window** (B897, GitLab #213). `friends_layout.h` quotes `EQUI_FriendsWnd.xml`: one
`Style_Sizable false` 168x288 window (drawn unscaled) whose TabBox holds `FW_FriendsPage` (Contact, Who,
the list, the name Editbox, Add, Delete) and `FW_IgnorePage` (the list, the Editbox, Add, Delete -- the
file declares no Contact or Who there, so none is drawn or hit-testable). Its title height (24) and tab strip
(22, the `FT_DefTabBorder` strip the Options pages sit under) are derived, its list row (14) and VScroll strip
(12) are fable's own and flagged. The LISTS are the game thread's (`state::GameState::social()`,
`include/client/state/social_lists.h`); the panel holds a copy fed by `GameEventType::SocialListsChanged`, and
Add / Delete / Who push `SocialListIntent`, whose handler runs the very bodies `/friends`, `/ignore` and
`/friendswho` run; Who with a friend selected is a who-all for that name. Contact prefills the chat input with
`/tell <name> `. Both measured on the official client (`review/UIGAP-ORACLE/213/friends.tsv`). Both
lists persist in `<cache dir>/ui/<Char>_<server>.ini` `[Friends]`/`[Ignored]` -- the oracle's own file and
keys, sorted and tail-packed with `*NULL*` as every real file is -- written by `state::SocialListsSync` on the game
thread (headless too); the window's open state is `[FriendsWindow] Show=` in the UI_ file. Drawn and
hit-tested just below the Options window; ESC closes it when it is the newest window (B460). The Editboxes take the keyboard through
`processFriendsNameInput()`, beside the tradeskill gate, with their own hold flag.

**The trade and give windows** (B896, GitLab #212). `trade_layout.h` quotes `EQUI_TradeWnd.xml`
(236x365 at (0,25); `Style_Sizable true` but it fits 640x480, so unscaled) and `give_layout.h`
`EQUI_GiveWnd.xml` (108x375, `Style_Sizable false`); the two bottom-anchored Trade/Cancel buttons'
arithmetic is written out there, and `TITLE_H` is fable's own 18 as in the bank and container windows.
One state, `TradePanelState` (`trade_panel.h/.cpp`): the trade window for a player, the give window
for an NPC (`isNpc`). Fed only by the game: `TradeStarted` opens it AND the inventory popup,
`InventorySlotChanged` 3000..3007 fills our column, `TradeItemUpdated` the partner's (it now carries
the icon and count), `TradeMoneyUpdated` the coin rows, `TradeAcceptStateChanged` the names, and
`TradeCompleted`/`TradeCancelled` close it and the inventory. Everything is MEASURED on two official
clients (`review/UIGAP-ORACLE/212/trade.tsv`): our side is the RIGHT column and the slots are
column-major (3000-3003 then 3004-3007); both names are RED and the accepting side's turns GREEN, any
change resets it; a click on an offered item does nothing (it cannot be taken back); the give window's
NPC name is white. Input is `StaticUIInput::processTradeInput()` (`trade_input.cpp`), run by
`FrameComponent::processTradeWindowInput()` (`renderer_frame_trade.cpp`) right after the QuantityWnd:
the window is drawn above the centre popups, the bank, the trainer, tracking and the containers, and
the inventory is always open beside it, so a click over it is spent there. A left click with an item
held on an EMPTY own slot is B442's `slotClick()` with slot 3000+n; a right click on an occupied one
is the item display; an own coin row with coin held (from the purse) pushes
`TradeWindowIntent{CoinFromCursor}` (one `OP_MoveCoin` cursor -> trade); Trade/Give pushes `{Accept}`
and Cancel and the close box `{Cancel}` -- the `TradeManager::clickAccept()` / `cancelTrade()` bodies
`/accept` and `/canceltrade` run. Opening: a world click on a player or NPC with an item held
(`TargetIntent` -> `requestTrade`) -- which takes precedence over B442b's drop prompt
(`classifyHeldItemWorldClick()`, consulted by `processCursorGestureFrame()`: player = trade, NPC =
give, anything else = drop) -- then the held item drops into 3000 by itself when the request is
acknowledged (held COIN at a player goes in as `OP_MoveCoin 0 -> 3`, trade.tsv row 14); an incoming request is acknowledged with no prompt.
Refused client-side, the click then only targets (`BridgeComponent::HeldItemTargetRefusal()`, measured rows 12-13): a NO-DROP item at
a player prints eqstr 12921 and sends no `OP_TradeRequest` (B1001); any item at a banker prints eqstr 8505. ESC does not touch it, and the
close box is Cancel (both assumptions awaiting the oracle). Tests: `tests/test_trade_window.cpp`.
**The tip windows** (B834, GitLab #202, UIGAP-small). The Tip of the Day pops after a character's first zone-in of the session, and a context tip pops on the RISING EDGE of the window it belongs to (`FrameComponent::updateTipWindows()`, `renderer_frame_tips.cpp`, once a frame, so every opener counts): Inventory, Spellbook, Skills, Options > Keys, Friends, Merchant, Bank, Trainer, Loot, the world container, Quantity, Trade and Give (B896's window) -- the ones fable has a window for out of the observed table (`review/UIGAP-ORACLE/202/README.md`; Consider/Sit/Death are not popped, and B442b's confirmation dialog is not in the table). Text and caption are the client's own `help/tips/ctx*.html` / `TOTD.txt` under `eqClientPath`, read lazily; each tip's ini KEY is eqgame.exe's own ctx-file -> key table (`tipCatalog()`); the switches are the official `[Tips]` section of `UI_<char>_<server>.ini` (`ShowTips`, `ShowTOTD`, `<Key>=0`, the keys real player files carry), through `UiPreferences::tips`. Pieces as measured: the Tip of the Day shows Show Another Tip + "Don't show any tips"; a context tip shows the History line + both checkboxes; checkboxes apply on Close. ESC closes it first when it is the newest window (B460). Assumptions awaiting oracle, in `tip_window.h`: once per session, the TOTD checkbox -> `ShowTOTD`, the TOTD entry order, a newer tip replaces an open one, colours dropped. Tests: `tests/test_tip_window.cpp`.

**Character Creation** (B888, GitLab #204). Three device-free layers: `char_create_layout.h` quotes
`EQUI_CharacterCreate.xml` (every `<Location>`/`<Size>`/anchor) plus the placement rules MEASURED on the
oracle's 1024x768 frames (`eq-oracle/logs/shots/cc_*.png`: title top-centre, the Character/Deity columns
left and vertically centred, Abilities/City right, the help pane bottom-centre at the XML size -- its
oracle size differs and is flagged); `char_create_screen.h` is the state machine (opens on a RANDOM valid
selection with nothing spent, as the oracle does; greyed = not offered and inert; Next refused with eqstr
1577 while points are left, 1576 for a bad name, 1580 while the request is in flight; Back page 1 -> 0 ->
character select); `char_create_panel.h` draws and resolves clicks (right-click a stat name = every point
into it). Post-Velious races/classes/cities are HIDDEN, never greyed (Will 2026-09-23): the in-era buttons
take the column's slots in order, Gnome in Froglok's. The rules are the SERVER's
(`include/client/login_world/char_create_rules.h`, quoting `CheckCharCreateInfoTitanium`), the offers are
data (`data/char_create.tsv`; measured rows cite their oracle frame, the rest say `assumption`). Since B998
it is HOSTED by the Character Select screen below (an empty slot's "Create a New Character"); the scripted
path `--create-character` (`include/client/login_world/world_script.h`) remains for automation. Not drawn,
flagged: the 3D scene and sample model, FacePick (`CC0_Face_Button` is greyed and not hit-testable), the
tutorial controls (OUT by ruling). Tests: `tests/test_char_create.cpp`, `tests/test_char_create_screen.cpp`.

**Character Select** (B998, GitLab #318, #204 Legs S/M). The pre-zone screen between `OP_SendCharInfo` and
`OP_EnterWorld`. **Shown only with graphics, no world script, and either no configured `character` or
`--character-select`** (`CharacterSelectScreenWanted()`, `include/client/login_world/character_select.h`; a
`--scenario` run needs the flag): with a configured character the T14 auto-select is byte-for-byte what it was
(`CharacterSelectDecision.*` pins it). Layers: `char_select_layout.h` quotes `EQUI_CharacterSelect.xml`
(placement measured: left edge, vertically centred); `char_select_screen.h` is the flow (a name selects, an
empty slot reads eqstr 1947 and opens B888's screen, Delete asks eqstr 9065 Yes/No on B442b's confirmation
dialog and closes itself after 10 s, Enter World / Quit; the creation name field holds 15 characters and
lower-cases after the first, both measured); `char_select_panel.h` draws and resolves clicks. THREADS: the
list and the B888 flow live on the game thread (`LoginWorldComponent`); `CharacterSelectService` (game
thread) publishes a `CharacterSelectView` -- rows composed there, eqstr text pre-resolved there -- into a
mutex-guarded `CharacterSelectChannel` and drains the screen's `CharacterSelectRequest`s into the same
`Request*` bodies the world script uses. `CharacterSelectHost` (render thread, `char_select_host.h`) runs a
frame through `IrrlichtRenderer::drawPreZoneUI()` from `Application::render()`'s pre-zone branch -- where the
handshake loading screen is drawn -- and exists only while the channel is active (list in, `OP_EnterWorld`
not sent). Not drawn: the 3D scene/preview (Explore and Rotate greyed), the tutorial and Return Home buttons
(OUT). Assumptions awaiting oracle are listed in `char_select_screen.h` (`review/UIGAP-ORACLE/318/`). Tests:
`tests/test_character_select.cpp`. The scenario lane cannot click here: `ScenarioRunner` does not `Step()`
before `IsFullyZonedIn()`, so `create_character` keeps its scripted route.
**B1005 (#324): `/camp` comes back here.** On every graphics run with no world script the channel exists
from the start (`CampReturnsToCharacterSelect()`; the render thread reads the pointer unlocked, so it is
never created later). After the 30 s camp and the server's `OP_LogoutReply`,
`LoginWorldComponent::ReturnToCharacterSelect()` re-runs the world handshake as a fresh login (zoning 0) and
the service holds the screen down until the FRESH `OP_SendCharInfo` (`AwaitingFreshCharacterList()`); Enter
World then zones in as a first login does. The old zone's scene stays loaded until the next zone load
unloads it (the pre-zone frame draws no scene). The camp rule: `include/client/player/camp_timer.h`.

**The three commerce windows** (T436, board **B429** CLOSED). Three producers filled an `iconId`
nothing could draw, because the merchant window, the bank window and the cursor attachment did not
exist -- the commerce vocabulary was reachable only as `/vendor`, `/vendorlist`, `/buy`, `/sell`,
`/bank`. Each takes its geometry from a header that quotes its own oracle XML and computes no rect in
its `.cpp`: `merchant_layout.h` (`EQUI_MerchantWnd.xml`), `bank_layout.h` (`EQUI_BankWnd.xml`),
`cursor_attachment_layout.h` (`EQUI_CursorAttachment.xml`).

- **Merchant** — `Style_Sizable true` and 505px tall from its own `<Location>`, which does not fit
  640x480, so it is mapped proportionally onto `centerPopup`, T415's rule. It is the LAST centre
  popup. **`MW_Buy_Button` and `MW_Sell_Button` carry byte-identical anchors in the shipped file**, so
  the official client can only ever show one. WHICH ONE IS MEASURED (B442b, #152,
  `review/UIGAP-ORACLE/152/merchant.tsv`): the side of the LAST selection -- Buy on open and after a
  list row, Sell after a left click on one of YOUR items, which with a merchant open SELECTS it (yellow
  frame; `static_ui_merchant.cpp`) and never touches the cursor. T436's "an item on the cursor means
  Sell" was a guess and is gone. Buy pushes `VendorBuyIntent` (CTRL = one, measured), Sell
  `VendorSellIntent` from the selected slot (a stack opens the QuantityWnd, `Purpose::Sell`; SHIFT =
  all, CTRL = one, per the Merchant Tip), Done `VendorToggleIntent` — all three pre-existing, the same
  intents `/buy`, `/sell` and `/vendor` push. `MW_DoneButton` (no underscores)
  is declared in the file and is in NO `<Pieces>` list, so the oracle never instantiates it and
  neither does fable.
- **Bank** — `Style_Sizable false` at 108x347, so it is drawn UNSCALED at the oracle's own offsets,
  T415's other rule; it is CENTRED rather than at the oracle's `<Location>` (0,25), the same flagged
  anchor deviation T434 and T415 made. **Eight slots, not sixteen**: `inventory_constants.h`'s
  `BANK_COUNT = 16` is a post-Velious bank, and the era file declares `BW_BankSlot0..7`.
  `BW_SharedBankSlot0/1`, `BW_SharedMoney0`, `BW_SharedBankLabel` and `BW_AltStorageButton` are IN the
  `<Pieces>` list and carry a `<ScreenID>` and nothing else — no `<Location>`, no `<Size>` — so the
  era file itself declares them as geometry-less stubs and fable draws none of them. `BW_ChangeButton` (B441, #26, UIGAP-small) pushes `BankMakeChangeIntent`: ONE click converts every coin in the bank AND on the person to the fewest coins -- the client's own `help/tips/ctxbank.html` says exactly that, and no `EQUI_*.xml` declares a conversion dialog, so none exists to build. The game thread's arm sends `eqt::merchant::BuildMakeChangeMoves()`'s OP_MoveCoin steps (cp->sp->gp->pp, `from_slot == to_slot`, multiples of 10, `coin_change.h`) and projects the same arithmetic (eqemu answers with silence); `/bank change` pushes the same intent. Order of the steps is an assumption awaiting oracle; `tests/test_bank_change.cpp` replays them through eqemu's own conversion arithmetic. `BW_DoneButton` and the close box push
  **`CloseBankIntent`**, the unit's one new intent, whose handler is the existing `CloseBankWindow()`.
- **Cursor attachment** — `CA_Anim` at (5,5) 40x40 in a 50x50 box that follows the mouse, drawn LAST
  of everything (it is the pointer). `Style_Transparent true` / `Style_Border false`, so no plate and
  no frame is drawn under it. There is no count label anywhere in that screen, which is the same fact
  T413 recorded when it dropped `CursorItemChangedData::queueSize` (board B296) — a stack's quantity
  is simply never shown there. `CursorItemChangedData` is NOT the feed (it carries one bool and a
  `sizeof` test pins that): the feed is the enriched `InventorySlotChanged` for slot **30**, which the
  bridge's arm had been dropping along with the bank's 2000..2007.

None of the three closes itself. Each pushes the intent that ends the SERVER-side session and waits
for the server's own `VendorWindowClosed` / `BankWindowClosed`, T290's rule; ESC closes the most recently opened window (B460, see `StaticUIInput` above), each in its own way. **B442 (#151)** gave all
of them the click-to-cursor gesture: a bank slot (`BW_BankSlot<n>` = Titanium slot 2000+n) takes the
same click as an inventory slot -- and only while the bank is open: the game thread refuses a bank-slot or bank-coin
gesture with the window shut, the banker opens it at <= 20 units (beyond, the click is a consider) and an open bank
closes itself past 20 with eqstr 12143, all measured (B996, `review/UIGAP-ORACLE/316/`; `BANKER_INTERACTION_DISTANCE`). The merchant is NOT a put-down target: the oracle measured that
selling never uses the cursor (`review/UIGAP-ORACLE/152/`, board B443 / #152 owns it).

**The skill trainer window** (B898, #214). Its geometry is `train_layout.h`, which quotes `EQUI_TrainWindow.xml` (`Style_Sizable false`, so it is drawn UNSCALED, like the bank, and CENTRED: the oracle's `<Location>` (120,20) runs off a 640-wide screen). It is not a centre popup: it draws and is hit-tested above the bank and below the item display, and ESC ends it (pushes the close intent) when it is the newest window (B460). Fed by `TrainerWindowOpened`/`TrainerWindowClosed`, whose bridge arms were LOG_TRACEs that dropped the decoded `OP_GMTraining` reply. The game thread RE-PUBLISHES `TrainerWindowOpened` after every `OP_SkillUpdate` of an open session (`MerchantComponent::PublishTrainerWindow`, `merchant_trainer.cpp`), the only packet a Titanium server sends after a train; the applier treats a same-trainer open as a refresh and keeps the selection by skill id. Train pushes `TrainSkillIntent` (the `/train <id>` intent) and Done, the close box and ESC push `CloseTrainerIntent` (the `/trainclose` intent). Both paths' refusals are ONE body, `MerchantComponent::TrainRefusal()`. Measured by UIGAP-live2 (`review/UIGAP-ORACLE/214/train.tsv` rows 10-15): a row click prints eqstr 12056 "Interested in training in <skill> ?" (a select-only `TrainSkillIntent`, no packet); Train is greyed with 0 practices or a purse short of the row's cost (`TrainerPanelState::trainEnabled()`); a skill at its cap is listed, Train stays enabled and a press sends nothing and prints eqstr 12930 -- against the session's cap, since fable has no per-level cap table (the official client's at-cap case was the cap at the current level, owed). The Rank column is drawn empty (oracle-owed). `Style_Minimizebox` is not drawn. The scroll bar's arrows are clickable; the wheel does not reach this list yet. Openers are `/train`, T, and Ctrl+click on a guildmaster, which is fable's stand-in for the official right-click.

**The Tracking Window** (B895, GitLab #211). Geometry is `tracking_layout.h`, which quotes
`EQUI_TrackingWnd.xml` anchor by anchor (the window is `Style_Sizable true`, so the bottom-anchored
controls follow the window's height); the panel is `tracking_panel.cpp`. It is not a centre popup:
drawn and hit-tested directly above the bank and below the item display, and ESC hides it when it is the newest window (B460). Its one feed is `TrackingListChanged`, published WHOLE by the game thread's
`eqt::tracking::TrackingController` (`include/client/tracking/`): rows already filtered, sorted and
con-coloured (`eqt::ConTierForLevels()`, the one level table, moved out of `EntityRenderer`), and the
combobox text composed from eqstr. The window opens on an OP_Track reply (`/useskill tracking`, a
skill hotbar button) and its controls push ONE intent, `TrackingIntent` (Track / ToggleFilter /
SetSort / SetPlayers), onto the same bodies `/track`, `/trackfilter`, `/tracksort` and
`/trackplayers` reach. Cancel, the close box and ESC only hide it (they do not stop tracking);
settings are not changed optimistically. Not drawn: `Style_Minimizebox` (no minimised state for any
static panel). The direction lines (eqstr 12676-12680) and the lost line are the client's own on
Titanium -- eqemu's `DoTracking()` runs only for SoD+ -- and tick from `MovementComponent`'s game
tick every `kDirectionIntervalSec`, 2 s, with one line at once after "You begin tracking". The
official client was measured (`review/UIGAP-ORACLE/211/tracking.tsv`): OP_Track goes out with an
empty OP_TrackUnknown, consider sorts hardest first, a filtered-out button is highlighted, and the
lost line is eqstr **12102** "You have lost your target." -- NOT 12681, which the issue named.
Tests: `tests/test_tracking_window.cpp` (cites the ledger rows).

**Text entry** (T421, board B365). There is exactly ONE text-entry widget in the static UI: `EQT::Graphics::TextEntryState` and `applyTextEntryKey()` (`include/client/graphics/ui/text_entry.h`). `ChatPanelState` DERIVES from it -- `inputText` / `cursorPos` / `inputActive` are its members -- and the tradeskill window's three Editboxes (`COMBW_MinTrivialEdit`, `COMBW_MaxTrivialEdit`, `COMBW_SearchTextEdit`) are three more of them, selected by `TradeskillPanelState::focusedField`. The KEYBOARD GATE is `FrameComponent::processChatInput()`, still the only caller of `setChatInputFocused()`: it early-returns into `processTradeskillSearchInput()` when a search box holds the caret, so the two key drains are mutually exclusive and no keystroke is read twice (`tests/test_read_and_reset_ownership.cpp` records that as `popKeyEvent`'s two-sites-one-consumer exception). Enter in a search box, or `COMBW_SearchButton`, pushes `RecipeSearchIntent` into the existing `SendRecipesSearch()`; the favourites row pushes `RecipeFavoriteIntent` into the existing `SendRecipesFavorite()` (with board **B377** on the missing favourites set).

**The item display window** takes its geometry from `include/client/graphics/ui/item_display_layout.h` (which quotes `EQUI_ItemDisplay.xml`) and its TEXT from the GAME thread: `eqt::inventory::BuildItemDisplayText()` composes the description from format strings quoted out of the official client's own `eqstr_us.txt`, and ships the finished lines on the `ItemDisplayShown` event. That is what the official client does too -- the XML's whole body is one `<STMLbox>`. A RIGHT click on an occupied inventory or bank slot pushes `ItemDisplayRequestIntent` -- the oracle's own button. Until B442 it was a LEFT click (T421's deviation: the static UI latched no right click); `RendererEventReceiver::wasRightButtonClicked()` is that latch now, consumed only by `renderStaticUI()`, and mouselook still reads `isRightButtonDown()`, so a right drag off the panels turns the camera as before.

**Item-name tooltips (B1004, #323).** `item_tooltip.h/.cpp` (logic, geometry, draw) and `renderer_frame_tooltips.cpp` (`FrameComponent::renderItemTooltipLayer()`, its own top-most layer, below only the cursor attachment). MEASURED on the official client (`review/UIGAP-ORACLE/323/tooltips.tsv`): ALT HELD with the pointer over a bag window labels EVERY occupied slot of that bag with the item's name at once (a hold, not a toggle; gone when the pointer leaves the bag or Alt is released); the bank does the same, and with the bank open Alt over the INVENTORY shows the bank's labels; the inventory's own slots, the merchant and (unmeasured) the trade window get none. Name only, never the stack count; navy box, white text; left column at the 40x40 slot's (+35,+1), right column (+35,+21). The HOVER tooltip (any item slot: inventory, bag, bank, trade) is the client's own documented rule (`help/tips/ctxbank.html`) that the oracle rig never displayed, in by Will's ruling, on the measured static-tooltip dwell (600 ms) with the box's bottom edge on the pointer; suppressed while Alt labels are up. No ini key exists, so nothing persists. Not reproduced: the oracle's right half of the bank showed no labels (3/3, unexplained). Tests: `tests/test_item_tooltip.cpp`.

**Click-to-cursor (B442, #151).** One gesture for every item window, `eqt::inventory::CursorGesture` (`include/client/inventory/cursor_gesture.h`), whose rules are the official client's MEASURED ones (`review/UIGAP-ORACLE/151/gestures.tsv`): LEFT on a single item picks it up; LEFT on a stack of 2+ opens the QuantityWnd prefilled with the whole stack (`quantity_window.h`, `quantity_layout.h` quoting `EQUI_QuantityWnd.xml`; drawn and hit-tested above everything, ESC does not close it, other slot clicks are ignored while it is up); SHIFT+LEFT takes the whole stack and CTRL+LEFT exactly one, no dialog; LEFT with an item held places / swaps / merges (any modifier); RIGHT is the item display shown only WHILE held, ALT+LEFT keeps it (B442b, `ItemDisplayPanelState::heldOnly`), and RIGHT on a bag in a general slot toggles its window (`InventorySlotChangedData::bagSlots` -> `CursorGestureOp::ToggleBag` -> the `/bag` body); a purse row (`IW_Money0..3`) or a bank coin row (`BW_Money0..3`, OP_MoveCoin slot 2) opens the QuantityWnd for that coin (SHIFT: all of it) and held coin goes back. B442b also: held coin is DRAWN (dragitem 644-647 plus a measured count strip, fed by `CursorCoinChanged`; there is no render-side "coin held" shadow); the QuantityWnd's box takes typed digits; a motionless world click with an item held and `IW_Destroy` raise the reusable confirmation dialog (`confirmation_dialog.h`, `EQUI_ConfirmationDialog.xml`, text composed game-side from eqstr 9057 / 2043 and sent on `ConfirmationRequested`), whose Yes sends `OP_GroundSpawn` (empty) or `OP_MoveItem 30 -> -1`; left unanswered it closes itself as a No after 20 s (drop) / 10 s (destroy), measured by UIGAP-live2 -- the request carries `timeoutSeconds` and `tickConfirmationDialog()` is the one clock, shared with the character-select delete prompt's 10 s. The render thread pushes `CursorGestureIntent` and never moves an item itself; the game thread resolves it in `ApplyCursorGesture()` against `InventoryManager` and republishes `InventorySlotChanged` for the slots it changed (eqemu answers a good `OP_MoveItem` with silence and a bad one with a resync). The loot window's LEFT click is `LootItemIntent` with auto_loot 0, which eqemu puts on the cursor; its RIGHT click is auto_loot 1, straight into the inventory (both measured, gestures.tsv rows 46a/46c). `/pickup <slot> [qty]`, `/putdown <slot>`, `/cursor` and `/coin <pp|gp|sp|cp> <n>` push the same intent; slot numbers are `inventory.slot_id` through `parseUserSlot()`. `tests/test_cursor_gesture.cpp` pins it against the ledger's rows.

**The same window shows SPELLS** (T425, board B298 spell half). The oracle has no second display window at all -- there is no `EQUI_SpellDisplay.xml` and no `SpellDisplay` screen anywhere in `uifiles/default`, and `IDW_IconButton`'s own `<NormalDecal>BuffIcons</NormalDecal>` is the SPELL icon sheet -- so a spell reuses `ItemDisplayPanelState`, `renderItemDisplayPopup()`, `ItemDisplayLayout` and the one `ScrollTarget::ItemDisplay`. Its lines are composed game-side by `eqt::spell::BuildSpellDisplayText()` from eqstr 3004/3005/3006/3102/3103/3105/3107/6730/9234 and arrive on the SAME `ItemDisplayShown` event. **SHIFT+left click** on a memorised gem or a spellbook row asks for it (`SpellDisplayRequestIntent`); a plain click still casts and still memorises. The oracle's gesture is a right click, which fable cannot bind, and unlike the inventory slot left click was already taken here.

**The container window** (B899, #215 / #151 Leg B). ONE window shape, `EQUI_Container.xml`'s
ContainerWindow, for a WORLD container (forge, oven: the server's `ClickObjectAction` open) and every
CARRIED bag (`/bag <general slot>`, `CommandsComponent::RequestToggleBag()`, the game thread being the one
truth for which bags are open). Geometry in `container_layout.h` (transcribed, unscaled, `Style_Sizable
false`); state, appliers, draw and hit tests in `container_panel.h/.cpp`; input in `container_input.cpp`
(`StaticUIInput::processContainerInput()` / `processContainerEscape()`). Every window sits at the oracle's
(350,100), newest on top (measured, `review/UIGAP-ORACLE/151/gestures.tsv` row 20); every open window is
drawn, oldest first, each its own `UIRenderer` layer (`containerDrawOrder()`, B992), so the newest covers the
rest, text included -- the panel is the atlas's alpha-180 `panel_background`, so the covered windows show
through it dimly (whether the oracle's window is opaque is an assumption awaiting oracle). Combine (on every
container but a plain storage bag, label always "Combine": measured, `review/UIGAP-ORACLE/215/combine.tsv`)
pushes `TradeskillCombineIntent` with 1000 (world) or the bag's slot
(carried); Done, the close box and ESC push `TradeskillCloseIntent` (`inventorySlot` set for a bag) and
the window waits for `TradeskillContainerClosed` (T290's rule); a stack in the container is refused
client-side with eqstr 8504 (`onTradeskillCombineIntent`, measured row 7). A tradeskill container opens
straight in this view, not the official client's recipe view (Will's ruling; discrepancy flagged). A cell click goes to ONE call site,
B442's shared `slotClick()` / `CursorGesture` with the cell's wire slot (bag 251-330, world 4000-4009). Augment controls are
out of era and not drawn. The T415 recipe window (`TradeskillPanelState`) is no longer opened by anything.

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
| F10 | Toggle ALL UI (T301: also hides the static panels -- chat, hotbar, gems, popups, XP bar; before T301 it gated only the casting bars, the legacy `guienv_` draw and the FPS counter, so with the static UI on it hid nothing. Scriptable as `/hideui`.) B582 moved it from F11: F10 is the oracle's official "Toggle UI Display" (T466), while F11 is the oracle's netstat toggle, unbound in fable |
| F12 | Screenshot |
| PageUp / PageDown | Scroll the static-UI panel under the cursor one page (chat scrollback, the skills list, the loot list). T313; scriptable as `/scroll`. PageDown used to be a second StrafeRight binding — that moved to Home, pairing with StrafeLeft's End, so paging a loot list does not also strafe. |
| Mouse wheel | Scroll that same panel 3 lines per notch. T313: `EMIE_MOUSE_WHEEL` had no case in `RendererEventReceiver::OnEvent()` at all before this, so the wheel was discarded client-wide. **T461: a notch with the cursor over no scrollable panel zooms the camera** — the official client's Mouse Wheel Zoom option (`eqclient.ini` `MouseWheelZoom=1`, `EQUI_OptionsWindow.xml` `OMP_MouseWheelZoomCheckbox`). There is still one reader (`FrameComponent::processFrameInput()`), so no notch both scrolls and zooms. |
| LMB+drag | Look around (camera) |
| RMB+drag | Look around (camera) |
| LMB on an item slot | Pick up / place / swap / stack (click-to-cursor, B442); on a stack of 2+ the Quantity window |
| Shift+LMB / Ctrl+LMB on a stack | Whole stack / exactly one, no dialog (B442, measured) |
| LMB on Destroy with an item held | "Are you absolutely sure you want to destroy your %1?" (eqstr 2043); Yes = `OP_MoveItem 30 -> -1` (B442b, rows 34-35) |
| RMB (held) on an item slot | Item display while the button is held, gone on release (B442b, measured row 25) |
| Alt+LMB on an item slot | Item display, kept until ESC (B442; rows 26-27) |
| Alt (held), pointer over a bag or the bank | Name labels on every item in that window, while held (B1004, #323, measured); the pointer resting on any item shows its name after 600 ms |
| RMB on a bag in a general slot | Toggle its container window -- the `/bag` body (B442b, rows 19-21) |
| LMB (motionless) on the world with an item held | "ARE YOU SURE you wish to drop your %1 on the ground?" (eqstr 9057); Yes = `OP_GroundSpawn`. A press that moves is a camera drag (B442b, rows 31-33) |
| LMB on an inventory item with a merchant open | Selects it for sale -- never the cursor; the merchant button then reads Sell (B442b, #152) |
| Ctrl+LMB+drag | Look around (single-button mouse) |
| Alt+F | Toggle the Friends window (B897). MEASURED at the oracle's Options > Keys > UI page (`review/T377/oracle/T377_oracle_options_keys_UI_page1.png`: "Toggle friend window   Alt F", eqstr 6599, Alternate column empty). Scriptable as `/friendswindow` (registry 3). |
| Alt+O | Toggle the Options window (T434). MEASURED at the oracle's Options > Keys > UI page (`review/T377/oracle/T377_oracle_options_keys_UI_page2.png`: "Toggle options window   Alt O", Alternate column empty). fable bound a bare `O` to an arm that was `break;`; `O` is now unbound, as it is in the oracle. |
| Shift+ESC | Quit |
| Ctrl+F1 | Toggle wireframe |
| Ctrl+F2 | Toggle HUD |
| Ctrl+F3 | Toggle name tags |
| Ctrl+F4 | Toggle zone lights |
| F9 | Cycle the camera through the official set, in `eqstr_us.txt` 9500-9505 order: First Person, Overhead, Chase, User Defined Camera 1, User Defined Camera 2, Tether (T461, Will's ruling; F9 REPLACED Ctrl+F5 with no alias). Ctrl+F5 had never done anything: the receiver queued `RendererAction::ToggleCameraMode` and `processCommonInput()` had no arm for it. T466 MEASURED on the official client that F9 is its Toggle camera view key and that it cycles in exactly this order. A character with no saved `[Camera] View=` logs in on **First Person**, the official login camera (T466 measured, T471 applied). |
| Ctrl+F6 | Toggle Classic/Luclin models |

**Player Mode:**
| Key | Action |
|-----|--------|
| WASD/Arrows | Move (with collision) |
| Ctrl+A / End | Strafe left |
| Ctrl+D / Home | Strafe right (T313 moved this off PageDown, which now scrolls the UI) |
| Q | Toggle auto-attack |
| Ctrl+Q | Attack (initiate combat). B905 (#225): the key had no receiver arm until UIGAP-small; it now reaches `InputActionBridge`'s long-standing `InputAction::Attack` consumer |
| ` / NumLock / Numpad+ | Toggle autorun. Live since T343 and live-proven by T345 (board B002 CLOSED): the route is RENDERER-side — the receiver queues `RA::ToggleAutorun` on `actionQueue_`, `processCommonInput()` flips `PlayerCameraComponent::toggleAutorun()`, and movement phase 1 cancels it on a translational key (turning does not cancel). The game-side `EqActionHandler::toggleAutorun()` survives for the console handler alone |
| Space | Jump |
| 1-8 | Hotbar slots 1-8 (routed by slot content type: Spell → cast, Skill → useSkill, Item → useItemById (T230), Emote/Macro/Combat → activateHotbarCommandSlot (T239)) |
| 9-0 | Hotbar slots 9-10 (same routing) |
| Alt+1-8 | Cast spell from gem 1-8 |
| F1 | Target self |
| F2-F6 | Target group member 1-5 |
| F7 | Target nearest PC |
| F8 | Target nearest NPC |
| Tab | Cycle targets. B905 (#225): a Global `ChatAutocomplete` binding with no consumer SHADOWED this in every production run (`getAction()` resolves Global first); deleted with seven other consumerless actions, and `tests/test_hotkey_dispatch_census.cpp` now fails on any shadowed or consumerless binding |
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
| (none) | B582: the four volume keys (F10 / F11 / Shift+F10 / Shift+F11) and L's `CycleObjectLights` binding are GONE -- the official keymap has no volume key and L is the official Leadership window (post-Velious, ignored). Volume routes: the Options > General sliders plus `/musicvolume` and `/effectsvolume` (`CycleObjectLights` keeps its consumer, no key and no slash alias) |
| ESC | Close the most recently opened window, one per press (B460, measured); with none open, clear target. `/cleartarget` (no alias, category Combat) reaches the same `ActionDispatcher::clearTarget()` since T347 (board B012 CLOSED), so a `--script` run can clear a target |
| Numpad7 / Insert, Numpad1 / Delete, +/- | Camera zoom in / out: the active orbit camera's distance -- Chase's too since T474, which made Chase a rig at T466's oracle fit (T461, `PlayerCameraComponent::zoomCamera()`, shared with the wheel). T466 saw no visible change on the official client after Insert/Delete in Chase; whether the official Chase zooms is board B680. First person does not zoom. Numpad7/Insert and Numpad1/Delete are the official keys, MEASURED on the oracle's Options > Keys > Camera page (T466, bound by T471). fable's own +/- (and Shift+= / Numpad- in the shipped file) are kept; on the official client Shift+- / Shift+= are Clip plane and Numpad- is Screenshot (board B652). |
| Alt+Numpad9 / Alt+PageUp, Alt+Numpad3 / Alt+PageDown | Pitch the ACTIVE user camera up / down 5 degrees per press (`CameraPitchUp` / `CameraPitchDown`; keys measured, T466/T471). Pitch up tilts the VIEW up (the camera goes lower), as the official key does. T466 boarded fable's sign as mirrored; T471 found it already matched. The bare PageUp/PageDown still scroll the UI. |
| Alt+Numpad6 / Alt+Right, Alt+Numpad4 / Alt+Left | Tether-slide right / left (`CameraOrbitRight` / `CameraOrbitLeft`, the official `RIGHT_CAM` / `LEFT_CAM`): slides the Tether camera 5 degrees of world azimuth per press towards the viewer's right / left. It does nothing in other cameras (T466 pressed it in User 1 and nothing moved). The bare arrows stay movement: `getAction()` and the movement poll both compare modifiers exactly. |
| Alt+Numpad8 / Alt+Up, Alt+Numpad2 / Alt+Down | Raise / Lower camera (`CameraRaise` / `CameraLower`, T471): the Tether camera's look-at height, 1 unit per press. The keys are measured; what the official key moves, and its step, are ORACLE-OWED (board B653). The DRM/evdev device always sends the numpad codes; the `--egl-x11` device looks keys up in their NumLock-off form and maps none of the numpad digits (board B654). |
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
`[CombatAbilityWnd]` -- plus the camera, using the official client's own
filename and section names. **T461 changed the camera keys:** `[Camera] View=`
holds the official camera (1..6, `camera_mode.h`), and `User1Azimuth` /
`User1Pitch` / `User1Distance` / `User1Height` (and `User2*`) hold the two user
cameras (since T471 the azimuth is a WORLD azimuth, board B580), because the official client persists its own (`eqclient.ini`
`Camera1-*` / `Camera2-*`). T432's `Mode=` (0 Free, 1 Follow, 2 FirstPerson)
collides with the new numbering, so it is still READ — mapped explicitly, 0 and
1 to Chase, 2 to First Person — and never written. It is read at zone-in and written whenever a
toggle changes and at `/camp`. An ABSENT `Show=` key keeps the default, so
`defaults.ini`'s "a default is stated by omission" rule (above) still governs a
fresh character -- and, since T467 (board B560), a second character in the
same renderer: `setUiPreferencesIdentity()` arms a reset of every persisted
field to its default on a change FROM a character, and the next
`syncUiPreferences()` applies it before reading the new file
(`tests/test_ui_preferences_identity.cpp`). Loot, tradeskill-container and item-display windows are NOT
persisted -- the server or a click opens them, the player does not. Neither is
`F10`'s all-UI hide: a client that starts with no UI gives the player nothing
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
   (`src/client/action/command_processor_registration.cpp`, **156**
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
   of F10, T301), `/scroll [chat|skills|loot] [up|down|top|bottom] [lines]`
   (the scriptable half of the wheel and PageUp/PageDown, T313 — a `--script`
   run cannot generate either), `/camera` (T66; T461 — no argument reports
   the mode, `cycle` is F9, `<mode>` names one of the six official cameras,
   `set az <deg> pitch <deg> dist <units> height <units>` places the active
   user camera exactly (any subset; T474: in Chase it edits the Chase rig, whose
   distance and height are the fitted character's and scale with the player's
   eye; outside a user camera or Chase it switches to User
   Defined Camera 1 and says so), `nudge` adds the same four, `get` prints
   parseable `[CAMERA-GET]` lines — mode, the four values (T471: with `az_frame=world` for User Defined 1/2 and Tether, `az_frame=player_heading` for Overhead and Chase), the player pose,
   the ideal rig pose and the camera node's actual pose, plus in Chase a `chase eye_above_feet= fit_eye_above_feet= scale=` line — and `help` states
   the units; the grammar is `include/client/graphics/slash/camera_command.h`),
   `/skills`, `/inventory`, `/newbook`,
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
[name]` (`/findperson`), `/friendswho [names]` (no argument: the stored friends
list), `/friends [name]` (`/friend`; toggles the name, no name lists the list --
B897 ended its old aliasing to `/friendswho`, which the official client's
`/friends <name>` contradicts), `/ignore [name]` (toggle / list). The window
toggle is `/friendswindow` (registry 3).

**Character:** `/appearance <field> <0-255> ...` (T153/T167).

**Spells:** `/cast <gem#>`, `/mem <gem#> <spell>` (`/memspell`, `/memorize`),
`/forget <gem#>`, `/gems`, `/interrupt` (`/ducking`), `/spellbook` (`/book`).

**Skills:** `/bandage` (`/bindwound`; inbound reply parsed since T177),
`/train`, `/track [name]` (B895: starts tracking -- OP_TrackTarget, "You begin tracking %1." and the
direction lines), `/trackfilter <red|yellow|white|blue|lblue|green>`, `/tracksort
<normal|distance|consider|rdistance|rconsider>`, `/trackplayers <on|off|group>`, `/stoptracking`
(the official commands, with the official confirmation and FORMAT lines) and `/tracklist` (fable's:
prints the window's rows for a headless script). The skills window toggle is `/skills`
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
(`/recipefav`), `/clickobject <drop_id>` (`/clickobj`), `/combine [general slot]`, `/closecontainer`, `/bag <general slot>`
(B899: opens/closes a carried container's window).

**Commerce / Inventory:** `/vendor`, `/vendorlist`, `/buy <slot> [qty]`,
`/sell <slot> [qty]`, `/bank [deposit|withdraw|convert|change]`, `/equip <from> <to> | <item name> | list`,
`/pickup <slot> [qty]`, `/putdown <slot>`, `/cursor`, `/coin <pp|gp|sp|cp> <n>`
(B442: the click-to-cursor gesture's scriptable half, one `CursorGestureIntent`).

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
