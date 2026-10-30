// eqt::commands::CommandsComponent combat-resolution packet cluster.
//
// Split out of eq.cpp (Batch F, unit F13): the Consider handler
// (ZoneProcessConsider), the Action handler (ZoneProcessAction), the Damage
// handler (ZoneProcessDamage, ~237 lines, plus its file-local getDamageVerb
// helper), the MoneyOnCorpse handler (ZoneProcessMoneyOnCorpse), and the
// OP_Stun handler (ZoneProcessStun/IsStunned, T247). Moved verbatim, no
// behavior change. See refactor/F13_combat_actions.md.
//
// K12 (Batch K): moved again, from src/client/eq_combat_actions.cpp to
// here, and the bodies rebased from EverQuest onto
// eqt::commands::CommandsComponent -- the "combat actions" section of the
// commands family, per the batch plan. m_stunned_until moved WITH this
// family, so its references need no prefix at all. See the K12 Review.
//
// T268: the headless loot-item handler (ZoneProcessLootItem, a second parser
// of the loot ItemPacket that read the item name from the wrong field) is
// gone -- every loot window is populated by ZoneProcessLootItemToUI
// (eq_merchant.cpp), which now also feeds CombatManager's auto-loot queue.
// The loot-corpse family (RequestLootCorpse, LootItemFromCorpse,
// CloseLootWindow) lives in eq_bridge.cpp.

#include "client/eq.h"
#include "client/commands/commands_component.h"
#include "client/combat.h"
#include "client/spell/spell_manager.h"
#include "client/trade_manager.h"
#include "client/animation_constants.h"
#include "common/logging.h"
#include "common/packet_structs.h"
#include "common/name_utils.h"
#include "client/bridge/game_state_bridge.h"
// T382 (B172): the corpse-coin grant writes the purse, publishes
// CurrencyChanged and asks for the coin sound through T328 Leg B's intent.
#include "client/state/game_state.h"
#include "client/state/event_bus.h"
#include "client/events/renderer_intents.h"

// P28: renderer-free chat data model, used unconditionally below
// (eqt::ui::ChatChannel).
#include "client/chat/chat_message_buffer.h"
#include "client/commands/consider_text.h"

#ifdef WITH_AUDIO
#include "client/audio/audio_manager.h"
#include "client/audio/sound_assets.h"
#include "client/audio/ui_sounds.h"
#endif

#include <sstream>

// Combat-related packet handlers
void eqt::commands::CommandsComponent::ZoneProcessConsider(const EQ::Net::Packet &p)
{
	// Consider response from server
	if (p.Length() < 30) { // 2 opcode + 28 Consider_Struct
		return;
	}

	struct Consider_Struct {
		uint32_t playerid;
		uint32_t targetid;
		uint32_t faction;
		uint32_t level;
		int32_t  cur_hp;
		int32_t  max_hp;
		uint8_t  pvpcon;
		uint8_t  unknown3[3];
	} *con = (Consider_Struct*)((uint8_t*)p.Data() + 2);

	if (EverQuest::GetDebugLevel() >= 1) {
		LOG_DEBUG(MOD_MAIN, "Consider: target={}, faction={}, level={}, hp={}/{}",
			con->targetid, con->faction, con->level, con->cur_hp, con->max_hp);
	}

	// Update combat manager with consider info
	if (ctx_.combat_manager) {
		ctx_.combat_manager->ProcessConsiderResponse(con->targetid, con->faction, con->level,
			con->cur_hp, con->max_hp);
	}

	// Display consider message to chat
	std::string target_name = "Unknown";
	auto it = ctx_.entities.find(static_cast<uint16_t>(con->targetid));
	if (it != ctx_.entities.end()) {
		target_name = EQT::toDisplayName(it->second.name);
	}

	// T429 Leg B (boards B298 entity half / B390): the faction phrase and the
	// whole line come out of the shipped string table now -- eqstr 12239
	// "%1 %2 -- %3" with 12212..12220 in %2. Before this the nine phrases were
	// literals here, and faction 8 printed "threatens you", a phrase that is
	// in NO eqstr row (the oracle's is 12218 "glares at you threateningly").
	// See include/client/commands/consider_text.h.

	// Determine con colour name from the server level value. This is FABLE'S
	// OWN wording, not the oracle's: eqstr has sixteen difficulty lines
	// (12223..12238) and no colour->line mapping, so picking six of sixteen
	// would be inference (board B417).
	std::string con_color;
	switch (con->level) {
		case 2:  con_color = "cons green"; break;
		case 4:  con_color = "cons blue"; break;
		case 6:  con_color = "cons gray"; break;
		case 10: con_color = "cons light blue"; break;
		case 13: con_color = "cons red"; break;
		case 15: con_color = "cons yellow"; break;
		case 18: con_color = "cons light blue"; break;
		case 20: con_color = "cons white"; break;
		default: con_color = "cons white"; break;
	}

	const std::string message = eqt::commands::BuildConsiderText(
		ctx_.strings.Db(), target_name, con->faction, con_color);
	if (message.empty()) {
		// No 12239 in the table: print nothing rather than fabricate a line.
		LOG_WARN(MOD_MAIN, "Consider: eqstr 12239 missing, no line drawn");
		return;
	}
	// Publish SystemMessage event to bridge
	if (ctx_.bridge) {
		eqt::state::ChatMessageData data;
		data.message = message;
		data.channelType = static_cast<uint32_t>(eqt::ui::ChatChannel::System);
		data.channelName = "system";
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::SystemMessage, std::move(data)));
	}
	LOG_INFO(MOD_MAIN, "{}", message);
}

