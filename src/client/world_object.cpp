#include "client/world_object.h"
#include "client/graphics/ui/inventory_constants.h"

namespace eqt {

bool WorldObject::isTradeskillContainer() const {
    return inventory::isTradeskillContainerType(static_cast<uint8_t>(object_type));
}

const char* WorldObject::getTradeskillName() const {
    return inventory::getTradeskillContainerName(static_cast<uint8_t>(object_type));
}

} // namespace eqt
