// bridge_component_intents.cpp
//
// The EverQuest <-> renderer bridge pumps, split out of eq_bridge.cpp by J13
// (refactor/J13_eq_bridge_split.md); originally split out of eq.cpp (F10).
// Contains ProcessBridgeIntents (the renderer -> game intent consumer, now
// pure dispatch over one named onX handler per RendererIntent member -- G02,
// refactor/G02_process_bridge_intents.md) and ProcessBridgeEvents (the
// single-stage game -> renderer event drain).
//
// G02 decomposed ProcessBridgeIntents: its 36 is_same_v arms became 36
// private handlers, bodies verbatim, and the visitor became a 36-line
// overload set. See the banner above the handlers below for the text
// contract the two coverage tests hold this file to.
//
// See refactor/F10_bridge.md for the original moved/left mapping.
#include "client/eq.h"
#include "client/display_eqstr.h"
#include "client/inspect_answer.h"
#include "client/willeq_database.h"
#include "client/pathfinder_interface.h"
#ifdef EQT_HAS_NAVMESH
#include "client/pathfinder_nav_mesh.h"
#endif
#include "client/hc_map.h"
#include "client/combat.h"
#include "client/trade_manager.h"
#include "client/world_object.h"
#include "client/spell/spell_manager.h"
#include "client/spell/cast_refusal.h"  // T244: official refusal strings
#include "client/spell/buff_manager.h"
#include "client/skill/skill_manager.h"
#include "client/audio/volume_steps.h"  // T439: the oracle's 0..20 volume step
#include "client/state/social_lists.h"  // B897: the friends/ignore command bodies
#include "client/merchant/coin_change.h"  // B441: the Change button's steps
#include "client/zone_lines.h"
#include "client/formatted_message.h"
#include "common/logging.h"
#include "common/packet_structs.h"
#include "common/name_utils.h"
#include "common/performance_metrics.h"

// P28: entity_pose_state.h, wld_loader.h, and the inventory/chat/hotbar/
// command-registry data-model headers have no renderer dependency — used
// unconditionally below (OnSpawnAddedGraphics's EntityPoseState, the
// Player-mode inventory/loot/vendor/bank/hotbar methods). Only the
// genuinely renderer-only headers (used solely inside InitGraphics/
// ShutdownGraphics below) stay gated.
#include "client/state/entity_pose_state.h"
#include "client/inventory/inventory_manager.h"
#include "client/inventory/item_instance.h"
#include "client/inventory/item_display_text.h"
#include "client/spell/spell_display_text.h"
#include "client/spell/spell_data.h"
#include "client/entities/entities_component.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/item_instance.h"
#include "client/chat/chat_message_buffer.h"
#include "client/graphics/ui/command_registry.h"
#include "client/hotbar/hotbar_model.h"
#include "common/util/json_config.h"
#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/loading_thread.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/weather_effects_controller.h"
#include "client/graphics/weather_config_loader.h"
#include "client/graphics/weather_quality_preset.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/input/hotkey_manager.h"
#include "client/graphics/detail/detail_manager.h"
#include "client/graphics/detail/detail_types.h"
#include "client/graphics/detail/seasonal_controller.h"
#include "client/graphics/environment/spell_effects_config.h"
#endif

#include <limits>

#include "client/bridge/game_state_bridge.h"
#include "client/position_frames.h"
#include "client/bridge/irrlicht_bridge.h"

#ifdef WITH_AUDIO
#include "client/audio/audio_manager.h"
#include "client/audio/zone_audio_manager.h"
#include "client/audio/sfx_manager.h"
#include "client/audio/sound_assets.h"
#include "client/audio/door_sounds.h"
#include "client/audio/water_sounds.h"
#endif

#include "common/net/posix_udp_transport.h"
#include "client/animation_constants.h"
#include <array>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <cctype>
#include <vector>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <thread>
#include <chrono>
#include <map>
#include <tuple>
#include <fstream>
#ifdef __linux__
#include <unistd.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// G02: the renderer -> game intent consumers.
//
// One handler per RendererIntent alternative, body VERBATIM from the
// is_same_v arm it replaces. ONE RULE, no exceptions: every arm is a named
// handler, so ProcessBridgeIntents below is pure dispatch and reads as a
// table. Handlers are private and reached only from that dispatcher.
//
// NOTE: tests/test_bridge_intent_table.cpp and
// tests/test_reachability_sweep.cpp open THIS FILE AS TEXT and require that
// every RendererIntent member has exactly one dispatch line of the canonical
// form   [this](const eqt::events::X& i) { onX(i); },
// in the visitor below. If you move an arm out of this file, repoint them.
// ============================================================================

void eqt::bridge::BridgeComponent::onPlayerPositionChanged(const eqt::events::PlayerPositionChanged& i) {
	// D14/S2: movement intent — sole movement channel
	OnGraphicsMovement(i);
}

// B893: the renderer's free-fall landing. Runs on the game thread (the intent
// drain), so calling the component directly is thread-safe -- the same thread
// that owns MovementComponent is the one sending. The height is already the
// peak-to-ground drop the renderer measured; SendFallDamage turns it into the
// damage the table gives (0 for a safe-height step, which it does not send).
void eqt::bridge::BridgeComponent::onFallDamageIntent(const eqt::events::FallDamageIntent& i) {
	if (!ctx_.bridge) {
		return;
	}
	ctx_.Movement().SendFallDamage(i.fallHeight);
}

// T311: the renderer's measured origin-to-feet distance for our own
// model. One store (PlayerState), no event — nothing downstream of
// the game thread consumes it; it exists so the packet edges can
// convert render frame <-> wire frame.
void eqt::bridge::BridgeComponent::onPlayerModelGeometryChanged(const eqt::events::PlayerModelGeometryChanged& i) {
	auto& player = ctx_.game_state.player();
	const float previous = player.collisionZOffset();
	player.setCollisionZOffset(i.collisionZOffset);
	// The deferred half of the self-spawn conversion. The spawn z
	// arrives before the model does, so it is stored raw; the moment
	// the mesh offset is measurable, convert it into the render frame
	// exactly once. Without this the send path would treat a wire
	// value as a render value and add the offset a second time.
	if (previous <= 0.0f) {
		ConvertSelfZToRenderFrame(i.collisionZOffset);
	}
}

// T311/T332: the conversion itself, in ONE place. It used to be inline above,
// which is why the second discoverer of a pending conversion
// (OnGraphicsMovement, when the renderer echoes the zone-in seed back) had no
// way to run it and cleared the flag instead -- B049, 0.625 per login.
bool eqt::bridge::BridgeComponent::ConvertSelfZToRenderFrame(float collisionZOffset) {
	auto& player = ctx_.game_state.player();
	if (collisionZOffset <= 0.0f || !player.zAwaitingFrameConversion()) {
		// No measured mesh yet, or nothing owed. Leave the flag ALONE: with no
		// offset there is nothing to convert through, and the send path already
		// passes an unconverted wire z out verbatim (position_frames.h).
		return false;
	}
	player.setZAwaitingFrameConversion(false);
	// SetPosition (not SetPosLocal): the renderer was handed the unconverted z
	// at zone-in and needs the corrected one handed back.
	ctx_.self.SetPosition(ctx_.self.PosX(), ctx_.self.PosY(),
	            eqt::wireZToRenderZ(ctx_.self.PosZ(), collisionZOffset, player.size()));
	// T332: and it really does have to be handed back. SetPosition fires
	// PlayerMoved on the STATE LAYER's own EventBus, which has no production
	// listener and never reaches the bridge (zone_entry.cpp says so at the
	// PlayerProfile push, and zone_component.cpp hand-rolls this same event for
	// the same reason). Without this the renderer keeps the raw wire z it was
	// seeded with and draws us 0.625*size - collisionZOffset off the ground.
	if (ctx_.bridge) {
		eqt::state::PlayerMovedData data;
		data.x = ctx_.self.PosX(); data.y = ctx_.self.PosY(); data.z = ctx_.self.PosZ();
		data.heading = ctx_.self.HeadingDeg() * 512.0f / 360.0f;
		data.dx = 0; data.dy = 0; data.dz = 0;
		data.isMoving = false;
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PlayerMoved, std::move(data)));
	}
	return true;
}

