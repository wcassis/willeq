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

#include <fmt/core.h>

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
    if (impostorTargetId_ != targetId) {
        // B1071: the pin's input edge, at the frame where it moves. Silent
        // whenever the target is unchanged; loud exactly when it is not.
        LOG_INFO(MOD_GRAPHICS, "[impostor] target-id frame={} {} -> {} (loading={})",
                 impostorFrame_, impostorTargetId_, targetId, loading ? "true" : "false");
    }
    impostorTargetId_ = targetId;
    impostorGroupNames_ = std::move(groupMemberNames);
    impostorLoading_ = loading;
}

void EntityRenderer::prepareImpostorCache() {
    if (!impostorsEnabled()) return;
    if (!impostors_) {
        const size_t budget = impostorTestBudget_ > 0 ? impostorTestBudget_
                                                      : constrainedConfig_->impostorTextureMemoryBytes;
        impostors_ = std::make_unique<ImpostorCache>(driver_, smgr_, budget);
        if (!impostors_->usable())
            LOG_WARN(MOD_GRAPHICS, "Impostor: cache unusable (driver or budget) -- every entity stays a model");
        return;
    }
    impostors_->prepare();
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
    if (impostorLoading_ || !cam) {
        impostorLastFrameT_ = {};  // a loading screen is not a frame interval
        return;
    }

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
    {
        // B1071 (GitLab #391): the pin's decision context, and the current
        // target's DECISION PATH, after the tier decision (fresh tiers, post
        // force-mode override), logged only when something changes (silent
        // whenever the target is a pinned full-model candidate -- the healthy
        // case; loud exactly when the target's path changes, with WHY: not
        // spawned yet, or ineligible, with the failing flag).
        //   cand=true  tier=full     -- healthy: tiered candidate kept full
        //   cand=false inScene=0      -- worker culled it (inScene=false); the
        //                                pin draws it full anyway -- healthy
        //                                post-fix, was INVISIBLE pre-fix
        //                                (B1064's solid-hull still)
        //   cand=true  tier=IMPOSTOR -- a policy violation (plan() checks
        //                                pinned before the tier)
        //   cand=false shown=0        -- a pin violation (nothing drawn)
        if (impostorTargetId_ != 0) {
            bool inCands = false, pinnedInCands = false, full = true;
            float d = -1.0f;
            for (size_t i = 0; i < impostorCands_.size(); ++i)
                if (impostorCands_[i].spawnId == impostorTargetId_) {
                    inCands = true; pinnedInCands = impostorCands_[i].pinned;
                    d = std::sqrt(impostorCands_[i].distSq);
                    full = impostorTiers_[i] == Tier::Full;
                }
            auto ev = entities_.find(impostorTargetId_);
            bool shown = (ev != entities_.end() && ev->second.sceneNode &&
                          ev->second.sceneNode->isVisible());
            bool inScene = (ev != entities_.end() && ev->second.inSceneGraph);
            std::string detail;
            if (!inCands) {
                detail = ev == entities_.end() ? "NOT spawned"
                    : fmt::format("ineligible inScene={} meshBuilt={} animNode={} phase={} "
                                  "player={} corpse={} fading={} placeholder={} collision={}",
                                  (ev->second.inSceneGraph ? 1 : 0), ev->second.meshBuilt,
                                  (ev->second.animatedNode != nullptr ? 1 : 0),
                                  (int)ev->second.buildPhase, ev->second.isPlayer, ev->second.isCorpse,
                                  ev->second.isFading, ev->second.usesPlaceholder, ev->second.hasCollision);
            } else if (!pinnedInCands) {
                detail = "IN_CANDS_NOT_PINNED";
            }
            const char* state = inCands ? (full ? "full" : "IMPOSTOR")
                                        : (shown ? (inScene ? "shown" : "culled-full") : "NOT-DRAWN");
            const bool viol = (inCands && !full) || (!inCands && !shown);
            const bool changed = viol ||
                                 lastImpostorTargetState_ != state ||
                                 impostorTargetId_ != lastImpostorTargetId_;
            if (changed) {
                LOG_INFO(MOD_GRAPHICS,
                         "[impostor] target frame={} id={} cand={} pinned={} tier={} shown={} inScene={} d={:.1f} limit={} -- {}",
                         frame, impostorTargetId_, inCands, pinnedInCands, state, shown, inScene, d,
                         impostorPolicy_.fullLimit(), detail);
                lastImpostorTargetState_ = state;
                lastImpostorTargetId_ = impostorTargetId_;
            }
            if (inCands && !full)
                LOG_INFO(MOD_GRAPHICS, "[impostor] PIN-BUT-IMPOSTOR frame={} id={} dist={:.1f} limit={}",
                         frame, impostorTargetId_, d, impostorPolicy_.fullLimit());
        } else {
            lastImpostorTargetState_ = nullptr;
            lastImpostorTargetId_ = 0;
        }
    }

    ImpostorFrameStats fs;
    fs.candidates = static_cast<int>(impostorCands_.size());
    fs.fullLimit = impostorPolicy_.fullLimit();
    for (size_t i = 0; i < impostorCands_.size(); ++i) {
        ImpostorSpawnState& st = impostorSpawns_[impostorCands_[i].spawnId];
        st.lastSeenFrame = frame;
        const bool want = impostorTiers_[i] == Tier::Impostor;
        if (want && !st.wantsImpostor && st.hasKey) {
            // Tier flip back to the impostor: re-capture if the view angle or
            // the pose (the animation playing) changed since the capture;
            // until then the old capture is drawn (a flip never shows a gap).
            const ImpostorCapture* cap = cache.captureFor(st.key);
            const EntityVisual& v = entities_.at(impostorCands_[i].spawnId);
            const irr::core::vector3df p = v.sceneNode->getPosition();
            const float ang = relativeViewAngleDeg(camPos.X, camPos.Z, p.X, p.Z, v.sceneNode->getRotation().Y);
            const bool stale = !cap || !cap->captured || needsViewRefresh(cap->viewAngleDeg, ang) ||
                               cap->pose != v.animatedNode->getCurrentAnimation();
            if (st.shared && cache.cells().freeCells() + (cache.cells().maxPages() - cache.cells().pages()) *
                                                             kCellsPerPage > 0) {
                // A shared spawn asks for its own cell again when there is room.
                st.hasKey = false;
                st.shared = false;
                st.needsCapture = true;
                st.reason = ImpostorSpawnState::Reason::Create;
            } else if (stale && !st.shared) {
                st.needsCapture = true;
                st.reason = ImpostorSpawnState::Reason::Flip;
            } else {
                cache.stats().flipReused++;
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
            // Creations first (the entity is drawn as nothing or a model
            // until then); refreshes of a drawn impostor after them.
            const bool creation = !st.hasKey || st.reason == ImpostorSpawnState::Reason::Create;
            work.push_back({c.distSq + (creation ? 0.0f : 1e12f), c.spawnId});
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
    // B979: the B973 scheduler is the shared gate for capture units (kind
    // Impostor, recorded below). It learns the capture's real cost per kind
    // and defers a capture that would not fit the frame's slice, so a slow
    // first-draw (30-62 ms, B964) can never be joined by a second capture in
    // the same frame. The pass passes its OWN frame accounting because it
    // runs inside the frame after the build slice (and tools that drive the
    // pass directly never open a scheduler frame at all). The per-pass
    // budget (ub) stays on top: two independent caps, neither weaker than
    // the other.
    EntityBuildScheduler& sched = buildScheduler_;
    // Measurement switch (refactor/B964/README.md): WILLEQ_IMPOSTOR_FINISH=1
    // finishes the GPU before the units, so a unit's time is its own work and
    // not the previous frame's deferred rasterization (llvmpipe flushes it at
    // the first FBO read). Off by default: a finish costs CPU/GPU overlap.
    static const bool finishFirst = [] {
        const char* e = std::getenv("WILLEQ_IMPOSTOR_FINISH");
        return e && e[0] == '1';
    }();
    if (finishFirst && !work.empty()) cache.finishGpu();
    const auto frameT0 = std::chrono::steady_clock::now();
    auto& cs = cache.stats();
    // A unit starts only if the time already spent plus the measured average
    // unit fits the frame's capture budget (the first unit of a frame always
    // runs): checking only the time already spent let 2-3 units of 15-20 ms
    // land in one Pi frame (53 ms, growthplane, refactor/B964/README.md).
    const double avgUnitMs = cs.units ? cs.unitMsSum / static_cast<double>(cs.units) : 0.0;
    for (const Work& w : work) {
        const double spent = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameT0).count();
        if (fs.unitsThisFrame >= ub.maxUnits || spent >= ub.maxMs) break;
        if (fs.unitsThisFrame > 0 && spent + avgUnitMs > ub.maxMs) break;
        if (!sched.mayStartCaptureUnit(static_cast<float>(spent), fs.unitsThisFrame)) break;
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
        sched.recordUnit(EntityBuildUnitKind::Impostor, static_cast<float>(ums));
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
    // The visibility cap the simulation worker applies was raised by the cell
    // capacity (impostorVisibilityAllowance), so the old cap is re-applied
    // HERE to everything that is not drawn as an impostor: walking every
    // visible entity, pinned first and then nearest first, the first
    // maxVisibleEntities that are drawn as models (full-tier candidates,
    // candidates whose impostor is not ready yet, and entities that can never
    // be impostors -- placeholders, corpses, boats) are shown and the rest are
    // not drawn, exactly as the cap culled them before B964. Impostors take no
    // slot. Measured on the Pi before this rule (qeynos2, 1280x720, a crowd of
    // 200 still streaming in as placeholders): 20 fps against 27 with
    // impostors off, because the raised cap let every placeholder draw.
    struct Item {
        float distSq;
        uint16_t id;
        int cand;     // index into impostorCands_, or -1
        bool pinned;
    };
    std::vector<Item> items;
    items.reserve(entities_.size());
    for (size_t i = 0; i < impostorCands_.size(); ++i)
        items.push_back({impostorCands_[i].distSq, impostorCands_[i].spawnId, static_cast<int>(i),
                         impostorCands_[i].pinned});
    for (auto& [id, v] : entities_) {
        if (eligible(v) || !v.sceneNode || !v.inSceneGraph || v.isPlayer) continue;
        const bool pinned = id == impostorTargetId_;
        items.push_back({(v.sceneNode->getPosition() - camPos).getLengthSQ(), id, -1, pinned});
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.pinned != b.pinned) return a.pinned;
        return a.distSq != b.distSq ? a.distSq < b.distSq : a.id < b.id;
    });
    const int maxFull = impostorPolicy_.config().maxFull;
    int fullDrawn = 0;
    for (const Item& it : items) {
        EntityVisual& v = entities_.at(it.id);
        ImpostorSpawnState& st = impostorSpawns_[it.id];
        if (it.cand < 0) {
            // Never an impostor: the old cap decides.
            const bool show = it.pinned || fullDrawn < maxFull;
            if (show) {
                if (st.capHidden) { v.sceneNode->setVisible(true); st.capHidden = false; }
                ++fullDrawn;
            } else if (!st.capHidden) {
                v.sceneNode->setVisible(false);
                st.capHidden = true;
            }
            continue;
        }
        if (st.capHidden) { v.sceneNode->setVisible(true); st.capHidden = false; }
        const ImpostorCapture* cap = st.hasKey ? cache.captureFor(st.key) : nullptr;
        const bool fresh = !(st.needsCapture && st.reason == ImpostorSpawnState::Reason::Create);
        const bool ready = cap && cap->captured && fresh;
        const bool drawImpostor = st.wantsImpostor && ready;
        // Not an impostor (yet): a model while the slots last, else not drawn.
        const bool hide = !drawImpostor && !it.pinned && fullDrawn >= maxFull;
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
        } else {
            ++fs.capHidden;
        }
    }
    for (const Item& it : items)
        if (it.cand < 0 && impostorSpawns_[it.id].capHidden) ++fs.capHidden;
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
                 "cap_hidden={} refresh_flip={} flip_reused={} demotions={} shared_claims={} cap_full={} units={} unit_ms_avg={:.2f} "
                 "unit_ms_max={:.2f} frame_unit_ms_max={:.2f} frame_units_max={} draws={} incomplete_fbo={}",
                 frame, fs.candidates, fs.tierFull, fs.tierImpostor, fs.wantImpostor, fs.fullLimit,
                 stateName(gstate), avgMs, cache.cells().pages(), cache.cells().cellsUsed(),
                 cache.cells().uniqueCount(), cache.cells().sharedCount(), cache.rowBytes(), cs.created,
                 cs.refreshAppearance, cs.refreshAngle, fs.capHidden, cs.refreshFlip, cs.flipReused, cs.demotions, cs.sharedClaims, cs.capFull,
                 cs.units, cs.units ? cs.unitMsSum / cs.units : 0.0, cs.unitMsMax, cs.frameUnitMsMax,
                 cs.frameUnitsMax, fs.drawCalls, cs.incompleteFbo);
    }
}

}  // namespace Graphics
}  // namespace EQT
