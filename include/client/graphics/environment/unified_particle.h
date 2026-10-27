#pragma once

// Unified Particle System — Phase 1: Core data structures
// Point-sprite-based particle rendering for GLES2 (Mali 400 / embedded GPUs)
// Coexists with the existing billboard particle path (ambient/environmental effects)

#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>

#include "client/graphics/environment/particle_options.h"  // B995: ParticleFamily
#include "client/spell/spells_new_edd_colour.h"         // B1146: eddParticleAlpha

namespace EQT {
namespace Graphics {
namespace Environment {

// === Enums ===

enum class MotionType : uint8_t {
    LINEAR = 0,           // pos += vel * dt; vel += gravity * dt; vel *= (1 - drag*dt)
    CAMERA_RELATIVE = 1,  // Spawns/recycles within volume around camera (rain, snow)
    RADIAL_EXPAND = 2,    // Outward from center in XZ plane (spell impacts)
    ORBITAL = 3,          // Orbit center with vertical drift (cast glows, auras)
    BURST = 4,            // One-shot scatter (same physics as LINEAR)
};

enum class UnifiedBlendMode : uint8_t {
    ADDITIVE = 0,   // glBlendFunc(GL_ONE, GL_ONE) — fire, sparks, holy, lightning
    ALPHA = 1,      // glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) — smoke, snow
    // B1146: glBlendFunc(GL_SRC_ALPHA, GL_ONE) -- the official client's spell
    // additive blend (EQGraphicsDX9 0x1005f7c2). A renderer batch only: a
    // particle carries it as ADDITIVE + hasEddColour().
    ADDITIVE_SRC_ALPHA = 2,
};

enum class UnifiedRendererType : uint8_t {
    POINT_SPRITE = 0,   // GL_POINTS with gl_PointSize + gl_PointCoord
    BATCHED_QUAD = 1,   // Future: indexed quads for larger/shaped particles
};

enum class SpawnShape : uint8_t {
    POINT = 0,  // Spawn at emitter position
    BOX = 1,    // Random within box half-extents
    SPHERE = 2, // Random within sphere of radius spawnExtents.x
    RING = 3,   // Random angle at fixed radius spawnExtents.x
    DISC = 4,   // B1131: uniform over a flat disc of radius spawnExtents.x, +-spawnExtents.y thick
};

// === Particle Structure ===

// Single unified particle instance (B1146 added the three EDD alpha words)
// Stored contiguously in a fixed pool for cache-friendly linear scan
struct UnifiedParticle {
    // Core state — 48 bytes
    glm::vec3 position{0.0f};       // World position (Irrlicht Y-up)
    glm::vec3 velocity{0.0f};       // Units per second
    glm::vec4 color{1.0f};          // Current RGBA
    float size = 0.0f;              // Current point size (screen pixels before perspective)
    float age = 0.0f;               // Seconds since spawn
    float maxLifetime = 1.0f;       // Seconds until death

    // Spawn parameters for interpolation — 24 bytes
    glm::vec4 colorStart{1.0f};     // Color at birth
    glm::vec4 colorEnd{0.0f};       // Color at death
    float sizeStart = 1.0f;         // Size at birth
    float sizeEnd = 2.0f;           // Size at death

    // Motion parameters — 16 bytes
    float phase = 0.0f;             // Random phase for oscillation
    float angularVelocity = 0.0f;   // For orbital/helical motion
    float radius = 0.0f;            // Current orbital radius
    float drag = 0.0f;              // Velocity damping per second

    // Metadata — 8 bytes
    uint16_t emitterID = 0;         // Which emitter owns this particle
    uint16_t textureIndex = 0;      // Region within atlas (UV offset selector)
    uint8_t flags = 0;              // Bit 0: alive, Bit 1: blend mode (0=additive, 1=alpha), Bits 2-3: family (B995), Bit 4: EDD colour (B1146), Bit 5: EDD sprite (B1149), Bit 6: EDD motion (B1151)
    MotionType motionType = MotionType::LINEAR;
    uint16_t padding = 0;

    // UV rotation for rain streaks (radians, 0 = no rotation)
    float rotation = 0.0f;