// ================================================================
// D15: Interaction intents (D20a — intent-only, callbacks removed)
// ================================================================
void eqt::bridge::BridgeComponent::onTargetIntent(const eqt::events::TargetIntent& i) {
	// Check if we have a cursor item or cursor money and clicked on a player - initiate trade
	if (ctx_.inventory_manager && ctx_.trade_manager) {
		const auto* cursorItem = ctx_.inventory_manager->getItem(eqt::inventory::CURSOR_SLOT);
		bool hasCursorMoney = ctx_.inventory_manager->hasCursorMoney();
		if (cursorItem || hasCursorMoney) {
			auto it = ctx_.entities.find(i.spawnId);
			const uint32_t refusal = it != ctx_.entities.end() ? HeldItemTargetRefusal(it->second) : 0;
			if (refusal != 0) {
				// UIGAP-residue (B1001 #321, #212 rows 12-13): refused
				// client-side -- the official line, then the click is a plain
				// target (OP_TargetMouse) and NO OP_TradeRequest goes out.
				const std::string line = eqt::EqStrLine(ctx_.strings.Db(), refusal, {});
				if (!line.empty()) {
					ctx_.out.System(line);
				}
				LOG_INFO(MOD_MAIN, "Held-item click on {} refused client-side (eqstr {})",
				         it->second.name, refusal);
			} else if (it != ctx_.entities.end() && (it->second.npc_type == 0 || it->second.npc_type == 1)) {
				bool isNpc = (it->second.npc_type == 1);
				if (cursorItem) {
					LOG_INFO(MOD_MAIN, "Initiating trade with {} (cursor item: {}, isNpc={})",
					         it->second.name, cursorItem->name, isNpc);
				} else {
					LOG_INFO(MOD_MAIN, "Initiating trade with {} (cursor money, isNpc={})",
					         it->second.name, isNpc);
				}
				ctx_.trade_manager->requestTrade(i.spawnId, it->second.name, isNpc);
				return;  // Don't target, we're initiating trade
			}
		}
	}

	if (ctx_.combat_manager) {
		if (ctx_.combat_manager->SetTarget(i.spawnId)) {
			SetTrackedTargetId(i.spawnId);
			// T286: SetTarget already wrote CombatState (the only store)

			auto it = ctx_.entities.find(i.spawnId);
			if (it != ctx_.entities.end()) {
				const Entity& e = it->second;

				// Publish TargetChanged event to bridge (handles renderer update)
				if (ctx_.bridge) {
					eqt::state::TargetChangedData tdata;
					tdata.spawnId = e.spawn_id;
					tdata.name = e.name;
					tdata.level = e.level;
					tdata.hpPercent = e.hp_percent;
					tdata.raceId = e.race_id;
					tdata.gender = e.gender;
					tdata.classId = e.class_id;
					tdata.bodyType = e.bodytype;
					tdata.npcType = e.npc_type;
					tdata.helm = e.helm;
					tdata.showHelm = e.showhelm;
					tdata.texture = e.equip_chest2;
					for (int j = 0; j < 9; ++j) {
						tdata.equipment[j] = e.equipment[j];
						tdata.equipmentTint[j] = e.equipment_tint[j];
					}
					ctx_.bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::TargetChanged, std::move(tdata)));
				}

				LOG_DEBUG(MOD_ENTITY, "=== TARGET SELECTED: {} ===", e.name);
				LOG_DEBUG(MOD_ENTITY, "  spawn_id={} race_id={} gender={} level={} class_id={}",
					i.spawnId, e.race_id, (int)e.gender, (int)e.level, (int)e.class_id);
				LOG_DEBUG(MOD_ENTITY, "  npc_type={} (0=player,1=npc,2=pc_corpse,3=npc_corpse) bodytype={}",
					(int)e.npc_type, (int)e.bodytype);
				LOG_DEBUG(MOD_ENTITY, "  face={} haircolor={} hairstyle={} beardcolor={} beard={}",
					(int)e.face, (int)e.haircolor, (int)e.hairstyle, (int)e.beardcolor, (int)e.beard);
				LOG_DEBUG(MOD_ENTITY, "  texture(equip_chest2)={} helm={} showhelm={} light={}",
					(int)e.equip_chest2, (int)e.helm, (int)e.showhelm, (int)e.light);
				LOG_DEBUG(MOD_ENTITY, "  equipment[0-8]: head={} chest={} arms={} wrist={} hands={} legs={} feet={} primary={} secondary={}",
					e.equipment[0], e.equipment[1], e.equipment[2], e.equipment[3],
					e.equipment[4], e.equipment[5], e.equipment[6], e.equipment[7], e.equipment[8]);
				LOG_DEBUG(MOD_ENTITY, "  equipment_tint[0-8]: {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}",
					e.equipment_tint[0], e.equipment_tint[1], e.equipment_tint[2], e.equipment_tint[3],
					e.equipment_tint[4], e.equipment_tint[5], e.equipment_tint[6], e.equipment_tint[7], e.equipment_tint[8]);
			}
		}
	}
}

void eqt::bridge::BridgeComponent::onLootCorpseIntent(const eqt::events::LootCorpseIntent& i) {
	// T268: the gesture enters the same loot machine /loot does
	// (CombatManager::LootCorpse -> RequestLootCorpse), so
	// LootAllIntent/CloseLootIntent can drive it afterwards.
	if (ctx_.combat_manager) {
		ctx_.combat_manager->LootCorpse(i.spawnId);
	} else {
		RequestLootCorpse(i.spawnId);
	}
}

void eqt::bridge::BridgeComponent::onDoorInteractIntent(const eqt::events::DoorInteractIntent& i) {
	ctx_.Entities().SendClickDoor(i.doorId);
}

void eqt::bridge::BridgeComponent::onVendorToggleIntent(const eqt::events::VendorToggleIntent& i) {
	// If vendor window is open, close it
	if (ctx_.self.IsVendorWindowOpen()) {
		ctx_.Merchant().CloseVendorWindow();
		return;
	}

	// Check if we have a target
	if (!ctx_.combat_manager || !ctx_.combat_manager->HasTarget()) {
		LOG_DEBUG(MOD_INVENTORY, "Vendor toggle: No target selected");
		return;
	}

	uint16_t target_id = ctx_.combat_manager->GetTargetId();
	auto it = ctx_.entities.find(target_id);
	if (it == ctx_.entities.end()) {
		LOG_DEBUG(MOD_INVENTORY, "Vendor toggle: Target {} not found in entities", target_id);
		return;
	}

	const Entity& target = it->second;

	if (target.npc_type != 1) {
		LOG_DEBUG(MOD_INVENTORY, "Vendor toggle: Target {} is not an NPC (type={})", target.name, target.npc_type);
		return;
	}

	constexpr uint8_t CLASS_MERCHANT = 41;
	if (target.class_id != CLASS_MERCHANT) {
		LOG_DEBUG(MOD_INVENTORY, "Vendor toggle: Target {} is not a merchant (class={})", target.name, target.class_id);
		return;
	}

	LOG_DEBUG(MOD_INVENTORY, "Vendor toggle: Opening vendor {} (id={})", target.name, target_id);
	ctx_.Merchant().RequestOpenVendor(target_id);
}

