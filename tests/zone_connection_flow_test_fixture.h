// Shared packet-building helpers for the test_zone_connection_flow family.
//
// T446: test_zone_connection_flow.cpp reached 1,367 lines. The cases are now
// split across test_zone_connection_flow_*.cpp, all of them sources of the
// SINGLE test_zone_connection_flow target (T165's precedent from the
// test_command_processor family), so the split costs no extra binary, no
// extra ctest discovery pass, and -- because every case keeps its suite AND
// its name -- no ctest test name changes at all.
//
// Everything here is a free function or a local fake, so the anonymous
// namespace is kept: each TU gets its own copy, which is correct and free.
//
// T04: zone connection-flow sequencing tests.
//
// Targets CheckZoneRequestPhaseComplete (eq_zone_entry.cpp) and the
// surrounding zone-stream dispatcher (DispatchZonePacket,
// eq_zone_dispatch.cpp) -- both split out of eq.cpp by J02 -- driven entirely
// through the Batch R replay/test seam (EverQuest::ReplayZonePacket,
// eq.h:697) -- no network, no graphics, no live connection.
//
// Scope: zone stream ONLY. Login/world handlers are private with no replay
// equivalent (see refactor/T04_zone_connection_flow.md "Scope boundary");
// they are explicitly out of scope for this unit.
//
// This file follows tests/test_session_replay.cpp's idiom for building
// synthetic packets: BuildPayload() fills a DynamicPacket at the packet
// offsets the ZoneProcess* handlers actually read (offsets documented in
// the handler being exercised), Replay() feeds it through the same opcode
// switch ZoneOnPacketRecv uses.
//
// See refactor/T04_zone_connection_flow.md for the design/review notes.

#pragma once

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "client/eq.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/zone_bsp.h"
#include "client/bridge/game_state_bridge.h"
#include "client/inventory/inventory_constants.h"
#include "common/net/packet.h"

