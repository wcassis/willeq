// Zone-load step 8 (Entities) and its 8a-8f sub-steps: base-model prep,
// variant model prep, variant textures, equipment prep, prep-result
// distribution, and entity mesh build.
//
// Split out of renderer_zone_load.cpp by T166; see that file's header.

#include "client/graphics/irrlicht_renderer.h"
#include "client/willeq_database.h"
#include "client/graphics/loading_thread.h"
#include "client/graphics/entity_prep_worker.h"
#include "client/graphics/eqe1_loader.h"
#include "client/graphics/load_mem_audit.h"
#include "client/graphics/eq/dds_decoder.h"
#include "client/graphics/eq/equipment_textures.h"
#include "client/graphics/frame_budget_governor.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/light_source.h"
#include "client/graphics/weather_effects_controller.h"
#include "client/graphics/environment/zone_biome.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/race_codes.h"
#include "client/inventory/inventory_manager.h"
#include "client/graphics/spell_visual_fx.h"
#include "client/graphics/sky_renderer.h"
#include "client/graphics/eq/sky_loader.h"
#include "client/graphics/sky_config.h"
#include "client/zone_lines.h"
#include "client/hc_map.h"
#include "client/bridge/game_state_bridge.h"
#include "client/state/event_bus.h"
#include "client/events/renderer_intents.h"
#include "common/logging.h"
#include "client/core/memory_ledger.h"
#include <fmt/format.h>
#include "common/name_utils.h"
#include "common/performance_metrics.h"
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <sstream>

#ifdef EQT_HAS_DRM
#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#include <EGL/eglext.h>
#else
#include <GL/gl.h>
#endif
#include <EGL/egl.h>
#endif
#include "client/graphics/zone_load/zone_load_component.h"
#include "client/graphics/zone_load/zone_load_context.h"

namespace EQT {
namespace Graphics {

bool ZoneLoadComponent::loadZoneStep8_Entities(ZoneLoadContext& ctx) {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::EntityInstances);  // T491
    // ── Step 8: Entities — inline prep + build (no background threads) ────
    ctx.updateProgress(80, "Preparing entity models...");
    {
        auto stepStart = std::chrono::steady_clock::now();
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8: entityRenderer_={}, skipEntityBuild={}",
                  (bool)ctx_.entityRenderer, ctx_.config.constrainedConfig.skipEntityBuild);

