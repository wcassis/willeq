// B1170 (#489): the official client's spell missile object. See the header
// and refactor/B1170/README.md; every address is an instruction start in
// review/B1141/ghidra/out/eqgame.dis unless it names EQGraphicsDX9.

#include "client/spell/spell_missile_object.h"

#include "client/graphics/spell_b1163_missile_rules.h"

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace EQ {
namespace SpellMissile {

namespace {

float ReadF32(const uint8_t* p, size_t off) {
    float v;
    std::memcpy(&v, p + off, sizeof(v));
    return v;
}

uint32_t ReadU32(const uint8_t* p, size_t off) {
    uint32_t v;
    std::memcpy(&v, p + off, sizeof(v));
    return v;
}

// The two EQGraphicsDX9 tables FUN_101848a0 builds: cos[i] = (float)
// cos(i x [0x101ec4b0]) (the x87 fcos of a double product), with the
// quadrant points forced to exact 0 / +-1; sin[(i + 128) & 511] = cos[i].
struct TrigTables {
    std::array<float, 512> cosT{};
    std::array<float, 512> sinT{};
    std::array<float, 128> tanT{};  // FUN_101849c0: sin[i] / cos[i]
    TrigTables() {
        const long double step = 0.012271846302734375L;  // [0x101ec4b0] (f64)
        for (int i = 0; i < 512; ++i) {
            const float c = static_cast<float>(std::cos(static_cast<long double>(i) * step));
            cosT[static_cast<size_t>(i)] = c;
            sinT[static_cast<size_t>((i + 128) & 511)] = c;
        }
        sinT[0] = 0.0f;   sinT[256] = 0.0f;  sinT[128] = 1.0f;  sinT[384] = -1.0f;
        cosT[128] = 0.0f; cosT[384] = 0.0f;  cosT[0] = 1.0f;    cosT[256] = -1.0f;
        for (size_t i = 0; i < 128; ++i) tanT[i] = sinT[i] / cosT[i];
    }
};

const TrigTables& Tables() {
    static const TrigTables t;
    return t;
}

// _ftol2 (EQGraphicsDX9 101a9b60): truncation toward zero, then & 0x1ff.
size_t TableIndex(float angle512) {
    const auto i = static_cast<int64_t>(angle512);
    return static_cast<size_t>(static_cast<uint64_t>(i) & 0x1ffu);
}

}  // namespace

bool DecodeWire(const uint8_t* payload, size_t len, Wire& out) {
    if (!payload || len < kWireSize) return false;
    Wire w;
    w.pos[0] = ReadF32(payload, 0x10);
    w.pos[1] = ReadF32(payload, 0x14);
    w.pos[2] = ReadF32(payload, 0x18);
    w.aim = ReadF32(payload, 0x3c);   // the order the received branch copies
    w.pitch = ReadF32(payload, 0x30);
    w.headingRate = ReadF32(payload, 0x38);
    w.pitchRate = ReadF32(payload, 0x34);
    w.heading = ReadF32(payload, 0x2c);
    w.word = ReadF32(payload, 0x28);
    w.vel[0] = ReadF32(payload, 0x1c);
    w.vel[1] = ReadF32(payload, 0x20);
    w.vel[2] = ReadF32(payload, 0x24);
    w.sourceId = ReadU32(payload, 0x4c);
    w.targetId = ReadU32(payload, 0x50);
    w.word54 = ReadU32(payload, 0x54);
    w.word58 = ReadU32(payload, 0x58);
    w.spellId = ReadU32(payload, 0x5c);
    w.light = payload[0x60];
    w.byte61 = payload[0x61];
    w.damped = payload[0x62];
    w.type = payload[0x63];
    w.type2 = payload[0x64];
    // +0x65: a NUL-terminated string copied with strcpy (FUN_0060e450); this
    // decoder stops at the end of the 0x88-byte message.
    const char* s = reinterpret_cast<const char*>(payload + 0x65);
    w.model.assign(s, ::strnlen(s, kWireSize - 0x65));
    // Both constructor branches end by zeroing a velocity component under
    // 0.001 in magnitude ([0x641088]).
    for (float& v : w.vel)
        if (std::fabs(static_cast<double>(v)) < 0.0010000000474974513) v = 0.0f;
    out = std::move(w);
    return true;
}

bool ParseSpellsUsLine(const std::string& line, uint32_t& id, SpellFields& out) {
    std::vector<std::string> f;
    size_t start = 0;
    for (;;) {
        const size_t p = line.find('^', start);
        f.push_back(line.substr(start, p == std::string::npos ? std::string::npos : p - start));
        if (p == std::string::npos) break;
        start = p + 1;
    }
    if (f.size() <= 122) return false;
    try {
        id = static_cast<uint32_t>(std::stoul(f[0]));
        out.aeDuration = static_cast<int32_t>(std::stol(f[18]));
        out.targetType = static_cast<uint8_t>(std::stoi(f[98]));
        out.travelType = static_cast<uint8_t>(std::stoi(f[122]));
    } catch (...) {
        return false;
    }
    out.exists = true;
    return true;
}

float TableSin(float angle512) { return Tables().sinT[TableIndex(angle512)]; }
float TableCos(float angle512) { return Tables().cosT[TableIndex(angle512)]; }

int TableAtanIndex(float ratio) {
    // The BST walk ends on the node the ratio sorts to; mode 1 (101847f5)
    // then steps one index back when the ratio is below that node's value:
    // the result is the largest i with tan[i] <= ratio (0 below tan[1]).
    const auto& t = Tables().tanT;
    int best = 0;
    for (int i = 0; i < 128; ++i)
        if (t[static_cast<size_t>(i)] <= ratio) best = i;
    return best;
}

float WrapAngle(float a) {
    int i = 0;
    while (a >= 512.0f) {
        a -= 512.0f;
        if (i > 5) { a = 0.0f; break; }
        ++i;
    }
    i = 0;
    while (a < 0.0f) {
        a += 512.0f;
        if (i > 5) { a = 0.0f; break; }
        ++i;
    }
    return a;
}

float AngleDiff(float a, float b, float* signedOut) {
    const float lo = b < a ? b : a;
    const float hi = b < a ? a : b;
    const float d = hi - lo;
    if (d <= 256.0f) {  // [0x641140]
        if (signedOut) *signedOut = d;
        return d;
    }
    if (signedOut) *signedOut = d - 512.0f;
    return -(d - 512.0f);
}

float Bearing(float y1, float x1, float y2, float x2) {
    double dy = std::fabs(static_cast<double>(y1 - y2));
    const double dx = std::fabs(static_cast<double>(x1 - x2));
    if (dy < 9.999999974752427e-07) dy = 9.999999974752427e-07;  // [0x647170]
    long double f = static_cast<long double>(std::atan2(dx, dy)) * 180.0L * 0.3183099014828645L;
    if (y2 <= y1) {
        if (x1 <= x2) {
            f = (90.0L - f) + 90.0L;
        } else {
            f = f + 180.0L;
        }
    } else if (x2 <= x1) {
        f = (90.0L - f) + 270.0L;
    }
    return static_cast<float>(f * 511.5L * static_cast<long double>(0.0027777778450399637f));
}

float DistanceXY(float y1, float x1, float y2, float x2) {
    const double dy = static_cast<double>(y1) - y2;
    const double dx = static_cast<double>(x1) - x2;
    return static_cast<float>(std::sqrt(dy * dy + dx * dx));
}

bool TargetTypeHasNoFlight(uint8_t t) {
    return t == 8 || (t > 0x14 && (t < 0x18 || t == 0x1c));
}

bool Spawn(Object& o, uint32_t now, const SpellFields* spell, const Entity& source,
           const Entity& target, uint32_t sourceTickAge) {
    // 00476b45 .. 00476b63: no caster actor -> [actor+0x1005] = 1.
    if (!source.exists) {
        o.hit = true;
        return false;
    }
    o.lifeEnd = now + 120000;  // 00476b6f add eax, 0x1d4c0
    o.stage = 0;
    auto& w = o.w;
    if (w.type == kTypeSpell) {
        if (!SpellMissileRules::SpellIdInRange(w.spellId) || !spell || !spell->exists) {
            o.hit = true;  // 00476b93 / 00476ba4 -> 00476b65
            return false;
        }
        // 00476bbe .. 00476be0: the lifetime.
        uint32_t life;
        if (spell->aeDuration != 0) life = static_cast<uint32_t>(spell->aeDuration);
        else if (spell->travelType == 3) life = 30000;
        else life = 0x32;
        o.lifeEnd = now + life;
        if (TargetTypeHasNoFlight(spell->targetType)) {
            // 00476d31 .. 00476d6e: on the target, no flight, the Target stage.
            if (target.exists) {
                w.pos[0] = target.pos[0];
                w.pos[1] = target.pos[1];
                w.pos[2] = target.pos[2];
            }
            o.stage = 2;
            w.vel[0] = w.vel[1] = w.vel[2] = 0.0f;
            w.word = 0.0f;
        } else {
            o.stage = 1;
            // 00476c18 .. 00476c36: 0.02 x the caster's tick age.
            const float k = static_cast<float>(sourceTickAge) * 0.019999999552965164f;
            // The race 0x31 / 0x9e / 0xc4 branch (00476c3c ..) is not
            // implemented (refactor/B1170/README.md, FOLLOW-UP).
            if (spell->travelType != 3) {
                const float c = 5.5f;  // [0x64263c]
                w.pos[0] = w.vel[0] * c + w.pos[0];
                w.pos[1] = w.vel[1] * c + w.pos[1];
                w.pos[2] = w.vel[2] * c + w.pos[2];
                w.pos[0] = k * w.vel[0] + w.pos[0];
                w.pos[1] = k * w.vel[1] + w.pos[1];
                w.pos[2] = k * w.vel[2] + w.pos[2];
            } else {
                w.vel[2] = 0.0f;
            }
        }
    } else {
        // Any other type: one velocity step.
        w.pos[0] = w.vel[0] + w.pos[0];
        w.pos[1] = w.vel[1] + w.pos[1];
        w.pos[2] = w.vel[2] + w.pos[2];
    }
    // 00476e0a .. 00476f30: the actor's homing state.
    o.bearing = w.heading;
    o.aimTarget = w.aim;
    o.stored = SpellMissileRules::kMissileInitialStored;
    o.lastPass = now;
    o.refreshAt = now + (w.type == kTypeSpell ? 2000u : 500u);
    o.spawned = true;
    return true;
}

void Home(Object& o, uint32_t frameNow, const Entity& target) {
    auto& w = o.w;
    // 004766d8 .. 00476703: a zero word, no target, or a target named 'x'
    // (not modelled) -> nothing.
    if (w.word == 0.0f || o.targetCleared || !target.exists) return;
    // 0047670b .. 0047671e: aimed at its own caster -> the target is cleared.
    if (w.targetId == w.sourceId) {
        o.targetCleared = true;
        return;
    }
    uint32_t threshold;
    if (o.stored >= 200.0f) {           // 00476730 [0x641f90]
        if (o.stored < 250.0f) threshold = 0xfa;       // 0047678d [0x64475c]
        else threshold = o.stored < 350.0f ? 0x1f4 : 0x3e8;  // 004767ac [0x64756c]
    } else {
        threshold = 0;
        if (o.stored < SpellMissileRules::kMissileHitDistance && target.hasActor) {
            o.hit = true;               // 00476769 mov byte ptr [eax + 0x1005], 1
            return;
        }
    }
    if (frameNow - o.refreshAt > threshold) {  // 004767cb .. 004767d3 (unsigned)
        const float tz = target.height * 0.5f + target.pos[2];
        const float dz = tz - w.pos[2];
        const float adz = dz < 0.0f ? -dz : dz;
        o.refreshAt = frameNow;
        o.bearing = Bearing(w.pos[0], w.pos[1], target.pos[0], target.pos[1]);
        o.stored = DistanceXY(w.pos[0], w.pos[1], target.pos[0], target.pos[1]);
        if (adz <= 0.5f) {
            o.aimTarget = 0.0f;         // 00476925 .. 0047692a
        } else {
            o.aimTarget = 128.0f - static_cast<float>(TableAtanIndex(o.stored / adz));
            if (dz < 0.0f) o.aimTarget = -o.aimTarget;
        }
    }
    if (o.stored < 32.0f) {             // 00476939 [0x64112c]
        w.heading = o.bearing;
        w.aim = o.aimTarget;
        return;
    }
    float s1 = 0.0f, s2 = 0.0f;
    const float d1 = AngleDiff(w.heading, o.bearing, &s1);
    const float d2 = AngleDiff(w.aim, o.aimTarget, &s2);
    if (d1 >= 1.0f) {
        const float step = d1 < 16.0f ? d1 : 16.0f;
        if ((w.heading < o.bearing && s1 < 0.0f) || (o.bearing < w.heading && s1 > 0.0f))
            w.heading = w.heading - step;
        else
            w.heading = step + w.heading;
        w.heading = WrapAngle(w.heading);
    } else {
        w.heading = o.bearing;
    }
    if (d2 >= 1.0f) {
        if (d2 < 16.0f) return;         // 00476a79 .. 00476a80
        if (s2 < 0.0f || o.aimTarget < w.aim) w.aim = w.aim - 16.0f;
        else w.aim = w.aim + 16.0f;
        w.aim = WrapAngle(w.aim);
    } else {
        w.aim = o.aimTarget;
    }
}

void Physics(Object& o, uint32_t now) {
    auto& w = o.w;
    if (o.lastPass == 0) {              // 004c0002 .. 004c0010
        o.lastPass = now;
        return;
    }
    const int32_t elapsed = static_cast<int32_t>(now - o.lastPass);
    double e = static_cast<double>(elapsed);
    if (elapsed < 0) e += 4294967296.0;  // [0x63f4d8]
    float dt = static_cast<float>(e * 0.019999999552965164);
    if (dt > 3.0f) dt = 3.0f;           // 004c0049
    o.lastPass = now;
    // 004c01c2 .. 004c0228: heading += dt x rate, wrapped.
    w.heading = WrapAngle(dt * w.headingRate + w.heading);
    // 004c022c .. 004c025c: pitch += dt x rate, clamped to +-128.
    w.pitch = dt * w.pitchRate + w.pitch;
    if (w.pitch < -128.0f) w.pitch = -128.0f;
    if (w.pitch > 128.0f) w.pitch = 128.0f;
    float ty = 0.0f, tx = 0.0f, tz = 0.0f;
    if (w.word != 0.0f) {               // 004c0279 .. 004c0325
        ty = TableCos(w.heading) * w.word + ty;
        tx = TableSin(w.heading) * w.word + tx;
        if (w.aim != 0.0f) tz = TableSin(w.aim) * w.word;
    }
    // 004c04bf .. 004c04d6: 0.8 thrust, 0.625 damping; 0.85 when +0x62 == 0.
    const float scale = SpellMissileRules::kMissileThrustScale;
    float damp = SpellMissileRules::kMissileDamping;
    float dampZ = SpellMissileRules::kMissileDamping;
    if (w.damped == 0) damp = dampZ = SpellMissileRules::kMissileDampingNo62;
    if (ty != 0.0f) ty = ty * scale;
    if (tx != 0.0f) tx = tx * scale;
    if (tz != 0.0f) tz = tz * scale;
    if (w.vel[0] != 0.0f) w.vel[0] = damp * w.vel[0];
    if (w.vel[1] != 0.0f) w.vel[1] = damp * w.vel[1];
    if (w.vel[2] != 0.0f) w.vel[2] = dampZ * w.vel[2];
    for (float& v : w.vel)              // 004c07c1 .. 004c082d, [0x645e54]
        if (std::fabs(static_cast<double>(v)) < 1.1920928955078125e-07) v = 0.0f;
    w.vel[0] = ty + w.vel[0];
    w.vel[1] = tx + w.vel[1];
    w.vel[2] = tz + w.vel[2];
    // 004c0854 .. 004c08a5: the XY speed under 0.05 stops a +0x62 == 0 object.
    if (w.damped == 0) {
        const float speed = std::sqrt(w.vel[1] * w.vel[1] + w.vel[0] * w.vel[0]);
        if (speed < 0.05000000074505806f) w.vel[0] = w.vel[1] = w.vel[2] = 0.0f;
    }
    // 004c0993 .. 004c09b4: the move.
    w.pos[0] = dt * w.vel[0] + w.pos[0];
    w.pos[1] = dt * w.vel[1] + w.pos[1];
    w.pos[2] = dt * w.vel[2] + w.pos[2];
}

bool RemovedAfterPass(const Object& o, uint32_t frameNow) {
    return o.hit || frameNow > o.lifeEnd;
}

bool Pass(Object& o, uint32_t now, const Entity& target) {
    Home(o, now, target);
    Physics(o, now);
    return RemovedAfterPass(o, now);
}

RemovalFire RemovalFireFor(const Object& o) {
    if (o.w.type != kTypeSpell || !o.spawned) return RemovalFire::None;
    if (o.w.spellId == 0x36) return RemovalFire::AtPositionBack;          // 00477959
    if (o.w.spellId == SpellMissileRules::kSpell859) return RemovalFire::AtLocalPlayer;  // 00477962
    return RemovalFire::None;
}

void RemovalPosition(const Object& o, float out[3]) {
    for (int i = 0; i < 3; ++i) out[i] = o.w.pos[i] - o.w.vel[i];
}

}  // namespace SpellMissile
}  // namespace EQ
