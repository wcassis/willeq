#pragma once

#include "inventory_window.h"
#include "bag_window.h"
#include "bank_window.h"
#include "loot_window.h"
#include "vendor_window.h"
#include "trade_window.h"
#include "trade_request_dialog.h"
#include "money_input_dialog.h"
#include "chat_window.h"
#include "spell_gem_panel.h"
#include "spell_book_window.h"
#include "buff_window.h"
#include "group_window.h"
#include "pet_window.h"
#include "player_status_window.h"
#include "hotbar_window.h"
#include "hotbar_cursor.h"
#include "skills_window.h"
#include "skill_trainer_window.h"
#include "note_window.h"
#include "options_window.h"
#include "casting_bar.h"
#include "tradeskill_container_window.h"
#include "item_tooltip.h"
#include "buff_tooltip.h"
#include "item_icon_loader.h"
#include "inventory_manager.h"
#include "ui_settings.h"
#include "client/spell/spell_data.h"
#include <json/json.h>
#include <memory>

class TradeManager;

namespace EQ {
struct ActiveBuff;
class SpellDatabase;
class SkillManager;
}

namespace EQT {
namespace Graphics {
class RaceModelLoader;
class EquipmentModelLoader;
struct EntityAppearance;
}
}
#include <vector>
#include <map>

namespace eqt {
namespace ui {

// Confirmation dialog state
enum class ConfirmDialogType {
    None,
    DestroyItem
};

class WindowManager {
public:
    WindowManager();
    ~WindowManager();

    // Initialize with Irrlicht components
    void init(irr::video::IVideoDriver* driver,
              irr::gui::IGUIEnvironment* gui,
              inventory::InventoryManager* invManager,
              int screenWidth, int screenHeight,
              const std::string& eqClientPath = "");

    // Get item icon texture
    irr::video::ITexture* getItemIcon(uint32_t iconId);

    // Screen resize
    void onResize(int screenWidth, int screenHeight);

    // UI Settings Management
    void toggleUILock();
    bool isUILocked() const;
    bool saveUILayout(const std::string& path = "");
    bool loadUILayout(const std::string& path = "");
    void resetUIToDefaults();
    void applyUISettings();

    // Window management
    void toggleInventory();
    void openInventory();
    void closeInventory();
    void closeAllWindows();
    InventoryWindow* getInventoryWindow() { return inventoryWindow_.get(); }
    const InventoryWindow* getInventoryWindow() const { return inventoryWindow_.get(); }

    // Bag window management
    void toggleBagWindow(int16_t generalSlot);
    void openBagWindow(int16_t generalSlot);
    void closeBagWindow(int16_t generalSlot);
    void closeAllBagWindows();
    bool isBagWindowOpen(int16_t generalSlot) const;

    // Bank window management
    void openBankWindow();
    void closeBankWindow();
    void toggleBankWindow();
    bool isBankWindowOpen() const;
    BankWindow* getBankWindow() { return bankWindow_.get(); }
    const BankWindow* getBankWindow() const { return bankWindow_.get(); }

    // Bank bag window management
    void openBankBagWindow(int16_t bankSlot);
    void closeBankBagWindow(int16_t bankSlot);
    void closeAllBankBagWindows();
    bool isBankBagWindowOpen(int16_t bankSlot) const;

    // Bank window callbacks (set by EverQuest class for packet handling)
    void setOnBankClose(BankCloseCallback callback);
    void setOnBankCurrencyMove(BankCurrencyMoveCallback callback);
    void setOnBankCurrencyConvert(BankCurrencyConvertCallback callback);

    // Loot window management
    void openLootWindow(uint16_t corpseId, const std::string& corpseName);
    void closeLootWindow();
    bool isLootWindowOpen() const;
    LootWindow* getLootWindow() { return lootWindow_.get(); }
    const LootWindow* getLootWindow() const { return lootWindow_.get(); }

    // Loot item management (called from packet handlers)
    void addLootItem(int16_t slot, std::unique_ptr<inventory::ItemInstance> item);
    void removeLootItem(int16_t slot);
    void clearLootItems();

    // Loot window callbacks (set by EverQuest class for packet handling)
    void setOnLootItem(LootItemCallback callback);
    void setOnLootAll(LootAllCallback callback);
    void setOnDestroyAll(DestroyAllCallback callback);
    void setOnLootClose(LootCloseCallback callback);

