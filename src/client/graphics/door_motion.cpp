// door_motion.cpp -- the measured door motion. See door_motion.h for every
// number's provenance; nothing here may be tuned without a new oracle session.
#include "client/graphics/door_motion.h"

#include <algorithm>
#include <cmath>

namespace EQT {
namespace Graphics {

bool DoorOpentypeIsLift(uint8_t opentype)
{
    return opentype == 59;
}

bool DoorTravels(uint8_t opentype, uint32_t doorParam)
{
    return DoorOpentypeIsLift(opentype) && doorParam > 0;
}

float LiftTravelSeconds(uint32_t doorParam)
{
    if (doorParam == 0) {
        return 0.0f;
    }
    return static_cast<float>(doorParam) / DoorMotion::kLiftUnitsPerSecond;
}

bool PlayerRidesLift(float feetZ, float surfaceZ, float tolerance)
{
    return std::fabs(feetZ - surfaceZ) <= tolerance;
}

void LiftCycle::init(float travelUnits)
{
    travel_ = std::max(0.0f, travelUnits);
    offset_ = travel_;   // parked at the far end -- see the header
    phase_ = LiftPhase::Parked;
    timer_ = 0.0f;
}

void LiftCycle::trigger()
{
    if (travel_ <= 0.0f || phase_ != LiftPhase::Parked) {
        return;
    }
    phase_ = LiftPhase::Outbound;
    timer_ = 0.0f;
}

void LiftCycle::advance(float deltaSeconds)
{
    if (phase_ == LiftPhase::Parked || deltaSeconds <= 0.0f) {
        return;
    }
    const float step = DoorMotion::kLiftUnitsPerSecond * deltaSeconds;
    switch (phase_) {
        case LiftPhase::Outbound:
            offset_ -= step;
            if (offset_ <= 0.0f) {
                offset_ = 0.0f;
                phase_ = LiftPhase::Dwell;
                timer_ = 0.0f;
            }
            break;
        case LiftPhase::Dwell:
            timer_ += deltaSeconds;
            if (timer_ >= DoorMotion::kLiftDwellSeconds) {
                phase_ = LiftPhase::Inbound;
                timer_ = 0.0f;
            }
            break;
        case LiftPhase::Inbound:
            offset_ += step;
            if (offset_ >= travel_) {
                offset_ = travel_;
                phase_ = LiftPhase::Parked;
                timer_ = 0.0f;
            }
            break;
        case LiftPhase::Parked:
            break;
    }
}

}  // namespace Graphics
}  // namespace EQT
