#pragma once
/*
 * B441 (GitLab #26, UIGAP-small) -- the bank window's Change button.
 *
 * WHAT THE OFFICIAL BUTTON DOES. There is NO coin-conversion dialog in the
 * client's UI: no EQUI_*.xml under uifiles/default declares one (the bank
 * window's own file has BW_ChangeButton and nothing it opens), and the
 * client's own Bank Tip window says what the button does, in so many words
 * (help/tips/ctxbank.html):
 *
 *   "Click on the "Change" button to convert all your coin, both in the bank
 *    and on your person, to the fewest possible coins."
 *
 * So Change is ONE click, no dialog: every copper that makes a silver
 * becomes one, then silver to gold, then gold to platinum, in the bank purse
 * and in the carried purse. T436 (and #26's text) assumed a denomination
 * dialog; the oracle's own files contradict that, so none is built.
 *
 * THE WIRE. Each step is one OP_MoveCoin with from_slot == to_slot (2 the
 * bank, 1 the carried purse) and cointype1 -> cointype1 + 1, exactly the
 * conversion eqemu's Client::OPMoveCoin accepts: it clamps the amount to the
 * source bucket, adds amount * coppers(from) / coppers(to) TRUNCATING, takes
 * back only what that added, and SENDS NOTHING BACK. So every amount here is
 * an exact multiple of 10 (nothing is truncated away) and the caller applies
 * the same arithmetic to its own counters. A step with nothing to convert is
 * not sent.
 *
 * ASSUMPTIONS AWAITING ORACLE: the order (bank purse before the carried one,
 * copper upwards) and that the official client also converts in place
 * rather than moving coin between the two purses. Neither changes the
 * end state the tip describes.
 */

#include <array>
#include <cstdint>
#include <vector>

#include "common/packets/merchant.h"

namespace eqt {
namespace merchant {

/** One purse, indexed by EQT::COINTYPE_* (0 cp, 1 sp, 2 gp, 3 pp). */
using CoinPurse = std::array<uint32_t, 4>;

/**
 * The OP_MoveCoin steps that turn `purse` (held in `slot`,
 * EQT::COINSLOT_BANK or EQT::COINSLOT_INVENTORY) into the fewest coins, and
 * `purse` rewritten to the result the server will hold. Copper -> silver ->
 * gold -> platinum, each step carrying what the previous one made.
 */
std::vector<EQT::MoveCoin_Struct> BuildMakeChangeMoves(int32_t slot, CoinPurse& purse);

}  // namespace merchant
}  // namespace eqt