    // Vendor window management
    void openVendorWindow(uint16_t npcId, const std::string& vendorName, float sellRate);
    void closeVendorWindow();
    bool isVendorWindowOpen() const;
    VendorWindow* getVendorWindow() { return vendorWindow_.get(); }
    const VendorWindow* getVendorWindow() const { return vendorWindow_.get(); }

    // Vendor item management (called from packet handlers)
    void addVendorItem(uint32_t slot, std::unique_ptr<inventory::ItemInstance> item);
    void removeVendorItem(uint32_t slot);
    void clearVendorItems();

    // Refresh sellable items from player inventory (for vendor sell mode)
    void refreshVendorSellableItems();

    // Vendor window callbacks (set by EverQuest class for packet handling)
    void setOnVendorBuy(VendorBuyCallback callback);
    void setOnVendorSell(VendorSellCallback callback);
    void setOnVendorClose(VendorCloseCallback callback);

    // Trade window management
    void initTradeWindow(TradeManager* tradeMgr);
    void openTradeWindow(uint32_t partnerSpawnId, const std::string& partnerName, bool isNpcTrade = false);
    void closeTradeWindow(bool sendCancel = true);
    bool isTradeWindowOpen() const;
    TradeWindow* getTradeWindow() { return tradeWindow_.get(); }
    const TradeWindow* getTradeWindow() const { return tradeWindow_.get(); }

    // Trade request dialog
    void showTradeRequest(uint32_t spawnId, const std::string& playerName);
    void dismissTradeRequest();
    bool isTradeRequestShown() const;

    // Money input dialog
    bool isMoneyInputDialogShown() const;

    // Trade partner item management (called from packet handlers)
    void setTradePartnerItem(int slot, std::unique_ptr<inventory::ItemInstance> item);
    void clearTradePartnerItem(int slot);

    // Trade money display
    void setTradeOwnMoney(uint32_t pp, uint32_t gp, uint32_t sp, uint32_t cp);
    void setTradePartnerMoney(uint32_t pp, uint32_t gp, uint32_t sp, uint32_t cp);

    // Trade accept state display
    void setTradeOwnAccepted(bool accepted);
    void setTradePartnerAccepted(bool accepted);

    // Trade window callbacks (set by EverQuest class for packet handling)
    void setOnTradeAccept(TradeAcceptCallback callback);
    void setOnTradeCancel(TradeCancelCallback callback);
    void setOnTradeRequestAccept(TradeRequestAcceptCallback callback);
    void setOnTradeRequestDecline(TradeRequestDeclineCallback callback);
    void setOnTradeError(TradeErrorCallback callback);

    // Chat window management
    ChatWindow* getChatWindow() { return chatWindow_.get(); }
    const ChatWindow* getChatWindow() const { return chatWindow_.get(); }
    bool isChatInputFocused() const;
    void focusChatInput();
    void unfocusChatInput();
    void setChatSubmitCallback(ChatSubmitCallback callback);

    // Spell gem panel management
    void initSpellGemPanel(EQ::SpellManager* spellMgr);
    SpellGemPanel* getSpellGemPanel() { return spellGemPanel_.get(); }
    const SpellGemPanel* getSpellGemPanel() const { return spellGemPanel_.get(); }
    void setGemCastCallback(GemCastCallback callback);
    void setGemForgetCallback(GemForgetCallback callback);

    // Spellbook window management
    void toggleSpellbook();
    void openSpellbook();
    void closeSpellbook();
    bool isSpellbookOpen() const;
    SpellBookWindow* getSpellBookWindow() { return spellBookWindow_.get(); }
    const SpellBookWindow* getSpellBookWindow() const { return spellBookWindow_.get(); }
    void setSpellMemorizeCallback(SpellClickCallback callback);
    void setSpellbookStateCallback(SpellbookStateCallback callback);
    void setSpellScrollPickupCallback(SpellScrollPickupCallback callback);
    void setScribeSpellRequestCallback(ScribeSpellRequestCallback callback);

    // Buff window management
    void initBuffWindow(EQ::BuffManager* buffMgr);
    void toggleBuffWindow();
    void openBuffWindow();
    void closeBuffWindow();
    bool isBuffWindowOpen() const;
    BuffWindow* getBuffWindow() { return buffWindow_.get(); }
    const BuffWindow* getBuffWindow() const { return buffWindow_.get(); }
    void setBuffCancelCallback(BuffCancelCallback callback);