    // B1146 (#466): the client's alpha for a particle drawn with its
    // spellsnew.edd colour words (hasEddColour()): min(fadeIn * fadeOut,
    // peak), see EQ::eddParticleAlpha(). Unused otherwise.
    float eddFadeIn = 0.0f;         // +0x90, s
    float eddFadeOut = 0.0f;        // +0x94, s
    float eddPeakOpacity = 1.0f;    // +0xA4

    // B1149 (#469): a spellsnew.edd sprite (hasEddSprite()) is a
    // screen-facing quad `size` wide and `height` tall in WORLD units at the
    // particle (EQGraphicsDX9 0x1006335a..: half-width w * 0.5 / depth times
    // the camera's projection scale), not a point sprite; its base width is
    // sizeStart (== sizeEnd), its base height eddBaseHeight, and the client's
    // grow-in / shrink-out times scale both over the particle's life
    // (eddSpriteSize()).
    float height = 0.0f;            // current height, world units
    float eddBaseHeight = 0.0f;     // h drawn at spawn (+0x17C..+0x180)
    float eddGrowIn = 0.0f;         // +0x98, s
    float eddShrinkOut = 0.0f;      // +0x9C, s
    float eddHeightShrink = 0.0f;   // +0x190, s
    float eddWidthShrink = 0.0f;    // +0x194, s

    // B1150 (#470): the emitter's +0xD0 view-depth bias. The renderer subtracts
    // it from the particle's view depth before the near/far cull and the
    // perspective divide (EQGraphicsDX9: `fsub [edi+0xd0]` at 0x10063069 in the
    // main projection and 0x10063198 in the +0x70 second pass, each followed by
    // the near/far compares and the 1.0/depth divide).
    float eddDepthBias = 0.0f;

    // B1151 (#471): the record's motion words (hasEddMotion()). Emitter-frame
    // state: the height coordinate along eddBasis1 (axis 1), the radius and
    // swirl angle (512ths of a turn) in the eddBasis2/eddBasis3 plane, and
    // the axis-2/axis-3 offsets the per-axis speeds integrate. The velocities
    // are the client's per-particle draws (0x100620fd..0x100621a1) plus one
    // frame of their accels (0x100626f9..0x100628f6); the sim integrates
    // them (SimulationWorker::updateEddMotion, addresses in refactor/B1151/
    // README.md). eddGy/eddDx are the world gravity (-z) and +X drift
    // integrals. Non-motion particles keep every one of these at its default.
    glm::vec3 eddBasis1{0.0f, 1.0f, 0.0f};  // the POST-tilt world axes
    glm::vec3 eddBasis2{1.0f, 0.0f, 0.0f};
    glm::vec3 eddBasis3{0.0f, 0.0f, 1.0f};
    glm::vec3 eddBase{0.0f};        // the emitter origin (incl. attachOffset) at spawn
    glm::vec3 eddFrameOffset{0.0f}; // the shape point's world offset (follow: re-expressed from the emitter now)
    float eddH = 0.0f;              // axis-1 coordinate
    float eddR = 0.0f;              // radial coordinate
    float eddTheta512 = 0.0f;       // swirl angle, 512ths of a turn
    float eddO2 = 0.0f, eddO3 = 0.0f;
    float eddV1 = 0.0f, eddV2 = 0.0f, eddV3 = 0.0f, eddVr = 0.0f, eddSwirl = 0.0f;
    float eddGv = 0.0f, eddGy = 0.0f, eddDx = 0.0f;
    float eddNR = 1.0f, eddNH = 0.0f;  // the spawn normal in (r, h) (the +0x110 rule)
    float eddSpin = 0.0f;         // 512ths/s, drawn from +0x138..+0x188
    float eddStart512 = 0.0f;     // the start angle, 512ths (+0x174 rule)
    int eddAlignWord = 0;         // +0x178 (0 = no alignment)
    int eddFrameCount = 0;        // +0x130 (0/1 = the sheet's own frame rule)
    float eddFrameRate = 0.0f;    // +0x134
    uint8_t eddSpriteMode = 0;    // +0x70: 0 billboard, 1 streak, 2 world-oriented
    uint8_t eddNoDepthWrite = 0;  // +0x64 (the batch's depth state; see the README)
    uint8_t eddDrawNegativeRadius = 0;  // +0x198 == 1: r < 0 still drawn

