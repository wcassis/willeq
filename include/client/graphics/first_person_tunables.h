#pragma once

/**
 * T310 Leg A -- THE FIRST-PERSON CAMERA'S NUMBERS, SETTABLE AT RUNTIME.
 *
 * WHY THIS EXISTS. Will, 2026-08-29: "adjust willeq's output until it matches
 * the official client exactly. Then you'll have your camera position. ... This
 * should not require recompiling." Every first-person camera parameter was a
 * `constexpr` in first_person_eye.h / first_person_camera.h, so each candidate
 * value cost a full rebuild -- which is exactly why two units (T301 Leg B,
 * T309) picked the forward offset by ARGUMENT instead of by SEARCH. A sweep
 * that has to rebuild per sample is not a sweep.
 *
 * WHAT THIS IS NOT. It is not a new set of numbers and it changes no
 * behaviour: every default below IS the named constant it replaces, read
 * directly from that constant, so a run with no override renders byte-for-byte
 * what it rendered before. tests/test_first_person_tunables.cpp pins that.
 *
 * WHERE THE DEFAULTS LIVE. The eye-side constants stay in first_person_eye.h.
 * The four camera-side constants MOVED HERE from first_person_camera.h (which
 * includes this header, so every existing `EQT::Graphics::kFirstPerson...`
 * spelling still resolves) purely so the default table can name them without a
 * circular include -- their values, and the reasoning comments attached to
 * them, are unchanged.
 *
 * THREAD MODEL. Reads happen on the render thread (the camera formula) and on
 * the model-load path; writes happen from the render thread (/fpcam, drained
 * from the RendererCommand bridge queue) and once at startup before any thread
 * exists (--fpcam). Every slot is an atomic so a torn float is impossible;
 * relaxed ordering is right because each value is independent and no other
 * state is published through them.
 *
 * LIVE vs ADVISORY. Two of the exposed parameters have NO production consumer
 * today -- kCrownAboveEyeUnits and kEyeHeightFractionOfStandingHeight are
 * cross-check constants read only by tests/test_first_person_eye_anatomy.cpp.
 * They are still listed (the plan names the first explicitly) but they are
 * marked ADVISORY in every listing, because a tunable that silently does
 * nothing is a trap for a sweep driver that assumes it moved the camera.
 */

#include <array>
#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

#include "client/graphics/first_person_eye.h"

