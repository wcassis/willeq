/*
 * Game-side options the Options window drives (B430/B433).
 *
 * These are the Options-window settings whose AUTHORITY is the server, not
 * the renderer: the server keeps them in the player profile (or, for the chat
 * filters, in its per-client filter table) and the client changes them with a
 * packet. The renderer therefore never persists them; it asks for a change
 * with `GameOptionIntent` and shows what the game thread publishes back on
 * `GameEventType::GameOptionChanged` -- the T439 VolumesChanged shape, so a
 * slash command and a click can never disagree.
 *
 * Wire facts, each read from the eqemu server source the akk-stack builds
 * (/mnt/projects/akk-stack/code):
 *   AnonStatus     OP_SpawnAppearance type 21 (AppearanceType::Anonymous):
 *                  0 normal, 1 anonymous, 2 roleplaying -- the server stores
 *                  ONE of the three (zone/client_packet.cpp folds 3 into 2),
 *                  PlayerProfile.anon @13053.
 *   AutoSplit      OP_SpawnAppearance type 28 (AppearanceType::Split), 0/1,
 *                  PlayerProfile.autosplit @13256.
 *   AutoConsent*   OP_SpawnAppearance types 40/41/42 (Group/Raid/Guild
 *                  AutoConsent), 0/1, PlayerProfile @19572/19573/19574.
 *   ServerFilters  OP_SetServerFilter, uint32 filters[29], one slot per
 *                  eqemu eqFilterType (EQT::ServerFilterSlot, packets/guild.h).
 *
 * Device-free and dependency-free: both threads include it.
 */

#pragma once

#include <array>
#include <cstdint>

namespace eqt {

enum class GameOption : uint8_t {
    AnonStatus = 0,      // 0 normal, 1 anonymous, 2 roleplaying
    AutoSplit,           // 0/1
    AutoConsentGroup,    // 0/1
    AutoConsentRaid,     // 0/1
    AutoConsentGuild,    // 0/1
    ServerFilters,       // the whole OP_SetServerFilter payload, `filters`
    Count
};

/** The OP_SpawnAppearance type a scalar game option is sent as, or -1. */
constexpr int gameOptionAppearanceType(GameOption o) {
    switch (o) {
        case GameOption::AnonStatus:       return 21;
        case GameOption::AutoSplit:        return 28;
        case GameOption::AutoConsentGroup: return 40;
        case GameOption::AutoConsentRaid:  return 41;
        case GameOption::AutoConsentGuild: return 42;
        default:                           return -1;
    }
}

/** Number of OP_SetServerFilter slots (SetServerFilter_Struct). */
constexpr int kServerFilterSlots = 29;

using ServerFilterPayload = std::array<uint32_t, kServerFilterSlots>;

/**
 * T243's show-everything payload -- what a client that never touched a filter
 * sends. The slot conventions are NOT uniform (eqemu Client::ServerFilter()):
 * the eleven Filter0 slots (guild chat .. bad words, my misses .. missed me)
 * show only for the exact value 1; every other slot shows for 0. Moved here
 * from ZoneComponent::ZoneSendSetServerFilter() so the Options window's
 * Filters page and the zone-in packet start from the one table.
 */
inline ServerFilterPayload defaultServerFilterPayload() {
    static constexpr bool kShowIsOne[kServerFilterSlots] = {
        false,                                            // 0  none
        true, true, true, true, true, true, true,         // 1-7  guild/socials/group/shouts/auctions/ooc/badwords
        false, false, false, false, false, false,         // 8-13 pc spells .. spell damage
        true, true, true, true,                           // 14-17 my misses / others miss / others hit / missed me
        false, false, false, false, false, false,         // 18-23 damage shields .. pet spells
        false, false, false, false, false                 // 24-28 heal over time .. bard songs on pets
    };
    ServerFilterPayload p{};
    for (int i = 0; i < kServerFilterSlots; ++i) p[i] = kShowIsOne[i] ? 1u : 0u;
    return p;
}

}  // namespace eqt
