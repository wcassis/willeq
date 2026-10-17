// B1088 (#408) fix leg: the OP_NewZone zone_long_name READ, through the real
// packet handler.
//
// The lookup unit (tests/test_zone_display_name.cpp) and the zone-identity
// test (test_position_store_coherence.cpp) both build OP_NewZone payloads
// WITHOUT the long-name field, and call noteNewZone()/setOverride() with
// hand-made strings -- so the one line that reads the name OFF THE WIRE
// (ZoneProcessNewZone, src/client/zone/zone_entry.cpp) was untested. It was
// reading at packet offset 96, which lands in the NUL-padded tail of the
// 32-byte zone_short_name field (struct 64-95): every real short name is
// shorter than 30 characters, so the read returned "" and
// noteNewZone(zone_id, "") CLEARED the override -- the server's long name
// was never stored, and the OP_NewZone refresh half of B1088 was dead for
// every zone era_zones.tsv omits (tutoriala/b, jaggedpine, nedaria,
// shadowrest: the loading screen drew no line).
//
// Ground truth (three agreeing sources, the review leg re-derived all three):
//   include/common/packets/spawn.h:169  zone_long_name/*0096*/ char[278]
//   the sibling reads in the SAME handler use +2 for the 2-byte opcode
//   prefix: GetCString(66) = struct 64 (short name), GetUInt16(686) =
//   struct 684 (zone_id);
//   the issue spec: "OP_NewZone zone_long_name at struct offset 96
//   (packet offset 98)".
//
// So: struct offset 96 = PACKET offset 98. These tests build a full
// 700-byte OP_NewZone payload with the long name at packet offset 98 (the
// same BuildPayload/ReplayZone replay seam test_position_store_coherence
// uses) and assert the handler stores it.
//
// RED measured at the parent (da28af94): test 1 fails -- the buggy
// GetCString(96) reads the short-name field's NUL and the lookup answers
// "" (or the era table, never the server's name). GREEN at this head.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/eq.h"
#undef private
#undef protected

#include "client/bridge/game_state_bridge.h"
#include "client/zone/zone_display_name.h"
#include "zone_stream_test_utils.h"

#include <cstring>
#include <string>
#include <vector>

using namespace eqt;
using namespace zonetest;

namespace {

// A full NewZone_Struct (2 + 700 bytes, zeroed) with:
//   zone_short_name  at packet offset 66  (struct 64, 32 bytes),
//   zone_long_name   at packet offset 98  (struct 96, 278 bytes),
//   zone_id          at packet offset 686 (struct 684).
// Exactly the fields ZoneProcessNewZone reads (zone_entry.cpp).
std::vector<uint8_t> NewZonePayloadWithLongName(const std::string& shortName,
                                                const std::string& longName,
                                                uint16_t zoneId) {
    return BuildPayload(2 + 700, [&](EQ::Net::DynamicPacket& p) {
        std::memset(p.Data(), 0, 2 + 700);
        std::memcpy(static_cast<uint8_t*>(p.Data()) + 66, shortName.c_str(), shortName.size() + 1);
        if (!longName.empty()) {
            std::memcpy(static_cast<uint8_t*>(p.Data()) + 98, longName.c_str(), longName.size() + 1);
        }
        p.PutUInt16(686, zoneId);
    });
}

EverQuest MakeFreshEq() {
    EverQuest eq{"127.0.0.1", 0, "test", "test", "test", "Testchar"};
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    return eq;
}

}  // namespace

