#include "client/eq_profile_appearance.h"

#include "common/net/packet.h"

namespace EQT {

ProfileAppearance ParseProfileAppearance(const EQ::Net::Packet& p, size_t base) {
    ProfileAppearance a;
    a.haircolor  = p.GetUInt8(base + PP_OFFSET_HAIRCOLOR);
    a.beardcolor = p.GetUInt8(base + PP_OFFSET_BEARDCOLOR);
    a.eyecolor1  = p.GetUInt8(base + PP_OFFSET_EYECOLOR1);
    a.eyecolor2  = p.GetUInt8(base + PP_OFFSET_EYECOLOR2);
    a.hairstyle  = p.GetUInt8(base + PP_OFFSET_HAIRSTYLE);
    a.beard      = p.GetUInt8(base + PP_OFFSET_BEARD);
    a.face       = p.GetUInt8(base + PP_OFFSET_FACE);
    a.valid = true;
    return a;
}

}  // namespace EQT
