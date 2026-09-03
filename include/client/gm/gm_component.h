#pragma once

// Batch K (refactor/K_component_extraction.md), unit K02.
//
// The gm family, lifted out of class EverQuest, following K01's
// eqt::guild::GuildComponent shape. Every body in
// src/client/gm/gm_component.cpp is the body that used to live in
// src/client/eq_gm.cpp, moved verbatim; the only edits inside a body are
// the substitutions §Mechanism 4 allows (m_x -> ctx_.x / owner_.m_x,
// Foo() -> owner_.Foo() for a method that stayed on EverQuest).
//
// `owner_` is the TRANSITION handle: the moved code still reaches EverQuest
// members and methods that have not been extracted yet (PosX()/PosY()/PosZ(),
// MySpawnId(), AddChatSystemMessage()). EverQuest declares
// `friend class eqt::gm::GmComponent` for exactly that. K-final removes
// both the handle and the friendship.

#include "client/client_context.h"

#include <cstdint>
#include <string>
#include <vector>

class EverQuest;

namespace EQ {
namespace Net {
class Packet;
}
}

namespace eqt {
namespace gm {

class GmComponent {
public:
	GmComponent(ClientContext& ctx, EverQuest& owner) : ctx_(ctx), owner_(owner) {}

	// Senders. Were public on EverQuest; K02 deleted those declarations and
	// retargeted the callers (eq_action_handler_extended.cpp, tests).
	void SendGMZoneRequest(const std::string& charname, uint16_t zone_id);
	void SendGMSummon(const std::string& charname);
	void SendGMGoto(const std::string& charname);
	void SendGMFind(const std::string& charname);
	void SendGMKick(const std::string& charname);
	void SendGMKill(const std::string& charname);
	void SendGMHideMe(bool hide);
	void SendGMEmoteZone(const std::string& text);
	void SendGMLastName(const std::string& charname, const std::string& lastname);

	// Packet handlers. Were private on EverQuest; DispatchZonePacket
	// (eq_zone_dispatch.cpp) now calls m_gm->ZoneProcessGM*() directly.
	void ZoneProcessGMZoneRequest(const EQ::Net::Packet& p);
	void ZoneProcessGMFind(const EQ::Net::Packet& p);
	void ZoneProcessGMSummon(const EQ::Net::Packet& p);

	// T217 test seam: the complete OP_GMHideMe packet (opcode + body) the
	// last SendGMHideMe() call built, captured before the connection gate.
	// Raw bytes rather than decoded fields so the test pins the wire layout
	// eqemu's Handle_OP_GMHideMe size-checks, not a re-derivation of it.
	struct TestSentGMHideMe {
		bool sent = false;
		std::vector<uint8_t> bytes;
	};
	const TestSentGMHideMe& Test_GetLastGMHideMe() const { return m_test_last_gm_hideme; }

	// T229 test seam, same shape: the complete OP_GMSummon packet (opcode +
	// 152-byte GMSummon_Struct) the last SendGMSummon() call built, captured
	// before the connection gate. Pins that the body carries the GM's own
	// position in SERVER coordinates as floats -- eqemu's OPGMSummon reads
	// them (MovePC to gms->x/y/z), so the zeros T148 shipped sent the target
	// to the origin.
	struct TestSentGMSummon {
		bool sent = false;
		std::vector<uint8_t> bytes;
	};
	const TestSentGMSummon& Test_GetLastGMSummon() const { return m_test_last_gm_summon; }

private:
	ClientContext& ctx_;
	EverQuest& owner_;

	TestSentGMHideMe m_test_last_gm_hideme;
	TestSentGMSummon m_test_last_gm_summon;
};

}  // namespace gm
}  // namespace eqt
