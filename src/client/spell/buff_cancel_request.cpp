/*
 * T277: the C->S OP_Buff click-off request builder. See
 * include/client/spell/buff_cancel_request.h for the wire truth this
 * encodes and why bufffade is 0 on the request.
 */

#include "client/spell/buff_cancel_request.h"
#include "client/spell/buff_manager.h"

#include <cstring>

namespace EQ {

namespace {

void PutU16(uint8_t* p, size_t off, uint16_t v) { std::memcpy(p + off, &v, sizeof(v)); }
void PutU32(uint8_t* p, size_t off, uint32_t v) { std::memcpy(p + off, &v, sizeof(v)); }

}  // namespace

std::array<uint8_t, kBuffCancelPacketSize> BuildBuffCancelPacket(
    uint16_t opcode, uint32_t entityId, uint8_t slot, const ActiveBuff& buff)
{
    std::array<uint8_t, kBuffCancelPacketSize> out{};
    uint8_t* p = out.data();

    PutU16(p, 0, opcode);
    PutU32(p, kBuffPacketEntityId, entityId);

    // Mirror the buff we hold. The server reads only spellid, but a REFUSAL
    // echoes these bytes back through our own ZoneProcessBuff(), so every
    // field has to describe the buff as it already is.
    p[kBuffPacketEffectType] = static_cast<uint8_t>(buff.effect_type);
    p[kBuffPacketLevel]      = buff.caster_level;
    p[kBuffPacketBardMod]    = 0;
    p[kBuffPacketUnknown003] = 0;
    PutU32(p, kBuffPacketSpellId, buff.spell_id);

    // ActiveBuff stores seconds; the wire field is ticks (6s each).
    // Permanent buffs carry the sentinel through unchanged, which the
    // reader turns back into "permanent" via its duration < 0 branch.
    uint32_t durationTicks = buff.isPermanent()
        ? 0xFFFFFFFFu
        : buff.remaining_seconds / 6u;
    PutU32(p, kBuffPacketDuration, durationTicks);

    PutU32(p, kBuffPacketCounters, buff.counters);
    PutU32(p, kBuffPacketPlayerId, buff.caster_id);
    PutU32(p, kBuffPacketSlotId, slot);
    PutU32(p, kBuffPacketBuffFade, 0u);  // NOT 1 -- see the header.

    return out;
}

}  // namespace EQ
