/*
 * The friends list and the ignore list -- the model, its on-disk shape and
 * the text of every message it prints (B897, GitLab #213).
 *
 * WHAT THE OFFICIAL CLIENT DOES, from its own files (not guessed):
 *
 *   - ONE window holds both lists: /mnt/projects/EverQuestP1999/uifiles/
 *     default/EQUI_FriendsWnd.xml, a TabBox with FW_FriendsPage and
 *     FW_IgnorePage (include/client/graphics/ui/friends_layout.h quotes it).
 *   - Both lists are CLIENT-SIDE. eqemu's Titanium opcode table has
 *     OP_FriendsWho and nothing else for either list
 *     (/mnt/projects/akk-stack/code/utils/patches/patch_Titanium.conf:542),
 *     and no ignore opcode exists at all, so the server never learns either
 *     list; the only wire traffic is the friends-who query.
 *   - Both persist in `<Char>_<server>.ini` (NOT the `UI_` file), as
 *     `[Friends] Friend0..Friend99` and `[Ignored] Ignored0..Ignored99`, an
 *     empty slot written `*NULL*` -- e.g.
 *     /mnt/projects/EverQuestP1999/Obian_project1999.ini lines 1 and 102.
 *     So the capacity is 100 each, and that is the number eqstr 12983 /
 *     13244 report.
 *   - Names are stored AS TYPED: the same file holds `Friend93=Xenephon` and
 *     `Friend94=bilblow`, and Bilblow_project1999.ini holds both
 *     `Friend93=Gudknobadd` and Badong_project1999.ini `Friend95=gudknobadd`.
 *     Nothing case-folds the stored text.
 *   - Every real file on disk fills its slots from the END: the occupied
 *     entries are always the last n (Friend93..99, Friend97..99,
 *     Ignored99 in Logjam_project1999.ini) and the rest are `*NULL*`. The
 *     writer below reproduces that shape exactly.
 *
 * MEASURED on the official client (review/UIGAP-ORACLE/213/friends.tsv,
 * the ledger rows are cited where each rule is applied):
 *   - `/friend <name>` and `/ignore <name>` TOGGLE (rows 4, 10); no existence
 *     check (row 3); stored as typed (row 5); self-ignore prints 13242
 *     (row 8); add/remove send NOTHING on the wire (rows 1-17).
 *   - Each list is kept SORTED and fills from the END (rows 1, 11, 17).
 *
 * NOT MEASURED, and therefore fable's stated default, listed under
 * "assumptions awaiting oracle" in B897's MR and in
 * refactor/B897_friends_window.md:
 *   - MATCHING is case-insensitive (EQ character names are); storage is not.
 *   - The sort is case-insensitive (both measured pairs agree with it).
 *   - The 100 cap and its messages 12983 / 13244 (row 18: not captured).
 *   - "Too long" (eqstr 12981) is more than kSocialNameMaxLength characters
 *     -- the 64-byte name buffer every Titanium struct uses, less its NUL.
 *     eqemu's world drops a whole friends-who request whose segment is longer
 *     than 64 (world/clientlist.cpp, ClientList::SendFriendsWho).
 *   - "That person cannot be ignored." (13242) is printed for yourself.
 *   - An Add of a name already present, and a Delete of one that is not,
 *     print nothing and change nothing.
 *
 * Device-free and graphics-free on purpose: the game thread owns the model
 * (state::GameState::social()), the slash commands and the window's intents
 * both call the SAME free functions below, and the chat receive path asks
 * isIgnored(). Disk I/O is NOT here -- it is social_lists_io.h, so the
 * command-layer test binaries link this TU without the cache-path resolver.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace eqt {
namespace state {

/** The two lists, in the TabBox's own page order. */
enum class SocialList : uint8_t {
    Friends = 0,   // FW_FriendsPage, [Friends] FriendN
    Ignored = 1,   // FW_IgnorePage,  [Ignored] IgnoredN
};

