// T18: entity-domain zone-stream tests (eq_entities.cpp, previously ~0%
// direct coverage; largest domain TU at ~2445 lines).
//
// Two evidence sources, per the batch plan:
//   - Real capture: the full P29 zone stream replayed in capture order into
//     a headless EverQuest, with the resulting entity table asserted.
//     Numeric pins below come from a discovery run against the capture
//     (deliberate-fail-then-pin, same method as T14), not guesses.
//   - Synthetic packets at the offsets the handlers document, including
//     truncated/malformed shapes (the T04/T05/T07/T08 crash class) and the
//     Spawn_Struct position bitfield decode with its INVERSEXY swap.
//
// Observables: EverQuest::GetEntities() (T09-style read-only accessor,
// already public) and GameStateBridge events (setBridge is public; the
// bridge is the game->renderer contract, so asserting events is asserting
// product behavior, not test plumbing).

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;

namespace {

// Encodes the four Spawn_Struct position bitfield words the handlers
// decode (packet offsets = struct offset + 2 for the opcode prefix).
struct PosFields {
    uint32_t field1;  // deltaHeading | x<<10
    uint32_t field2;  // y | animation<<19
    uint32_t field3;  // z | deltaY<<19
    uint32_t field4;  // deltaX | heading<<13
};

PosFields EncodePos(int32_t server_x, int32_t server_y, int32_t z, uint32_t raw_heading,
                    uint32_t animation = 0) {
    PosFields f{};
    f.field1 = (static_cast<uint32_t>(server_x) & 0x7FFFF) << 10;
    f.field2 = (static_cast<uint32_t>(server_y) & 0x7FFFF) |
               ((animation & 0x3FF) << 19);
    f.field3 = static_cast<uint32_t>(z) & 0x7FFFF;
    f.field4 = (raw_heading & 0x7FF) << 13;
    return f;
}

// Writes one 385-byte Titanium Spawn_Struct at packet offset `base`
// (base = 2 for NewSpawn; 2 + i*385 for ZoneSpawns batches).
void WriteSpawnStruct(EQ::Net::DynamicPacket& p, size_t base, uint32_t spawn_id,
                      const std::string& name, uint8_t level, uint32_t race,
                      uint8_t char_class, uint8_t gender, uint8_t npc_type,
                      uint8_t hp_percent, const PosFields& pos) {
    p.PutCString(base + 7, name.c_str());
    p.PutUInt8(base + 83, npc_type);
    p.PutUInt8(base + 86, hp_percent);
    p.PutUInt32(base + 94, pos.field1);
    p.PutUInt32(base + 98, pos.field2);
    p.PutUInt32(base + 102, pos.field3);
    p.PutUInt32(base + 106, pos.field4);
    p.PutUInt8(base + 151, level);
    p.PutUInt32(base + 284, race);
    p.PutUInt8(base + 331, char_class);
    p.PutUInt8(base + 334, gender);
    p.PutUInt32(base + 340, spawn_id);
}

EverQuest MakeEq() {
    return EverQuest("127.0.0.1", 0, "test", "test", "test", "Testchar");
}

std::vector<uint8_t> NewSpawnPayload(uint32_t spawn_id, const std::string& name,
                                     uint8_t level = 5, uint8_t npc_type = 1) {
    return BuildPayload(387, [&](EQ::Net::DynamicPacket& p) {
        WriteSpawnStruct(p, 2, spawn_id, name, level, 54, 1, 0, npc_type, 100,
                         EncodePos(100, 200, 30, 0));
    });
}

// ===================================================================
// Real capture: full zone stream
// ===================================================================

TEST(ZoneEntitiesRealCapture, FullZoneStreamReplay_PopulatesEntityTable) {
    EverQuest eq = MakeEq();
    size_t recordCount = 0;

    bool haveCapture = ForEachCaptureRecord(
        EQT::Net::SessionStream::Zone,
        [&](EQT::Net::SessionDirection dir, uint16_t opcode,
            const std::vector<uint8_t>& payload) {
            if (dir != EQT::Net::SessionDirection::ServerToClient) {
                return;
            }
            ++recordCount;
            ASSERT_NO_THROW(ReplayZone(eq, opcode, payload))
                << "zone record " << recordCount << " opcode 0x" << std::hex << opcode;
        });
    if (!haveCapture) {
        GTEST_SKIP() << "No session capture at " << DefaultSessionLogPath()
                     << " (set WILLEQ_SESSION_LOG to override).";
    }

    ASSERT_GT(recordCount, 0u) << "capture has no zone-stream S->C records";

    // Values pinned from a discovery run against the P29 capture (qeynos2
    // tour): 2788 zone S->C records leave 184 entities after all spawn/
    // despawn traffic, with the player at spawn id 261.
    EXPECT_EQ(recordCount, 2788u);
    const auto& entities = eq.GetEntities();
    EXPECT_EQ(entities.size(), 184u);
    EXPECT_EQ(eq.GetEntityID(), 261);

    ASSERT_EQ(entities.count(1), 1u);
    EXPECT_EQ(entities.at(1).name, "zone_controller000");
    EXPECT_EQ(entities.at(1).level, 200);

    // A merchant NPC with non-default class, and a guard with full position:
    // both decoded through the ZoneSpawns bulk path.
    ASSERT_EQ(entities.count(2), 1u);
    EXPECT_EQ(entities.at(2).name, "Tyokan_Mekase000");
    EXPECT_EQ(entities.at(2).level, 25);
    EXPECT_EQ(entities.at(2).race_id, 71u);
    EXPECT_EQ(entities.at(2).class_id, 41);
    EXPECT_EQ(entities.at(2).npc_type, 1);

    ASSERT_EQ(entities.count(5), 1u);
    EXPECT_EQ(entities.at(5).name, "Guard_Treitan000");
    EXPECT_EQ(entities.at(5).level, 35);
    EXPECT_EQ(entities.at(5).class_id, 20);
    EXPECT_FLOAT_EQ(entities.at(5).x, 270.0f);
    EXPECT_FLOAT_EQ(entities.at(5).y, 202.0f);
    EXPECT_FLOAT_EQ(entities.at(5).z, 3.75f);

    // The player's own spawn is tracked like any other entity.
    ASSERT_EQ(entities.count(261), 1u);
    EXPECT_EQ(entities.at(261).name, "Testchar");
}

// ===================================================================
// Synthetic: NewSpawn
// ===================================================================

TEST(ZoneEntitiesNewSpawn, ParsesDocumentedFieldsIncludingXYSwap) {
    EverQuest eq = MakeEq();

    // server_x=1234, server_y=-567 (sign-extension case), z=89,
    // raw_heading=512 (-> 512*360/2048 = 90 degrees), animation=7.
    auto payload = BuildPayload(387, [&](EQ::Net::DynamicPacket& p) {
        WriteSpawnStruct(p, 2, 4242, "a_test_rat", 13, 54, 1, 0, 1, 87,
                         EncodePos(1234, -567, 89, 512, 7));
    });

    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, payload));

    const auto& entities = eq.GetEntities();
    ASSERT_EQ(entities.size(), 1u);
    ASSERT_EQ(entities.count(4242), 1u);
    const Entity& e = entities.at(4242);
    EXPECT_EQ(e.name, "a_test_rat");
    EXPECT_EQ(e.level, 13);
    EXPECT_EQ(e.race_id, 54u);
    EXPECT_EQ(e.class_id, 1);
    EXPECT_EQ(e.gender, 0);
    EXPECT_EQ(e.npc_type, 1);
    EXPECT_EQ(e.hp_percent, 87);
    // INVERSEXY: entity.x <- server_y, entity.y <- server_x.
    EXPECT_FLOAT_EQ(e.x, -567.0f);
    EXPECT_FLOAT_EQ(e.y, 1234.0f);
    EXPECT_FLOAT_EQ(e.z, 89.0f);
    EXPECT_FLOAT_EQ(e.heading, 90.0f);
    EXPECT_EQ(e.animation, 7u);
}

