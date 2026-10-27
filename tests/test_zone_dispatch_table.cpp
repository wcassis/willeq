// G01 (refactor/G01_dispatch_zone_packet_table.md) -- the opcode-coverage net
// for ZoneComponent::DispatchZonePacket.
//
// Before G01 the dispatcher was a 145-arm `switch`, and "an opcode silently
// stopped being dispatched" was a compile-clean, test-clean regression: you
// delete an arm and nothing anywhere notices. G01 turned the switch into a
// table, which makes the arm set DATA -- and this file pins that data against
// a golden list extracted mechanically from the pre-G01 source
// (tests/goldens/zone_opcodes_g01.txt, generated from
// `git show fable:src/client/zone/zone_dispatch.cpp`).
//
// The invariant is TWO-DIRECTIONAL:
//   * every former case label is a key of ZoneOpcodeTable() or of
//     IgnoredZoneOpcodes() -- nothing stopped being dispatched;
//   * every key of those two maps was a former case label -- nothing was
//     invented by the refactor;
// and the two maps are disjoint, so "handled" and "deliberately ignored" can
// never both be claimed for one opcode.
//
// This is a coverage net, NOT a behaviour test: the six packet-driving suites
// (test_zone_component, test_entities_component, test_chat_component,
// test_guild_component, test_combat_manager, test_eq_commands_equip) still own
// what each handler computes. Adding a genuinely new opcode handler is meant
// to require a line in the golden list -- that edit is the review moment.

#include <gtest/gtest.h>

#include "client/zone/zone_component.h"

#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>

namespace {

#ifndef WILLEQ_REPO_ROOT
#define WILLEQ_REPO_ROOT "."
#endif

std::string GoldenPath() {
    return std::string(WILLEQ_REPO_ROOT) + "/tests/goldens/zone_opcodes_g01.txt";
}

// opcode -> the label the pre-G01 switch spelled it with (for readable
// failure messages).
std::map<uint16_t, std::string> LoadGolden() {
    std::map<uint16_t, std::string> golden;
    std::ifstream in(GoldenPath());
    EXPECT_TRUE(in.is_open()) << "golden list missing: " << GoldenPath();
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string hex, label;
        ls >> hex >> label;
        if (hex.empty()) continue;
        golden[static_cast<uint16_t>(std::stoul(hex, nullptr, 16))] = label;
    }
    return golden;
}

std::string Hex(uint16_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%04x", v);
    return buf;
}

std::string Join(const std::set<std::string>& s) {
    std::string out;
    for (const auto& x : s) {
        if (!out.empty()) out += ", ";
        out += x;
    }
    return out;
}

using eqt::zone::ZoneComponent;

// The golden itself has to be non-trivial, or every assertion below passes
// vacuously against an empty or unreadable file (the readFile trap, K11 ee).
TEST(ZoneDispatchTable, GoldenListIsTheMeasured145Arms) {
    const auto golden = LoadGolden();
    // B1170 (#489) added the one genuinely new handler since G01, 0x4033
    // (the missile object), as the header comment asks: a golden line.
    EXPECT_EQ(golden.size(), 146u)
        << "the pre-G01 switch had 145 case labels, plus B1170's 0x4033; the golden must carry all of them";
}

// Direction 1: nothing stopped being dispatched.
TEST(ZoneDispatchTable, EveryFormerCaseLabelIsHandledOrDeliberatelyIgnored) {
    const auto golden = LoadGolden();
    ASSERT_FALSE(golden.empty());
    const auto& table = ZoneComponent::ZoneOpcodeTable();
    const auto& ignored = ZoneComponent::IgnoredZoneOpcodes();

    std::set<std::string> missing;
    for (const auto& [opcode, label] : golden) {
        if (table.count(opcode) == 0 && ignored.count(opcode) == 0) {
            missing.insert(label + " (" + Hex(opcode) + ")");
        }
    }
    EXPECT_TRUE(missing.empty())
        << "opcodes the switch handled that the table no longer dispatches: "
        << Join(missing);
}

// Direction 2: nothing was invented.
TEST(ZoneDispatchTable, EveryTableEntryWasAFormerCaseLabel) {
    const auto golden = LoadGolden();
    ASSERT_FALSE(golden.empty());

    std::set<std::string> extra;
    for (const auto& [opcode, handler] : ZoneComponent::ZoneOpcodeTable()) {
        (void)handler;
        if (golden.count(opcode) == 0) extra.insert(Hex(opcode));
    }
    for (const auto& [opcode, reason] : ZoneComponent::IgnoredZoneOpcodes()) {
        (void)reason;
        if (golden.count(opcode) == 0) extra.insert(Hex(opcode));
    }
    EXPECT_TRUE(extra.empty())
        << "dispatched/ignored opcodes with no former case label (add them to "
        << "tests/goldens/zone_opcodes_g01.txt deliberately, not by accident): "
        << Join(extra);
}

// An opcode is handled or ignored, never both -- the two maps partition the
// golden exactly.
TEST(ZoneDispatchTable, HandledAndIgnoredAreDisjointAndPartitionTheGolden) {
    const auto& table = ZoneComponent::ZoneOpcodeTable();
    const auto& ignored = ZoneComponent::IgnoredZoneOpcodes();

    std::set<std::string> both;
    for (const auto& [opcode, reason] : ignored) {
        (void)reason;
        if (table.count(opcode) != 0) both.insert(Hex(opcode));
    }
    EXPECT_TRUE(both.empty()) << "opcode both dispatched and ignored: " << Join(both);

    EXPECT_EQ(table.size() + ignored.size(), LoadGolden().size());
}

// The ignored set is the deliberate-no-op set, and each entry must say WHY:
// its reason is the comment its former switch arm carried. An empty reason is
// an opcode that was dropped rather than a decision that was recorded.
TEST(ZoneDispatchTable, EveryIgnoredOpcodeCarriesItsReason) {
    const auto& ignored = ZoneComponent::IgnoredZoneOpcodes();
    // The pre-G01 switch had exactly three comment-only arms. T469 (board B604)
    // moved one of them, HC_OP_InterruptCast ("spell manager handles this via
    // other packets" -- no other packet did), into ZoneOpcodeTable(): a
    // reviewed edit, learned in place (G-correction 32). The golden list of
    // opcodes is unchanged; only the handled/ignored split moved.
    EXPECT_EQ(ignored.size(), 2u) << "SetGroupTarget and LinkedReuse are the two comment-only arms left";
    for (const auto& [opcode, reason] : ignored) {
        EXPECT_NE(reason, nullptr) << Hex(opcode);
        EXPECT_GT(std::string(reason == nullptr ? "" : reason).size(), 10u)
            << "no reason recorded for ignored opcode " << Hex(opcode);
    }
}

// Every table entry must be a callable -- a null function pointer would look
// like coverage and crash on the first packet.
TEST(ZoneDispatchTable, EveryTableEntryHasAHandler) {
    for (const auto& [opcode, handler] : ZoneComponent::ZoneOpcodeTable()) {
        EXPECT_NE(handler, nullptr) << Hex(opcode);
    }
}

}  // namespace