        if (ctx_.entityRenderer && ctx_.entityRenderer->getRaceModelLoader()
            && !ctx_.config.constrainedConfig.skipEntityBuild) {

            ctx.modelLoader = ctx_.entityRenderer->getRaceModelLoader();
            ctx.equipLoader = ctx_.entityRenderer->getEquipmentModelLoader();
            ensureEntityPrepWorkerStarted(ctx.modelLoader, ctx.equipLoader);  // T109
            const auto& entities = ctx_.entityRenderer->getEntities();
            ctx.totalEntities = entities.size();
            ctx.prepCount = 0;
            ctx.buildCount = 0;
            ctx.modelCacheHits = 0;
            ctx.modelDiskLoads = 0;

            LOG_INFO(MOD_GRAPHICS, "Sequential: preparing {} entities inline (chrCacheMaxEntries={})",
                     ctx.totalEntities, ctx_.config.constrainedConfig.chrCacheMaxEntries);

            // Collect spawn IDs (iterate copy since we modify visuals)
            std::vector<uint16_t> spawnIds;
            spawnIds.reserve(ctx.totalEntities);
            std::vector<std::pair<uint16_t, uint8_t>> raceSet;   // T504c Leg 4
            for (const auto& [spawnId, vis] : entities) {
                spawnIds.push_back(spawnId);
                const std::pair<uint16_t, uint8_t> pair{vis.raceId, vis.gender};
                if (std::find(raceSet.begin(), raceSet.end(), pair) == raceSet.end()) raceSet.push_back(pair);
            }
            // T504c Leg 4: announce the zone's race set, so the first parse of
            // each character archive below covers every race the loop will ask
            // for (one parse per archive per load instead of one per race).
            if (ctx.modelLoader) ctx.modelLoader->setPrefetchRaces(raceSet);

            for (uint16_t spawnId : spawnIds) {

                const auto& entities2 = ctx_.entityRenderer->getEntities();
                auto it = entities2.find(spawnId);
                if (it == entities2.end()) continue;
                ctx.spawnId = spawnId;
                ctx.vis = &it->second;

                // T105: per-entity working-set snapshot (LOG_DEBUG only, default-off);
                // distFromPlayer is also T109's gate input below.
                float dx = ctx.vis->lastX - ctx_.playerX, dy = ctx.vis->lastY - ctx_.playerY, dz = ctx.vis->lastZ - ctx_.playerZ;
                float distFromPlayer = std::sqrt(dx * dx + dy * dy + dz * dz);
                LOG_DEBUG(MOD_GRAPHICS, "T105 ENTITY spawn={} race={} gender={} dist={:.1f} "
                          "texture={} helm={} chestMat={} equip7={} equip8={} isNPC={} isPlayer={}",
                          ctx.spawnId, ctx.vis->raceId, ctx.vis->gender, distFromPlayer,
                          ctx.vis->appearance.texture, ctx.vis->appearance.helm,
                          ctx.vis->appearance.equipment[static_cast<uint8_t>(EquipSlot::Chest)] & 0xFF,
                          ctx.vis->appearance.equipment[7], ctx.vis->appearance.equipment[8],
                          ctx.vis->isNPC, ctx.vis->isPlayer);

                // Update progress within entity phase (80-88%)
                if (ctx.totalEntities > 0) {
                    int pct = 80 + static_cast<int>(8.0f * ctx.prepCount / ctx.totalEntities);
                    ctx.updateProgress(pct, "Preparing entities [" + std::to_string(ctx.prepCount + 1)
                                        + "/" + std::to_string(ctx.totalEntities) + "]");
                }

                // T109 load-distance gate (zone_load/renderer_entity_load_gate.cpp).
                if (deferEntityForLoadGate(ctx.spawnId, ctx.vis->name, ctx.vis->raceId, ctx.vis->gender,
                                           distFromPlayer, ctx.deferredCount, ctx.deferredRaceGenderPairs)) {
                    ++ctx.prepCount; continue;  // still "processed" for the progress bar
                }
                // T209: was this race/gender already fully prepared by an earlier
                // entity in this same loop? If so every prep sub-step below should
                // be a cache hit, and whatever time they still take is redundant.
                const bool t209repeat =
                    !ctx.preppedRaceGenderPairs.insert({ctx.vis->raceId, ctx.vis->gender}).second;
                if (t209repeat) ++ctx.repeatPairEntities;
                auto t209 = std::chrono::steady_clock::now();
                auto t209lap = [&t209]() {
                    auto now = std::chrono::steady_clock::now();
                    double ms = std::chrono::duration<double, std::milli>(now - t209).count();
                    t209 = now;
                    return ms;
                };

                // ── Inline entity prep (EntityPrepWorker::processRequest logic) ──

                if (!loadZoneStep8a_BaseModelPrep(ctx)) return false;
                { const double d = t209lap(); ctx.ms8a += d; if (t209repeat) ctx.rp8a += d; }

                // T38: a missing base model (ctx.entitySuccess == false) means
                // there is no model data for 8b (variant model)/8c (variant
                // textures)/8d (equipment — keyed off the base model's staging
                // data)/8e (distributes 8b-8d's prep results) to act on. Only
                // 8a itself and 8f (mesh build, which independently falls back
                // to a placeholder cube via EntityRenderer::getMeshForRace when
                // no model is cached) run for a skipped entity — see
                // loadZoneStep8a_BaseModelPrep for where the skip is decided
                // and counted.
                if (ctx.entitySuccess) {
                    loadZoneStep8b_VariantModelPrep(ctx);
                    { const double d = t209lap(); ctx.ms8b += d; if (t209repeat) ctx.rp8b += d; }

                    loadZoneStep8c_VariantTextures(ctx);
                    { const double d = t209lap(); ctx.ms8c += d; if (t209repeat) ctx.rp8c += d; }

                    if (!loadZoneStep8d_EquipmentPrep(ctx)) return false;
                    { const double d = t209lap(); ctx.ms8d += d; if (t209repeat) ctx.rp8d += d; }

                    loadZoneStep8e_DistributePrepResults(ctx);
                    { const double d = t209lap(); ctx.ms8e += d; if (t209repeat) ctx.rp8e += d; }
                }

                // T04: processUploadQueue removed. Textures uploaded via uploadTexture() in T09.

                // Promote prepared models so buildEntityMesh finds them
                ctx.modelLoader->promotePreparedModels();
                ctx.msPromote += t209lap();

                // Upload all pending textures (including composite EQE1 entries queued in Step 8a)
                // so they're available as GPU textures when buildEntityMesh builds composite meshes.
                // T412 (board B291): the same skipEntityTextureUpload knob
                // EntityRenderer::uploadPendingEntityTextures() honours -- these are
                // entity textures too, and skipping only the gameplay-time site would
                // have left zone-in uploading everything anyway.
                if (ctx_.constrainedTextureCache
                    && !ctx_.config.constrainedConfig.skipEntityTextureUpload) {
                    ctx_.constrainedTextureCache->uploadAllPending();
                }
                ctx.msUpload += t209lap();

                loadZoneStep8f_BuildEntityMesh(ctx);
                { const double d = t209lap(); ctx.ms8f += d; if (t209repeat) ctx.rp8f += d; }

                ++ctx.prepCount;
            }

            // T209: where Step 8's time actually goes, and how much of it is spent
            // on entities whose race/gender was already prepared by an earlier one.
            LOG_INFO(MOD_GRAPHICS,
                     "T209 Step8 split (ms): 8a_base={:.1f} 8b_variant={:.1f} "
                     "8c_vtex={:.1f} 8d_equip={:.1f} 8e_distribute={:.1f} "
                     "8f_mesh={:.1f} promote={:.1f} upload={:.1f} | {} of {} entities "
                     "were a repeat race/gender ({} distinct pairs)",
                     ctx.ms8a, ctx.ms8b, ctx.ms8c, ctx.ms8d, ctx.ms8e, ctx.ms8f,
                     ctx.msPromote, ctx.msUpload, ctx.repeatPairEntities,
                     ctx.prepCount - ctx.deferredCount, ctx.preppedRaceGenderPairs.size());
            const double t209rpTotal = ctx.rp8a + ctx.rp8b + ctx.rp8c + ctx.rp8d + ctx.rp8e + ctx.rp8f;
            const double t209total  = ctx.ms8a + ctx.ms8b + ctx.ms8c + ctx.ms8d + ctx.ms8e + ctx.ms8f;
            LOG_INFO(MOD_GRAPHICS,
                     "T209 Step8 REPEAT-ONLY (ms): 8a={:.1f} 8b={:.1f} 8c={:.1f} 8d={:.1f} "
                     "8e={:.1f} 8f={:.1f} | {:.1f} of {:.1f}ms ({}%) spent on entities whose "
                     "race/gender was already fully prepared",
                     ctx.rp8a, ctx.rp8b, ctx.rp8c, ctx.rp8d, ctx.rp8e, ctx.rp8f,
                     t209rpTotal, t209total,
                     t209total > 0 ? (int)(t209rpTotal * 100 / t209total) : 0);

            bool hasBoats = ctx_.entityRenderer->hasBoatsInZone();
            LOG_INFO(MOD_GRAPHICS, "Sequential: prepped {} entities, built {} meshes (model cache hits={}, disk loads={}, hasBoats={})",
                     ctx.prepCount, ctx.buildCount, ctx.modelCacheHits, ctx.modelDiskLoads, hasBoats);
            // T109: unconditional, same discipline as entitiesSkipped below.
            // Ships inert (radius ~unbounded) — see refactor/deferred.md's
            // T109 entry: deferred entities are NOT reliably rebuilt today.
            LOG_INFO(MOD_GRAPHICS, "T109: load-distance gate radius={:.1f} — {} in-radius ({} distinct "
                     "race/gender), {} deferred ({} distinct race/gender)",
                     ctx_.config.constrainedConfig.entityLoadRadius,
                     ctx.prepCount - ctx.deferredCount, ctx.preppedRaceGenderPairs.size(),
                     ctx.deferredCount, ctx.deferredRaceGenderPairs.size());
            if (hasBoats) {
                LOG_INFO(MOD_GRAPHICS, "Sequential: zone has boats — boat collision checks enabled");
            } else {
                LOG_INFO(MOD_GRAPHICS, "Sequential: no boats in zone — boat collision checks disabled (zero per-frame cost)");
            }

            // T38: make missing-model skips visible, not a silent omission — a zone
            // that rendered with placeholder-cube NPCs must say so plainly, loudly,
            // and every time (not just on first occurrence), so this is unconditional
            // (always logged, even when entitiesSkipped == 0, so its absence in a log
            // is itself confirmation nothing was skipped rather than evidence the
            // check didn't run).
            if (ctx.entitiesSkipped > 0) {
                std::string pairsStr;
                for (const auto& [raceId, gender] : ctx.skippedRaceGenderPairs) {
                    if (!pairsStr.empty()) pairsStr += ", ";
                    pairsStr += "race=" + std::to_string(raceId) + " gender=" + std::to_string(static_cast<int>(gender));
                }
                LOG_WARN(MOD_GRAPHICS, "Sequential: SKIPPED {} entities with no loadable base model "
                         "({} distinct race/gender pair(s): {}) — rendered as placeholder cubes, "
                         "zone load continued",
                         ctx.entitiesSkipped, ctx.skippedRaceGenderPairs.size(), pairsStr);
            } else {
                LOG_INFO(MOD_GRAPHICS, "Sequential: 0 entities skipped for missing base models — all {} entities had loadable models",
                         ctx.prepCount);
            }
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8: entity prep skipped (entityRenderer_={}, raceModelLoader={}, skipEntityBuild={})",
                      (bool)ctx_.entityRenderer,
                      ctx_.entityRenderer ? (bool)ctx_.entityRenderer->getRaceModelLoader() : false,
                      ctx_.config.constrainedConfig.skipEntityBuild);
        }
        // T09: Upload all decoded textures from Steps 8a-8d to GPU.
        // getOrLoad() decoded them; uploadAllPending() promotes DecodedEntry → CachedTexture.
        // T412 (board B291): honours skipEntityTextureUpload, as above.
        if (ctx_.constrainedTextureCache
            && !ctx_.config.constrainedConfig.skipEntityTextureUpload) {
            int uploaded = ctx_.constrainedTextureCache->uploadAllPending();
            LOG_INFO(MOD_GRAPHICS, "SEQ Step8: uploadAllPending uploaded {} entity textures", uploaded);
        }