TEST(ZoneEntitiesNewSpawn, TruncatedPacketRejectedWithoutSideEffects) {
    EverQuest eq = MakeEq();

    // One byte short of the 387-byte guard.
    auto payload = BuildPayload(386, [&](EQ::Net::DynamicPacket& p) {
        WriteSpawnStruct(p, 2, 4242, "a_test_rat", 13, 54, 1, 0, 1, 87,
                         EncodePos(1, 2, 3, 0));
    });

    EXPECT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, payload));
    EXPECT_TRUE(eq.GetEntities().empty());
}

TEST(ZoneEntitiesNewSpawn, RespawnWithSameIdOverwritesEntry) {
    EverQuest eq = MakeEq();

    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayload(7, "first_mob")));
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayload(7, "second_mob")));

    const auto& entities = eq.GetEntities();
    ASSERT_EQ(entities.size(), 1u);
    EXPECT_EQ(entities.at(7).name, "second_mob");
}

// ===================================================================
// Synthetic: ZoneSpawns batches
// ===================================================================

TEST(ZoneEntitiesZoneSpawns, BatchOfTwoParsesBothAndStopsAtEmptyName) {
    EverQuest eq = MakeEq();

    // Three struct slots; the third has an empty name, which the handler
    // documents as end-of-data.
    auto payload = BuildPayload(2 + 3 * 385, [&](EQ::Net::DynamicPacket& p) {
        WriteSpawnStruct(p, 2, 11, "mob_one", 1, 54, 1, 0, 1, 100, EncodePos(10, 20, 3, 0));
        WriteSpawnStruct(p, 2 + 385, 22, "mob_two", 2, 60, 1, 0, 1, 100, EncodePos(40, 50, 6, 0));
        // slot 3 left all-zero: empty name terminates the walk
    });

    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_ZoneSpawns, payload));

    const auto& entities = eq.GetEntities();
    ASSERT_EQ(entities.size(), 2u);
    EXPECT_EQ(entities.at(11).name, "mob_one");
    EXPECT_EQ(entities.at(22).name, "mob_two");
}

