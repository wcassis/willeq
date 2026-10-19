/**
 * B938 (GitLab #256): size pin for PlacedObjectRecord, the per-placement state
 * that replaced the full Irrlicht IMeshSceneNode (~2.2 KB per placement: the
 * node object, its name, its relative/absolute matrices, and a copied
 * SMaterial per mesh buffer).
 *
 * The record carries exactly what drawing a placement needs: the shared mesh
 * pointer (one per geometry, B936), the world transform, the culling data
 * (position / region / box, folded in from the four renderer-parallel
 * vectors), and the optional node handle that exists ONLY in the rare no-PVS
 * zone. In every PVS zone (all gate zones) the node is null and the per-
 * placement cost is sizeof(PlacedObjectRecord).
 *
 * Measured on this platform (x86-64, GCC, C++17, SSE2 — the canonical CI
 * target): the struct lays out as
 *
 *     +0    mesh            (8)
 *     +8    world           (matrix4 = 16 floats, 64 B, alignment 4)
 *    +72    position        (vector3df, 12 B; +84)
 *    +88    region          (size_t, 8; +84 rounds to +88)
 *    +96    box             (aabbox3df = two vector3df, 24 B, alignment 4; +120)
 *   +120    inSceneGraph    (bool, 1 B; +121)
 *   +128    name            (std::string, 32, 8-aligned; +160)
 *   +160    node            (8; +168)
 *
 * so sizeof is 168 and offsetof(node) is 160. This test pins both, plus the
 * structural invariants the refactor rests on:
 *
 *   * the node handle is a plain pointer (never a reference-counted member —
 *     the record owns ONE mesh reference and at most one node reference, both
 *     raw, dropped in zone teardown);
 *   * the record is move-constructible (objectRecords_.push_back(std::move(
 *     record)) is the load path's idiom; a non-movable struct would either
 *     fail to compile or silently copy the string);
 *   * the culling data sits where the old parallel vectors' bytes used to —
 *     position (vector3df), region (size_t, BSP region index), box
 *     (aabbox3df, world-space) and inSceneGraph (bool) — one per placement,
 *     indexed by placement, so a growth of any one of them is a red test.
 *
 * A regression in any of these is a memory regression, not a nit: the `objects`
 * ledger category was 88-98 % of its measured bytes in the per-placement node
 * alone in every zone measured pre-B938.
 *
 * No Irrlicht device, no zone, no display — the struct's type surface is
 * header-only (irrlicht's IMesh/IMeshSceneNode appear only as pointers).
 */

#include <gtest/gtest.h>

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/placed_object_meshes.h"
#include <irrlicht.h>

#include <cstddef>
#include <string>
#include <type_traits>

using namespace EQT::Graphics;

namespace {

// The exact layout measured above. Re-measured per ctest run on every build
// of this file, so a platform/standard drift (e.g. a -fshort-enables change
// to vector3df's alignment, or a std::string ABI change) fails the test
// loudly instead of letting the ledger's per-placement arithmetic go stale.
//
// B1044 (#364): the ONE member whose size differs between the two standard
// libraries the tree builds with is `name`: libstdc++'s std::string is 32
// bytes, libc++'s (Apple clang on macOS) is 24. Every other member lays out
// identically (pointers, floats, size_t and bool have the same size and
// alignment on x86-64 Linux and arm64 Darwin), so on libc++ `node` sits at
// +152 and sizeof is 160. Both pins are explicit, per standard library,
// rather than derived from sizeof(std::string), so a change on either side
// still fails loudly.
#if defined(_LIBCPP_VERSION)
constexpr size_t kExpectedRecordSize = 160;
constexpr size_t kExpectedNodeOffset = 152;
static_assert(sizeof(std::string) == 24, "libc++ std::string is 24 bytes (B1044 layout note)");
#else
constexpr size_t kExpectedRecordSize = 168;
constexpr size_t kExpectedNodeOffset = 160;
#endif

}  // namespace

// The size itself. 168 B vs the ~2.2 KB node it replaced: ~13x smaller per
// placement in the zones that dominate the ledger (the PVS ones, where
// record.node is null and no node is allocated at all).
TEST(PlacedObjectRecordSize, IsPinned) {
    static_assert(sizeof(PlacedObjectRecord) == kExpectedRecordSize,
                  "PlacedObjectRecord grew/shrank: the per-placement memory "
                  "arithmetic (and the ledger's `objects` category) is stale. "
                  "Re-measure the layout in this file's header comment before "
                  "moving the pin.");
    EXPECT_EQ(sizeof(PlacedObjectRecord), kExpectedRecordSize);
}

// The node handle sits where the measurement puts it. Offsets drift whenever
// a member's size or alignment changes, so this is the second tripwire: a
// field reorder that preserves sizeof would still move `node` and break the
// teardown/sim-bridge assumptions that the record is one contiguous struct
// indexed by placement.
TEST(PlacedObjectRecordSize, NodeFieldOffsetIsPinned) {
    static_assert(offsetof(PlacedObjectRecord, node) == kExpectedNodeOffset,
                  "PlacedObjectRecord layout changed: re-measure and move the pin.");
    EXPECT_EQ(offsetof(PlacedObjectRecord, node), kExpectedNodeOffset);
}