        ctx_.entityPrepReady = true;
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8: entityPrepReady_=true");
        ctx_.ZoneStream().logAssetBuildTime("seq_entities", 0, stepStart);
        LOG_INFO(MOD_GRAPHICS, "SEQ Step8: about to exit scope");
    }
    LOG_INFO(MOD_GRAPHICS, "SEQ Step8: scope exited, flushing log...");
    FlushThreadLog();
    LOG_INFO(MOD_GRAPHICS, "SEQ: Step 8 complete, entering Step 9...");
    FlushThreadLog();
    return true;
}

bool ZoneLoadComponent::loadZoneStep8a_BaseModelPrep(ZoneLoadContext& ctx) {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::EntityModels);  // T491
    // Step 8a: Base race model prep (S3D load, WLD parse, animation merge)
    uint32_t key = (static_cast<uint32_t>(ctx.vis->raceId) << 8) | ctx.vis->gender;
    bool alreadyCached = ctx.modelLoader->isModelDataCached(key);
    // T94: a race/gender proven absent by an earlier entity's full search
    // (preloadModelData() walked every S3D source and found nothing) never
    // becomes "cached" in the positive sense above — it has no model data to
    // cache. Without this check, every subsequent entity of that race/gender
    // would redo the full search from scratch (T91 measured 130/452 entities
    // in Nektulos doing exactly this, 93.8% of Step 8's wall time) AND log
    // its own per-entity "preload ... took Nms" line. Checked here, at the
    // call site, so both costs — the search and the per-entity log line —
    // are skipped entirely, not merely made fast.
    bool knownMissing = !alreadyCached && ctx.modelLoader->isRaceModelKnownMissing(key);
    bool success = false;
    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8a: entity {} ({}) race={} gender={} cacheKey={} cached={} knownMissing={}",
              ctx.spawnId, ctx.vis->name, ctx.vis->raceId, ctx.vis->gender, key, alreadyCached, knownMissing);

    if (alreadyCached) {
        success = true;
        ++ctx.modelCacheHits;
    } else if (knownMissing) {
        // Same accounting as the "not found" branch below, minus the
        // per-entity search and log line — preloadModelData() already
        // logged this race/gender's absence once, the first time it was
        // discovered.
        ++ctx.entitiesSkipped;
        ctx.skippedRaceGenderPairs.insert({ctx.vis->raceId, ctx.vis->gender});
    } else {
        auto prepStart = std::chrono::steady_clock::now();
        success = ctx.modelLoader->preloadModelData(ctx.vis->raceId, ctx.vis->gender);
        auto prepMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - prepStart).count();
        if (success) {
            ++ctx.modelDiskLoads;
        } else {
            // T38: a single entity's missing base model is NOT a zone-fatal
            // condition (was: LOG_FATAL + loadFailed_=true + abort the whole
            // zone load — see deferred.md "race=415 missing model data still
            // crashes graphics mode"). Downgraded to WARN + skip just this
            // entity: the caller loop (loadZoneStep8_Entities) checks
            // ctx.entitySuccess and skips 8b-8e (which all depend on base
            // model data existing), but still runs 8f, whose
            // EntityRenderer::getMeshForRace() already has a designed
            // fallback to a colored placeholder cube (src/client/graphics/
            // CLAUDE.md, "Model Loading Order" step 5) — this is what lets
            // that fallback actually be reached instead of the zone dying
            // first. Counted here (not just logged) so the zone-load summary
            // can name exactly how many entities and which race/gender pairs
            // were affected — see the end of loadZoneStep8_Entities.
            LOG_WARN(MOD_GRAPHICS, "Skipping entity {} ({}): base race model not found: race={} gender={} "
                     "(not found in any S3D archive) — will render as a placeholder cube, zone load continues",
                     ctx.spawnId, ctx.vis->name, ctx.vis->raceId, ctx.vis->gender);
            ++ctx.entitiesSkipped;
            ctx.skippedRaceGenderPairs.insert({ctx.vis->raceId, ctx.vis->gender});
        }
        LOG_INFO(MOD_GRAPHICS, "Sequential: preload race={} gender={} took {}ms success={}",
                 ctx.vis->raceId, ctx.vis->gender, prepMs, success);
    }
    ctx.entitySuccess = success;
    return true;
}

