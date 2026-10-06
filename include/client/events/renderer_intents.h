#pragma once

#include <cstdint>
#include <string>
#include <variant>

// T156: the hotbar numbering, shared by value so HotbarActivateIntent cannot
// carry a raw uint8_t in some other scheme. Dependency-free header.
#include "client/game_options.h"  // B430: GameOptionIntent
#include "client/hotbar/hotbar_slot_type.h"

namespace eqt {
namespace events {

// ============================================================================
// Renderer intent structs — posted by the renderer to the game thread via
// the GameStateBridge intent queue. Each struct represents a user action
// that requires game-state mutation or network communication.
//
// No renderer or EverQuest includes allowed in this file.
// ============================================================================

// --- Movement ---

struct PlayerPositionChanged {
    float x, y, z;
    float heading;
    float dx, dy, dz;  // Velocity/delta (animation derived on game thread from velocity + water state)
    // T332 (B049): PROVENANCE of x/y/z. False means the renderer DERIVED this
    // position itself (its movement integrator wrote it). True means the
    // renderer is echoing back a position it was HANDED through
    // setPlayerPosition() -- the zone-load snapshot seed, or a PlayerMoved
    // relocation -- and has not moved since. An echo carries no information the
    // game thread does not already hold, and at zone-in the thing it echoes is
    // a raw WIRE z, so the game thread must not relabel it as render frame.
    // See BridgeComponent::OnGraphicsMovement and position_frames.h.
    bool seededPosition = false;
};

// T311: the player model's own measured origin-to-feet distance, published by
// the renderer whenever it changes (model load, race/size change).
//
// The game thread needs it to convert between the RENDER frame it receives from
// the renderer and the WIRE frame it must transmit -- see
// include/client/position_frames.h. It is a property of the SHIPPED MESH, so
// only the renderer can measure it (EntityRenderer::getPlayerCollisionZOffset,
// from collisionZOffsetFromBounds); it is pushed as a one-way intent rather than
// read back synchronously because EverQuest holds no renderer pointer by design
// (CLAUDE.md, "Key separation").
struct PlayerModelGeometryChanged {
    float collisionZOffset;  // |bbox.MinEdge.Y| * scale, world units; 0 = unknown
};

// --- Targeting ---

struct TargetIntent {
    uint16_t spawnId;  // 0 = clear target
};

// --- Chat ---

struct ChatSubmitIntent {
    std::string text;  // Full text including /channel prefix
};

// --- Interaction ---

struct DoorInteractIntent {
    uint8_t doorId;
};

struct LootCorpseIntent {
    uint16_t spawnId;
};

struct LootItemIntent {
    uint16_t corpseId;
    uint8_t slot;
};

struct LootAllIntent {
    uint16_t corpseId;
};

struct CloseLootIntent {
    uint16_t corpseId;
};

struct WorldObjectInteractIntent {
    uint32_t dropId;
};

struct ZoningEnabledIntent {
    bool enabled;
};

// T273: `filename` is the server-side book key (eqemu `books`.`name`, the
// item's Filename field) -- NOT the text, which only comes back in the
// server's OP_ReadBook reply. `type` is the item's BookType, echoed back by
// the server on Titanium: 0=scroll, 1=book, 2=item info.
struct ReadItemIntent {
    std::string filename;
    uint8_t type;
};

// --- Vendor ---

struct VendorToggleIntent {};  // NPC class/distance validation stays on game thread

struct VendorBuyIntent {
    uint16_t npcId;
    uint16_t slot;
    uint8_t quantity;
};

struct VendorSellIntent {
    uint16_t npcId;
    uint16_t slot;
    uint8_t quantity;
};

// --- Bank ---

struct BankerInteractIntent {
    uint16_t npcId;  // Validation stays on game thread
};

// T436 (board B429): the bank window's BW_DoneButton and its Style_Closebox.
// BankerInteractIntent cannot close -- its handler returns early when the
// window is already open -- and there was no other renderer-side way to end a
// bank session, so this is the CloseTrainerIntent shape: an empty close
// intent whose handler is the existing MerchantComponent::CloseBankWindow().
// The panel closes when BankWindowClosed comes back, never optimistically.
struct CloseBankIntent {};

// T269: produced by EverQuest::RequestBankCoinMove() (/bank deposit|withdraw)
// -- the same intent a bank-window coin gesture would push. Consumed by the
// eq_bridge.cpp arm that builds OP_MoveCoin {inventory<->bank, cointype1==
// cointype2} (eqemu zone/client_process.cpp OPMoveCoin: slot 1 = inventory,
// slot 2 = bank; cointype 0=cp 1=sp 2=gp 3=pp).
struct BankCurrencyMoveIntent {
    uint8_t coinType;   // 0=copper, 1=silver, 2=gold, 3=platinum
    int32_t amount;
    bool fromBank;      // true = bank→inventory, false = inventory→bank
};

// T269: produced by EverQuest::RequestBankCoinConvert() (/bank convert).
// OP_MoveCoin bank->bank with cointype1 != cointype2; the server converts at
// CoinTypeCoppers(from)/CoinTypeCoppers(to) (10:1 per step, either
// direction, truncating) -- so toCoinType is explicit rather than the old
// implied "fromCoinType + 1".
struct BankCurrencyConvertIntent {
    uint8_t fromCoinType;  // 0=copper, 1=silver, 2=gold, 3=platinum
    uint8_t toCoinType;    // same scale; must differ from fromCoinType
    int32_t amount;        // in fromCoinType coins
};

// --- Trainer ---

struct TrainerToggleIntent {};  // Validation stays on game thread

// --- Spells ---

struct CastSpellIntent {
    uint8_t gemSlot;
};

struct MemorizeSpellIntent {
    uint8_t gemSlot;
    uint32_t spellId;
};

struct ScribeSpellIntent {
    uint32_t spellId;
    // T276: was uint8_t. The Titanium spellbook is 400 slots
    // (EQ::MAX_SPELLBOOK_SLOTS, eqemu EQ::spells::SPELLBOOK_SIZE) and
    // EverQuest::ScribeSpellFromScroll()/SpellManager::scribeSpell() have
    // always taken uint16_t, so the intent was the one narrowing point on
    // the route -- book slots 256..399 would have wrapped to 0..143 and
    // scribed over an occupied slot. Never observed because nothing
    // produced the intent (T264 F5).
    uint16_t bookSlot;
    int16_t sourceSlot;
};

struct SpellbookStateIntent {
    bool isOpen;  // Sends appearance animation
};

// --- Buffs ---

struct BuffCancelIntent {
    uint8_t slot;
};

// --- Skills ---

struct SkillActivateIntent {
    uint32_t skillId;
};

// D20f4: BspTreeAvailableIntent removed — BSP tree set via typed setter

// --- Volume ---

// T439 (board B432): ABSOLUTE, on the oracle's own 0..20 slider step
// (eqt::audio::kVolumeSteps, include/client/audio/volume_steps.h). These
// carried a `float delta` until T439, which is why the renderer had to keep
// a shadow record of what it last told the audio manager -- and why a
// /musicvolume, which sets it absolutely, left the Options slider stale and
// made its next drag apply a delta computed from a stale value. The audio
// manager's applied step comes back on GameEventType::VolumesChanged, so
// neither side has to remember anything.
struct MusicVolumeChangeIntent {
    int step;  // 0..20, clamped by the handler
};

struct EffectsVolumeChangeIntent {
    int step;  // 0..20, clamped by the handler
};

// --- Options window (B430/B433) ---

// A server-owned Options setting the player changed: a scalar option is sent
// as OP_SpawnAppearance (eqt::gameOptionAppearanceType), ServerFilters as the
// whole OP_SetServerFilter payload in `filters`. The game thread publishes
// the applied scalar back on GameEventType::GameOptionChanged.
struct GameOptionIntent {
    eqt::GameOption option;
    int32_t value = 0;
    eqt::ServerFilterPayload filters{};
};

// --- Tradeskill ---

struct TradeskillCombineIntent {
    int16_t containerSlot;  // SLOT_TRADESKILL_EXPERIMENT_COMBINE for world containers
};

struct TradeskillCloseIntent {
    uint32_t dropId;  // World object drop ID (0 for inventory containers)
    // B899: the general slot of a CARRIED container whose window is closing
    // (Done, the close box, ESC). SLOT_INVALID (-1) for a world container.
    // A carried bag has no server session, so this closes it game-side
    // (CommandsComponent::CloseCarriedContainer) and sends nothing.
    int16_t inventorySlot = -1;
};

// T419 (board B335): a click on a COMBW_RecipeList row. Reaches
// MerchantComponent::SendRecipeDetails(), the OP_RecipeDetails sender that
// /recipedetails already used -- the row is a second producer for it, not a
// second packet.
struct RecipeDetailsRequestIntent {
    uint32_t recipeId;
};

// T421 (board B365): the tradeskill window's own COMBW_SearchButton (and the
// Enter key in COMBW_SearchTextEdit). Reaches
// MerchantComponent::SendRecipesSearch(), the OP_RecipesSearch sender
// /recipesearch already used -- a second producer for it, not a second
// packet. The bounds are the two trivial editboxes; 0 means "no bound",
// which is what /recipesearch sends when the player omits them.
struct RecipeSearchIntent {
    std::string query;
    uint32_t minTrivial = 0;
    uint32_t maxTrivial = 0;
};

// T421 (board B365): COMBW_AddFavoriteButton / COMBW_RemoveFavoriteButton /
// COMBW_ShowFavoritesButton. All three reach
// MerchantComponent::SendRecipesFavorite(), which takes the WHOLE favourites
// list -- eqemu's OP_RecipesFavorite is a set, not a delta -- so the add and
// remove buttons send the edited list and the show button sends the list
// unchanged to re-fetch it.
enum class RecipeFavoriteOp : uint8_t { Add = 0, Remove, Show };

struct RecipeFavoriteIntent {
    RecipeFavoriteOp op = RecipeFavoriteOp::Show;
    uint32_t recipeId = 0;  // the selected row, ignored by Show
};

// T421 (board B298, item half): a click on an inventory slot that holds an
// item. The game thread answers with an ItemDisplayShown event carrying the
// composed description -- the renderer never sees an ItemInstance.
struct ItemDisplayRequestIntent {
    int16_t slot;
};

// T425 (board B298, spell half): a SHIFT+click on an occupied spell gem or on
// a spellbook entry. The game thread answers with the SAME ItemDisplayShown
// event -- the oracle has ONE display window (there is no EQUI_SpellDisplay
// in uifiles/default at all) and it shows a spell or an item, so the renderer
// gets composed lines either way and never sees a SpellData.
struct SpellDisplayRequestIntent {
    uint32_t spellId;
};

// --- Trainer ---

struct TrainSkillIntent {
    uint8_t skillId;
};

struct CloseTrainerIntent {};

// --- Hotbar ---

struct HotbarActivateIntent {
    int index = -1;      // Hotbar slot index (for cooldown feedback)
    // T156: was `uint8_t buttonType`, always value-initialised to 0 by the
    // only producer, so the Skill arm was unreachable. Now the model's own
    // enum, filled from the cached slot state.
    HotbarSlotType type = HotbarSlotType::Empty;
    uint32_t id = 0;     // spell_id, item_id, or skill_id
    std::string emoteText;  // For emote buttons
};

// --- Inventory ---

struct MoveItemIntent {
    int16_t fromSlot;
    int16_t toSlot;
    uint32_t quantity;  // 0 = move entire stack
};

struct DeleteItemIntent {
    int16_t slot;
};

struct EquipmentChangedIntent {
    int16_t slotId;
};

// B442 (#151): the click-to-cursor gesture. ONE intent for every item
// window's slot click AND for /pickup, /putdown and /coin, so a --script
// tour and a click are one path. The render thread only says WHICH slot was
// clicked and how; the game thread resolves it against InventoryManager's
// model (eqt::inventory::ApplyCursorGesture, cursor_gesture.h), which pushes
// the MoveItemIntent(s). See include/client/inventory/cursor_gesture.h.
enum class CursorGestureOp : uint8_t {
    Click = 0,   // left click: pick up, or place / swap / merge onto a stack
    PickUp,      // /pickup: pick up only -- refused while the cursor holds an item
    PutDown,     // /putdown: place only -- refused while the cursor is empty
    CoinClick,   // purse row / /coin: coin onto the cursor, or held coin back
    // B442b (#151 rows 19-21): RIGHT click on a carried bag -- the body /bag
    // runs (CommandsComponent::RequestToggleBag). `slot` = the general slot.
    ToggleBag,
    // B442b (#151 rows 31-35): the two confirmations. A Request asks the game
    // thread for the prompt (eqstr 9057 / 2043 with the held item's name,
    // published back as ConfirmationRequested); a Confirm is the dialog's Yes.
    RequestDrop,     // a motionless world click with an item held (row 31)
    ConfirmDrop,     // Yes -> OP_GroundSpawn, empty body (row 33)
    RequestDestroy,  // IW_Destroy with an item held (row 34)
    ConfirmDestroy,  // Yes -> OP_MoveItem 30 -> -1 qty 0 (row 35)
};

struct CursorGestureIntent {
    CursorGestureOp op = CursorGestureOp::Click;
    int16_t slot = -1;      // Titanium (model) slot id; unused by CoinClick
    int32_t quantity = 0;   // 0 = the whole stack / the whole coin pile
    uint8_t coinType = 0;   // EQT::COINTYPE_* (0 cp .. 3 pp), CoinClick only
    // B442b (#151 rows 43-44): which coin store a CoinClick is on --
    // EQT::COINSLOT_INVENTORY (1, the purse) or EQT::COINSLOT_BANK (2).
    uint8_t coinSource = 1;
    bool echo = false;      // a slash command: print the outcome to chat
};

// --- Audio ---

// T328 Leg B: "play this sound id now".
//
// The one existing audio path, BridgeComponent::PlaySound(), lives on the
// bridge component -- reachable from a family component as
// ctx_.Bridge().PlaySound() (movement_state.cpp does exactly that for
// WaterIn) but NOT from InventoryManager, which holds only a GameStateBridge.
// Rather than give InventoryManager a second back-channel, both producers push
// this intent and ProcessBridgeIntents() forwards it to the same
// BridgeComponent::PlaySound(). That also makes every wired UI sound
// assertable in a unit test with no audio device: a RecordingBridge drains the
// intent queue and the sound id is right there.
//
// Sound ids come from EQT::Audio::SoundBank / UISoundId (sound_assets.h),
// which T328 Leg A measured against the official client.
struct PlaySoundIntent {
    uint32_t soundId;
};

// --- Tracking (B895, GitLab #211) ---

// The Tracking Window's controls. ONE intent for the four gestures, because
// all four land on eqt::tracking::TrackingController -- the same bodies the
// /track, /trackfilter, /tracksort and /trackplayers commands reach, so a
// click and a command cannot do different things. `value` is the filter
// index (TRW_Filter*Button order R Y W B L G), the sort or the players
// setting, in eqt::tracking's numbering.
enum class TrackingIntentOp : uint8_t { Track = 0, ToggleFilter, SetSort, SetPlayers };

struct TrackingIntent {
    TrackingIntentOp op = TrackingIntentOp::Track;
    uint16_t spawnId = 0;  // Track: the selected TRW_TrackingList row
    uint8_t value = 0;
};

// B897 (GitLab #213): a Friends-window button. The handler runs the SAME
// eqt::state::ApplySocialEdit() / RunFriendsWho() bodies /friends, /ignore and
// /friendswho run (include/client/state/social_lists.h), so the window and
// the slash commands print the same oracle strings. `list` is
// eqt::state::SocialList (0 friends, 1 ignored). Who with a `name` (a selected
// friend) is a who-all for that name; Who with none queries the stored list
// (review/UIGAP-ORACLE/213/friends.tsv rows 13-14).
enum class SocialListOp : uint8_t { Add, Remove, Who };
struct SocialListIntent {
    uint8_t list;
    SocialListOp op;
    std::string name;
};

// B896 (GitLab #212): the trade window's and the NPC give window's buttons
// and coin rows. Accept is TRDW_Trade_Button / GVW_Give_Button and runs
// TradeManager::clickAccept() -- the body /accept runs during an active
// trade (OP_TradeAcceptClick, MEASURED review/UIGAP-ORACLE/212/trade.tsv rows
// 4 and 10); Cancel is the Cancel button and the close box and runs
// TradeManager::cancelTrade(), the body /canceltrade runs (OP_CancelTrade,
// row 8). CoinFromCursor is a click on an own coin row with coin on the
// cursor: one OP_MoveCoin cursor -> trade (row 2, to_slot 3). Item slots are
// NOT here -- they are B442's CursorGestureIntent with slot 3000+n.
enum class TradeWindowOp : uint8_t { Accept = 0, Cancel, CoinFromCursor };
struct TradeWindowIntent {
    TradeWindowOp op = TradeWindowOp::Accept;
};

// ============================================================================
// Variant type for all renderer intents
// ============================================================================

using RendererIntent = std::variant<
    // Movement
    PlayerPositionChanged,
    PlayerModelGeometryChanged,
    // Targeting
    TargetIntent,
    // Chat
    ChatSubmitIntent,
    // Interaction
    DoorInteractIntent,
    LootCorpseIntent,
    LootItemIntent,
    LootAllIntent,
    CloseLootIntent,
    WorldObjectInteractIntent,
    ZoningEnabledIntent,
    ReadItemIntent,
    // Vendor
    VendorToggleIntent,
    VendorBuyIntent,
    VendorSellIntent,
    // Bank
    BankerInteractIntent,
    CloseBankIntent,
    BankCurrencyMoveIntent,
    BankCurrencyConvertIntent,
    // Trainer
    TrainerToggleIntent,
    // Spells
    CastSpellIntent,
    MemorizeSpellIntent,
    ScribeSpellIntent,
    SpellbookStateIntent,
    // Buffs
    BuffCancelIntent,
    // Skills
    SkillActivateIntent,
    // Volume (D20c2)
    MusicVolumeChangeIntent,
    EffectsVolumeChangeIntent,
    // Hotbar (D20b2)
    HotbarActivateIntent,
    // Tradeskill
    TradeskillCombineIntent,
    TradeskillCloseIntent,
    RecipeDetailsRequestIntent,
    RecipeSearchIntent,
    RecipeFavoriteIntent,
    ItemDisplayRequestIntent,
    SpellDisplayRequestIntent,
    // Trainer
    TrainSkillIntent,
    CloseTrainerIntent,
    // Inventory
    MoveItemIntent,
    DeleteItemIntent,
    EquipmentChangedIntent,
    CursorGestureIntent,   // B442
    // Audio (T328)
    PlaySoundIntent,
    // Options window (B430)
    GameOptionIntent,
    // Tracking (B895)
    TrackingIntent,
    // Social (B897)
    SocialListIntent,
    // Trade (B896)
    TradeWindowIntent
>;

} // namespace events
} // namespace eqt