void eqt::commands::CommandsComponent::ZoneProcessAction(const EQ::Net::Packet &p)
{
	// Combat action (attack, spell cast, etc)
	if (p.Length() < 33) { // 2 opcode + the 31-byte wire Action struct below
		return;
	}

	// Local struct matching wire format (31 bytes)
	struct ActionPacket_Struct {
		uint16_t target;
		uint16_t source;
		uint16_t level;
		uint32_t instrument_mod;
		float    force;
		float    hit_heading;
		float    hit_pitch;
		uint8_t  type;
		uint16_t unknown23;
		uint16_t unknown25;
		uint16_t spell;
		uint8_t  spell_level;
		uint8_t  effect_flag;
	} __attribute__((packed));

	const ActionPacket_Struct* action = (const ActionPacket_Struct*)((uint8_t*)p.Data() + 2);

	if (EverQuest::GetDebugLevel() >= 1) {
		LOG_DEBUG(MOD_MAIN, "Action: source={} -> target={}, type={}, spell={}", action->source, action->target, action->type, action->spell);
	}

	// Convert to the spell manager's in-memory DTO (NOT a wire struct -- the
	// wire layout is the 31-byte ActionPacket_Struct above)
	EQT::SpellActionInfo spellAction;
	spellAction.target = action->target;
	spellAction.source = action->source;
	spellAction.level = action->level;
	spellAction.instrument_mod = action->instrument_mod;
	spellAction.force = action->force;
	spellAction.hit_heading = action->hit_heading;
	spellAction.hit_pitch = action->hit_pitch;
	spellAction.type = action->type;
	spellAction.spell = action->spell;
	spellAction.level2 = action->spell_level;
	spellAction.effect_flag = action->effect_flag;

	// T247: an NPC casting a detrimental spell at us is on our hate list.
	// Beneficial NPC casts (and our own / other players' actions) are not.
	if (action->target == ctx_.self.MySpawnId() && action->source != ctx_.self.MySpawnId() &&
	    ctx_.combat_manager && ctx_.spell_manager) {
		auto src = ctx_.entities.find(action->source);
		const EQ::SpellData* spell = ctx_.spell_manager->getSpell(action->spell);
		if (src != ctx_.entities.end() && src->second.npc_type == 1 && spell && spell->isDetrimental()) {
			ctx_.combat_manager->NoteAttackedBy(action->source);
		}
	}

	// Pass to spell manager for spell effect handling
	if (ctx_.spell_manager) {
		ctx_.spell_manager->handleAction(spellAction);
	}

	// T348 (board B055): a buff LANDING on us arrives here and nowhere else.
	// effect_flag == 4 is the second of the pair and means the spell took
	// effect; type 231 (0xE7) is the spell-effect action. eqemu never sends a
	// Titanium client an "applied" OP_Buff, so without this the buff list --
	// and every hook hanging off it, including T339's vision publish -- never
	// hears about a cast that landed until the next zone-in's PlayerProfile.
	// Measured wire order and the reasoning: PlayerComponent::NoteBuffLandedOnPlayer
	// in player/player_buffs.cpp.
	if (action->type == 231 && action->effect_flag == 4 &&
	    action->target == ctx_.self.MySpawnId()) {
		ctx_.Player().NoteBuffLandedOnPlayer(action->spell, action->source,
			static_cast<uint8_t>(action->level));
	}

	// Note: Attack animations are NOT triggered here - they come from animation updates
	// when the entity initiates an attack. The Action packet is for spell/ability effects.
}

