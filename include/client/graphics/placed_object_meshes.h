// placed_object_meshes.h -- placed objects hold ONE mesh per geometry, and the
// keyframe vertex animation (fragment 0x37) that some of them play.
//
// B936 (GitLab #254). T504 shared one IMesh per ZoneGeometry among a zone's
// placed objects, except the vertex-ANIMATED ones (flags, banners, and in the
// Kunark/Velious zones the swaying grass and plants), which each got a private
// mesh "because their vertices are rewritten every frame": growthplane had
// 4,314 private meshes of 4 geometries, 47.1 MB of `objects` on x86.
//
// That exclusion bought nothing. The animation is recorded KEYFRAMES
// (frames[k].positions, one delayMs per geometry, no interpolation), and every
// placement of a geometry shows the same keyframe at every instant: the
// simulation worker starts every counter at frame 0 with 0 ms elapsed
// (SimulationWorker::updateVertexAnimData) and advances them all by the same
// delta (computeVertexAnimations). So the private copies held the same bytes
// at all times. Now ONE mesh per geometry is posed once per keyframe change
// and every placement's scene node draws it with its own transform.
//
// PlacedObjectMeshes is the load-time half (loadZoneStep6_Objects and
// gles2_zone_benchmark --objects); applyVertexAnimFrame() is the ONE routine
// that poses a mesh (the client's per-frame apply and the benchmark's fixed
// keyframe), so the two pose with the same arithmetic.
#ifndef EQT_GRAPHICS_PLACED_OBJECT_MESHES_H
#define EQT_GRAPHICS_PLACED_OBJECT_MESHES_H

#include <irrlicht.h>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace EQT {
namespace Graphics {

struct ZoneGeometry;
struct MeshAnimatedVertices;

// A keyframe-animated object mesh (flags, banners, Kunark/Velious grass and
// plants). B936: ONE entry per MESH, which every placement of the geometry
// draws (this header's comment says why that is exact); `placements` counts
// them. The pose routine is applyVertexAnimFrame().
struct VertexAnimatedMesh {
    irr::scene::IMesh* mesh = nullptr;
    size_t placements = 0;   // scene nodes drawing this mesh
    std::shared_ptr<MeshAnimatedVertices> animData;
    int currentFrame = 0;
    std::string objectName;  // first placement's name, for the log
    // Mapping from mesh buffer vertex index to animation vertex index
    // Indexed by [bufferIndex][vertexIndex] -> animationVertexIndex
    std::vector<std::vector<size_t>> vertexMapping;
    // Center offset (EQ coords) - animation frames are relative to center,
    // but mesh vertices have center baked in. Add this to animation positions.
    float centerOffsetX = 0.0f;
    float centerOffsetY = 0.0f;
    float centerOffsetZ = 0.0f;
};

// True when `geom` carries a keyframe animation the renderer plays (two or
// more frames).
bool isVertexAnimatedGeometry(const ZoneGeometry& geom);

// Fill `vam`'s binding for `mesh`, which was built from `geom`: the animation
// data, the centre offset (geometry vertices have the mesh centre baked in,
// keyframe positions are raw) and, per mesh-buffer vertex, the keyframe vertex
// it takes its position from (matched by position, EQ -> Irrlicht x,z,y).
// Returns the number of mesh vertices mapped. Does not touch vam.mesh,
// vam.placements or vam.objectName.
size_t bindVertexAnimation(const ZoneGeometry& geom, irr::scene::IMesh* mesh,
                           VertexAnimatedMesh& vam);

// Pose vam.mesh at keyframe `frame` (vam.currentFrame is set): every mapped
// vertex gets frames[frame] + the centre offset, and each buffer is marked
// dirty. Out-of-range frames and unmapped vertices are left untouched.
void applyVertexAnimFrame(VertexAnimatedMesh& vam, int frame);

// The per-zone mesh table of the placed objects.
//
// share == true (the default everywhere): acquire() builds a geometry's mesh on
// its first placement and hands the same mesh to every later placement, animated
// or not; registerAnimated() adds ONE VertexAnimatedMesh per mesh and counts the
// placements drawing it. share == false (WILLEQ_SHARE_OBJECT_MESHES=0) is the
// pre-T504 layout -- a mesh and a registration per placement -- kept only so
// the same binary can render both for a frame-for-frame comparison.
class PlacedObjectMeshes {
public:
    explicit PlacedObjectMeshes(bool share) : share_(share) {}
    ~PlacedObjectMeshes() { release(); }
    PlacedObjectMeshes(const PlacedObjectMeshes&) = delete;
    PlacedObjectMeshes& operator=(const PlacedObjectMeshes&) = delete;

    // The mesh for one placement of `geom`, carrying ONE reference for the
    // caller (drop it when the placement's node holds its own). `build` is
    // called only when no mesh is cached for `geom`; a null result is
    // returned as is and not cached.
    irr::scene::IMesh* acquire(const ZoneGeometry& geom,
                               const std::function<irr::scene::IMesh*()>& build);

    // Record that a placement of the animated geometry `geom` draws `mesh`.
    // Appends a bound VertexAnimatedMesh to `out` the first time `mesh` is
    // seen and returns it; returns nullptr (and counts the placement on the
    // existing entry) otherwise. `mapped` receives the mapped vertex count of
    // a new entry.
    VertexAnimatedMesh* registerAnimated(const ZoneGeometry& geom, irr::scene::IMesh* mesh,
                                         const std::string& name,
                                         std::vector<VertexAnimatedMesh>& out,
                                         size_t* mapped = nullptr);

    // Drop the table's own references (the placements' nodes keep theirs).
    void release();

    bool sharing() const { return share_; }
    size_t meshesBuilt() const { return built_; }
    size_t placementsReusingAMesh() const { return reused_; }
    size_t animatedPlacements() const { return animatedPlacements_; }

private:
    bool share_;
    std::map<const ZoneGeometry*, irr::scene::IMesh*> byGeometry_;   // one reference each
    std::map<const irr::scene::IMesh*, size_t> animatedIndex_;       // -> index in `out`
    size_t built_ = 0;
    size_t reused_ = 0;
    size_t animatedPlacements_ = 0;
};

}  // namespace Graphics
}  // namespace EQT

#endif