    // Group window management
    void initGroupWindow(EverQuest* eq);
    void toggleGroupWindow();
    void openGroupWindow();
    void closeGroupWindow();
    bool isGroupWindowOpen() const;
    GroupWindow* getGroupWindow() { return groupWindow_.get(); }
    const GroupWindow* getGroupWindow() const { return groupWindow_.get(); }
    void setGroupInviteCallback(GroupInviteCallback callback);
    void setGroupDisbandCallback(GroupDisbandCallback callback);
    void setGroupAcceptCallback(GroupAcceptCallback callback);
    void setGroupDeclineCallback(GroupDeclineCallback callback);

    // Pet window management
    void initPetWindow(EverQuest* eq, EQ::BuffManager* buffMgr = nullptr);
    void togglePetWindow();
    void openPetWindow();
    void closePetWindow();
    bool isPetWindowOpen() const;
    PetWindow* getPetWindow() { return petWindow_.get(); }
    const PetWindow* getPetWindow() const { return petWindow_.get(); }
    void setPetCommandCallback(PetCommandCallback callback);

    // Hotbar window management
    void initHotbarWindow();
    void toggleHotbar();
    void openHotbar();
    void closeHotbar();
    bool isHotbarOpen() const;
    HotbarWindow* getHotbarWindow() { return hotbarWindow_.get(); }
    const HotbarWindow* getHotbarWindow() const { return hotbarWindow_.get(); }
    void setHotbarActivateCallback(HotbarActivateCallback callback);
    void setHotbarChangedCallback(HotbarChangedCallback callback);
    void startHotbarCooldown(int buttonIndex, uint32_t durationMs);
    void startSkillCooldown(uint32_t skillId, uint32_t durationMs);

    // Hotbar data persistence (for saving to per-character config)
    Json::Value collectHotbarData() const;
    void loadHotbarData(const Json::Value& data);

    // Hotbar cursor operations
    bool hasHotbarCursor() const { return hotbarCursor_.hasItem(); }
    void setHotbarCursor(HotbarButtonType type, uint32_t id,
                         const std::string& emoteText, uint32_t iconId);
    void clearHotbarCursor();
    const HotbarCursor& getHotbarCursor() const { return hotbarCursor_; }

    // Skills window management
    void initSkillsWindow(EQ::SkillManager* skillMgr);
    void toggleSkillsWindow();
    void openSkillsWindow();
    void closeSkillsWindow();
    bool isSkillsWindowOpen() const;
    SkillsWindow* getSkillsWindow() { return skillsWindow_.get(); }
    const SkillsWindow* getSkillsWindow() const { return skillsWindow_.get(); }
    void setSkillActivateCallback(SkillActivateCallback callback);
    void setHotbarCreateCallback(HotbarCreateCallback callback);

    // Skill trainer window management
    void initSkillTrainerWindow();
    void openSkillTrainerWindow(uint32_t trainerId, const std::wstring& trainerName,
                                const std::vector<TrainerSkillEntry>& skills);
    void closeSkillTrainerWindow();
    bool isSkillTrainerWindowOpen() const;
    void updateSkillTrainerSkill(uint8_t skillId, uint32_t newValue);
    void updateSkillTrainerMoney(uint32_t platinum, uint32_t gold,
                                 uint32_t silver, uint32_t copper);
    void updateSkillTrainerPracticePoints(uint32_t points);
    void decrementSkillTrainerPracticePoints();
    SkillTrainerWindow* getSkillTrainerWindow() { return skillTrainerWindow_.get(); }
    const SkillTrainerWindow* getSkillTrainerWindow() const { return skillTrainerWindow_.get(); }
    void setSkillTrainCallback(SkillTrainCallback callback);
    void setTrainerCloseCallback(TrainerCloseCallback callback);

    // Note window management (for reading books/notes)
    void showNoteWindow(const std::string& text, uint8_t type);
    void closeNoteWindow();
    bool isNoteWindowOpen() const;
    NoteWindow* getNoteWindow() { return noteWindow_.get(); }
    const NoteWindow* getNoteWindow() const { return noteWindow_.get(); }

    // Read item callback (set by EverQuest to handle book/note reading requests)
    void setOnReadItem(ReadItemCallback callback);