void eqt::bridge::BridgeComponent::onBankerInteractIntent(const eqt::events::BankerInteractIntent& i) {
	// If bank window is already open, ignore
	if (ctx_.self.IsBankWindowOpen()) {
		LOG_DEBUG(MOD_INVENTORY, "Banker interact: Bank already open");
		return;
	}

	auto it = ctx_.entities.find(i.npcId);
	if (it == ctx_.entities.end()) {
		LOG_DEBUG(MOD_INVENTORY, "Banker interact: NPC {} not found in entities", i.npcId);
		return;
	}

	const Entity& target = it->second;

	if (target.npc_type != 1) {
		LOG_DEBUG(MOD_INVENTORY, "Banker interact: Target {} is not an NPC (type={})", target.name, target.npc_type);
		return;
	}

	// Check distance to NPC
	float distSq = ctx_.Movement().CalculateDistance2D(ctx_.self.PosX(), ctx_.self.PosY(), target.x, target.y);
	distSq = distSq * distSq;  // CalculateDistance2D returns distance, not squared
	constexpr uint8_t CLASS_BANKER = 40;
	if (target.class_id == CLASS_BANKER) {
		// UIGAP-residue (B996 #316, MEASURED -- review/UIGAP-ORACLE/316/
		// range.tsv): the official right-click opens the bank at <= 20 units
		// and is a plain /consider (OP_Consider) at 21.2 and beyond -- no
		// "too far" line. A gate stricter than that is a defect (Will's rule).
		if (distSq > BANKER_INTERACTION_DISTANCE_SQUARED) {
			LOG_DEBUG(MOD_INVENTORY, "Banker interact: {} at {:.1f} > {:.0f}: a consider",
			          target.name, std::sqrt(distSq), BANKER_INTERACTION_DISTANCE);
			if (ctx_.combat_manager && ctx_.combat_manager->SetTarget(i.npcId)) {
				SetTrackedTargetId(i.npcId);
				ctx_.combat_manager->ConsiderTarget();
			}
			return;
		}
	} else if (distSq > NPC_INTERACTION_DISTANCE_SQUARED) {
		LOG_DEBUG(MOD_INVENTORY, "Banker interact: Target {} is too far away (dist={})", target.name, std::sqrt(distSq));
		ctx_.out.System("You are too far away to interact with this NPC.");
		return;
	}

	// B898 (#214): a guildmaster opens the trainer window. Ctrl+click on an
	// NPC is fable's stand-in for the official client's RIGHT click on one
	// (the right button is mouselook until #151's latch), and right-clicking
	// a guildmaster is how the official client opens training (eqstr 6788,
	// "Stand close to and right click on the %1 to train your skills.") --
	// the request is OP_GMTraining, the packet /train and T send.
	constexpr uint8_t CLASS_WARRIOR_GM = 20;
	constexpr uint8_t CLASS_BERSERKER_GM = 35;
	if (target.class_id >= CLASS_WARRIOR_GM && target.class_id <= CLASS_BERSERKER_GM) {
		LOG_INFO(MOD_MAIN, "Requesting training from {} (id={})", target.name, i.npcId);
		ctx_.Merchant().RequestTrainerWindow(i.npcId);
		return;
	}

	if (target.class_id != CLASS_BANKER) {
		LOG_DEBUG(MOD_INVENTORY, "Banker interact: Target {} is not a banker (class={})", target.name, target.class_id);
		ctx_.out.System("This NPC is not a banker.");
		return;
	}

	LOG_INFO(MOD_INVENTORY, "Opening bank window for {} (id={})", target.name, i.npcId);
	ctx_.Merchant().OpenBankWindow(i.npcId);
}

void eqt::bridge::BridgeComponent::onTrainerToggleIntent(const eqt::events::TrainerToggleIntent& i) {
	// If trainer window is open, close it
	if (ctx_.self.IsTrainerWindowOpen()) {
		ctx_.Merchant().CloseTrainerWindow();
		return;
	}

	if (!ctx_.combat_manager || !ctx_.combat_manager->HasTarget()) {
		LOG_DEBUG(MOD_MAIN, "Trainer toggle: No target selected");
		return;
	}

	uint16_t target_id = ctx_.combat_manager->GetTargetId();
	auto it = ctx_.entities.find(target_id);
	if (it == ctx_.entities.end()) {
		LOG_DEBUG(MOD_MAIN, "Trainer toggle: Target {} not found in entities", target_id);
		return;
	}

	const Entity& target = it->second;

	if (target.npc_type != 1) {
		LOG_DEBUG(MOD_MAIN, "Trainer toggle: Target {} is not an NPC (type={})", target.name, target.npc_type);
		return;
	}

	constexpr uint8_t CLASS_WARRIOR_GM = 20;
	constexpr uint8_t CLASS_BERSERKER_GM = 35;
	if (target.class_id < CLASS_WARRIOR_GM || target.class_id > CLASS_BERSERKER_GM) {
		LOG_DEBUG(MOD_MAIN, "Trainer toggle: Target {} is not a trainer (class={})", target.name, target.class_id);
		ctx_.out.System("That is not a trainer.");
		return;
	}

	LOG_DEBUG(MOD_MAIN, "Trainer toggle: Requesting trainer {} (id={}, class={})", target.name, target_id, target.class_id);
	ctx_.Merchant().RequestTrainerWindow(target_id);
}

void eqt::bridge::BridgeComponent::onWorldObjectInteractIntent(const eqt::events::WorldObjectInteractIntent& i) {
	auto it = ctx_.Entities().WorldObjects().find(i.dropId);
	if (it != ctx_.Entities().WorldObjects().end() && it->second.isTradeskillContainer()) {
		LOG_INFO(MOD_INVENTORY, "Clicking tradeskill container: dropId={} name='{}' type={}",
			i.dropId, it->second.name, it->second.object_type);
		ctx_.Entities().SendClickObject(i.dropId);
	} else if (it != ctx_.Entities().WorldObjects().end()) {
		LOG_DEBUG(MOD_ENTITY, "World object {} is not a tradeskill container (type={})",
			i.dropId, it->second.object_type);
	}
}

void eqt::bridge::BridgeComponent::onZoningEnabledIntent(const eqt::events::ZoningEnabledIntent& i) {
	ctx_.Zone().SetZoningEnabled(i.enabled);
	LOG_INFO(MOD_ZONE, "Zoning {}", i.enabled ? "enabled" : "disabled");
}

void eqt::bridge::BridgeComponent::onChatSubmitIntent(const eqt::events::ChatSubmitIntent& i) {
	ctx_.Chat().ProcessChatInput(i.text);
}

void eqt::bridge::BridgeComponent::onReadItemIntent(const eqt::events::ReadItemIntent& i) {
	ctx_.Merchant().RequestReadBook(i.filename, i.type);
}

