// T443 (board B418) -- see include/client/guild/guild_name_table.h for the
// wire shape and its citations.

#include "client/guild/guild_name_table.h"

namespace eqt {
namespace guild {

size_t GuildNameTable::parseTitaniumGuildsList(const uint8_t* data, size_t len) {
    names_.clear();
    if (data == nullptr || len <= kGuildsListHeadBytes) return 0;

    const size_t entryBytes = len - kGuildsListHeadBytes;
    size_t entries = entryBytes / kGuildNameBytes;
    if (entries > kMaxNumberGuilds) entries = kMaxNumberGuilds;

    for (size_t i = 0; i < entries; ++i) {
        const uint8_t* field = data + kGuildsListHeadBytes + i * kGuildNameBytes;
        // Bounded by the field, not by a NUL: strn0cpy may fill all 64 bytes.
        size_t n = 0;
        while (n < kGuildNameBytes && field[n] != 0) ++n;
        if (n == 0) continue;  // an unwritten slot is an absent guild
        names_.emplace(static_cast<uint32_t>(i),
                       std::string(reinterpret_cast<const char*>(field), n));
    }
    return names_.size();
}

const std::string* GuildNameTable::find(uint32_t id) const {
    auto it = names_.find(id);
    return it == names_.end() ? nullptr : &it->second;
}

}  // namespace guild
}  // namespace eqt