    // Options window management
    void initOptionsWindow();
    void toggleOptionsWindow();
    void openOptionsWindow();
    void closeOptionsWindow();
    bool isOptionsWindowOpen() const;
    OptionsWindow* getOptionsWindow() { return optionsWindow_.get(); }
    const OptionsWindow* getOptionsWindow() const { return optionsWindow_.get(); }
    void setDisplaySettingsChangedCallback(DisplaySettingsChangedCallback callback);

    // Tradeskill container window management
    void openTradeskillContainer(uint32_t dropId, const std::string& name,
                                  uint8_t objectType, int slotCount);
    void openTradeskillContainerForItem(int16_t containerSlot, const std::string& name,
                                        uint8_t bagType, int slotCount);
    void closeTradeskillContainer();
    bool isTradeskillContainerOpen() const;
    TradeskillContainerWindow* getTradeskillContainerWindow() { return tradeskillWindow_.get(); }
    const TradeskillContainerWindow* getTradeskillContainerWindow() const { return tradeskillWindow_.get(); }

    // Tradeskill container callbacks (set by EverQuest for packet handling)
    void setOnTradeskillCombine(TradeskillCombineCallback callback);
    void setOnTradeskillClose(TradeskillCloseCallback callback);

    // Player status window management
    void initPlayerStatusWindow(EverQuest* eq);
    PlayerStatusWindow* getPlayerStatusWindow() { return playerStatusWindow_.get(); }
    const PlayerStatusWindow* getPlayerStatusWindow() const { return playerStatusWindow_.get(); }

    // Casting bar management (player's casting bar)
    void initCastingBar();
    void startCast(const std::string& spellName, uint32_t castTimeMs);
    void cancelCast();
    void completeCast();
    bool isCastingBarActive() const;
    CastingBar* getCastingBar() { return castingBar_.get(); }
    const CastingBar* getCastingBar() const { return castingBar_.get(); }

    // Target casting bar management (shows target's casting)
    void startTargetCast(const std::string& casterName, const std::string& spellName, uint32_t castTimeMs);
    void cancelTargetCast();
    void completeTargetCast();
    bool isTargetCastingBarActive() const;
    CastingBar* getTargetCastingBar() { return targetCastingBar_.get(); }
    const CastingBar* getTargetCastingBar() const { return targetCastingBar_.get(); }

    // Memorizing bar management (shows spell memorization progress)
    void startMemorize(const std::string& spellName, uint32_t durationMs);
    void cancelMemorize();
    void completeMemorize();
    bool isMemorizingBarActive() const;
    CastingBar* getMemorizingBar() { return memorizingBar_.get(); }
    const CastingBar* getMemorizingBar() const { return memorizingBar_.get(); }

    // Spell cursor management (for spellbook-to-spellbar memorization)
    void setSpellOnCursor(uint32_t spellId, irr::video::ITexture* icon);
    void clearSpellCursor();
    bool hasSpellOnCursor() const;
    uint32_t getSpellOnCursor() const;

    // Input handling (returns true if input was consumed)
    bool handleKeyPress(irr::EKEY_CODE key, bool shift, bool ctrl = false);
    bool handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl = false);
    bool handleMouseUp(int x, int y, bool leftButton);
    bool handleMouseMove(int x, int y);
    bool handleMouseWheel(float delta);

    // Rendering
    void render();
    void update(uint32_t currentTimeMs);

    // State queries
    bool isInventoryOpen() const;
    bool hasOpenWindows() const;
    bool hasCursorItem() const;
    bool hasLootCursorItem() const;
    bool hasAnyCursorItem() const;  // Either inventory or loot cursor

    // Loot cursor operations (click-to-move from loot window)
    void pickupLootItem(uint16_t corpseId, int16_t lootSlot);
    void placeLootItem();  // Place loot item into inventory (triggers loot callback)
    void cancelLootCursor();  // Cancel loot pickup

    // Character info
    void setCharacterInfo(const std::wstring& name, int level, const std::wstring& className);
    void setCharacterDeity(const std::wstring& deity);
    void setExpProgress(float progress);  // 0.0 to 1.0
    void updateCharacterStats(uint32_t curHp, uint32_t maxHp,
                              uint32_t curMana, uint32_t maxMana,
                              uint32_t curEnd, uint32_t maxEnd,
                              int ac, int atk,
                              int str, int sta, int agi, int dex, int wis, int intel, int cha,
                              int pr, int mr, int dr, int fr, int cr,
                              float weight, float maxWeight,
                              uint32_t platinum, uint32_t gold, uint32_t silver, uint32_t copper);

