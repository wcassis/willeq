#pragma once

// Batch K (refactor/K_component_extraction.md), unit K11.
//
// The bridge family, lifted out of class EverQuest: the graphics lifecycle
// (InitGraphics/ShutdownGraphics), the per-frame game-thread ticks, the
// renderer intent and event pumps (ProcessBridgeIntents/ProcessBridgeEvents),
// the zone-load snapshot, the spawn/pet/movement bridge marshalling, the
// loot window, per-client config + hotbar + inspect-message persistence, the
// audio lifecycle and the T269 bank-coin projection helpers.
//
// Every body in src/client/bridge/bridge_component*.cpp is the body that used
// to live in src/client/eq_bridge.cpp / eq_bridge_intents.cpp /
// eq_bridge_ui_audio.cpp, moved verbatim; the only edits inside a body are
// the substitutions the batch doc's section Mechanism allows (m_x -> ctx_.x /
// owner_.m_x, Foo() -> owner_.Foo() or owner_.<Family>().Foo(), and an
// EverQuest-scoped name -> EverQuest::Name).
//
// ProcessBridgeIntents moved WHOLE in K11 (580 lines); G02
// (refactor/G02_process_bridge_intents.md) then decomposed it into the 36
// private onX handlers below, leaving the pump as pure dispatch.
//
// FIVE SANCTIONED FORWARDERS. setBridge(), InitGraphics(),
// ShutdownGraphics(), GameTick() and PostRenderTick() are EverQuest's
// LIFECYCLE entry points: Application starts, ticks and stops the game
// through them, and must not have to know a component exists to do it. Their
// BODIES moved here; EverQuest keeps a one-line forwarder for each (defined
// in src/client/eq.cpp). They are the only forwarders in Batch K -- section
// Mechanism 5's no-forwarder rule holds for everything else, including
// ProcessBridgeIntents/ProcessBridgeEvents, CreateZoneLoadSnapshot,
// SetZoneBspTree, LoadHotbarConfig and LoadInspectMessage, whose callers go
// through Bridge().
//
// Folding this component into GameStateBridge/IrrlichtBridge is a DESIGN
// step and is K13-or-later; this unit is the mechanical extraction that
// makes it delegable.
//
// T341 (B038 clusters 1-3): the `friend class eqt::bridge::BridgeComponent`
// line is GONE. The renderer-lifetime state Application wires
// (m_graphics_initialized, the constrained-rendering preset/config,
// m_loading_status_ptr) and the whole loading-phase state machine are now
// MEMBERS of this component; the zone environment went to
// eqt::zone::ZoneComponent and ClientContext at K13; TickNetwork became
// ctx_.lifecycle.TickNetwork(). Both axes of the K13 (ii) friend test are
// empty.
//
// T341 removed the `friend` line; T366 (B038 CLOSED) removed the `owner_`
// handle that outlived it. Its only use was two intent arms passing it, by
// reference, to EQ::castRefusalMessage(EverQuest&, CastResult); that free
// function now takes the ClientContext, so the arms pass ctx_.
// m_zone_load_requested stays on EverQuest; Application consumes it
// through ConsumeZoneLoadRequest() and no component reaches it.
//
// The reference-returning loot getters below are the mirror image: the
// merchant, entities and zone components still touch loot-window state that
// now lives here. Transition scaffolding too.

#include "client/client_context.h"
// (B1018 removed the "common/logging.h" include: its only user was
// SetVendorMusic()'s B344 one-time WARN, which moved to the parse site in
// application_config.cpp.)
#include "client/eq_enums.h"   // T341: LoadingPhase, this component's state now
#include "client/pet_constants.h"
#include "client/hotbar/hotbar_slot_type.h"
#include "client/hotbar/hotbar_persistence.h"
#include "client/zone_load_snapshot.h"
#include "client/state/social_lists_io.h"  // B897: SocialListsSync
// G02: the intent handlers below take one alternative of RendererIntent each,
// by const reference. Dependency-free header (cstdint/string/variant + the
// hotbar slot-type enum), so this costs nothing the component did not pay.
#include "client/events/renderer_intents.h"
#ifdef EQT_HAS_GRAPHICS
// T341 (B038 cluster 1): the constrained-rendering preset/config members
// below are held by value, so this component needs the complete types.
#include "client/graphics/constrained_renderer_config.h"
#endif