    bool isAlive() const { return (flags & 0x01) != 0; }
    void setAlive(bool alive) { if (alive) flags |= 0x01; else flags &= ~0x01; }

    UnifiedBlendMode getBlendMode() const {
        return (flags & 0x02) ? UnifiedBlendMode::ALPHA : UnifiedBlendMode::ADDITIVE;
    }
    void setBlendMode(UnifiedBlendMode mode) {
        if (mode == UnifiedBlendMode::ALPHA) flags |= 0x02; else flags &= ~0x02;
    }

    // B995: which Options particle page governs this particle (opacity,
    // near clip at draw; density was applied when it spawned).
    ParticleFamily getFamily() const { return static_cast<ParticleFamily>((flags >> 2) & 0x03); }
    void setFamily(ParticleFamily f) {
        flags = static_cast<uint8_t>((flags & ~0x0C) | ((static_cast<uint8_t>(f) & 0x03) << 2));
    }

    // B1146: the particle is on the CLIENT's alpha (a region with
    // RegionColour::clientAlpha) -- its alpha is the fade x peak of its
    // spellsnew.edd emitter, and an additive one blends SRC_ALPHA, ONE (the
    // client's DESTBLEND ONE over SRCBLEND SRCALPHA), not the style's ONE, ONE.
    bool hasEddColour() const { return (flags & 0x10) != 0; }
    void setEddColour(bool on) { if (on) flags |= 0x10; else flags &= ~0x10; }
    float eddAlpha() const {
        return EQ::eddParticleAlpha(age, maxLifetime - age, eddFadeIn, eddFadeOut, eddPeakOpacity);
    }

    // B1149: the particle is a spellsnew.edd sprite (a world-unit quad).
    bool hasEddSprite() const { return (flags & 0x20) != 0; }
    void setEddSprite(bool on) { if (on) flags |= 0x20; else flags &= ~0x20; }
    // The client's size over life (0x1006326c..0x100632e9): w and h times
    // age / grow-in while younger than it and remaining / shrink-out while
    // less than it is left; the height and width shrink-outs scale their own
    // side only. A time <= 0 never scales.
    void eddSpriteSize() {
        const float remaining = maxLifetime - age;
        float k = 1.0f;
        if (eddGrowIn > 0.0f && age < eddGrowIn) k *= age / eddGrowIn;
        if (eddShrinkOut > 0.0f && remaining < eddShrinkOut) k *= remaining / eddShrinkOut;
        float kw = k, kh = k;
        if (eddHeightShrink > 0.0f && remaining < eddHeightShrink) kh *= remaining / eddHeightShrink;
        if (eddWidthShrink > 0.0f && remaining < eddWidthShrink) kw *= remaining / eddWidthShrink;
        size = sizeStart * (kw < 0.0f ? 0.0f : kw);
        height = eddBaseHeight * (kh < 0.0f ? 0.0f : kh);
    }

    // Get normalized age (0 = just spawned, 1 = about to die)
    float getNormalizedAge() const {
        return maxLifetime > 0.0f ? age / maxLifetime : 1.0f;
    }

    // B1151 (#471): the particle is on the record's motion words -- its
    // position is re-derived every frame from the emitter-frame state above
    // (SimulationWorker::updateEddMotion), not integrated from velocity.
    bool hasEddMotion() const { return (flags & 0x40) != 0; }
    void setEddMotion(bool on) { if (on) flags |= 0x40; else flags &= ~0x40; }

    // +0x198 (TSV row, draw site 0x10062e5c): r < 0 (inward radial motion
    // past the axis) is NOT drawn unless the word is 1.
    bool eddDraws() const {
        return !hasEddMotion() || eddDrawNegativeRadius != 0 || eddR >= 0.0f;
    }

    // +0x130/+0x134 (TRACE.md (c), 0x10063b7a..0x10063d03): frame =
    // ftol(age * fps) % N; 2..4 = a 2x2 grid, 5..8 four columns x two rows,
    // 9..16 a 4x4 grid (row-major), > 16 the whole texture; N <= 1 frame 0.
    // The sheet packs a sprite's frames into the first N of its 16 cells, so
    // the frame IS the cell offset.
    int eddFrame() const {
        if (eddFrameCount <= 1) return 0;
        if (eddFrameCount > 16) return 0;  // the whole texture
        int f = static_cast<int>(std::floor(age * eddFrameRate));
        f %= eddFrameCount;
        return f < 0 ? f + eddFrameCount : f;
    }

