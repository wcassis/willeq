// B1054 (#374): unit coverage for
// docker/irrlicht-drm/drm_card_select.h's eqt_drm::selectDRMCard(), the
// pure card-selection decision extracted from CIrrDeviceFB::initDRM() so
// the measured defect (a card that OPENS but has no connected connector
// ending the search, hiding the iGPU panel on the next card -- measured
// on the ThinkPad, card0 = NVIDIA Quadro headless, card1 = Intel UHD 630
// eDP-1) can be verified WITHOUT a DRM device / real hardware: the same
// shape as tests/test_drm_mode_select.cpp (P49), faking the card list via
// the open/check callbacks instead of libdrm.
//
// Linux-only: the header's enumeration half uses <dirent.h> (the decision
// half is platform-neutral, but the header guards the enumeration under
// __has_include(<dirent.h>) and the test exercises it against a temp dir,
// so it is registered on Linux only, like the other DRM tests).
#include <gtest/gtest.h>

#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#include "docker/irrlicht-drm/drm_card_select.h"

using eqt_drm::DRMCardSelection;
using eqt_drm::DRMCardState;
using eqt_drm::cardPathLess;
using eqt_drm::drmCardCandidates;
using eqt_drm::selectDRMCard;

namespace {

// Fake card table: the caller decides per path whether open() succeeds and
// what the check reports. Successful opens return REAL fds (a /dev/null
// open) because selectDRMCard closes any fd it rejects -- a fake fd number
// would close whatever real descriptor happened to hold that number.
struct FakeCards {
    bool openable[8] = {false, false, false, false, false, false, false, false};
    DRMCardState state[8];
    int attempts = 0;  // every open() call, success or not
    int opens = 0;     // successful opens
    int checks = 0;

    int open(const std::string& path) {
        // Map basename "cardN" to a real fd when openable; unknown paths
        // never open. The basename is used because callers pass full paths
        // ("/dev/dri/cardN").
        const std::string base = path.substr(path.find_last_of('/') + 1);
        if (base.size() > 4 && base.compare(0, 4, "card") == 0) {
            int n = std::atoi(base.c_str() + 4);
            if (n >= 0 && n < 8) {
                attempts++;
                if (openable[n]) {
                    opens++;
                    return ::open("/dev/null", O_RDONLY | O_CLOEXEC);
                }
            }
        }
        return -1;
    }

    DRMCardState check(int fd, const std::string& path) {
        const std::string base = path.substr(path.find_last_of('/') + 1);
        int n = base.size() > 4 && base.compare(0, 4, "card") == 0
                    ? std::atoi(base.c_str() + 4)
                    : -1;
        if (n >= 0 && n < 8) {
            checks++;
            return state[n];
        }
        (void)fd;
        DRMCardState none;
        return none;
    }
};

}  // namespace

// The B1054 measured case: card0 opens but has no connected connector;
// card1 opens with a connected connector + CRTC. The selection MUST skip
// card0 and land on card1 (pre-B1054 the first OPENED card won).
TEST(DRMCardSelect, OpenableCardWithoutConnectorIsSkipped) {
    FakeCards cards;
    cards.openable[0] = true;
    cards.state[0] = DRMCardState{false, false, 0};  // opens, no connector
    cards.openable[1] = true;
    cards.state[1] = DRMCardState{true, true, 9};  // eDP
    DRMCardSelection sel = selectDRMCard(
        {"/dev/dri/card0", "/dev/dri/card1"},
        [&](const std::string& p) { return cards.open(p); },
        [&](int fd, const std::string& p) { return cards.check(fd, p); });
    ASSERT_TRUE(sel.ok);
    EXPECT_EQ(sel.index, 1);
    EXPECT_EQ(sel.reason, "");
    EXPECT_EQ(cards.attempts, 2);
    EXPECT_EQ(cards.opens, 2);
    EXPECT_EQ(cards.checks, 2);
}

// Both cards open, the second one has no connector and the first is the
// connected one: the first usable card still wins (selection is ordered).
TEST(DRMCardSelect, FirstUsableCardWins) {
    FakeCards cards;
    cards.openable[0] = true;
    cards.state[0] = DRMCardState{true, true, 9};
    cards.openable[1] = true;
    cards.state[1] = DRMCardState{false, false, 0};
    DRMCardSelection sel = selectDRMCard(
        {"/dev/dri/card0", "/dev/dri/card1"},
        [&](const std::string& p) { return cards.open(p); },
        [&](int fd, const std::string& p) { return cards.check(fd, p); });
    ASSERT_TRUE(sel.ok);
    EXPECT_EQ(sel.index, 0);
    EXPECT_EQ(cards.checks, 1);  // the search stops at the winner
}

