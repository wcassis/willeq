#include "client/world_object.h"
#include "client/inventory/inventory_constants.h"
#include "common/packet_structs.h"

#include <algorithm>
#include <cstring>

namespace eqt {

bool WorldObject::isTradeskillContainer() const {
    return inventory::isTradeskillContainerType(static_cast<uint8_t>(object_type));
}

const char* WorldObject::getTradeskillName() const {
    return inventory::getTradeskillContainerName(static_cast<uint8_t>(object_type));
}

namespace {
constexpr size_t kObjectTypeOffset = offsetof(EQT::Object_Struct, object_type);
constexpr size_t kMinBodyBytes = kObjectTypeOffset + sizeof(uint32_t);  // 84
constexpr size_t kNameOffset = offsetof(EQT::Object_Struct, object_name);
constexpr size_t kNameMax = sizeof(EQT::Object_Struct::object_name);
}  // namespace

size_t GroundSpawnMinBodyBytes() {
    return kMinBodyBytes;
}

bool ParseGroundSpawn(const uint8_t* body, size_t body_len, WorldObject& out) {
    if (body == nullptr || body_len < kMinBodyBytes) {
        return false;
    }

    // Field-by-field reads at the Titanium wire offsets rather than a whole
    // struct cast: an OP_GroundSpawn carrying a 0-length object name is only
    // 91 bytes, one short of sizeof(Object_Struct), so a cast-and-copy would
    // read past the packet. See the header comment on Object_Struct.
    WorldObject obj;
    std::memcpy(&obj.drop_id, body + offsetof(EQT::Object_Struct, drop_id), sizeof(obj.drop_id));
    std::memcpy(&obj.zone_id, body + offsetof(EQT::Object_Struct, zone_id), sizeof(obj.zone_id));
    std::memcpy(&obj.zone_instance, body + offsetof(EQT::Object_Struct, zone_instance),
                sizeof(obj.zone_instance));
    std::memcpy(&obj.heading, body + offsetof(EQT::Object_Struct, heading), sizeof(obj.heading));
    std::memcpy(&obj.z, body + offsetof(EQT::Object_Struct, z), sizeof(obj.z));
    std::memcpy(&obj.x, body + offsetof(EQT::Object_Struct, x), sizeof(obj.x));
    std::memcpy(&obj.y, body + offsetof(EQT::Object_Struct, y), sizeof(obj.y));
    std::memcpy(&obj.object_type, body + kObjectTypeOffset, sizeof(obj.object_type));

    // object_name: NUL-terminated, at most 32 bytes. The length bound is
    // belt-and-braces only: body_len is at least 84 here, so at least 40
    // bytes remain from offset 44 and the 32-byte field is always whole. The
    // bound exists so a malformed short packet can never walk off the end.
    const size_t name_avail = std::min(kNameMax, body_len - kNameOffset);
    const char* name_ptr = reinterpret_cast<const char*>(body + kNameOffset);
    obj.name.assign(name_ptr, ::strnlen(name_ptr, name_avail));

    out = std::move(obj);
    return true;
}

} // namespace eqt