// Structural invariants the refactor's reference accounting depends on.

TEST(PlacedObjectRecordShape, NodeHandleIsAPointer) {
    // The record holds at most one node reference, as a raw pointer — never a
    // reference-counted member. In PVS zones it is null and no node exists;
    // zone teardown drops it where present. A smart-pointer member would
    // change the layout AND the accounting (the node's lifetime would follow
    // the record, not the explicit grab/drop in load step 6 and teardown).
    static_assert(std::is_same_v<decltype(PlacedObjectRecord::node),
                                 irr::scene::IMeshSceneNode*>,
                  "record.node must stay a raw pointer (lifetime managed by "
                  "the explicit grab in load step 6 and the drop in zone "
                  "teardown).");
    EXPECT_TRUE((std::is_same_v<decltype(PlacedObjectRecord::node),
                                irr::scene::IMeshSceneNode*>));
}

TEST(PlacedObjectRecordShape, MeshHandleIsAPointer) {
    // One shared mesh per geometry (B936); the record holds exactly one
    // reference to it, grabbed in PVS zones (needsOwnRef) and dropped in
    // teardown. Raw pointer, same lifetime story as the node handle.
    static_assert(std::is_same_v<decltype(PlacedObjectRecord::mesh),
                                 irr::scene::IMesh*>,
                  "record.mesh must stay a raw pointer (one explicit grab per "
                  "PVS placement, dropped in zone teardown).");
    EXPECT_TRUE((std::is_same_v<decltype(PlacedObjectRecord::mesh),
                                irr::scene::IMesh*>));
}

TEST(PlacedObjectRecordShape, CullingDataIsPerPlacement) {
    // The four old renderer-parallel vectors (objectPositions_ /
    // objectRegions_ / objectBoundingBoxes_ / objectInSceneGraph_) folded in
    // here, indexed by placement. Pin the exact types so a "cleanup" back to
    // parallel storage (or a type change that would desync the sim bridge's
    // copy of these fields) is a red test, not a silent drift.
    static_assert(std::is_same_v<decltype(PlacedObjectRecord::position),
                                 irr::core::vector3df>,
                  "position must stay vector3df (Irrlicht coords).");
    static_assert(std::is_same_v<decltype(PlacedObjectRecord::region), size_t>,
                  "region must stay size_t (BSP region index).");
    static_assert(std::is_same_v<decltype(PlacedObjectRecord::box),
                                 irr::core::aabbox3df>,
                  "box must stay aabbox3df (world-space transformed bbox).");
    static_assert(std::is_same_v<decltype(PlacedObjectRecord::inSceneGraph), bool>,
                  "inSceneGraph must stay bool.");
    EXPECT_TRUE((std::is_same_v<decltype(PlacedObjectRecord::position), irr::core::vector3df>));
    EXPECT_TRUE((std::is_same_v<decltype(PlacedObjectRecord::region), size_t>));
    EXPECT_TRUE((std::is_same_v<decltype(PlacedObjectRecord::box), irr::core::aabbox3df>));
    EXPECT_TRUE((std::is_same_v<decltype(PlacedObjectRecord::inSceneGraph), bool>));
}

TEST(PlacedObjectRecordShape, IsMoveConstructible) {
    // The load path does ctx_.objectRecords.push_back(std::move(record)); a
    // non-movable record would either fail to compile or silently fall back
    // to a copy (copying the string every placement). Pin movability so the
    // idiom keeps working as the struct evolves.
    static_assert(std::is_move_constructible<PlacedObjectRecord>::value,
                  "objectRecords_.push_back(std::move(record)) requires a movable record.");
    EXPECT_TRUE(std::is_move_constructible<PlacedObjectRecord>::value);
}

// A default-constructed record is the "nothing loaded yet" state: no mesh,
// no node, no scene-graph membership, region unset. Every consumer that
// guards on `!rec.mesh` / `!rec.node` relies on these defaults being safe
// null/false/SIZE_MAX.
TEST(PlacedObjectRecordShape, DefaultsAreTheUnloadedState) {
    PlacedObjectRecord rec;
    EXPECT_EQ(rec.mesh, nullptr);
    EXPECT_EQ(rec.node, nullptr);
    EXPECT_FALSE(rec.inSceneGraph);
    EXPECT_EQ(rec.region, SIZE_MAX);
    EXPECT_TRUE(rec.name.empty());
    EXPECT_TRUE(rec.world.isIdentity());
    EXPECT_EQ(rec.position, irr::core::vector3df());
}

#else

TEST(PlacedObjectRecordSize, SkippedNoGraphics) {
    GTEST_SKIP() << "EQT_HAS_GRAPHICS not compiled in";
}

#endif