void ZoneLoadComponent::loadZoneStep8b_VariantModelPrep(ZoneLoadContext& ctx) {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::EntityModels);  // T491
    // Step 8b: Variant model prep (zone-specific S3D for head/body variants)
    if (ctx.entitySuccess) {
        uint8_t headVariant = ctx.vis->appearance.helm;
        uint8_t bodyVariant = 0;
        uint8_t chestMaterial = static_cast<uint8_t>(
            ctx.vis->appearance.equipment[static_cast<uint8_t>(EquipSlot::Chest)] & 0xFF);
        if (isRobeTexture(ctx.vis->appearance.texture) || isRobeTexture(chestMaterial)) {
            bodyVariant = 1;
        }
        if (headVariant != 0 || bodyVariant != 0) {
            ctx.modelLoader->preloadVariantModel(ctx.vis->raceId, ctx.vis->gender, headVariant, bodyVariant);
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8b: entity {} ({}) preloadVariantModel head={} body={}",
                      ctx.spawnId, ctx.vis->name, headVariant, bodyVariant);
        }
    }
}

void ZoneLoadComponent::loadZoneStep8c_VariantTextures(ZoneLoadContext& ctx) {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::EntityModels);  // T491
    // Step 8c: Load variant textures via constrained cache
    // T06: Replaced inline DDS/BMP decoder with getOrLoad() calls.
    ctx.variantTextureCount = 0;
    {
        auto modelData = ctx.modelLoader->getRaceModelData(ctx.vis->raceId, ctx.vis->gender);
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8c: entity {} ({}) appearance.texture={} helm={} modelData={}",
                  ctx.spawnId, ctx.vis->name, ctx.vis->appearance.texture, ctx.vis->appearance.helm, (bool)modelData);
        if (ctx.vis->appearance.texture != 0 || ctx.vis->appearance.helm != 0) {
            std::string raceCode = RaceModelLoader::getRaceCode(ctx.vis->raceId);
            std::string lowerRaceCode = raceCode;
            std::transform(lowerRaceCode.begin(), lowerRaceCode.end(), lowerRaceCode.begin(),
                           [](unsigned char c) { return std::tolower(c); });

            std::set<std::string> loadedNames;
            auto loadVariantTexture = [&](const std::string& texName, const std::shared_ptr<TextureInfo>& texInfo) {
                if (!texInfo || texInfo->data.empty()) return;
                std::string lowerName = texName;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                if (loadedNames.count(lowerName) > 0) return;
                ctx_.constrainedTextureCache->getOrLoad(lowerName, texInfo->data,
                                                     "entity_perpart", "loadZoneStep8c_VariantTextures");
                loadedNames.insert(lowerName);
                ++ctx.variantTextureCount;
            };

            // Build variant prefixes
            std::vector<std::string> variantPrefixes;
            if (ctx.vis->appearance.texture >= 10) {
                char buf[32];
                snprintf(buf, sizeof(buf), "clk%02d", ctx.vis->appearance.texture - 10);
                variantPrefixes.push_back(buf);
            } else if (ctx.vis->appearance.texture > 0) {
                char buf[32];
                const char* slots[] = {"ch", "ua", "fa", "hn", "lg", "ft"};
                for (const char* slot : slots) {
                    snprintf(buf, sizeof(buf), "%s%s%02d", lowerRaceCode.c_str(), slot, ctx.vis->appearance.texture);
                    variantPrefixes.push_back(buf);
                }
            }
            if (ctx.vis->appearance.helm > 0) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%she%02d", lowerRaceCode.c_str(), ctx.vis->appearance.helm);
                variantPrefixes.push_back(buf);
            }

            auto searchTextures = [&](const std::map<std::string, std::shared_ptr<TextureInfo>>& textures) {
                for (const auto& [texName, texInfo] : textures) {
                    std::string ln = texName;
                    std::transform(ln.begin(), ln.end(), ln.begin(),
                                   [](unsigned char c) { return std::tolower(c); });
                    for (const auto& prefix : variantPrefixes) {
                        if (ln.find(prefix) != std::string::npos) {
                            loadVariantTexture(texName, texInfo);
                            break;
                        }
                    }
                }
            };

            if (modelData && !modelData->textures.empty())
                searchTextures(modelData->textures);

            // Search variant model textures
            uint8_t hv = ctx.vis->appearance.helm;
            uint8_t bv2 = 0;
            uint8_t cm = static_cast<uint8_t>(
                ctx.vis->appearance.equipment[static_cast<uint8_t>(EquipSlot::Chest)] & 0xFF);
            if (isRobeTexture(ctx.vis->appearance.texture) || isRobeTexture(cm))
                bv2 = 1;
            if (hv != 0 || bv2 != 0) {
                auto variantModelData = ctx.modelLoader->getVariantModelData(
                    ctx.vis->raceId, ctx.vis->gender, hv, bv2);
                if (variantModelData && !variantModelData->textures.empty())
                    searchTextures(variantModelData->textures);
            }
        }
    }

    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8c: entity {} ({}) loaded {} variant textures via cache",
              ctx.spawnId, ctx.vis->name, ctx.variantTextureCount);
}

