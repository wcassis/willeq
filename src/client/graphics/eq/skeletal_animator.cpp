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
    , queuedLoop_(true) {
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
        LOG_DEBUG(MOD_GRAPHICS, "SkeletalAnimator::playAnimation - no skeleton");
        return false;
    }

    // Find the animation
    auto it = skeleton_->animations.find(animCode);
    if (it == skeleton_->animations.end()) {
        LOG_DEBUG(MOD_GRAPHICS, "SkeletalAnimator::playAnimation - animation '{}' not found", animCode);
        return false;
    }

    // Redundancy check: if already playing this animation (and not playThrough), skip
    if (currentAnimCode_ == animCode && !playThrough && state_ == AnimationState::Playing) {
        return true;  // Already playing, no change needed
    }

    // If a playThrough animation is active, queue the new animation instead
    if (playThroughActive_ && state_ == AnimationState::Playing) {
        queuedAnimCode_ = animCode;
        queuedLoop_ = loop;
        LOG_TRACE(MOD_GRAPHICS, "SkeletalAnimator::playAnimation - queued '{}' (playThrough active)", animCode);
        return true;  // Queued successfully
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

    // Use TRACE level for frequent animation logs to reduce noise at debug level 1
    LOG_TRACE(MOD_GRAPHICS, "SkeletalAnimator::playAnimation - playing '{}' frameCount={} animTimeMs={} looping={} playThrough={}",
              animCode, (currentAnim_ ? currentAnim_->frameCount : 0), (currentAnim_ ? currentAnim_->animationTimeMs : 0),
              looping_, playThrough);

    computeBoneTransforms();
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
        } else {
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

    // If we have an active animation, use it
    if (currentAnim_ && !currentAnimCode_.empty()) {
        auto trackIt = bone.animationTracks.find(currentAnimCode_);
        if (trackIt != bone.animationTracks.end() && trackIt->second) {
            const auto& trackDef = trackIt->second;
            if (!trackDef->frames.empty()) {
                int frameIdx = std::min(frame, static_cast<int>(trackDef->frames.size()) - 1);
                return trackDef->frames[frameIdx];
            }
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

} // namespace Graphics
} // namespace EQT
