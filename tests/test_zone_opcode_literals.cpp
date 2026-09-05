// T374 Leg B (BOARD B137): no bare hex literal in the zone opcode table.
//
// G01 turned DispatchZonePacket's 145-arm switch into two data tables keyed by
// opcode. The keys are meant to be HC_OP_* enumerators, so that
// include/client/eq_opcodes.h stays the one place a Titanium opcode number is
// written, eqt::strings::GetOpcodeName can name the opcode in a log line, and
// a reviewer reading the table sees protocol names rather than magic numbers.
// Two entries had escaped that: OnTargetCommandResponse was keyed by the raw
// 0x61f9 (no enumerator existed at all) and the Consider response by 0x65ca
// (HC_OP_Consider existed and simply was not used).
//
// This is a SOURCE READER, deliberately: the values are unchanged, so nothing
// observable at runtime distinguishes `0x61f9` from `HC_OP_TargetCommandReply`
// -- the G01 golden (tests/goldens/zone_opcodes_g01.txt) pins the opcode SET
// and stays byte-identical across this change, and test_zone_dispatch_table
// passes unedited. The only way to pin the spelling is to read the text.
//
// Companion assertion: every key the table uses resolves to a name through
// eqt::strings::GetOpcodeName, so a new enumerator cannot be added to
// eq_opcodes.h and forgotten in the name table.

#include <gtest/gtest.h>

#include "client/eq_opcodes.h"
#include "client/eq_strings.h"
#include "client/zone/zone_component.h"

#include <fstream>
#include <regex>
#include <string>
#include <vector>

namespace {

#ifndef WILLEQ_REPO_ROOT
#define WILLEQ_REPO_ROOT "."
#endif

std::string DispatchSourcePath() {
    return std::string(WILLEQ_REPO_ROOT) + "/src/client/zone/zone_dispatch.cpp";
}

// Every `{<key>, ...}` initialiser line inside the two static tables, paired
// with its 1-based line number. The tables are the only brace-initialiser
// lists in the file whose first token is an opcode, and both are bounded by
// their `static const std::unordered_map` line and the closing `};`.
std::vector<std::pair<int, std::string>> TableKeys() {
    std::vector<std::pair<int, std::string>> keys;
    std::ifstream in(DispatchSourcePath());
    EXPECT_TRUE(in.is_open()) << "cannot read " << DispatchSourcePath();

    const std::regex table_open(R"(static const std::unordered_map<uint16_t,)");
    const std::regex entry(R"(^\s*\{\s*([A-Za-z0-9_]+)\s*,)");

    bool in_table = false;
    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        if (!in_table) {
            if (std::regex_search(line, table_open)) in_table = true;
            continue;
        }
        if (line.find("};") != std::string::npos) {
            in_table = false;
            continue;
        }
        std::smatch m;
        if (std::regex_search(line, m, entry)) {
            keys.emplace_back(lineno, m[1].str());
        }
    }
    return keys;
}

TEST(ZoneOpcodeLiterals, EveryTableKeyIsAnHcOpEnumeratorNotAHexLiteral) {
    const auto keys = TableKeys();
    // Sanity: the reader actually found the tables. 142 handled + 3 ignored at
    // G01; the exact number is free to grow, zero means the regex broke.
    ASSERT_GT(keys.size(), 100u) << "the table reader matched nothing -- did the "
                                    "table shape in zone_dispatch.cpp change?";
    for (const auto& [lineno, key] : keys) {
        EXPECT_EQ(key.rfind("HC_OP_", 0), 0u)
            << DispatchSourcePath() << ":" << lineno
            << ": zone opcode table key '" << key
            << "' is not an HC_OP_* enumerator. Add it to "
               "include/client/eq_opcodes.h (and to GetOpcodeName in "
               "src/client/core/eq_strings.cpp) rather than writing the number "
               "here.";
    }
}

TEST(ZoneOpcodeLiterals, TargetCommandReplyHasAnEnumeratorAtTheMeasuredValue) {
    // The value G01's golden pins for OnTargetCommandResponse. The enumerator
    // is new; the number is not, and must not move.
    EXPECT_EQ(static_cast<uint16_t>(HC_OP_TargetCommandReply), 0x61f9);
}

TEST(ZoneOpcodeLiterals, EveryDispatchedOpcodeResolvesToAName) {
    for (const auto& [opcode, handler] : eqt::zone::ZoneComponent::ZoneOpcodeTable()) {
        (void)handler;
        const std::string name = eqt::strings::GetOpcodeName(opcode);
        EXPECT_EQ(name.rfind("HC_OP_", 0), 0u)
            << "dispatched opcode 0x" << std::hex << opcode
            << " has no entry in GetOpcodeName (got '" << name << "')";
    }
    for (const auto& [opcode, reason] : eqt::zone::ZoneComponent::IgnoredZoneOpcodes()) {
        (void)reason;
        const std::string name = eqt::strings::GetOpcodeName(opcode);
        EXPECT_EQ(name.rfind("HC_OP_", 0), 0u)
            << "ignored opcode 0x" << std::hex << opcode
            << " has no entry in GetOpcodeName (got '" << name << "')";
    }
}

}  // namespace