bool ZoneLoadComponent::loadZoneStep8d_EquipmentPrep(ZoneLoadContext& ctx) {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::Equipment);  // T491
    // Step 8d: Equipment model extraction + texture decode via constrained cache
    // T07: Replaced inline DDS decoder with getOrLoad() calls.
    using EquipPrepData = ZoneLoadContext::EquipPrepData;
    ctx.equipmentData.clear();

    bool equipError = false;
    if (ctx.equipLoader) {
        uint32_t primaryId = ctx.vis->appearance.equipment[7];
        uint32_t secondaryId = ctx.vis->appearance.equipment[8];

        auto prepOneEquip = [&](uint32_t equipmentId, bool isPrimary) {
            if (equipError) return;
            if (equipmentId == 0) return;
            // T498 (board B755/B823): equipment[] is uniformly a MATERIAL /
            // IT model number.
            int modelId = static_cast<int>(equipmentId);
            const EquipmentModelLoader::EquipmentModelRef* modelRef = ctx.equipLoader->getModelRef(modelId);
            if (!modelRef) {
                // P10b (borrowed verbatim from task/anim-video-capture's
                // already-documented, already-validated workaround, applied
                // here because this pre-existing FATAL blocked P10b's own
                // befallen verification matrix in this environment — a
                // single NPC's missing equipment-model data-completeness gap
                // (e.g. a giant rat with a primary-weapon modelId not present
                // in item_models.json) previously aborted the ENTIRE zone
                // load. Not root-caused here either (out of scope for P10b;
                // item_models.json coverage is a separate data-completeness
                // question) — degraded to WARN + skip this one equipment
                // slot (entity renders unequipped for that slot).
                LOG_WARN(MOD_GRAPHICS, "Skipping equipment model ref: entity {} ({}) equipment {} ({}) modelId={} (not found in item_models.json) — entity will render unequipped for this slot",
                          ctx.spawnId, ctx.vis->name, equipmentId, isPrimary ? "primary" : "secondary", modelId);
                return;
            }
            auto equipData2 = ctx.equipLoader->extractEquipmentModelCached(*modelRef, modelId);  // T504
            if (!equipData2) {
                LOG_WARN(MOD_GRAPHICS, "Skipping equipment model: entity {} ({}) equipment {} ({}) modelId={} — entity will render unequipped for this slot",
                          ctx.spawnId, ctx.vis->name, equipmentId, isPrimary ? "primary" : "secondary", modelId);
                return;
            }

            EquipPrepData prepData;
            prepData.modelId = modelId;
            prepData.equipmentId = equipmentId;
            prepData.isPrimary = isPrimary;
            prepData.geometry = equipData2->geometry;
            prepData.rawTextures = equipData2->textures;

            // Load equipment textures into constrained cache
            for (const auto& texName : equipData2->textureNames) {
                std::string ln = texName;
                std::transform(ln.begin(), ln.end(), ln.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                auto texIt = equipData2->textures.find(ln);
                if (texIt == equipData2->textures.end() || !texIt->second) continue;
                const auto& texInfo = texIt->second;
                if (texInfo->data.empty()) continue;

                ctx_.constrainedTextureCache->getOrLoad(ln, texInfo->data,
                                                     "entity_perpart", "loadZoneStep8d_EquipmentPrep");
            }
            ctx.equipmentData.push_back(std::move(prepData));
        };

        prepOneEquip(primaryId, true);
        prepOneEquip(secondaryId, false);
        LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8d: entity {} ({}) equipment: primary={} secondary={} extracted={}",
                  ctx.spawnId, ctx.vis->name, ctx.vis->appearance.equipment[7], ctx.vis->appearance.equipment[8], ctx.equipmentData.size());
    }

    if (equipError) {
        pendingZoneComputations_.reset();
        loadFailed_ = true;
        return false;
    }
    return true;
}

