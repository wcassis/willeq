// B898 (GitLab #214) -- the trainer session's two game-thread bodies the
// trainer WINDOW needs, in their own TU (M00):
//
//   PublishTrainerWindow()  the TrainerWindowOpened event, built from the
//                           caps the server's OP_GMTraining reply carried.
//                           Moved verbatim out of ZoneProcessGMTraining
//                           (merchant_vendor_bank.cpp) so the OP_SkillUpdate
//                           arm (zone_dispatch.cpp) can re-publish it: on
//                           Titanium that packet is the ONLY thing the server
//                           sends back after a train (commands_train.cpp's
//                           header has the wire truth), so it is the only
//                           honest trigger for the window's new value,
//                           practice count and price.
//   TrainRefusal()          the local refusals /train <skill|id> already
//                           made (T280), shared with the window's Train
//                           button so a click and a command are refused --
//                           or sent -- identically.

#include "client/merchant/merchant_component.h"

#include "client/eq.h"
#include "client/bridge/game_state_bridge.h"
#include "client/display_eqstr.h"
#include "client/string_database.h"
#include "client/skill/skill_constants.h"
#include "client/skill/skill_manager.h"
#include "common/logging.h"
#include "common/packet_structs.h"

#include <string>

namespace {

// UIGAP-residue (#214): the trainer window's rows and its two client-side
// lines name a skill the way the OFFICIAL client does -- "1H Slashing" in the
// measured run2 chat and list, where EQ::getSkillName() (fable's own short
// table, read by the skills window, /train name matching and the item
// display) says "1H Slash". Of its 75 names exactly three differ from the
// official skill-name rows eqstr 13850-13924, so those three are read from
// that table; every other id keeps getSkillName(). A table miss falls back.
std::string officialSkillName(const EQT::StringDatabase& strings, uint8_t skillId) {
	uint32_t eqstrId = 0;
	switch (skillId) {
		case 1:  eqstrId = 13856; break;   // "1H Slashing" (fable: "1H Slash")
		case 3:  eqstrId = 13858; break;   // "2H Slashing" (fable: "2H Slash")
		case 48: eqstrId = 13915; break;   // "Pick Pockets" (fable: "Pick Pocket")
		default: break;
	}
	if (eqstrId != 0) {
		const std::string s = strings.getString(eqstrId);
		if (!s.empty()) {
			return s;
		}
	}
	const char* name = EQ::getSkillName(skillId);
	return name ? std::string(name) : fmt::format("skill {}", skillId);
}

}  // namespace

void eqt::merchant::MerchantComponent::PublishTrainerWindow()
{
	if (ctx_.game_state.player().trainerNpcId() == 0) {
		return;
	}

	eqt::state::TrainerWindowOpenedData trainerData;
	trainerData.npcId = ctx_.game_state.player().trainerNpcId();
	trainerData.npcName = ctx_.game_state.player().trainerName();
	trainerData.platinum = ctx_.self.GetPlatinum();
	trainerData.gold = ctx_.self.GetGold();
	trainerData.silver = ctx_.self.GetSilver();
	trainerData.copper = ctx_.self.GetCopper();
	trainerData.practicePoints = ctx_.self.GetPracticePoints();

	for (uint8_t skill_id = 0; skill_id < m_trainer_skill_caps.size(); ++skill_id) {
		uint32_t max_trainable = m_trainer_skill_caps[skill_id];

		// Skip skills with max_trainable = 0 (not trainable at this trainer)
		if (max_trainable == 0) {
			continue;
		}

		// Get current skill value
		uint32_t current_value = 0;
		if (ctx_.skill_manager) {
			current_value = ctx_.skill_manager->getSkillValue(skill_id);
		}

		// UIGAP-residue (#214, MEASURED train.tsv row 13): a skill AT its cap
		// is still LISTED (1H Blunt 125 at L20, Rank "Master", its price still
		// shown) and selectable; Train then refuses it client-side
		// (TrainRefusal's eqstr 12930). B898 skipped it here.

		// Get skill name
		const std::string skill_name = officialSkillName(ctx_.strings.Db(), skill_id);

		// Training cost, in COPPER. T280 corrected this: the old
		// "(current_value + 1) * 10" was invented, and disagreed with the
		// server at every skill value. eqemu's Client::OPGMTrainSkill
		// (zone/client_process.cpp) is the authority and computes
		//     AdjustedSkillLevel = skilllevel - 10;
		//     if (AdjustedSkillLevel > 0)
		//         Cost = AdjustedSkillLevel^3 / 100;
		// off the CURRENT (pre-train) value -- so the first ten points of
		// any skill are free, and the price then grows cubically. Cost is
		// what the server DEDUCTS; it is not sent back on Titanium (see
		// TrainingCost()), so this figure is the only one the player ever
		// sees and it had better match.
		uint32_t cost = TrainingCost(current_value);

		eqt::state::TrainerSkillEntryData skill;
		skill.skillId = skill_id;
		skill.name = skill_name;
		skill.currentValue = current_value;
		skill.maxTrainable = max_trainable;
		skill.cost = cost;
		trainerData.skills.push_back(std::move(skill));

		if (EverQuest::GetDebugLevel() >= 2) {
			LOG_DEBUG(MOD_MAIN, "  Skill {}: {} cur={} max={} cost={}",
				skill_id, skill_name, current_value, max_trainable, cost);
		}
	}

	LOG_DEBUG(MOD_MAIN, "TrainerWindowOpened for {} (id={}): {} trainable skills, {} practices",
		trainerData.npcName, trainerData.npcId, trainerData.skills.size(),
		trainerData.practicePoints);

	if (ctx_.bridge) {
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::TrainerWindowOpened, std::move(trainerData)));
	}
}

