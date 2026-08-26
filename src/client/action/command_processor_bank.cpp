// T269 -- CommandProcessor::cmdBank: /bank and its coin sub-commands
// (deposit / withdraw / convert), split out of command_processor.cpp, which
// sits at check_file_sizes.sh's 3000-line FAIL ceiling.
//
// This layer owns the PARSE and the LOCAL refusal: the sub-command, the
// amount, the denomination names, and whether the player actually holds
// that many coins in the pool being drained (read from GameState). A
// refused command dispatches nothing -- no intent, no packet. Everything
// stateful past that point lives where the state lives:
// EverQuest::RequestBankCoinMove/Convert (window-open guard, intent push)
// and the eq_bridge.cpp intent arms (OP_MoveCoin bytes + projection).
//
// Denomination scale is the WIRE's (EQT::COINTYPE_*: cp=0 sp=1 gp=2 pp=3),
// so nothing downstream has to remap it. Ratios come from eqemu's own
// CoinTypeCoppers (zone/client_process.cpp): cp 1, sp 10, gp 100, pp 1000.

#include "client/action/command_processor.h"
#include "client/state/game_state.h"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <exception>
#include <limits>
#include <string>

namespace eqt {
namespace action {

namespace {

// T269 (T264 F3): /bank's coin sub-commands. The denomination scale is the
// wire's (EQT::COINTYPE_*: cp=0 sp=1 gp=2 pp=3) so nothing has to remap it
// downstream. Returns -1 for an unknown name.
int parseCoinDenomination(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (name == "cp" || name == "copper") return 0;
    if (name == "sp" || name == "silver") return 1;
    if (name == "gp" || name == "gold") return 2;
    if (name == "pp" || name == "platinum" || name == "plat") return 3;
    return -1;
}

// A positive coin count that fits an int32 amount. Returns -1 on anything else
// (empty, non-numeric, zero, negative, overflow).
int64_t parseCoinAmount(const std::string& text) {
    if (text.empty()) return -1;
    for (char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return -1;
    }
    try {
        long long v = std::stoll(text);
        if (v <= 0 || v > std::numeric_limits<int32_t>::max()) return -1;
        return v;
    } catch (const std::exception&) {
        return -1;
    }
}

}  // namespace

ActionResult CommandProcessor::cmdBank(const std::string& args) {
    auto parts = splitArgs(args);
    if (parts.empty()) {
        return m_dispatcher.bankToggle();
    }

    static const char* kUsage =
        "Usage: /bank [deposit|withdraw <amount> <pp|gp|sp|cp>] "
        "[convert <amount> <from> <to>]";

    std::string sub = parts[0];
    std::transform(sub.begin(), sub.end(), sub.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const bool isMove = (sub == "deposit" || sub == "withdraw");
    const bool isConvert = (sub == "convert");
    if (!isMove && !isConvert) {
        return ActionResult::Failure(kUsage);
    }

    const size_t expected = isMove ? 3u : 4u;
    if (parts.size() != expected) {
        return ActionResult::Failure(kUsage);
    }

    int64_t amount = parseCoinAmount(parts[1]);
    if (amount < 0) {
        return ActionResult::Failure(kUsage);
    }

    // The coin pools this command reads are GameState's -- written by the
    // PlayerProfile load, the OP_BankerChange handler and (T269) the bank
    // arms' own projection. Refusing here means nothing is sent; the same
    // check is repeated in the arm, which owns the authoritative counters.
    const auto& player = m_state.player();
    if (!player.isAtBank()) {
        return ActionResult::Failure("You must have a bank window open (/bank) to move coins.");
    }

    auto poolFor = [&player](int coinType, bool bank) -> uint32_t {
        switch (coinType) {
        case 0: return bank ? player.bankCopper() : player.copper();
        case 1: return bank ? player.bankSilver() : player.silver();
        case 2: return bank ? player.bankGold() : player.gold();
        default: return bank ? player.bankPlatinum() : player.platinum();
        }
    };
    static const char* kCoinNames[4] = {"copper", "silver", "gold", "platinum"};
    static const int64_t kCoinCoppers[4] = {1, 10, 100, 1000};

    if (isMove) {
        int coinType = parseCoinDenomination(parts[2]);
        if (coinType < 0) {
            return ActionResult::Failure(kUsage);
        }
        const bool fromBank = (sub == "withdraw");
        if (poolFor(coinType, fromBank) < static_cast<uint32_t>(amount)) {
            return ActionResult::Failure(fmt::format("You do not have {} {} {}.",
                amount, kCoinNames[coinType], fromBank ? "in the bank" : "on hand"));
        }
        return m_dispatcher.bankCoinMove(static_cast<uint8_t>(coinType),
                                         static_cast<int32_t>(amount), fromBank);
    }

    int fromType = parseCoinDenomination(parts[2]);
    int toType = parseCoinDenomination(parts[3]);
    if (fromType < 0 || toType < 0) {
        return ActionResult::Failure(kUsage);
    }
    if (fromType == toType) {
        return ActionResult::Failure("Pick two different denominations to convert between.");
    }
    if (poolFor(fromType, /*bank=*/true) < static_cast<uint32_t>(amount)) {
        return ActionResult::Failure(fmt::format("You do not have {} {} in the bank.",
            amount, kCoinNames[fromType]));
    }
    // Converting UP (cp -> sp) needs a whole multiple of the ratio, or the
    // server would take only part of it (eqemu OPMoveCoin truncates both the
    // amount added and the amount taken).
    if (kCoinCoppers[toType] > kCoinCoppers[fromType]) {
        const int64_t step = kCoinCoppers[toType] / kCoinCoppers[fromType];
        if (amount % step != 0) {
            return ActionResult::Failure(fmt::format(
                "Converting {} to {} takes a multiple of {}.",
                kCoinNames[fromType], kCoinNames[toType], step));
        }
    }
    return m_dispatcher.bankCoinConvert(static_cast<uint8_t>(fromType),
                                        static_cast<uint8_t>(toType),
                                        static_cast<int32_t>(amount));
}

}  // namespace action
}  // namespace eqt
