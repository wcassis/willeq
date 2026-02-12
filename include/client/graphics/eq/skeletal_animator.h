#ifndef EQT_GRAPHICS_SKELETAL_ANIMATOR_H
#define EQT_GRAPHICS_SKELETAL_ANIMATOR_H

#include "wld_loader.h"
#include "s3d_loader.h"
#include "common/simd_detect.h"
#include <string>
#include <vector>
#include <memory>
#include <cmath>
#include <functional>

namespace EQT {
namespace Graphics {

// 4x4 matrix for bone transforms (column-major order)
struct BoneMat4 {
    float m[16];

    BoneMat4() {
        // Identity matrix
        for (int i = 0; i < 16; ++i) m[i] = 0.0f;
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }

    static BoneMat4 identity() {
        return BoneMat4();
    }

    static BoneMat4 translate(float x, float y, float z) {
        BoneMat4 mat;
        mat.m[12] = x;
        mat.m[13] = y;
        mat.m[14] = z;
        return mat;
    }

    static BoneMat4 scale(float s) {
        BoneMat4 mat;
        mat.m[0] = mat.m[5] = mat.m[10] = s;
        return mat;
    }

    static BoneMat4 fromQuaternion(float qx, float qy, float qz, float qw) {
        BoneMat4 mat;
        float xx = qx * qx, yy = qy * qy, zz = qz * qz;
        float xy = qx * qy, xz = qx * qz, yz = qy * qz;
        float wx = qw * qx, wy = qw * qy, wz = qw * qz;

        mat.m[0]  = 1.0f - 2.0f * (yy + zz);
        mat.m[1]  = 2.0f * (xy + wz);
        mat.m[2]  = 2.0f * (xz - wy);
        mat.m[4]  = 2.0f * (xy - wz);
        mat.m[5]  = 1.0f - 2.0f * (xx + zz);
        mat.m[6]  = 2.0f * (yz + wx);
        mat.m[8]  = 2.0f * (xz + wy);
        mat.m[9]  = 2.0f * (yz - wx);
        mat.m[10] = 1.0f - 2.0f * (xx + yy);
        return mat;
    }

    BoneMat4 operator*(const BoneMat4& rhs) const {
#ifdef EQT_HAS_NEON
        BoneMat4 result;
        // Load this matrix columns
        float32x4_t col0 = vld1q_f32(&m[0]);
        float32x4_t col1 = vld1q_f32(&m[4]);
        float32x4_t col2 = vld1q_f32(&m[8]);
        float32x4_t col3 = vld1q_f32(&m[12]);
        // For each output column, multiply-accumulate
        for (int c = 0; c < 4; ++c) {
            float32x4_t rhsCol = vld1q_f32(&rhs.m[c * 4]);
            float32x4_t res = vmulq_lane_f32(col0, vget_low_f32(rhsCol), 0);
            res = vmlaq_lane_f32(res, col1, vget_low_f32(rhsCol), 1);
            res = vmlaq_lane_f32(res, col2, vget_high_f32(rhsCol), 0);
            res = vmlaq_lane_f32(res, col3, vget_high_f32(rhsCol), 1);
            vst1q_f32(&result.m[c * 4], res);
        }
        return result;
#elif defined(EQT_HAS_SSE2)
        BoneMat4 result;
        __m128 col0 = _mm_loadu_ps(&m[0]);
        __m128 col1 = _mm_loadu_ps(&m[4]);
        __m128 col2 = _mm_loadu_ps(&m[8]);
        __m128 col3 = _mm_loadu_ps(&m[12]);
        for (int c = 0; c < 4; ++c) {
            __m128 rhsCol = _mm_loadu_ps(&rhs.m[c * 4]);
            __m128 r0 = _mm_shuffle_ps(rhsCol, rhsCol, _MM_SHUFFLE(0,0,0,0));
            __m128 r1 = _mm_shuffle_ps(rhsCol, rhsCol, _MM_SHUFFLE(1,1,1,1));
            __m128 r2 = _mm_shuffle_ps(rhsCol, rhsCol, _MM_SHUFFLE(2,2,2,2));
            __m128 r3 = _mm_shuffle_ps(rhsCol, rhsCol, _MM_SHUFFLE(3,3,3,3));
            __m128 res = _mm_add_ps(
                _mm_add_ps(_mm_mul_ps(col0, r0), _mm_mul_ps(col1, r1)),
                _mm_add_ps(_mm_mul_ps(col2, r2), _mm_mul_ps(col3, r3)));
            _mm_storeu_ps(&result.m[c * 4], res);
        }
        return result;
#else
        BoneMat4 result;
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                result.m[c*4 + r] =
                    m[0*4 + r] * rhs.m[c*4 + 0] +
                    m[1*4 + r] * rhs.m[c*4 + 1] +
                    m[2*4 + r] * rhs.m[c*4 + 2] +
                    m[3*4 + r] * rhs.m[c*4 + 3];
            }
        }
        return result;
#endif
    }

