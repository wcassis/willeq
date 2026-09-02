// Group wire layouts: the group action constants and the seven group
// packet structures.
//
// Wire layouts, extracted verbatim from the umbrella header by
// refactor/J14_packet_structs_split.md. The RULES for this file family live
// in the umbrella, include/common/packet_structs.h -- read them before
// touching a struct or a compile-time size check here.
//
// This header opens and closes its own one-byte packing region; no include
// sits inside it.

#ifndef EQT_PACKETS_GROUP_H
#define EQT_PACKETS_GROUP_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "common/packets/common.h"

namespace EQT {

#pragma pack(push, 1)

// ============================================================================
// Group Structures (from titanium_structs.h)
// ============================================================================

// Group action constants
enum GroupAction : uint32_t {
    GROUP_ACT_JOIN = 0,
    GROUP_ACT_LEAVE = 1,
    GROUP_ACT_DISBAND = 6,
    GROUP_ACT_UPDATE = 7,
    GROUP_ACT_MAKE_LEADER = 8,
    GROUP_ACT_INVITE_INITIAL = 9,
    GROUP_ACT_AA_UPDATE = 10
};

// Generic group structure (used for simple operations)
struct GroupGeneric_Struct {
/*0000*/ char name1[64];
/*0064*/ char name2[64];
/*0128*/
};
static_assert(sizeof(GroupGeneric_Struct) == 128, "GroupGeneric_Struct must be 128 bytes");

// Group invite packet (client <-> server)
struct GroupInvite_Struct {
/*0000*/ char invitee_name[64];
/*0064*/ char inviter_name[64];
/*0128*/
};
static_assert(sizeof(GroupInvite_Struct) == 128, "GroupInvite_Struct must be 128 bytes");

// Group cancel invite
struct GroupCancel_Struct {
/*0000*/ char name1[64];
/*0064*/ char name2[64];
/*0128*/ uint8_t toggle;
/*0129*/
};
static_assert(sizeof(GroupCancel_Struct) == 129, "GroupCancel_Struct must be 129 bytes");

// Group join notification (server -> client, single member add/remove)
struct GroupJoin_Struct {
/*0000*/ uint32_t action;
/*0004*/ char yourname[64];
/*0068*/ char membername[64];
/*0132*/ uint8_t unknown[84];
/*0216*/
};
static_assert(sizeof(GroupJoin_Struct) == 216, "GroupJoin_Struct must be 216 bytes");

// Full group update (server -> client, complete group state)
struct GroupUpdate_Struct {
/*0000*/ uint32_t action;
/*0004*/ char yourname[64];
/*0068*/ char membername[5][64];  // 5 other members (excluding self)
/*0388*/ char leadersname[64];
/*0452*/
};
static_assert(sizeof(GroupUpdate_Struct) == 452, "GroupUpdate_Struct must be 452 bytes");

// Group follow request (accept invite, client -> server)
struct GroupFollow_Struct {
/*0000*/ char name1[64];  // Inviter name
/*0064*/ char name2[64];  // Invitee name (you)
/*0128*/
};
static_assert(sizeof(GroupFollow_Struct) == 128, "GroupFollow_Struct must be 128 bytes");

// Group disband (client -> server)
struct GroupDisband_Struct {
/*0000*/ char name1[64];
/*0064*/ char name2[64];
/*0128*/
};
static_assert(sizeof(GroupDisband_Struct) == 128, "GroupDisband_Struct must be 128 bytes");


#pragma pack(pop)

} // namespace EQT

#endif // EQT_PACKETS_GROUP_H