/** Friend0..Friend99 / Ignored0..Ignored99. */
constexpr int kSocialListCapacity = 100;

/** See the header comment: assumption, the 64-byte name buffer less NUL. */
constexpr size_t kSocialNameMaxLength = 63;

/** The oracle's empty-slot marker. */
constexpr const char* kSocialEmptySlot = "*NULL*";

/** What one edit did. */
enum class SocialEditResult : uint8_t {
    Added,
    Removed,
    AlreadyPresent,   // Add of a name already on the list: no change
    NotPresent,       // Remove of a name not on the list: no change
    Full,             // the list holds kSocialListCapacity already
    TooLong,          // longer than kSocialNameMaxLength
    Invalid,          // empty, or the literal *NULL* marker
    Self,             // /ignore <yourself>
};

/** The three edits the window and the slash commands can ask for. */
enum class SocialEditOp : uint8_t {
    Add,      // FW_AddButton / FW_IgnoreAddButton
    Remove,   // FW_DeleteButton / FW_IgnoreDeleteButton
    Toggle,   // /friends <name>, /ignore <name> -- the oracle's toggle
};

class SocialListsModel {
public:
    const std::vector<std::string>& names(SocialList list) const {
        return list == SocialList::Friends ? m_friends : m_ignored;
    }
    const std::vector<std::string>& friends() const { return m_friends; }
    const std::vector<std::string>& ignored() const { return m_ignored; }

    /** Case-insensitive membership. */
    bool contains(SocialList list, const std::string& name) const;
    /** THE chat-filter predicate. False for an empty name. */
    bool isIgnored(const std::string& name) const {
        return contains(SocialList::Ignored, name);
    }

    /**
     * One edit. `selfName` is the player's own name (only /ignore refuses
     * it). Bumps revision() whenever the list changed.
     */
    SocialEditResult edit(SocialList list, SocialEditOp op,
                          const std::string& name,
                          const std::string& selfName = std::string());

    /**
     * Replace both lists wholesale (a load). Names are validated, deduplicated
     * case-insensitively and capped exactly as edit() would.
     */
    void assign(const std::vector<std::string>& friends,
                const std::vector<std::string>& ignored);

    void clear() { assign({}, {}); }

    /** Monotonic change counter: the persistence tick compares it. */
    uint64_t revision() const { return m_revision; }

private:
    std::vector<std::string>& mutableNames(SocialList list) {
        return list == SocialList::Friends ? m_friends : m_ignored;
    }

    std::vector<std::string> m_friends;
    std::vector<std::string> m_ignored;
    uint64_t m_revision = 0;
};

// --- the oracle's text ------------------------------------------------------

/**
 * The eqstr ids this feature prints, and their text QUOTED from
 * /mnt/projects/EverQuestP1999/eqstr_us.txt (tests pin every one against the
 * file when it is present).
 */
namespace SocialStr {
constexpr uint32_t kFriendsOnline = 12307;      // Friends currently on EverQuest:
constexpr uint32_t kNoFriendsWho = 12308;       // You have no friends, but you can add some with: /friends <name>
constexpr uint32_t kNoFriends = 12977;          // You have no friends! Awww, how sad...
constexpr uint32_t kNoFriendsHint = 12978;      // Maybe you should add one by typing: /friend <name>
constexpr uint32_t kListOfFriends = 12979;      // List of Friends
constexpr uint32_t kFriendCount = 12980;        // You have %1 friend(s).
constexpr uint32_t kFriendTooLong = 12981;      // That is too long to be the name of a friend!
constexpr uint32_t kNoLongerFriend = 12982;     // %1 is no longer your friend.
constexpr uint32_t kFriendsFull = 12983;        // You may only have %1 friends ...
constexpr uint32_t kNowFriend = 12984;          // %1 is now your friend.
constexpr uint32_t kIgnoreHeader = 13240;       // NAMES OF THOSE CURRENTLY BEING IGNORED:
constexpr uint32_t kIgnoreRemoved = 13241;      // You have removed %1 from your IGNORE list.
constexpr uint32_t kCannotIgnore = 13242;       // That person cannot be ignored.
constexpr uint32_t kIgnoreAdded = 13243;        // You have added %1 to your IGNORE list.
constexpr uint32_t kIgnoreFull = 13244;         // You have all %1 ignore slots full already. ...
}  // namespace SocialStr

