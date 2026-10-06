#pragma once

// B888 (GitLab #204): the four world-stream packets of character creation and
// deletion, built and parsed exactly as eqemu's Titanium world reads them.
// Every builder returns the full fable packet (2-byte opcode prefix + body),
// the shape every other world sender in login_world_component.cpp queues.
//
//   OP_ApproveName (0x3ea6), client -> world: Titanium's NameApproval
//     (common/patches/titanium_structs.h:72-78): name[64], race, class_,
//     deity -- 76 bytes. The patch has no DECODE for it, so world reads the
//     first 72 through common/eq_packet_structs.h:646 NameApproval_Struct
//     (name, race_id, class_id) and never checks the size
//     (world/client.cpp:567). Which length the official client sends is an
//     assumption awaiting the oracle pcap; the bytes world reads are pinned.
//   OP_ApproveName, world -> client: ONE byte, 1 = approved, 0 = refused
//     (:622-626). World sends the same 1-byte 0 when OP_CharacterCreate
//     itself fails (:760-765), so the meaning depends on what was asked last.
//   OP_CharacterCreate (0x10b2): Titanium CharCreate_Struct, 80 bytes,
//     titanium_structs.h:576-612, DECODE_LENGTH_EXACT (titanium.cpp:2604) --
//     any other length is dropped. `tutorial` (+76) is ALWAYS 0: Will's
//     ruling, the character goes to its home city.
//   OP_RandomNameGenerator (0x23d4): NameGeneration_Struct, 72 bytes,
//     eq_packet_structs.h:657 (race, gender, name[64]); world fills `name` and
//     echoes the same struct back (:636-697).
//   OP_DeleteCharacter (0x26c9): the NUL-terminated name, read as a C string
//     (:1072-1082); world answers with a fresh OP_SendCharInfo. MEASURED
//     (B998, review/UIGAP-ORACLE/318/charselect.opcodes_hex.tsv 15:42:08): the
//     official client sends the name and its NUL, nothing more --
//     "Testmelee\0", 10 bytes. kDeleteCharacterBytes is the name's cap.

#include "client/login_world/char_create_rules.h"
#include "common/net/packet.h"

#include <cstdint>
#include <string>

namespace eqt {
namespace charcreate {

/** Everything one creation sends. Field names follow CharCreate_Struct. */
struct CharCreateRequest {
    std::string name;
    uint32_t race = 0;
    uint32_t gender = Gender::Male;
    uint32_t cls = 0;
    uint32_t deity = 0;
    uint32_t startZone = 0;   // the CC1_City<n> index == the server's StartZoneIndex
    Stats stats{};
    // FacePick (EQUI_FacePick.xml). fable has no FacePick window yet, so these
    // stay 0 -- the server stores them as given (world/client.cpp:1762-1768).
    uint32_t face = 0, hairstyle = 0, haircolor = 0, beard = 0, beardcolor = 0,
             eyecolor1 = 0, eyecolor2 = 0;
};

constexpr size_t kNameApprovalBytes = 76;
constexpr size_t kCharCreateBytes = 80;
constexpr size_t kNameGenerationBytes = 72;
constexpr size_t kDeleteCharacterBytes = 64;

EQ::Net::DynamicPacket BuildApproveName(const std::string& name, uint32_t race, uint32_t cls,
                                        uint32_t deity);
EQ::Net::DynamicPacket BuildCharacterCreate(const CharCreateRequest& req);
EQ::Net::DynamicPacket BuildRandomName(uint32_t race, uint32_t gender);
EQ::Net::DynamicPacket BuildDeleteCharacter(const std::string& name);

/** World's 1-byte reply. -1 when the packet carries no body byte. */
int ParseApproveNameReply(const uint8_t* body, size_t size);
/** World's echoed NameGeneration_Struct -> the generated name ("" if short). */
std::string ParseRandomNameReply(const uint8_t* body, size_t size);

}  // namespace charcreate
}  // namespace eqt