    // The spin angle, 512ths of a turn (the +0x138 rule, draws 0x100634c1 /
    // 0x100638e8): age * spin + start. The +0x178 alignment is added by the
    // renderer (it is a SCREEN angle, camera-dependent).
    float eddSpinAngle512() const { return age * eddSpin + eddStart512; }
};

// === Emitter Configuration ===

// Data-driven emitter definition. Same structure serves fire, weather, and spells.
struct EmitterConfig {
    // Spawning
    float spawnRate = 10.0f;         // Particles per second (0 = burst mode)
    int burstCount = 0;              // For burst mode: how many at once
    float emitterLifetime = 0.0f;    // 0 = permanent (torches), >0 = temporary (spells)
    // B1121: a two-phase emission for a landing that outlasts its peak. spawnRate
    // runs while emitterAge < spawnWindow (0 = the whole emitterLifetime);
    // sustainRate runs from emitterAge >= sustainFrom until emitterLifetime.
    float spawnWindow = 0.0f;
    float sustainRate = 0.0f;
    float sustainFrom = 0.0f;

    // Spawn volume
    SpawnShape spawnShape = SpawnShape::POINT;
    glm::vec3 spawnExtents{0.0f};    // Half-extents for BOX

    // Initial velocity
    glm::vec3 velocityBase{0.0f, 2.5f, 0.0f};   // Base velocity (Irrlicht Y-up)
    glm::vec3 velocitySpread{0.0f};               // Random spread per-axis

    // Forces
    glm::vec3 gravity{0.0f};        // Acceleration (Y-up; positive Y = up)
    float drag = 0.0f;              // Velocity damping per second (0=none, 1=full stop)
    float windResponse = 0.0f;      // How much global wind affects this particle (0-1)

    // Motion pattern
    MotionType motionType = MotionType::LINEAR;

    // Appearance
    glm::vec4 colorStart{1.0f, 1.0f, 0.5f, 1.0f};  // RGBA at birth
    glm::vec4 colorEnd{1.0f, 0.2f, 0.0f, 0.0f};    // RGBA at death
    float sizeStartMin = 1.5f;      // Min size at birth (randomized per particle)
    float sizeStartMax = 2.5f;      // Max size at birth
    float sizeEndMin = 3.0f;        // Min size at death
    float sizeEndMax = 4.0f;        // Max size at death
    float lifetimeMin = 0.3f;       // Min seconds
    float lifetimeMax = 0.6f;       // Max seconds

    // Rendering
    UnifiedRendererType rendererType = UnifiedRendererType::POINT_SPRITE;
    UnifiedBlendMode blendMode = UnifiedBlendMode::ADDITIVE;
    uint8_t textureRegions[4] = {0, 0, 0, 0};  // Possible atlas tile indices
    uint8_t textureRegionCount = 1;              // How many regions to pick from

    // B1146 (#466): the spellsnew.edd colour words of the emitter that names
    // textureRegions[k] (SpellPresets::applyStageEmitterColours()). A region
    // whose `active` is false keeps the config's colorStart/colorEnd and
    // blendMode -- every non-spellsnew emitter (B1149: a spellsnew.eff
    // emitter's regions are all active and on the client's alpha).
    struct RegionColour {
        bool active = false;
        // The client's fade x peak alpha and +0x68 blend as well as its RGB
        // (UnifiedParticle::hasEddColour()); false keeps the style's alpha
        // and blend (B1146's RGB-only setting; B1149 ships true).
        bool clientAlpha = false;
        bool additive = true;            // +0x68
        glm::vec3 start{1.0f};           // +0xD4..+0xDC / 255
        glm::vec3 end{1.0f};             // +0xE0..+0xE8 / 255
        float fadeIn = 0.0f;             // +0x90, s
        float fadeOut = 0.0f;            // +0x94, s
        float peakOpacity = 1.0f;        // +0xA4
    };
    RegionColour regionColours[4];

