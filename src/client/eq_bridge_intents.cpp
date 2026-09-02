// eq_bridge_intents.cpp
//
// The EverQuest <-> renderer bridge pumps, split out of eq_bridge.cpp by J13
// (refactor/J13_eq_bridge_split.md); originally split out of eq.cpp (F10).
// Contains ProcessBridgeIntents (the renderer -> game intent consumer, with
// one is_same_v<T, eqt::events::...> arm per RendererIntent member) and
// ProcessBridgeEvents (the single-stage game -> renderer event drain).
//
// ProcessBridgeIntents is 580 lines and is a recorded Batch-G decomposition
// candidate; J13 deliberately did NOT decompose it (this commit is move-only).
//
// NOTE: tests/test_reachability_sweep.cpp opens THIS FILE AS TEXT and asserts
// that every RendererIntent member has an is_same_v<T, eqt::events::...> arm
// in it, and that every blocked intent still has one. If you move an arm out
// of this file, repoint that test.
//
// This is a pure move; no behavior or interface changes. See
// refactor/F10_bridge.md for the original moved/left mapping.
#include "client/eq.h"
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
#include "client/inventory/inventory_constants.h"
#include "client/inventory/item_instance.h"
#include "client/chat/chat_message_buffer.h"
#include "client/graphics/ui/command_registry.h"
#include "client/hotbar/hotbar_model.h"
#include "common/util/json_config.h"
#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/irrlicht_renderer.h"
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