#include <cstdint>
#include <optional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class EverQuest;

namespace Json { class Value; }

namespace EQT {
namespace Graphics {
struct BspTree;
class IZoneLoadSurface;  // K16 (B006): the two-method zone-load surface
struct LoadingStatus;    // T341: Application owns it; this component signals it
}
}

namespace eqt {
namespace bridge {

class GameStateBridge;

class BridgeComponent {
public:
	// T366 (B038): the EverQuest& transition handle is gone -- this component
	// names nothing on EverQuest any more.
	explicit BridgeComponent(ClientContext& ctx) : ctx_(ctx) {}

	// Loot window item, game-side (D18a/T268).
	struct GameLootItem {
		uint32_t itemId = 0;
		std::string name;
	};

	// ---- the five sanctioned lifecycle forwarders' real bodies ----
	// T46: setBridge assigns m_bridge and propagates the same pointer
	// (including null) to the four managers' own bridge pointers via
	// wireBridgeToSubsystems(), so the headless path wires them too.
	void setBridge(eqt::bridge::GameStateBridge* bridge);
	// D21a: split tick -- game thread vs render thread.
	void GameTick(float deltaTime);        // spell/buff updates + intent processing
	void PostRenderTick(float deltaTime);  // audio sync
#ifdef EQT_HAS_GRAPHICS
	bool InitGraphics(int width, int height,
	                  EQT::Graphics::IZoneLoadSurface* renderer,
	                  eqt::bridge::GameStateBridge* bridge);
	void ShutdownGraphics();
#endif

	// ------------------------------------------------------------------
	// T341 (B038 cluster 2): the LOADING-PHASE STATE MACHINE.
	//
	// Was EverQuest's (m_loading_phase + m_loading_status_text +
	// SetLoadingPhase/GetLoadingProgress/GetLoadingStatusText/IsGameStateReady/
	// IsGraphicsReady), reached by four families through the transition handle
	// (18 owner_. uses). K13 declined to fold it into eqt::PlayerFacade
	// because it is not player state; it is the GRAPHICS LIFECYCLE, which is
	// this component's whole job -- SetLoadingPhase signals
	// m_loading_status_ptr and publishes ZoneLoading, both of which now live
	// here.
	//
	// EverQuest keeps a one-line forwarder for FIVE of the six: Application
	// drives the loading screen through GetLoadingPhase / GetLoadingProgress /
	// IsGraphicsReady / SetLoadingPhase, and ~80 test sites read them on the
	// EverQuest receiver. GetLoadingStatusText() got no forwarder -- zero
	// callers on that receiver (Mechanism 5). See the T341 Review.
	LoadingPhase GetLoadingPhase() const { return m_loading_phase; }
	void SetLoadingPhase(LoadingPhase phase, const char* statusText = nullptr);
	float GetLoadingProgress() const;
	const char* GetLoadingStatusText() const;
	// B1099 (#419): the line the loading screen shows NOW -- the phase's
	// table text, or the override SetLoadingPhase() was handed (the
	// "Could not connect to ..." line of a pre-zone connect give-up).
	const char* CurrentLoadingStatusText() const { return m_loading_status_text; }
	bool IsGameStateReady() const { return static_cast<int>(m_loading_phase) >= static_cast<int>(LoadingPhase::ZONE_AWAITING_CONFIRM); }
	bool IsGraphicsReady() const { return m_loading_phase == LoadingPhase::COMPLETE; }

#ifdef EQT_HAS_GRAPHICS
	// ------------------------------------------------------------------
	// T341 (B038 cluster 1): RENDERER-LIFETIME STATE. K11 recorded the
	// standing that these four are "renderer-lifetime state Application
	// wires"; they are read and written by this component and by nothing
	// else, so they are its members now. EverQuest keeps SetLoadingStatus()
	// and SetConstrainedConfig() as forwarders (Application calls both) and
	// OnGameStateComplete() reads IsGraphicsInitialized().
	bool IsGraphicsInitialized() const { return m_graphics_initialized; }
	// D20e2: Application owns the loading status; this signals it.
	void SetLoadingStatus(EQT::Graphics::LoadingStatus* status) { m_loading_status_ptr = status; }
	void SetConstrainedConfig(const EQT::Graphics::ConstrainedRendererConfig& config);
#endif

