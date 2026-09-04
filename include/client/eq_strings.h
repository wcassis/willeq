#pragma once

// Batch K (refactor/K_component_extraction.md), unit K14.
//
// The last flat src/client/eq_*.cpp file to move: eq_strings.cpp (F02) held
// 14 EverQuest bodies -- a mix of pure lookups (no EverQuest state) and three
// that read m_string_db. This header splits the two shapes the way the K14
// plan calls for:
//
//   * eqt::strings -- free functions. Ten static lookup tables (class/race/
//     deity/opcode/body-type/chat-type/equip-slot/npc-type names, the price
//     formatter) plus the two DumpPacket overloads. None of them read
//     EverQuest state; DumpPacket read the private static s_debug_level, so
//     it now takes eqt::strings::GetDebugLevel() -- EverQuest's own public
//     accessor -- the same non-friend-destination substitution K13 gave
//     eqt::PacketSender::SafeQueueZonePacket (K13 correction 3 / (jj)).
//     GetOpcodeName is called from DumpPacket as a same-namespace sibling
//     now, so that call needed no qualification change at all.
//
//   * eqt::StringDb -- a context service, K13's shape exactly. It holds a
//     reference to EverQuest's own m_string_db (spelled the same, per K13
//     correction (kk)) and carries the three bodies that read it:
//     GetStringMessage, GetFormattedStringMessage, LoadStringFiles. Exposed
//     on ClientContext as ctx_.strings, and on EverQuest as Strings() for
//     the many non-component callers (the action layer, spell_cast_checks,
//     the UI) that reach it the same way they reach Guild()/Chat()/etc.
//
// DumpEntityAppearance is NOT here. Its plan entry called it a "debug helper
// with s_debug_level reads" alongside DumpPacket, but the body reads
// m_entities (EverQuest's own entity map) and has no s_debug_level read at
// all -- it stayed on EverQuest, in src/client/core/everquest.cpp, next to
// the state it dumps. See this unit's Review, deviation 1.

#include <cstdint>
#include <string>
#include <vector>

namespace EQ {
namespace Net {
class Packet;
}
}
namespace EQT {
class StringDatabase;
}

namespace eqt {
namespace strings {

std::string GetClassName(uint32_t class_id);
std::string GetRaceName(uint32_t race_id);
std::string GetDeityName(uint32_t deity_id);
std::string GetOpcodeName(uint16_t opcode);
std::string GetBodyTypeName(uint8_t bodytype);
std::string GetChatTypeName(uint32_t chat_type);
std::string GetEquipSlotName(int slot);
std::string GetNpcTypeName(uint8_t npc_type);

// Format copper amount as currency string (e.g. "5p 3g 2s 1c"). Was also
// EverQuest::FormatVendorPrice -- a one-line alias with a single caller,
// inlined at that caller (§Mechanism 5).
std::string FormatPrice(int32_t copperAmount);

void DumpPacket(const std::string &prefix, uint16_t opcode, const EQ::Net::Packet &p);
void DumpPacket(const std::string &prefix, uint16_t opcode, const uint8_t *data, size_t size);

}  // namespace strings

class StringDb {
public:
	explicit StringDb(EQT::StringDatabase& string_db) : m_string_db(string_db) {}

	// T244: the eqstr lookup surface -- EqActionHandler (not a friend) renders
	// cast refusals through it. See client/spell/cast_refusal.h.
	std::string GetStringMessage(uint32_t string_id);
	// T251: same reason -- castRefusalMessage() substitutes the missing
	// component's name into 433 "You are missing %1.".
	std::string GetFormattedStringMessage(uint32_t string_id, const std::vector<std::string>& args);
	bool LoadStringFiles(const std::string& eqClientPath);

	// T341 (B038 cluster 4): the raw lookup table, for the two callers that
	// need getString()'s "" on a miss rather than GetStringMessage()'s
	// built-in fallback -- ChatComponent::ZoneProcessFormattedMessage (which
	// branches on the template being empty) and PlayerComponent's /who eqstr
	// hook (which falls back to EQT::whoDefaultString). They reached it as
	// owner_.m_string_db before this unit. EverQuest::GetStringDatabase(),
	// which had ZERO callers tree-wide, was deleted with them (Mechanism 5).
	const EQT::StringDatabase& Db() const { return m_string_db; }

private:
	// Reference to EverQuest's own m_string_db, spelled the same (K13
	// correction (kk)) -- the service owns nothing, GetStringDatabase()
	// still reads the one copy.
	EQT::StringDatabase& m_string_db;
};

}  // namespace eqt
