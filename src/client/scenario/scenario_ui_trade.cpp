// UIGAP-live2: the live legs owed by B896 (#212 trade / give), B442b (#151
// drop / destroy / coin) and UIGAP-small (#26 bank Change). Every assertion
// cites the official-client measurement it holds fable to
// (review/UIGAP-ORACLE/<issue>/).
//
//   trade_roundtrip    the actor (Testchar). Dagger on the cursor + a world
//                      click on the peer -> the trade opens and the dagger
//                      drops into our first trade slot (212 trade.tsv rows
//                      1-3); /coin pp 5 + the coin row -> one OP_MoveCoin
//                      0 -> 3 (row 14); Trade; the peer's Trade completes it
//                      and NOTHING is printed (row 7). Then a second trade
//                      (the bottle) that we Cancel: eqstr 1450 "You have
//                      cancelled the trade." and the bottle comes back (rows
//                      8, 15). The DB side is trade_roundtrip.post.sh.
//   trade_peer         its partner (Testbuddy): auto-acks, waits for the
//                      offer and the actor's Trade, presses Trade, then sees
//                      the second trade cancelled with the same 1450 line.
//   give_npc           a Gnoll Fang on the cursor + a world click on Captain
//                      Tillin -> the give window (row 9), Give -> his quest
//                      takes the fang and summons a Moonstone.
//   drop_destroy_coin  the drop and destroy confirmations (151 rows 31-35:
//                      ConfirmationRequested, then Yes -> OP_GroundSpawn /
//                      OP_MoveItem 30 -> -1), coin purse -> cursor -> purse
//                      (rows 43-44), bank coin -> purse and purse -> bank by
//                      OP_MoveCoin slot 2 at an open bank.
//   bank_change        /bank change (#26): both purses to the fewest coins,
//                      asserted against the arithmetic here AND the DB
//                      (bank_change.post.sh).