	// ---- bridge pumps ----
	void ProcessBridgeIntents();  // Drain and process intents from the renderer (D14+)
	void ProcessBridgeEvents();   // Drain and apply events to the renderer (D14+)

#ifdef EQT_HAS_GRAPHICS
	eqt::ZoneLoadSnapshot CreateZoneLoadSnapshot() const;  // D20c3: snapshot for loading thread
	// P17/T260: installs the renderer's tree from a completed zone load.
	// builtForZone is the zone that load was FOR (Application passes the
	// consumed snapshot's zoneName). T260: a tree built for any zone other
	// than the current one is REJECTED, not installed -- that is the
	// graphicsCompleteReady finalization of the previous load landing after
	// a fast re-zone's NewZone (P15's "SIZE_MAX with zone=<source>" FATAL was
	// the previous zone's tree paired with the new zone's position). The
	// tree LoadZoneSources() built for the current zone stays in place.
	// P17 tagged with the zone name at call time, which is exactly
	// the value that is wrong in that race.
	void SetZoneBspTree(std::shared_ptr<EQT::Graphics::BspTree> bsp, const std::string& builtForZone);
#endif

	// ---- spawn / pet graphics marshalling (driven by the entities, group
	// and zone components) ----
	void OnSpawnAddedGraphics(const Entity& entity);  // Only creates entity if loading is complete
	void OnSpawnRemovedGraphics(uint16_t spawn_id);
	void OnSpawnMovedGraphics(uint16_t spawn_id, float x, float y, float z, float heading,
	                          float dx, float dy, float dz, int32_t animation);
	void OnPetCreated(const Entity& pet);
	void OnPetRemoved();
	void OnPetButtonStateChanged(EQT::PetButton button, bool state);

	void UpdateInventoryStats();  // Update inventory window with current stats (base + equipment)

	// B896 (#212): OUR trade request was acknowledged (player or NPC). The
	// official client then drops the item it was opened with into the first
	// trade slot by itself -- OP_MoveItem 30 -> 3000 (MEASURED,
	// review/UIGAP-ORACLE/212/trade.tsv rows 1 and 9). Runs the same
	// CursorGesture place a slot click does; a no-op with nothing held.
	// Called from ZoneComponent::OnTradeRequestAck (bridge_component_trade.cpp).
	void OfferHeldItemOnTradeOpen();

	// UIGAP-residue (B1001 #321, #212 rows 12-13): a world click on `target`
	// with something held that the official client REFUSES client-side, by
	// the eqstr id it prints (0 = no refusal, open the trade / give window).
	// The click then only targets (OP_TargetMouse); no OP_TradeRequest.
	uint32_t HeldItemTargetRefusal(const Entity& target) const;

	// ---- config paths and persistence ----
	void SetRegionMapsPath(const std::string& path) { m_region_maps_path = path; }
	void SetConfigPath(const std::string& path) { m_config_path = path; }
	void SaveHotbarConfig();  // Save to per-character cache file
	eqt::HotbarLoadResult LoadHotbarConfig();  // Load per-character file; import legacy config once
	void SaveInspectMessage();  // T171: persist the inspect message
	void LoadInspectMessage();  // T171: restore it at startup

	// T171: the inspect message this client sends when answering an inspect.
	// Truncated to EQT::INSPECT_MESSAGE_MAX_LEN on set -- the server stores
	// only 256 bytes and a longer value would be stored unterminated.
	//
	// Setting it does NOT push anything to the server: there is no opcode for
	// that on Titanium (OP_InspectMessageUpdate is SoF+ and is not even mapped
	// in patch_Titanium.conf). It reaches the server the next time somebody
	// inspects us, which is also the moment it replaces whatever the server
	// held -- the same last-writer-wins the real client has.
	const std::string& GetInspectMessage() const { return m_inspect_message; }
	void SetInspectMessage(const std::string& message);