// ================================================================
// D16: UI intents (D20a — intent-only, callbacks removed)
// ================================================================
// Spell intents
void eqt::bridge::BridgeComponent::onCastSpellIntent(const eqt::events::CastSpellIntent& i) {
	if (!ctx_.spell_manager) return;
	uint16_t targetId = ctx_.combat_manager ? ctx_.combat_manager->GetTargetId() : 0;
	EQ::CastResult result = ctx_.spell_manager->beginCastFromGem(i.gemSlot, targetId);
	if (result == EQ::CastResult::Success) {
		uint32_t spellId = ctx_.spell_manager->getMemorizedSpell(i.gemSlot);
		const EQ::SpellData* spell = ctx_.spell_manager->getSpell(spellId);
		if (spell) ctx_.out.System(fmt::format("Casting {}", spell->name));
	} else if (std::string refusal = EQ::castRefusalMessage(ctx_, result); !refusal.empty()) {
		// T244: same official-string table as EqActionHandler::
		// beginCastFromGem() -- the two sites had drifted. T251:
		// same renderer too (names a missing component).
		ctx_.out.System(refusal);
	} else if (result == EQ::CastResult::NotMemorized) {
		ctx_.out.System(fmt::format("No spell in gem {}", i.gemSlot + 1));
	} else if (result == EQ::CastResult::SpellNotReady) {
		ctx_.out.System("Spell not ready");
	}
}

void eqt::bridge::BridgeComponent::onScribeSpellIntent(const eqt::events::ScribeSpellIntent& i) {
	ctx_.Merchant().ScribeSpellFromScroll(i.spellId, i.bookSlot, i.sourceSlot);
}

void eqt::bridge::BridgeComponent::onSpellbookStateIntent(const eqt::events::SpellbookStateIntent& i) {
	ctx_.Entities().SendSpawnAppearance(AT_ANIMATION, i.isOpen ? 110 : 100);
}

// Buff/skill intents
void eqt::bridge::BridgeComponent::onBuffCancelIntent(const eqt::events::BuffCancelIntent& i) {
	// T277: gained a real producer this unit (a click on the
	// buff bar, static_ui_input.cpp; /removebuff reaches the
	// same body through EqActionHandler). Was OPTIMISTIC AND
	// LOCAL-ONLY: removeBuffBySlot(0, slot) erased the buff
	// from our own BuffManager and printed "cancelled" without
	// sending a single byte, so the server still had the buff,
	// it kept ticking, and the next OP_Buff or zone-in put it
	// straight back -- and it "worked" on detrimental buffs the
	// server would have refused. Now it asks and waits: the
	// buff leaves the model only when OP_Buff comes back with
	// bufffade == 1.
	ctx_.Player().SendBuffCancelRequest(i.slot);
}

void eqt::bridge::BridgeComponent::onSkillActivateIntent(const eqt::events::SkillActivateIntent& i) {
	// T156: gained a real producer this unit (a click on a row of
	// the skills popup, static_ui_input.cpp). Pass the current
	// target — Taunt/Disarm/PickPocket all need a real target id.
	if (ctx_.skill_manager && i.skillId <= 0xFF) {
		uint16_t targetId = ctx_.combat_manager ? ctx_.combat_manager->GetTargetId() : 0;
		ctx_.skill_manager->activateSkill(static_cast<uint8_t>(i.skillId), targetId);
	}
}

// Loot intents (T268: producers are the /loot family; a loot
// popup would push the same intents)
void eqt::bridge::BridgeComponent::onLootItemIntent(const eqt::events::LootItemIntent& i) {
	// Measured (#151 rows 46a/46c): LEFT -> auto_loot 0, onto the cursor;
	// RIGHT -> auto_loot 1, straight into the inventory.
	LootItemFromCorpse(i.corpseId, i.slot, i.autoLoot);
}

void eqt::bridge::BridgeComponent::onLootAllIntent(const eqt::events::LootAllIntent& i) {
	if (ctx_.combat_manager) {
		ctx_.combat_manager->LootAll();
	}
}

void eqt::bridge::BridgeComponent::onCloseLootIntent(const eqt::events::CloseLootIntent& i) {
	CloseLootWindow(i.corpseId);
}

// Vendor intents
void eqt::bridge::BridgeComponent::onVendorBuyIntent(const eqt::events::VendorBuyIntent& i) {
	ctx_.Merchant().BuyFromVendor(i.npcId, i.slot, i.quantity);
}

void eqt::bridge::BridgeComponent::onVendorSellIntent(const eqt::events::VendorSellIntent& i) {
	ctx_.Merchant().SellToVendor(i.npcId, i.slot, i.quantity);
}

// T436 (board B429): the bank window's Done button / close box. The existing
// BankerInteractIntent cannot close (its handler returns early when the
// window is already open), and CloseBankWindow() is the body /bank's no-arg
// toggle already reaches -- this is a second producer for it, not a second
// packet. The panel closes on the BankWindowClosed event that call publishes,
// never optimistically.
void eqt::bridge::BridgeComponent::onCloseBankIntent(const eqt::events::CloseBankIntent& i) {
	(void)i;
	ctx_.Merchant().CloseBankWindow();
}

// Bank intents (T269: producers are EverQuest::RequestBankCoinMove/
// Convert via /bank deposit|withdraw|convert). Wire truth, from
// eqemu zone/client_process.cpp Client::OPMoveCoin: from_slot/
// to_slot 1 = inventory, 2 = bank; cointype 0=cp 1=sp 2=gp 3=pp;
// the server clamps amount to the source bucket, converts at
// CoinTypeCoppers(cointype1)/CoinTypeCoppers(cointype2) truncating,
// requires a banker within USE_NPC_RANGE2 for slot 2, and SENDS
// NOTHING BACK on success (it ends in SaveCurrency(), a DB write).
// So there is no echo to project from: the arm refuses anything
// the server would clamp or drop, and otherwise applies the same
// arithmetic the server applies to both the EverQuest counters and
// the GameState pools /bank's pre-check reads.
void eqt::bridge::BridgeComponent::onBankCurrencyMoveIntent(const eqt::events::BankCurrencyMoveIntent& i) {
	if (!ctx_.self.IsBankWindowOpen()) {
		LOG_WARN(MOD_INVENTORY, "Bank coin move refused: bank window not open (type={} amount={} fromBank={})",
			i.coinType, i.amount, i.fromBank);
		return;
	}
	if (i.amount <= 0 || i.coinType > EQT::COINTYPE_PP) {
		LOG_WARN(MOD_INVENTORY, "Bank coin move refused: bad amount/type (type={} amount={})", i.coinType, i.amount);
		return;
	}

	const uint32_t srcCoins = BankCoinGet(i.coinType, /*bank=*/i.fromBank);
	const uint32_t dstCoins = BankCoinGet(i.coinType, /*bank=*/!i.fromBank);
	if (srcCoins < static_cast<uint32_t>(i.amount)) {
		// The server would silently move only what is there; refuse
		// instead so the projection and the wire never disagree.
		ctx_.out.System(fmt::format("You do not have {} {} {}.",
			i.amount, BankCoinName(i.coinType), i.fromBank ? "in the bank" : "on hand"));
		return;
	}

	EQT::MoveCoin_Struct move;
	move.from_slot = i.fromBank ? EQT::COINSLOT_BANK : EQT::COINSLOT_INVENTORY;
	move.to_slot = i.fromBank ? EQT::COINSLOT_INVENTORY : EQT::COINSLOT_BANK;
	move.cointype1 = i.coinType;
	move.cointype2 = i.coinType;
	move.amount = i.amount;
	ctx_.Merchant().SendMoveCoin(move);

	BankCoinSet(i.coinType, /*bank=*/i.fromBank, srcCoins - static_cast<uint32_t>(i.amount));
	BankCoinSet(i.coinType, /*bank=*/!i.fromBank, dstCoins + static_cast<uint32_t>(i.amount));

	LOG_DEBUG(MOD_INVENTORY, "Bank currency move: type={} amount={} fromBank={}, bank now: {}pp {}gp {}sp {}cp, inv now: {}pp {}gp {}sp {}cp",
		i.coinType, i.amount, i.fromBank,
		ctx_.game_state.player().bankPlatinum(), ctx_.game_state.player().bankGold(), ctx_.game_state.player().bankSilver(), ctx_.game_state.player().bankCopper(),
		ctx_.game_state.player().platinum(), ctx_.game_state.player().gold(), ctx_.game_state.player().silver(), ctx_.game_state.player().copper());

	PublishBankCoinProjection(/*inventoryChanged=*/true);
}