    // B1149 (#469): a spellsnew.edd emitter's sprite (SpellPresets::
    // applySlotEmitter()): each particle draws its height u in [hMin, hMax]
    // and its width in [wMin, wMax] (from u again when sameRandom), times
    // `scale` (+0x16C x the attach scale when +0x6C is set), and is drawn as
    // a world-unit quad (UnifiedParticle::hasEddSprite()). Inactive: the
    // style's point-sprite sizes above.
    struct EddSprite {
        bool active = false;
        float widthMin = 0.0f, widthMax = 0.0f;    // +0xCC, +0x184
        float heightMin = 0.0f, heightMax = 0.0f;  // +0x17C, +0x180
        float scale = 1.0f;
        bool sameRandom = true;                    // +0x18C
        float growIn = 0.0f, shrinkOut = 0.0f;     // +0x98, +0x9C
        float heightShrink = 0.0f, widthShrink = 0.0f;  // +0x190, +0x194
    };
    EddSprite eddSprite;

    // B1150 (#470): a spellsnew.edd emitter's spawn geometry
    // (SpellPresets::applySlotEmitter()): every spell with a spellsnew.eff
    // field-145 record spawns by the record's +0xA8 shape (the ten official
    // cases, spell_spawn_shape.h, each with its Render address); the config's
    // spawnShape / spawnExtents are NOT read for such an emitter. Inactive:
    // those (plus fire/weather emitters, which never have a record).
    struct EddShape {
        bool active = false;
        int shape = 0;                          // +0xA8: 0..9
        float dimX = 0.0f, dimY = 0.0f, dimZ = 0.0f;   // +0xAC / +0xB0 / +0xB4
        float offX = 0.0f, offY = 0.0f, offZ = 0.0f;   // +0xB8 / +0xBC / +0xC0
        float tiltA = 0.0f, tiltB = 0.0f;       // +0xC4 / +0xC8, EQ 512ths
        float scale = 1.0f;                     // +0x19C: attach scale (1.0 when 0)
        float depthBias = 0.0f;                 // +0xD0: view depth bias (renderer)
    };
    EddShape eddShape;

    // B1151 (#471): a spellsnew.edd emitter's motion words
    // (SpellPresets::applySlotEmitter()): the per-axis / radial / swirl /
    // gravity / drift words (+0xEC..+0x12C), the spin range (+0x138/+0x188,
    // +0x174 random start), the spin alignment (+0x178), the frame rule
    // (+0x130/+0x134) and the flags (+0x60 orient, +0x64 depth write off,
    // +0x70 sprite mode, +0x74 follow, +0x198 draw at r < 0). `scale` is s,
    // the attach scale every word above multiplies by (the +0x6C read path,
    // B1164: the client's actor scale, spell_fx_attach_scale.h). Inactive:
    // config/preset emitters (fire, weather, non-record spells), which keep
    // their existing paths.
    struct EddMotion {
        bool active = false;
        bool orientToAttach = false;   // +0x60
        bool noDepthWrite = false;     // +0x64
        int spriteMode = 0;            // +0x70: 0 billboard, 1 streak, 2 world-oriented
        bool follow = false;           // +0x74
        float upMin = 0.0f, upMax = 0.0f, upAccel = 0.0f;          // +0xEC/+0xF0/+0xF4
        float axis2Min = 0.0f, axis2Max = 0.0f, axis2Accel = 0.0f; // +0xF8/+0xFC/+0x100
        float axis3Min = 0.0f, axis3Max = 0.0f, axis3Accel = 0.0f; // +0x104/+0x108/+0x10C
        float radialMin = 0.0f, radialMax = 0.0f, radialAccel = 0.0f;  // +0x110/+0x114/+0x118
        float swirlMin = 0.0f, swirlMax = 0.0f, swirlAccel = 0.0f;    // +0x11C/+0x120/+0x124, 512ths/s
        float gravity = 0.0f;          // +0x128
        float driftX = 0.0f;           // +0x12C
        bool randomStartSpin = false;  // +0x174
        float spinMin = 0.0f, spinMax = 0.0f;  // +0x138/+0x188, 512ths/s
        int spinAlign = 0;             // +0x178
        int frameCount = 1;            // +0x130
        float frameRate = 0.0f;        // +0x134
        bool drawNegativeRadius = false;  // +0x198 == 1
        float scale = 1.0f;            // s
    };
    EddMotion eddMotion;