void ZoneLoadComponent::loadZoneStep8e_DistributePrepResults(ZoneLoadContext& ctx) {
    // Step 8e: Distribute prep results to EntityVisual
    // (equivalent to pollAndDistributePrepResults inline)
    {
        // We need a mutable reference — use const_cast since we own the renderer
        auto& mutableEntities = const_cast<std::map<uint16_t, EntityVisual>&>(ctx_.entityRenderer->getEntities());
        auto visIt = mutableEntities.find(ctx.spawnId);
        if (visIt == mutableEntities.end()) {
            LOG_WARN(MOD_GRAPHICS, "SEQ Step8e: entity {} ({}) vanished before distribution — prep work discarded "
                     "(variantTex={}, equipData={})",
                     ctx.spawnId, ctx.vis->name, ctx.variantTextureCount, ctx.equipmentData.size());
        } else {
            auto& mvis = visIt->second;
            // T06: variantTextures no longer stored on EntityVisual — textures are in constrained cache.

            for (auto& eq : ctx.equipmentData) {
                EntityVisual::EquipmentStaging staging;
                staging.modelId = eq.modelId;
                staging.equipmentId = eq.equipmentId;
                staging.isPrimary = eq.isPrimary;
                staging.geometry = eq.geometry;
                staging.rawTextures = eq.rawTextures;
                mvis.equipmentStaging.push_back(std::move(staging));
            }

            // Textures already in constrained cache from T05 (base) + T06 (variant) + T07 (equipment).
            mvis.entityPrepComplete = true;
            LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8e: entity {} ({}) distributed: equipStaging={}",
                      ctx.spawnId, ctx.vis->name, mvis.equipmentStaging.size());
        }
    }
}

void ZoneLoadComponent::loadZoneStep8f_BuildEntityMesh(ZoneLoadContext& ctx) {
    // Step 8f: Build entity mesh (GL work — scene node, materials, name tag)
    bool meshBuilt = ctx_.entityRenderer->buildEntityMesh(ctx.spawnId);
    if (meshBuilt) ++ctx.buildCount;
    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8f: entity {} ({}) buildEntityMesh result={}", ctx.spawnId, ctx.vis->name, meshBuilt);
}

} // namespace Graphics
} // namespace EQT
