// B888 (GitLab #204) -- character creation and deletion at character select,
// LoginWorldComponent's half. Its own TU (M00: new code in new TUs). The
// packet shapes are char_create_packets.h; the script grammar is
// world_script.h; the flow's server citations are inline.
//
// The flow, as eqemu's Titanium world runs it (world/client.cpp):
//   OP_ApproveName(name, race, class, deity) -> 1 byte: 1 approved (the name
//     is now RESERVED for this account, char_name kept), 0 refused (:567-634)
//   OP_CharacterCreate(CharCreate_Struct) -> OP_SendCharInfo listing the new
//     character on success; on failure a 1-byte OP_ApproveName 0 and the
//     reservation deleted (:746-775). Create uses the name from the APPROVAL,
//     so an approval always precedes it in the same session.
//   OP_DeleteCharacter(name) -> OP_SendCharInfo (:1072-1082); an unknown or
//     foreign name is ignored without a reply.

#include "client/eq.h"
#include "client/login_world/login_world_component.h"

#include "client/fatal_exit.h"
#include "client/login_world/char_create_packets.h"
#include "common/logging.h"

#include <iostream>

namespace eqt {
namespace login_world {

using charcreate::CharCreateRequest;
using charcreate::WorldScript;
using Stage = LoginWorldComponent::CharCreateStatus::Stage;
using Result = LoginWorldComponent::CharCreateStatus::Result;

namespace {
// eqstr_us.txt, verbatim.
constexpr uint32_t kEqstrNameNotValid = 1576;       // "Name is not valid."
constexpr uint32_t kEqstrSubmitting = 1580;         // "Submitting name to server for approval. Please Wait."
constexpr uint32_t kEqstrNameRejected = 1581;       // "Name has been rejected. Please choose another."
}  // namespace

bool LoginWorldComponent::CharacterListHas(const std::string& name) const {
	for (const auto& e : m_character_select_list) {
		if (e.name == name) return true;
	}
	return false;
}

bool LoginWorldComponent::RequestCreateCharacter(const CharCreateRequest& req) {
	if (m_create_status.stage != Stage::Idle) {
		LOG_WARN(MOD_WORLD, "[CHARCREATE] a request is already in flight; '{}' not sent", req.name);
		return false;
	}
	if (charcreate::validateCharacterName(req.name) != charcreate::NameCheck::kOk) {
		m_create_status.last = Result::NameRefused;
		m_create_status.lastMessage = ctx_.strings.GetStringMessage(kEqstrNameNotValid);
		LOG_WARN(MOD_WORLD, "[CHARCREATE] '{}' refused before sending: {}", req.name,
		         charcreate::nameCheckText(charcreate::validateCharacterName(req.name)));
		return false;
	}
	if (!charcreate::checkCharCreateStats(req.race, req.cls, req.stats)) {
		// The allocator cannot build this; only a hand-built request can.
		m_create_status.last = Result::CreateRefused;
		m_create_status.lastMessage = "stats fail the server's CheckCharCreateInfoTitanium";
		LOG_WARN(MOD_WORLD, "[CHARCREATE] '{}' refused before sending: {}", req.name,
		         m_create_status.lastMessage);
		return false;
	}
	m_create_status.stage = Stage::AwaitingNameApproval;
	m_create_status.pending = req;
	m_create_status.last = Result::None;
	m_create_status.lastMessage = ctx_.strings.GetStringMessage(kEqstrSubmitting);
	auto p = charcreate::BuildApproveName(req.name, req.race, req.cls, req.deity);
	LOG_INFO(MOD_WORLD, "[CHARCREATE] OP_ApproveName name={} race={} class={} deity={}",
	         req.name, req.race, req.cls, req.deity);
	eqt::strings::DumpPacket("C->S", HC_OP_ApproveName, p);
	ctx_.net.QueueWorld(p);
	return true;
}

bool LoginWorldComponent::RequestRandomName(uint32_t race, uint32_t gender) {
	if (m_create_status.stage != Stage::Idle) return false;
	m_create_status.stage = Stage::AwaitingRandomName;
	auto p = charcreate::BuildRandomName(race, gender);
	eqt::strings::DumpPacket("C->S", HC_OP_RandomNameGenerator, p);
	ctx_.net.QueueWorld(p);
	return true;
}

bool LoginWorldComponent::RequestDeleteCharacter(const std::string& name) {
	if (m_create_status.stage != Stage::Idle) return false;
	if (!CharacterListHas(name)) {
		m_create_status.last = Result::DeleteFailed;
		m_create_status.lastMessage = name + " is not one of this account's characters";
		LOG_WARN(MOD_WORLD, "[CHARCREATE] delete '{}': not on the character list", name);
		return false;
	}
	m_create_status.stage = Stage::AwaitingDelete;
	m_create_status.pendingDelete = name;
	auto p = charcreate::BuildDeleteCharacter(name);
	LOG_INFO(MOD_WORLD, "[CHARCREATE] OP_DeleteCharacter name={}", name);
	eqt::strings::DumpPacket("C->S", HC_OP_DeleteCharacter, p);
	ctx_.net.QueueWorld(p);
	return true;
}

void LoginWorldComponent::WorldProcessApproveName(const EQ::Net::Packet& p) {
	const int verdict = p.Length() > 2
		? charcreate::ParseApproveNameReply(static_cast<const uint8_t*>(p.Data()) + 2, p.Length() - 2)
		: -1;
	const std::string name = m_create_status.pending.name;
	if (m_create_status.stage == Stage::AwaitingNameApproval) {
		if (verdict == 1) {
			m_create_status.stage = Stage::AwaitingCreate;
			auto out = charcreate::BuildCharacterCreate(m_create_status.pending);
			LOG_INFO(MOD_WORLD, "[CHARCREATE] name '{}' approved; OP_CharacterCreate start_zone={} "
			         "stats={}/{}/{}/{}/{}/{}/{} tutorial=0", name, m_create_status.pending.startZone,
			         m_create_status.pending.stats[0], m_create_status.pending.stats[1],
			         m_create_status.pending.stats[2], m_create_status.pending.stats[3],
			         m_create_status.pending.stats[4], m_create_status.pending.stats[5],
			         m_create_status.pending.stats[6]);
			eqt::strings::DumpPacket("C->S", HC_OP_CharacterCreate, out);
			ctx_.net.QueueWorld(out);
			return;
		}
		m_create_status.stage = Stage::Idle;
		m_create_status.last = Result::NameRefused;
		// MEASURED (review/UIGAP-ORACLE/204/12_name_rejected_dialog.png, a
		// taken name): the official modal is eqstr 1581, not 5975.
		m_create_status.lastMessage = ctx_.strings.GetStringMessage(kEqstrNameRejected);
		LOG_WARN(MOD_WORLD, "[CHARCREATE] name '{}' refused by the server: {}", name,
		         m_create_status.lastMessage);
		FinishWorldScript(false, "the server refused the name '" + name + "'");
		return;
	}
	if (m_create_status.stage == Stage::AwaitingCreate) {
		// A 0 here is world's OPCharCreate failure (:760-765); a 1 is not sent.
		// Which modal the official client shows for it is not measured; the
		// name refusal's 1581 is used (assumption awaiting oracle).
		m_create_status.stage = Stage::Idle;
		m_create_status.last = Result::CreateRefused;
		m_create_status.lastMessage = ctx_.strings.GetStringMessage(kEqstrNameRejected);
		LOG_WARN(MOD_WORLD, "[CHARCREATE] the server refused to create '{}' (OP_ApproveName {})",
		         name, verdict);
		FinishWorldScript(false, "the server refused to create '" + name + "'");
		return;
	}
	LOG_DEBUG(MOD_WORLD, "[CHARCREATE] OP_ApproveName {} with nothing pending", verdict);
}

void LoginWorldComponent::WorldProcessRandomName(const EQ::Net::Packet& p) {
	const std::string name = p.Length() > 2
		? charcreate::ParseRandomNameReply(static_cast<const uint8_t*>(p.Data()) + 2, p.Length() - 2)
		: std::string();
	if (m_create_status.stage == Stage::AwaitingRandomName) m_create_status.stage = Stage::Idle;
	m_create_status.randomName = name;
	LOG_INFO(MOD_WORLD, "[CHARCREATE] Get Name -> '{}'", name);
}

void LoginWorldComponent::OnCharacterListReceived() {
	if (m_create_status.stage == Stage::AwaitingCreate) {
		const std::string name = m_create_status.pending.name;
		if (CharacterListHas(name)) {
			m_create_status.stage = Stage::Idle;
			m_create_status.last = Result::Created;
			m_create_status.lastMessage.clear();
			m_created_this_session = true;
			LOG_INFO(MOD_WORLD, "[CHARCREATE] created '{}'", name);
			// MEASURED (review/UIGAP-ORACLE/204/14_after_create_entered_world_qeytoqrg.png):
			// after a successful create the official client goes STRAIGHT
			// into the world with the new character -- it does not return to
			// character select. A screen-driven create does the same.
			EnterWorldAfterCreate(name);
		}
		return;
	}
	if (m_create_status.stage == Stage::AwaitingDelete) {
		const std::string name = m_create_status.pendingDelete;
		m_create_status.stage = Stage::Idle;
		const bool gone = !CharacterListHas(name);
		m_create_status.last = gone ? Result::Deleted : Result::DeleteFailed;
		LOG_INFO(MOD_WORLD, "[CHARCREATE] delete '{}': {}", name, gone ? "gone" : "STILL LISTED");
		if (m_world_script.kind == WorldScript::Kind::Delete) {
			if (gone) std::cout << "DELETED " << name << std::endl;
			FinishWorldScript(gone, gone ? std::string() : name + " is still listed after the delete");
		}
		return;
	}
	if (m_world_script.active() && !m_world_script_started) {
		m_world_script_started = true;
		RunWorldScript();
	}
}

void LoginWorldComponent::RunWorldScript() {
	switch (m_world_script.kind) {
	case WorldScript::Kind::List:
		for (const auto& e : m_character_select_list) {
			std::cout << "CHARACTER slot=" << e.slot << " name=" << e.name
			          << " level=" << static_cast<int>(e.level) << " class=" << static_cast<int>(e.char_class)
			          << " race=" << e.race << " zone=" << e.zone << std::endl;
		}
		std::cout << "CHARACTERS " << m_character_select_list.size() << std::endl;
		FinishWorldScript(true, std::string());
		break;
	case WorldScript::Kind::Delete:
		if (!RequestDeleteCharacter(m_world_script.name)) {
			FinishWorldScript(false, m_world_script.name + " is not one of this account's characters");
		}
		break;
	case WorldScript::Kind::Create:
		if (CharacterListHas(m_world_script.create.name)) {
			FinishWorldScript(false, m_world_script.create.name + " already exists on this account");
		} else if (!RequestCreateCharacter(m_world_script.create)) {
			FinishWorldScript(false, "'" + m_world_script.create.name + "' refused before sending");
		}
		break;
	case WorldScript::Kind::None:
		break;
	}
}

void LoginWorldComponent::FinishWorldScript(bool ok, const std::string& reason) {
	if (!m_world_script.active()) return;
	const auto kind = m_world_script.kind;
	m_world_script.kind = WorldScript::Kind::None;
	if (!ok) {
		LOG_ERROR(MOD_WORLD, "[CHARCREATE] FAILED: {}", reason);
		std::cerr << "[CHARCREATE] FAILED: " << reason << std::endl;
		eqt::RecordFatalExit("character script failed: " + reason);
		if (ctx_.lifecycle.RequestQuit) ctx_.lifecycle.RequestQuit();
		return;
	}
	// List and Delete end the run here; Create ends by entering world.
	if (kind != WorldScript::Kind::Create && ctx_.lifecycle.RequestQuit) {
		ctx_.lifecycle.RequestQuit();
	}
}

void LoginWorldComponent::EnterWorldAfterCreate(const std::string& name) {
	// The configured character IS the new one from here on: the zoning world
	// pass (WorldProcessPostEnterWorld) and every per-character file key off it.
	ctx_.character = name;
	for (const auto& e : m_character_select_list) {
		if (e.name == name) m_character_select_index = e.slot;
	}
	if (m_created_this_session) {
		// Clears world's StartInTutorial (see the member's comment): the
		// character goes to its home city, never zone 189.
		WorldSendWorldClientCRC();
	}
	std::cout << "CREATED " << name << std::endl;
	FinishWorldScript(true, std::string());
	WorldSendEnterWorld(name);
}

}  // namespace login_world
}  // namespace eqt