void eqt::bridge::BridgeComponent::onBankCurrencyConvertIntent(const eqt::events::BankCurrencyConvertIntent& i) {
	if (!ctx_.self.IsBankWindowOpen()) {
		LOG_WARN(MOD_INVENTORY, "Bank coin convert refused: bank window not open");
		return;
	}
	if (i.amount <= 0 || i.fromCoinType > EQT::COINTYPE_PP || i.toCoinType > EQT::COINTYPE_PP
	    || i.fromCoinType == i.toCoinType) {
		LOG_WARN(MOD_INVENTORY, "Bank coin convert refused: bad amount/types (from={} to={} amount={})",
			i.fromCoinType, i.toCoinType, i.amount);
		return;
	}

	// Same ratio the server computes: CoinTypeCoppers(from)/CoinTypeCoppers(to).
	const int64_t fromCoppers = BankCoinCoppers(i.fromCoinType);
	const int64_t toCoppers = BankCoinCoppers(i.toCoinType);
	int64_t converted = 0;
	if (toCoppers > fromCoppers) {
		const int64_t step = toCoppers / fromCoppers;  // 10, 100 or 1000
		if (i.amount % step != 0) {
			// The server would take only the largest multiple and
			// leave the remainder -- the wire amount would then
			// not describe what happened. Refuse.
			ctx_.out.System(fmt::format("Amount must be a multiple of {} to convert {} to {}.",
				step, BankCoinName(i.fromCoinType), BankCoinName(i.toCoinType)));
			return;
		}
		converted = i.amount / step;
	} else {
		converted = static_cast<int64_t>(i.amount) * (fromCoppers / toCoppers);
	}

	const uint32_t srcCoins = BankCoinGet(i.fromCoinType, /*bank=*/true);
	const uint32_t dstCoins = BankCoinGet(i.toCoinType, /*bank=*/true);
	if (srcCoins < static_cast<uint32_t>(i.amount)) {
		ctx_.out.System(fmt::format("You do not have {} {} in the bank.",
			i.amount, BankCoinName(i.fromCoinType)));
		return;
	}
	if (converted > static_cast<int64_t>(std::numeric_limits<uint32_t>::max() - dstCoins)) {
		ctx_.out.System("That conversion would overflow the destination pool.");
		return;
	}

	EQT::MoveCoin_Struct move;
	move.from_slot = EQT::COINSLOT_BANK;
	move.to_slot = EQT::COINSLOT_BANK;
	move.cointype1 = i.fromCoinType;
	move.cointype2 = i.toCoinType;
	move.amount = i.amount;
	ctx_.Merchant().SendMoveCoin(move);

	BankCoinSet(i.fromCoinType, /*bank=*/true, srcCoins - static_cast<uint32_t>(i.amount));
	BankCoinSet(i.toCoinType, /*bank=*/true, dstCoins + static_cast<uint32_t>(converted));

	LOG_DEBUG(MOD_INVENTORY, "Bank currency convert: {} {} -> {} {}; bank now: {}pp {}gp {}sp {}cp",
		i.amount, BankCoinName(i.fromCoinType), converted, BankCoinName(i.toCoinType),
		ctx_.game_state.player().bankPlatinum(), ctx_.game_state.player().bankGold(), ctx_.game_state.player().bankSilver(), ctx_.game_state.player().bankCopper());

	PublishBankCoinProjection(/*inventoryChanged=*/false);
}

// B441 (#26): the bank's Change button / `/bank change`. Both purses, bank
// then carried, each converted up to the fewest coins (coin_change.h quotes
// the client's own description and the assumptions). Same rules as the two
// T269 arms above: the bank window must be open (the server refuses slot 2
// with no banker in range), the server answers with silence, so the
// projection applies the arithmetic BuildMakeChangeMoves() already did.
void eqt::bridge::BridgeComponent::onBankMakeChangeIntent(const eqt::events::BankMakeChangeIntent& i) {
	(void)i;
	if (!ctx_.self.IsBankWindowOpen()) {
		LOG_WARN(MOD_INVENTORY, "Bank change refused: bank window not open");
		return;
	}
	size_t sent = 0;
	for (const bool bank : {true, false}) {
		eqt::merchant::CoinPurse purse{};
		for (uint8_t t = 0; t < 4; ++t) purse[t] = BankCoinGet(t, bank);
		const auto moves = eqt::merchant::BuildMakeChangeMoves(
			bank ? EQT::COINSLOT_BANK : EQT::COINSLOT_INVENTORY, purse);
		for (const auto& move : moves) ctx_.Merchant().SendMoveCoin(move);
		for (uint8_t t = 0; t < 4; ++t) BankCoinSet(t, bank, purse[t]);
		sent += moves.size();
	}
	LOG_DEBUG(MOD_INVENTORY, "Bank change: {} OP_MoveCoin steps; bank now {}pp {}gp {}sp {}cp, carried {}pp {}gp {}sp {}cp",
		sent,
		ctx_.game_state.player().bankPlatinum(), ctx_.game_state.player().bankGold(), ctx_.game_state.player().bankSilver(), ctx_.game_state.player().bankCopper(),
		ctx_.game_state.player().platinum(), ctx_.game_state.player().gold(), ctx_.game_state.player().silver(), ctx_.game_state.player().copper());
	if (sent > 0) PublishBankCoinProjection(/*inventoryChanged=*/true);
}

// Volume change intents (D20c2). T439 (board B432): the payload is an
// ABSOLUTE step 0..20, not a delta, and the applied value is published back
// on GameEventType::VolumesChanged -- which is what stops the Options slider
// and /musicvolume from disagreeing.
void eqt::bridge::BridgeComponent::onMusicVolumeChangeIntent(const eqt::events::MusicVolumeChangeIntent& i) {
#ifdef WITH_AUDIO
	if (ctx_.audio_manager) {
		const int step = eqt::audio::clampVolumeStep(i.step);
		ctx_.audio_manager->setMusicVolume(eqt::audio::volumeFractionFromStep(step));
		ctx_.out.System(fmt::format("Music volume: {}%", step * 5));
		PublishVolumesChanged();
	}
#endif
}

void eqt::bridge::BridgeComponent::onEffectsVolumeChangeIntent(const eqt::events::EffectsVolumeChangeIntent& i) {
#ifdef WITH_AUDIO
	if (ctx_.audio_manager) {
		const int step = eqt::audio::clampVolumeStep(i.step);
		ctx_.audio_manager->setEffectsVolume(eqt::audio::volumeFractionFromStep(step));
		ctx_.out.System(fmt::format("Effects volume: {}%", step * 5));
		PublishVolumesChanged();
	}
#endif
}

