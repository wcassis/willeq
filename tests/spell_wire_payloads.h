// T469 / T482 -- the Titanium S->C cast packets, byte for byte, for the spell
// wire tests (tests/test_spell_interrupt_wire.cpp,
// tests/test_spell_cast_end_wire.cpp). Each builder returns the payload WITHOUT
// the 2-byte opcode, which is what zonetest::ReplayZone() takes. Every layout is
// the struct eqemu puts on the wire (akk-stack/code/common/patches/
// titanium_structs.h unless named), after the Titanium ENCODE where there is one.

#pragma once

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace spellwire {

inline void PutLE(std::vector<uint8_t>& v, size_t at, uint32_t value, size_t width) {
    for (size_t i = 0; i < width; ++i) {
        v[at + i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
    }
}

// titanium_structs.h:426 InterruptCast_Struct {uint32 spawnid; uint32
// messageid; char message[0];}. No ENCODE. The caster copy has no message; the
// bystander copy carries the caster's clean name, NUL-terminated
// (zone/spells.cpp:1339-1343).
inline std::vector<uint8_t> InterruptCastPayload(uint32_t spawnid, uint32_t messageid,
                                                 const std::string* name = nullptr) {
    std::vector<uint8_t> v(8 + (name ? name->size() + 1 : 0), 0);
    PutLE(v, 0, spawnid, 4);
    PutLE(v, 4, messageid, 4);
    if (name) {
        std::memcpy(v.data() + 8, name->c_str(), name->size() + 1);
    }
    return v;
}

// titanium_structs.h:441 ManaChange_Struct {uint32 new_mana; uint32 stamina;
// uint32 spell_id; uint8 keepcasting; uint8 padding[3];} -- 16 bytes; the
// Titanium ENCODE (titanium.cpp:1247) drops the server struct's trailing slot.
inline std::vector<uint8_t> ManaChangePayload(uint32_t mana, uint32_t stamina,
                                              uint32_t spell_id, uint8_t keepcasting) {
    std::vector<uint8_t> v(16, 0);
    PutLE(v, 0, mana, 4);
    PutLE(v, 4, stamina, 4);
    PutLE(v, 8, spell_id, 4);
    v[12] = keepcasting;
    return v;
}

// BeginCast_Struct {uint16 caster_id; uint16 spell_id; uint32 cast_time;}
// (eq_packet_structs.h; what Mob::SendBeginCast fills, zone/spells.cpp:497).
inline std::vector<uint8_t> BeginCastWire(uint16_t caster, uint16_t spell, uint32_t ms) {
    std::vector<uint8_t> v(8, 0);
    PutLE(v, 0, caster, 2);
    PutLE(v, 2, spell, 2);
    PutLE(v, 4, ms, 4);
    return v;
}

// titanium_structs.h:407 MemorizeSpell_Struct {uint32 slot; uint32 spell_id;
// uint32 scribing; uint32 reduction;} -- 16 bytes. The ENCODE
// (titanium.cpp:1260-1274) maps `slot` through ServerToTitaniumCastingSlot for
// scribing 3 (a gem stays its 0-based index) and never copies `reduction`.
inline std::vector<uint8_t> MemorizeSpellPayload(uint32_t slot, uint32_t spell_id,
                                                 uint32_t scribing) {
    std::vector<uint8_t> v(16, 0);
    PutLE(v, 0, slot, 4);
    PutLE(v, 4, spell_id, 4);
    PutLE(v, 8, scribing, 4);
    return v;
}

// titanium_structs.h:2547 SimpleMessage_Struct {uint32 string_id; uint32
// color; uint32 unknown8;} -- what Client::MessageString(type, id) queues
// (zone/client.cpp:3811-3814).
inline std::vector<uint8_t> SimpleMessagePayload(uint32_t string_id, uint32_t color) {
    std::vector<uint8_t> v(12, 0);
    PutLE(v, 0, string_id, 4);
    PutLE(v, 4, color, 4);
    return v;
}

// titanium_structs.h:2541 FormattedMessage_Struct {uint32 unknown0; uint32
// string_id; uint32 type; char message[0];}: each argument NUL-terminated, then
// one more NUL (zone/client.cpp:3860-3870; the Titanium ENCODE rebuilds the
// same shape, titanium.cpp:676).
inline std::vector<uint8_t> FormattedMessagePayload(uint32_t string_id, uint32_t type,
                                                    std::initializer_list<std::string> args) {
    std::vector<uint8_t> v(12, 0);
    PutLE(v, 4, string_id, 4);
    PutLE(v, 8, type, 4);
    for (const auto& a : args) {
        v.insert(v.end(), a.begin(), a.end());
        v.push_back(0);
    }
    v.push_back(0);
    return v;
}

// titanium_structs.h:1118 Action_Struct -- 31 bytes (target, source, level,
// instrument_mod, force, hit_heading, hit_pitch, type, unknown23, unknown25,
// spell, spell_level, effect_flag). type 231 is a spell (zone/spells.cpp:3996).
inline std::vector<uint8_t> ActionPayload(uint16_t target, uint16_t source, uint16_t level,
                                          uint16_t spell, uint8_t effect_flag) {
    std::vector<uint8_t> v(31, 0);
    PutLE(v, 0, target, 2);
    PutLE(v, 2, source, 2);
    PutLE(v, 4, level, 2);
    v[22] = 231;
    PutLE(v, 27, spell, 2);
    v[29] = static_cast<uint8_t>(level);
    v[30] = effect_flag;
    return v;
}

// eq_packet_structs.h:428 LinkedSpellReuseTimer_Struct {uint32 timer_id;
// uint32 end_time; uint32 start_time;} -- no Titanium ENCODE.
inline std::vector<uint8_t> LinkedReusePayload(uint32_t timer_id, uint32_t end_time,
                                               uint32_t start_time) {
    std::vector<uint8_t> v(12, 0);
    PutLE(v, 0, timer_id, 4);
    PutLE(v, 4, end_time, 4);
    PutLE(v, 8, start_time, 4);
    return v;
}

// titanium_structs.h:482 SpawnAppearance_Struct {uint16 spawn_id; uint16
// type; uint32 parameter;} -- 8 bytes.
inline std::vector<uint8_t> SpawnAppearancePayload(uint16_t spawn_id, uint16_t type,
                                                   uint32_t parameter) {
    std::vector<uint8_t> v(8, 0);
    PutLE(v, 0, spawn_id, 2);
    PutLE(v, 2, type, 2);
    PutLE(v, 4, parameter, 4);
    return v;
}

}  // namespace spellwire