    // B1149 (#469): the client's emission ticks (CParticleSystem::Render
    // 0x10060ea4..0x10060ffd): tick n falls at emitter age n / ticksPerSecond
    // and each new tick emits tickParticles (the slot's +0x84 share carried
    // by this emitter; fractions accumulate); the burst (burstCount) is the
    // first frame's. 0 = the rate path (spawnRate) above.
    float ticksPerSecond = 0.0f;
    float tickParticles = 0.0f;
    // B1165 (#484): the Spell page's density was applied to this emitter's
    // counts when it was created (SpellPresets::applySlotEmitter on a
    // SpellFxSlotTimingAtDensity timing, CreateEmitter 0x1005a430), so the
    // worker must not scale its spawns by the family density again.
    bool densityAtCreate = false;

    // Spell motion parameters (ORBITAL / RADIAL_EXPAND)
    float orbitalRadius = 1.0f;           // ORBITAL: orbit radius
    float orbitalAngularVelocity = 4.0f;  // ORBITAL: rad/s
    float expandSpeed = 3.0f;             // RADIAL_EXPAND: outward speed multiplier

    // Weather-specific (CAMERA_RELATIVE motion)
    int targetCount = 0;                  // 0 = use spawnRate; >0 = maintain count via recycling
    glm::vec3 spawnVolumeHalfExtents{0};  // Camera-relative spawn box half-extents
    float spawnVolumeTopBias = 0.8f;      // Fraction of spawns placed at top of volume
    float driftFrequency = 0.0f;          // Snow lateral sine-wave frequency
    float driftAmplitude = 0.0f;          // Snow lateral drift displacement
    float twinkleSpeed = 0.0f;            // Snow alpha modulation speed (0=disabled)
    float sizeSpeedCorrelation = 0.0f;    // Large particles fall slower (0-1)
};

// === Active Emitter ===

// Runtime state for a spawning emitter
struct ActiveEmitter {
    EmitterConfig config;
    glm::vec3 position{0.0f};       // World position (Irrlicht Y-up)
    float spawnAccumulator = 0.0f;   // Fractional particle accumulation
    float emitterAge = 0.0f;         // Time since activation (for lifetime check)
    bool active = true;
    uint16_t emitterID = 0;
    float lightRadius = 0.0f;        // Source light radius (for classification)
    float transitionAlpha = 1.0f;    // 0→1 ramp for smooth weather onset
    float transitionRate = 0.5f;     // Per-second ramp speed
    // B995: the Options page whose knobs govern this emitter's particles.
    // Fire and weather are Environment; spell effects set Spell.
    ParticleFamily family = ParticleFamily::Environment;

    // Entity attachment (spell effects)
    uint16_t attachEntityID = 0;     // Entity to follow (0 = static)
    glm::vec3 attachOffset{0.0f};    // Offset from entity position (Irrlicht Y-up)
    bool isBurstSpawned = false;     // BURST: prevent re-spawning after initial burst
    int lastTick = 0;                // B1149: the last emission tick emitted (config.ticksPerSecond)

    // Dynamic velocity direction (spray effects — updated each frame via callback)
    bool useDynamicDirection = false;
    glm::vec3 dynamicDirection{0.0f, 0.0f, 1.0f};  // Normalized direction for spray

    // Projectile path (lerps from caster to target over travelDuration)
    bool isProjectile = false;
    glm::vec3 projectileStartPos{0.0f};
    glm::vec3 projectileTargetPos{0.0f};
    float travelDuration = 0.0f;
    float travelElapsed = 0.0f;
    uint16_t targetEntityID = 0;
};

// === Fire Emitter Presets ===

namespace FirePresets {
    EmitterConfig Torch();
    EmitterConfig CampfireFlame();
    EmitterConfig CampfireEmber();
}

// === Weather Emitter Presets ===

namespace WeatherPresets {
    EmitterConfig Rain(uint8_t intensity = 5);
    EmitterConfig Snow(uint8_t intensity = 5);
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