	// ---- loot window (game-side tracking, D18a) ----
	// The single senders (T268): OP_LootRequest, OP_LootItem,
	// OP_EndLootRequest. corpseId 0 means "the open window".
	void RequestLootCorpse(uint16_t corpseId);
	// autoLoot=true: item goes straight to inventory (eqemu
	// AutoPutLootInInventory); false: to the cursor, as the UI gesture does.
	// Returns false (nothing sent) when no window is open.
	bool LootItemFromCorpse(uint16_t corpseId, int16_t slot, bool autoLoot = false);
	void CloseLootWindow(uint16_t corpseId);
	const std::map<int16_t, GameLootItem>& GetLootItems() const { return m_loot_items; }
	// Case-insensitive match against the open window's item names: an exact
	// match wins, else the lowest slot whose name contains `name`. Returns
	// -1 when nothing matches (or no window is open).
	int16_t FindLootSlotByName(const std::string& name) const;

	// K11 transition scaffolding: loot-window state whose remaining
	// readers/writers live in the merchant, entities and zone components.
	// Reference-returning so a retargeted call site is a name substitution
	// and nothing else. K-final folds them in.
	std::map<int16_t, GameLootItem>& LootItems() { return m_loot_items; }
	std::vector<int16_t>& PendingLootSlots() { return m_pending_loot_slots; }
	uint16_t& LootCompleteCorpseId() { return m_loot_complete_corpse_id; }

	// T230: fire an inventory item's click effect (OP_CastSpell, slot 10)
	// from an INVENTORY slot. The one sender for both the mouse hotbar path
	// (HotbarActivateIntent, Item) and the keyboard hotbar path
	// (EqActionHandler::useItem). Every refusal is reported via
	// AddChatSystemMessage; returns true only when the packet was queued.
	// Pushes HotbarCooldownStarted for each hotbar slot holding the item.
	bool UseInventoryItem(int16_t slot);

	// T239: fire a hotbar slot whose content is a COMMAND rather than a
	// spell/skill/item: Emote and Macro carry command text (run through
	// ProcessChatInput, the live chat window's unified dispatch entry);
	// Combat toggles auto-attack (CombatManager::ToggleAutoAttack). The one
	// body for both the mouse hotbar path (HotbarActivateIntent) and the
	// keyboard hotbar path (EqActionHandler::activateHotbarCommandSlot <-
	// ActionDispatcher <- InputActionBridge). Refusals are reported via
	// AddChatSystemMessage; returns true only when something was executed.
	// Any other slot type is not this function's business and returns false.
	bool ActivateHotbarCommandSlot(eqt::HotbarSlotType type, const std::string& text);

	// B1025-H3 (GitLab #345): /hotbutton -- the game thread's one hot-button
	// producer. Publishes HotbarAssignPending (a social button, an Emote
	// carrying the command text) on the bridge; the render thread lands it on
	// the cursor (IrrlichtBridge's arm) and draws it. Returns false when there
	// is no bridge (a headless run): the caller refuses with eqstr 13283.
	bool PublishHotbarAssignPending(eqt::HotbarSlotType type, uint32_t id,
	                               const std::string& text, uint32_t iconId);

#ifdef WITH_AUDIO
	// Audio lifecycle. ShutdownAudio() is also called from EverQuest's
	// destructor (eq.cpp), so it is public.
	void InitializeAudio();
	void ShutdownAudio();
	// Day/night state for audio (calculated from game time); driven by the
	// entities family's OP_TimeOfDay handler.
	void UpdateDayNightState();
	// T334 (board B045): the per-frame audio listener sync, called from
	// PostRenderTick(). Lives in bridge_component_ui_audio.cpp with the rest
	// of the audio family, not beside the tick, because the tick file owns no
	// other audio logic.
	void UpdateAudioListener(float deltaTime);
	// Sound effect helper (client-triggered). K11 deleted PlaySoundAt,
	// PlayCombatSound, PlaySpellSound and PlayUISound -- zero callers
	// tree-wide (section Mechanism 5); boarded.
	void PlaySound(uint32_t soundId);

