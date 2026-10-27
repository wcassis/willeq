/*
 * B1170 (#489): the official client's spell missile OBJECT, device-free.
 *
 * The client draws a record's Middle stage (stage 1) only on a missile object
 * (eqgame 00495d8e / 00495d93: an anchor-2 slot with no object is skipped),
 * and the only object is the one the 0x4033 message arm builds
 * (004606f6 sub ecx, 8 / 004606f9 je 0x46070f -> 0046070f push 0x88 ->
 * 00460737 call 0x476f34, with the message as the third argument, 00460732
 * push ebx). This header is that object: the wire decode (the constructor's
 * RECEIVED branch copies the 0x88-byte message field by field, 004770eb ..),
 * the spawn (FUN_00476b12), the per-pass homing (FUN_004766aa) and physics
 * (FUN_004bfe0f's missile arm), and the removal rules (FUN_00477cc9 /
 * FUN_0047789d). Every address is an instruction start in
 * review/B1141/ghidra/out/eqgame.dis (or EQGraphicsDX9.dis for the trig
 * tables); the trace is refactor/B1170/README.md.
 *
 * No <irrlicht.h>: SpellVisualFX (src/client/graphics/spell_visual_fx_missile.cpp)
 * draws on it, and test_b1170_missile_object pins it without a display.
 *
 * Coordinates are the CLIENT's: pos[0] / vel[0] are EQ y, pos[1] / vel[1]
 * EQ x, pos[2] / vel[2] EQ z (the server writes src_y at +0x10 and src_x at
 * +0x14, eqemu common/eq_packet_structs.h Arrow_Struct). Angles are in the
 * client's 512-unit circle. Time is the client's time() unit, read as ms
 * (refactor/B1163/README.md (A); the unit is a runtime rdtsc calibration,
 * refactor/B1168/README.md (B)).
 */