// Hotbar activate intent (D20b2)
void eqt::bridge::BridgeComponent::onHotbarActivateIntent(const eqt::events::HotbarActivateIntent& i) {
	// T156: switch on the model's enumerators, not on raw ints.
	// The old numbering here (0=Spell, 1=Item, 2=Emote, 3=Skill)
	// matched NEITHER enum in the tree; nothing ever produced it.
	// -Wswitch now catches the next drift, so there is no default.
	switch (i.type) {
	case eqt::HotbarSlotType::Spell: { // Spell
		if (ctx_.spell_manager) {
			for (uint8_t gem = 0; gem < EQT::VISIBLE_SPELL_GEM_COUNT; gem++) {  // T483: offered gems only
				if (ctx_.spell_manager->getMemorizedSpell(gem) == i.id) {
					uint16_t targetId = ctx_.combat_manager ? ctx_.combat_manager->GetTargetId() : 0;
					EQ::CastResult result = ctx_.spell_manager->beginCastFromGem(gem, targetId);
					if (result == EQ::CastResult::Success) {
						const EQ::SpellData* spell = ctx_.spell_manager->getSpell(i.id);
						if (spell) ctx_.out.System(fmt::format("Casting {}", spell->name));
					} else if (std::string refusal = EQ::castRefusalMessage(ctx_, result); !refusal.empty()) {
						// T253: third refusal site, same table/renderer as
						// CastSpellIntent above and EqActionHandler::
						// beginCastFromGem(). Was three literals (mana /
						// cooldown / already casting) and silence for
						// every other refusal.
						ctx_.out.System(refusal);
					} else if (result == EQ::CastResult::SpellNotReady) {
						ctx_.out.System("Spell not ready");
					}
					break;
				}
			}
		}
		break;
	}
	case eqt::HotbarSlotType::Item: {
		// T230: the id->slot hop + click-effect send now live in
		// UseInventoryItem(), shared with the keyboard route
		// (EqActionHandler::useItem <- ActionDispatcher::useItemById).
		if (ctx_.inventory_manager) {
			int16_t slot = ctx_.inventory_manager->findItemSlotByItemId(i.id);
			if (slot == eqt::inventory::SLOT_INVALID) {
				ctx_.out.System("Item not found in inventory");
				break;
			}
			UseInventoryItem(slot);
		}
		break;
	}
	case eqt::HotbarSlotType::Emote:
	case eqt::HotbarSlotType::Macro:
	case eqt::HotbarSlotType::Combat:
		// T239: the command-slot body is shared with the keyboard
		// route (EqActionHandler::activateHotbarCommandSlot). Was:
		// Emote -> ProcessChatInput inline, Macro/Combat no-ops.
		ActivateHotbarCommandSlot(i.type, i.emoteText);
		break;
	case eqt::HotbarSlotType::Skill: {
		// T156: reachable at last — see refactor/T156_skill_reachability.md.
		if (ctx_.skill_manager && i.id <= 0xFF) {
			uint16_t targetId = ctx_.combat_manager ? ctx_.combat_manager->GetTargetId() : 0;
			ctx_.skill_manager->activateSkill(static_cast<uint8_t>(i.id), targetId);
		}
		break;
	}
	case eqt::HotbarSlotType::Empty:
		break;
	}
}

// Tradeskill intents (D20b1)
void eqt::bridge::BridgeComponent::onTradeskillCombineIntent(const eqt::events::TradeskillCombineIntent& i) {
	// B899, MEASURED (UIGAP-ORACLE 215 combine.tsv row 7): a stack in the
	// container is refused here, with eqstr 8504 and no packet -- the window's
	// Combine and /combine both arrive through this one intent.
	if (ctx_.Commands().ContainerHoldsAStack(i.containerSlot)) {
		ctx_.out.System(ctx_.strings.GetStringMessage(8504));
		return;
	}
	ctx_.Player().SendTradeSkillCombine(i.containerSlot);
}

void eqt::bridge::BridgeComponent::onTradeskillCloseIntent(const eqt::events::TradeskillCloseIntent& i) {
	// B899: a CARRIED container's window (Done, close box, ESC). No server
	// session exists for it, and the world container -- if one is open --
	// is not the one being closed, so the active object id is left alone.
	if (i.inventorySlot >= 0) {
		ctx_.Commands().CloseCarriedContainer(i.inventorySlot);
		return;
	}
	if (i.dropId != 0) {
		ctx_.Entities().SendCloseContainer(i.dropId);
	}
	ctx_.active_tradeskill_object_id = 0;
}

// T419 (board B335): a recipe row click asks the server for that recipe's
// component list, the same OP_RecipeDetails /recipedetails sends.
void eqt::bridge::BridgeComponent::onRecipeDetailsRequestIntent(const eqt::events::RecipeDetailsRequestIntent& i) {
	ctx_.Merchant().SendRecipeDetails(i.recipeId);
}

namespace {

// T421: the bridge-side twin of EqActionHandler::resolveOpenContainerObjectType().
// The object_type of the OPEN tradeskill container, or false if none is open
// or its ground-spawn record is missing. Silent rather than chatty: unlike
// the slash-command path there is no typed command to answer, and a click on
// a window that is drawn can only be a stale frame.
bool resolveOpenTradeskillObjectType(eqt::ClientContext& ctx, uint32_t& out) {
	const uint32_t dropId = ctx.active_tradeskill_object_id;
	if (dropId == 0) {
		return false;
	}
	const eqt::WorldObject* obj = ctx.Entities().GetWorldObject(dropId);
	if (obj == nullptr) {
		LOG_WARN(MOD_UI, "Open tradeskill container {} has no world object record", dropId);
		return false;
	}
	out = obj->object_type;
	return true;
}

}  // namespace

// T421 (board B365): the tradeskill window's own search box and favourites
// row. Both reach senders that already existed and were reachable only from
// a slash command.
//
// `object_type` is not on either intent because it is not the UI's to know:
// it comes from the OPEN container's ground-spawn record, exactly the way
// EqActionHandler::resolveOpenContainerObjectType() reads it for
// /recipesearch. Resolving it here rather than shipping it across the bridge
// keeps one source for it.
void eqt::bridge::BridgeComponent::onRecipeSearchIntent(const eqt::events::RecipeSearchIntent& i) {
	uint32_t objectType = 0;
	if (!resolveOpenTradeskillObjectType(ctx_, objectType)) {
		return;
	}
	ctx_.Merchant().SendRecipesSearch(objectType, i.query, i.minTrivial, i.maxTrivial);
}

void eqt::bridge::BridgeComponent::onRecipeFavoriteIntent(const eqt::events::RecipeFavoriteIntent& i) {
	uint32_t objectType = 0;
	if (!resolveOpenTradeskillObjectType(ctx_, objectType)) {
		return;
	}
	// eqemu's OP_RecipesFavorite carries the WHOLE favourites set, not a
	// delta, so Add and Remove send the edited list. fable keeps no
	// favourites list of its own yet -- board B377 -- so Add sends the one
	// selected id and Remove sends an empty set, which is what "remove the
	// only thing in it" means on this wire. Show re-fetches whatever the
	// server has, which is the list-boundary request the window needs.
	std::vector<uint32_t> ids;
	if (i.op == eqt::events::RecipeFavoriteOp::Add) {
		if (i.recipeId == 0) {
			return;  // nothing selected -- refuse rather than send an empty add
		}
		ids.push_back(i.recipeId);
	} else if (i.op == eqt::events::RecipeFavoriteOp::Remove) {
		if (i.recipeId == 0) {
			return;
		}
	}
	ctx_.Merchant().SendRecipesFavorite(objectType, ids);
}