	// T439 (board B432): the ONE publisher of GameEventType::VolumesChanged.
	// Every site that moves a volume calls it right after the setter -- the
	// config apply in InitializeAudio(), the two volume intents, and
	// /musicvolume + /effectsvolume (through ctx_.Bridge()). It reads the
	// audio manager back rather than taking the value it was just handed, so
	// the event states what the audio manager ACTUALLY holds after its own
	// clamping, which is the whole point of the board item.
	void PublishVolumesChanged();

	// B430: the ONE publisher of GameEventType::GameOptionChanged, and the one
	// body that applies a server-owned Options setting (bridge_component_options.cpp).
	// ApplyGameOption() sends it (OP_SpawnAppearance, or OP_SetServerFilter for
	// ServerFilters) and publishes the applied value; PublishProfileGameOptions()
	// publishes the player-profile values read at zone-in.
	void ApplyGameOption(const eqt::events::GameOptionIntent& i);
	void PublishGameOption(eqt::GameOption option, int32_t value);
	void PublishProfileGameOptions(uint8_t anon, uint32_t autosplit, uint8_t consentGroup,
	                               uint8_t consentRaid, uint8_t consentGuild);

	// Audio configuration (applied during InitializeAudio; Application sets
	// these from the CLI/config before InitGraphics).
	void SetAudioEnabled(bool enabled) { m_audio_config_enabled = enabled; }
	void SetMasterVolume(float volume) { m_audio_config_master_volume = volume; }
	void SetMusicVolume(float volume) { m_audio_config_music_volume = volume; }
	void SetEffectsVolume(float volume) { m_audio_config_effects_volume = volume; }
	void SetSoundFont(const std::string& path) { m_audio_config_soundfont = path; }
	// B344: the vendor-music config reaches no audio decision, so the member
	// and the store went; the key is user-facing, so the config keeps
	// parsing it. B1018 (GitLab #339): the "parsed but ignored" WARN does NOT
	// live here. Application::initEverQuestClient() calls this setter
	// UNCONDITIONALLY with config.audioVendorMusic (whose default is
	// "gl.xmi"), so a warn in this body fired on every WITH_AUDIO run even
	// when no config named the key. The warn belongs at the PARSE site,
	// application_config.cpp's `audio_config.isMember("vendor_music")` arm,
	// which runs only when the key is actually present -- pinned by
	// test_vendor_music_config_warn (0 warns absent, exactly 1 present).
	void SetVendorMusic(const std::string& /*filename*/) {}
	void SetUse3DAudio(bool enabled) { m_audio_config_use_3d_audio = enabled; }
#endif

	// B897 (GitLab #213): the friends/ignore persistence tick, once per
	// GameTick. Loads the character's `<Char>_<server>.ini` on a new identity,
	// saves on a changed revision, and on either publishes the ONE
	// GameEventType::SocialListsChanged (state::SocialListsSync decides).
	void SyncSocialLists();

private:
	// T46: assigns m_bridge and propagates the same pointer (including
	// null) to the four managers' own bridge pointers
	// (InventoryManager/SpellManager/SkillManager/HotbarModel), each of
	// which is a separate member from m_bridge. Called from both
	// setBridge() (the headless path) and InitGraphics() (the graphics
	// path), so the managers get wired whichever path sets the bridge.
	// Plain pointer assignment underneath, so calling it twice with the
	// same pointer (graphics mode: InitGraphics() after Application's own
	// setBridge()-equivalent construction) is a harmless no-op.
	void wireBridgeToSubsystems(eqt::bridge::GameStateBridge* bridge);

	// S2: takes the movement intent directly (name predates this being a
	// plain bridge-intent handler). Reached only from ProcessBridgeIntents.
	void OnGraphicsMovement(const eqt::events::PlayerPositionChanged& update);

	// T311/T332: THE ONE deferred wire->render conversion of our OWN stored z,
	// called from both places that can discover the conversion is still owed --
	// onPlayerModelGeometryChanged (the mesh offset just became measurable) and
	// OnGraphicsMovement (the renderer echoed the seed back at us). No-op, and
	// leaves the flag set, unless a mesh offset is known AND the conversion is
	// still pending; returns whether it converted. Defined in
	// bridge_component_intents.cpp next to its first caller.
	bool ConvertSelfZToRenderFrame(float collisionZOffset);