#include "client/scenario/scenarios.h"
#include "client/state/event_types.h"
#include "scenario_steps.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace eqt {
namespace scenario {
namespace {

using steps::ChatHas;
using steps::Step;
using steps::StepScenario;

constexpr int kCursor = 33;
constexpr int kTrade1 = 3000;
constexpr long long kDagger = 7007;
constexpr long long kBottle = 16598;
constexpr long long kFang = 13915;      // Gnoll Fang -- Captain Tillin's bounty item
constexpr long long kMoonstone = 10070; // his reward
constexpr int kConfirmationRequested =
    static_cast<int>(eqt::state::GameEventType::ConfirmationRequested);
// CursorGestureOp (renderer_intents.h), as ints for IScenarioWorld.
constexpr int kOpCoinClick = 3;
constexpr int kOpRequestDrop = 5;
constexpr int kOpConfirmDrop = 6;
constexpr int kOpRequestDestroy = 7;
constexpr int kOpConfirmDestroy = 8;
constexpr int kPP = 3;
constexpr int kPurse = 1;
constexpr int kBank = 2;
// TradeButton ops.
constexpr int kTradeAccept = 0;
constexpr int kTradeCancel = 1;
constexpr int kTradeCoinRow = 2;
const char* const kCancelledLine = "You have cancelled the trade.";  // eqstr 1450

bool Has(IScenarioWorld& w, int bit) {
    const int f = w.TradeFlags();
    return f >= 0 && (f & bit) != 0;
}
bool Trading(IScenarioWorld& w) { return Has(w, IScenarioWorld::kTradeActive); }

long long Pp(IScenarioWorld& w, bool bank) {
    const auto c = w.Coins(bank);
    return c.size() == 4 ? c[0] : -1;
}

std::string CoinStr(IScenarioWorld& w, bool bank) {
    const auto c = w.Coins(bank);
    if (c.size() != 4) return "?";
    std::ostringstream os;
    os << c[0] << "pp " << c[1] << "gp " << c[2] << "sp " << c[3] << "cp";
    return os.str();
}

std::string Describe(IScenarioWorld& w) {
    std::ostringstream os;
    os << "trade=" << w.TradeFlags() << " dagger@" << w.FindItemSlot(kDagger) << " bottle@"
       << w.FindItemSlot(kBottle) << " fang@" << w.FindItemSlot(kFang) << " moonstone@"
       << w.FindItemSlot(kMoonstone) << " cursor=" << w.ClientItemAt(kCursor) << " cursorCoin="
       << w.CursorCoinCopper() << " purse=" << CoinStr(w, false) << " bank=" << CoinStr(w, true)
       << " objects=" << w.WorldObjectCount();
    return os.str();
}

/** The first chat line from `first` on that contains `needle`, ignoring case
 *  and the typed-command echo; empty when there is none. */
std::string ChatLineWith(IScenarioWorld& w, size_t first, std::string needle) {
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    };
    needle = lower(needle);
    for (size_t i = first; i < w.ChatLineCount(); ++i) {
        const std::string line = w.ChatLine(i);
        const size_t bar = line.find('|', line.find('|') + 1);
        const std::string text = bar == std::string::npos ? line : line.substr(bar + 1);
        if (!text.empty() && text[0] == '/') continue;
        if (lower(text).find(needle) != std::string::npos) return line;
    }
    return std::string();
}

class Leg final : public StepScenario {
public:
    Leg(const char* name, std::vector<steps::Step> s) : StepScenario(name, 20.0, std::move(s)) {}

protected:
    std::string Summary(IScenarioWorld& w) override { return "MODEL " + Describe(w); }
};

// ---------------------------------------------------------------------------
// trade_roundtrip / trade_peer
// ---------------------------------------------------------------------------

// Fixture (trade_roundtrip_seed.sql): Testchar with the dagger in 24, the
// bottle in 25 and 10 pp; Testbuddy beside it with an empty backpack, 0 pp.
std::unique_ptr<IScenario> MakeTradeRoundtrip(const ScenarioOptions& o) {
    const std::string peer = o.castTargetName;
    auto base = std::make_shared<size_t>(0);
    std::vector<Step> s;
    if (peer.empty()) {
        s.push_back({"needs --scenario-cast-target <peer>", nullptr, nullptr,
                     [](IScenarioWorld&) { return std::string("no peer name (--scenario-cast-target)"); },
                     Describe, 1.0, 0.0});
    }
    s.push_back({"fixture: dagger 24, bottle 25, 10 pp, no trade; the peer is here", nullptr,
                 [](IScenarioWorld& w) {
                     return w.FindItemSlot(kDagger) == 24 && w.FindItemSlot(kBottle) == 25 && Pp(w, false) == 10 &&
                            !Trading(w) && w.OtherPlayerCount() >= 1;
                 },
                 nullptr, Describe, 150.0, 2.0});
    s.push_back({"/pickup 24 -> the dagger on the cursor", [](IScenarioWorld& w) { w.SendCommand("/pickup 24"); },
                 [](IScenarioWorld& w) { return w.ClientItemAt(kCursor) == kDagger; }, nullptr, Describe, 10.0, 1.0});
    s.push_back({"CLICK the peer with the dagger held -> the trade opens, the dagger drops into 3000",
                 [peer](IScenarioWorld& w) {
                     if (!w.ClickEntity(peer)) w.Note("ClickEntity(" + peer + ") found no such entity");
                 },
                 [](IScenarioWorld& w) {
                     return Trading(w) && w.ClientItemAt(kTrade1) == kDagger && w.CursorQueueSize() == 0;
                 },
                 nullptr, Describe, 15.0, 1.0});
    s.push_back({"/coin pp 5 -> 5 pp on the cursor", [](IScenarioWorld& w) { w.SendCommand("/coin pp 5"); },
                 [](IScenarioWorld& w) { return w.CursorCoinCopper() == 5000 && Pp(w, false) == 5; }, nullptr,
                 Describe, 10.0, 1.0});
    s.push_back({"the coin row -> the 5 pp go into the trade (OP_MoveCoin 0 -> 3)",
                 [](IScenarioWorld& w) { w.TradeButton(kTradeCoinRow); },
                 [](IScenarioWorld& w) { return w.CursorCoinCopper() == 0 && Trading(w); }, nullptr, Describe, 10.0,
                 2.0});
    s.push_back({"Trade; the peer's Trade completes it -> the dagger is gone, the window shut",
                 [base](IScenarioWorld& w) {
                     *base = w.ChatLineCount();
                     w.TradeButton(kTradeAccept);
                 },
                 [](IScenarioWorld& w) { return !Trading(w) && w.FindItemSlot(kDagger) == 0; },
                 [base](IScenarioWorld& w) {
                     return ChatHas(w, *base, kCancelledLine) ? std::string("the trade was CANCELLED, not completed")
                                                              : std::string();
                 },
                 Describe, 60.0, 0.0});
    s.push_back({"completion prints NOTHING (212 row 7), for 5 s", nullptr, nullptr,
                 [base](IScenarioWorld& w) {
                     const std::string line = ChatLineWith(w, *base, "trade");
                     return line.empty() ? std::string() : "a completion line was printed: '" + line + "'";
                 },
                 Describe, 1.0, 5.0});
    s.push_back({"/pickup 25 -> the bottle on the cursor", [](IScenarioWorld& w) { w.SendCommand("/pickup 25"); },
                 [](IScenarioWorld& w) { return w.ClientItemAt(kCursor) == kBottle; }, nullptr, Describe, 10.0, 1.0});
    s.push_back({"CLICK the peer again -> a second trade, the bottle in 3000",
                 [peer](IScenarioWorld& w) { w.ClickEntity(peer); },
                 [](IScenarioWorld& w) { return Trading(w) && w.ClientItemAt(kTrade1) == kBottle; }, nullptr,
                 Describe, 15.0, 4.0});
    s.push_back({"Cancel -> eqstr 1450 here, the window shut, the bottle back with us",
                 [base](IScenarioWorld& w) {
                     *base = w.ChatLineCount();
                     w.TradeButton(kTradeCancel);
                 },
                 [base](IScenarioWorld& w) {
                     const long long at = w.FindItemSlot(kBottle);
                     return !Trading(w) && ChatHas(w, *base, kCancelledLine) && at > 0 && at < kTrade1;
                 },
                 nullptr, Describe, 15.0, 4.0});
    return std::make_unique<Leg>("trade_roundtrip", std::move(s));
}

std::unique_ptr<IScenario> MakeTradePeer(const ScenarioOptions&) {
    auto base = std::make_shared<size_t>(0);
    std::vector<Step> s;
    s.push_back({"the actor's request is auto-acked -> the trade window is open", nullptr,
                 [](IScenarioWorld& w) { return Trading(w); }, nullptr, Describe, 240.0, 0.0});
    s.push_back({"the actor offered (dagger + coin) and pressed Trade", nullptr,
                 [](IScenarioWorld& w) {
                     return Has(w, IScenarioWorld::kTradePartnerOffered) &&
                            Has(w, IScenarioWorld::kTradePartnerAccepted);
                 },
                 nullptr, Describe, 60.0, 1.0});
    s.push_back({"Trade -> the trade completes, the dagger is ours",
                 [base](IScenarioWorld& w) {
                     *base = w.ChatLineCount();
                     w.TradeButton(kTradeAccept);
                 },
                 [](IScenarioWorld& w) {
                     const long long at = w.FindItemSlot(kDagger);
                     return !Trading(w) && at > 0 && at < kTrade1;
                 },
                 [base](IScenarioWorld& w) {
                     return ChatHas(w, *base, kCancelledLine) ? std::string("the trade was CANCELLED") : std::string();
                 },
                 Describe, 30.0, 0.0});
    s.push_back({"completion prints NOTHING here either, for 5 s", nullptr, nullptr,
                 [base](IScenarioWorld& w) {
                     const std::string line = ChatLineWith(w, *base, "trade");
                     return line.empty() ? std::string() : "a completion line was printed: '" + line + "'";
                 },
                 Describe, 1.0, 5.0});
    s.push_back({"the second trade opens", [base](IScenarioWorld& w) { *base = w.ChatLineCount(); },
                 [](IScenarioWorld& w) { return Trading(w); }, nullptr, Describe, 90.0, 0.0});
    s.push_back({"the actor cancels -> eqstr 1450 here too, the window shut", nullptr,
                 [base](IScenarioWorld& w) { return !Trading(w) && ChatHas(w, *base, kCancelledLine); }, nullptr,
                 Describe, 60.0, 2.0});
    return std::make_unique<Leg>("trade_peer", std::move(s));
}

// ---------------------------------------------------------------------------
// give_npc
// ---------------------------------------------------------------------------

// Fixture (give_npc_seed.sql): Testchar 7 units from Captain Tillin (South
// Qeynos, -512 37), a Gnoll Fang in 26. His quest (qeynos/Captain_Tillin.lua)
// takes one fang and summons a Moonstone.
std::unique_ptr<IScenario> MakeGiveNpc(const ScenarioOptions&) {
    auto base = std::make_shared<size_t>(0);
    std::vector<Step> s;
    s.push_back({"fixture: the fang in 26, no moonstone, no trade", nullptr,
                 [](IScenarioWorld& w) {
                     return w.FindItemSlot(kFang) == 26 && w.FindItemSlot(kMoonstone) == 0 && !Trading(w);
                 },
                 nullptr, Describe, 30.0, 0.0});
    s.push_back({"/pickup 26 -> the fang on the cursor", [](IScenarioWorld& w) { w.SendCommand("/pickup 26"); },
                 [](IScenarioWorld& w) { return w.ClientItemAt(kCursor) == kFang; }, nullptr, Describe, 10.0, 1.0});
    s.push_back({"CLICK Captain Tillin with the fang held -> the GIVE window, the fang in 3000",
                 [](IScenarioWorld& w) {
                     if (!w.ClickEntity("Tillin")) w.Note("ClickEntity(Tillin) found no such entity");
                 },
                 [](IScenarioWorld& w) {
                     return Trading(w) && Has(w, IScenarioWorld::kTradeWithNpc) && w.ClientItemAt(kTrade1) == kFang;
                 },
                 nullptr, Describe, 15.0, 2.0});
    s.push_back({"Give -> the fang left us, the window shut, his reward arrived",
                 [base](IScenarioWorld& w) {
                     *base = w.ChatLineCount();
                     w.TradeButton(kTradeAccept);
                 },
                 [base](IScenarioWorld& w) {
                     return !Trading(w) && w.FindItemSlot(kFang) == 0 && w.FindItemSlot(kMoonstone) > 0 &&
                            ChatHas(w, *base, "One less gnoll");
                 },
                 nullptr, Describe, 20.0, 3.0});
    return std::make_unique<Leg>("give_npc", std::move(s));
}

// ---------------------------------------------------------------------------
// drop_destroy_coin
// ---------------------------------------------------------------------------

// Fixture (drop_destroy_coin_seed.sql): Testchar 9 units from the banker Sorn
// Copperhold, dagger 24, bottle 25, 10 pp carried, 3 pp banked.
std::unique_ptr<IScenario> MakeDropDestroyCoin(const ScenarioOptions&) {
    auto events = std::make_shared<long long>(0);
    auto objects = std::make_shared<long long>(0);
    auto asked = [events](IScenarioWorld& w) { return w.EventCount(kConfirmationRequested) > *events; };
    auto arm = [events](int op) {
        return [events, op](IScenarioWorld& w) {
            *events = w.EventCount(kConfirmationRequested);
            w.CursorGesture(op, 0, 0, 0);
        };
    };
    auto coin = [](int qty, int source) {
        return [qty, source](IScenarioWorld& w) { w.CursorGesture(kOpCoinClick, kPP, qty, source); };
    };
    auto coins = [](long long cursorCp, long long purse, long long bank) {
        return [=](IScenarioWorld& w) {
            return w.CursorCoinCopper() == cursorCp && Pp(w, false) == purse && Pp(w, true) == bank;
        };
    };
    std::vector<Step> s;
    s.push_back({"fixture: dagger 24, bottle 25, 10 pp carried, 3 pp banked", nullptr,
                 [](IScenarioWorld& w) {
                     return w.FindItemSlot(kDagger) == 24 && w.FindItemSlot(kBottle) == 25 && Pp(w, false) == 10 &&
                            Pp(w, true) == 3 && w.CursorQueueSize() == 0;
                 },
                 nullptr, Describe, 30.0, 0.0});
    s.push_back({"/pickup 25 -> the bottle on the cursor", [](IScenarioWorld& w) { w.SendCommand("/pickup 25"); },
                 [](IScenarioWorld& w) { return w.ClientItemAt(kCursor) == kBottle; }, nullptr, Describe, 10.0, 1.0});
    s.push_back({"a world click with it held -> the drop confirmation is asked (eqstr 9057)",
                 arm(kOpRequestDrop), asked, nullptr, Describe, 10.0, 1.0});
    s.push_back({"Yes -> OP_GroundSpawn: the bottle leaves us and a ground object appears",
                 [objects](IScenarioWorld& w) {
                     *objects = w.WorldObjectCount();
                     w.CursorGesture(kOpConfirmDrop, 0, 0, 0);
                 },
                 [objects](IScenarioWorld& w) {
                     return w.FindItemSlot(kBottle) == 0 && w.CursorQueueSize() == 0 &&
                            w.WorldObjectCount() > *objects;
                 },
                 nullptr, Describe, 10.0, 3.0});
    s.push_back({"/pickup 24 -> the dagger on the cursor", [](IScenarioWorld& w) { w.SendCommand("/pickup 24"); },
                 [](IScenarioWorld& w) { return w.ClientItemAt(kCursor) == kDagger; }, nullptr, Describe, 10.0, 1.0});
    s.push_back({"Destroy with it held -> the destroy confirmation is asked (eqstr 2043)",
                 arm(kOpRequestDestroy), asked, nullptr, Describe, 10.0, 1.0});
    s.push_back({"Yes -> OP_MoveItem 30 -> -1: the dagger is gone and stays gone",
                 [](IScenarioWorld& w) { w.CursorGesture(kOpConfirmDestroy, 0, 0, 0); },
                 [](IScenarioWorld& w) { return w.FindItemSlot(kDagger) == 0 && w.CursorQueueSize() == 0; },
                 nullptr, Describe, 10.0, 3.0});
    s.push_back({"purse pp row, 3 -> 3 pp on the cursor (OP_MoveCoin 1 -> 0)", coin(3, kPurse),
                 coins(3000, 7, 3), nullptr, Describe, 10.0, 2.0});
    s.push_back({"purse pp row with the coin held -> back in the purse (0 -> 1)", coin(0, kPurse),
                 coins(0, 10, 3), nullptr, Describe, 10.0, 2.0});
    s.push_back({"open the bank (target the banker, /bank)",
                 [](IScenarioWorld& w) {
                     w.SendCommand("/target Sorn");
                     w.SendCommand("/bank");
                 },
                 nullptr, nullptr, Describe, 1.0, 2.0});
    s.push_back({"bank pp row, 2 -> 2 pp on the cursor (OP_MoveCoin 2 -> 0)", coin(2, kBank), coins(2000, 10, 1),
                 nullptr, Describe, 10.0, 2.0});
    s.push_back({"purse pp row with the bank coin held -> carried (0 -> 1)", coin(0, kPurse), coins(0, 12, 1),
                 nullptr, Describe, 10.0, 2.0});
    s.push_back({"purse pp row, 4 -> 4 pp on the cursor", coin(4, kPurse), coins(4000, 8, 1), nullptr, Describe,
                 10.0, 2.0});
    s.push_back({"bank pp row with the coin held -> banked (0 -> 2)", coin(0, kBank), coins(0, 8, 5), nullptr,
                 Describe, 10.0, 4.0});
    return std::make_unique<Leg>("drop_destroy_coin", std::move(s));
}

// ---------------------------------------------------------------------------
// bank_change
// ---------------------------------------------------------------------------

// Fixture (bank_change_seed.sql): carried 0pp 25gp 37sp 123cp, banked 3pp
// 14gp 9sp 50cp, 9 units from Sorn Copperhold. Each store goes up to the
// fewest coins on its own (ctxbank.html): carried 123cp -> 3cp + 12sp, 49sp
// -> 9sp + 4gp, 29gp -> 9gp + 2pp = 2/9/9/3; banked 50cp -> 5sp, 14sp ->
// 4sp + 1gp, 15gp -> 5gp + 1pp = 4/5/4/0.
const std::vector<long long> kPurseBefore = {0, 25, 37, 123};
const std::vector<long long> kBankBefore = {3, 14, 9, 50};
const std::vector<long long> kPurseAfter = {2, 9, 9, 3};
const std::vector<long long> kBankAfter = {4, 5, 4, 0};

std::unique_ptr<IScenario> MakeBankChange(const ScenarioOptions&) {
    std::vector<Step> s;
    s.push_back({"fixture: carried 0/25/37/123, banked 3/14/9/50", nullptr,
                 [](IScenarioWorld& w) { return w.Coins(false) == kPurseBefore && w.Coins(true) == kBankBefore; },
                 nullptr, Describe, 30.0, 0.0});
    s.push_back({"open the bank (target the banker, /bank)",
                 [](IScenarioWorld& w) {
                     w.SendCommand("/target Sorn");
                     w.SendCommand("/bank");
                 },
                 nullptr, nullptr, Describe, 1.0, 2.0});
    s.push_back({"/bank change -> carried 2/9/9/3, banked 4/5/4/0, and it holds",
                 [](IScenarioWorld& w) { w.SendCommand("/bank change"); },
                 [](IScenarioWorld& w) { return w.Coins(false) == kPurseAfter && w.Coins(true) == kBankAfter; },
                 nullptr, Describe, 10.0, 5.0});
    return std::make_unique<Leg>("bank_change", std::move(s));
}

}  // namespace

std::unique_ptr<IScenario> MakeTradeRoundtripScenario(const ScenarioOptions& o) { return MakeTradeRoundtrip(o); }
std::unique_ptr<IScenario> MakeTradePeerScenario(const ScenarioOptions& o) { return MakeTradePeer(o); }
std::unique_ptr<IScenario> MakeGiveNpcScenario(const ScenarioOptions& o) { return MakeGiveNpc(o); }
std::unique_ptr<IScenario> MakeDropDestroyCoinScenario(const ScenarioOptions& o) { return MakeDropDestroyCoin(o); }
std::unique_ptr<IScenario> MakeBankChangeScenario(const ScenarioOptions& o) { return MakeBankChange(o); }

}  // namespace scenario
}  // namespace eqt