// The other half of the skip rule: a card that opens and has a connected
// connector but NO CRTC is skipped too.
TEST(DRMCardSelect, ConnectedButNoCrtcIsSkipped) {
    FakeCards cards;
    cards.openable[0] = true;
    cards.state[0] = DRMCardState{true, false, 9};
    cards.openable[1] = true;
    cards.state[1] = DRMCardState{true, true, 9};
    DRMCardSelection sel = selectDRMCard(
        {"/dev/dri/card0", "/dev/dri/card1"},
        [&](const std::string& p) { return cards.open(p); },
        [&](int fd, const std::string& p) { return cards.check(fd, p); });
    ASSERT_TRUE(sel.ok);
    EXPECT_EQ(sel.index, 1);
}

// A card that fails to open is skipped without a check.
TEST(DRMCardSelect, UnopenableCardIsSkipped) {
    FakeCards cards;
    cards.openable[0] = false;
    cards.openable[1] = true;
    cards.state[1] = DRMCardState{true, true, 9};
    DRMCardSelection sel = selectDRMCard(
        {"/dev/dri/card0", "/dev/dri/card1"},
        [&](const std::string& p) { return cards.open(p); },
        [&](int fd, const std::string& p) { return cards.check(fd, p); });
    ASSERT_TRUE(sel.ok);
    EXPECT_EQ(sel.index, 1);
    EXPECT_EQ(cards.attempts, 2);
    EXPECT_EQ(cards.opens, 1);
    EXPECT_EQ(cards.checks, 1);
}

// Every card opens, none is usable: failure with the "usable display"
// reason (this is the ThinkPad-shape where card1 would be missing).
TEST(DRMCardSelect, AllOpenNoneUsableReportsNoUsableDisplay) {
    FakeCards cards;
    cards.openable[0] = true;
    cards.state[0] = DRMCardState{false, false, 0};
    cards.openable[1] = true;
    cards.state[1] = DRMCardState{false, false, 0};
    DRMCardSelection sel = selectDRMCard(
        {"/dev/dri/card0", "/dev/dri/card1"},
        [&](const std::string& p) { return cards.open(p); },
        [&](int fd, const std::string& p) { return cards.check(fd, p); });
    ASSERT_FALSE(sel.ok);
    EXPECT_EQ(sel.index, -1);
    EXPECT_EQ(sel.reason, "no card with a usable display");
}

// No card opens at all: a distinct reason.
TEST(DRMCardSelect, NothingOpensReportsNoCardOpens) {
    FakeCards cards;
    DRMCardSelection sel = selectDRMCard(
        {"/dev/dri/card0"},
        [&](const std::string& p) { return cards.open(p); },
        [&](int fd, const std::string& p) { return cards.check(fd, p); });
    ASSERT_FALSE(sel.ok);
    EXPECT_EQ(sel.index, -1);
    EXPECT_EQ(sel.reason, "no card opens");
}

// Empty candidate list (no /dev/dri at all): a distinct reason.
TEST(DRMCardSelect, EmptyCandidateListReportsNoCandidates) {
    FakeCards cards;
    DRMCardSelection sel = selectDRMCard(
        {},
        [&](const std::string& p) { return cards.open(p); },
        [&](int fd, const std::string& p) { return cards.check(fd, p); });
    ASSERT_FALSE(sel.ok);
    EXPECT_EQ(sel.index, -1);
    EXPECT_EQ(sel.reason, "no candidates");
}

// WILLEQ_DRM_DEVICE override to a path that does not open: the failure
// carries the override-specific reason ("card does not open") and,
// crucially, the candidate list is exactly the one path: no widening to
// auto-selection.
TEST(DRMCardSelect, OverrideToMissingPathFailsExplicitly) {
    FakeCards cards;
    unsetenv("WILLEQ_DRM_DEVICE");
    setenv("WILLEQ_DRM_DEVICE", "/dev/dri/card5", 1);  // openable[5] = false: the path does not open
    std::vector<std::string> candidates = drmCardCandidates("/nonexistent");
    ASSERT_EQ(candidates.size(), 1u);
    EXPECT_EQ(candidates[0], "/dev/dri/card5");
    DRMCardSelection sel = selectDRMCard(
        candidates,
        [&](const std::string& p) { return cards.open(p); },
        [&](int fd, const std::string& p) { return cards.check(fd, p); },
        /*override=*/true);
    ASSERT_FALSE(sel.ok);
    EXPECT_EQ(sel.index, -1);
    EXPECT_EQ(sel.reason, "card does not open");
    EXPECT_EQ(cards.attempts, 1);  // the override path, tried once, alone
    EXPECT_EQ(cards.opens, 0);     // ...and refused
    unsetenv("WILLEQ_DRM_DEVICE");
}