    // Update just the base currency values (called when server sends money update)
    void updateBaseCurrency(uint32_t platinum, uint32_t gold, uint32_t silver, uint32_t copper);

    // Update bank currency values (called when bank window is opened or currency changes)
    void updateBankCurrency(uint32_t platinum, uint32_t gold, uint32_t silver, uint32_t copper);

    // Character model view (3D preview in inventory)
    void initModelView(irr::scene::ISceneManager* smgr,
                       EQT::Graphics::RaceModelLoader* raceLoader,
                       EQT::Graphics::EquipmentModelLoader* equipLoader);
    void setPlayerAppearance(uint16_t raceId, uint8_t gender,
                             const EQT::Graphics::EntityAppearance& appearance);

    // Confirmation dialog
    void showConfirmDialog(ConfirmDialogType type, const std::wstring& message);
    void closeConfirmDialog();
    bool isConfirmDialogOpen() const { return confirmDialogType_ != ConfirmDialogType::None; }

    // Quantity slider (for shift+click on stacks)
    bool isQuantitySliderOpen() const { return quantitySliderActive_; }

    // Item tooltip for chat links
    void showItemTooltip(const inventory::ItemInstance* item, int mouseX, int mouseY);

    // Buff tooltip
    void showBuffTooltip(const EQ::ActiveBuff* buff, int mouseX, int mouseY);
    void hideBuffTooltip();

private:
    // Slot click handling
    void handleSlotClick(int16_t slotId, bool shift, bool ctrl);
    void handleBagSlotClick(int16_t slotId, bool shift, bool ctrl);
    void handleBankSlotClick(int16_t slotId, bool shift, bool ctrl);
    void handleTradeSlotClick(int16_t tradeSlot, bool shift, bool ctrl);
    void handleTradeMoneyAreaClick();
    void handleTradeskillSlotClick(int16_t slotId, bool shift, bool ctrl);
    void handleTradeskillSlotHover(int16_t slotId, int mouseX, int mouseY);
    void handleSlotHover(int16_t slotId, int mouseX, int mouseY);
    void handleLootSlotHover(int16_t slotId, int mouseX, int mouseY);
    void handleDestroyClick();
    void handleBagOpenClick(int16_t generalSlot);
    void handleBankBagOpenClick(int16_t bankSlot);
    void handleCurrencyClick(CurrencyType type, uint32_t maxAmount);
    void handleMoneyInputConfirm(CurrencyType type, uint32_t amount);
    void refreshCurrencyDisplay();  // Update inventory currency display accounting for cursor money

    // Cursor item handling
    void pickupItem(int16_t slotId);
    void placeItem(int16_t targetSlot);
    void returnCursorItem();

    // Window positioning
    void positionInventoryWindow();
    void positionLootWindow();
    void positionVendorWindow();
    void positionBankWindow();
    void tileBagWindows();
    void tileBankBagWindows();
    void positionCastingBarAboveChat();
    irr::core::recti getBagTilingArea() const;

    // UI Settings helpers
    void collectWindowPositions();
    void applyWindowPositions();

    // Window hover state tracking (for unlock highlighting)
    void updateWindowHoverStates(int x, int y);

    // Rendering helpers
    void renderCursorItem();
    void renderCursorMoney();
    void renderSpellCursor();
    void renderConfirmDialog();
    void renderQuantitySlider();
    void renderSpellTooltip();
    void renderLockIndicator();

    // Quantity slider helpers
    void showQuantitySlider(int16_t slotId, int32_t maxQuantity);
    void closeQuantitySlider();
    void confirmQuantitySlider();

    // Hit testing
    WindowBase* getWindowAtPosition(int x, int y);

    // Z-order management - windows at back of vector are on top (rendered last, checked first)
    void bringToFront(WindowBase* window);
    void addToZOrder(WindowBase* window);
    void removeFromZOrder(WindowBase* window);
    std::vector<WindowBase*> windowZOrder_;  // Windows in z-order (back = topmost)

    // Irrlicht components
    irr::video::IVideoDriver* driver_ = nullptr;
    irr::gui::IGUIEnvironment* gui_ = nullptr;