namespace {

// Builds the payload bytes (packet minus the 2-byte opcode prefix) for a
// synthetic Zone S->C packet. `totalPacketSize` includes the opcode prefix
// so callers can write field offsets exactly as documented in the
// ZoneProcess* handlers (which read at those same packet offsets).
std::vector<uint8_t> BuildPayload(size_t totalPacketSize,
                                   const std::function<void(EQ::Net::DynamicPacket&)>& fill) {
    EQ::Net::DynamicPacket p;
    p.Resize(totalPacketSize);
    if (fill) {
        fill(p);
    }
    std::vector<uint8_t> payload(totalPacketSize >= 2 ? totalPacketSize - 2 : 0);
    if (!payload.empty()) {
        std::memcpy(payload.data(), static_cast<const uint8_t*>(p.Data()) + 2, payload.size());
    }
    return payload;
}

// A zero-length payload -- opcode only, no body.
std::vector<uint8_t> EmptyPayload() {
    return {};
}

void Replay(EverQuest& eq, uint16_t opcode, const std::vector<uint8_t>& payload) {
    eq.ReplayZonePacket(opcode, payload.data(), payload.size());
}

// The four gating-relevant packets, built with the minimal (empty) payload
// their handlers actually need -- none of ZoneProcessSendAATable,
// ZoneProcessRespondAA, ZoneProcessTributeInfo, ZoneProcessSendGuildTributes
// read any packet fields (all four in eq_player_info.cpp); they only log, bump a
// counter (except RespondAA), and call CheckZoneRequestPhaseComplete().
void SendNewZone(EverQuest& eq) {
    // Zero-length payload: ZoneProcessNewZone guards every field read behind
    // p.Length() thresholds (ZoneProcessNewZone, eq_zone_entry.cpp), so an empty payload safely
    // skips zone-name/environment parsing and falls through to
    // m_new_zone_received = true plus the four AA/tribute/guild-tribute
    // request sends (suppressed, no live connection).
    Replay(eq, HC_OP_NewZone, EmptyPayload());
}
void SendAATable(EverQuest& eq) { Replay(eq, HC_OP_SendAATable, EmptyPayload()); }
void SendRespondAA(EverQuest& eq) { Replay(eq, HC_OP_RespondAA, EmptyPayload()); }
void SendTributeInfo(EverQuest& eq) { Replay(eq, HC_OP_TributeInfo, EmptyPayload()); }
void SendGuildTributes(EverQuest& eq) { Replay(eq, HC_OP_SendGuildTributes, EmptyPayload()); }

// T41: minimal GameStateBridge fake that just records every pushed event's
// type, so tests can assert PublishFullStateSnapshot() fired (and fired only
// once) without depending on IrrlichtBridge/ConsoleBridge or a real renderer.
// applyEvent() is unused here -- this test only drains and inspects the
// event queue EverQuest pushed into, it never calls applyEvent() itself.
class RecordingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

// T41 addendum: writes one 385-byte Titanium Spawn_Struct at packet offset
// `base`, same field offsets test_zone_entities.cpp's WriteSpawnStruct uses
// (this file doesn't share that header, so a small local copy -- extended
// with is_pet/pet_owner_id, which that helper doesn't need for its own
// tests), for building a synthetic "our pet, already in the zone" NPC spawn.
// is_pet at struct offset 329 (uint8), pet_owner_id at 189 (uint32) --
// confirmed against eq_entities.cpp, the same offsets
// ZoneProcessZoneSpawns/ZoneProcessNewSpawn read.
void WritePetSpawnStruct(EQ::Net::DynamicPacket& p, size_t base, uint32_t spawnId,
                          const std::string& name, uint32_t petOwnerId) {
    p.PutCString(base + 7, name.c_str());
    p.PutUInt8(base + 83, 1);     // npc_type: NPC
    p.PutUInt8(base + 86, 100);   // hp_percent
    p.PutUInt8(base + 151, 1);    // level
    p.PutUInt32(base + 189, petOwnerId);
    p.PutUInt32(base + 284, 54);  // race
    p.PutUInt8(base + 329, 1);    // is_pet
    p.PutUInt8(base + 331, 1);    // char_class
    p.PutUInt32(base + 340, spawnId);
}

// T43: builds a minimal but well-formed Titanium serialized-item string, the
// same wire format TitaniumItemParser::parseItem() (inventory_item_parser.cpp)
// expects from a real HC_OP_CharInventory/HC_OP_ItemPacket -- instance
// fields, then a quoted block of >=135 static fields, per
// "titanium.cpp SerializeItem" (see parseInstanceData/parseStaticData's own
// field-index comments). Only the fields this test actually asserts on
// (slot, itemId, name, icon) are set to nonzero/nonempty; everything else is
// left empty (TitaniumItemParser::toInt() defaults an empty field to 0).
std::string BuildItemString(int16_t slot, uint32_t itemId, const std::string& name, uint32_t icon) {
    // Instance fields (11): stackCount|unknown|slot|price|count|exp|serial|recast|charges|attuned|unknown
    std::string instance = "0|0|" + std::to_string(slot) + "|0|1|0|0|0|0|0|0";

    // Static fields (135 minimum -- parseStaticData rejects fewer).
    std::vector<std::string> fields(135, "");
    fields[0] = "0";                    // itemType (0 = Common)
    fields[1] = name;                   // name
    fields[2] = name;                   // lore
    fields[3] = "IT63";                 // idfile (unused by the parser beyond presence)
    fields[4] = std::to_string(itemId); // itemId
    fields[5] = "10";                   // weight (tenths -> 1.0)
    fields[6] = "1";                    // noRent field: nonzero -> noRent=false
    fields[7] = "1";                    // noDrop field: nonzero -> noDrop=false
    fields[8] = "0";                    // size
    fields[9] = "0";                    // equip slots bitmask
    fields[10] = "0";                   // vendor price
    fields[11] = std::to_string(icon);  // icon

    std::string staticData;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) staticData += "|";
        staticData += fields[i];
    }

    return instance + "\"" + staticData + "\"";
}

} // namespace