// T421 (board B298, item half): describe an inventory item.
//
// The TEXT is composed here, on the game thread, because this is the thread
// the ItemInstance lives on -- the renderer never sees one. See
// include/client/inventory/item_display_text.h for the oracle.
void eqt::bridge::BridgeComponent::onItemDisplayRequestIntent(const eqt::events::ItemDisplayRequestIntent& i) {
	if (!ctx_.inventory_manager || !ctx_.bridge) {
		return;
	}
	const eqt::inventory::ItemInstance* item = ctx_.inventory_manager->getItem(i.slot);
	if (item == nullptr) {
		return;
	}
	eqt::state::ItemDisplayShownData data;
	data.itemId = item->itemId;
	data.name = item->name;
	data.iconId = item->icon;
	// T429 (B390): the formats come out of the shipped eqstr table, not out
	// of the composer's source, so it needs EverQuest's own StringDatabase.
	data.lines = eqt::inventory::BuildItemDisplayText(ctx_.strings.Db(), *item);
	ctx_.bridge->pushEvent(eqt::state::GameEvent(
		eqt::state::GameEventType::ItemDisplayShown, std::move(data)));
}

// T425 (board B298, spell half). The SAME window as the item display, because
// the oracle has only one: uifiles/default holds no EQUI_SpellDisplay.xml and
// no SpellDisplay screen -- ItemDisplayWindow's own icon button decals the
// spell sheet (<NormalDecal>BuffIcons</NormalDecal>). So the answer is an
// ItemDisplayShown event with lines composed by BuildSpellDisplayText(), and
// the renderer never learns a SpellData exists.
void eqt::bridge::BridgeComponent::onSpellDisplayRequestIntent(const eqt::events::SpellDisplayRequestIntent& i) {
	if (!ctx_.spell_manager || !ctx_.bridge) {
		return;
	}
	const EQ::SpellData* spell = ctx_.spell_manager->getSpell(i.spellId);
	if (spell == nullptr) {
		return;
	}
	eqt::state::ItemDisplayShownData data;
	data.itemId = spell->id;
	data.name = spell->name;
	data.iconId = spell->spell_icon;
	data.lines = eqt::spell::BuildSpellDisplayText(
		ctx_.strings.Db(), *spell,
		ctx_.game_state.player().classId(), ctx_.game_state.player().level());
	ctx_.bridge->pushEvent(eqt::state::GameEvent(
		eqt::state::GameEventType::ItemDisplayShown, std::move(data)));
}

// Trainer intents (D20b1)
void eqt::bridge::BridgeComponent::onTrainSkillIntent(const eqt::events::TrainSkillIntent& i) {
	// B898: /train <skill|id> and the trainer window's Train button both push
	// this intent, so the refusals live on the consumer too (one body,
	// MerchantComponent::TrainRefusal). For /train it re-asks what the command
	// already asked of the same state, so it cannot disagree.
	if (i.selectOnly) {
		// UIGAP-residue (#214 train.tsv row 10): a row selection's line, no packet.
		const std::string line = ctx_.Merchant().TrainerSelectLine(i.skillId);
		if (!line.empty()) {
			ctx_.out.System(line);
		}
		return;
	}
	const std::string refusal = ctx_.Merchant().TrainRefusal(i.skillId);
	if (!refusal.empty()) {
		ctx_.out.System(refusal);
		return;
	}
	ctx_.Merchant().TrainSkill(i.skillId);
}

void eqt::bridge::BridgeComponent::onCloseTrainerIntent(const eqt::events::CloseTrainerIntent& i) {
	ctx_.Merchant().CloseTrainerWindow();
}

// Inventory intents (D20b1)
void eqt::bridge::BridgeComponent::onMoveItemIntent(const eqt::events::MoveItemIntent& i) {
	ctx_.Merchant().SendMoveItem(i.fromSlot, i.toSlot, i.quantity);
}

void eqt::bridge::BridgeComponent::onDeleteItemIntent(const eqt::events::DeleteItemIntent& i) {
	ctx_.Merchant().SendDeleteItem(i.slot);
}

void eqt::bridge::BridgeComponent::onEquipmentChangedIntent(const eqt::events::EquipmentChangedIntent& i) {
	ctx_.Entities().UpdatePlayerAppearanceFromInventory();
}

// B442 (#151): every item window's slot click and /pickup /putdown /coin.
void eqt::bridge::BridgeComponent::onCursorGestureIntent(const eqt::events::CursorGestureIntent& i) {
	RunCursorGesture(i);
}

// Audio (T328 Leg B) -- the ONE consumer of PlaySoundIntent.
// Forwards to the same BridgeComponent::PlaySound() the water
// path uses, so the loading-phase gate applies identically. In a
// build without audio the intent is produced and dropped here,
// which keeps the producers unconditional and testable.
void eqt::bridge::BridgeComponent::onPlaySoundIntent(const eqt::events::PlaySoundIntent& i) {
#ifdef WITH_AUDIO
	PlaySound(i.soundId);
#endif
}

// B430 -- the Options window's server-owned settings. One line: the body is
// ApplyGameOption() in bridge_component_options.cpp, which /option shares.
void eqt::bridge::BridgeComponent::onGameOptionIntent(const eqt::events::GameOptionIntent& i) {
	ApplyGameOption(i);
}

void eqt::bridge::BridgeComponent::onMemorizeSpellIntent(const eqt::events::MemorizeSpellIntent& i) {
	// T272: this arm had its ARGUMENTS SWAPPED --
	// memorizeSpell(uint32_t spell_id, uint8_t gem_slot) was
	// called as memorizeSpell(i.gemSlot, i.spellId), so it asked
	// SpellManager to memorize spell <gem number> into gem
	// <spell id truncated to 8 bits>. Nothing ever produced this
	// intent, so nothing ever executed the line (T264's dead-arm
	// class). Fixed with the producer that now exists.
	//
	// SpellManager is the gem authority (T240) and owns the same
	// body /mem reaches via EqActionHandler::memorizeSpellByName;
	// the feedback below mirrors the CastSpellIntent arm above,
	// which likewise renders its own chat text.
	if (!ctx_.spell_manager) {
		ctx_.out.System("Spell system not initialized");
	} else if (ctx_.spell_manager->memorizeSpell(i.spellId, i.gemSlot)) {
		ctx_.out.System(fmt::format("Memorizing spell in gem {}", i.gemSlot + 1));
	} else {
		ctx_.out.System(fmt::format(
			"Cannot memorize into gem {} - check if scribed and level requirement",
			i.gemSlot + 1));
	}
}

// B895 (GitLab #211): the Tracking Window's Track button, filter toggles and
// the two comboboxes. All four land on the bodies /track, /trackfilter,
// /tracksort and /trackplayers reach, so a click prints the same eqstr line
// a command does and publishes the same TrackingListChanged.
void eqt::bridge::BridgeComponent::onTrackingIntent(const eqt::events::TrackingIntent& i) {
	ctx_.Player().Tracking().ApplyIntent(i);
}