/**
 * The two rule lines the official client prints under a heading, MEASURED
 * (review/UIGAP-ORACLE/213/A.chat.txt): 17 dashes under "List of Friends"
 * (friends.tsv row 2) and 33 under "Friends currently on EverQuest:"
 * (row 13). Neither is in eqstr_us.txt.
 */
constexpr const char* kFriendsListRule = "-----------------";
constexpr const char* kFriendsOnlineRule = "---------------------------------";

/** The built-in copy of one of the ids above; empty for any other id. */
std::string SocialDefaultString(uint32_t id);

/** SocialDefaultString(id) with `%1` replaced by `arg1`. */
std::string FormatSocialString(uint32_t id, const std::string& arg1 = std::string());

// --- the one command body ---------------------------------------------------

/**
 * What a command or a window click produced: the chat lines to print (in
 * order) and, for the who query, the comma-delimited name list to send as
 * OP_FriendsWho. Both the slash commands and the window's intents run these,
 * so the two cannot say different things.
 */
struct SocialOutcome {
    std::vector<std::string> lines;
    std::string whoList;        // non-empty: send OP_FriendsWho with it
    bool changed = false;
};

/**
 * One edit, with the oracle's message for its result. `list` picks which of
 * the two message families is used.
 */
SocialOutcome ApplySocialEdit(SocialListsModel& model, SocialList list,
                              SocialEditOp op, const std::string& name,
                              const std::string& selfName);

/** `/friends` (no argument: the list) and `/friends <name>` (toggle). */
SocialOutcome RunFriendsCommand(SocialListsModel& model, const std::string& args,
                                const std::string& selfName);

/** `/ignore` (no argument: the list) and `/ignore <name>` (toggle). */
SocialOutcome RunIgnoreCommand(SocialListsModel& model, const std::string& args,
                               const std::string& selfName);

/**
 * `/friendswho` and FW_WhoButton. No argument asks about the STORED friends
 * list (12308 when it is empty, nothing sent); an argument is sent verbatim,
 * which is what /friendswho did before B897.
 */
SocialOutcome RunFriendsWho(const SocialListsModel& model, const std::string& args);

/** The name a command argument or an Editbox carries: its first word, trimmed. */
std::string SocialNameFromArgs(const std::string& args);

// --- the ini shape ----------------------------------------------------------

/**
 * Read `[Friends] FriendN` and `[Ignored] IgnoredN` (N 0..99) out of the
 * oracle's `<Char>_<server>.ini` text. `*NULL*` and empty values are empty
 * slots; names come back in slot order. Every other section is ignored.
 */
void ParseSocialListsIni(const std::string& text, std::vector<std::string>& friends,
                         std::vector<std::string>& ignored);

/**
 * Write the two sections in the oracle's exact shape -- all 100 keys each,
 * the occupied ones packed at the END (see the header comment) -- into
 * `existing`, replacing any [Friends]/[Ignored] section it has and keeping
 * every other section verbatim, so a player's copied official file keeps its
 * hotbuttons and socials.
 */
std::string MergeSocialListsIni(const std::string& existing,
                                const SocialListsModel& model);

/** `<Char>_<server>.ini`, both parts sanitised as UiPreferencesFileName does. */
std::string SocialListsFileName(const std::string& character, const std::string& server);

/** The `ui/<file>` name handed to the cache-path resolvers. */
std::string SocialListsCacheName(const std::string& character, const std::string& server);

}  // namespace state
}  // namespace eqt
