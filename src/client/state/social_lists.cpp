/*
 * The friends list and the ignore list (B897, GitLab #213) -- see
 * include/client/state/social_lists.h for what is quoted from the official
 * client's files and what is fable's stated default.
 *
 * Free functions and one small class over std::string: no device, no
 * renderer, no file I/O (that is social_lists_io.cpp), so the command-layer
 * test binaries can link this TU on its own.
 */

#include "client/state/social_lists.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace eqt {
namespace state {

namespace {

std::string Trim(const std::string& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

bool IEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

// UIGAP-ORACLE/213 friends.tsv row 1/11: the official client keeps each list
// SORTED ("Nobody" before "testbuddy", "Nobody" before "Zzfriend" in its own
// ini). Case-insensitive order with a case-sensitive tie-break is fable's
// reading of those two pairs (both orders agree on them) -- an assumption.
bool NameLess(const std::string& a, const std::string& b) {
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        const int ca = std::tolower(static_cast<unsigned char>(a[i]));
        const int cb = std::tolower(static_cast<unsigned char>(b[i]));
        if (ca != cb) return ca < cb;
    }
    if (a.size() != b.size()) return a.size() < b.size();
    return a < b;
}

int IndexOf(const std::vector<std::string>& names, const std::string& name) {
    for (size_t i = 0; i < names.size(); ++i) {
        if (IEquals(names[i], name)) return static_cast<int>(i);
    }
    return -1;
}

bool IsEmptySlotValue(const std::string& v) {
    return v.empty() || v == kSocialEmptySlot;
}

// The same rule ui_preferences.cpp's SanitizePart applies to the UI_ file:
// a character name out of a packet and a server name out of config may not
// walk out of the cache directory.
std::string SanitizePart(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (const char c : in) {
        const unsigned char u = static_cast<unsigned char>(c);
        out.push_back((std::isalnum(u) || c == '_' || c == '-') ? c : '_');
    }
    if (out.empty()) return "unknown";
    return out;
}

const char* SectionName(SocialList list) {
    return list == SocialList::Friends ? "Friends" : "Ignored";
}

const char* KeyPrefix(SocialList list) {
    return list == SocialList::Friends ? "Friend" : "Ignored";
}

// `Friend37` -> 37 under prefix "Friend"; -1 for anything else (including
// the eqclient.ini `Friend00` family, which is not under [Friends]).
int SlotIndex(const std::string& key, const char* prefix) {
    const std::string p(prefix);
    if (key.size() <= p.size() || key.compare(0, p.size(), p) != 0) return -1;
    int v = 0;
    for (size_t i = p.size(); i < key.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(key[i]))) return -1;
        v = v * 10 + (key[i] - '0');
        if (v >= kSocialListCapacity) return -1;
    }
    return v;
}

}  // namespace

// --- the model -------------------------------------------------------------

bool SocialListsModel::contains(SocialList list, const std::string& name) const {
    if (name.empty()) return false;
    return IndexOf(names(list), name) >= 0;
}

SocialEditResult SocialListsModel::edit(SocialList list, SocialEditOp op,
                                        const std::string& rawName,
                                        const std::string& selfName) {
    const std::string name = Trim(rawName);
    if (name.empty() || name == kSocialEmptySlot) return SocialEditResult::Invalid;
    if (name.size() > kSocialNameMaxLength) return SocialEditResult::TooLong;

    auto& v = mutableNames(list);
    const int at = IndexOf(v, name);

    const bool wantRemove =
        op == SocialEditOp::Remove || (op == SocialEditOp::Toggle && at >= 0);
    if (wantRemove) {
        if (at < 0) return SocialEditResult::NotPresent;
        v.erase(v.begin() + at);
        ++m_revision;
        return SocialEditResult::Removed;
    }

    if (at >= 0) return SocialEditResult::AlreadyPresent;
    if (list == SocialList::Ignored && !selfName.empty() && IEquals(name, selfName)) {
        return SocialEditResult::Self;
    }
    if (static_cast<int>(v.size()) >= kSocialListCapacity) return SocialEditResult::Full;
    v.insert(std::lower_bound(v.begin(), v.end(), name, NameLess), name);
    ++m_revision;
    return SocialEditResult::Added;
}

void SocialListsModel::assign(const std::vector<std::string>& friends,
                              const std::vector<std::string>& ignored) {
    m_friends.clear();
    m_ignored.clear();
    for (const auto& n : friends) edit(SocialList::Friends, SocialEditOp::Add, n);
    for (const auto& n : ignored) edit(SocialList::Ignored, SocialEditOp::Add, n);
    ++m_revision;
}

// --- the oracle's text -----------------------------------------------------