// Helper: Get damage verb based on damage type
// Returns pair: {second_person, third_person} e.g. {"crush", "crushes"}
static std::pair<const char*, const char*> getDamageVerb(uint8_t damage_type) {
	switch (damage_type) {
		case 0:  // Blunt (1HBlunt, 2HBlunt)
			return {"crush", "crushes"};
		case 1:  // Slashing (1HSlashing, 2HSlashing)
			return {"slash", "slashes"};
		case 4:  // Hand-to-hand, Feign Death
			return {"hit", "hits"};
		case 7:  // Archery
			return {"hit", "hits"};
		case 8:  // Backstab
			return {"backstab", "backstabs"};
		case 10: // Bash
			return {"bash", "bashes"};
		case 21: // Dragon Punch
			return {"strike", "strikes"};
		case 23: // Eagle Strike, Tiger Claw
			return {"strike", "strikes"};
		case 30: // Kick, Flying Kick, Round Kick
			return {"kick", "kicks"};
		case 36: // Piercing (1HPiercing, 2HPiercing)
			return {"pierce", "pierces"};
		case 51: // Throwing
			return {"hit", "hits"};
		case 74: // Frenzy
			return {"hit", "hits"};
		default:
			return {"hit", "hits"};
	}
}

void eqt::commands::CommandsComponent::ZoneProcessDamage(const EQ::Net::Packet &p)
{
	LOG_INFO(MOD_COMBAT, "ZoneProcessDamage called, packet length={}", p.Length());

	// Damage notification
	if (p.Length() < 25) { // 2 opcode + 23 CombatDamage_Struct
		LOG_INFO(MOD_COMBAT, "Damage packet too short: {} < 25", p.Length());
		return;
	}

	struct CombatDamage_Struct {
		uint16_t target;
		uint16_t source;
		uint8_t  type;
		uint16_t spellid;
		uint32_t damage;
		float    force;
		float    hit_heading;
		float    hit_pitch;
		uint8_t  unknown06;
	} __attribute__((packed)) *dmg = (CombatDamage_Struct*)((uint8_t*)p.Data() + 2);

	// Debug: show raw bytes for damage field
	// Copy packed struct fields to local variables for fmt::format
	uint16_t target_id = dmg->target;
	uint16_t source_id = dmg->source;
	uint8_t damage_type = dmg->type;
	uint16_t spell_id = dmg->spellid;
	int32_t damage_amount = dmg->damage;

	// T247: every attack an NPC makes on us arrives here -- hits, misses
	// (damage 0) and DMG_INVULNERABLE (-5) alike -- and is the client's only
	// evidence that it is on that NPC's hate list. Mark it for CheckForAggro.
	if (target_id == ctx_.self.MySpawnId() && source_id != ctx_.self.MySpawnId() && ctx_.combat_manager) {
		auto src = ctx_.entities.find(source_id);
		if (src != ctx_.entities.end() && src->second.npc_type == 1) {
			ctx_.combat_manager->NoteAttackedBy(source_id);
		}
	}

	// Cancel trade if player takes damage (entering combat)
	if (target_id == ctx_.self.MySpawnId() && damage_amount > 0 &&
		ctx_.trade_manager && ctx_.trade_manager->isTrading()) {
		LOG_DEBUG(MOD_MAIN, "Player took damage during trade, canceling trade");
		ctx_.trade_manager->cancelTrade();
		ctx_.out.System("Trade cancelled - you entered combat");
	}

	if (EverQuest::GetDebugLevel() >= 3) {
		const uint8_t* data = static_cast<const uint8_t*>(p.Data()) + 2;
		std::string hexBytes;
		for (int i = 0; i < 12 && i < static_cast<int>(p.Length()) - 2; ++i) {
			hexBytes += fmt::format("{:02x} ", data[i]);
		}
		LOG_TRACE(MOD_COMBAT, "Damage packet raw bytes (first 12): {}", hexBytes);
		LOG_DEBUG(MOD_COMBAT, "Damage target={} ({:04x}), source={} ({:04x}), type={} ({:02x}), spell={} ({:04x})", target_id, target_id, source_id, source_id, damage_type, damage_type, spell_id, spell_id);
		LOG_TRACE(MOD_COMBAT, "Damage bytes at offset 7: {:02x} {:02x} {:02x} {:02x}", data[7], data[8], data[9], data[10]);
	}

	// Get entity names for combat messages
	std::string target_name;
	std::string source_name;
	auto target_it = ctx_.entities.find(target_id);
	auto source_it = ctx_.entities.find(source_id);
	if (target_it != ctx_.entities.end()) {
		target_name = EQT::toDisplayName(target_it->second.name);
	}
	if (source_it != ctx_.entities.end()) {
		source_name = EQT::toDisplayName(source_it->second.name);
	}

	if (EverQuest::GetDebugLevel() >= 2 || IsTrackedTarget(target_id) || IsTrackedTarget(source_id)) {
		LOG_DEBUG(MOD_COMBAT, "{} -> {} for {} damage (type {})",
			source_name.empty() ? std::to_string(source_id) : source_name,
			target_name.empty() ? std::to_string(target_id) : target_name,
			damage_amount, damage_type);
	}

	// Add combat chat messages
	if (damage_amount > 0 && !target_name.empty()) {
		bool player_is_target = (target_id == ctx_.self.MySpawnId());
		bool player_is_source = (source_id == ctx_.self.MySpawnId());

		// Get verb based on damage type (crush, slash, pierce, kick, etc.)
		auto [verb_second, verb_third] = getDamageVerb(damage_type);

		std::string msg;
		if (player_is_source) {
			// Player dealt damage
			if (spell_id > 0 && spell_id != 0xFFFF) {
				msg = fmt::format("You hit {} for {} points of non-melee damage.", target_name, damage_amount);
			} else {
				msg = fmt::format("You {} {} for {} points of damage.", verb_second, target_name, damage_amount);
			}
			ctx_.out.Combat(msg, true);
		} else if (player_is_target) {
			// Player took damage
			if (spell_id > 0 && spell_id != 0xFFFF) {
				msg = fmt::format("{} hit you for {} points of non-melee damage.", source_name.empty() ? "Unknown" : source_name, damage_amount);
			} else {
				msg = fmt::format("{} {} YOU for {} points of damage.", source_name.empty() ? "Unknown" : source_name, verb_third, damage_amount);
			}
			ctx_.out.Combat(msg, true);
		} else if (!source_name.empty()) {
			// Observing combat between others
			if (spell_id > 0 && spell_id != 0xFFFF) {
				msg = fmt::format("{} hit {} for {} points of non-melee damage.", source_name, target_name, damage_amount);
			} else {
				msg = fmt::format("{} {} {} for {} points of damage.", source_name, verb_third, target_name, damage_amount);
			}
			ctx_.out.Combat(msg, false);
		}
	} else if (damage_amount == 0 && !target_name.empty()) {
		// Miss - use CombatMiss channel (filterable separately from hits)
		bool player_is_source = (source_id == ctx_.self.MySpawnId());
		bool player_is_target = (target_id == ctx_.self.MySpawnId());

		// Get verb for miss messages too
		auto [verb_second, verb_third] = getDamageVerb(damage_type);

		std::string msg;
		if (player_is_source) {
			msg = fmt::format("You try to {} {} but miss!", verb_second, target_name);
			ctx_.out.Miss(msg);
		} else if (player_is_target && !source_name.empty()) {
			msg = fmt::format("{} tries to {} YOU but misses!", source_name, verb_second);
			ctx_.out.Miss(msg);
		} else if (!source_name.empty()) {
			// Observing combat between others - also show misses
			msg = fmt::format("{} tries to {} {} but misses!", source_name, verb_second, target_name);
			ctx_.out.Miss(msg);
		}
	}

#ifdef WITH_AUDIO
	// Play combat sounds at target position
	if (ctx_.audio_manager && target_it != ctx_.entities.end()) {
		const Entity& target = target_it->second;
		bool isMelee = (spell_id == 0 || spell_id == 0xFFFF);

		if (damage_amount > 0) {
			if (isMelee) {
				// Melee hit sound
				ctx_.audio_manager->playSound(EQT::Audio::SoundId::MELEE_HIT,
					glm::vec3(target.x, target.y, target.z));
			} else {
				// Spell damage - play spell impact sound
				// TODO: Map specific spells to specific impact sounds
				ctx_.audio_manager->playSound(EQT::Audio::SoundId::SPELL_CAST,
					glm::vec3(target.x, target.y, target.z));
			}
		} else if (isMelee) {
			// Miss - play swing sound
			ctx_.audio_manager->playSound(EQT::Audio::SoundId::MELEE_MISS,
				glm::vec3(target.x, target.y, target.z));
		}
	}
#endif

	// Publish DamageEvent to bridge
	if (ctx_.bridge && damage_amount > 0) {
		eqt::state::DamageEventData data;
		data.sourceId = source_id;
		data.targetId = target_id;
		data.amount = damage_amount;
		data.type = damage_type;
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::DamageEvent, std::move(data)));
	}

	// Publish combat animation events via bridge (D17b: bridge is sole path to renderer)
	if (ctx_.bridge) {
		bool isMeleeDamage = (damage_type < 80);

		// Don't animate dead entities
		auto target_entity_it = ctx_.entities.find(target_id);
		bool targetAlive = (target_entity_it != ctx_.entities.end() && target_entity_it->second.hp_percent > 0);

		if (isMeleeDamage && targetAlive) {
			// Calculate damage percentage for animation selection
			float damagePercent = 0.0f;
			if (target_id == ctx_.self.MySpawnId() && ctx_.game_state.player().maxHP() > 0) {
				damagePercent = (static_cast<float>(damage_amount) / static_cast<float>(ctx_.game_state.player().maxHP())) * 100.0f;
			} else if (target_entity_it != ctx_.entities.end()) {
				uint8_t level = target_entity_it->second.level > 0 ? target_entity_it->second.level : 1;
				float estimatedMaxHP = static_cast<float>(level) * 15.0f;
				damagePercent = (static_cast<float>(damage_amount) / estimatedMaxHP) * 100.0f;
			}

			eqt::state::CombatAnimationData data;
			data.sourceId = source_id;
			data.targetId = target_id;
			data.damageType = damage_type;
			data.damageAmount = damage_amount;
			data.damagePercent = static_cast<uint8_t>(damagePercent);
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::CombatAnimation, std::move(data)));

			if (EverQuest::GetDebugLevel() >= 2 || IsTrackedTarget(target_id) || IsTrackedTarget(source_id)) {
				LOG_DEBUG(MOD_COMBAT, "Queued melee anim: source={} target={} skill={} dmg={} pct={:.1f}%",
					source_id, target_id, damage_type, damage_amount, damagePercent);
			}
		} else if (damage_amount > 0 && targetAlive && !isMeleeDamage) {
			// Non-melee damage (spells, DoT, environmental) - play immediately
			float damagePercent = 0.0f;
			if (target_id == ctx_.self.MySpawnId() && ctx_.game_state.player().maxHP() > 0) {
				damagePercent = (static_cast<float>(damage_amount) / static_cast<float>(ctx_.game_state.player().maxHP())) * 100.0f;
			} else if (target_entity_it != ctx_.entities.end()) {
				uint8_t level = target_entity_it->second.level > 0 ? target_entity_it->second.level : 1;
				float estimatedMaxHP = static_cast<float>(level) * 15.0f;
				damagePercent = (static_cast<float>(damage_amount) / estimatedMaxHP) * 100.0f;
			}

			bool isDrowning = (damage_type == 253);
			bool isTrap = (damage_type == 254);
			const char* damageAnim = EQ::getDamageAnimation(damagePercent, isDrowning, isTrap);

			eqt::state::EntityAnimationEventData data;
			data.spawnId = target_id;
			data.animCode = 0;
			data.animName = damageAnim;
			data.loop = false;
			data.playThrough = true;
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::EntityAnimationEvent, std::move(data)));
			if (EverQuest::GetDebugLevel() >= 2 || IsTrackedTarget(target_id)) {
				LOG_DEBUG(MOD_COMBAT, "Damage reaction '{}' on target={} (dmg={}, pct={:.1f}%, type={})",
					damageAnim, target_id, damage_amount, damagePercent, damage_type);
			}
		}
	}

	// Check if target died
	if (damage_amount > 0 && target_id != 0) {
		auto it = ctx_.entities.find(target_id);
		if (it != ctx_.entities.end() && it->second.hp_percent == 0) {
			// Target died, might want to loot
			if (ctx_.combat_manager && ctx_.combat_manager->IsAutoAttackEnabled() &&
				target_id == ctx_.combat_manager->GetTargetId()) {
				// Our target died
				if (EverQuest::GetDebugLevel() >= 2 || IsTrackedTarget(target_id)) {
					LOG_DEBUG(MOD_COMBAT, "Target {} ({}) died", target_id, it->second.name);
				}
			}
		}
	}
}