#ifndef EQT_SPELL_SPELL_MISSILE_OBJECT_H
#define EQT_SPELL_SPELL_MISSILE_OBJECT_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace EQ {
namespace SpellMissile {

// The message: OP_SomeItemPacketMaybe (Titanium 0x4033), 0x88 bytes
// (0046070f push 0x88 sizes the object the message is copied into).
inline constexpr uint16_t kOpcode = 0x4033;
inline constexpr size_t kWireSize = 0x88;

// The object type byte [+0x63] for a spell missile (00476b49 cmp byte ptr
// [esi + 0x63], 9 in FUN_00476b12; 0047793d in the removal).
inline constexpr uint8_t kTypeSpell = 9;

// The fields the constructor's received branch copies (exe.c 95447-95471),
// at the same offsets in the message and in the object.
struct Wire {
    float pos[3] = {0, 0, 0};    // +0x10 +0x14 +0x18 (EQ y, x, z)
    float vel[3] = {0, 0, 0};    // +0x1c +0x20 +0x24
    float word = 0.0f;           // +0x28 thrust word (004770eb / 004770ee)
    float heading = 0.0f;        // +0x2c
    float pitch = 0.0f;          // +0x30
    float pitchRate = 0.0f;      // +0x34
    float headingRate = 0.0f;    // +0x38
    float aim = 0.0f;            // +0x3c the thrust's vertical angle
    uint32_t sourceId = 0;       // +0x4c
    uint32_t targetId = 0;       // +0x50
    uint32_t word54 = 0;         // +0x54
    uint32_t word58 = 0;         // +0x58
    uint32_t spellId = 0;        // +0x5c
    uint8_t light = 0;           // +0x60
    uint8_t byte61 = 0;          // +0x61
    uint8_t damped = 0;          // +0x62 (0: damping 0.85 and the 0.05 stop, 004c075c)
    uint8_t type = 0;            // +0x63 (9 = spell)
    uint8_t type2 = 0;           // +0x64
    std::string model;           // +0x65, NUL-terminated (FUN_0060e450 strcpy)
};

// Decode the message payload (the bytes after the 2-byte opcode). False when
// it is shorter than kWireSize (the client copies 0x88 bytes regardless; a
// short message is not one this decoder will guess at). Velocity components
// below 0.001 in magnitude are zeroed, as the constructor does after either
// branch (exe.c 95498-95509, [0x641088] = 0.001).
bool DecodeWire(const uint8_t* payload, size_t len, Wire& out);

// The spell fields the spawner and removal read from the client's spell
// record (FUN_0040cbd6), spells_us.txt 0-based fields: 18 (+0x28, the
// lifetime / the stage's caller duration), 98 (+0x180, target type), 122
// (+0x188, travel type).
struct SpellFields {
    bool exists = false;
    int32_t aeDuration = 0;   // field 18
    uint8_t targetType = 0;   // field 98
    uint8_t travelType = 0;   // field 122
};
// Parse one ^-separated spells_us.txt line into `out` (false: not a full
// record). `id` is field 0.
bool ParseSpellsUsLine(const std::string& line, uint32_t& id, SpellFields& out);

// An entity the object reads: its position (client order y, x, z), its model
// height (+0x238, the homing aims at z + 0.5 x height, 004767df / 004767e5)
// and whether it has an actor (the hit test needs [target+0x1a8], 0047674d).
struct Entity {
    bool exists = false;
    float pos[3] = {0, 0, 0};
    float height = 0.0f;
    bool hasActor = true;
};

// --- The trig the client uses: EQGraphicsDX9's 512-entry tables -------------
// FUN_101848a0 fills cos[i] = cos(i x [0x101ec4b0]) for i in 0..511 (exact
// 0 / +-1 at the quadrant points) and sin from the same values shifted by
// 128; the lookups 10184500 (sin) / 10184520 (cos) truncate the angle
// (_ftol2, 101a9b60) and mask it with 0x1ff.
float TableSin(float angle512);
float TableCos(float angle512);
// FUN_101846a0 (the vt+0x18 slot) for a first-quadrant ratio y / x (x > 0):
// the largest table index i in 0..127 with tan[i] <= ratio (tan[i] =
// sin[i] / cos[i], FUN_101849c0), found by the BST walk 10184780 ..
// 101847fe.
int TableAtanIndex(float ratio);
// FUN_004365db: wrap into [0, 512) (at most 7 steps, else 0).
float WrapAngle(float a);
// FUN_00436628: |a - b| on the 512 circle; `signedOut` gets the raw
// difference (<= 256) or difference - 512.
float AngleDiff(float a, float b, float* signedOut);
// FUN_0046e42f: the bearing from (y1, x1) to (y2, x2), in 512 units
// (degrees x 511.5 / 360, [0x64604c] / [0x646048]).
float Bearing(float y1, float x1, float y2, float x2);
// FUN_00439c41 with a 0.0 7th argument: the XY distance.
float DistanceXY(float y1, float x1, float y2, float x2);

// --- The object ------------------------------------------------------------
struct Object {
    Wire w;                    // the live state (pos / vel / word / angles)
    bool spawned = false;      // FUN_00476b12 built the actor ([[m+0x48]] != 0)
    bool hit = false;          // [actor+0x1005] = 1 (removed this pass)
    uint32_t lastPass = 0;     // [actor+0x50]
    uint32_t refreshAt = 0;    // [actor+0xe4]
    uint32_t lifeEnd = 0;      // [actor+0x68]
    float bearing = 0.0f;      // [actor+0xfec]
    float aimTarget = 0.0f;    // [actor+0xff0]
    float stored = 2000.0f;    // [actor+0xff4], 00476eef 0x44fa0000
    bool targetCleared = false;// [actor+0xfe8] = 0 (target == source, 00476714)
    int stage = 0;             // the stage FUN_00476b12 fired: 0 none, 1, 2
};

// FUN_00476b12 (the spawn, run while the object has no actor). `spell` is
// the record for w.spellId (nullptr: none); `source` / `target` are the
// entities w.sourceId / w.targetId name; `sourceTickAge` is time() minus the
// caster's last physics tick ([[caster+0x1a8]+0x50], the catch-up term).
// Returns false when the object is removed without an actor (no source
// entity, or a type-9 object whose spell id / record does not resolve:
// 00476b65 sets [actor+0x1005]). On success sets spawned, lifeEnd, stage
// (1 = Middle, 2 = Target for the no-flight target types), the catch-up
// position and the homing state.
bool Spawn(Object& o, uint32_t now, const SpellFields* spell, const Entity& source,
           const Entity& target, uint32_t sourceTickAge);

// The no-flight target types (00476bec .. 00476c02): 8, 0x15..0x17, 0x1c.
bool TargetTypeHasNoFlight(uint8_t targetType);

// A pass runs when at least kMinPassUnits elapsed since [actor+0x50]
// (00445577 .. 00445583 jb).
inline constexpr uint32_t kMinPassUnits = 33;
inline bool PassDue(const Object& o, uint32_t frameNow) {
    return o.spawned && frameNow - o.lastPass >= kMinPassUnits;
}

// FUN_004766aa (00445587): the homing / hit step, before the physics.
void Home(Object& o, uint32_t frameNow, const Entity& target);
// FUN_004bfe0f's missile arm (0044558f): damped thrust, then the move.
void Physics(Object& o, uint32_t now);
// FUN_00477cc9's removal decisions this implements: the hit flag and the
// lifetime (0047821b cmp ecx, dword ptr [eax + 0x68] / 0047821e ja).
bool RemovedAfterPass(const Object& o, uint32_t frameNow);

// One whole pass (Home, Physics, RemovedAfterPass) at `now` (frame time =
// time()); returns true when the object is removed.
bool Pass(Object& o, uint32_t now, const Entity& target);

// FUN_0047789d's stage-2 fires at removal (type 9 with an actor only,
// 0047793d / 00477947): spell 0x36 (54) fires stage 2 at the position one
// velocity step back (00477959 .. 00477aa3); spell 0x35b (859) goes through
// the 40-unit gate at the local player (spell_b1163_missile_rules.h).
enum class RemovalFire { None, AtPositionBack, AtLocalPlayer };
RemovalFire RemovalFireFor(const Object& o);
// The position spell 54's fire uses: pos - vel (00477a55 .. 00477a6f).
void RemovalPosition(const Object& o, float out[3]);

}  // namespace SpellMissile
}  // namespace EQ

#endif
