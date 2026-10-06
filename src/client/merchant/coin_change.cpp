/*
 * B441 (#26) -- the bank's Change button. See coin_change.h.
 */

#include "client/merchant/coin_change.h"

#include <limits>

namespace eqt {
namespace merchant {

std::vector<EQT::MoveCoin_Struct> BuildMakeChangeMoves(int32_t slot, CoinPurse& purse) {
    std::vector<EQT::MoveCoin_Struct> moves;
    for (int32_t from = EQT::COINTYPE_CP; from < EQT::COINTYPE_PP; ++from) {
        const uint32_t made = purse[from] / 10;
        if (made == 0) continue;
        // OP_MoveCoin's amount is an int32; a purse past that converts in
        // more than one step (the server clamps to the bucket either way).
        uint32_t remaining = made * 10;
        while (remaining > 0) {
            const uint32_t chunk = remaining > 2000000000u ? 2000000000u : remaining;
            EQT::MoveCoin_Struct m{};
            m.from_slot = slot;
            m.to_slot = slot;
            m.cointype1 = from;
            m.cointype2 = from + 1;
            m.amount = static_cast<int32_t>(chunk);
            moves.push_back(m);
            remaining -= chunk;
        }
        purse[from] -= made * 10;
        const uint32_t room = std::numeric_limits<uint32_t>::max() - purse[from + 1];
        purse[from + 1] += made > room ? room : made;
    }
    return moves;
}

}  // namespace merchant
}  // namespace eqt