namespace EQT {
namespace Graphics {

// -------------------------------------------------------------------------
// The camera-side defaults. MOVED VERBATIM from first_person_camera.h (T310
// Leg A) so this header can name them; that header includes this one, so
// nothing that referred to them by name had to change.
// -------------------------------------------------------------------------

// Fixed downward bias applied to the look ray, on top of the user's
// mouse-look pitch. T75 shipped 12 degrees; that number was chosen to tip
// the frustum onto in-world arm geometry, which is not what first person is
// for. 3.65 is the official client's measured value.
constexpr float kFirstPersonPitchDownDegrees = 3.65f;

// Horizontal field of view. fable never called setFOV(), so the world camera
// ran at Irrlicht's default 72 degrees VERTICAL (PI/2.5), which at 4:3 is
// ~88 degrees horizontal -- far wider than the official client's 62.8.
constexpr float kFirstPersonHorizontalFovDegrees = 62.8f;

// The gimbal clamp on the UP side is NOT part of the hiding mechanism and is
// unchanged at +89: the body hangs BELOW the eye, so looking up moves it
// further out of frame, and T301 Leg A measured only the down side. 89 exists
// to keep the look-at camera from degenerating when the target sits exactly
// above the eye. Named separately so the two clamps are not confused.
constexpr float kFirstPersonMaxUpPitchDegrees = 89.0f;

// The near plane. T300 (post-Leg-A): THE SAME IN EVERY MODE -- Leg A measured
// the official client clipping nothing, so first person must clip exactly as
// third person does. Matches setupCamera()'s original setNearValue(1.0f).
constexpr float kWorldNearPlaneUnits = 1.0f;

// -------------------------------------------------------------------------
// The tunable set.
// -------------------------------------------------------------------------

enum class FpTunable : int {
    EyeForwardOffsetUnits = 0,
    EyeHeightFractionOfSkull,
    CrownAboveEyeUnits,
    EyeHeightFractionOfStandingHeight,
    MaxDownPitchDegrees,
    MaxUpPitchDegrees,
    PitchDownDegrees,
    HorizontalFovDegrees,
    NearPlaneUnits,
    Count
};

constexpr int kFpTunableCount = static_cast<int>(FpTunable::Count);

struct FpTunableSpec {
    const char* name;         // the /fpcam spelling
    const char* constantName; // the constexpr it defaults from
    const char* units;        // "eq_units" | "degrees" | "fraction"
    const char* frame;        // the frame the number is expressed in
    float defaultValue;
    bool live;                // false == no production consumer (ADVISORY)
    const char* help;
};

inline const FpTunableSpec* fpTunableSpecs() {
    static const FpTunableSpec kSpecs[kFpTunableCount] = {
        {"eyeForwardOffset", "kEyeForwardOffsetUnits", "eq_units",
         "along_look_ray_horizontal(world)", kEyeForwardOffsetUnits, true,
         "Extra forward nudge of the eye along the heading, on top of the head "
         "cluster's own anatomical position. T301 Leg A measured +0.9+/-0.25, "
         "T298 measured 0.00+/-0.4; T309 took 0.0. THIS IS THE PARAMETER T310 "
         "EXISTS TO SOLVE."},
        {"eyeHeightFractionOfSkull", "kEyeHeightFractionOfSkull", "fraction",
         "head_bone_local_z(symmetrised skull span, 0=chin 1=crown)",
         kEyeHeightFractionOfSkull, true,
         "Where up the skull the eye sits. Applied per race to that race's own "
         "head-bone vertex cluster, so one global value covers every mesh."},
        {"crownAboveEye", "kCrownAboveEyeUnits", "eq_units",
         "model_space_z(inside the head)", kCrownAboveEyeUnits, false,
         "ADVISORY: cross-check only. eyeHeightFractionOfSkull was SOLVED so a "
         "Human male's eye lands at crown-minus-this; nothing reads it at "
         "runtime (tests/test_first_person_eye_anatomy.cpp does). Setting it "
         "moves no pixels."},
        {"eyeHeightFractionOfStandingHeight", "kEyeHeightFractionOfStandingHeight",
         "fraction", "model_standing_height(0=feet 1=crown)",
         kEyeHeightFractionOfStandingHeight, false,
         "ADVISORY: cross-check only, and the one the sit-drop is expressed "
         "against. No runtime consumer; setting it moves no pixels."},
        {"maxDownPitch", "kFirstPersonMaxDownPitchDegrees", "degrees",
         "camera_total_down_pitch_below_horizon", kFirstPersonMaxDownPitchDegrees, true,
         "Control-feel limit: how far down the official client lets you look. "
         "MEASURED at 94.9, i.e. PAST the nadir. It is NOT a hiding mechanism "
         "(the draw rule is)."},
        {"maxUpPitch", "kFirstPersonMaxUpPitchDegrees", "degrees",
         "camera_total_up_pitch_above_horizon", kFirstPersonMaxUpPitchDegrees, true,
         "Gimbal clamp on the up side."},
        {"pitchDown", "kFirstPersonPitchDownDegrees", "degrees",
         "fixed_bias_added_to_mouse_look_pitch", kFirstPersonPitchDownDegrees, true,
         "Fixed downward bias on the look ray, on top of mouse-look pitch. "
         "Official client measured 3.65."},
        {"hfov", "kFirstPersonHorizontalFovDegrees", "degrees",
         "horizontal_field_of_view(converted to vertical through the aspect)",
         kFirstPersonHorizontalFovDegrees, true,
         "Official client measured 62.8 horizontal (49.3 vertical at 4:3)."},
        {"nearPlane", "kWorldNearPlaneUnits", "eq_units",
         "distance_from_camera_along_view_axis", kWorldNearPlaneUnits, true,
         "The near plane. Applied in EVERY camera mode, so changing it also "
         "changes third person and therefore the visual goldens."},
    };
    return kSpecs;
}

namespace detail {

struct FpTunableStore {
    std::array<std::atomic<float>, kFpTunableCount> value;
    std::array<std::atomic<bool>, kFpTunableCount> overridden;

