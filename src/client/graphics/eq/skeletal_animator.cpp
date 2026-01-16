#include "client/graphics/eq/skeletal_animator.h"
#include "common/logging.h"
#include <algorithm>
#include <iostream>
#include <cmath>

namespace EQT {
namespace Graphics {

SkeletalAnimator::SkeletalAnimator()
    : state_(AnimationState::Stopped)
    , currentTimeMs_(0.0f)
    , currentFrame_(0)
    , looping_(true)
    , playbackSpeed_(1.0f)
    , playThroughActive_(false)
    , holdOnComplete_(false)
    , queuedLoop_(true)
    , blendingEnabled_(true)
    , blendDurationMs_(100.0f)
    , blendTimeMs_(0.0f)
    , isBlending_(false)
    , nextTriggerId_(0)
    , lastReportedFrame_(-1) {
}

void SkeletalAnimator::setSkeleton(std::shared_ptr<CharacterSkeleton> skeleton) {
    skeleton_ = skeleton;
    stopAnimation();

    if (skeleton_) {
        boneStates_.resize(skeleton_->bones.size());
        computeBoneTransforms();
    } else {
        boneStates_.clear();
    }
}

bool SkeletalAnimator::playAnimation(const std::string& animCode, bool loop, bool playThrough) {
    if (!skeleton_) {
        if (verboseLogging_) {
            LOG_DEBUG(MOD_GRAPHICS, "SkeletalAnimator::playAnimation - no skeleton");
        }
        return false;
    }

    // Find the animation
    auto it = skeleton_->animations.find(animCode);
    if (it == skeleton_->animations.end()) {
        if (verboseLogging_) {
            LOG_DEBUG(MOD_GRAPHICS, "SkeletalAnimator::playAnimation - animation '{}' not found (have {} animations) model={}",
                      animCode, skeleton_->animations.size(), skeleton_->modelCode);
        }
        return false;
    }

    // Check if any bones have animation tracks for this animation
    int bonesWithTracks = 0;
    for (const auto& bone : skeleton_->bones) {
        if (bone.animationTracks.find(animCode) != bone.animationTracks.end()) {
            bonesWithTracks++;
        }
    }
    if (bonesWithTracks == 0) {
        if (verboseLogging_) {
            LOG_WARN(MOD_GRAPHICS, "SkeletalAnimator::playAnimation - animation '{}' exists but no bones have tracks for it! ({} bones total) model={}",
                     animCode, skeleton_->bones.size(), skeleton_->modelCode);
        }
    } else if (verboseLogging_) {
        LOG_DEBUG(MOD_GRAPHICS, "SkeletalAnimator::playAnimation - animation '{}' tracks={}/{} model={}",
                  animCode, bonesWithTracks, skeleton_->bones.size(), skeleton_->modelCode);
    }

    // Redundancy check: if already playing this animation (and not playThrough), skip
    if (currentAnimCode_ == animCode && !playThrough && state_ == AnimationState::Playing) {
        return true;  // Already playing, no change needed
    }

    // If a playThrough animation is active, queue the new animation instead
    if (playThroughActive_ && state_ == AnimationState::Playing) {
        queuedAnimCode_ = animCode;
        queuedLoop_ = loop;
        if (verboseLogging_) {
            LOG_DEBUG(MOD_GRAPHICS, "SkeletalAnimator::playAnimation - QUEUED '{}' (playThrough '{}' active) model={}",
                      animCode, currentAnimCode_, (skeleton_ ? skeleton_->modelCode : "?"));
        }
        return false;  // Return false to indicate animation wasn't started, just queued
    }

    // ========================================================================
    // Animation Blending (Phase 6.1)
    // Capture current bone states for blending before switching animations
    // ========================================================================
    if (blendingEnabled_ && state_ == AnimationState::Playing && !boneStates_.empty()) {
        // Store current bone states to blend from
        blendFromStates_ = boneStates_;
        blendTimeMs_ = 0.0f;
        isBlending_ = true;
        LOG_TRACE(MOD_GRAPHICS, "SkeletalAnimator::playAnimation - starting blend from '{}' to '{}'",
                  currentAnimCode_, animCode);
    } else {
        isBlending_ = false;
        blendTimeMs_ = blendDurationMs_;  // Skip blending
    }

    // Start the animation
    currentAnim_ = it->second;
    currentAnimCode_ = animCode;
    looping_ = loop || currentAnim_->isLooped;
    currentTimeMs_ = 0.0f;
    currentFrame_ = 0;
    state_ = AnimationState::Playing;
    playThroughActive_ = playThrough;
    // When not looping, hold on the last frame instead of resetting
    holdOnComplete_ = !looping_;

    // Performance: Update track pointer cache for direct access
    updateTrackPointerCache();

    // Reset frame trigger tracking
    lastReportedFrame_ = -1;

    // Log animation details when starting playThrough animations (combat, damage, etc.)
    if (playThrough && verboseLogging_) {
        LOG_DEBUG(MOD_GRAPHICS, "SkeletalAnimator::playAnimation - STARTED '{}' frames={} duration={}ms model={}",
                  animCode, (currentAnim_ ? currentAnim_->frameCount : 0),
                  (currentAnim_ ? currentAnim_->animationTimeMs : 0), skeleton_->modelCode);
    }

    computeBoneTransforms();

    // Fire Started event (Phase 6.3)
    fireEvent(AnimationEvent::Started);

    return true;
}

void SkeletalAnimator::stopAnimation() {
    currentAnim_ = nullptr;
    currentAnimCode_.clear();
    currentTimeMs_ = 0.0f;
    currentFrame_ = 0;
    state_ = AnimationState::Stopped;
    playThroughActive_ = false;
    queuedAnimCode_.clear();

    // Reset blending state
    isBlending_ = false;
    blendTimeMs_ = blendDurationMs_;
    blendFromStates_.clear();
    lastReportedFrame_ = -1;

    // Return to default pose
    computeBoneTransforms();
}

void SkeletalAnimator::pauseAnimation() {
    if (state_ == AnimationState::Playing) {
        state_ = AnimationState::Paused;
    }
}

void SkeletalAnimator::resumeAnimation() {
    if (state_ == AnimationState::Paused) {
        state_ = AnimationState::Playing;
    }
}

void SkeletalAnimator::update(float deltaMs) {
    if (state_ != AnimationState::Playing || !currentAnim_ || currentAnim_->frameCount <= 1) {
        return;
    }

    // ========================================================================
    // Update blend timer (Phase 6.1)
    // ========================================================================
    if (isBlending_ && blendTimeMs_ < blendDurationMs_) {
        blendTimeMs_ += deltaMs;
        if (blendTimeMs_ >= blendDurationMs_) {
            isBlending_ = false;
            blendFromStates_.clear();
            LOG_TRACE(MOD_GRAPHICS, "SkeletalAnimator::update - blend complete");
        }
    }

    // Track previous frame for trigger detection
    int previousFrame = currentFrame_;

    // Advance time
    currentTimeMs_ += deltaMs * playbackSpeed_;

    // Calculate frame time
    float frameTimeMs = static_cast<float>(currentAnim_->animationTimeMs) /
                        static_cast<float>(currentAnim_->frameCount);
    if (frameTimeMs < 1.0f) frameTimeMs = 100.0f;  // Default 100ms per frame

    // Calculate current frame
    float totalFrames = currentTimeMs_ / frameTimeMs;
    currentFrame_ = static_cast<int>(totalFrames);

    // Handle animation end
    if (currentFrame_ >= currentAnim_->frameCount) {
        if (looping_ && !playThroughActive_) {
            // Loop back to start (only if not a playThrough animation)
            currentTimeMs_ = std::fmod(currentTimeMs_, static_cast<float>(currentAnim_->animationTimeMs));
            currentFrame_ = currentFrame_ % currentAnim_->frameCount;

            // Fire Looped event (Phase 6.3)
            fireEvent(AnimationEvent::Looped);
            lastReportedFrame_ = -1;  // Reset trigger tracking on loop
        } else {
            // Animation completed - fire Completed event first
            fireEvent(AnimationEvent::Completed);

            // Animation completed - check for queued animation
            std::string nextAnim = queuedAnimCode_;
            bool nextLoop = queuedLoop_;

            // Check if we should hold on last frame (for death animations)
            if (holdOnComplete_) {
                // Freeze on last frame - don't transition to idle
                currentFrame_ = currentAnim_->frameCount - 1;
                currentTimeMs_ = static_cast<float>(currentAnim_->animationTimeMs);
                state_ = AnimationState::Stopped;
                playThroughActive_ = false;
                holdOnComplete_ = false;
                LOG_TRACE(MOD_GRAPHICS, "SkeletalAnimator::update - playThrough complete, holding on last frame");
                computeBoneTransforms();
                return;
            }

            // Clear playThrough state
            playThroughActive_ = false;
            queuedAnimCode_.clear();

            if (!nextAnim.empty()) {
                // Play the queued animation
                LOG_TRACE(MOD_GRAPHICS, "SkeletalAnimator::update - playThrough complete, playing queued '{}'", nextAnim);
                playAnimation(nextAnim, nextLoop, false);
            } else {
                // No queued animation - return to idle (o01) or stop
                if (hasAnimation("o01")) {
                    LOG_TRACE(MOD_GRAPHICS, "SkeletalAnimator::update - playThrough complete, returning to idle");
                    playAnimation("o01", true, false);
                } else {
                    // Stop at last frame
                    currentFrame_ = currentAnim_->frameCount - 1;
                    currentTimeMs_ = static_cast<float>(currentAnim_->animationTimeMs);
                    state_ = AnimationState::Stopped;
                }
            }
            return;  // Don't call computeBoneTransforms twice
        }
    }

    computeBoneTransforms();

    // ========================================================================
    // Check frame triggers (Phase 6.3)
    // ========================================================================
    checkFrameTriggers();
}

bool SkeletalAnimator::hasAnimation(const std::string& animCode) const {
    if (!skeleton_) return false;
    return skeleton_->animations.find(animCode) != skeleton_->animations.end();
}

std::vector<std::string> SkeletalAnimator::getAnimationList() const {
    std::vector<std::string> list;
    if (skeleton_) {
        for (const auto& [code, anim] : skeleton_->animations) {
            list.push_back(code);
        }
    }
    return list;
}

float SkeletalAnimator::getProgress() const {
    if (!currentAnim_ || currentAnim_->animationTimeMs <= 0) {
        return 0.0f;
    }
    return currentTimeMs_ / static_cast<float>(currentAnim_->animationTimeMs);
}

void SkeletalAnimator::setToLastFrame() {
    if (!currentAnim_ || currentAnim_->frameCount <= 0) {
        return;
    }

    // Set to the last frame of the animation
    currentFrame_ = currentAnim_->frameCount - 1;
    currentTimeMs_ = static_cast<float>(currentAnim_->animationTimeMs);
    state_ = AnimationState::Stopped;  // Stop playback at this frame
    looping_ = false;

    computeBoneTransforms();
}

void SkeletalAnimator::computeBoneTransforms() {
    if (!skeleton_ || skeleton_->bones.empty()) {
        LOG_TRACE(MOD_GRAPHICS, "SkeletalAnimator::computeBoneTransforms - no skeleton or empty bones");
        return;
    }

    boneStates_.resize(skeleton_->bones.size());

    static bool debugOnce = true;
    if (debugOnce) {
        LOG_TRACE(MOD_GRAPHICS, "SkeletalAnimator::computeBoneTransforms for {} bones", skeleton_->bones.size());
    }

    // Compute local transforms for each bone
    for (size_t i = 0; i < skeleton_->bones.size(); ++i) {
        BoneTransform transform = getBoneTransformAtFrame(static_cast<int>(i), currentFrame_);

        if (debugOnce && i < 3) {
            LOG_TRACE(MOD_GRAPHICS, "  Bone[{}] '{}' hasPose={} trans=({},{},{}) quat=({},{},{},{}) scale={}",
                      i, skeleton_->bones[i].name, (skeleton_->bones[i].poseTrack ? "yes" : "no"),
                      transform.shiftX, transform.shiftY, transform.shiftZ,
                      transform.quatX, transform.quatY, transform.quatZ, transform.quatW, transform.scale);
        }

        // Build local transform matrix: T * R * S
        BoneMat4 scaleMat = BoneMat4::scale(transform.scale);
        BoneMat4 rotMat = BoneMat4::fromQuaternion(
            transform.quatX, transform.quatY, transform.quatZ, transform.quatW);
        BoneMat4 transMat = BoneMat4::translate(
            transform.shiftX, transform.shiftY, transform.shiftZ);

        boneStates_[i].localTransform = transMat * rotMat * scaleMat;
    }

    if (debugOnce) {
        debugOnce = false;
    }

    // Compute world transforms by traversing hierarchy
    for (size_t i = 0; i < skeleton_->bones.size(); ++i) {
        int parentIdx = skeleton_->bones[i].parentIndex;

        if (parentIdx >= 0 && parentIdx < static_cast<int>(boneStates_.size())) {
            // World = Parent World * Local
            boneStates_[i].worldTransform =
                boneStates_[parentIdx].worldTransform * boneStates_[i].localTransform;
        } else {
            // Root bone - world = local
            boneStates_[i].worldTransform = boneStates_[i].localTransform;
        }
    }

    // ========================================================================
    // Apply Animation Blending (Phase 6.1)
    // Interpolate between old and new bone transforms during blend period
    // ========================================================================
    if (isBlending_ && !blendFromStates_.empty() && blendFromStates_.size() == boneStates_.size()) {
        float blendFactor = getBlendProgress();

        for (size_t i = 0; i < boneStates_.size(); ++i) {
            // Blend world transforms (simpler and more stable than blending locals)
            BoneMat4& curr = boneStates_[i].worldTransform;
            const BoneMat4& prev = blendFromStates_[i].worldTransform;

            // Linear interpolation of matrix elements (works well for small changes)
            for (int j = 0; j < 16; ++j) {
                curr.m[j] = prev.m[j] + (curr.m[j] - prev.m[j]) * blendFactor;
            }
        }
    }
}

BoneTransform SkeletalAnimator::getBoneTransformAtFrame(int boneIndex, int frame) const {
    if (!skeleton_ || boneIndex < 0 || boneIndex >= static_cast<int>(skeleton_->bones.size())) {
        // Return identity transform
        BoneTransform identity;
        identity.quatX = identity.quatY = identity.quatZ = 0.0f;
        identity.quatW = 1.0f;
        identity.shiftX = identity.shiftY = identity.shiftZ = 0.0f;
        identity.scale = 1.0f;
        return identity;
    }

    const AnimatedBone& bone = skeleton_->bones[boneIndex];

    // Performance: Use cached track pointer instead of map lookup
    if (currentAnim_ && !currentAnimCode_.empty() &&
        boneIndex < static_cast<int>(cachedTrackPtrs_.size())) {
        TrackDef* trackDef = cachedTrackPtrs_[boneIndex];
        if (trackDef && !trackDef->frames.empty()) {
            int frameIdx = std::min(frame, static_cast<int>(trackDef->frames.size()) - 1);
            return trackDef->frames[frameIdx];
        }
    }

    // Fall back to pose track
    if (bone.poseTrack && !bone.poseTrack->frames.empty()) {
        return bone.poseTrack->frames[0];
    }

    // Return identity if no track data
    BoneTransform identity;
    identity.quatX = identity.quatY = identity.quatZ = 0.0f;
    identity.quatW = 1.0f;
    identity.shiftX = identity.shiftY = identity.shiftZ = 0.0f;
    identity.scale = 1.0f;
    return identity;
}

BoneTransform SkeletalAnimator::interpolate(const BoneTransform& a, const BoneTransform& b, float t) {
    BoneTransform result;

    // Linear interpolation for translation
    result.shiftX = a.shiftX + (b.shiftX - a.shiftX) * t;
    result.shiftY = a.shiftY + (b.shiftY - a.shiftY) * t;
    result.shiftZ = a.shiftZ + (b.shiftZ - a.shiftZ) * t;

    // Linear interpolation for scale
    result.scale = a.scale + (b.scale - a.scale) * t;

    // SLERP for rotation quaternion
    slerp(a.quatX, a.quatY, a.quatZ, a.quatW,
          b.quatX, b.quatY, b.quatZ, b.quatW,
          t,
          result.quatX, result.quatY, result.quatZ, result.quatW);

    return result;
}

void SkeletalAnimator::slerp(float ax, float ay, float az, float aw,
                              float bx, float by, float bz, float bw,
                              float t,
                              float& rx, float& ry, float& rz, float& rw) {
    // Compute dot product
    float dot = ax*bx + ay*by + az*bz + aw*bw;

    // If negative dot, negate one quaternion to take shorter path
    if (dot < 0.0f) {
        bx = -bx; by = -by; bz = -bz; bw = -bw;
        dot = -dot;
    }

    // If quaternions are very close, use linear interpolation
    if (dot > 0.9995f) {
        rx = ax + (bx - ax) * t;
        ry = ay + (by - ay) * t;
        rz = az + (bz - az) * t;
        rw = aw + (bw - aw) * t;

        // Normalize
        float len = std::sqrt(rx*rx + ry*ry + rz*rz + rw*rw);
        if (len > 0.0001f) {
            rx /= len; ry /= len; rz /= len; rw /= len;
        }
        return;
    }

    // Compute SLERP
    float theta = std::acos(dot);
    float sinTheta = std::sin(theta);
    float wa = std::sin((1.0f - t) * theta) / sinTheta;
    float wb = std::sin(t * theta) / sinTheta;

    rx = ax * wa + bx * wb;
    ry = ay * wa + by * wb;
    rz = az * wa + bz * wb;
    rw = aw * wa + bw * wb;
}

// ============================================================================
// Phase 6.1: Animation Blending
// ============================================================================

float SkeletalAnimator::getBlendProgress() const {
    if (!isBlending_ || blendDurationMs_ <= 0.0f) {
        return 1.0f;  // Fully at new animation
    }
    return std::min(1.0f, blendTimeMs_ / blendDurationMs_);
}

// ============================================================================
// Phase 6.2: Animation Speed Matching
// ============================================================================

void SkeletalAnimator::setTargetDuration(float targetDurationMs) {
    if (!currentAnim_ || currentAnim_->animationTimeMs <= 0 || targetDurationMs <= 0) {
        return;
    }

    // Calculate speed multiplier to make animation complete in target duration
    float animDuration = static_cast<float>(currentAnim_->animationTimeMs);
    playbackSpeed_ = animDuration / targetDurationMs;

    LOG_TRACE(MOD_GRAPHICS, "SkeletalAnimator::setTargetDuration - animDuration={}ms target={}ms speed={}",
              animDuration, targetDurationMs, playbackSpeed_);
}

void SkeletalAnimator::matchMovementSpeed(float baseSpeed, float actualSpeed) {
    if (baseSpeed <= 0.0f) {
        playbackSpeed_ = 1.0f;
        return;
    }

    // Scale animation speed proportionally to movement speed
    playbackSpeed_ = actualSpeed / baseSpeed;

    // Clamp to reasonable range (0.5x to 2.0x)
    playbackSpeed_ = std::max(0.5f, std::min(2.0f, playbackSpeed_));

    LOG_TRACE(MOD_GRAPHICS, "SkeletalAnimator::matchMovementSpeed - base={} actual={} speed={}",
              baseSpeed, actualSpeed, playbackSpeed_);
}

// ============================================================================
// Phase 6.3: Animation Callbacks/Events
// ============================================================================

int SkeletalAnimator::addFrameTrigger(int frameIndex) {
    int id = nextTriggerId_++;
    frameTriggers_.push_back({id, frameIndex});
    LOG_TRACE(MOD_GRAPHICS, "SkeletalAnimator::addFrameTrigger - added trigger {} for frame {}", id, frameIndex);
    return id;
}

void SkeletalAnimator::removeFrameTrigger(int triggerId) {
    auto it = std::remove_if(frameTriggers_.begin(), frameTriggers_.end(),
                             [triggerId](const std::pair<int, int>& p) { return p.first == triggerId; });
    frameTriggers_.erase(it, frameTriggers_.end());
}

void SkeletalAnimator::clearFrameTriggers() {
    frameTriggers_.clear();
    LOG_TRACE(MOD_GRAPHICS, "SkeletalAnimator::clearFrameTriggers - cleared all triggers");
}

void SkeletalAnimator::addHitFrameTrigger() {
    if (!currentAnim_ || currentAnim_->frameCount <= 0) {
        return;
    }
    // Add trigger at approximately 50% of the animation (typical hit point)
    int hitFrame = currentAnim_->frameCount / 2;
    addFrameTrigger(hitFrame);
    LOG_TRACE(MOD_GRAPHICS, "SkeletalAnimator::addHitFrameTrigger - added hit trigger at frame {}", hitFrame);
}

void SkeletalAnimator::addFootstepTriggers() {
    if (!currentAnim_ || currentAnim_->frameCount < 4) {
        return;
    }
    // Add triggers at 25% and 75% of animation (typical footstep points for walk/run)
    int step1 = currentAnim_->frameCount / 4;
    int step2 = (currentAnim_->frameCount * 3) / 4;
    addFrameTrigger(step1);
    addFrameTrigger(step2);
    LOG_TRACE(MOD_GRAPHICS, "SkeletalAnimator::addFootstepTriggers - added footstep triggers at frames {} and {}", step1, step2);
}

void SkeletalAnimator::fireEvent(AnimationEvent event) {
    if (!eventCallback_) {
        return;
    }

    AnimationEventData data;
    data.event = event;
    data.animCode = currentAnimCode_;
    data.currentFrame = currentFrame_;
    data.totalFrames = currentAnim_ ? currentAnim_->frameCount : 0;
    data.progress = getProgress();

    eventCallback_(data);
}

void SkeletalAnimator::checkFrameTriggers() {
    if (frameTriggers_.empty() || !eventCallback_ || !currentAnim_) {
        return;
    }

    // Don't fire triggers for frames we've already reported
    if (currentFrame_ <= lastReportedFrame_) {
        return;
    }

    for (const auto& [triggerId, triggerFrame] : frameTriggers_) {
        // Handle -1 as "last frame"
        int actualFrame = (triggerFrame < 0) ? (currentAnim_->frameCount - 1) : triggerFrame;

        // Check if we crossed this trigger frame since last check
        if (actualFrame > lastReportedFrame_ && actualFrame <= currentFrame_) {
            AnimationEventData data;
            data.event = AnimationEvent::FrameReached;
            data.animCode = currentAnimCode_;
            data.currentFrame = actualFrame;
            data.totalFrames = currentAnim_->frameCount;
            data.progress = static_cast<float>(actualFrame) / static_cast<float>(currentAnim_->frameCount);

            eventCallback_(data);
            LOG_TRACE(MOD_GRAPHICS, "SkeletalAnimator::checkFrameTriggers - fired trigger {} for frame {}", triggerId, actualFrame);
        }
    }

    lastReportedFrame_ = currentFrame_;
}

// ============================================================================
// Performance: Animation Track Pointer Caching (Phase 3)
// ============================================================================

void SkeletalAnimator::updateTrackPointerCache() {
    cachedTrackPtrs_.clear();

    if (!skeleton_ || currentAnimCode_.empty()) {
        cachedTrackAnimCode_.clear();
        return;
    }

    // Build cache of track pointers for current animation
    // One pointer per bone - nullptr if bone has no track for this animation
    cachedTrackPtrs_.resize(skeleton_->bones.size(), nullptr);

    for (size_t i = 0; i < skeleton_->bones.size(); ++i) {
        const AnimatedBone& bone = skeleton_->bones[i];
        auto trackIt = bone.animationTracks.find(currentAnimCode_);
        if (trackIt != bone.animationTracks.end() && trackIt->second) {
            cachedTrackPtrs_[i] = trackIt->second.get();
        }
    }

    cachedTrackAnimCode_ = currentAnimCode_;
}

} // namespace Graphics
} // namespace EQT