TEST(ZoneNewZoneDisplayName, TheLongNameReadsAtPacketOffset98AndIsStored) {
    // An id NO era-table row can know: the ONLY source that can name it is
    // the packet's zone_long_name. (test_zone_display_name.cpp uses 999999
    // for this role; 900001 keeps the two files independent.)
    const uint32_t zoneId = 900001;
    ZoneDisplayNames::instance().clearEntry();

    EverQuest eq = MakeFreshEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewZone,
                               NewZonePayloadWithLongName("shadowrest", "Shadowrest -- the Sunken City",
                                                          zoneId)));

    // The zone identity still lands (the pre-fix tests pinned this half).
    EXPECT_EQ(eq.GetGameState().zoneName(), "shadowrest");
    // The SERVER's long name -- verbatim, not the short name, not "" --
    // is stored as the override for the zone id.
    const ZoneDisplayNames& names = ZoneDisplayNames::instance();
    EXPECT_EQ(names.lookup(zoneId), "Shadowrest -- the Sunken City")
        << "ZoneProcessNewZone must read zone_long_name at packet offset 98 "
           "(struct 96, spawn.h) and noteNewZone() it; at packet offset 96 "
           "the read lands in the short-name field's NUL and stores \"\"";
    EXPECT_EQ(names.entryLine(names.lookup(zoneId)),
              "Entering Shadowrest -- the Sunken City...");
    ZoneDisplayNames::instance().clearEntry();
    ZoneDisplayNames::instance().setOverride(zoneId, "");
}

TEST(ZoneNewZoneDisplayName, AnEmptyLongNameIsBoundedAndClearsOnlyTheOverride) {
    // The short-name path: a zone whose long-name field is empty (NUL at
    // packet offset 98). The documented store contract (zone_display_name.h)
    // is that an empty field CLEARS that zone's override and the era table
    // answers again -- so what is pinned here is: the read is BOUNDED (a
    // 700-byte packet, no throw from the handler), the zone identity still
    // lands, and the clear reaches only the override -- never the era
    // table's own rows. (The buggy offset read is not distinguishable from
    // a genuine empty field by this test alone -- it too returned "";
    // test 1 pins the offset. This pins that the empty path itself is
    // safe.)
    const uint32_t zoneId = 900002;  // era-unknown id
    ZoneDisplayNames::instance().clearEntry();
    ZoneDisplayNames::instance().setOverride(zoneId, "Jagged Pine");

    EverQuest eq = MakeFreshEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewZone,
                               NewZonePayloadWithLongName("jaggedpine", "", zoneId)));

    EXPECT_EQ(eq.GetGameState().zoneName(), "jaggedpine");
    EXPECT_EQ(eq.GetGameState().zoneId(), zoneId);
    // The override is cleared (the documented contract -- an empty
    // OP_NewZone field means "the server sent no name"), and the era
    // table answers (empty for this id). No error, no corruption.
    EXPECT_EQ(ZoneDisplayNames::instance().lookup(zoneId), "")
        << "an empty zone_long_name clears the override; the era table -- "
           "never the packet -- answers again";
    ZoneDisplayNames::instance().clearEntry();
    ZoneDisplayNames::instance().setOverride(zoneId, "");
}

TEST(ZoneNewZoneDisplayName, TheShortNameFieldDoesNotLeakIntoTheLongName) {
    // The pre-fix read (packet 96) started INSIDE the 32-byte short-name
    // field (packet 66-97), two bytes before the long name. Pin the leak
    // in its worst shape: a short name that FILLs the whole field (32
    // non-NUL chars, so no terminator sits between the fields) makes the
    // buggy read run from packet 96 straight through the long name --
    // returning the field's tail GLUED to the long name. The fixed read
    // (packet 98) returns the long name alone. (A short name that ends
    // early hides the bug: the buggy read hits the field's NUL and
    // returns "" -- which is exactly what the production zones showed.)
    const uint32_t zoneId = 900003;
    const std::string shortName(32, 's');   // fills struct 64-95 entirely
    ZoneDisplayNames::instance().clearEntry();

    EverQuest eq = MakeFreshEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewZone,
                               NewZonePayloadWithLongName(shortName, "Nedaria", zoneId)));

    const ZoneDisplayNames& names = ZoneDisplayNames::instance();
    const std::string longName = names.lookup(zoneId);
    EXPECT_EQ(longName, "Nedaria");
    // The identity half lands the SHORT name as the zone name, unchanged.
    EXPECT_EQ(eq.GetGameState().zoneName(), shortName);
    // The leak shape: the buggy read's answer starts with the short-name
    // field's last two bytes.
    EXPECT_NE(longName.rfind(shortName.substr(30, 2), 0), 0u)
        << "the long name carries the short-name field's tail -- the read "
           "started at packet 96, inside the short-name field (66-97)";
    ZoneDisplayNames::instance().clearEntry();
    ZoneDisplayNames::instance().setOverride(zoneId, "");
}