// T382 (B172). OP_MoneyOnCorpse is how the server tells the client how much
// coin the corpse just handed over: eqemu's Corpse::MakeLootRequestPackets
// calls Client::AddMoneyToPP() and sends this packet in the SAME breath, so
// the coin is granted by OPENING the loot window, not by any later click
// ("Coins on the corpse are taken automatically by the first person to loot
// the corpse" -- the official client's own Looting Tip).
//
// MEASURED against the official client, T377 (review/T377/, B036's corpse
// row): the instant the loot window opens it plays BuyItem.WAV (UI sound 138,
// the same generic coin sound T328/T342 measured for every other coin
// movement) and prints, in the green system colour,
//
//     You receive 1 platinum, 8 gold, 3 silver and 4 copper from the corpse.
//
// -- eqstr 12072 "You receive %1 from the corpse." with the four counts
// substituted in. NOTE the phrase is spelled WITHOUT the Oxford comma the
// eqstr table's own list template (13108, "%1 platinum, %2 gold, %3 silver,
// and %4 copper") carries; the oracle's pixels are the golden here, so the
// list is built below to match what the client actually printed and 13108 is
// deliberately NOT used.
//
// Before this unit the handler parsed the four counts, logged them behind
// GetDebugLevel() >= 1 and dropped them on the floor.
void eqt::commands::CommandsComponent::ZoneProcessMoneyOnCorpse(const EQ::Net::Packet &p)
{
	// T382: the canonical struct (common/packets/combat.h, static_asserted at
	// 20 bytes against eqemu's moneyOnCorpseStruct) replaces the local
	// re-declaration that used to sit here. The old length check was the same
	// 22 bytes spelled as a literal.
	if (p.Length() < sizeof(EQT::MoneyOnCorpse_Struct) + 2) {
		return;
	}

	const auto* money = reinterpret_cast<const EQT::MoneyOnCorpse_Struct*>(
		static_cast<const char*>(p.Data()) + 2);

	// Copy packed struct fields to local variables for fmt::format
	uint32_t plat = money->platinum;
	uint32_t gold = money->gold;
	uint32_t silver = money->silver;
	uint32_t copper = money->copper;

	if (EverQuest::GetDebugLevel() >= 1) {
		LOG_DEBUG(MOD_MAIN, "MoneyOnCorpse: {} platinum, {} gold, {} silver, {} copper\n", plat, gold, silver, copper);
	}

	// Every packet that is NOT a coin grant carries four zeros: the six
	// SendLootReqErrorPacket responses (LootResponse::SomeoneElse / NotAtThisTime
	// / Hostiles / TooFar), a corpse with no money at all, and the autosplit
	// path (which zeroes the struct and pays the group through its own
	// OP_MoneyUpdate). So "all four zero" is the one guard that keeps the sound
	// and the chat line off every failure shape without this client having to
	// guess at eqemu's response enum, which is not on the wire in any form the
	// oracle was measured against.
	if (plat == 0 && gold == 0 && silver == 0 && copper == 0) {
		return;
	}

	// Bank it. PlayerState::setCurrency is the tree's ONE purse writer (the
	// OP_MoneyUpdate handler, the buy path and zone entry are its other
	// callers); addCurrency is a thin delta wrapper OVER it, not a second
	// writer -- see include/client/state/player_state.h.
	auto& player = ctx_.game_state.player();
	player.addCurrency(plat, gold, silver, copper);

	// Same projection OP_MoneyUpdate publishes after it writes the purse
	// (MerchantComponent::ZoneProcessMoneyUpdate) -- D18a: the bridge is the
	// sole path to the renderer, so the coin counters on screen move only if
	// this event is pushed.
	if (ctx_.bridge) {
		eqt::state::CurrencyChangedData data;
		data.platinum = static_cast<int32_t>(player.platinum());
		data.gold = static_cast<int32_t>(player.gold());
		data.silver = static_cast<int32_t>(player.silver());
		data.copper = static_cast<int32_t>(player.copper());
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::CurrencyChanged, std::move(data)));
	}

