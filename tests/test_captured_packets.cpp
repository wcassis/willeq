/*
 * T147: layout-level regression tests built on REAL CAPTURED BYTES.
 *
 * T144 pinned OP_GroundSpawn behaviourally, through ParseGroundSpawn(). That
 * guards the parser. These tests guard the STRUCT: they read the same captured
 * bytes at the offsets `EQT::Object_Struct` declares, using offsetof(), so if
 * a field moves the test fails even though the hand-rolled parser would keep
 * working (and vice versa). Between them the two levels close the gap that let
 * `Object_Struct` sit at the wrong size, with a passing assert, for the entire
 * life of the repo.
 *
 * The harness (tests/captured_packets.h) is deliberately struct-agnostic so the
 * next unit with a live capture -- OP_Death and OP_Consider are the two open
 * ones, see refactor/deferred.md -- can add a CapturedPacket and a field table
 * and get the same gate for free.
 */

#include <gtest/gtest.h>
#include <gtest/gtest-spi.h>
#include "captured_packets.h"
#include "common/packet_structs.h"

using namespace eqt_test;

namespace {

// Every capture must be the opcode it claims to be before anything decoded
// from it means anything.
TEST(CapturedPackets, OpcodesMatchTheirCaptures) {
    ExpectOpcode(kGroundSpawnKiln);
    ExpectOpcode(kGroundSpawnForge);
    ExpectOpcode(kGroundSpawnItem);
}

// eqemu ENCODE(OP_GroundSpawn) writes
//   size = strlen(object_name) + sizeof(structs::Object_Struct) - 1
// so a correct Object_Struct makes this identity hold for every capture. This
// is the assertion that a `sizeof == N` static_assert cannot make: it ties the
// declared size to observed packet lengths.
TEST(CapturedPackets, GroundSpawn_BodyLengthTiesToObject_StructSize) {
    const CapturedPacket* caps[] = {&kGroundSpawnKiln, &kGroundSpawnForge, &kGroundSpawnItem};
    for (const CapturedPacket* c : caps) {
        SCOPED_TRACE(c->name);
        const std::string name = ReadCStr(*c, offsetof(EQT::Object_Struct, object_name));
        EXPECT_EQ(BodyLen(*c), name.size() + sizeof(EQT::Object_Struct) - 1);
    }
}

// The field table is written against offsetof(), not against literals, so this
// fails the moment the struct's layout drifts from the bytes on the wire.
TEST(CapturedPackets, GroundSpawn_Kiln_FieldsDecodeAtStructOffsets) {
    ExpectFields(kGroundSpawnKiln, {
        U32("drop_id",       offsetof(EQT::Object_Struct, drop_id),       199),
        U16("zone_id",       offsetof(EQT::Object_Struct, zone_id),         1),   // qeynos
        U16("zone_instance", offsetof(EQT::Object_Struct, zone_instance),   0),
        F32("heading",       offsetof(EQT::Object_Struct, heading),     117.0f),
        F32("z",             offsetof(EQT::Object_Struct, z),             0.0f),
        F32("x",             offsetof(EQT::Object_Struct, x),          -177.0f),
        F32("y",             offsetof(EQT::Object_Struct, y),           484.0f),
        Str("object_name",   offsetof(EQT::Object_Struct, object_name), "IT10804_ACTORDEF"),
        U32("object_type",   offsetof(EQT::Object_Struct, object_type),    17),   // blacksmithing
        U32("unknown084",    offsetof(EQT::Object_Struct, unknown084), 0xFFFFFFFFu),
    });
}

TEST(CapturedPackets, GroundSpawn_Forge_FieldsDecodeAtStructOffsets) {
    ExpectFields(kGroundSpawnForge, {
        U32("drop_id",     offsetof(EQT::Object_Struct, drop_id),      190),
        U16("zone_id",     offsetof(EQT::Object_Struct, zone_id),        1),
        F32("heading",     offsetof(EQT::Object_Struct, heading),   369.0f),
        F32("x",           offsetof(EQT::Object_Struct, x),        -296.0f),
        F32("y",           offsetof(EQT::Object_Struct, y),         179.0f),
        Str("object_name", offsetof(EQT::Object_Struct, object_name), "IT66_ACTORDEF"),
        U32("object_type", offsetof(EQT::Object_Struct, object_type),   17),
    });
}

// The short packet. object_type still lands inside the body because the format
// is strlen+91, not a fixed 100/102 bytes.
TEST(CapturedPackets, GroundSpawn_DroppedItem_FieldsDecodeAtStructOffsets) {
    EXPECT_EQ(BodyLen(kGroundSpawnItem), 95u);
    ExpectFields(kGroundSpawnItem, {
        U32("drop_id",     offsetof(EQT::Object_Struct, drop_id),     189),
        F32("x",           offsetof(EQT::Object_Struct, x),        155.0f),
        F32("y",           offsetof(EQT::Object_Struct, y),       -144.7f),
        F32("z",           offsetof(EQT::Object_Struct, z),        -36.9f),
        Str("object_name", offsetof(EQT::Object_Struct, object_name), "IT63"),
        U32("object_type", offsetof(EQT::Object_Struct, object_type),   0),
    });
}

// A NUL-terminated name is not a 32-byte field: the forge capture carries
// "_ACTORDEF" left over in the server's uninitialised send buffer immediately
// after the terminator.
TEST(CapturedPackets, GroundSpawn_NameFieldIsNulTerminatedNotFixedWidth) {
    const size_t off = offsetof(EQT::Object_Struct, object_name);
    EXPECT_EQ(ReadCStr(kGroundSpawnForge, off), "IT66_ACTORDEF");
    const char* raw = reinterpret_cast<const char*>(Body(kGroundSpawnForge) + off);
    EXPECT_EQ(std::string(raw + 14, 9), "_ACTORDEF");
}

// The harness must fail loudly rather than read past a short capture.
TEST(CapturedPackets, HarnessRefusesToReadPastTheEndOfAPacket) {
    EXPECT_NONFATAL_FAILURE(
        ReadAt<uint32_t>(kGroundSpawnItem, BodyLen(kGroundSpawnItem) - 1),
        "runs past the packet");
    EXPECT_EQ(ReadCStr(kGroundSpawnItem, BodyLen(kGroundSpawnItem) + 10), "");
}

}  // namespace