	// ---- G02: the renderer intent handlers ----
	// One per RendererIntent alternative, listed in the variant's own order.
	// ProcessBridgeIntents is a std::visit overload set that does nothing but
	// call these, one line each; each body is the is_same_v arm it replaced,
	// verbatim. ONE RULE, no exceptions -- onPlayerPositionChanged is a
	// one-line forward to OnGraphicsMovement above rather than a hole in the
	// table. Reached only from ProcessBridgeIntents.
	// Pinned by tests/test_bridge_intent_table.cpp (arm <-> declaration <->
	// definition, all three directions) and by the fact that the overload set
	// has no generic fallback: a new alternative without a handler here is a
	// COMPILE ERROR.
	void onPlayerPositionChanged(const eqt::events::PlayerPositionChanged& i);
	void onFallDamageIntent(const eqt::events::FallDamageIntent& i);
	void onPlayerModelGeometryChanged(const eqt::events::PlayerModelGeometryChanged& i);
	void onTargetIntent(const eqt::events::TargetIntent& i);
	void onChatSubmitIntent(const eqt::events::ChatSubmitIntent& i);
	void onDoorInteractIntent(const eqt::events::DoorInteractIntent& i);
	void onLootCorpseIntent(const eqt::events::LootCorpseIntent& i);
	void onLootItemIntent(const eqt::events::LootItemIntent& i);
	void onLootAllIntent(const eqt::events::LootAllIntent& i);
	void onCloseLootIntent(const eqt::events::CloseLootIntent& i);
	void onWorldObjectInteractIntent(const eqt::events::WorldObjectInteractIntent& i);
	void onZoningEnabledIntent(const eqt::events::ZoningEnabledIntent& i);
	void onReadItemIntent(const eqt::events::ReadItemIntent& i);
	void onVendorToggleIntent(const eqt::events::VendorToggleIntent& i);
	void onVendorBuyIntent(const eqt::events::VendorBuyIntent& i);
	void onVendorSellIntent(const eqt::events::VendorSellIntent& i);
	void onBankerInteractIntent(const eqt::events::BankerInteractIntent& i);
	void onCloseBankIntent(const eqt::events::CloseBankIntent& i);
	void onBankCurrencyMoveIntent(const eqt::events::BankCurrencyMoveIntent& i);
	void onBankCurrencyConvertIntent(const eqt::events::BankCurrencyConvertIntent& i);
	void onBankMakeChangeIntent(const eqt::events::BankMakeChangeIntent& i);
	void onTrainerToggleIntent(const eqt::events::TrainerToggleIntent& i);
	void onCastSpellIntent(const eqt::events::CastSpellIntent& i);
	void onMemorizeSpellIntent(const eqt::events::MemorizeSpellIntent& i);
	void onScribeSpellIntent(const eqt::events::ScribeSpellIntent& i);
	void onScribeSpellFromScrollIntent(const eqt::events::ScribeSpellFromScrollIntent& i);
	void onSwapSpellbookSlotsIntent(const eqt::events::SwapSpellbookSlotsIntent& i);
	void onSpellbookStateIntent(const eqt::events::SpellbookStateIntent& i);
	void onBuffCancelIntent(const eqt::events::BuffCancelIntent& i);
	void onSkillActivateIntent(const eqt::events::SkillActivateIntent& i);
	void onMusicVolumeChangeIntent(const eqt::events::MusicVolumeChangeIntent& i);
	void onEffectsVolumeChangeIntent(const eqt::events::EffectsVolumeChangeIntent& i);
	void onHotbarActivateIntent(const eqt::events::HotbarActivateIntent& i);
	// B1025-H2: the hotbar window's page buttons (HotbarPageChangedIntent).
	void onHotbarPageChangedIntent(const eqt::events::HotbarPageChangedIntent& i);
	// B1025-H3 (GitLab #345): a placed hot button (a render-thread hold, or the
	// /hotbutton command's) assigns a slot -- the first production caller of
	// HotbarModel::assignSlot().
	void onHotbarAssignIntent(const eqt::events::HotbarAssignIntent& i);
	// B1025-H3 (GitLab #345): the pickup half -- a hold on an occupied slot
	// cleared its source while the cursor carried the button off; the first
	// production caller of HotbarModel::clearSlot().
	void onHotbarClearIntent(const eqt::events::HotbarClearIntent& i);
	void onTradeskillCombineIntent(const eqt::events::TradeskillCombineIntent& i);
	void onTradeskillCloseIntent(const eqt::events::TradeskillCloseIntent& i);
	void onRecipeDetailsRequestIntent(const eqt::events::RecipeDetailsRequestIntent& i);
	void onRecipeSearchIntent(const eqt::events::RecipeSearchIntent& i);
	void onRecipeFavoriteIntent(const eqt::events::RecipeFavoriteIntent& i);
	void onItemDisplayRequestIntent(const eqt::events::ItemDisplayRequestIntent& i);
	void onSpellDisplayRequestIntent(const eqt::events::SpellDisplayRequestIntent& i);
	void onTrainSkillIntent(const eqt::events::TrainSkillIntent& i);
	void onCloseTrainerIntent(const eqt::events::CloseTrainerIntent& i);
	void onMoveItemIntent(const eqt::events::MoveItemIntent& i);
	void onDeleteItemIntent(const eqt::events::DeleteItemIntent& i);
	void onEquipmentChangedIntent(const eqt::events::EquipmentChangedIntent& i);
	// B442 (#151): the click-to-cursor gesture and /pickup /putdown /coin.
	// The arm is a one-line relay; the body is RunCursorGesture() in
	// bridge_component_cursor.cpp, beside the slot republisher it needs.
	void onCursorGestureIntent(const eqt::events::CursorGestureIntent& i);
	void RunCursorGesture(const eqt::events::CursorGestureIntent& i);
	void PublishInventorySlot(int16_t slot);
	// B442b (#151): the coin on the cursor (row 29), the drop / destroy
	// confirmations (rows 31-35) and the bag toggle (rows 19-21) -- all in
	// bridge_component_cursor.cpp beside RunCursorGesture().
	void PublishCursorCoin();
	void RunCursorCoinGesture(const eqt::events::CursorGestureIntent& i);
	void RunCursorConfirmation(const eqt::events::CursorGestureIntent& i);
	void onPlaySoundIntent(const eqt::events::PlaySoundIntent& i);
	void onGameOptionIntent(const eqt::events::GameOptionIntent& i);
	// B895: the Tracking Window's controls -> TrackingController.
	void onTrackingIntent(const eqt::events::TrackingIntent& i);
	// B897: the Friends window buttons.
	void onSocialListIntent(const eqt::events::SocialListIntent& i);
	// B896 (#212): the trade / give window's buttons and coin rows. The arm
	// relays to RunTradeWindowIntent() (bridge_component_trade.cpp), which
	// also holds the two trade hooks RunCursorGesture() calls: the refusal of
	// a withdrawal (TradeGestureRefusal) and the accept reset after our own
	// side changed (AfterTradeGesture).
	void onTradeWindowIntent(const eqt::events::TradeWindowIntent& i);
	void RunTradeWindowIntent(const eqt::events::TradeWindowIntent& i);
	std::string TradeGestureRefusal(const eqt::events::CursorGestureIntent& i) const;
	void AfterTradeGesture(const eqt::events::CursorGestureIntent& i);