#ifdef WITH_AUDIO
	// T328 Leg B's seam: one intent, one consumer arm, forwarded to
	// BridgeComponent::PlaySound().
	if (ctx_.bridge) {
		ctx_.bridge->pushIntent(eqt::events::PlaySoundIntent{
			EQT::Audio::UISoundId::BUY_ITEM});
	}
#endif

	// The four counts are printed even when one of them is zero. T377's single
	// capture had all four non-zero, so what the official client does with a
	// zero denomination -- drop that term, or print "0 silver" -- is UNMEASURED;
	// printing all four is what the one measured line shows. If a later live
	// unit sees a corpse with, say, no silver, this is the line to correct.
	std::string counts = fmt::format("{} platinum, {} gold, {} silver and {} copper",
		plat, gold, silver, copper);
	ctx_.out.System(ctx_.strings.GetFormattedStringMessage(12072, {counts}));
}

// T247. OP_Stun: eqemu Stun_Struct{uint32 duration} in ms, sent only to the
// stunned client (Client::Stun / Client::UnStun with 0). A shorter stun does
// not cut a longer one short (Mob::Stun's own rule); 0 clears. Only casting
// consults this (SpellManager::checkPlayerState); the server still rejects
// anything else a stunned client tries.
void eqt::commands::CommandsComponent::ZoneProcessStun(uint32_t duration_ms)
{
	auto now = std::chrono::steady_clock::now();
	if (duration_ms == 0) {
		if (IsStunned()) {
			ctx_.out.System(ctx_.strings.GetStringMessage(12480));  // You are no longer stunned.
		}
		m_stunned_until = {};
		// B1183 (#502) / B1199 (#518): the stun-end message's direct
		// write of 100 (standing) on the local player (eqgame.exe.c:
		// 80326-80340, the 0x210b case: 100 at 80336, the stun-flag
		// clear at 80337). The feed goes through the machine's stun
		// entry point (onStunFeed).
		{
			auto self = ctx_.entities.find(ctx_.self.MySpawnId());
			if (self != ctx_.entities.end()) {
				self->second.stand_state.onStunFeed(eqt::state::StunFeed::StunEnd0x210b);
			}
		}
		LOG_INFO(MOD_COMBAT, "OP_Stun: unstunned");
		return;
	}
	auto until = now + std::chrono::milliseconds(duration_ms);
	bool was_stunned = IsStunned();
	if (until > m_stunned_until) {
		m_stunned_until = until;
	}
	if (!was_stunned) {
		ctx_.out.System(ctx_.strings.GetStringMessage(12479));  // You are stunned!
		// B1199 (#518): this feed is the 0x1e51 OP_Stun opcode, whose
		// client case (eqgame.exe.c:79576-79581: `if (DAT_00905d00 != 0)
		// FUN_00417839();`) writes NO +0x251 -- the pose byte holds.
		// The client's 102 (0x66, frozen) writers are the 0x25aa case
		// (80377) and the 0x3013 case (80459), which fable does not
		// process; onStunFeed keeps them in the machine so its
		// vocabulary is the client's, byte for byte. The 102 write the
		// B1183 port had here was the measured fable/oracle difference
		// (B1183 legF: oracle 100 vs fable 102 on GM #stun) and is
		// removed: OpStun0x1e51 is the documented no-op.
		{
			auto self = ctx_.entities.find(ctx_.self.MySpawnId());
			if (self != ctx_.entities.end()) {
				self->second.stand_state.onStunFeed(eqt::state::StunFeed::OpStun0x1e51);
			}
		}
	}
	LOG_INFO(MOD_COMBAT, "OP_Stun: stunned for {} ms", duration_ms);
}

bool eqt::commands::CommandsComponent::IsStunned() const
{
	return m_stunned_until != std::chrono::steady_clock::time_point{} &&
	       std::chrono::steady_clock::now() < m_stunned_until;
}