    // Inventory manager
    inventory::InventoryManager* invManager_ = nullptr;

    // Trade manager
    TradeManager* tradeManager_ = nullptr;

    // Screen dimensions
    int screenWidth_ = 800;
    int screenHeight_ = 600;

    // Base currency values (before cursor money is deducted)
    uint32_t basePlatinum_ = 0;
    uint32_t baseGold_ = 0;
    uint32_t baseSilver_ = 0;
    uint32_t baseCopper_ = 0;

    // Bank currency values
    uint32_t bankPlatinum_ = 0;
    uint32_t bankGold_ = 0;
    uint32_t bankSilver_ = 0;
    uint32_t bankCopper_ = 0;

    // Windows
    std::unique_ptr<InventoryWindow> inventoryWindow_;
    std::unique_ptr<LootWindow> lootWindow_;
    std::unique_ptr<VendorWindow> vendorWindow_;
    std::unique_ptr<TradeWindow> tradeWindow_;
    std::unique_ptr<TradeRequestDialog> tradeRequestDialog_;
    std::unique_ptr<MoneyInputDialog> moneyInputDialog_;
    std::unique_ptr<ChatWindow> chatWindow_;
    std::unique_ptr<SpellGemPanel> spellGemPanel_;
    std::unique_ptr<SpellBookWindow> spellBookWindow_;
    std::unique_ptr<BuffWindow> buffWindow_;
    std::unique_ptr<GroupWindow> groupWindow_;
    std::unique_ptr<PetWindow> petWindow_;
    std::unique_ptr<HotbarWindow> hotbarWindow_;
    HotbarCursor hotbarCursor_;
    std::unique_ptr<SkillsWindow> skillsWindow_;
    std::unique_ptr<SkillTrainerWindow> skillTrainerWindow_;
    std::unique_ptr<NoteWindow> noteWindow_;
    std::unique_ptr<OptionsWindow> optionsWindow_;
    std::unique_ptr<TradeskillContainerWindow> tradeskillWindow_;
    std::unique_ptr<PlayerStatusWindow> playerStatusWindow_;
    std::unique_ptr<CastingBar> castingBar_;
    std::unique_ptr<CastingBar> targetCastingBar_;  // For showing target's casting
    std::unique_ptr<CastingBar> memorizingBar_;     // For showing spell memorization progress
    std::map<int16_t, std::unique_ptr<BagWindow>> bagWindows_;  // keyed by parent slot ID

    // Bank window
    std::unique_ptr<BankWindow> bankWindow_;
    std::map<int16_t, std::unique_ptr<BagWindow>> bankBagWindows_;  // keyed by bank slot ID

    // Bank window callbacks
    BankCloseCallback onBankCloseCallback_;
    BankCurrencyMoveCallback onBankCurrencyMoveCallback_;
    BankCurrencyConvertCallback onBankCurrencyConvertCallback_;

    // Currency click source tracking (for bank currency movement)
    enum class CurrencyClickSource { None, Inventory, Bank };
    CurrencyClickSource currencyClickSource_ = CurrencyClickSource::None;

    // Loot window callbacks
    LootItemCallback onLootItemCallback_;
    LootAllCallback onLootAllCallback_;
    DestroyAllCallback onDestroyAllCallback_;
    LootCloseCallback onLootCloseCallback_;

    // Vendor window callbacks
    VendorBuyCallback onVendorBuyCallback_;
    VendorSellCallback onVendorSellCallback_;
    VendorCloseCallback onVendorCloseCallback_;

    // Trade window callbacks
    TradeAcceptCallback onTradeAcceptCallback_;
    TradeCancelCallback onTradeCancelCallback_;
    TradeRequestAcceptCallback onTradeRequestAcceptCallback_;
    TradeRequestDeclineCallback onTradeRequestDeclineCallback_;
    TradeErrorCallback onTradeErrorCallback_;

    // Buff window callbacks
    BuffCancelCallback buffCancelCallback_;

    // Spellbook callbacks
    SpellClickCallback spellMemorizeCallback_;
    SpellbookStateCallback spellbookStateCallback_;
    SpellScrollPickupCallback spellScrollPickupCallback_;
    ScribeSpellRequestCallback scribeSpellRequestCallback_;

