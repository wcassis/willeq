// B1096 (#416) -- the EQE1 name->archive resolution, installed EARLY.
//
// See the header for the why: the player's race model (the paperdoll) is
// built on the render thread BEFORE loadZoneStep0_EQE1 opens the zone's
// packs and registers the resolver, so a variant texture whose owner is not
// willeq.eqe1 hit the all-or-nothing FATAL (getOrLoad's "EQE1 is configured
// but texture not found" branch).

#include "client/graphics/eqe1/eqe1_early_resolution.h"

#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eqe1/eqe1_archive_component.h"
#include "client/data_paths.h"
#include "common/logging.h"

#include <string>

namespace EQT {
namespace Graphics {

void initializeEQE1EarlyResolution(IrrlichtRenderer& renderer,
                                   ConstrainedTextureCache* cache) {
    const auto& config = renderer.Context().config;
    if (config.constrainedConfig.eqe1Path.empty()) return;  // no EQE1 tree
    if (!cache) return;

    // T207's index, loaded once for the process. The zone Step0 keeps its
    // own call -- its isLoaded() guard makes the second load a no-op, and
    // keeping it there is the zone-load path's existing behaviour.
    if (!renderer.Eqe1().eqe1Index().isLoaded()) {
        if (!renderer.Eqe1().eqe1Index().load(eqt::ResolveDataPath("data/eqe1_texture_index.json"))) {
            LOG_ERROR(MOD_GRAPHICS,
                      "EQE1: no data/eqe1_texture_index.json -- EQE1 lookups "
                      "will resolve nothing. Rebuild it with "
                      "eqe1_texture_index_builder --eqe1-path {}",
                      config.constrainedConfig.eqe1Path);
        }
    }

    // The same resolver Step0 registers: a lookup miss resolves to the ONE
    // archive that holds the name and opens it (getOrOpenEQE1 registers it
    // with the cache). A name no archive holds still resolves to nothing --
    // the genuine asset gap stays fatal under decision #2.
    cache->setEQE1ArchiveResolver(
        [&renderer](const std::string& name) { return renderer.Eqe1().openArchiveContaining(name); });

    LOG_DEBUG(MOD_GRAPHICS, "EQE1: early name->archive resolution installed (pre-zone); "
              "the index answers a miss with at most one open");
}

} // namespace Graphics
} // namespace EQT
