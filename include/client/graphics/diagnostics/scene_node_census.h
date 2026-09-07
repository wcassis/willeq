#pragma once

// T443 (board B453) -- /dumpscene's "unaccounted" arithmetic, lifted out of
// DiagnosticsComponent::dumpScene() so it can be measured against a
// hand-built scene graph instead of only against a live zone.
//
// The line this backs is
//
//     OTHER: camera=..., sunLight=..., playerLight=..., unaccounted=N
//
// plus the UNACCOUNTED[i] enumeration under it. `unaccounted` is the whole
// point of the line: it is the count of scene-graph nodes the renderer holds
// that NO tracked category claims, i.e. a leak or an untracked creator. A
// category that is drawn but never subtracted makes the number lie.
//
// B453 was exactly that. Every zone-side node was subtracted -- the collision
// node, the placeholder, the camera, the two lights, the sky and weather
// sets, the region meshes, the objects, the entities -- except
// `RendererContext::zoneMeshNode`, the single-zone-mesh node
// `ZoneLifecycleComponent::createZoneMesh()` adds to the ROOT scene node
// (`addOctreeSceneNode(mesh, nullptr, -1, 256)`,
// src/client/graphics/zone_lifecycle/renderer_zone_draw.cpp). That node is
// non-null exactly in the no-BSP/PVS install arm, so `/dumpscene` in Plane of
// Sky (the one playable zone with no BSP tree and no regions -- T440) printed
// `unaccounted=1` and enumerated the zone mesh itself as an unidentified
// `octree` node. In a BSP zone the member is null and the geometry is in
// `regionMeshNodes`, which WAS subtracted, so the count read 0 there and the
// gap stayed invisible.
//
// The arithmetic below is the pre-existing arithmetic, moved: entity subtrees
// are subtracted whole (an entity plus every descendant), doors cost two
// nodes each (pivot + mesh child), the sky and weather renderers report their
// own counts and collect their own pointers, and everything else is one node.
// `known` is what the enumeration filters by, so a node that is subtracted
// but not inserted would still be listed -- the two halves are populated
// together here rather than at eleven call sites.

#include <irrlicht.h>

#include <set>
#include <vector>

namespace EQT {
namespace Graphics {

struct SceneNodeCensusInputs {
    // The scene manager whose root is walked. Null means "no scene": the
    // census is all zeroes, which is what dumpScene() reported before.
    irr::scene::ISceneManager* smgr = nullptr;

    // Nodes whose ENTIRE subtree is accounted for by one entry (entities:
    // the model node plus its attachments, name tag, equipment, ...).
    std::vector<irr::scene::ISceneNode*> subtrees;

    // Nodes worth exactly one scene-graph node each. This is where
    // zoneMeshNode belongs, alongside the collision node, the placeholder,
    // the camera, the two renderer lights, each region mesh, each object,
    // each object light and each in-graph entity light.
    std::vector<irr::scene::ISceneNode*> singles;

    // Categories that report a COUNT of their own and hand back their
    // pointers separately: the sky renderer, the weather controller, and the
    // doors (two nodes per in-graph door, of which only the pivot is a
    // pointer anything else holds).
    int countedElsewhere = 0;
    std::set<irr::scene::ISceneNode*> collected;
};

struct SceneNodeCensus {
    int total = 0;        // scene-graph nodes, excluding the root itself
    int unaccounted = 0;  // total minus every category above
    std::set<irr::scene::ISceneNode*> known;
};

// Recursive node count including `node` itself; 0 for null.
int countSceneNodes(irr::scene::ISceneNode* node);

// Null and duplicate entries in `subtrees` / `singles` are ignored, so a
// caller may pass an optional member straight through without a guard.
SceneNodeCensus computeSceneNodeCensus(const SceneNodeCensusInputs& in);

}  // namespace Graphics
}  // namespace EQT
