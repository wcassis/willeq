// See include/client/skill/behind_target.h for the authority and the heading
// convention. This is a port of eqemu's Mob::MobAngle (zone/mob.cpp:4576),
// kept structurally identical to it on purpose -- including the literal
// 3.1415 and the acos domain clamps -- so the boundary case agrees.

#include "client/skill/behind_target.h"

#include <cmath>

namespace EQ {
namespace skill {

float MobAngleDegrees(float target_server_x, float target_server_y,
                      float target_heading_deg,
                      float attacker_server_x, float attacker_server_y) {
    // eqemu: `float mobx = -(other->GetX()); // mob xloc (inverse because eq)`
    const float mobx = -target_server_x;
    const float moby = target_server_y;

    // eqemu converts GetHeading() (0..512) to degrees here; Entity::heading is
    // already that value, so only the quadrant rotation is left to do.
    float heading = target_heading_deg;
    if (heading < 270.0f) {
        heading += 90.0f;
    } else {
        heading -= 270.0f;
    }
    heading = heading * 3.1415f / 180.0f;

    // A vector of length 10 along the target's facing.
    const float vectorx = mobx + (10.0f * std::cos(heading));
    const float vectory = moby + (10.0f * std::sin(heading));

    const float ourx = -attacker_server_x;
    const float oury = attacker_server_y;

    const float lengthb =
        std::sqrt(((ourx - mobx) * (ourx - mobx)) + ((oury - moby) * (oury - moby)));
    if (!(lengthb > 0.0f)) {
        // Same point (or NaN input). eqemu divides by zero here; we report
        // "dead ahead", which makes IsBehindTarget() false rather than
        // indeterminate.
        return 0.0f;
    }

    const float dotp = ((vectorx - mobx) * (ourx - mobx) +
                        (vectory - moby) * (oury - moby)) / (10.0f * lengthb);
    if (dotp > 1.0f) {
        return 0.0f;
    }
    if (dotp < -1.0f) {
        return 180.0f;
    }

    return std::acos(dotp) * 180.0f / 3.1415f;
}

bool IsBehindTarget(float target_server_x, float target_server_y,
                    float target_heading_deg,
                    float attacker_server_x, float attacker_server_y) {
    // eqemu Mob::BehindMob: "greater than 90 is behind".
    return MobAngleDegrees(target_server_x, target_server_y, target_heading_deg,
                           attacker_server_x, attacker_server_y) > 90.0f;
}

}  // namespace skill
}  // namespace EQ
