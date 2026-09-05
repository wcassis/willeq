// T378 (BOARD B145): the direct-zone-sender census, as a source-reader pin.
//
// T376 measured 70 sites under src/client / include/client that sent on the
// zone stream by dereferencing the connection directly --
// ctx_.zone_connection->QueuePacket(...) or, on EverQuest itself,
// m_zone_connection->QueuePacket(...) -- instead of going through
// eqt::PacketSender. T378 routed all of them (69 at its tip; T375 had already
// taken one on its way past). Ten were an unguarded shared_ptr dereference;
// every one of them was invisible to the R01 session recorder and the T268
// ordered-capture seam, both of which live inside PacketSender.
//
// A sweep that is not pinned is repeated by hand. This re-runs the census on
// every ctest and fails, naming the file and line, the moment a new direct
// sender appears.
//
// TWO exemptions, and only two:
//
//   1. src/client/packet_sender.cpp -- PacketSender IS the destination; its
//      own two bodies are where the real QueuePacket call has to live.
//   2. src/client/login_world/login_world_component.cpp -- the LOGIN and
//      WORLD streams, board B146. Those 12 sends run before the zone
//      connection exists, so SafeQueueZone is the wrong destination for them;
//      they need their own sender. Asserted at exactly 12 so that closing
//      B146 has to come back here and say so, and so that a new login/world
//      sender cannot slip in under the exemption either.
//
// The m_eq->QueuePacket(opcode, &packet) call sites in combat.cpp,
// spell_manager.cpp and skill_manager.cpp are NOT direct senders: that is
// EverQuest's forwarder to PacketSender::Queue, the K13 exception kept for
// non-family callers, and it feeds the seam and the recorder already.
//
// Text-level parsing, deliberately -- a compile-time check cannot see which
// pointer a call went through, which is the whole point of the sweep.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef WILLEQ_REPO_ROOT
#error "WILLEQ_REPO_ROOT must be defined (tests/cmake/action_handlers.cmake)"
#endif

namespace fs = std::filesystem;

namespace {

const fs::path kRoot = WILLEQ_REPO_ROOT;

// Relative to the repo root.
const char* kPacketSenderTu = "src/client/packet_sender.cpp";
const char* kLoginWorldTu = "src/client/login_world/login_world_component.cpp";
constexpr size_t kLoginWorldExpected = 0;   // board B146 CLOSED by T379: routed through PacketSender::QueueLogin/QueueWorld

struct Hit {
    std::string file;  // repo-relative
    int line = 0;
    std::string text;
};

std::string trimLeft(const std::string& s) {
    size_t i = s.find_first_not_of(" \t");
    return i == std::string::npos ? std::string() : s.substr(i);
}

// The same filter the T376/T378 hand census used: skip whole-line comments
// and continuation lines of a block comment.
bool isCommentLine(const std::string& line) {
    const std::string t = trimLeft(line);
    return t.rfind("//", 0) == 0 || t.rfind("*", 0) == 0 || t.rfind("/*", 0) == 0;
}

std::vector<Hit> collect() {
    std::vector<Hit> hits;
    for (const char* dir : {"src/client", "include/client"}) {
        const fs::path base = kRoot / dir;
        EXPECT_TRUE(fs::is_directory(base)) << "missing " << base.string();
        for (const auto& entry : fs::recursive_directory_iterator(base)) {
            if (!entry.is_regular_file()) continue;
            const std::string ext = entry.path().extension().string();
            if (ext != ".cpp" && ext != ".h") continue;
            std::ifstream in(entry.path());
            std::string line;
            int n = 0;
            while (std::getline(in, line)) {
                ++n;
                if (line.find("->QueuePacket(") == std::string::npos) continue;
                if (isCommentLine(line)) continue;
                if (line.find("m_eq->QueuePacket") != std::string::npos) continue;
                hits.push_back(Hit{fs::relative(entry.path(), kRoot).string(), n,
                                   trimLeft(line)});
            }
        }
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        return a.file == b.file ? a.line < b.line : a.file < b.file;
    });
    return hits;
}

// A new zone-stream sender that dereferences the connection itself: the
// regression B145 closed.
TEST(DirectSenderCensus, NoZoneStreamSenderBypassesPacketSender) {
    std::vector<Hit> offenders;
    for (const auto& h : collect()) {
        if (h.file == kPacketSenderTu || h.file == kLoginWorldTu) continue;
        offenders.push_back(h);
    }

    std::string detail;
    for (const auto& h : offenders) {
        detail += "\n  " + h.file + ":" + std::to_string(h.line) + "  " + h.text;
    }
    EXPECT_TRUE(offenders.empty())
        << offenders.size()
        << " zone-stream send(s) bypass eqt::PacketSender. Each one is "
           "invisible to the R01 session recorder and the T268 ordered-capture "
           "seam, and an unguarded one is a segfault on a dropped connection "
           "(BOARD B145 / B140). Send through ctx_.net.SafeQueueZone(...) -- "
           "or m_packet_sender.SafeQueueZone(...) on EverQuest itself -- "
           "instead:" << detail;
}

// The other direction: PacketSender must still hold the real sends, so a
// refactor that empties it cannot make the test above vacuously green.
TEST(DirectSenderCensus, PacketSenderStillHoldsTheRealSends) {
    size_t n = 0;
    for (const auto& h : collect()) {
        if (h.file == kPacketSenderTu) ++n;
    }
    EXPECT_GE(n, 2u)
        << kPacketSenderTu
        << " must still contain the real zone_connection->QueuePacket() calls "
           "(SafeQueueZone's and Queue's). Finding none means the sweep above "
           "is passing for the wrong reason.";
}

// The B146 exemption, pinned at its measured size so that closing B146 --
// or adding a new login/world sender -- has to come back to this file.
TEST(DirectSenderCensus, LoginAndWorldStreamsHaveNoDirectSendersEither) {
    std::vector<Hit> lw;
    for (const auto& h : collect()) {
        if (h.file == kLoginWorldTu) lw.push_back(h);
    }
    EXPECT_EQ(lw.size(), kLoginWorldExpected)
        << "T379 closed B146: the 12 login/world sends go through "
           "PacketSender::QueueLogin/QueueWorld, so this TU has ZERO direct "
           "senders too. A new login/world sender needs the same treatment "
           "the other 81 got (merge-step learning at the T378/T379 merge).";
}

}  // namespace
