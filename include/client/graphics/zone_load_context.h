// ZoneLoadContext -- the state that crosses loadZoneSequential()'s step
// boundaries.
//
// T166 moved this here from renderer_zone_load.cpp. It was a file-local
// definition when every step method lived in one 2996-line translation unit;
// splitting that file across four TUs means all four need the complete type,
// so it becomes a private header rather than staying a file-local detail.
// The header still only forward-declares it (irrlicht_renderer.h:1316) --
// nothing outside the zone-load TUs may include this.

#pragma once

#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/zone_geometry.h"

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace EQT {
namespace Graphics {

// ── ZoneLoadContext ─────────────────────────────────────────────────────────
// Holds every local that crosses a loadZoneSequential() step boundary (G01
// decomposition). Defined here (not in the header) since it is purely an
// implementation detail of the step methods; the header only forward-declares
// it so the private step-method declarations can take it by reference.
struct IrrlichtRenderer::ZoneLoadContext {
    // Setup-scope locals used across many steps.
    std::string eqPath;
    std::chrono::steady_clock::time_point totalStart;
    std::function<void(int, const std::string&)> updateProgress;

    // Step 8 (Entities) per-entity-loop state. These are set up once before the
    // loop (modelLoader/equipLoader/totalEntities/*Count accumulators) or once
    // per iteration (spawnId/vis/entitySuccess/variantTextureCount/equipmentData)
    // and threaded through the 8a-8f sub-step calls that make up one iteration.
    RaceModelLoader* modelLoader = nullptr;
    EquipmentModelLoader* equipLoader = nullptr;
    size_t totalEntities = 0;
    size_t prepCount = 0;
    size_t buildCount = 0;
    size_t modelCacheHits = 0;
    size_t modelDiskLoads = 0;

    uint16_t spawnId = 0;
    const EntityVisual* vis = nullptr;
    bool entitySuccess = false;       // set by 8a, consumed by 8b-8e guards
    size_t variantTextureCount = 0;   // set by 8c, consumed by 8e (warn log)

    // T38: entities skipped because their base model could not be loaded
    // (e.g. race=415 absent from race_models). Accumulated across the whole
    // entity loop, not reset per-iteration; consumed by loadZoneStep8_Entities'
    // end-of-zone summary line. A missing model is now a per-entity WARN +
    // skip, not a zone-fatal LOG_FATAL — see loadZoneStep8a_BaseModelPrep.
    size_t entitiesSkipped = 0;
    std::set<std::pair<uint16_t, uint8_t>> skippedRaceGenderPairs;  // {raceId, gender}

    size_t deferredCount = 0;  // T109 distance-gate deferrals (vs. entitiesSkipped's prepped-but-no-model)
    std::set<std::pair<uint16_t, uint8_t>> deferredRaceGenderPairs, preppedRaceGenderPairs;

    struct EquipPrepData {
        int modelId = 0;
        uint32_t equipmentId = 0;
        bool isPrimary = true;
        std::shared_ptr<ZoneGeometry> geometry;
        std::map<std::string, std::shared_ptr<TextureInfo>> rawTextures;
    };
    std::vector<EquipPrepData> equipmentData;  // set by 8d, consumed by 8e
};

}  // namespace Graphics
}  // namespace EQT