    void transformPoint(float& x, float& y, float& z) const {
#ifdef EQT_HAS_NEON
        float32x4_t col0 = vld1q_f32(&m[0]);
        float32x4_t col1 = vld1q_f32(&m[4]);
        float32x4_t col2 = vld1q_f32(&m[8]);
        float32x4_t col3 = vld1q_f32(&m[12]);
        float32x4_t res = vmulq_n_f32(col0, x);
        res = vmlaq_n_f32(res, col1, y);
        res = vmlaq_n_f32(res, col2, z);
        res = vaddq_f32(res, col3);
        x = vgetq_lane_f32(res, 0);
        y = vgetq_lane_f32(res, 1);
        z = vgetq_lane_f32(res, 2);
#elif defined(EQT_HAS_SSE2)
        __m128 col0 = _mm_loadu_ps(&m[0]);
        __m128 col1 = _mm_loadu_ps(&m[4]);
        __m128 col2 = _mm_loadu_ps(&m[8]);
        __m128 col3 = _mm_loadu_ps(&m[12]);
        __m128 vx = _mm_set1_ps(x);
        __m128 vy = _mm_set1_ps(y);
        __m128 vz = _mm_set1_ps(z);
        __m128 res = _mm_add_ps(
            _mm_add_ps(_mm_mul_ps(col0, vx), _mm_mul_ps(col1, vy)),
            _mm_add_ps(_mm_mul_ps(col2, vz), col3));
        EQT_ALIGN(16) float out[4];
        _mm_store_ps(out, res);
        x = out[0]; y = out[1]; z = out[2];
#else
        float nx = m[0]*x + m[4]*y + m[8]*z  + m[12];
        float ny = m[1]*x + m[5]*y + m[9]*z  + m[13];
        float nz = m[2]*x + m[6]*y + m[10]*z + m[14];
        x = nx; y = ny; z = nz;
#endif
    }
};

// Current state of a bone during animation
struct AnimatedBoneState {
    BoneMat4 localTransform;   // Local bone transform (relative to parent)
    BoneMat4 worldTransform;   // World transform (accumulated from root)
};

// Animation playback state
enum class AnimationState {
    Stopped,
    Playing,
    Paused
};

// Animation event types for callbacks
enum class AnimationEvent {
    Started,        // Animation just started
    FrameReached,   // Specific frame was reached (for hit/cast/footstep events)
    Completed,      // Animation finished (for non-looping or playThrough)
    Looped,         // Animation looped back to start
    Blending        // Currently blending between animations
};

// Animation event callback data
struct AnimationEventData {
    AnimationEvent event;
    std::string animCode;       // Current animation code
    int currentFrame;           // Current frame number
    int totalFrames;            // Total frames in animation
    float progress;             // 0.0 to 1.0 progress through animation
};

// Animation callback function type
using AnimationCallback = std::function<void(const AnimationEventData&)>;

