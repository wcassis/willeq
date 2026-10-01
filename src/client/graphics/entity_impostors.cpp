// B964 (GitLab #282): EntityRenderer's impostor pass -- the tier decision, the
// governor-scheduled capture units and the billboard batch. Runs once per
// frame from updateMainThreadEntityState() (frame phase 3), i.e. before
// beginScene(): every FBO switch a capture makes happens outside the frame.
//
// Pieces: impostor_policy.h (the rules, device-free), impostor_cache.h (the
// atlas pages, the scratch and the capture), impostor_batch_node.h (one draw
// per page). Design and measurements: refactor/B964/README.md.

#include "client/graphics/entity_renderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

#include "client/core/baseline_mode.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/frame_budget_governor.h"
#include "client/graphics/impostor/impostor_batch_node.h"
#include "client/graphics/impostor/impostor_cache.h"
#include "common/logging.h"

namespace EQT {
namespace Graphics {

using namespace impostor;

namespace {

GovernorState governorState(const FrameBudgetGovernor* g) {
    if (!g) return GovernorState::Green;
    switch (g->getState()) {
        case BudgetState::Red: return GovernorState::Red;
        case BudgetState::Yellow: return GovernorState::Yellow;
        case BudgetState::Green:
        default: return GovernorState::Green;
    }
}

const char* stateName(GovernorState s) {
    return s == GovernorState::Red ? "red" : s == GovernorState::Yellow ? "yellow" : "green";
}

bool eligible(const EntityVisual& v) {
    return v.sceneNode && v.inSceneGraph && v.meshBuilt && v.animatedNode && v.isAnimated &&
           v.buildPhase == EntityBuildPhase::Built && !v.isPlayer && !v.isCorpse && !v.isFading &&
           !v.usesPlaceholder && !v.hasCollision;
}

}  // namespace

void EntityRenderer::setImpostorFrameInputs(const FrameBudgetGovernor* governor, uint16_t targetId,
                                            std::vector<std::string> groupMemberNames, bool loading) {
    impostorGovernor_ = governor;
    impostorTargetId_ = targetId;
    impostorGroupNames_ = std::move(groupMemberNames);
    impostorLoading_ = loading;
}

bool EntityRenderer::impostorsEnabled() const {
    if (impostorTestBudget_ > 0) return true;
    // A non-original LOD feature: the baseline draws every character as its model.
    if (eqt::BaselineOnly()) return false;
    // A/B measurement switch (refactor/B964/README.md): WILLEQ_IMPOSTORS=0
    // turns the feature off and nothing else, unlike WILLEQ_BASELINE=1.
    static const bool abOff = [] {
        const char* e = std::getenv("WILLEQ_IMPOSTORS");
        return e && e[0] == '0';
    }();
    if (abOff) return false;
    return constrainedConfig_ && constrainedConfig_->impostorTextureMemoryBytes > 0;
}

int EntityRenderer::impostorVisibilityAllowance() const {
    if (!impostorsEnabled()) return 0;
    const size_t budget = impostorTestBudget_ > 0 ? impostorTestBudget_
                                                  : constrainedConfig_->impostorTextureMemoryBytes;
    return maxPagesForBudget(budget) * kCellsPerPage;
}

void EntityRenderer::enableImpostorsForTesting(size_t budgetBytes, float tierDistance) {
    impostorTestBudget_ = budgetBytes;
    impostorTestTierDistance_ = tierDistance;
}

bool EntityRenderer::isEntityDrawnAsImpostor(uint16_t spawnId) const {
    auto it = impostorSpawns_.find(spawnId);
    return it != impostorSpawns_.end() && it->second.drawnAsImpostor;
}

void EntityRenderer::impostorRestoreFull(EntityVisual& visual, ImpostorSpawnState& st) {
    if (visual.animatedNode) {
        visual.animatedNode->setWithheldFromRenderList(false);
        if (visual.animatedNode->isSkinningSuspended()) {
            visual.animatedNode->setSkinningSuspended(false);
            visual.animatedNode->forceAnimationUpdate();
        }
    }
    st.drawnAsImpostor = false;
}

void EntityRenderer::impostorOnEntityRemoved(uint16_t spawnId) {
    auto it = impostorSpawns_.find(spawnId);
    if (it == impostorSpawns_.end()) return;
    ImpostorSpawnState& st = it->second;
    if (impostorRetainOnRemove_) {
        // updateEntityAppearance() rebuilds the entity: keep the cell, draw
        // the old capture until the refresh unit re-captures the new look.
        // A shared spawn is re-keyed (its texture variant may have changed).
        st.needsCapture = true;
        st.reason = ImpostorSpawnState::Reason::Appearance;
        st.drawnAsImpostor = false;  // the rebuilt node starts un-withheld
        if (st.shared) st.hasKey = false;
        return;
    }
    if (impostors_ && st.hasKey && !st.shared) impostors_->releaseCell(st.key);
    impostorSpawns_.erase(it);
}

void EntityRenderer::updateImpostors() {
    if (!impostorsEnabled()) return;
    if (!impostors_) {
        const size_t budget = impostorTestBudget_ > 0 ? impostorTestBudget_
                                                      : constrainedConfig_->impostorTextureMemoryBytes;
        impostors_ = std::make_unique<ImpostorCache>(driver_, smgr_, budget);
        if (!impostors_->usable())
            LOG_WARN(MOD_GRAPHICS, "Impostor: cache unusable (driver or budget) -- every entity stays a model");
    }
    if (!impostors_->usable()) return;
    ImpostorCache& cache = *impostors_;
    if (cache.batchNode() && !cache.batchNode()->getParent())
        smgr_->getRootSceneNode()->addChild(cache.batchNode());
    cache.beginBatch();
    irr::scene::ICameraSceneNode* cam = smgr_->getActiveCamera();
    if (impostorLoading_ || !cam) return;

    ++impostorFrame_;
    const uint32_t frame = impostorFrame_;
    {
        // Frame time as this pass sees it: the interval between two passes.
        const auto t = std::chrono::steady_clock::now();
        if (impostorLastFrameT_.time_since_epoch().count() != 0)
            impostorFrameMs_.push_back(
                static_cast<float>(std::chrono::duration<double, std::milli>(t - impostorLastFrameT_).count()));
        impostorLastFrameT_ = t;
    }
    const irr::core::vector3df camPos = cam->getPosition();
    const GovernorState gstate = governorState(impostorGovernor_);
    const float targetMs = impostorGovernor_ ? impostorGovernor_->getTargetFrameTimeMs() : 33.3f;
    const float avgMs = impostorGovernor_ ? impostorGovernor_->getAverageFrameTimeMs() : 0.0f;

    TierPolicyConfig pcfg;
    pcfg.tierDistance = impostorTestTierDistance_ >= 0.0f
                            ? impostorTestTierDistance_
                            : (constrainedConfig_ ? constrainedConfig_->impostorTierDistance : 80.0f);
    pcfg.maxFull = constrainedConfig_ ? constrainedConfig_->maxVisibleEntities : 40;
    impostorPolicy_.configure(pcfg);

    // ---- 1. The tier decision over this frame's visible candidates ----
    impostorCands_.clear();
    for (auto& [id, v] : entities_) {
        if (!eligible(v)) continue;
        TierCandidate c;
        c.spawnId = id;
        c.distSq = (v.sceneNode->getPosition() - camPos).getLengthSQ();
        c.pinned = id == impostorTargetId_ ||
                   std::find(impostorGroupNames_.begin(), impostorGroupNames_.end(), v.name) !=
                       impostorGroupNames_.end();
        impostorCands_.push_back(c);
    }
    impostorPolicy_.plan(impostorCands_, gstate, avgMs, targetMs, impostorTiers_);
    if (impostorForceMode_ != 0)
        for (auto& t : impostorTiers_) t = impostorForceMode_ == 1 ? Tier::Impostor : Tier::Full;

    ImpostorFrameStats fs;
    fs.candidates = static_cast<int>(impostorCands_.size());
    fs.fullLimit = impostorPolicy_.fullLimit();
    for (size_t i = 0; i < impostorCands_.size(); ++i) {
        ImpostorSpawnState& st = impostorSpawns_[impostorCands_[i].spawnId];
        st.lastSeenFrame = frame;
        const bool want = impostorTiers_[i] == Tier::Impostor;
        if (want && !st.wantsImpostor && st.hasKey) {
            // Tier flip: re-capture at the current angle and pose before it shows.
            st.needsCapture = true;
            st.reason = ImpostorSpawnState::Reason::Flip;
            // A shared spawn asks for its own cell again when there is room.
            if (st.shared && cache.cells().freeCells() + (cache.cells().maxPages() - cache.cells().pages()) *
                                                             kCellsPerPage > 0) {
                st.hasKey = false;
                st.shared = false;
            }
        }
        st.wantsImpostor = want;
        if (want) ++fs.wantImpostor;
    }
    // Spawns that left the candidate set (culled, a corpse, rebuilt): models again.
    for (auto& [id, st] : impostorSpawns_) {
        if (st.lastSeenFrame == frame) continue;
        st.wantsImpostor = false;
        if (st.drawnAsImpostor) {
            auto e = entities_.find(id);
            if (e != entities_.end()) impostorRestoreFull(e->second, st);
            else st.drawnAsImpostor = false;
        }
    }

    // ---- 2. Capture units: creation, flips, appearance and angle refreshes ----
    struct Work { float distSq; uint16_t id; };
    std::vector<Work> work;
    for (const auto& c : impostorCands_) {
        ImpostorSpawnState& st = impostorSpawns_[c.spawnId];
        if (!st.wantsImpostor) continue;
        if (!st.hasKey || st.needsCapture) {
            work.push_back({c.distSq, c.spawnId});
            continue;
        }
        if (!st.shared) {
            const ImpostorCapture* cap = cache.captureFor(st.key);
            const EntityVisual& v = entities_.at(c.spawnId);
            const irr::core::vector3df p = v.sceneNode->getPosition();
            const float ang = relativeViewAngleDeg(camPos.X, camPos.Z, p.X, p.Z, v.sceneNode->getRotation().Y);
            if (cap && cap->captured && needsViewRefresh(cap->viewAngleDeg, ang)) {
                st.needsCapture = true;
                st.reason = ImpostorSpawnState::Reason::Angle;
                work.push_back({c.distSq + 1e12f, c.spawnId});  // after every creation
            }
        }
    }
    std::sort(work.begin(), work.end(), [](const Work& a, const Work& b) {
        return a.distSq != b.distSq ? a.distSq < b.distSq : a.id < b.id;
    });
    const UnitBudget ub = unitBudgetFor(gstate, targetMs);
    const auto frameT0 = std::chrono::steady_clock::now();
    auto& cs = cache.stats();
    for (const Work& w : work) {
        const double spent = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameT0).count();
        if (fs.unitsThisFrame >= ub.maxUnits || spent >= ub.maxMs) break;
        auto eit = entities_.find(w.id);
        if (eit == entities_.end()) continue;
        EntityVisual& v = eit->second;
        ImpostorSpawnState& st = impostorSpawns_[w.id];

        // Key: unique while the cap admits it, else the shared race/gender/texture cell.
        if (!st.hasKey) {
            bool isNew = false;
            const uint64_t uk = uniqueKey(w.id);
            if (!st.shared && cache.acquireCell(uk, frame, isNew)) {
                st.key = uk;
                st.hasKey = true;
                st.needsCapture = true;
                if (st.reason == ImpostorSpawnState::Reason::None) st.reason = ImpostorSpawnState::Reason::Create;
            } else {
                const uint64_t sk = sharedKey(v.raceId, v.gender, v.appearance.texture);
                const ImpostorCapture* existing = cache.captureFor(sk);
                if (existing && existing->captured) {
                    st.key = sk;
                    st.hasKey = true;
                    st.shared = true;
                    st.needsCapture = false;
                    st.reason = ImpostorSpawnState::Reason::None;
                    cs.sharedClaims++;
                    continue;  // no GPU work: it draws the existing shared cell
                }
                if (!cache.acquireCell(sk, frame, isNew)) {
                    // No cell anywhere: take the least-recently-drawn unique one.
                    uint64_t victim = 0;
                    if (!cache.lruUnique(victim)) continue;
                    cache.releaseCell(victim);
                    cs.demotions++;
                    auto vs = impostorSpawns_.find(static_cast<uint16_t>(victim));
                    if (vs != impostorSpawns_.end()) {
                        vs->second.hasKey = false;
                        vs->second.shared = true;
                        vs->second.needsCapture = true;
                        vs->second.reason = ImpostorSpawnState::Reason::Create;
                        auto ve = entities_.find(vs->first);
                        if (vs->second.drawnAsImpostor && ve != entities_.end())
                            impostorRestoreFull(ve->second, vs->second);
                    }
                    if (!cache.acquireCell(sk, frame, isNew)) continue;
                }
                st.key = sk;
                st.hasKey = true;
                st.shared = true;
                st.needsCapture = true;
            }
        }

        // The unit: one capture.
        const irr::core::vector3df p = v.sceneNode->getPosition();
        const float ang = relativeViewAngleDeg(camPos.X, camPos.Z, p.X, p.Z, v.sceneNode->getRotation().Y);
        std::vector<irr::scene::IMeshSceneNode*> held;
        if (v.primaryEquipNode) held.push_back(v.primaryEquipNode);
        if (v.secondaryEquipNode) held.push_back(v.secondaryEquipNode);
        const bool hadCapture = cache.captureFor(st.key) && cache.captureFor(st.key)->captured;
        const auto u0 = std::chrono::steady_clock::now();
        const bool ok = cache.capture(st.key, v.animatedNode, held, camPos, ang, w.id);
        const double ums = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - u0).count();
        ++fs.unitsThisFrame;
        cs.units++;
        cs.unitMsSum += ums;
        cs.unitMsMax = std::max(cs.unitMsMax, ums);
        if (!ok) continue;
        switch (st.reason) {
            case ImpostorSpawnState::Reason::Appearance: cs.refreshAppearance++; break;
            case ImpostorSpawnState::Reason::Angle: cs.refreshAngle++; break;
            case ImpostorSpawnState::Reason::Flip:
                if (hadCapture) cs.refreshFlip++;
                else cs.created++;
                break;
            default: cs.created++; break;
        }
        st.needsCapture = false;
        st.reason = ImpostorSpawnState::Reason::None;
    }
    fs.unitMsThisFrame = static_cast<float>(
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameT0).count());
    if (fs.unitsThisFrame > 0) {
        cs.frameUnitMsMax = std::max(cs.frameUnitMsMax, static_cast<double>(fs.unitMsThisFrame));
        cs.frameUnitsMax = std::max(cs.frameUnitsMax, fs.unitsThisFrame);
    }

    // ---- 3. Draw state: withhold the impostored models, batch their billboards ----
    // An entity that wants its impostor but has none yet stays a model while
    // the full-model slots last; past them it is not drawn until its impostor
    // exists (before B964 the visibility cap culled it outright).
    int fullDrawn = 0;
    for (size_t i = 0; i < impostorCands_.size(); ++i) {
        const uint16_t id = impostorCands_[i].spawnId;
        EntityVisual& v = entities_.at(id);
        ImpostorSpawnState& st = impostorSpawns_[id];
        const ImpostorCapture* cap = st.hasKey ? cache.captureFor(st.key) : nullptr;
        const bool fresh = !(st.needsCapture && (st.reason == ImpostorSpawnState::Reason::Create ||
                                                 st.reason == ImpostorSpawnState::Reason::Flip));
        const bool ready = cap && cap->captured && fresh;
        bool drawImpostor = st.wantsImpostor && ready;
        bool hide = false;
        if (st.wantsImpostor && !ready) {
            if (fullDrawn >= impostorPolicy_.config().maxFull) hide = true;
        }
        if (drawImpostor || hide) {
            if (!st.drawnAsImpostor) {
                v.animatedNode->setWithheldFromRenderList(true);
                v.animatedNode->setSkinningSuspended(true);
                st.drawnAsImpostor = true;
            }
        } else if (st.drawnAsImpostor) {
            impostorRestoreFull(v, st);
        }
        if (drawImpostor) {
            cache.touch(st.key, frame);
            cache.addBillboard(st.key, v.sceneNode->getPosition(), !st.shared);
            if (v.nameNode && cache.batchNode()) cache.batchNode()->addNameNode(v.nameNode);
            ++fs.tierImpostor;
        } else if (!hide) {
            ++fs.tierFull;
            ++fullDrawn;
        }
    }
    fs.drawCalls = cache.batchNode() ? cache.batchNode()->drawCallsLastRender() : 0;

    // Shared cells no spawn draws any more go back on the free list.
    if (frame % 30 == 0) {
        for (uint64_t k : cache.sharedKeys()) {
            bool used = false;
            for (const auto& [id, st] : impostorSpawns_)
                if (st.hasKey && st.key == k) { used = true; break; }
            if (!used) cache.releaseCell(k);
        }
    }

    impostorFrameStats_ = fs;
    if (frame % 30 == 1 || fs.unitsThisFrame > 0) cache.publishRow(fs.tierImpostor, fs.tierFull, fs.tierImpostor);

    const auto now = std::chrono::steady_clock::now();
    if (now - impostorLastLog_ >= std::chrono::seconds(5)) {
        impostorLastLog_ = now;
        float p50 = 0.0f, p95 = 0.0f, pmax = 0.0f;
        if (!impostorFrameMs_.empty()) {
            std::sort(impostorFrameMs_.begin(), impostorFrameMs_.end());
            const size_t n = impostorFrameMs_.size();
            p50 = impostorFrameMs_[(n - 1) / 2];
            p95 = impostorFrameMs_[std::min(n - 1, (n * 95 + 99) / 100 - 1)];
            pmax = impostorFrameMs_.back();
        }
        LOG_INFO(MOD_GRAPHICS, "[impostor-frames] frame={} n={} frame_p50_ms={:.2f} frame_p95_ms={:.2f} frame_max_ms={:.2f}",
                 frame, impostorFrameMs_.size(), p50, p95, pmax);
        impostorFrameMs_.clear();
        LOG_INFO(MOD_GRAPHICS,
                 "[impostor] frame={} candidates={} full={} impostor={} want={} limit={} gov={} avg_ms={:.1f} "
                 "pages={} cells={} unique={} shared={} row={} created={} refresh_appearance={} refresh_angle={} "
                 "refresh_flip={} demotions={} shared_claims={} cap_full={} units={} unit_ms_avg={:.2f} "
                 "unit_ms_max={:.2f} frame_unit_ms_max={:.2f} frame_units_max={} draws={} incomplete_fbo={}",
                 frame, fs.candidates, fs.tierFull, fs.tierImpostor, fs.wantImpostor, fs.fullLimit,
                 stateName(gstate), avgMs, cache.cells().pages(), cache.cells().cellsUsed(),
                 cache.cells().uniqueCount(), cache.cells().sharedCount(), cache.rowBytes(), cs.created,
                 cs.refreshAppearance, cs.refreshAngle, cs.refreshFlip, cs.demotions, cs.sharedClaims, cs.capFull,
                 cs.units, cs.units ? cs.unitMsSum / cs.units : 0.0, cs.unitMsMax, cs.frameUnitMsMax,
                 cs.frameUnitsMax, fs.drawCalls, cs.incompleteFbo);
    }
}

}  // namespace Graphics
}  // namespace EQT
