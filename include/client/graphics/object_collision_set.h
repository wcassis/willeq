// object_collision_set.h -- placed objects' collision, each distinct mesh held
// ONCE.
//
// B909b (GitLab #229). Movement collision and the ground query test the placed
// objects (trees, plants, rocks, furniture) of the BSP region(s) a ray touches.
// Until B909b they read std::map<size_t, std::vector<triangle3df>>, built at
// zone load by running an Irrlicht triangle selector over EVERY object node and
// appending its world-space triangles: 36 B per triangle PER INSTANCE. The
// instances share their mesh (T504 builds one IMesh per distinct geometry), so
// that was the same mesh copied once per placement -- 40.2 MB in growthplane on
// the Orange Pi (refactor/B909b/README.md has the measured numbers).
//
// Now the set holds
//   * each distinct mesh ONCE, in object space, read in place from the mesh the
//     draw path already holds (its vertex and index arrays are charged to the
//     `objects` ledger category and stay there: the block has two claimants,
//     object drawing and object collision, and is counted once). The set grabs
//     the mesh so it outlives any node that drops it;
//   * per instance: the world matrix the selector used, a world AABB and the
//     mesh slot, grouped by BSP region (sorted, no per-region node).
// A vertex-ANIMATED object (flag, banner, and in Kunark/Velious zones the
// swaying grass and plants: 4,314 of growthplane's 4,333 placements) has a
// private mesh whose vertices are rewritten every frame; for it the set keeps
// an object-space snapshot taken at the point the old copy was taken, so its
// collision is the same frozen load-time pose it always was. Snapshots are
// shared by CONTENT: every placement of one animated geometry has the same
// load-time vertices, so growthplane's 4,314 snapshots are 4.
//
// Identical results. A ray against a region skips every instance whose world
// AABB (inflated, conservative) its segment cannot reach, then transforms the
// remaining instances' vertices with the SAME matrix4::transformVect the
// selector applied and runs the SAME Moller-Trumbore step, instances in
// placement order and triangles in mesh order. The triangles tested are
// therefore bit-for-bit the old copy's, in the old order, and a skipped
// instance could not have produced a hit (test_object_collision_set).
#ifndef EQT_GRAPHICS_OBJECT_COLLISION_SET_H
#define EQT_GRAPHICS_OBJECT_COLLISION_SET_H

#include <irrlicht.h>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace EQT {
namespace Graphics {

// The matrix CTriangleSelector::getTriangles() applies to a node's triangles:
// identity, then `*=` the node's absolute transformation. Computed the way the
// selector computes it, so the product has the same bits.
inline irr::core::matrix4 SelectorWorldMatrix(const irr::core::matrix4& absolute) {
    irr::core::matrix4 mat;
    mat *= absolute;
    return mat;
}

class ObjectCollisionSet {
public:
    ObjectCollisionSet() = default;
    ~ObjectCollisionSet();
    ObjectCollisionSet(const ObjectCollisionSet&) = delete;
    ObjectCollisionSet& operator=(const ObjectCollisionSet&) = delete;

    // Add one placed instance, in placement order. `world` is the node's
    // absolute transformation. `snapshot` = copy the mesh's object-space
    // triangles now instead of reading the mesh later (vertex-animated
    // meshes). Returns false (nothing added) for a mesh with no triangle.
    bool addInstance(size_t bspRegion, irr::scene::IMesh* mesh,
                     const irr::core::matrix4& world, bool snapshot);
    // Group the instances by region (stable: placement order within a region).
    // Call once after the last addInstance(); lookups before it see nothing.
    void finish();
    void clear();

    bool empty() const { return instances_.empty(); }
    bool hasRegion(size_t bspRegion) const;
    size_t regionCount() const { return regions_.size(); }
    size_t instanceCount() const { return instances_.size(); }
    size_t meshCount() const { return meshes_.size(); }
    // Triangles the per-instance world-space copy held (sum over instances).
    size_t worldTriangleCount() const { return worldTriangles_; }
    // Triangles of the distinct meshes (each counted once).
    size_t meshTriangleCount() const;
    // Heap bytes this set owns (instances, region index, mesh table, snapshots).
    size_t ownedBytes() const;
    // Bytes of the shared mesh vertex+index arrays the set reads in place
    // (owned and counted by the `objects` category, NOT by this set).
    size_t sharedMeshBytes() const;

    // The Moller-Trumbore test over every object triangle of `bspRegion`, with
    // RayIntersectTriangles' contract: nearest hit with t in (EPS, rayLen+1),
    // hitDistSq = |hitPoint - origin|^2. Returns false (outputs untouched)
    // on a miss.
    bool rayIntersect(size_t bspRegion, const irr::core::vector3df& rayOrigin,
                      const irr::core::vector3df& rayDir, float rayLen,
                      irr::core::vector3df& hitPoint,
                      irr::core::triangle3df& hitTriangle, float& hitDistSq) const;

private:
    // One index run of a mesh: `indexCount` u16 indices into a vertex array
    // with `stride` bytes per vertex, position first (every Irrlicht vertex
    // type). indices == nullptr means sequential (a snapshot).
    struct Run {
        const uint8_t* vertices = nullptr;
        const uint16_t* indices = nullptr;
        uint32_t stride = 0;
        uint32_t indexCount = 0;
    };
    struct Mesh {
        irr::scene::IMesh* mesh = nullptr;   // grabbed; null for a snapshot
        uint32_t firstRun = 0;
        uint32_t runCount = 0;
        uint32_t triangles = 0;
        irr::core::aabbox3df objectBox;       // over the triangles' vertices
        std::vector<irr::core::vector3df> snapshot;   // 3 per triangle
        uint64_t snapshotHash = 0;            // FNV-1a of the snapshot's bytes
    };
    struct Instance {
        irr::core::matrix4 world;
        irr::core::aabbox3df worldBox;        // inflated, conservative
        size_t region = 0;
        uint32_t mesh = 0;
    };

    uint32_t meshSlot(irr::scene::IMesh* mesh, bool snapshot);

    std::vector<Mesh> meshes_;
    std::vector<Run> runs_;
    std::vector<Instance> instances_;
    // (region, first instance), ascending by region; the region's instances
    // run to the next entry's first (or the end).
    std::vector<std::pair<size_t, uint32_t>> regions_;
    size_t worldTriangles_ = 0;
    bool finished_ = false;
};

}  // namespace Graphics
}  // namespace EQT

#endif