void EverQuest::ProcessBridgeIntents() {
	if (!m_bridge) return;

	auto intents = m_bridge->drainIntents();
	for (const auto& intent : intents) {
		std::visit([this](const auto& i) {
			using T = std::decay_t<decltype(i)>;
			if constexpr (std::is_same_v<T, eqt::events::PlayerPositionChanged>) {
				// D14/S2: movement intent — sole movement channel
				OnGraphicsMovement(i);
			}
			// T311: the renderer's measured origin-to-feet distance for our own
			// model. One store (PlayerState), no event — nothing downstream of
			// the game thread consumes it; it exists so the packet edges can
			// convert render frame <-> wire frame.
			else if constexpr (std::is_same_v<T, eqt::events::PlayerModelGeometryChanged>) {
				auto& player = m_game_state.player();
				const float previous = player.collisionZOffset();
				player.setCollisionZOffset(i.collisionZOffset);
				// The deferred half of the self-spawn conversion. The spawn z
				// arrives before the model does, so it is stored raw; the moment
				// the mesh offset is measurable, convert it into the render frame
				// exactly once. Without this the send path would treat a wire
				// value as a render value and add the offset a second time.
				if (i.collisionZOffset > 0.0f && previous <= 0.0f
				    && player.zAwaitingFrameConversion()) {
					player.setZAwaitingFrameConversion(false);
					// SetPosition (not SetPosLocal): the renderer was handed the
					// unconverted z at zone-in and needs the corrected one.
					SetPosition(PosX(), PosY(),
					            eqt::wireZToRenderZ(PosZ(), i.collisionZOffset, player.size()));
				}
			}
			// ================================================================
			// D15: Interaction intents (D20a — intent-only, callbacks removed)
			// ================================================================
			else if constexpr (std::is_same_v<T, eqt::events::TargetIntent>) {
				// Check if we have a cursor item or cursor money and clicked on a player - initiate trade
				if (m_inventory_manager && m_trade_manager) {
					const auto* cursorItem = m_inventory_manager->getItem(eqt::inventory::CURSOR_SLOT);
					bool hasCursorMoney = m_inventory_manager->hasCursorMoney();
					if (cursorItem || hasCursorMoney) {
						auto it = m_entities.find(i.spawnId);
						if (it != m_entities.end() && (it->second.npc_type == 0 || it->second.npc_type == 1)) {
							bool isNpc = (it->second.npc_type == 1);
							if (cursorItem) {
								LOG_INFO(MOD_MAIN, "Initiating trade with {} (cursor item: {}, isNpc={})",
								         it->second.name, cursorItem->name, isNpc);
							} else {
								LOG_INFO(MOD_MAIN, "Initiating trade with {} (cursor money, isNpc={})",
								         it->second.name, isNpc);
							}
							m_trade_manager->requestTrade(i.spawnId, it->second.name, isNpc);
							return;  // Don't target, we're initiating trade
						}
					}
				}

				if (m_combat_manager) {
					if (m_combat_manager->SetTarget(i.spawnId)) {
						SetTrackedTargetId(i.spawnId);
						// T286: SetTarget already wrote CombatState (the only store)

						auto it = m_entities.find(i.spawnId);
						if (it != m_entities.end()) {
							const Entity& e = it->second;

							// Publish TargetChanged event to bridge (handles renderer update)
							if (m_bridge) {
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
								m_bridge->pushEvent(eqt::state::GameEvent(
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
			else if constexpr (std::is_same_v<T, eqt::events::LootCorpseIntent>) {
				// T268: the gesture enters the same loot machine /loot does
				// (CombatManager::LootCorpse -> RequestLootCorpse), so
				// LootAllIntent/CloseLootIntent can drive it afterwards.
				if (m_combat_manager) {
					m_combat_manager->LootCorpse(i.spawnId);
				} else {
					RequestLootCorpse(i.spawnId);
				}
			}
			else if constexpr (std::is_same_v<T, eqt::events::DoorInteractIntent>) {
				SendClickDoor(i.doorId);
			}
			else if constexpr (std::is_same_v<T, eqt::events::VendorToggleIntent>) {
				// If vendor window is open, close it
				if (IsVendorWindowOpen()) {
					CloseVendorWindow();
					return;
				}

				// Check if we have a target
				if (!m_combat_manager || !m_combat_manager->HasTarget()) {
					LOG_DEBUG(MOD_INVENTORY, "Vendor toggle: No target selected");
					return;
				}

				uint16_t target_id = m_combat_manager->GetTargetId();
				auto it = m_entities.find(target_id);
				if (it == m_entities.end()) {
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
				RequestOpenVendor(target_id);
			}
			else if constexpr (std::is_same_v<T, eqt::events::BankerInteractIntent>) {
				// If bank window is already open, ignore
				if (IsBankWindowOpen()) {
					LOG_DEBUG(MOD_INVENTORY, "Banker interact: Bank already open");
					return;
				}

				auto it = m_entities.find(i.npcId);
				if (it == m_entities.end()) {
					LOG_DEBUG(MOD_INVENTORY, "Banker interact: NPC {} not found in entities", i.npcId);
					return;
				}

				const Entity& target = it->second;

				if (target.npc_type != 1) {
					LOG_DEBUG(MOD_INVENTORY, "Banker interact: Target {} is not an NPC (type={})", target.name, target.npc_type);
					return;
				}

				// Check distance to NPC
				float distSq = CalculateDistance2D(PosX(), PosY(), target.x, target.y);
				distSq = distSq * distSq;  // CalculateDistance2D returns distance, not squared
				if (distSq > NPC_INTERACTION_DISTANCE_SQUARED) {
					LOG_DEBUG(MOD_INVENTORY, "Banker interact: Target {} is too far away (dist={})", target.name, std::sqrt(distSq));
					AddChatSystemMessage("You are too far away to interact with this NPC.");
					return;
				}

				constexpr uint8_t CLASS_BANKER = 40;
				if (target.class_id != CLASS_BANKER) {
					LOG_DEBUG(MOD_INVENTORY, "Banker interact: Target {} is not a banker (class={})", target.name, target.class_id);
					AddChatSystemMessage("This NPC is not a banker.");
					return;
				}

				LOG_INFO(MOD_INVENTORY, "Opening bank window for {} (id={})", target.name, i.npcId);
				OpenBankWindow(i.npcId);
			}
			else if constexpr (std::is_same_v<T, eqt::events::TrainerToggleIntent>) {
				// If trainer window is open, close it
				if (IsTrainerWindowOpen()) {
					CloseTrainerWindow();
					return;
				}

				if (!m_combat_manager || !m_combat_manager->HasTarget()) {
					LOG_DEBUG(MOD_MAIN, "Trainer toggle: No target selected");
					return;
				}

				uint16_t target_id = m_combat_manager->GetTargetId();
				auto it = m_entities.find(target_id);
				if (it == m_entities.end()) {
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
					AddChatSystemMessage("That is not a trainer.");
					return;
				}

				LOG_DEBUG(MOD_MAIN, "Trainer toggle: Requesting trainer {} (id={}, class={})", target.name, target_id, target.class_id);
				RequestTrainerWindow(target_id);
			}
			else if constexpr (std::is_same_v<T, eqt::events::WorldObjectInteractIntent>) {
				auto it = m_world_objects.find(i.dropId);
				if (it != m_world_objects.end() && it->second.isTradeskillContainer()) {
					LOG_INFO(MOD_INVENTORY, "Clicking tradeskill container: dropId={} name='{}' type={}",
						i.dropId, it->second.name, it->second.object_type);
					SendClickObject(i.dropId);
				} else if (it != m_world_objects.end()) {
					LOG_DEBUG(MOD_ENTITY, "World object {} is not a tradeskill container (type={})",
						i.dropId, it->second.object_type);
				}
			}
			else if constexpr (std::is_same_v<T, eqt::events::ZoningEnabledIntent>) {
				SetZoningEnabled(i.enabled);
				LOG_INFO(MOD_ZONE, "Zoning {}", i.enabled ? "enabled" : "disabled");
			}
			else if constexpr (std::is_same_v<T, eqt::events::ChatSubmitIntent>) {
				ProcessChatInput(i.text);
			}
			else if constexpr (std::is_same_v<T, eqt::events::ReadItemIntent>) {
				RequestReadBook(i.filename, i.type);
			}
			// ================================================================
			// D16: UI intents (D20a — intent-only, callbacks removed)
			// ================================================================
			// Spell intents
			else if constexpr (std::is_same_v<T, eqt::events::CastSpellIntent>) {
				if (!m_spell_manager) return;
				uint16_t targetId = m_combat_manager ? m_combat_manager->GetTargetId() : 0;
				EQ::CastResult result = m_spell_manager->beginCastFromGem(i.gemSlot, targetId);
				if (result == EQ::CastResult::Success) {
					uint32_t spellId = m_spell_manager->getMemorizedSpell(i.gemSlot);
					const EQ::SpellData* spell = m_spell_manager->getSpell(spellId);
					if (spell) AddChatSystemMessage(fmt::format("Casting {}", spell->name));
				} else if (std::string refusal = EQ::castRefusalMessage(*this, result); !refusal.empty()) {
					// T244: same official-string table as EqActionHandler::
					// beginCastFromGem() -- the two sites had drifted. T251:
					// same renderer too (names a missing component).
					AddChatSystemMessage(refusal);
				} else if (result == EQ::CastResult::NotMemorized) {
					AddChatSystemMessage(fmt::format("No spell in gem {}", i.gemSlot + 1));
				} else if (result == EQ::CastResult::SpellNotReady) {
					AddChatSystemMessage("Spell not ready");
				}
			}
			else if constexpr (std::is_same_v<T, eqt::events::ScribeSpellIntent>) {
				ScribeSpellFromScroll(i.spellId, i.bookSlot, i.sourceSlot);
			}
			else if constexpr (std::is_same_v<T, eqt::events::SpellbookStateIntent>) {
				SendSpawnAppearance(AT_ANIMATION, i.isOpen ? 110 : 100);
			}
			// Buff/skill intents
			else if constexpr (std::is_same_v<T, eqt::events::BuffCancelIntent>) {
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
				SendBuffCancelRequest(i.slot);
			}
			else if constexpr (std::is_same_v<T, eqt::events::SkillActivateIntent>) {
				// T156: gained a real producer this unit (a click on a row of
				// the skills popup, static_ui_input.cpp). Pass the current
				// target — Taunt/Disarm/PickPocket all need a real target id.
				if (m_skill_manager && i.skillId <= 0xFF) {
					uint16_t targetId = m_combat_manager ? m_combat_manager->GetTargetId() : 0;
					m_skill_manager->activateSkill(static_cast<uint8_t>(i.skillId), targetId);
				}
			}
			// Loot intents (T268: producers are the /loot family; a loot
			// popup would push the same intents)
			else if constexpr (std::is_same_v<T, eqt::events::LootItemIntent>) {
				LootItemFromCorpse(i.corpseId, i.slot);  // to the cursor, like the real client
			}
			else if constexpr (std::is_same_v<T, eqt::events::LootAllIntent>) {
				if (m_combat_manager) {
					m_combat_manager->LootAll();
				}
			}
			else if constexpr (std::is_same_v<T, eqt::events::CloseLootIntent>) {
				CloseLootWindow(i.corpseId);
			}
			// Vendor intents
			else if constexpr (std::is_same_v<T, eqt::events::VendorBuyIntent>) {
				BuyFromVendor(i.npcId, i.slot, i.quantity);
			}
			else if constexpr (std::is_same_v<T, eqt::events::VendorSellIntent>) {
				SellToVendor(i.npcId, i.slot, i.quantity);
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
			else if constexpr (std::is_same_v<T, eqt::events::BankCurrencyMoveIntent>) {
				if (!IsBankWindowOpen()) {
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
					AddChatSystemMessage(fmt::format("You do not have {} {} {}.",
						i.amount, BankCoinName(i.coinType), i.fromBank ? "in the bank" : "on hand"));
					return;
				}

				EQT::MoveCoin_Struct move;
				move.from_slot = i.fromBank ? EQT::COINSLOT_BANK : EQT::COINSLOT_INVENTORY;
				move.to_slot = i.fromBank ? EQT::COINSLOT_INVENTORY : EQT::COINSLOT_BANK;
				move.cointype1 = i.coinType;
				move.cointype2 = i.coinType;
				move.amount = i.amount;
				SendMoveCoin(move);

				BankCoinSet(i.coinType, /*bank=*/i.fromBank, srcCoins - static_cast<uint32_t>(i.amount));
				BankCoinSet(i.coinType, /*bank=*/!i.fromBank, dstCoins + static_cast<uint32_t>(i.amount));

				LOG_DEBUG(MOD_INVENTORY, "Bank currency move: type={} amount={} fromBank={}, bank now: {}pp {}gp {}sp {}cp, inv now: {}pp {}gp {}sp {}cp",
					i.coinType, i.amount, i.fromBank,
					m_game_state.player().bankPlatinum(), m_game_state.player().bankGold(), m_game_state.player().bankSilver(), m_game_state.player().bankCopper(),
					m_game_state.player().platinum(), m_game_state.player().gold(), m_game_state.player().silver(), m_game_state.player().copper());

				PublishBankCoinProjection(/*inventoryChanged=*/true);
			}
			else if constexpr (std::is_same_v<T, eqt::events::BankCurrencyConvertIntent>) {
				if (!IsBankWindowOpen()) {
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
						AddChatSystemMessage(fmt::format("Amount must be a multiple of {} to convert {} to {}.",
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
					AddChatSystemMessage(fmt::format("You do not have {} {} in the bank.",
						i.amount, BankCoinName(i.fromCoinType)));
					return;
				}
				if (converted > static_cast<int64_t>(std::numeric_limits<uint32_t>::max() - dstCoins)) {
					AddChatSystemMessage("That conversion would overflow the destination pool.");
					return;
				}

				EQT::MoveCoin_Struct move;
				move.from_slot = EQT::COINSLOT_BANK;
				move.to_slot = EQT::COINSLOT_BANK;
				move.cointype1 = i.fromCoinType;
				move.cointype2 = i.toCoinType;
				move.amount = i.amount;
				SendMoveCoin(move);

				BankCoinSet(i.fromCoinType, /*bank=*/true, srcCoins - static_cast<uint32_t>(i.amount));
				BankCoinSet(i.toCoinType, /*bank=*/true, dstCoins + static_cast<uint32_t>(converted));

				LOG_DEBUG(MOD_INVENTORY, "Bank currency convert: {} {} -> {} {}; bank now: {}pp {}gp {}sp {}cp",
					i.amount, BankCoinName(i.fromCoinType), converted, BankCoinName(i.toCoinType),
					m_game_state.player().bankPlatinum(), m_game_state.player().bankGold(), m_game_state.player().bankSilver(), m_game_state.player().bankCopper());

				PublishBankCoinProjection(/*inventoryChanged=*/false);
			}
			// Volume change intents (D20c2)
			else if constexpr (std::is_same_v<T, eqt::events::MusicVolumeChangeIntent>) {
#ifdef WITH_AUDIO
				if (m_audio_manager) {
					float newVol = std::max(0.0f, std::min(1.0f, m_audio_manager->getMusicVolume() + i.delta));
					m_audio_manager->setMusicVolume(newVol);
					AddChatSystemMessage(fmt::format("Music volume: {}%", static_cast<int>(newVol * 100)));
				}
#endif
			}
			else if constexpr (std::is_same_v<T, eqt::events::EffectsVolumeChangeIntent>) {
#ifdef WITH_AUDIO
				if (m_audio_manager) {
					float newVol = std::max(0.0f, std::min(1.0f, m_audio_manager->getEffectsVolume() + i.delta));
					m_audio_manager->setEffectsVolume(newVol);
					AddChatSystemMessage(fmt::format("Effects volume: {}%", static_cast<int>(newVol * 100)));
				}
#endif
			}
			// Hotbar activate intent (D20b2)
			else if constexpr (std::is_same_v<T, eqt::events::HotbarActivateIntent>) {
				// T156: switch on the model's enumerators, not on raw ints.
				// The old numbering here (0=Spell, 1=Item, 2=Emote, 3=Skill)
				// matched NEITHER enum in the tree; nothing ever produced it.
				// -Wswitch now catches the next drift, so there is no default.
				switch (i.type) {
				case eqt::HotbarSlotType::Spell: { // Spell
					if (m_spell_manager) {
						for (uint8_t gem = 0; gem < EQ::MAX_SPELL_GEMS; gem++) {
							if (m_spell_manager->getMemorizedSpell(gem) == i.id) {
								uint16_t targetId = m_combat_manager ? m_combat_manager->GetTargetId() : 0;
								EQ::CastResult result = m_spell_manager->beginCastFromGem(gem, targetId);
								if (result == EQ::CastResult::Success) {
									const EQ::SpellData* spell = m_spell_manager->getSpell(i.id);
									if (spell) AddChatSystemMessage(fmt::format("Casting {}", spell->name));
								} else if (std::string refusal = EQ::castRefusalMessage(*this, result); !refusal.empty()) {
									// T253: third refusal site, same table/renderer as
									// CastSpellIntent above and EqActionHandler::
									// beginCastFromGem(). Was three literals (mana /
									// cooldown / already casting) and silence for
									// every other refusal.
									AddChatSystemMessage(refusal);
								} else if (result == EQ::CastResult::SpellNotReady) {
									AddChatSystemMessage("Spell not ready");
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
					if (m_inventory_manager) {
						int16_t slot = m_inventory_manager->findItemSlotByItemId(i.id);
						if (slot == eqt::inventory::SLOT_INVALID) {
							AddChatSystemMessage("Item not found in inventory");
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
					if (m_skill_manager && i.id <= 0xFF) {
						uint16_t targetId = m_combat_manager ? m_combat_manager->GetTargetId() : 0;
						m_skill_manager->activateSkill(static_cast<uint8_t>(i.id), targetId);
					}
					break;
				}
				case eqt::HotbarSlotType::Empty:
					break;
				}
			}
			// Tradeskill intents (D20b1)
			else if constexpr (std::is_same_v<T, eqt::events::TradeskillCombineIntent>) {
				SendTradeSkillCombine(i.containerSlot);
			}
			else if constexpr (std::is_same_v<T, eqt::events::TradeskillCloseIntent>) {
				if (i.dropId != 0) {
					SendCloseContainer(i.dropId);
				}
				m_active_tradeskill_object_id = 0;
			}
			// Trainer intents (D20b1)
			else if constexpr (std::is_same_v<T, eqt::events::TrainSkillIntent>) {
				TrainSkill(i.skillId);
			}
			else if constexpr (std::is_same_v<T, eqt::events::CloseTrainerIntent>) {
				CloseTrainerWindow();
			}
			// Inventory intents (D20b1)
			else if constexpr (std::is_same_v<T, eqt::events::MoveItemIntent>) {
				SendMoveItem(i.fromSlot, i.toSlot, i.quantity);
			}
			else if constexpr (std::is_same_v<T, eqt::events::DeleteItemIntent>) {
				SendDeleteItem(i.slot);
			}
			else if constexpr (std::is_same_v<T, eqt::events::EquipmentChangedIntent>) {
				UpdatePlayerAppearanceFromInventory();
			}
			else if constexpr (std::is_same_v<T, eqt::events::MemorizeSpellIntent>) {
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
				if (!m_spell_manager) {
					AddChatSystemMessage("Spell system not initialized");
				} else if (m_spell_manager->memorizeSpell(i.spellId, i.gemSlot)) {
					AddChatSystemMessage(fmt::format("Memorizing spell in gem {}", i.gemSlot + 1));
				} else {
					AddChatSystemMessage(fmt::format(
						"Cannot memorize into gem {} - check if scribed and level requirement",
						i.gemSlot + 1));
				}
			}
			else {
				LOG_TRACE(MOD_MAIN, "Bridge: unhandled intent type");
			}
		}, intent);
	}
}

// Single-stage drain: drain + applyEvent() on the CALLING thread. Production
// caller is Application::updateGameState() in headless mode only (game
// thread, ConsoleBridge). Graphics runs must not call this -- their drain is
// split across threads (T220, see the note above and game_state_sync_bridge.h).
void EverQuest::ProcessBridgeEvents() {
	if (!m_bridge) return;

	auto events = m_bridge->drainEvents();
	for (const auto& event : events) {
		m_bridge->applyEvent(event);
	}
}