	// T171: shared resolution of the per-client config node. See the
	// definitions in bridge_component_ui_audio.cpp for why read and write
	// are separate.
	static Json::Value* ResolveClientConfigForWrite(Json::Value& root);
	static const Json::Value* ResolveClientConfigForRead(const Json::Value& root);

	// T269 bank-coin helpers (next to the Bank* intent arms).
	// coinType is the wire scale (EQT::COINTYPE_*: 0=cp 1=sp 2=gp 3=pp).
	// BankCoinGet/BankCoinSet read and write the counter for one
	// denomination in one pool (bank=true -> the bank pool, false -> the
	// on-hand pool). T286: the counters live only in
	// m_game_state.player(), so this is a get/set pair rather than the
	// pointer-into-a-member it used to be.
	uint32_t BankCoinGet(uint8_t coinType, bool bank) const;
	void BankCoinSet(uint8_t coinType, bool bank, uint32_t value);
	static const char* BankCoinName(uint8_t coinType);
	static int64_t BankCoinCoppers(uint8_t coinType);
	// Publishes CurrencyChanged (only when the on-hand pool moved) +
	// BankCurrencyChanged. T286: no mirroring step -- the counters are
	// GameState's own.
	void PublishBankCoinProjection(bool inventoryChanged);

	// T341 (B038 cluster 2): the loading-phase store, moved verbatim from
	// include/client/eq.h with its comment.
	// Loading phase tracking (covers entire pre-gameplay process)
	LoadingPhase m_loading_phase = LoadingPhase::DISCONNECTED;
	const char* m_loading_status_text = "";

#ifdef EQT_HAS_GRAPHICS
	// T341 (B038 cluster 1): moved verbatim from include/client/eq.h's
	// EQT_HAS_GRAPHICS private block. m_zone_load_requested stayed on
	// EverQuest -- Application consumes it through ConsumeZoneLoadRequest()
	// and no component ever reached it.
	bool m_graphics_initialized = false;
	EQT::Graphics::ConstrainedRenderingPreset m_constrained_preset = EQT::Graphics::ConstrainedRenderingPreset::OrangePi;  // Constrained rendering preset (startup-only)
	std::optional<EQT::Graphics::ConstrainedRendererConfig> m_constrained_config;  // Custom constrained config (from NxNxN spec)
	// D20e2: Loading thread owned by Application; EverQuest holds pointer to signal it
	EQT::Graphics::LoadingStatus* m_loading_status_ptr = nullptr;
#endif