TEST(ZoneEntitiesZoneSpawns, SecondBatchAddsWithoutClearingFirst) {
    EverQuest eq = MakeEq();

    auto batch1 = BuildPayload(2 + 385, [&](EQ::Net::DynamicPacket& p) {
        WriteSpawnStruct(p, 2, 11, "mob_one", 1, 54, 1, 0, 1, 100, EncodePos(10, 20, 3, 0));
    });
    auto batch2 = BuildPayload(2 + 385, [&](EQ::Net::DynamicPacket& p) {
        WriteSpawnStruct(p, 2, 22, "mob_two", 2, 60, 1, 0, 1, 100, EncodePos(40, 50, 6, 0));
    });

    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_ZoneSpawns, batch1));
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_ZoneSpawns, batch2));

    EXPECT_EQ(eq.GetEntities().size(), 2u);
}

TEST(ZoneEntitiesZoneSpawns, UndersizedPacketParsesNothing) {
    EverQuest eq = MakeEq();

    // Smaller than one Spawn_Struct: the while guard never admits it.
    auto payload = BuildPayload(2 + 100, nullptr);

    EXPECT_NO_THROW(ReplayZone(eq, HC_OP_ZoneSpawns, payload));
    EXPECT_TRUE(eq.GetEntities().empty());
}

// ===================================================================
// Synthetic: DeleteSpawn
// ===================================================================

TEST(ZoneEntitiesDeleteSpawn, RemovesTrackedEntity) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayload(99, "doomed_mob")));
    ASSERT_EQ(eq.GetEntities().size(), 1u);

    auto payload = BuildPayload(4, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt16(2, 99);
    });
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_DeleteSpawn, payload));

    EXPECT_TRUE(eq.GetEntities().empty());
}

TEST(ZoneEntitiesDeleteSpawn, UnknownSpawnIdIsNoOp) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayload(99, "survivor_mob")));

    auto payload = BuildPayload(4, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt16(2, 12345);
    });
    EXPECT_NO_THROW(ReplayZone(eq, HC_OP_DeleteSpawn, payload));

    EXPECT_EQ(eq.GetEntities().size(), 1u);
}

TEST(ZoneEntitiesDeleteSpawn, TruncatedPacketRejected) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayload(99, "survivor_mob")));

    // 3 bytes < the 4-byte guard.
    auto payload = BuildPayload(3, nullptr);
    EXPECT_NO_THROW(ReplayZone(eq, HC_OP_DeleteSpawn, payload));

    EXPECT_EQ(eq.GetEntities().size(), 1u);
}

// ===================================================================
// Synthetic: health updates
// ===================================================================

TEST(ZoneEntitiesHealth, MobHealthUpdatesPercentAndPushesStatsEvent) {
    EverQuest eq = MakeEq();
    TestBridge bridge;
    eq.setBridge(&bridge);
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayload(50, "hurt_mob")));
    bridge.drainEvents();  // discard spawn-time events; this test is about MobHealth

    auto payload = BuildPayload(5, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt16(2, 50);
        p.PutUInt8(4, 42);
    });
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_MobHealth, payload));

    EXPECT_EQ(eq.GetEntities().at(50).hp_percent, 42);

    auto events = bridge.drainEvents();
    bool sawStats = false;
    for (const auto& ev : events) {
        if (ev.type == eqt::state::GameEventType::EntityStatsChanged) {
            const auto& data = std::get<eqt::state::EntityStatsChangedData>(ev.data);
            EXPECT_EQ(data.spawnId, 50);
            EXPECT_EQ(data.hpPercent, 42);
            sawStats = true;
        }
    }
    EXPECT_TRUE(sawStats) << "MobHealth did not publish EntityStatsChanged";
    eq.setBridge(nullptr);
}

TEST(ZoneEntitiesHealth, MobHealthUnknownSpawnAndTruncatedAreNoOps) {
    EverQuest eq = MakeEq();

    auto unknown = BuildPayload(5, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt16(2, 999);
        p.PutUInt8(4, 10);
    });
    EXPECT_NO_THROW(ReplayZone(eq, HC_OP_MobHealth, unknown));

    auto truncated = BuildPayload(4, nullptr);
    EXPECT_NO_THROW(ReplayZone(eq, HC_OP_MobHealth, truncated));
}

TEST(ZoneEntitiesHealth, HPUpdateComputesPercentAndGuardsZeroMax) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayload(60, "fraction_mob")));

    // cur=25, max=200 -> 12%.
    auto payload = BuildPayload(12, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2, 25);
        p.PutUInt32(6, 200);
        p.PutUInt16(10, 60);
    });
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_HPUpdate, payload));
    EXPECT_EQ(eq.GetEntities().at(60).hp_percent, 12);

    // max_hp = 0 must not divide; handler documents the fallback as 100.
    auto zeroMax = BuildPayload(12, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2, 25);
        p.PutUInt32(6, 0);
        p.PutUInt16(10, 60);
    });
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_HPUpdate, zeroMax));
    EXPECT_EQ(eq.GetEntities().at(60).hp_percent, 100);
}

}  // namespace