// Override to a path that opens but has no display: the other override
// failure reason.
TEST(DRMCardSelect, OverrideToNoDisplayCardFailsExplicitly) {
    FakeCards cards;
    unsetenv("WILLEQ_DRM_DEVICE");
    setenv("WILLEQ_DRM_DEVICE", "/dev/dri/card4", 1);
    // "card4" maps to a real /dev/null fd via openable[4].
    cards.openable[4] = true;
    cards.state[4] = DRMCardState{false, false, 0};
    std::vector<std::string> candidates = drmCardCandidates("/nonexistent");
    ASSERT_EQ(candidates.size(), 1u);
    EXPECT_EQ(candidates[0], "/dev/dri/card4");
    DRMCardSelection sel = selectDRMCard(
        candidates,
        [&](const std::string& p) { return cards.open(p); },
        [&](int fd, const std::string& p) { return cards.check(fd, p); },
        /*override=*/true);
    ASSERT_FALSE(sel.ok);
    EXPECT_EQ(sel.reason, "card has no usable display");
    unsetenv("WILLEQ_DRM_DEVICE");
}

// Override to a good path: exactly that path wins.
TEST(DRMCardSelect, OverrideToGoodPathPicksIt) {
    FakeCards cards;
    cards.openable[3] = true;
    cards.state[3] = DRMCardState{true, true, 9};
    unsetenv("WILLEQ_DRM_DEVICE");
    setenv("WILLEQ_DRM_DEVICE", "/dev/dri/card3", 1);
    std::vector<std::string> candidates = drmCardCandidates("/nonexistent");
    ASSERT_EQ(candidates.size(), 1u);
    EXPECT_EQ(candidates[0], "/dev/dri/card3");
    DRMCardSelection sel = selectDRMCard(
        candidates,
        [&](const std::string& p) { return cards.open(p); },
        [&](int fd, const std::string& p) { return cards.check(fd, p); });
    ASSERT_TRUE(sel.ok);
    EXPECT_EQ(sel.index, 0);
    unsetenv("WILLEQ_DRM_DEVICE");
}

// With no override the candidates come from the directory, in NUMERIC
// card order (card2 before card10, which a plain string compare reverses).
TEST(DRMCardSelect, EnumeratesCardsInNumericOrder) {
    char tmpl[] = "/tmp/b1054_cards_XXXXXX";
    ASSERT_NE(mkdtemp(tmpl), nullptr);
    const std::string root(tmpl);
    ASSERT_EQ(::mkdir((root + "/card0").c_str(), 0755), 0);
    ASSERT_EQ(::mkdir((root + "/card1").c_str(), 0755), 0);
    ASSERT_EQ(::mkdir((root + "/card2").c_str(), 0755), 0);
    ASSERT_EQ(::mkdir((root + "/card10").c_str(), 0755), 0);
    // Non-card entries are dropped (a regular file, not just dirs).
    ASSERT_EQ(::mkdir((root + "/renderD128").c_str(), 0755), 0);
    {
        int f = ::open((root + "/by-path").c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        ASSERT_GE(f, 0);
        ::close(f);
    }

    unsetenv("WILLEQ_DRM_DEVICE");
    std::vector<std::string> candidates = drmCardCandidates(root);
    ASSERT_EQ(candidates.size(), 4u);
    EXPECT_EQ(candidates[0], root + "/card0");
    EXPECT_EQ(candidates[1], root + "/card1");
    EXPECT_EQ(candidates[2], root + "/card2");
    EXPECT_EQ(candidates[3], root + "/card10");

    for (int i = 0; i < 4; i++)
        ::rmdir((root + "/card" + std::to_string(i == 3 ? 10 : i)).c_str());
    ::rmdir((root + "/renderD128").c_str());
    ::unlink((root + "/by-path").c_str());
    ::rmdir(root.c_str());
}

// cardPathLess on its own: "card2" < "card10" numerically.
TEST(DRMCardSelect, CardPathLessIsNumeric) {
    EXPECT_TRUE(cardPathLess("card2", "card10"));
    EXPECT_TRUE(cardPathLess("card0", "card1"));
    EXPECT_FALSE(cardPathLess("card10", "card2"));
}