	std::string m_region_maps_path = "data/region_maps";
	std::string m_config_path;  // Client config path (legacy hotbar import and inspect message)

	// T170/T171: what we send as our inspect message. eqemu never tells a
	// Titanium client its own stored message, so this client keeps its own --
	// see PlayerComponent::SendInspectAnswer() and SaveInspectMessage().
	std::string m_inspect_message;

	// Loot window state (game-side tracking, D18a). T268: the ONE loot
	// window -- set by RequestLootCorpse for every open path (/loot,
	// shift+click, auto-hunt) and read by the item-packet dispatch; the
	// loot-all sequencing lives in CombatManager's LootSubState machine
	// (the m_loot_all_* loop that duplicated it is gone).
	std::vector<int16_t> m_pending_loot_slots;  // Corpse slots waiting for server confirmation
	uint16_t m_loot_complete_corpse_id = 0;  // Corpse ID for which looting was completed (ready for deletion)
	std::map<int16_t, GameLootItem> m_loot_items;  // slot -> item data (game-side)

#ifdef WITH_AUDIO
	// Day/night state for audio (calculated from game time)
	bool m_is_daytime = true;

	// Audio configuration (applied during InitializeAudio)
	bool m_audio_config_enabled = true;
	float m_audio_config_master_volume = 1.0f;
	float m_audio_config_music_volume = 0.7f;
	float m_audio_config_effects_volume = 1.0f;
	std::string m_audio_config_soundfont;
	// B344 deleted m_audio_config_vendor_music (write-only); the vendor_music
	// config key is still parsed, and the "parsed but ignored" WARN now lives
	// at that parse site (application_config.cpp) -- B1018 moved it out of
	// SetVendorMusic(), which Application calls unconditionally.
	// K15 (B037): re-wired. K11 deleted SetUse3DAudio() as callerless and the
	// flag was then settable only by editing this header -- AND it had zero
	// READERS too (the K11 note claiming InitializeAudio() read it was wrong,
	// re-measured at 7c125b28). It now has both: Application sets it from
	// "audio.use_3d", and InitializeAudio() pushes it into
	// AudioManager::setUse3DAudio(), which gates the spatial branch of
	// playSoundInternal().
	bool m_audio_config_use_3d_audio = true;
#endif

	// B897: when the friends/ignore lists were last loaded or written.
	eqt::state::SocialListsSync m_social_sync;

	ClientContext& ctx_;
};

}  // namespace bridge
}  // namespace eqt
