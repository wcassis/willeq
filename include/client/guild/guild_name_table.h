#pragma once

// T443 (board B418) -- OP_GuildsList, parsed.
//
// Before this, `LoginWorldComponent::WorldProcessGuildsList()` was one
// `LOG_DEBUG(MOD_WORLD, "Received guilds list")` that read none of the
// packet, and `data/willeq.db` has no guild table, so the client had NO
// guild-id -> name source of any kind. That is the gap T429 Leg B measured
// behind `Entity::guildId` (board B418), the same shape as B391's missing
// item-name lookup.
//
// THE WIRE SHAPE (Titanium), quoted, not inferred:
//
//   /mnt/projects/akk-stack/code/common/patches/titanium_structs.h:1412
//       struct GuildsListEntry_Struct { char name[64]; };
//       static const uint32 MAX_NUMBER_GUILDS = 1500;
//       struct GuildsList_Struct {
//           uint8 head[64];   // "First on guild list seems to be empty..."
//           GuildsListEntry_Struct Guilds[MAX_NUMBER_GUILDS];
//       };
//
//   /mnt/projects/akk-stack/code/common/patches/titanium.cpp:819  ENCODE(OP_GuildsList)
//       auto outapp = new EQApplicationPacket(OP_GuildsList,
//                                             sizeof(structs::GuildsList_Struct));
//       for (auto const& g : glms.guild_detail) {
//           if (g.guild_id < Titanium::constants::MAX_GUILD_ID) {
//               strn0cpy(out->Guilds[g.guild_id].name, g.guild_name.c_str(),
//                        sizeof(out->Guilds[g.guild_id].name));
//           }
//       }
//
// Two facts a reader would otherwise assume wrongly, and both are why this
// is a table and not a list:
//
//   * THE ARRAY INDEX IS THE GUILD ID. The encoder writes
//     `out->Guilds[g.guild_id]` -- it does not append entries in order and
//     it sends no id field, so entry i IS guild i. A "walk the entries and
//     number them 1,2,3..." reading gives every guild the wrong name on any
//     server whose guild ids are not dense from zero.
//   * THE PACKET IS FIXED SIZE AND MOSTLY EMPTY. It is always
//     64 + 1500*64 = 96,064 bytes regardless of how many guilds exist, and
//     `MAX_GUILD_ID` is 1500
//     (/mnt/projects/akk-stack/code/common/patches/titanium_limits.h:287),
//     so a guild whose id is >= 1500 is DROPPED BY THE SERVER and can never
//     be resolved by this table at all. An unwritten slot is 64 zero bytes,
//     which is an absent guild, not a guild whose name is "".
//
// The 64-byte head is skipped: nothing writes it, and guild ids start at 0
// in the array that follows it. Names are NUL-terminated within their 64
// bytes and are NOT guaranteed to have one (strn0cpy fills exactly the
// field), so the scan is bounded by the field, not by the NUL.
//
// Device-free, database-free, allocation-bounded: only non-empty entries are
// stored, so a server with three guilds keeps three strings, not 1500.

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace eqt {
namespace guild {

// From titanium_structs.h / titanium_limits.h, quoted above.
constexpr size_t kGuildsListHeadBytes = 64;
constexpr size_t kGuildNameBytes = 64;
constexpr uint32_t kMaxNumberGuilds = 1500;
constexpr size_t kGuildsListBodyBytes =
    kGuildsListHeadBytes + static_cast<size_t>(kMaxNumberGuilds) * kGuildNameBytes;

class GuildNameTable {
public:
    // `data`/`len` are the packet BODY -- the bytes after fable's two-byte
    // opcode prefix. Returns the number of named guilds found. A body
    // shorter than the fixed struct is parsed as far as it goes and the
    // short read is the caller's to log; a longer one is truncated to the
    // 1500 entries the server can address. Replaces any previous contents:
    // the server sends the whole list, so this is a snapshot, not a delta.
    size_t parseTitaniumGuildsList(const uint8_t* data, size_t len);

    // The guild's name, or nullptr when this id has no name in the list.
    // The parameter is `id`, not `guildId`, ON PURPOSE: the WriteOnlyFields
    // hygiene sweep (scripts/refactor/check_write_only_fields.py) matches
    // member names tree-wide by NAME ALONE, so a parameter called `guildId`
    // reads as a read of `Entity::guildId` and would hide the very
    // write-only field B418 exists to track.
    // A caller that gets nullptr must draw NO guild text -- there is no
    // fallback string to invent, and the official client prints nothing
    // rather than an id (T363: nothing is displayed that is not quoted).
    const std::string* find(uint32_t id) const;

    size_t size() const { return names_.size(); }
    bool empty() const { return names_.empty(); }
    void clear() { names_.clear(); }

private:
    std::map<uint32_t, std::string> names_;
};

}  // namespace guild
}  // namespace eqt