// Skeletal animation controller
class SkeletalAnimator {
public:
    SkeletalAnimator();
    ~SkeletalAnimator() = default;

    // Set the skeleton to animate
    void setSkeleton(std::shared_ptr<CharacterSkeleton> skeleton);

    // Get the skeleton
    std::shared_ptr<CharacterSkeleton> getSkeleton() const { return skeleton_; }

    // Play an animation by code (e.g., "l01" for walk, "c01" for combat)
    // playThrough: if true, animation must complete before next can start (for jumps, attacks, emotes)
    //              if false, animation can be interrupted at any time (for walk, run, idle)
    // When loop=false, animation holds on last frame automatically
    // Returns true if animation was found and started (or queued if playThrough is active)
    bool playAnimation(const std::string& animCode, bool loop = true, bool playThrough = false);

    // Stop the current animation and return to pose (clears queue)
    void stopAnimation();

    // Check if a playThrough animation is currently active
    bool isPlayingThrough() const { return playThroughActive_; }

    // Get the queued animation (empty if none)
    const std::string& getQueuedAnimation() const { return queuedAnimCode_; }

    // Clear the queued animation
    void clearQueuedAnimation() { queuedAnimCode_.clear(); }

    // Pause/resume the current animation
    void pauseAnimation();
    void resumeAnimation();

    // Update animation state (call each frame)
    // deltaMs: time since last update in milliseconds
    void update(float deltaMs);

    // Get current animation code (empty if none playing)
    const std::string& getCurrentAnimation() const { return currentAnimCode_; }

    // Get animation state
    AnimationState getState() const { return state_; }

    // Check if an animation exists
    bool hasAnimation(const std::string& animCode) const;

    // Get list of available animations
    std::vector<std::string> getAnimationList() const;

    // Get the world transform matrices for all bones
    // Use these to transform mesh vertices during rendering
    const std::vector<AnimatedBoneState>& getBoneStates() const { return boneStates_; }

    // Get current animation progress (0.0 to 1.0)
    float getProgress() const;

    // Get current frame index
    int getCurrentFrame() const { return currentFrame_; }

    // Set to the last frame of the current animation (for corpse pose)
    void setToLastFrame();

    // Set playback speed multiplier (1.0 = normal)
    void setPlaybackSpeed(float speed) { playbackSpeed_ = speed; }
    float getPlaybackSpeed() const { return playbackSpeed_; }

    // Enable verbose logging (for debugging specific entities like current target)
    void setVerboseLogging(bool enabled) { verboseLogging_ = enabled; }
    bool isVerboseLogging() const { return verboseLogging_; }

    // ========================================================================
    // Animation Blending (Phase 6.1)
    // ========================================================================

    // Enable/disable animation blending (smooth transitions between animations)
    void setBlendingEnabled(bool enabled) { blendingEnabled_ = enabled; }
    bool isBlendingEnabled() const { return blendingEnabled_; }

    // Set blend duration in milliseconds (default 100ms)
    void setBlendDuration(float durationMs) { blendDurationMs_ = durationMs; }
    float getBlendDuration() const { return blendDurationMs_; }

    // Check if currently blending between animations
    bool isBlending() const { return blendTimeMs_ < blendDurationMs_ && blendDurationMs_ > 0; }

    // Get blend progress (0.0 = fully old animation, 1.0 = fully new animation)
    float getBlendProgress() const;

    // ========================================================================
    // Animation Speed Matching (Phase 6.2)
    // ========================================================================

    // Set animation duration to match a specific time (e.g., weapon delay, cast time)
    // This adjusts playback speed so animation completes in targetDurationMs
    void setTargetDuration(float targetDurationMs);

    // Match animation speed to movement speed
    // baseSpeed: reference movement speed (e.g., walk = 10, run = 23)
    // actualSpeed: current movement speed
    void matchMovementSpeed(float baseSpeed, float actualSpeed);

