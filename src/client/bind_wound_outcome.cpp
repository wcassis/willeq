#include "client/bind_wound_outcome.h"

namespace EQT {

std::string bindWoundOutcomeMessage(uint16_t type) {
    switch (type) {
        case 1:
            return "You have bound your wounds.";
        case 4:
            return "You cannot bind wounds on that target.";
        case 5:
            return "Your bind wound target is no longer in the zone.";
        case 7:
            return "You have no bandages.";
        // 0 (unlock/reset), 3 (start control), and any unknown type carry no
        // outcome to show. The caller logs the raw type.
        default:
            return "";
    }
}

}  // namespace EQT