    // Group window callbacks
    GroupInviteCallback groupInviteCallback_;
    GroupDisbandCallback groupDisbandCallback_;
    GroupAcceptCallback groupAcceptCallback_;
    GroupDeclineCallback groupDeclineCallback_;

    // Pet window callback
    PetCommandCallback petCommandCallback_;

    // Hotbar callbacks
    HotbarActivateCallback hotbarActivateCallback_;
    HotbarChangedCallback hotbarChangedCallback_;

    // Skills window callbacks
    SkillActivateCallback skillActivateCallback_;
    HotbarCreateCallback hotbarCreateCallback_;

    // Skill trainer window callbacks
    SkillTrainCallback skillTrainCallback_;
    TrainerCloseCallback trainerCloseCallback_;

    // Read item callback (for book/note reading)
    ReadItemCallback readItemCallback_;

    // Options window callback
    DisplaySettingsChangedCallback displaySettingsChangedCallback_;

    // Tradeskill container callbacks
    TradeskillCombineCallback tradeskillCombineCallback_;
    TradeskillCloseCallback tradeskillCloseCallback_;

    // Tooltips
    ItemTooltip tooltip_;
    BuffTooltip buffTooltip_;

    // Item icon loader
    ItemIconLoader iconLoader_;

    // Mouse state
    int mouseX_ = 0;
    int mouseY_ = 0;

    // Loot cursor state (for click-to-move from loot window)
    uint16_t lootCursorCorpseId_ = 0;
    int16_t lootCursorSlot_ = inventory::SLOT_INVALID;
    const inventory::ItemInstance* lootCursorItem_ = nullptr;
    int16_t pendingLootTargetSlot_ = inventory::SLOT_INVALID;  // Auto-place looted item here

    // Confirmation dialog
    ConfirmDialogType confirmDialogType_ = ConfirmDialogType::None;
    std::wstring confirmDialogMessage_;
    irr::core::recti confirmDialogBounds_;
    irr::core::recti confirmYesButtonBounds_;
    irr::core::recti confirmNoButtonBounds_;
    bool confirmYesHighlighted_ = false;
    bool confirmNoHighlighted_ = false;

    // Quantity slider state (for shift+click on stacks)
    bool quantitySliderActive_ = false;
    int16_t quantitySliderSlot_ = inventory::SLOT_INVALID;
    int32_t quantitySliderMax_ = 1;
    int32_t quantitySliderValue_ = 1;
    irr::core::recti quantitySliderBounds_;
    irr::core::recti quantitySliderTrackBounds_;
    irr::core::recti quantitySliderOkBounds_;
    irr::core::recti quantitySliderCancelBounds_;
    bool quantitySliderOkHighlighted_ = false;
    bool quantitySliderCancelHighlighted_ = false;
    bool quantitySliderDragging_ = false;

    // Spell tooltip for spell gem panel hover
    EQ::SpellManager* spellMgr_ = nullptr;
    uint32_t hoveredSpellId_ = 0xFFFFFFFF;  // EQ::SPELL_UNKNOWN
    int hoveredSpellX_ = 0;
    int hoveredSpellY_ = 0;

    // Emote dialog state (for hotbar right-click)
    bool emoteDialogActive_ = false;
    int emoteDialogSlot_ = -1;
    std::string emoteDialogText_;
    irr::core::recti emoteDialogBounds_;
    irr::core::recti emoteDialogInputBounds_;
    irr::core::recti emoteDialogOkBounds_;
    irr::core::recti emoteDialogCancelBounds_;
    bool emoteDialogOkHighlighted_ = false;
    bool emoteDialogCancelHighlighted_ = false;

    // Spell cursor state (for spellbook-to-spellbar memorization)
    struct SpellCursorState {
        bool active = false;
        uint32_t spellId = 0xFFFFFFFF;  // EQ::SPELL_UNKNOWN
        irr::video::ITexture* icon = nullptr;
    };
    SpellCursorState spellCursor_;

    // Layout constants
    static constexpr int INVENTORY_X = 50;
    static constexpr int INVENTORY_Y = 50;
    static constexpr int WINDOW_MARGIN = 10;
    static constexpr int RESERVED_LOOT_WIDTH = 250;  // Space for future loot window
    static constexpr int RESERVED_TRADE_HEIGHT = 200;  // Space for future trade window
};

} // namespace ui
} // namespace eqt
