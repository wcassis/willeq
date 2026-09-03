// T280 -- /train <skill|id|list> and /trainclose  (T264 F8, the last of the
// eight blocked intents).
//
// WIRE TRUTH, read out of eqemu rather than assumed (the T273/T277 lesson):
//
//   * Titanium maps three trainer opcodes and NOT a fourth:
//     utils/patches/patch_Titanium.conf
//         OP_GMTraining=0x238f   OP_GMEndTraining=0x613d
//         OP_GMTrainSkill=0x11d2
//         # OP_GMEndTrainingResponse=0x0000   <- COMMENTED OUT
//     There is no OP_GMTrainSkillConfirm line at all. So on Titanium the
//     server's OPGMEndTraining reply and its train CONFIRM both go nowhere.
//
//   * The one that trains is OP_GMTrainSkill carrying GMSkillChange_Struct
//     (12 bytes): npcid is a **uint16**, not a uint32 -- eqemu's own
//     eq_packet_structs.h declares `uint16 npcid` with MISLEADING /*002*/
//     offset comments on every field. Layout is npcid@0(u16),
//     unknown1@2(2), skillbank@4(u16), unknown2@6(2), skill_id@8(u16),
//     unknown3@10(2). skillbank is 0 for skills, 1 for languages.
//     Handle_OP_GMTrainSkill hard-gates on size == 12.
//
//   * What the server validates (Client::OPGMTrainSkill,
//     zone/client_process.cpp), in order, EVERY failure silent:
//       1. m_pp.points != 0                (a training point to spend)
//       2. npcid resolves to an NPC whose class is 20..35 (WarriorGM..
//          BerserkerGM)
//       3. trainerClass - 19 == your class, unless
//          RuleB(Character, AllowCrossClassTrainers) (default false)
//       4. DistanceSquared <= USE_NPC_RANGE2 == 40000, i.e. 200 units
//          (common/features.h). T50 already established this one.
//       5. skill_id <= HIGHEST_SKILL, CanHaveSkill(), MaxSkill() != 0
//       6. not already at MaxSkill / the tradeskill / research /
//          specialization sub-caps  (these DO answer, with
//          MORE_SKILLED_THAN_I)
//     then SetSkill(skill, value + 1), TakeMoneyFromPP(Cost), m_pp.points--.
//
//   * WHAT COMES BACK ON TITANIUM IS ONE PACKET: SetSkill() queues
//     OP_SkillUpdate (skillId u32, value u32). That is all.
//       - The GMTrainSkillConfirm packet that carries the cost and the
//         "you have learned the basics" message is gated on
//         `ClientVersion() >= SoF`, so Titanium never sees it.
//       - TakeMoneyFromPP(Cost) is called with update_client defaulting to
//         **false** (zone/client.h:899), and its SendMoneyUpdate() calls are
//         all inside `if (update_client)`. So the coin IS deducted and
//         saved server-side and the client is NEVER told. The trainer
//         window's money figure goes stale until the next zone/relog. That
//         is the protocol, not a defect here, and it is the reason nothing
//         below writes money locally: there is no reply to write from, and
//         guessing is exactly the T272/T277 optimistic-local-write mistake.
//       - The training-point decrement is likewise not sent; eq.cpp's
//         OP_SkillUpdate arm already mirrors it while a trainer session is
//         open, which is the closest honest approximation available.
//
//   * COST is (current - 10)^3 / 100 copper, integer, off the PRE-train
//     value; free below 11. See EverQuest::TrainingCost() (eq_merchant.cpp).
//
// So: this file refuses only what the client can actually know from the
// session the server itself opened -- no session, no training points, an
// unknown or ambiguous skill name, a skill this trainer does not teach, a
// skill already at this trainer's cap -- and pushes TrainSkillIntent for
// everything else. It writes NO skill value, NO money, NO point count.

#include "client/eq.h"
#include "client/commands/commands_component.h"
#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/skill/skill_constants.h"
#include "client/skill/skill_manager.h"
#include "common/logging.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace {

std::string ToLower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

std::string Trim(const std::string& s)
{
	size_t b = s.find_first_not_of(" \t");
	if (b == std::string::npos) return "";
	size_t e = s.find_last_not_of(" \t");
	return s.substr(b, e - b + 1);
}

bool IsAllDigits(const std::string& s)
{
	if (s.empty()) return false;
	for (unsigned char c : s) {
		if (!std::isdigit(c)) return false;
	}
	return true;
}

}  // namespace

void eqt::commands::CommandsComponent::RequestCloseTrainer()
{
	if (ctx_.game_state.player().trainerNpcId() == 0) {
		owner_.AddChatSystemMessage("You are not interacting with a trainer.");
		return;
	}
	if (!ctx_.bridge) {
		owner_.AddChatSystemMessage("Cannot close the trainer window - no bridge attached.");
		return;
	}
	// The same intent a trainer-panel close button would push. There is no
	// trainer panel yet (no hit-test anywhere in graphics/ui), so the
	// command is the only producer today -- stated plainly, T277-style,
	// rather than pretending a gesture exists.
	ctx_.bridge->pushIntent(eqt::events::CloseTrainerIntent{});
}

