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
// ProcessBridgeIntents moves WHOLE (580 lines, Batch G decides its
// decomposition, not this unit).
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
// `owner_` is the TRANSITION handle for the zone environment that stays with
// EverQuest for the zone family (m_zone_map, m_zone_bsp_tree*, the fog and
// sky/zone-type fields, m_eq_client_path, m_zone_audio_manager), for the
// renderer-lifetime state Application wires (m_graphics_initialized, the
// constrained-rendering preset/config, m_loading_status_ptr,
// m_zone_load_requested, m_hotbar_model) and for the EverQuest primitives the
// moved bodies still call (PosX/PosY/PosZ, MySpawnId, QueuePacket,
// AddChatSystemMessage, ...). EverQuest declares
// `friend class eqt::bridge::BridgeComponent` for exactly that; K-final
// removes both.
//
// The reference-returning loot getters below are the mirror image: the
// merchant, entities and zone components still touch loot-window state that
// now lives here. Transition scaffolding too.

#include "client/client_context.h"
#include "client/pet_constants.h"
#include "client/hotbar/hotbar_slot_type.h"
#include "client/zone_load_snapshot.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class EverQuest;

namespace Json { class Value; }

namespace EQT {
namespace Graphics {
struct BspTree;
class IrrlichtRenderer;
}
}

namespace eqt {
namespace events {
struct PlayerPositionChanged;
}
namespace bridge {

class GameStateBridge;

class BridgeComponent {
public:
	BridgeComponent(ClientContext& ctx, EverQuest& owner) : ctx_(ctx), owner_(owner) {}

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
	                  EQT::Graphics::IrrlichtRenderer* renderer,
	                  eqt::bridge::GameStateBridge* bridge);
	void ShutdownGraphics();
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

	// ---- config paths and persistence ----
	void SetRegionMapsPath(const std::string& path) { m_region_maps_path = path; }
	void SetConfigPath(const std::string& path) { m_config_path = path; }
	void SaveHotbarConfig();  // Save hotbar assignments to config file
	void LoadHotbarConfig();  // Load hotbar assignments from config file
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

#ifdef WITH_AUDIO
	// Audio lifecycle. ShutdownAudio() is also called from EverQuest's
	// destructor (eq.cpp), so it is public.
	void InitializeAudio();
	void ShutdownAudio();
	// Day/night state for audio (calculated from game time); driven by the
	// entities family's OP_TimeOfDay handler.
	void UpdateDayNightState();
	// Sound effect helper (client-triggered). K11 deleted PlaySoundAt,
	// PlayCombatSound, PlaySpellSound and PlayUISound -- zero callers
	// tree-wide (section Mechanism 5); boarded.
	void PlaySound(uint32_t soundId);

	// Audio configuration (applied during InitializeAudio; Application sets
	// these from the CLI/config before InitGraphics).
	void SetAudioEnabled(bool enabled) { m_audio_config_enabled = enabled; }
	void SetMasterVolume(float volume) { m_audio_config_master_volume = volume; }
	void SetMusicVolume(float volume) { m_audio_config_music_volume = volume; }
	void SetEffectsVolume(float volume) { m_audio_config_effects_volume = volume; }
	void SetSoundFont(const std::string& path) { m_audio_config_soundfont = path; }
	void SetVendorMusic(const std::string& filename) { m_audio_config_vendor_music = filename; }
	void SetUse3DAudio(bool enabled) { m_audio_config_use_3d_audio = enabled; }
#endif

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

	std::string m_region_maps_path = "data/region_maps";
	std::string m_config_path;  // Path to per-character config file

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
	std::string m_audio_config_vendor_music = "gl.xmi";
	// K15 (B037): re-wired. K11 deleted SetUse3DAudio() as callerless and the
	// flag was then settable only by editing this header -- AND it had zero
	// READERS too (the K11 note claiming InitializeAudio() read it was wrong,
	// re-measured at 7c125b28). It now has both: Application sets it from
	// "audio.use_3d", and InitializeAudio() pushes it into
	// AudioManager::setUse3DAudio(), which gates the spatial branch of
	// playSoundInternal().
	bool m_audio_config_use_3d_audio = true;
#endif

	ClientContext& ctx_;
	EverQuest& owner_;
};

}  // namespace bridge
}  // namespace eqt