std::string SocialDefaultString(uint32_t id) {
    // Quoted from /mnt/projects/EverQuestP1999/eqstr_us.txt, byte for byte
    // (13244 has TWO spaces after "already."). SocialListsStrings.
    // MatchTheOracleStringFile pins each against the file.
    switch (id) {
        case SocialStr::kFriendsOnline: return "Friends currently on EverQuest:";
        case SocialStr::kNoFriendsWho:
            return "You have no friends, but you can add some with: /friends <name>";
        case SocialStr::kNoFriends: return "You have no friends! Awww, how sad...";
        case SocialStr::kNoFriendsHint:
            return "Maybe you should add one by typing: /friend <name>";
        case SocialStr::kListOfFriends: return "List of Friends";
        case SocialStr::kFriendCount: return "You have %1 friend(s).";
        case SocialStr::kFriendTooLong: return "That is too long to be the name of a friend!";
        case SocialStr::kNoLongerFriend: return "%1 is no longer your friend.";
        case SocialStr::kFriendsFull:
            return "You may only have %1 friends in your list at once, please delete "
                   "one before adding another.";
        case SocialStr::kNowFriend: return "%1 is now your friend.";
        case SocialStr::kIgnoreHeader: return "NAMES OF THOSE CURRENTLY BEING IGNORED:";
        case SocialStr::kIgnoreRemoved: return "You have removed %1 from your IGNORE list.";
        case SocialStr::kCannotIgnore: return "That person cannot be ignored.";
        case SocialStr::kIgnoreAdded: return "You have added %1 to your IGNORE list.";
        case SocialStr::kIgnoreFull:
            return "You have all %1 ignore slots full already.  You must remove some "
                   "to add new ones.";
        default: return std::string();
    }
}

std::string FormatSocialString(uint32_t id, const std::string& arg1) {
    std::string s = SocialDefaultString(id);
    const size_t at = s.find("%1");
    if (at != std::string::npos) s.replace(at, 2, arg1);
    return s;
}

// --- the one command body --------------------------------------------------

std::string SocialNameFromArgs(const std::string& args) {
    const std::string t = Trim(args);
    size_t end = 0;
    while (end < t.size() && !std::isspace(static_cast<unsigned char>(t[end]))) ++end;
    return t.substr(0, end);
}

SocialOutcome ApplySocialEdit(SocialListsModel& model, SocialList list,
                              SocialEditOp op, const std::string& rawName,
                              const std::string& selfName) {
    SocialOutcome out;
    const std::string name = SocialNameFromArgs(rawName);
    // The stored spelling, so "Bob is no longer your friend." names the
    // entry the player sees in the list even when they typed "bob".
    std::string stored = name;
    for (const auto& n : model.names(list)) {
        if (n.size() == name.size() && IEquals(n, name)) { stored = n; break; }
    }
    const std::string cap = std::to_string(kSocialListCapacity);
    const bool friends = (list == SocialList::Friends);

    switch (model.edit(list, op, name, selfName)) {
        case SocialEditResult::Added:
            out.changed = true;
            out.lines.push_back(FormatSocialString(
                friends ? SocialStr::kNowFriend : SocialStr::kIgnoreAdded, name));
            break;
        case SocialEditResult::Removed:
            out.changed = true;
            out.lines.push_back(FormatSocialString(
                friends ? SocialStr::kNoLongerFriend : SocialStr::kIgnoreRemoved, stored));
            break;
        case SocialEditResult::Full:
            out.lines.push_back(FormatSocialString(
                friends ? SocialStr::kFriendsFull : SocialStr::kIgnoreFull, cap));
            break;
        case SocialEditResult::TooLong:
            out.lines.push_back(SocialDefaultString(
                friends ? SocialStr::kFriendTooLong : SocialStr::kCannotIgnore));
            break;
        case SocialEditResult::Self:
            out.lines.push_back(SocialDefaultString(SocialStr::kCannotIgnore));
            break;
        case SocialEditResult::AlreadyPresent:
        case SocialEditResult::NotPresent:
        case SocialEditResult::Invalid:
            break;   // assumption awaiting oracle: silent, nothing changes
    }
    return out;
}

SocialOutcome RunFriendsCommand(SocialListsModel& model, const std::string& args,
                                const std::string& selfName) {
    const std::string name = SocialNameFromArgs(args);
    if (!name.empty()) {
        return ApplySocialEdit(model, SocialList::Friends, SocialEditOp::Toggle, name,
                               selfName);
    }
    SocialOutcome out;
    const auto& f = model.friends();
    if (f.empty()) {
        out.lines.push_back(SocialDefaultString(SocialStr::kNoFriends));
        out.lines.push_back(SocialDefaultString(SocialStr::kNoFriendsHint));
        return out;
    }
    out.lines.push_back(SocialDefaultString(SocialStr::kListOfFriends));
    out.lines.push_back(kFriendsListRule);
    for (const auto& n : f) out.lines.push_back(n);
    out.lines.push_back(FormatSocialString(SocialStr::kFriendCount, std::to_string(f.size())));
    return out;
}