void eqt::commands::CommandsComponent::RequestTrainSkill(const std::string& args)
{
	const std::string arg = Trim(args);

	if (ctx_.game_state.player().trainerNpcId() == 0) {
		owner_.AddChatSystemMessage(
			"You are not interacting with a trainer. Target your guildmaster and use /train.");
		return;
	}

	if (arg.empty()) {
		owner_.AddChatSystemMessage("Usage: /train <skill name> | /train <skill id> | /train list");
		return;
	}

	const std::string lowered = ToLower(arg);

	// `list` is not a skill name (no EQ skill contains it), so it is safe as
	// a sub-word: it prints what THIS session can actually train, which is
	// the only way a player finds out without a panel.
	if (lowered == "list") {
		ListTrainableSkills();
		return;
	}

	// --- resolve the argument to a skill id -------------------------------
	int resolved = -1;
	if (IsAllDigits(arg)) {
		const long v = std::stol(arg);
		if (v < 0 || v >= static_cast<long>(EQ::MAX_SKILL_ID)) {
			owner_.AddChatSystemMessage(fmt::format(
				"There is no skill {} (skill ids are 0-{}).", arg, EQ::MAX_SKILL_ID - 1));
			return;
		}
		resolved = static_cast<int>(v);
	} else {
		std::vector<int> exact;
		std::vector<int> partial;
		for (int id = 0; id < static_cast<int>(EQ::MAX_SKILL_ID); ++id) {
			const char* name = EQ::getSkillName(static_cast<uint8_t>(id));
			if (!name) continue;
			const std::string ln = ToLower(name);
			if (ln == lowered) {
				exact.push_back(id);
			} else if (ln.find(lowered) != std::string::npos) {
				partial.push_back(id);
			}
		}
		const std::vector<int>& hits = !exact.empty() ? exact : partial;
		if (hits.empty()) {
			owner_.AddChatSystemMessage(fmt::format("You know of no skill called \"{}\".", arg));
			return;
		}
		if (hits.size() > 1) {
			std::string names;
			for (size_t i = 0; i < hits.size(); ++i) {
				if (i) names += ", ";
				names += EQ::getSkillName(static_cast<uint8_t>(hits[i]));
			}
			owner_.AddChatSystemMessage(fmt::format("\"{}\" matches {} skills: {}", arg, hits.size(), names));
			return;
		}
		resolved = hits.front();
	}

	const uint8_t skillId = static_cast<uint8_t>(resolved);
	const char* skillName = EQ::getSkillName(skillId);
	const std::string label = skillName ? skillName : fmt::format("skill {}", skillId);

	// --- refusals the client can actually make ----------------------------

	// eqemu's very first line is `if(!m_pp.points) return;` -- silent.
	if (owner_.GetPracticePoints() == 0) {
		owner_.AddChatSystemMessage("You have no training points left to spend.");
		return;
	}

	const uint32_t cap = owner_.Merchant().GetTrainerSkillCap(skillId);
	if (cap == 0) {
		owner_.AddChatSystemMessage(fmt::format("{} cannot train you in {}.", ctx_.game_state.player().trainerName(), label));
		return;
	}

	uint32_t current = 0;
	if (ctx_.skill_manager) {
		current = ctx_.skill_manager->getSkillValue(skillId);
	}
	if (current >= cap) {
		// The server's MORE_SKILLED_THAN_I branch, said up front.
		owner_.AddChatSystemMessage(fmt::format(
			"You are already more skilled in {} than {} can teach ({} of {}).",
			label, ctx_.game_state.player().trainerName(), current, cap));
		return;
	}

	if (!ctx_.bridge) {
		owner_.AddChatSystemMessage("Cannot train - no bridge attached.");
		return;
	}

	const uint32_t cost = eqt::merchant::MerchantComponent::TrainingCost(current);
	if (cost > 0) {
		owner_.AddChatSystemMessage(fmt::format("Training {} ({} -> {}) for {}.",
			label, current, current + 1, EverQuest::FormatPrice(static_cast<int32_t>(cost))));
	} else {
		owner_.AddChatSystemMessage(fmt::format("Training {} ({} -> {}).", label, current, current + 1));
	}

	LOG_DEBUG(MOD_MAIN, "T280 /train: skill {} ({}) cur={} cap={} cost={}c trainer={}",
		skillId, label, current, cap, cost, ctx_.game_state.player().trainerNpcId());

	// Nothing local changes here. The new value arrives as OP_SkillUpdate.
	ctx_.bridge->pushIntent(eqt::events::TrainSkillIntent{skillId});
}

void eqt::commands::CommandsComponent::ListTrainableSkills()
{
	if (ctx_.game_state.player().trainerNpcId() == 0) {
		owner_.AddChatSystemMessage("You are not interacting with a trainer.");
		return;
	}

	owner_.AddChatSystemMessage(fmt::format("=== {} can train ({} training points) ===",
		ctx_.game_state.player().trainerName(), owner_.GetPracticePoints()));

	size_t shown = 0;
	for (uint8_t id = 0; id < EQ::MAX_SKILL_ID; ++id) {
		const uint32_t cap = owner_.Merchant().GetTrainerSkillCap(id);
		if (cap == 0) continue;
		uint32_t current = 0;
		if (ctx_.skill_manager) {
			current = ctx_.skill_manager->getSkillValue(id);
		}
		if (current >= cap) continue;
		const char* name = EQ::getSkillName(id);
		owner_.AddChatSystemMessage(fmt::format("[{}] {} {}/{} - {}",
			id, name ? name : "Unknown", current, cap,
			EverQuest::FormatPrice(static_cast<int32_t>(eqt::merchant::MerchantComponent::TrainingCost(current)))));
		++shown;
	}

	if (shown == 0) {
		owner_.AddChatSystemMessage("Nothing here is trainable for you right now.");
	}
}
