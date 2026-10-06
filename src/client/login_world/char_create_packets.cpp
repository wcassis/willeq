// B888 (GitLab #204) -- character creation / deletion packets. The struct
// citations are in include/client/login_world/char_create_packets.h.

#include "client/login_world/char_create_packets.h"

#include "client/eq_opcodes.h"

#include <algorithm>
#include <cstring>

namespace eqt {
namespace charcreate {

namespace {

constexpr size_t kOp = 2;  // fable's opcode prefix

void putName(EQ::Net::DynamicPacket& p, size_t offset, const std::string& name, size_t field) {
    const size_t n = std::min(name.size(), field - 1);  // always NUL-terminated
    for (size_t i = 0; i < n; ++i) p.PutUInt8(offset + i, static_cast<uint8_t>(name[i]));
}

EQ::Net::DynamicPacket sized(uint16_t opcode, size_t body) {
    EQ::Net::DynamicPacket p;
    p.Resize(kOp + body);  // DynamicPacket::Resize zero-fills
    p.PutUInt16(0, opcode);
    return p;
}

}  // namespace

EQ::Net::DynamicPacket BuildApproveName(const std::string& name, uint32_t race, uint32_t cls,
                                        uint32_t deity) {
    auto p = sized(HC_OP_ApproveName, kNameApprovalBytes);
    putName(p, kOp + 0, name, 64);
    p.PutUInt32(kOp + 64, race);
    p.PutUInt32(kOp + 68, cls);
    p.PutUInt32(kOp + 72, deity);
    return p;
}

EQ::Net::DynamicPacket BuildCharacterCreate(const CharCreateRequest& r) {
    // titanium_structs.h:576-612, offset by offset. Titanium's DECODE copies
    // field-by-NAME into the server struct (IN(haircolor) etc.), so the names
    // at these offsets are what matter, whatever the struct's comments guess.
    auto p = sized(HC_OP_CharacterCreate, kCharCreateBytes);
    p.PutUInt32(kOp + 0, r.cls);
    p.PutUInt32(kOp + 4, r.haircolor);
    p.PutUInt32(kOp + 8, r.beardcolor);
    p.PutUInt32(kOp + 12, r.beard);
    p.PutUInt32(kOp + 16, r.gender);
    p.PutUInt32(kOp + 20, r.race);
    p.PutUInt32(kOp + 24, r.startZone);
    p.PutUInt32(kOp + 28, r.hairstyle);
    p.PutUInt32(kOp + 32, r.deity);
    for (int i = 0; i < kStatCount; ++i) {
        p.PutUInt32(kOp + 36 + 4 * static_cast<size_t>(i),
                    static_cast<uint32_t>(r.stats[static_cast<size_t>(i)]));
    }
    p.PutUInt32(kOp + 64, r.face);
    p.PutUInt32(kOp + 68, r.eyecolor1);
    p.PutUInt32(kOp + 72, r.eyecolor2);
    p.PutUInt32(kOp + 76, 0);  // tutorial: never (Will, 2026-09-23)
    return p;
}

EQ::Net::DynamicPacket BuildRandomName(uint32_t race, uint32_t gender) {
    auto p = sized(HC_OP_RandomNameGenerator, kNameGenerationBytes);
    p.PutUInt32(kOp + 0, race);
    p.PutUInt32(kOp + 4, gender);
    return p;
}

EQ::Net::DynamicPacket BuildDeleteCharacter(const std::string& name) {
    // strlen + 1, as the official client sends it (B998's pcap; header).
    const size_t body = std::min(name.size(), kDeleteCharacterBytes - 1) + 1;
    auto p = sized(HC_OP_DeleteCharacter, body);
    putName(p, kOp, name, body);
    return p;
}

int ParseApproveNameReply(const uint8_t* body, size_t size) {
    if (!body || size < 1) return -1;
    return body[0] ? 1 : 0;
}

std::string ParseRandomNameReply(const uint8_t* body, size_t size) {
    if (!body || size < kNameGenerationBytes) return std::string();
    const char* name = reinterpret_cast<const char*>(body + 8);
    return std::string(name, strnlen(name, 64));
}

}  // namespace charcreate
}  // namespace eqt