    // Reset to default playback speed
    void resetPlaybackSpeed() { playbackSpeed_ = 1.0f; }

    // ========================================================================
    // Animation Callbacks/Events (Phase 6.3)
    // ========================================================================

    // Set callback for animation events
    void setEventCallback(AnimationCallback callback) { eventCallback_ = callback; }

    // Clear the event callback
    void clearEventCallback() { eventCallback_ = nullptr; }

    // Register a frame trigger (callback fires when this frame is reached)
    // frameIndex: specific frame number to trigger on (-1 for last frame)
    // Returns trigger ID for removal
    int addFrameTrigger(int frameIndex);

    // Remove a frame trigger by ID
    void removeFrameTrigger(int triggerId);

    // Clear all frame triggers
    void clearFrameTriggers();

    // Common frame trigger helpers
    void addHitFrameTrigger();      // Adds trigger at ~50% of animation (typical hit point)
    void addFootstepTriggers();     // Adds triggers for walk/run footstep frames

private:
    // Compute bone transforms for the current frame
    void computeBoneTransforms();

    // Get bone transform from animation track at given frame
    BoneTransform getBoneTransformAtFrame(int boneIndex, int frame) const;

    // Interpolate between two bone transforms
    static BoneTransform interpolate(const BoneTransform& a, const BoneTransform& b, float t);

    // Spherical linear interpolation for quaternions
    static void slerp(float ax, float ay, float az, float aw,
                      float bx, float by, float bz, float bw,
                      float t,
                      float& rx, float& ry, float& rz, float& rw);

    std::shared_ptr<CharacterSkeleton> skeleton_;
    std::shared_ptr<Animation> currentAnim_;
    std::string currentAnimCode_;
    AnimationState state_;

    float currentTimeMs_;      // Current time within animation
    int currentFrame_;         // Current frame index
    bool looping_;             // Whether animation should loop
    float playbackSpeed_;      // Speed multiplier
    bool verboseLogging_ = false;  // Verbose debug logging (for current target)

    // Animation queuing for playThrough animations
    bool playThroughActive_;   // True if current animation must complete before next starts
    bool holdOnComplete_;      // True to freeze on last frame instead of returning to idle
    std::string queuedAnimCode_;  // Animation to play after current playThrough completes
    bool queuedLoop_;          // Loop setting for queued animation

    std::vector<AnimatedBoneState> boneStates_;  // Current bone transforms

    // ========================================================================
    // Performance: Cached Animation Track Pointers (Phase 3)
    // Direct pointer access instead of map lookup per bone per frame
    // ========================================================================
    std::vector<TrackDef*> cachedTrackPtrs_;  // One per bone, nullptr if no track for current anim
    std::string cachedTrackAnimCode_;                   // Animation code the cache was built for
    void updateTrackPointerCache();                     // Rebuild cache when animation changes

    // ========================================================================
    // Animation Blending State (Phase 6.1)
    // ========================================================================
    bool blendingEnabled_ = true;           // Whether blending is enabled
    float blendDurationMs_ = 100.0f;        // Duration of blend in milliseconds
    float blendTimeMs_ = 0.0f;              // Current time into blend (0 = just started)
    std::vector<AnimatedBoneState> blendFromStates_;  // Bone states at start of blend
    bool isBlending_ = false;               // Currently blending between animations

    // ========================================================================
    // Animation Callbacks State (Phase 6.3)
    // ========================================================================
    AnimationCallback eventCallback_;       // User callback for animation events
    std::vector<std::pair<int, int>> frameTriggers_;  // (triggerId, frameIndex) pairs
    int nextTriggerId_ = 0;                 // ID counter for triggers
    int lastReportedFrame_ = -1;            // Last frame we fired triggers for

    // Fire animation event callback
    void fireEvent(AnimationEvent event);

    // Check and fire frame triggers
    void checkFrameTriggers();
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_SKELETAL_ANIMATOR_H
