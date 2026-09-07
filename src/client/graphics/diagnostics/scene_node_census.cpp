// T443 (board B453) -- see the header for what this backs and why it moved.

#include "client/graphics/diagnostics/scene_node_census.h"

namespace EQT {
namespace Graphics {

int countSceneNodes(irr::scene::ISceneNode* node) {
    if (!node) return 0;
    int count = 1;
    for (auto* child : node->getChildren()) count += countSceneNodes(child);
    return count;
}

SceneNodeCensus computeSceneNodeCensus(const SceneNodeCensusInputs& in) {
    SceneNodeCensus out;
    if (!in.smgr) return out;

    // -1 for the root itself, which is not a node the renderer created.
    out.total = countSceneNodes(in.smgr->getRootSceneNode()) - 1;

    int remaining = out.total;

    // An entity is its model node plus every descendant it owns (name tag,
    // equipment, attachments), so the whole subtree comes off at once.
    for (auto* n : in.subtrees) {
        if (!n) continue;
        if (!out.known.insert(n).second) continue;
        remaining -= countSceneNodes(n);
    }

    // One node each.
    for (auto* n : in.singles) {
        if (!n) continue;
        if (!out.known.insert(n).second) continue;
        remaining--;
    }

    // Categories that count themselves (sky, weather, the two nodes per door).
    remaining -= in.countedElsewhere;
    out.known.insert(in.collected.begin(), in.collected.end());

    out.unaccounted = remaining;
    return out;
}

}  // namespace Graphics
}  // namespace EQT
