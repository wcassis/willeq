#include "client/who_response.h"

#include <cctype>
#include <cstring>

namespace EQT {

namespace {

// Reads a little-endian u32 at *pos, advancing it. False if out of bounds.
bool readU32(const uint8_t* data, size_t len, size_t& pos, uint32_t& v) {
    if (pos + 4 > len) {
        return false;
    }
    v = static_cast<uint32_t>(data[pos]) |
        (static_cast<uint32_t>(data[pos + 1]) << 8) |
        (static_cast<uint32_t>(data[pos + 2]) << 16) |
        (static_cast<uint32_t>(data[pos + 3]) << 24);
    pos += 4;
    return true;
}

// Reads a NUL-terminated string at *pos, advancing past the NUL. False if
// no terminator exists inside the buffer.
bool readCString(const uint8_t* data, size_t len, size_t& pos, std::string& s) {
    size_t end = pos;
    while (end < len && data[end] != 0) {
        ++end;
    }
    if (end >= len) {
        return false;
    }
    s.assign(reinterpret_cast<const char*>(data + pos), end - pos);
    pos = end + 1;
    return true;
}

bool parseOne(const uint8_t* data, size_t len, size_t& pos, WhoAllEntry& e) {
    return readU32(data, len, pos, e.formatString) &&
           readU32(data, len, pos, e.pidString) &&
           readCString(data, len, pos, e.name) &&
           readU32(data, len, pos, e.rankString) &&
           readCString(data, len, pos, e.guild) &&
           readU32(data, len, pos, e.unknown80[0]) &&
           readU32(data, len, pos, e.unknown80[1]) &&
           readU32(data, len, pos, e.zoneString) &&
           readU32(data, len, pos, e.zone) &&
           readU32(data, len, pos, e.classId) &&
           readU32(data, len, pos, e.level) &&
           readU32(data, len, pos, e.race) &&
           readCString(data, len, pos, e.account) &&
           readU32(data, len, pos, e.unknown100);
}

std::string lookup(const WhoFormatContext& ctx, uint32_t id) {
    if (id == kWhoStringNone || id == 0) {
        return "";
    }
    std::string s;
    if (ctx.eqstr) {
        s = ctx.eqstr(id);
    }
    if (s.empty()) {
        s = whoDefaultString(id);
    }
    return s;
}

std::string collapseBlanks(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    bool pendingSpace = false;
    for (char c : in) {
        if (c == ' ') {
            pendingSpace = true;
            continue;
        }
        if (pendingSpace && !out.empty()) {
            out += ' ';
        }
        pendingSpace = false;
        out += c;
    }
    return out;
}

}  // namespace

bool parseWhoAllEntries(const uint8_t* data, size_t len, uint32_t count,
                        std::vector<WhoAllEntry>& out) {
    size_t pos = 0;
    for (uint32_t i = 0; i < count; ++i) {
        WhoAllEntry e;
        if (!parseOne(data, len, pos, e)) {
            return false;
        }
        out.push_back(std::move(e));
    }
    return true;
}

std::string whoDefaultString(uint32_t id) {
    switch (id) {
        case kWhoStrPlayersInEq:    return "Players in EverQuest:";
        case kWhoStrZone:           return "ZONE: %1";
        case kWhoStrFormatRoleplay: return "%T1[ANONYMOUS] %2 %3 %4";
        case kWhoStrFormatAnon:     return "%T1[ANONYMOUS] %2 %3";
        case kWhoStrFormatNormal:   return "%T1[%2 %3] %4 (%5) %6 %7 %8 %9";
        case kWhoStrOnePlayer:      return "There is %1 player in EverQuest.";
        case kWhoStrNoPlayers:
            return "There are no players in EverQuest that match those who filters.";
        case kWhoStrManyPlayers:    return "There are %1 players in EverQuest.";
        case kWhoStrBuyer:          return "BUYER";
        case kWhoStrGm:             return "* GM * ";
        case kWhoStrLinkdead:       return " <LINKDEAD>";
        case kWhoStrTrader:         return " TRADER";
        default:                    return "";
    }
}

std::string formatWhoTemplate(const std::string& tmpl,
                              const std::vector<std::string>& args,
                              const WhoFormatContext& ctx) {
    std::string out;
    out.reserve(tmpl.size() + 32);
    size_t i = 0;
    while (i < tmpl.size()) {
        char c = tmpl[i];
        if (c != '%' || i + 1 >= tmpl.size()) {
            out += c;
            ++i;
            continue;
        }
        size_t j = i + 1;
        bool isT = (tmpl[j] == 'T' || tmpl[j] == 't');
        if (isT) {
            ++j;
        }
        size_t numStart = j;
        while (j < tmpl.size() && std::isdigit(static_cast<unsigned char>(tmpl[j]))) {
            ++j;
        }
        if (j == numStart) {
            // Not a placeholder ("%" followed by something else) -- literal.
            out += c;
            ++i;
            continue;
        }
        size_t idx = static_cast<size_t>(std::stoul(tmpl.substr(numStart, j - numStart)));
        std::string arg = (idx >= 1 && idx <= args.size()) ? args[idx - 1] : "";
        if (isT && !arg.empty()) {
            // %Tn: the arg is a string id; look it up and format recursively
            // with the same args (eqemu's nested-template convention).
            uint32_t id = 0;
            bool numeric = !arg.empty();
            for (char d : arg) {
                if (!std::isdigit(static_cast<unsigned char>(d))) {
                    numeric = false;
                    break;
                }
            }
            if (numeric) {
                id = static_cast<uint32_t>(std::stoul(arg));
                std::string looked = lookup(ctx, id);
                arg = looked.empty() ? "" : formatWhoTemplate(looked, args, ctx);
            }
        }
        out += arg;
        i = j;
    }
    return out;
}

std::string formatWhoAllEntry(const WhoAllEntry& e, const WhoFormatContext& ctx) {
    const std::string rank = (e.rankString == kWhoStringNone || e.rankString == 0)
                                 ? "" : std::to_string(e.rankString);

    std::string zone;
    if (e.zoneString != kWhoStringNone && e.zoneString != 0) {
        std::string zoneTmpl = lookup(ctx, e.zoneString);
        std::string zname = ctx.zoneName ? ctx.zoneName(e.zone) : "";
        if (zname.empty()) {
            zname = std::to_string(e.zone);
        }
        zone = zoneTmpl.empty() ? zname : formatWhoTemplate(zoneTmpl, {zname}, ctx);
    }

    std::string linkdead;
    if (e.unknown80[1] != kWhoStringNone && e.unknown80[1] != 0) {
        linkdead = lookup(ctx, e.unknown80[1]);
    }

    std::vector<std::string> args;
    switch (e.formatString) {
        case kWhoStrFormatAnon:
            args = {rank, e.name, e.guild};
            break;
        case kWhoStrFormatRoleplay:
            args = {rank, e.name, e.guild, zone};
            break;
        default: {
            // 5025 and anything unrecognised: the full layout.
            std::string cls = ctx.className ? ctx.className(e.classId)
                                            : std::to_string(e.classId);
            std::string race = ctx.raceName ? ctx.raceName(e.race)
                                            : std::to_string(e.race);
            args = {rank, std::to_string(e.level), cls, e.name, race,
                    e.guild, zone, linkdead, e.account};
            break;
        }
    }

    std::string tmpl = lookup(ctx, e.formatString);
    if (tmpl.empty()) {
        tmpl = whoDefaultString(kWhoStrFormatNormal);
    }
    return collapseBlanks(formatWhoTemplate(tmpl, args, ctx));
}

}  // namespace EQT
