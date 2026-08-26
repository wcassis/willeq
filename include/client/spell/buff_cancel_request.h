/*
 * Titanium buff click-off request (T277).
 *
 * WIRE TRUTH, verified against eqemu at /mnt/projects/akk-stack/code:
 *
 *   Titanium has NO OP_BuffRemoveRequest. `BuffRemoveRequest_Struct`
 *   (common/eq_packet_structs.h:559) is commented "Underfoot & later
 *   struct." and the opcode is registered only in rof/rof2/uf ops tables --
 *   `utils/patches/patch_Titanium.conf` has no OP_BuffRemoveRequest line at
 *   all, so a Titanium client that sent it would be sending an unmapped
 *   opcode. The Titanium click-off travels on OP_Buff (0x6a53) instead,
 *   carrying SpellBuffPacket_Struct.
 *
 *   Titanium SpellBuffPacket_Struct (common/patches/titanium_structs.h:504)
 *   is 32 bytes -- 4 shorter than the server-internal one, because
 *   Titanium's SpellBuff_Struct stops at player_id (20 bytes) where the
 *   emu struct continues with num_hits/y/x/z (36 bytes):
 *
 *       +00  uint32 entityid
 *       +04  SpellBuff_Struct buff   (20 bytes: effect_type, level,
 *                                      bard_modifier, unknown003, spellid,
 *                                      duration, counters, player_id)
 *       +24  uint32 slotid
 *       +28  uint32 bufffade
 *
 *   Server side, Client::Handle_OP_Buff (zone/client_packet.cpp:4181) reads
 *   exactly ONE field -- buff.spellid:
 *
 *     if (spid == 0xFFFF || (IsValidSpell(spid) && spells[spid].good_effect == 0))
 *         QueuePacket(app);            // REFUSED: the request is echoed back
 *     else
 *         BuffFadeBySpellID(spid);     // ACCEPTED
 *
 *   entityid, slotid and bufffade are never read. An accepted cancel is
 *   confirmed by the server's own OP_Buff with bufffade == 1
 *   (Client::MakeBuffFadePacket, zone/spells.cpp:5845), which is what
 *   actually removes the buff from our model.
 *
 *   THE ECHO IS WHY bufffade IS 0 ON THE REQUEST. A refusal replays our own
 *   bytes straight back into ZoneProcessBuff(). With bufffade == 1 the
 *   client would delete the buff it was just refused permission to drop --
 *   a refusal that looks exactly like a success. Sending 0 makes the echo a
 *   faithful re-application of a buff we already hold instead.
 *
 *   Note also that the OP_Buff path does NOT honour spells[].no_remove; only
 *   the Underfoot+ OP_BuffRemoveRequest path checks it. On Titanium the
 *   server's whole refusal rule is "detrimental spells stay".
 */

#ifndef EQT_BUFF_CANCEL_REQUEST_H
#define EQT_BUFF_CANCEL_REQUEST_H

#include <array>
#include <cstddef>
#include <cstdint>

namespace EQ {

struct ActiveBuff;

// Full packet size: 2-byte opcode + 32-byte Titanium SpellBuffPacket_Struct.
inline constexpr size_t kBuffCancelPacketSize = 34;

// Body offsets, opcode-inclusive (add 2 to the struct offsets above), which
// is how ZoneProcessBuff() reads the same packet coming the other way.
inline constexpr size_t kBuffPacketEntityId   = 2;
inline constexpr size_t kBuffPacketEffectType = 6;
inline constexpr size_t kBuffPacketLevel      = 7;
inline constexpr size_t kBuffPacketBardMod    = 8;
inline constexpr size_t kBuffPacketUnknown003 = 9;
inline constexpr size_t kBuffPacketSpellId    = 10;
inline constexpr size_t kBuffPacketDuration   = 14;
inline constexpr size_t kBuffPacketCounters   = 18;
inline constexpr size_t kBuffPacketPlayerId   = 22;
inline constexpr size_t kBuffPacketSlotId     = 26;
inline constexpr size_t kBuffPacketBuffFade   = 30;

/**
 * Build the C->S OP_Buff bytes that ask the server to drop `buff`.
 *
 * Pure and allocation-free so the bytes can be asserted without a zone
 * connection. `opcode` is passed in rather than hard-coded so the test
 * pins the same HC_OP_Buff the sender uses.
 */
std::array<uint8_t, kBuffCancelPacketSize> BuildBuffCancelPacket(
    uint16_t opcode, uint32_t entityId, uint8_t slot, const ActiveBuff& buff);

}  // namespace EQ

#endif  // EQT_BUFF_CANCEL_REQUEST_H