// B897 (GitLab #213) -- a Friends-window button. The SAME bodies the
// /friends, /ignore and /friendswho commands run (social_lists.h), so the
// window prints exactly the slash commands' oracle strings. The edited lists
// reach the window back through SocialListsChanged on the next GameTick
// (SyncSocialLists), never optimistically.
void eqt::bridge::BridgeComponent::onSocialListIntent(const eqt::events::SocialListIntent& i) {
	using eqt::state::SocialList;
	const SocialList list = (i.list == 1) ? SocialList::Ignored : SocialList::Friends;
	eqt::state::SocialOutcome outcome;
	if (i.op == eqt::events::SocialListOp::Who && !i.name.empty()) {
		// MEASURED (review/UIGAP-ORACLE/213/friends.tsv row 14): Who with a
		// friend SELECTED is a who-all for that one name (OP_WhoAllRequest,
		// "Players in EverQuest:"), i.e. what `/who <name>` sends.
		ctx_.Player().SendWhoAllRequest(i.name);
		return;
	}
	if (i.op == eqt::events::SocialListOp::Who) {
		// Row 13: no selection -> OP_FriendsWho with the stored names.
		outcome = eqt::state::RunFriendsWho(ctx_.game_state.social(), std::string());
	} else {
		outcome = eqt::state::ApplySocialEdit(
			ctx_.game_state.social(), list,
			i.op == eqt::events::SocialListOp::Add ? eqt::state::SocialEditOp::Add
			                                       : eqt::state::SocialEditOp::Remove,
			i.name, ctx_.character);
	}
	for (const auto& line : outcome.lines) ctx_.out.System(line);
	if (!outcome.whoList.empty()) ctx_.Player().SendFriendsWho(outcome.whoList);
}

// B896 (GitLab #212) -- a trade / give window button or coin row. The body is
// RunTradeWindowIntent() in bridge_component_trade.cpp, beside the trade
// hooks of the cursor gesture.
void eqt::bridge::BridgeComponent::onTradeWindowIntent(const eqt::events::TradeWindowIntent& i) {
	RunTradeWindowIntent(i);
}

// The intent pump. Pure dispatch: std::visit over an exhaustive overload set,
// one line per RendererIntent alternative, in the variant's own order. There
// is deliberately NO generic fallback lambda -- the old chain ended in an
// "unhandled intent type" LOG_TRACE that was unreachable (all 36 alternatives
// had arms). Without it, adding an alternative to RendererIntent without a
// handler here is a COMPILE ERROR rather than a runtime trace.
namespace {
template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;
}  // namespace

void eqt::bridge::BridgeComponent::ProcessBridgeIntents() {
	if (!ctx_.bridge) return;

	auto intents = ctx_.bridge->drainIntents();
	for (const auto& intent : intents) {
		std::visit(overloaded{
			[this](const eqt::events::PlayerPositionChanged& i) { onPlayerPositionChanged(i); },
			[this](const eqt::events::FallDamageIntent& i) { onFallDamageIntent(i); },
			[this](const eqt::events::PlayerModelGeometryChanged& i) { onPlayerModelGeometryChanged(i); },
			[this](const eqt::events::TargetIntent& i) { onTargetIntent(i); },
			[this](const eqt::events::ChatSubmitIntent& i) { onChatSubmitIntent(i); },
			[this](const eqt::events::DoorInteractIntent& i) { onDoorInteractIntent(i); },
			[this](const eqt::events::LootCorpseIntent& i) { onLootCorpseIntent(i); },
			[this](const eqt::events::LootItemIntent& i) { onLootItemIntent(i); },
			[this](const eqt::events::LootAllIntent& i) { onLootAllIntent(i); },
			[this](const eqt::events::CloseLootIntent& i) { onCloseLootIntent(i); },
			[this](const eqt::events::WorldObjectInteractIntent& i) { onWorldObjectInteractIntent(i); },
			[this](const eqt::events::ZoningEnabledIntent& i) { onZoningEnabledIntent(i); },
			[this](const eqt::events::ReadItemIntent& i) { onReadItemIntent(i); },
			[this](const eqt::events::VendorToggleIntent& i) { onVendorToggleIntent(i); },
			[this](const eqt::events::VendorBuyIntent& i) { onVendorBuyIntent(i); },
			[this](const eqt::events::VendorSellIntent& i) { onVendorSellIntent(i); },
			[this](const eqt::events::BankerInteractIntent& i) { onBankerInteractIntent(i); },
			[this](const eqt::events::CloseBankIntent& i) { onCloseBankIntent(i); },
			[this](const eqt::events::BankCurrencyMoveIntent& i) { onBankCurrencyMoveIntent(i); },
			[this](const eqt::events::BankCurrencyConvertIntent& i) { onBankCurrencyConvertIntent(i); },
			[this](const eqt::events::BankMakeChangeIntent& i) { onBankMakeChangeIntent(i); },
			[this](const eqt::events::TrainerToggleIntent& i) { onTrainerToggleIntent(i); },
			[this](const eqt::events::CastSpellIntent& i) { onCastSpellIntent(i); },
			[this](const eqt::events::MemorizeSpellIntent& i) { onMemorizeSpellIntent(i); },
			[this](const eqt::events::ScribeSpellIntent& i) { onScribeSpellIntent(i); },
			[this](const eqt::events::SpellbookStateIntent& i) { onSpellbookStateIntent(i); },
			[this](const eqt::events::BuffCancelIntent& i) { onBuffCancelIntent(i); },
			[this](const eqt::events::SkillActivateIntent& i) { onSkillActivateIntent(i); },
			[this](const eqt::events::MusicVolumeChangeIntent& i) { onMusicVolumeChangeIntent(i); },
			[this](const eqt::events::EffectsVolumeChangeIntent& i) { onEffectsVolumeChangeIntent(i); },
			[this](const eqt::events::HotbarActivateIntent& i) { onHotbarActivateIntent(i); },
			[this](const eqt::events::TradeskillCombineIntent& i) { onTradeskillCombineIntent(i); },
			[this](const eqt::events::TradeskillCloseIntent& i) { onTradeskillCloseIntent(i); },
			[this](const eqt::events::RecipeDetailsRequestIntent& i) { onRecipeDetailsRequestIntent(i); },
			[this](const eqt::events::RecipeSearchIntent& i) { onRecipeSearchIntent(i); },
			[this](const eqt::events::RecipeFavoriteIntent& i) { onRecipeFavoriteIntent(i); },
			[this](const eqt::events::ItemDisplayRequestIntent& i) { onItemDisplayRequestIntent(i); },
			[this](const eqt::events::SpellDisplayRequestIntent& i) { onSpellDisplayRequestIntent(i); },
			[this](const eqt::events::TrainSkillIntent& i) { onTrainSkillIntent(i); },
			[this](const eqt::events::CloseTrainerIntent& i) { onCloseTrainerIntent(i); },
			[this](const eqt::events::MoveItemIntent& i) { onMoveItemIntent(i); },
			[this](const eqt::events::DeleteItemIntent& i) { onDeleteItemIntent(i); },
			[this](const eqt::events::EquipmentChangedIntent& i) { onEquipmentChangedIntent(i); },
			[this](const eqt::events::CursorGestureIntent& i) { onCursorGestureIntent(i); },
			[this](const eqt::events::PlaySoundIntent& i) { onPlaySoundIntent(i); },
			[this](const eqt::events::GameOptionIntent& i) { onGameOptionIntent(i); },
			[this](const eqt::events::TrackingIntent& i) { onTrackingIntent(i); },
			[this](const eqt::events::SocialListIntent& i) { onSocialListIntent(i); },
			[this](const eqt::events::TradeWindowIntent& i) { onTradeWindowIntent(i); },
		}, intent);
	}
}

// Single-stage drain: drain + applyEvent() on the CALLING thread. Production
// caller is Application::updateGameState() in headless mode only (game
// thread, ConsoleBridge). Graphics runs must not call this -- their drain is
// split across threads (T220, see the note above and game_state_sync_bridge.h).
void eqt::bridge::BridgeComponent::ProcessBridgeEvents() {
	if (!ctx_.bridge) return;

	auto events = ctx_.bridge->drainEvents();
	for (const auto& event : events) {
		ctx_.bridge->applyEvent(event);
	}
}