std::string eqt::merchant::MerchantComponent::TrainRefusal(uint8_t skillId) const
{
	if (ctx_.game_state.player().trainerNpcId() == 0) {
		return "You are not interacting with a trainer. Target your guildmaster and use /train.";
	}

	const std::string label = officialSkillName(ctx_.strings.Db(), skillId);

	// eqemu's very first line is `if(!m_pp.points) return;` -- silent.
	if (ctx_.self.GetPracticePoints() == 0) {
		return "You have no training points left to spend.";
	}

	const uint32_t cap = GetTrainerSkillCap(skillId);
	if (cap == 0) {
		return fmt::format("{} cannot train you in {}.",
			ctx_.game_state.player().trainerName(), label);
	}

	uint32_t current = 0;
	if (ctx_.skill_manager) {
		current = ctx_.skill_manager->getSkillValue(skillId);
	}
	if (current >= cap) {
		// UIGAP-residue (#214, MEASURED train.tsv row 13): at cap the official
		// client keeps Train ENABLED, sends NO packet and prints, client-side,
		// eqstr 12930 "<trainer> tells you, 'You are already trained to the
		// limit of your abilities in <skill>.'" -- this line, not the server's
		// MORE_SKILLED_THAN_I. The cap here is the trainer's MAX-LEVEL cap
		// (Client::OPGMTraining fills skills[] from MaxSkill at
		// RuleI(Character, MaxLevel)), so this is never stricter than the
		// server. The official client also knows the cap at the CURRENT level
		// (the measured case: 1H Blunt 125 at L20, server cap 310); fable has
		// no per-level cap table (config/skill_caps.json is collapsed to the
		// lifetime cap), so below the max-level cap the request still goes out
		// and the server answers -- flagged in the UIGAP-residue report.
		const std::string line = eqt::EqStrLine(ctx_.strings.Db(), 12930,
			{ctx_.game_state.player().trainerName(), label});
		if (!line.empty()) {
			return line;
		}
		return fmt::format(
			"You are already more skilled in {} than {} can teach ({} of {}).",
			label, ctx_.game_state.player().trainerName(), current, cap);
	}
	return std::string();
}

std::string eqt::merchant::MerchantComponent::TrainerSelectLine(uint8_t skillId) const
{
	if (ctx_.game_state.player().trainerNpcId() == 0) {
		return std::string();
	}
	const std::string label = officialSkillName(ctx_.strings.Db(), skillId);
	// eqstr 12056 "%1 says, 'Interested in training in %2 ?'" -- the space
	// before the '?' is the string's own (and the measured chat line's).
	return eqt::EqStrLine(ctx_.strings.Db(), 12056,
		{ctx_.game_state.player().trainerName(), label});
}