SocialOutcome RunIgnoreCommand(SocialListsModel& model, const std::string& args,
                               const std::string& selfName) {
    const std::string name = SocialNameFromArgs(args);
    if (!name.empty()) {
        return ApplySocialEdit(model, SocialList::Ignored, SocialEditOp::Toggle, name,
                               selfName);
    }
    SocialOutcome out;
    out.lines.push_back(SocialDefaultString(SocialStr::kIgnoreHeader));
    for (const auto& n : model.ignored()) out.lines.push_back(n);
    return out;
}

SocialOutcome RunFriendsWho(const SocialListsModel& model, const std::string& args) {
    SocialOutcome out;
    const std::string given = Trim(args);
    if (!given.empty()) {
        out.whoList = given;
        return out;
    }
    const auto& f = model.friends();
    if (f.empty()) {
        out.lines.push_back(SocialDefaultString(SocialStr::kNoFriendsWho));
        return out;
    }
    for (size_t i = 0; i < f.size(); ++i) {
        if (i) out.whoList += ',';
        out.whoList += f[i];
    }
    return out;
}

// --- the ini shape ---------------------------------------------------------

void ParseSocialListsIni(const std::string& text, std::vector<std::string>& friends,
                         std::vector<std::string>& ignored) {
    std::vector<std::pair<int, std::string>> f;
    std::vector<std::pair<int, std::string>> ig;
    std::istringstream in(text);
    std::string line;
    std::string section;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = Trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#') continue;
        if (t.front() == '[' && t.back() == ']') {
            section = t.substr(1, t.size() - 2);
            continue;
        }
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = Trim(t.substr(0, eq));
        const std::string value = Trim(t.substr(eq + 1));
        if (IsEmptySlotValue(value)) continue;
        if (section == SectionName(SocialList::Friends)) {
            const int i = SlotIndex(key, KeyPrefix(SocialList::Friends));
            if (i >= 0) f.emplace_back(i, value);
        } else if (section == SectionName(SocialList::Ignored)) {
            const int i = SlotIndex(key, KeyPrefix(SocialList::Ignored));
            if (i >= 0) ig.emplace_back(i, value);
        }
    }
    auto bySlot = [](const auto& a, const auto& b) { return a.first < b.first; };
    std::stable_sort(f.begin(), f.end(), bySlot);
    std::stable_sort(ig.begin(), ig.end(), bySlot);
    friends.clear();
    ignored.clear();
    for (auto& e : f) friends.push_back(std::move(e.second));
    for (auto& e : ig) ignored.push_back(std::move(e.second));
}

std::string MergeSocialListsIni(const std::string& existing,
                                const SocialListsModel& model) {
    // CRLF, because every official file is CRLF.
    const char* nl = "\r\n";
    std::string out;
    for (const SocialList list : {SocialList::Friends, SocialList::Ignored}) {
        const auto& v = model.names(list);
        const int first = kSocialListCapacity - static_cast<int>(v.size());
        out += "[";
        out += SectionName(list);
        out += "]";
        out += nl;
        for (int slot = 0; slot < kSocialListCapacity; ++slot) {
            out += KeyPrefix(list);
            out += std::to_string(slot);
            out += "=";
            out += (slot >= first) ? v[static_cast<size_t>(slot - first)]
                                   : std::string(kSocialEmptySlot);
            out += nl;
        }
    }

    // Every other section of the existing file, verbatim (line endings
    // normalised), so a copied official file keeps what fable does not own.
    std::istringstream in(existing);
    std::string line;
    bool skipping = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = Trim(line);
        if (!t.empty() && t.front() == '[' && t.back() == ']') {
            const std::string s = t.substr(1, t.size() - 2);
            skipping = (s == SectionName(SocialList::Friends) ||
                        s == SectionName(SocialList::Ignored));
        }
        if (skipping) continue;
        out += line;
        out += nl;
    }
    return out;
}

std::string SocialListsFileName(const std::string& character, const std::string& server) {
    return SanitizePart(character) + "_" + SanitizePart(server) + ".ini";
}

std::string SocialListsCacheName(const std::string& character, const std::string& server) {
    return "ui/" + SocialListsFileName(character, server);
}

}  // namespace state
}  // namespace eqt
