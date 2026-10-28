// EQAnimatedMeshSceneNode -- lifecycle, per-frame animation and render.
//
// T451: split out of the 1,287-line
// src/client/graphics/eq/animated_mesh_scene_node.cpp. Bodies are contiguous
// line ranges of that file, moved verbatim.

#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/draw_census.h"  // B997: per-site 3D-draw census
#include "common/simd_detect.h"
#include "common/logging.h"
#include <atomic>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <set>

#include "client/graphics/eq/animated_mesh_skinning.h"

namespace EQT {
namespace Graphics {

EQAnimatedMeshSceneNode::EQAnimatedMeshSceneNode(
    EQAnimatedMesh* mesh,
    irr::scene::ISceneNode* parent,
    irr::scene::ISceneManager* mgr,
    irr::s32 id,
    const irr::core::vector3df& position,
    const irr::core::vector3df& rotation,
    const irr::core::vector3df& scale)
    : IAnimatedMeshSceneNode(parent, mgr, id, position, rotation, scale)
    , eqMesh_(mesh)
    , sceneManager_(mgr)
    , instanceMesh_(nullptr)
    , lastTimeMs_(0)
    , animationSpeed_(1.0f)
    , startFrame_(0)
    , endFrame_(0)
    , looping_(true)
    , readOnlyMaterials_(false)
    , animationEndCallback_(nullptr) {

    if (eqMesh_) {
        eqMesh_->grab();

        // Set up per-instance animator with shared skeleton
        auto skeleton = eqMesh_->getSkeleton();
        if (skeleton) {
            animator_.setSkeleton(skeleton);
            LOG_DEBUG(MOD_GRAPHICS, "EQAnimatedMeshSceneNode: Set skeleton model='{}' animations={}",
                      skeleton->modelCode, skeleton->animations.size());
        } else {
            LOG_WARN(MOD_GRAPHICS, "EQAnimatedMeshSceneNode: No skeleton from mesh!");
        }

        // Create per-instance mesh copy for animation
        createInstanceMesh();

        // Copy materials from the instance mesh
        if (instanceMesh_) {
            for (irr::u32 i = 0; i < instanceMesh_->getMeshBufferCount(); ++i) {
                irr::scene::IMeshBuffer* mb = instanceMesh_->getMeshBuffer(i);
                if (mb) {
                    materials_.push_back(mb->getMaterial());
                }
            }
        }

        endFrame_ = static_cast<irr::s32>(eqMesh_->getFrameCount()) - 1;
    }
}

EQAnimatedMeshSceneNode::~EQAnimatedMeshSceneNode() {
    if (instanceMesh_) {
        instanceMesh_->drop();
    }
    if (eqMesh_) {
        eqMesh_->drop();
    }
    if (animationEndCallback_) {
        animationEndCallback_->drop();
    }
}

void EQAnimatedMeshSceneNode::createInstanceMesh() {
    if (instanceMesh_) {
        instanceMesh_->drop();
        instanceMesh_ = nullptr;
    }

    if (!eqMesh_) {
        return;
    }

    irr::scene::IMesh* baseMesh = eqMesh_->getBaseMesh();
    if (!baseMesh) {
        return;
    }

    // Create a new mesh by copying all mesh buffers from the base mesh
    instanceMesh_ = new irr::scene::SMesh();

    for (irr::u32 i = 0; i < baseMesh->getMeshBufferCount(); ++i) {
        irr::scene::IMeshBuffer* srcBuffer = baseMesh->getMeshBuffer(i);
        if (!srcBuffer) continue;

        // Create a new mesh buffer with copied data
        irr::scene::SMeshBuffer* dstBuffer = new irr::scene::SMeshBuffer();

        // Copy material
        dstBuffer->Material = srcBuffer->getMaterial();

        // Copy vertices (assuming S3DVertex format)
        irr::video::S3DVertex* srcVerts = static_cast<irr::video::S3DVertex*>(srcBuffer->getVertices());
        irr::u32 vertCount = srcBuffer->getVertexCount();

        dstBuffer->Vertices.reallocate(vertCount);
        for (irr::u32 v = 0; v < vertCount; ++v) {
            dstBuffer->Vertices.push_back(srcVerts[v]);
        }

        // Copy indices
        irr::u16* srcIndices = srcBuffer->getIndices();
        irr::u32 indexCount = srcBuffer->getIndexCount();

        dstBuffer->Indices.reallocate(indexCount);
        for (irr::u32 idx = 0; idx < indexCount; ++idx) {
            dstBuffer->Indices.push_back(srcIndices[idx]);
        }

        dstBuffer->recalculateBoundingBox();
        instanceMesh_->addMeshBuffer(dstBuffer);
        dstBuffer->drop();
    }

    instanceMesh_->recalculateBoundingBox();
    boundingBox_ = instanceMesh_->getBoundingBox();

    // T504 (load-step audit): how many per-instance mesh copies exist and what
    // they weigh. Cumulative (copies are not subtracted on destruction), logged
    // every 25th copy so one zone-in leaves a handful of lines.
    {
        static std::atomic<uint64_t> copies{0}, vertBytes{0}, indexBytes{0}, buffers{0};
        uint64_t v = 0, ix = 0;
        for (irr::u32 i = 0; i < instanceMesh_->getMeshBufferCount(); ++i) {
            irr::scene::IMeshBuffer* mb = instanceMesh_->getMeshBuffer(i);
            v += static_cast<uint64_t>(mb->getVertexCount()) * sizeof(irr::video::S3DVertex);
            ix += static_cast<uint64_t>(mb->getIndexCount()) * sizeof(irr::u16);
        }
        vertBytes += v;
        indexBytes += ix;
        buffers += instanceMesh_->getMeshBufferCount();
        const uint64_t n = ++copies;
        if (n % 25 == 0) {
            LOG_INFO(MOD_GRAPHICS, "[instmesh] copies={} mesh_buffers={} vertex_bytes={} index_bytes={} avg_per_copy={}",
                     n, buffers.load(), vertBytes.load(), indexBytes.load(),
                     (vertBytes.load() + indexBytes.load()) / n);
        }
    }
}

void EQAnimatedMeshSceneNode::applyAnimation() {
    if (!instanceMesh_ || !eqMesh_) {
        return;
    }

    const auto& vertexPieces = eqMesh_->getVertexPieces();
    const auto& originalVertices = eqMesh_->getOriginalVertices();
    const auto& vertexMapping = eqMesh_->getVertexMapping();

    if (vertexPieces.empty()) {
        return;
    }

    const auto& boneStates = animator_.getBoneStates();
    if (boneStates.empty()) {
        return;
    }

    // Performance optimization: skip if animation frame hasn't changed
    int currentFrame = animator_.getCurrentFrame();
    irr::core::vector3df nodeRot = getRotation();

    if (isPlayerNode_) {
        // For player: also check rotation since we bake it into vertices
        bool frameChanged = (currentFrame != lastAppliedFrame_);
        bool rotChanged = (std::abs(nodeRot.Y - lastAppliedRotY_) > 0.1f);
        if (!frameChanged && !rotChanged) {
            return;  // Nothing changed, skip vertex transforms
        }
    } else {
        // For NPCs: only check frame (rotation handled by node transform)
        if (currentFrame == lastAppliedFrame_) {
            return;  // Frame unchanged, skip vertex transforms
        }
    }

    // Calculate rotation for player vertex baking
    // FIX: Enable vertex baking for player to compensate for VIEW/WORLD rotation cancellation.
    // When camera follows player, VIEW (camera orientation) and WORLD (model rotation) are both
    // based on the same heading, causing them to cancel in ETS_CURRENT = VIEW * WORLD.
    // Pre-baking rotation into vertices (+90° offset) ensures the model's back faces the camera.
    float cosRot = 1.0f;
    float sinRot = 0.0f;
    if (isPlayerNode_) {
        float rotRadians = (nodeRot.Y + 90.0f) * 3.14159f / 180.0f;
        cosRot = std::cos(rotRadians);
        sinRot = std::sin(rotRadians);
    }

    // Use shared helper for vertex transformation
    applyBoneTransformsToMeshImpl(
        instanceMesh_,
        boneStates,
        vertexPieces,
        originalVertices,
        vertexMapping,
        eqMesh_->getBaseMesh(),
        cosRot,
        sinRot
    );

    boundingBox_ = instanceMesh_->getBoundingBox();

    // Update tracking for frame caching optimization
    lastAppliedFrame_ = currentFrame;
    lastAppliedRotY_ = nodeRot.Y;
}

void EQAnimatedMeshSceneNode::OnRegisterSceneNode() {
    // T309: THE FIRST-PERSON DRAW RULE'S ONE SEAM.
    //
    // withheldFromRenderList_ skips the DRAW and nothing else. It does not
    // touch IsVisible, deliberately: Irrlicht gates OnAnimate() on IsVisible,
    // and the draw rule needs this skeleton to keep running while it is
    // hidden so an attack animation is at the right phase the instant it
    // becomes visible (see first_person_body_draw.h; T296 learned this the
    // hard way). OnAnimate() is driven from ISceneManager::drawAll()'s
    // separate animation pass, not from registration, so a withheld node
    // still animates, still updates its absolute transform, and still drives
    // its children's transforms.
    //
    // The child recursion goes with it: the rule draws the whole model WITH
    // its equipped weapon, or none of it, and the equipment nodes are
    // ordinary children of this node.
    //
    // T300's note that "the player node registers like any other node in
    // first person, the clamp does the hiding" was wrong about the mechanism
    // -- T301 Leg B and T307 both refuted the clamp -- and is superseded here.
    if (withheldFromRenderList_) {
        return;
    }
    if (IsVisible) {
        SceneManager->registerNodeForRendering(this);
    }
    ISceneNode::OnRegisterSceneNode();
}

void EQAnimatedMeshSceneNode::render() {
    if (!instanceMesh_ || !SceneManager) return;

    irr::video::IVideoDriver* driver = SceneManager->getVideoDriver();
    if (!driver) return;

    // Set transformation
    // For player: position + uniform scale (B1176) -- rotation stays out of
    // the matrix since it is baked into the vertices; for NPCs the full
    // AbsoluteTransformation.
    if (isPlayerNode_) {
        // B1176 (#495): the client scales the local player's model like every
        // other actor's (FUN_0047fa9e then FUN_0043c84b on both EQPlayer build
        // paths, eqgame.exe.c:106152->:106154 and :106406->:106416), so the
        // world matrix carries the node's uniform scale (set by
        // entityNodeScale, B1172's clientActorScale). The rotation remains
        // baked into the vertices (applyAnimation), and the equipment children
        // inherit the same scale through their absolute transforms.
        irr::core::matrix4 world;
        world.setTranslation(AbsoluteTransformation.getTranslation());
        irr::core::matrix4 modelScale;
        modelScale.setScale(AbsoluteTransformation.getScale());
        world *= modelScale;
        driver->setTransform(irr::video::ETS_WORLD, world);
    } else {
        driver->setTransform(irr::video::ETS_WORLD, AbsoluteTransformation);
    }

    // Render each mesh buffer from our per-instance mesh
    {
        // B997: count this entity's 3D client-array draws for the [drawsite]
        // census (the generic draw3d:client pool; entity bodies are not in the
        // driver's HWBufferMap, so they take the client-array fallback path).
        long drawnHere = 0;
        for (irr::u32 i = 0; i < instanceMesh_->getMeshBufferCount(); ++i) {
            irr::scene::IMeshBuffer* mb = instanceMesh_->getMeshBuffer(i);
            if (!mb) continue;

            // Set material
            if (i < materials_.size() && !readOnlyMaterials_) {
                driver->setMaterial(materials_[i]);
            } else {
                driver->setMaterial(mb->getMaterial());
            }

            // Draw the mesh buffer
            driver->drawMeshBuffer(mb);
            ++drawnHere;
        }
        drawCensusAddEntity(drawnHere);
    }

    // Debug: draw bounding box if enabled
    if (DebugDataVisible & irr::scene::EDS_BBOX) {
        irr::video::SMaterial debugMat;
        debugMat.Lighting = false;
        driver->setMaterial(debugMat);
        driver->draw3DBox(boundingBox_, irr::video::SColor(255, 255, 255, 255));
    }
}

const irr::core::aabbox3d<irr::f32>& EQAnimatedMeshSceneNode::getBoundingBox() const {
    return boundingBox_;
}

irr::video::SMaterial& EQAnimatedMeshSceneNode::getMaterial(irr::u32 i) {
    if (i < materials_.size()) {
        return materials_[i];
    }
    return materials_.empty() ? ISceneNode::getMaterial(0) : materials_[0];
}

irr::u32 EQAnimatedMeshSceneNode::getMaterialCount() const {
    return static_cast<irr::u32>(materials_.size());
}

void EQAnimatedMeshSceneNode::OnAnimate(irr::u32 timeMs) {
    if (lastTimeMs_ == 0) {
        lastTimeMs_ = timeMs;
    }

    // Calculate delta time
    irr::u32 deltaMs = timeMs - lastTimeMs_;
    lastTimeMs_ = timeMs;

    // Update our per-instance animator
    if (deltaMs > 0) {
        // Apply animation speed
        float adjustedDelta = static_cast<float>(deltaMs) * animationSpeed_;
        animator_.update(adjustedDelta);

        // Check for animation end
        if (animator_.getState() == AnimationState::Stopped && animationEndCallback_) {
            animationEndCallback_->OnAnimationEnd(this);
        }

        // Apply animation to our per-instance mesh -- unless the entity is
        // drawn as its impostor (B964), which needs no posed vertices.
        if (!skinningSuspended_) applyAnimation();
    }

    IAnimatedMeshSceneNode::OnAnimate(timeMs);
}

void EQAnimatedMeshSceneNode::setCurrentFrame(irr::f32 frame) {
    // Not directly supported - use playAnimation instead
}

bool EQAnimatedMeshSceneNode::setFrameLoop(irr::s32 begin, irr::s32 end) {
    startFrame_ = begin;
    endFrame_ = end;
    return true;
}

void EQAnimatedMeshSceneNode::setAnimationSpeed(irr::f32 framesPerSecond) {
    // Convert frames per second to speed multiplier
    // Assuming 10 fps as base speed
    animationSpeed_ = framesPerSecond / 10.0f;
    animator_.setPlaybackSpeed(animationSpeed_);
}

irr::f32 EQAnimatedMeshSceneNode::getAnimationSpeed() const {
    return animationSpeed_ * 10.0f;  // Convert back to fps
}

irr::f32 EQAnimatedMeshSceneNode::getFrameNr() const {
    return static_cast<irr::f32>(animator_.getCurrentFrame());
}

irr::s32 EQAnimatedMeshSceneNode::getStartFrame() const {
    return startFrame_;
}

irr::s32 EQAnimatedMeshSceneNode::getEndFrame() const {
    return endFrame_;
}

void EQAnimatedMeshSceneNode::setLoopMode(bool playAnimationLooped) {
    looping_ = playAnimationLooped;
}

bool EQAnimatedMeshSceneNode::getLoopMode() const {
    return looping_;
}

void EQAnimatedMeshSceneNode::setAnimationEndCallback(irr::scene::IAnimationEndCallBack* callback) {
    if (animationEndCallback_) {
        animationEndCallback_->drop();
    }
    animationEndCallback_ = callback;
    if (animationEndCallback_) {
        animationEndCallback_->grab();
    }
}

void EQAnimatedMeshSceneNode::setReadOnlyMaterials(bool readonly) {
    readOnlyMaterials_ = readonly;
}

bool EQAnimatedMeshSceneNode::isReadOnlyMaterials() const {
    return readOnlyMaterials_;
}

void EQAnimatedMeshSceneNode::setMesh(irr::scene::IAnimatedMesh* mesh) {
    // Only accept EQAnimatedMesh
    EQAnimatedMesh* eqMesh = dynamic_cast<EQAnimatedMesh*>(mesh);
    if (eqMesh) {
        if (eqMesh_) {
            eqMesh_->drop();
        }
        eqMesh_ = eqMesh;
        eqMesh_->grab();

        // Update per-instance animator with new skeleton
        auto skeleton = eqMesh_->getSkeleton();
        if (skeleton) {
            animator_.setSkeleton(skeleton);
        }

        // Recreate per-instance mesh
        createInstanceMesh();

        // Update materials from the new instance mesh
        materials_.clear();
        if (instanceMesh_) {
            for (irr::u32 i = 0; i < instanceMesh_->getMeshBufferCount(); ++i) {
                irr::scene::IMeshBuffer* mb = instanceMesh_->getMeshBuffer(i);
                if (mb) {
                    materials_.push_back(mb->getMaterial());
                }
            }
        }

        endFrame_ = static_cast<irr::s32>(eqMesh_->getFrameCount()) - 1;
    }
}

irr::scene::IAnimatedMesh* EQAnimatedMeshSceneNode::getMesh(void) {
    return eqMesh_;
}

const irr::scene::SMD3QuaternionTag* EQAnimatedMeshSceneNode::getMD3TagTransformation(
    const irr::core::stringc& tagname) {
    return nullptr;  // Not supported
}

void EQAnimatedMeshSceneNode::setJointMode(irr::scene::E_JOINT_UPDATE_ON_RENDER mode) {
    // Not applicable - we use our own animation system
}

void EQAnimatedMeshSceneNode::setTransitionTime(irr::f32 Time) {
    // Not supported yet - could be used for animation blending
}

void EQAnimatedMeshSceneNode::animateJoints(bool CalculateAbsolutePositions) {
    // Apply animation to our per-instance mesh
    applyAnimation();
}

void EQAnimatedMeshSceneNode::setRenderFromIdentity(bool On) {
    // Not supported
}

irr::scene::ISceneNode* EQAnimatedMeshSceneNode::clone(irr::scene::ISceneNode* newParent,
                                                        irr::scene::ISceneManager* newManager) {
    if (!newParent) newParent = Parent;
    if (!newManager) newManager = SceneManager;

    EQAnimatedMeshSceneNode* newNode = new EQAnimatedMeshSceneNode(
        eqMesh_, newParent, newManager, ID, RelativeTranslation, RelativeRotation, RelativeScale);
    // T127: same missing balancing drop() as RaceModelLoader's node-factory
    // functions (animated_mesh_creation.cpp) -- newParent->addChild() already
    // grab()'d newNode; this factory must release its own `new`-acquired
    // reference before returning the raw pointer. Not currently reachable
    // from any call site (no caller of clone() exists in this codebase today)
    // but fixed alongside the live bug to avoid leaving the same defect for
    // the next caller.
    newNode->drop();

    newNode->setAnimationSpeed(getAnimationSpeed());
    newNode->setLoopMode(looping_);
    newNode->setFrameLoop(startFrame_, endFrame_);

    return newNode;
}

irr::scene::IShadowVolumeSceneNode* EQAnimatedMeshSceneNode::addShadowVolumeSceneNode(
    const irr::scene::IMesh* shadowMesh,
    irr::s32 id,
    bool zfailmethod,
    irr::f32 infinity) {
    return nullptr;  // Not supported
}

irr::scene::IBoneSceneNode* EQAnimatedMeshSceneNode::getJointNode(const irr::c8* jointName) {
    return nullptr;  // Not supported - we use our own bone system
}

irr::scene::IBoneSceneNode* EQAnimatedMeshSceneNode::getJointNode(irr::u32 jointID) {
    return nullptr;  // Not supported
}

irr::u32 EQAnimatedMeshSceneNode::getJointCount() const {
    auto skeleton = animator_.getSkeleton();
    if (skeleton) {
        return static_cast<irr::u32>(skeleton->bones.size());
    }
    return 0;
}

bool EQAnimatedMeshSceneNode::setMD2Animation(irr::scene::EMD2_ANIMATION_TYPE anim) {
    // Map MD2 animation types to EQ animation codes
    switch (anim) {
        case irr::scene::EMAT_STAND:
            return playAnimation("o01", true);  // Stand idle
        case irr::scene::EMAT_RUN:
            return playAnimation("l02", true);  // Run
        case irr::scene::EMAT_ATTACK:
            return playAnimation("c01", false); // Combat
        case irr::scene::EMAT_PAIN_A:
        case irr::scene::EMAT_PAIN_B:
        case irr::scene::EMAT_PAIN_C:
            return playAnimation("d01", false); // Damage
        case irr::scene::EMAT_JUMP:
            return playAnimation("l03", false); // Jump
        case irr::scene::EMAT_CROUCH_STAND:
            return playAnimation("l08", true);  // Crouch
        case irr::scene::EMAT_CROUCH_WALK:
            return playAnimation("l06", true);  // Crouch walk
        case irr::scene::EMAT_DEATH_FALLBACK:
        case irr::scene::EMAT_DEATH_FALLFORWARD:
        case irr::scene::EMAT_DEATH_FALLBACKSLOW:
            return playAnimation("d05", false); // Death
        default:
            return playAnimation("o01", true);  // Default to standing
    }
}

bool EQAnimatedMeshSceneNode::setMD2Animation(const irr::c8* animationName) {
    return playAnimation(animationName, looping_);
}

// EQ-specific methods

bool EQAnimatedMeshSceneNode::playAnimation(const std::string& animCode, bool loop, bool playThrough) {
    return animator_.playAnimation(animCode, loop, playThrough);
}

void EQAnimatedMeshSceneNode::stopAnimation() {
    animator_.stopAnimation();
}

bool EQAnimatedMeshSceneNode::hasAnimation(const std::string& animCode) const {
    return animator_.hasAnimation(animCode);
}

std::vector<std::string> EQAnimatedMeshSceneNode::getAnimationList() const {
    return animator_.getAnimationList();
}

const std::string& EQAnimatedMeshSceneNode::getCurrentAnimation() const {
    return animator_.getCurrentAnimation();
}

bool EQAnimatedMeshSceneNode::isPlayingThrough() const {
    return animator_.isPlayingThrough();
}


} // namespace Graphics
} // namespace EQT
