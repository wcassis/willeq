// ZoneLoadContext -- the state that crosses loadZoneSequential()'s step
// boundaries.
//
// T166 moved this here from renderer_zone_load.cpp. It was a file-local
// definition when every step method lived in one 2996-line translation unit;
// splitting that file across four TUs means all four need the complete type,
// so it becomes a private header rather than staying a file-local detail.
// The component header still only forward-declares it -- nothing outside the
// zone-load TUs may include this.
//
// L10 (Batch L): ZoneLoadContext moved with the family into
// ZoneLoadComponent. It is the parameter type of all twenty step methods, so
// under batch correction 10 a nested type named in a moving SIGNATURE follows
// those signatures; leaving it on IrrlichtRenderer would force
// zone_load_component.h to name IrrlichtRenderer::ZoneLoadContext.

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
struct ZoneLoadComponent::ZoneLoadContext {
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

    // T209: per-sub-step wall time inside the Step 8 entity loop, plus how many
    // entities reached each sub-step with everything already prepared for their
    // race/gender. Step 8 is ~70% of zone-in on BOTH platforms (x86 2793ms, Pi
    // 24842ms) for 226 entities drawn from only 14 distinct race/gender pairs --
    // so the question this answers is how much of that is work redone for an
    // entity whose model was already fully prepared.
    double ms8a = 0, ms8b = 0, ms8c = 0, ms8d = 0, ms8e = 0, ms8f = 0, msPromote = 0, msUpload = 0;
    // Same totals but only for entities whose race/gender a previous entity in
    // this loop already prepared -- i.e. the portion that is repeat work.
    double rp8a = 0, rp8b = 0, rp8c = 0, rp8d = 0, rp8e = 0, rp8f = 0;
    size_t repeatPairEntities = 0;   // entity whose race/gender was already prepped

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