    FpTunableStore() {
        const FpTunableSpec* specs = fpTunableSpecs();
        for (int i = 0; i < kFpTunableCount; ++i) {
            value[i].store(specs[i].defaultValue, std::memory_order_relaxed);
            overridden[i].store(false, std::memory_order_relaxed);
        }
    }
};

// C++17 inline => exactly one instance across every TU that includes this.
inline FpTunableStore& fpStore() {
    static FpTunableStore store;
    return store;
}

}  // namespace detail

// The read every consumer uses. Deliberately tiny and header-inline: it sits
// in the per-frame camera formula.
inline float fpTunable(FpTunable t) {
    return detail::fpStore().value[static_cast<int>(t)].load(std::memory_order_relaxed);
}

inline bool fpTunableIsOverridden(int index) {
    if (index < 0 || index >= kFpTunableCount) return false;
    return detail::fpStore().overridden[index].load(std::memory_order_relaxed);
}

inline float fpTunableValue(int index) {
    if (index < 0 || index >= kFpTunableCount) return 0.0f;
    return detail::fpStore().value[index].load(std::memory_order_relaxed);
}

// Case-insensitive; accepts either the /fpcam spelling or the constant name
// it defaults from, so a reader who only knows `kEyeForwardOffsetUnits` from
// the header comments can still drive it. -1 if unknown.
int fpTunableIndexByName(const std::string& name);

// Returns false and leaves the store untouched if `index` is out of range or
// `v` is not finite. Any finite value is otherwise accepted: this is a
// measurement instrument and clamping it to a "sensible" range would silently
// truncate the sweep that is the point of the unit.
bool fpTunableSet(int index, float v);

// Back to the constant. Clears the overridden flag.
void fpTunableReset(int index);
void fpTunableResetAll();

// Parse and apply one "name=value" (or "name value") assignment.
// `error` is filled with a human-readable reason on failure.
bool fpTunableApplyAssignment(const std::string& assignment, std::string& error);

// Apply a comma- or whitespace-separated list of assignments (the --fpcam CLI
// form). Returns false if ANY assignment failed; `error` names the first.
bool fpTunableApplyAssignmentList(const std::string& list, std::string& error);

// One resolved first-person pose, as `/fpcam dump` reports it. The renderer
// gathers it (only the renderer's camera TU, renderer_camera.cpp, may read
// the camera formula --
// tests/test_first_person_camera_single_formula.cpp) and this header's
// formatter turns it into lines, so the WORDING of the sweep's input format is
// testable without a camera and does not grow the renderer's TU.
struct FirstPersonPoseSample {
    std::string modeName;
    bool cameraNodePresent = false;
    bool eyeFromHeadBone = false;
    // The eye, Irrlicht world axes (EQ x -> X, EQ z -> Y, EQ y -> Z).
    float eyeIrrX = 0.0f, eyeIrrY = 0.0f, eyeIrrZ = 0.0f;
    float targetIrrX = 0.0f, targetIrrY = 0.0f, targetIrrZ = 0.0f;
    float upX = 0.0f, upY = 0.0f, upZ = 0.0f;
    // The player node's origin == the server's Z for this entity, EQ frame.
    float nodeOriginEqZ = 0.0f;
    float collisionZOffset = 0.0f;
    // The node-origin-relative eye offset the renderer fed to the formula.
    float eyeOffsetIrrX = 0.0f, eyeOffsetIrrY = 0.0f, eyeOffsetIrrZ = 0.0f;
    float headingEqUnits = 0.0f;
    float lookPitchDeg = 0.0f, fixedPitchDownDeg = 0.0f, totalPitchDeg = 0.0f;
    float maxDownPitchDeg = 0.0f, maxUpPitchDeg = 0.0f;
    float hfovDeg = 0.0f, vfovDeg = 0.0f, aspect = 0.0f, nearPlane = 0.0f;
};

// Every number states its UNITS and its FRAME in the key itself, because the
// two frames in play here have been confused before. One key=value group per
// line, all prefixed [FPCAM-DUMP], so a sweep driver can grep one line and
// split on spaces without a parser.
std::vector<std::string> FormatFirstPersonPoseDump(const FirstPersonPoseSample& s);

// The /fpcam command's text half -- everything except `dump`, which needs the
// live camera and is answered by IrrlichtRenderer. Returns the reply lines the
// caller pushes through its own chat/log sink; it touches no renderer state,
// so it is directly unit-testable with no device.
std::vector<std::string> FirstPersonTunablesCommandLines(const std::string& args);

}  // namespace Graphics
}  // namespace EQT
