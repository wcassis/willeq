#include "client/graphics/simulation_worker.h"
#include "client/graphics/portal_system.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/environment/unified_particle.h"
#include "client/graphics/environment/spell_spawn_shape.h"
#include "client/graphics/environment/spell_particle_types.h"
#include "client/hc_map.h"
#include "common/logging.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace EQT {
namespace Graphics {

// ============================================================================
// Particle System
// ============================================================================

using namespace Environment;

void SimulationWorker::computeParticles(const SimulationInput& input, SimulationOutput& output) {
    output.particleOutput.valid = false;

    // Skip if unified renderer not initialized on main thread
    if (!input.particleInput.unifiedRendererInitialized) return;

    // Lazy-init pool on worker thread
    if (!particlePoolInitialized_) {
        int poolSize = input.particleInput.poolSize;
        if (poolSize <= 0) poolSize = 1024;
        particlePool_.resize(poolSize);
        particleFreeList_.resize(poolSize);
        for (int i = 0; i < poolSize; ++i) {
            particleFreeList_[i] = static_cast<uint16_t>(i);
            particlePool_[i].setAlive(false);
        }
        particleActiveCount_ = 0;
        particleRng_.seed(input.particleInput.rngSeed != 0 ? input.particleInput.rngSeed
                                                         : std::random_device{}());
        particlePoolInitialized_ = true;
        LOG_INFO(MOD_GRAPHICS, "SimulationWorker: Particle pool initialized (size={})", poolSize);
    }

    const auto& pi = input.particleInput;
    float deltaTime = pi.deltaTime;
    if (deltaTime <= 0.0f || deltaTime > 1.0f) {
        // B1106 (#426): this guard protects the PHYSICS, not the commands.
        // A frame delta outside (0, 1] s (a zone-in hitch, or the
        // surfaceless-dump rig whose own frames ran 1.4-1.5 s) must not be
        // stepped through the physics as-is -- but the command queue it
        // carries must still be EXECUTED: it is drained once per frame
        // (renderer_sim_bridge.cpp:447), so returning here discards it
        // forever. Measured in B1104's capture: the cast's CreateSpellEffect
        // was enqueued (GRAPHICS DEBUG) and never created (no SimWorker
        // receipt), on a tour whose frames all ran > 1.0 s. The command loop
        // below is idempotent and position-only (no time); the phases that
        // advance with time use a clamped delta, so a 14 s hitch ages an
        // emitter by at most 1 s instead of by 14.
        deltaTime = std::min(std::max(deltaTime, 0.0f), 1.0f);
    }

    // ---- Command processing ----
    for (const auto& cmd : pi.commands) {
        switch (cmd.type) {
            case ParticleCommand::CreateFireEmitters: {
                // Clear existing emitters first
                for (auto& p : particlePool_) {
                    if (p.isAlive()) {
                        p.setAlive(false);
                    }
                }
                particleFreeList_.resize(particlePool_.size());
                for (uint16_t i = 0; i < static_cast<uint16_t>(particlePool_.size()); ++i) {
                    particleFreeList_[i] = i;
                }
                particleActiveCount_ = 0;
                particleEmitters_.clear();
                particleWeatherEmitterID_ = 0;
                particleSpellEffects_.clear();

                const float campfireRadiusThreshold = 150.0f;
                for (size_t i = 0; i < cmd.firePositions.size(); ++i) {
                    const glm::vec3& eqPos = cmd.firePositions[i];
                    float radius = (i < cmd.fireRadii.size()) ? cmd.fireRadii[i] : 120.0f;
                    glm::vec3 irrPos(eqPos.x, eqPos.z, eqPos.y);

                    if (radius >= campfireRadiusThreshold) {
                        {
                            ActiveEmitter ae;
                            ae.config = FirePresets::CampfireFlame();
                            ae.position = irrPos;
                            ae.emitterID = particleNextEmitterID_++;
                            ae.lightRadius = radius;
                            particleEmitters_[ae.emitterID] = ae;
                        }
                        {
                            ActiveEmitter ae;
                            ae.config = FirePresets::CampfireEmber();
                            ae.position = irrPos;
                            ae.emitterID = particleNextEmitterID_++;
                            ae.lightRadius = radius;
                            particleEmitters_[ae.emitterID] = ae;
                        }
                    } else {
                        ActiveEmitter ae;
                        ae.config = FirePresets::Torch();
                        ae.position = irrPos;
                        ae.emitterID = particleNextEmitterID_++;
                        ae.lightRadius = radius;
                        particleEmitters_[ae.emitterID] = ae;
                    }
                }
                LOG_INFO(MOD_GRAPHICS, "SimWorker: Created {} fire emitters for {} sources",
                         particleEmitters_.size(), cmd.firePositions.size());
                break;
            }
            case ParticleCommand::ClearUnifiedEmitters: {
                for (auto& p : particlePool_) {
                    if (p.isAlive()) p.setAlive(false);
                }
                particleFreeList_.resize(particlePool_.size());
                for (uint16_t i = 0; i < static_cast<uint16_t>(particlePool_.size()); ++i) {
                    particleFreeList_[i] = i;
                }
                particleActiveCount_ = 0;
                particleEmitters_.clear();
                particleWeatherEmitterID_ = 0;
                particleSpellEffects_.clear();
                particleNextSpellEffectID_ = 1;
                break;
            }
            case ParticleCommand::ActivateWeather: {
                // Kill existing weather
                if (particleWeatherEmitterID_ != 0) {
                    auto it = particleEmitters_.find(particleWeatherEmitterID_);
                    if (it != particleEmitters_.end()) {
                        it->second.active = false;
                        for (auto& p : particlePool_) {
                            if (p.isAlive() && p.emitterID == particleWeatherEmitterID_) {
                                int idx = static_cast<int>(&p - particlePool_.data());
                                freeParticle(idx);
                            }
                        }
                        particleEmitters_.erase(it);
                    }
                    particleWeatherEmitterID_ = 0;
                }
                EmitterConfig cfg;
                if (cmd.weatherType == 1) {
                    cfg = WeatherPresets::Rain(cmd.weatherIntensity);
                } else if (cmd.weatherType == 2) {
                    cfg = WeatherPresets::Snow(cmd.weatherIntensity);
                } else break;

                ActiveEmitter ae;
                ae.config = cfg;
                ae.position = glm::vec3(0.0f);
                ae.emitterID = particleNextEmitterID_++;
                ae.transitionAlpha = 0.0f;
                ae.transitionRate = 0.5f;
                particleEmitters_[ae.emitterID] = ae;
                particleWeatherEmitterID_ = ae.emitterID;
                LOG_INFO(MOD_GRAPHICS, "SimWorker: Activated weather type={} intensity={}", cmd.weatherType, cmd.weatherIntensity);
                break;
            }
            case ParticleCommand::DeactivateWeather: {
                if (particleWeatherEmitterID_ != 0) {
                    auto it = particleEmitters_.find(particleWeatherEmitterID_);
                    if (it != particleEmitters_.end()) {
                        it->second.active = false;
                        for (auto& p : particlePool_) {
                            if (p.isAlive() && p.emitterID == particleWeatherEmitterID_) {
                                int idx = static_cast<int>(&p - particlePool_.data());
                                freeParticle(idx);
                            }
                        }
                        particleEmitters_.erase(it);
                    }
                    particleWeatherEmitterID_ = 0;
                }
                break;
            }
            case ParticleCommand::CreateSpellEffect: {
                SpellEffectInstance inst;
                inst.effectID = cmd.preAssignedEffectID;
                inst.spellID = 0;
                inst.casterEntityID = cmd.casterID;
                inst.targetEntityID = cmd.targetID;
                inst.age = 0.0f;
                inst.maxDuration = cmd.duration;
                inst.def = cmd.spellDef;
                inst.useDynamicDirection = cmd.useDynamicDir;
                if (cmd.projectileTravelDuration > 0.0f) {
                    inst.projectileTravelDuration = cmd.projectileTravelDuration;
                }
                for (int i = 0; i < static_cast<int>(cmd.spellDef.emitters.size()); ++i) {
                    SpellEffectInstance::EmitterState es;
                    es.defIndex = i;
                    es.activeEmitterID = 0;
                    es.triggered = false;
                    inst.emitterStates.push_back(es);
                }
                // Seed initial entity positions into the tracking map
                for (const auto& [eid, epos] : cmd.initialEntityPositions) {
                    // No need to store separately — pi.entityPositions should have them
                }
                particleSpellEffects_.push_back(std::move(inst));
                if (particleNextSpellEffectID_ <= cmd.preAssignedEffectID) {
                    particleNextSpellEffectID_ = cmd.preAssignedEffectID + 1;
                }
                LOG_DEBUG(MOD_GRAPHICS, "SimWorker: Created spell effect '{}' id={} caster={} target={}",
                          cmd.spellDef.name, cmd.preAssignedEffectID, cmd.casterID, cmd.targetID);
                break;
            }
            // B1139: the CreateSpellEffectAtPos arm (and SpellAttach::GROUND_TARGET,
            // the attach it rewrote CASTER emitters to) was deleted: its producer
            // ParticleManager::createSpellEffectAtPosition went in B1138.
            // T397 Leg B (board B220): the RemoveSpellEffect (by effect id) arm
            // was deleted here along with its command and its producer. It
            // matched cmd.effectID, which no producer ever set. Recover with:
            // git show 8382268d:src/client/graphics/sim_particles.cpp
            case ParticleCommand::RemoveSpellEffectsEntity: {
                for (auto it = particleSpellEffects_.begin(); it != particleSpellEffects_.end(); ) {
                    // T426 (board B400): an empty styleFilter keeps the old
                    // "every style on this entity" behaviour; a non-empty one
                    // removes only the named styles, so removeCastGlow() can
                    // take the glow down without taking the impact and the
                    // spell-complete burst with it.
                    const bool styleMatches =
                        cmd.styleFilter.empty() ||
                        std::find(cmd.styleFilter.begin(), cmd.styleFilter.end(),
                                  it->def.name) != cmd.styleFilter.end();
                    if (styleMatches &&
                        (it->casterEntityID == cmd.entityID || it->targetEntityID == cmd.entityID)) {
                        for (auto& es : it->emitterStates) {
                            if (es.activeEmitterID != 0) {
                                for (auto& p : particlePool_) {
                                    if (p.isAlive() && p.emitterID == es.activeEmitterID) {
                                        int idx = static_cast<int>(&p - particlePool_.data());
                                        freeParticle(idx);
                                    }
                                }
                                particleEmitters_.erase(es.activeEmitterID);
                            }
                        }
                        it = particleSpellEffects_.erase(it);
                    } else {
                        ++it;
                    }
                }
                break;
            }
            case ParticleCommand::ReleaseSpellEffectsEntity: {
                // B1122: same match as the Remove arm, but the effect's
                // maxDuration (the lifecycle phase's emission cut-off) moves to
                // `duration` seconds from now and nothing is freed: the effect
                // is erased once its last sprite dies.
                for (auto& fx : particleSpellEffects_) {
                    const bool styleMatches =
                        cmd.styleFilter.empty() ||
                        std::find(cmd.styleFilter.begin(), cmd.styleFilter.end(),
                                  fx.def.name) != cmd.styleFilter.end();
                    if (styleMatches &&
                        (fx.casterEntityID == cmd.entityID || fx.targetEntityID == cmd.entityID)) {
                        fx.maxDuration = fx.age + std::max(cmd.duration, 1e-4f);
                    }
                }
                break;
            }
            case ParticleCommand::ClearAllSpellEffects: {
                for (auto& effect : particleSpellEffects_) {
                    for (auto& es : effect.emitterStates) {
                        if (es.activeEmitterID != 0) {
                            for (auto& p : particlePool_) {
                                if (p.isAlive() && p.emitterID == es.activeEmitterID) {
                                    int idx = static_cast<int>(&p - particlePool_.data());
                                    freeParticle(idx);
                                }
                            }
                            particleEmitters_.erase(es.activeEmitterID);
                        }
                    }
                }
                particleSpellEffects_.clear();
                break;
            }
            case ParticleCommand::ToggleFire: {
                particleFireEnabled_ = !particleFireEnabled_;
                break;
            }
            case ParticleCommand::SetZoneEnter: {
                // Reset state for new zone
                break;
            }
            case ParticleCommand::ZoneLeave: {
                // Clear everything
                for (auto& p : particlePool_) {
                    if (p.isAlive()) p.setAlive(false);
                }
                particleFreeList_.resize(particlePool_.size());
                for (uint16_t i = 0; i < static_cast<uint16_t>(particlePool_.size()); ++i) {
                    particleFreeList_[i] = i;
                }
                particleActiveCount_ = 0;
                particleEmitters_.clear();
                particleWeatherEmitterID_ = 0;
                particleSpellEffects_.clear();
                particleNextSpellEffectID_ = 1;
                break;
            }
        }
    }

    // ---- Spawn phase ----
    for (auto& [id, emitter] : particleEmitters_) {
        if (!emitter.active) continue;

        bool isWeather = (emitter.config.motionType == MotionType::CAMERA_RELATIVE);
        if (!isWeather && !particleFireEnabled_) continue;

        // T395 (board B204): refresh an entity-attached emitter's position
        // BEFORE the lifetime check, the frustum test and the burst.
        //
        // The lifecycle phase below also does this, but it runs AFTER this
        // one, and pi.entityPositions is always one frame behind the effect
        // that asked for it (the renderer resolves only what the previous
        // frame's positionRequestEntities named). So an emitter attached to a
        // target was created at (0,0,0) and was still at (0,0,0) on its first
        // spawn frame: with culling off it fired its whole one-shot burst at
        // the world origin and latched isBurstSpawned -- MEASURED at 1,020
        // units from the target in tests/test_spell_impact_visual.cpp -- and
        // with culling on (the shipped configuration) it was culled there
        // instead, losing a frame out of the impact preset's 0.1 s emitter
        // lifetime. Projectiles keep their own path below; this is only the
        // attached case.
        if (!isWeather && !emitter.isProjectile && emitter.attachEntityID != 0) {
            auto posIt = pi.entityPositions.find(emitter.attachEntityID);
            if (posIt != pi.entityPositions.end()) {
                emitter.position = posIt->second;
            }
        }

        // Check emitter lifetime
        if (emitter.config.emitterLifetime > 0.0f) {
            emitter.emitterAge += deltaTime;
            if (emitter.emitterAge >= emitter.config.emitterLifetime) {
                emitter.active = false;
                continue;
            }
        }

        // Ramp transition alpha for weather
        if (isWeather && emitter.transitionAlpha < 1.0f) {
            emitter.transitionAlpha += emitter.transitionRate * deltaTime;
            if (emitter.transitionAlpha > 1.0f) emitter.transitionAlpha = 1.0f;
        }

        if (isWeather) {
            // CAMERA_RELATIVE: target-count spawning
            emitter.position = pi.cameraPos;

            int aliveCount = 0;
            for (const auto& p : particlePool_) {
                if (p.isAlive() && p.emitterID == emitter.emitterID) {
                    aliveCount++;
                }
            }

            // B995: the Env page's density scales the volume's population.
            const float density = pi.density[static_cast<size_t>(emitter.family) % 3];
            int effectiveTarget = static_cast<int>(emitter.config.targetCount * emitter.transitionAlpha *
                                                   std::min(1.0f, std::max(0.0f, density)));
            int deficit = effectiveTarget - aliveCount;
            for (int s = 0; s < deficit; ++s) {
                spawnWeatherParticle(emitter.config, emitter.emitterID, pi.cameraPos,
                                     emitter.transitionAlpha, pi.windDirection, pi.windStrength);
            }
        } else {
            // Non-weather: frustum cull using input frustum planes
            if (input.frustumValid) {
                // Convert emitter position (Irrlicht Y-up) to EQ Z-up for frustum test
                float eqX = emitter.position.x;
                float eqY = emitter.position.z;  // Irrlicht Z → EQ Y
                float eqZ = emitter.position.y;  // Irrlicht Y → EQ Z
                if (!testFrustumAABB(input.frustumPlanes,
                                      eqX - 2.0f, eqY - 2.0f, eqZ - 2.0f,
                                      eqX + 2.0f, eqY + 8.0f, eqZ + 2.0f)) {
                    continue;
                }
            }

            // Resolve dynamic direction from entity directions map
            const glm::vec3* dirPtr = nullptr;
            if (emitter.useDynamicDirection && emitter.attachEntityID != 0) {
                auto dirIt = pi.entityDirections.find(emitter.attachEntityID);
                if (dirIt != pi.entityDirections.end()) {
                    emitter.dynamicDirection = dirIt->second;
                }
                dirPtr = &emitter.dynamicDirection;
            }

            // BURST: one-shot spawn
            // B995: the family's Options density scales both spawn shapes
            // (Environment::scaledCount / a scaled rate); 0 spawns nothing.
            // B1165 (#484): a spellsnew.edd emitter got its density at
            // creation (CreateEmitter's rule, EmitterConfig::densityAtCreate).
            const float density = emitter.config.densityAtCreate
                                      ? 1.0f
                                      : pi.density[static_cast<size_t>(emitter.family) % 3];
            if (emitter.config.burstCount > 0 && !emitter.isBurstSpawned) {
                const int burst = Environment::scaledCount(emitter.config.burstCount, density);
                for (int s = 0; s < burst; ++s) {
                    // B1150: s/burst is the batch index/size (shapes 1/2 are per-batch deterministic).
                    spawnSpellParticle(emitter.config, emitter.emitterID,
                                       emitter.position + emitter.attachOffset, dirPtr, emitter.family,
                                       s, burst);
                }
                emitter.isBurstSpawned = true;
            }

            // Spawn-rate spawning.  B1121: spawnRate holds for its spawnWindow
            // (0 = the whole emitter life), then sustainRate feeds the emitter
            // from sustainFrom to emitterLifetime (EmitterConfig).
            const auto& ec = emitter.config;
            // B1149 (#469): a spellsnew.edd emitter emits by the client's
            // ticks: tick n at emitter age n / ticksPerSecond, each new tick
            // tickParticles (0x10060e8b..0x10060ffd: ticks = ftol(age x rate),
            // the burst stands for tick 0).
            if (ec.ticksPerSecond > 0.0f && ec.tickParticles > 0.0f) {
                const int ticks = static_cast<int>(std::floor(emitter.emitterAge * ec.ticksPerSecond));
                if (ticks > emitter.lastTick) {
                    emitter.spawnAccumulator += static_cast<float>(ticks - emitter.lastTick) * ec.tickParticles *
                                                std::min(1.0f, std::max(0.0f, density));
                    emitter.lastTick = ticks;
                    const int toSpawn = static_cast<int>(emitter.spawnAccumulator);
                    emitter.spawnAccumulator -= static_cast<float>(toSpawn);
                    for (int s = 0; s < toSpawn; ++s) {
                        // B1150: s/toSpawn is the batch index/size (shapes 1/2).
                        spawnSpellParticle(emitter.config, emitter.emitterID,
                                           emitter.position + emitter.attachOffset, dirPtr, emitter.family,
                                           s, toSpawn);
                    }
                }
                continue;
            }
            float rate = 0.0f;
            if (ec.spawnWindow <= 0.0f || emitter.emitterAge < ec.spawnWindow) rate += ec.spawnRate;
            if (ec.sustainRate > 0.0f && emitter.emitterAge >= ec.sustainFrom) rate += ec.sustainRate;
            if (rate > 0.0f) {
                emitter.spawnAccumulator += rate *
                                            std::min(1.0f, std::max(0.0f, density)) * deltaTime;
                int toSpawn = static_cast<int>(emitter.spawnAccumulator);
                emitter.spawnAccumulator -= static_cast<float>(toSpawn);
                for (int s = 0; s < toSpawn; ++s) {
                    // B1150: s/toSpawn is the batch index/size (shapes 1/2).
                    spawnSpellParticle(emitter.config, emitter.emitterID,
                                       emitter.position + emitter.attachOffset, dirPtr, emitter.family,
                                       s, toSpawn);
                }
            }
        }
    }

    // ---- Physics phase ----
    for (auto& p : particlePool_) {
        if (!p.isAlive()) continue;

        p.age += deltaTime;
        if (p.age >= p.maxLifetime) {
            int idx = static_cast<int>(&p - particlePool_.data());
            freeParticle(idx);
            continue;
        }

        float t = p.getNormalizedAge();

        // Interpolate color and size
        p.color = glm::mix(p.colorStart, p.colorEnd, t);
        if (p.hasEddColour()) p.color.a = p.eddAlpha();  // B1146: the client's fade x peak
        if (p.hasEddSprite())
            p.eddSpriteSize();  // B1149: the client's size over life
        else
            p.size = glm::mix(p.sizeStart, p.sizeEnd, t);

        if (p.hasEddMotion()) {
            // B1151 (#471): the record's motion words re-derive the position
            // from the emitter-frame state; the motionType chain below is
            // the non-record path.
            auto mIt = particleEmitters_.find(p.emitterID);
            if (mIt != particleEmitters_.end()) {
                updateEddMotion(p, mIt->second, deltaTime);
            }
            continue;
        }

        if (p.motionType == MotionType::CAMERA_RELATIVE) {
            auto it = particleEmitters_.find(p.emitterID);
            if (it == particleEmitters_.end()) continue;
            const EmitterConfig& cfg = it->second.config;

            // Gravity
            p.velocity += cfg.gravity * deltaTime;

            // Wind
            if (cfg.windResponse > 0.0f && pi.windStrength > 0.0f) {
                glm::vec3 windIrr(pi.windDirection.x, pi.windDirection.z, pi.windDirection.y);
                p.velocity += windIrr * (pi.windStrength * cfg.windResponse * deltaTime * 10.0f);
            }

            // Drag
            if (p.drag > 0.0f) {
                float dampFactor = 1.0f - p.drag * deltaTime;
                if (dampFactor < 0.0f) dampFactor = 0.0f;
                p.velocity *= dampFactor;
            }

            // Snow drift
            if (cfg.driftAmplitude > 0.0f && cfg.driftFrequency > 0.0f) {
                float drift = std::sin(p.age * cfg.driftFrequency * 6.28318f + p.phase) * cfg.driftAmplitude * deltaTime;
                p.position.x += drift;
                p.position.z += drift * 0.5f;
            }

            // Snow twinkle
            if (cfg.twinkleSpeed > 0.0f) {
                float baseAlpha = glm::mix(p.colorStart.a, p.colorEnd.a, t);
                p.color.a = baseAlpha * (0.7f + 0.3f * std::sin(p.age * cfg.twinkleSpeed + p.phase));
            }

            // Update position
            p.position += p.velocity * deltaTime;

            // Per-particle weather lighting
            constexpr float kParticleLightRadius = 15.0f;
            constexpr float kAmbientFactor = 0.1f;
            float lightR = pi.ambientColor.x * kAmbientFactor;
            float lightG = pi.ambientColor.y * kAmbientFactor;
            float lightB = pi.ambientColor.z * kAmbientFactor;
            for (const auto& light : pi.weatherLights) {
                glm::vec3 diff = p.position - light.position;
                float distSq = glm::dot(diff, diff);
                float dist = std::sqrt(distSq);
                if (dist < light.radius) {
                    float tt = 1.0f - dist / light.radius;
                    float atten = tt * tt;
                    atten *= atten;  // quartic
                    if (dist > kParticleLightRadius) atten = 0.0f;
                    lightR += light.color.x * atten;
                    lightG += light.color.y * atten;
                    lightB += light.color.z * atten;
                }
            }
            p.color.r *= std::min(lightR, 1.0f);
            p.color.g *= std::min(lightG, 1.0f);
            p.color.b *= std::min(lightB, 1.0f);

            // Recycle check
            glm::vec3 offset = p.position - pi.cameraPos;
            float hExtX = cfg.spawnVolumeHalfExtents.x * 1.5f;
            float hExtZ = cfg.spawnVolumeHalfExtents.z * 1.5f;
            float hExtY = cfg.spawnVolumeHalfExtents.y;
            if (std::abs(offset.x) > hExtX || std::abs(offset.z) > hExtZ || offset.y < -hExtY) {
                int idx = static_cast<int>(&p - particlePool_.data());
                freeParticle(idx);
            }
        } else if (p.motionType == MotionType::LINEAR ||
                   p.motionType == MotionType::BURST ||
                   p.motionType == MotionType::RADIAL_EXPAND) {
            auto it = particleEmitters_.find(p.emitterID);
            if (it != particleEmitters_.end()) {
                p.velocity += it->second.config.gravity * deltaTime;
            }

            if (p.drag > 0.0f) {
                float dampFactor = 1.0f - p.drag * deltaTime;
                if (dampFactor < 0.0f) dampFactor = 0.0f;
                p.velocity *= dampFactor;
            }

            p.position += p.velocity * deltaTime;

        } else if (p.motionType == MotionType::ORBITAL) {
            auto it = particleEmitters_.find(p.emitterID);
            if (it != particleEmitters_.end()) {
                glm::vec3 center = it->second.position + it->second.attachOffset;
                p.phase += p.angularVelocity * deltaTime;
                p.position.x = center.x + p.radius * std::cos(p.phase);
                p.position.z = center.z + p.radius * std::sin(p.phase);
                p.position.y += p.velocity.y * deltaTime;
            }
        }
    }

    // ---- Spell effect lifecycle ----
    if (!particleSpellEffects_.empty()) {
        for (auto it = particleSpellEffects_.begin(); it != particleSpellEffects_.end(); ) {
            auto& effect = *it;
            effect.age += deltaTime;

            // Check max duration
            if (effect.maxDuration > 0.0f && effect.age >= effect.maxDuration) {
                for (auto& es : effect.emitterStates) {
                    if (es.activeEmitterID != 0) {
                        auto emIt = particleEmitters_.find(es.activeEmitterID);
                        if (emIt != particleEmitters_.end()) {
                            emIt->second.active = false;
                        }
                    }
                }
            }

            bool allDone = true;
            for (auto& es : effect.emitterStates) {
                const auto& emDef = effect.def.emitters[es.defIndex];

                // Check triggers
                if (!es.triggered) {
                    bool shouldTrigger = false;
                    switch (emDef.trigger) {
                        case SpellTrigger::IMMEDIATE: shouldTrigger = true; break;
                        case SpellTrigger::DELAYED: shouldTrigger = (effect.age >= emDef.triggerDelay); break;
                        case SpellTrigger::ON_CAST_COMPLETE: shouldTrigger = effect.castCompleteSignaled; break;
                        case SpellTrigger::ON_HIT: shouldTrigger = effect.hitSignaled; break;
                    }

                    if (shouldTrigger) {
                        es.triggered = true;

                        glm::vec3 attachPos(0.0f);
                        uint16_t attachEntity = 0;

                        switch (emDef.attach) {
                            case SpellAttach::CASTER:
                                attachEntity = effect.casterEntityID;
                                if (attachEntity != 0) {
                                    auto posIt = pi.entityPositions.find(attachEntity);
                                    if (posIt != pi.entityPositions.end()) attachPos = posIt->second;
                                }
                                break;
                            case SpellAttach::TARGET:
                                attachEntity = effect.targetEntityID;
                                if (attachEntity != 0) {
                                    auto posIt = pi.entityPositions.find(attachEntity);
                                    if (posIt != pi.entityPositions.end()) attachPos = posIt->second;
                                }
                                break;
                            case SpellAttach::PROJECTILE_PATH:
                                if (effect.casterEntityID != 0) {
                                    auto posIt = pi.entityPositions.find(effect.casterEntityID);
                                    if (posIt != pi.entityPositions.end()) attachPos = posIt->second;
                                }
                                break;
                        }

                        ActiveEmitter ae;
                        ae.config = emDef.config;
                        ae.position = attachPos;
                        ae.emitterID = particleNextEmitterID_++;
                        ae.attachEntityID = attachEntity;
                        ae.attachOffset = emDef.positionOffset;
                        ae.family = ParticleFamily::Spell;  // B995
                        ae.useDynamicDirection = effect.useDynamicDirection;

                        if (emDef.attach == SpellAttach::PROJECTILE_PATH) {
                            ae.isProjectile = true;
                            ae.projectileStartPos = attachPos;
                            ae.targetEntityID = effect.targetEntityID;
                            ae.travelDuration = effect.projectileTravelDuration;
                            ae.travelElapsed = 0.0f;
                            if (effect.targetEntityID != 0) {
                                auto posIt = pi.entityPositions.find(effect.targetEntityID);
                                if (posIt != pi.entityPositions.end()) ae.projectileTargetPos = posIt->second;
                            }
                        }

                        particleEmitters_[ae.emitterID] = ae;
                        es.activeEmitterID = ae.emitterID;
                    }
                }

                // Update entity positions for attached emitters
                if (es.activeEmitterID != 0) {
                    auto emIt = particleEmitters_.find(es.activeEmitterID);
                    if (emIt != particleEmitters_.end()) {
                        auto& emitter = emIt->second;

                        if (emitter.isProjectile) {
                            emitter.travelElapsed += deltaTime;
                            if (emitter.targetEntityID != 0) {
                                auto posIt = pi.entityPositions.find(emitter.targetEntityID);
                                if (posIt != pi.entityPositions.end()) {
                                    emitter.projectileTargetPos = posIt->second;
                                }
                            }
                            float tt = (emitter.travelDuration > 0.0f)
                                ? std::min(emitter.travelElapsed / emitter.travelDuration, 1.0f) : 1.0f;
                            emitter.position = glm::mix(emitter.projectileStartPos, emitter.projectileTargetPos, tt);
                            if (tt >= 1.0f) {
                                emitter.active = false;
                                effect.hitSignaled = true;
                            }
                        } else if (emitter.attachEntityID != 0) {
                            auto posIt = pi.entityPositions.find(emitter.attachEntityID);
                            if (posIt != pi.entityPositions.end()) {
                                emitter.position = posIt->second;
                            }
                        }

                        if (emitter.active) {
                            allDone = false;
                        } else {
                            for (const auto& pp : particlePool_) {
                                if (pp.isAlive() && pp.emitterID == es.activeEmitterID) {
                                    allDone = false;
                                    break;
                                }
                            }
                        }
                    }
                } else if (!es.triggered) {
                    allDone = false;
                }
            }

            if (allDone && (effect.maxDuration <= 0.0f || effect.age >= effect.maxDuration)) {
                for (auto& es : effect.emitterStates) {
                    if (es.activeEmitterID != 0) {
                        particleEmitters_.erase(es.activeEmitterID);
                    }
                }
                it = particleSpellEffects_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // ---- Build output ----
    output.particleOutput.renderBuffer.clear();
    output.particleOutput.renderBuffer.reserve(particleActiveCount_);
    for (const auto& p : particlePool_) {
        if (p.isAlive()) {
            output.particleOutput.renderBuffer.push_back(p);
        }
    }
    output.particleOutput.activeCount = particleActiveCount_;

    // Build entity position/direction request sets
    output.particleOutput.positionRequestEntities.clear();
    output.particleOutput.directionRequestEntities.clear();
    for (const auto& effect : particleSpellEffects_) {
        if (effect.casterEntityID != 0) output.particleOutput.positionRequestEntities.insert(effect.casterEntityID);
        if (effect.targetEntityID != 0) output.particleOutput.positionRequestEntities.insert(effect.targetEntityID);
        for (const auto& es : effect.emitterStates) {
            if (es.activeEmitterID != 0) {
                auto emIt = particleEmitters_.find(es.activeEmitterID);
                if (emIt != particleEmitters_.end()) {
                    if (emIt->second.attachEntityID != 0) {
                        output.particleOutput.positionRequestEntities.insert(emIt->second.attachEntityID);
                    }
                    if (emIt->second.useDynamicDirection && emIt->second.attachEntityID != 0) {
                        output.particleOutput.directionRequestEntities.insert(emIt->second.attachEntityID);
                    }
                    if (emIt->second.isProjectile && emIt->second.targetEntityID != 0) {
                        output.particleOutput.positionRequestEntities.insert(emIt->second.targetEntityID);
                    }
                }
            }
        }
    }

    output.particleOutput.valid = true;
}

// Particle allocation helpers
int SimulationWorker::allocateParticle() {
    if (particleFreeList_.empty()) return -1;
    int idx = particleFreeList_.back();
    particleFreeList_.pop_back();
    particleActiveCount_++;
    return idx;
}

void SimulationWorker::freeParticle(int index) {
    if (index < 0 || index >= static_cast<int>(particlePool_.size())) return;
    particlePool_[index].setAlive(false);
    particleFreeList_.push_back(static_cast<uint16_t>(index));
    particleActiveCount_--;
}

float SimulationWorker::particleRandomFloat(float minVal, float maxVal) {
    std::uniform_real_distribution<float> dist(minVal, maxVal);
    return dist(particleRng_);
}

int SimulationWorker::particleRandomInt(int minVal, int maxVal) {
    std::uniform_int_distribution<int> dist(minVal, maxVal);
    return dist(particleRng_);
}

void SimulationWorker::spawnWeatherParticle(const EmitterConfig& cfg, uint16_t emitterID,
                                             const glm::vec3& cameraPos, float transitionAlpha,
                                             const glm::vec3& windDir, float windStrength) {
    int idx = allocateParticle();
    if (idx < 0) return;

    UnifiedParticle& p = particlePool_[idx];
    p.flags = 0;  // B1149: a reused slot keeps no EDD colour/sprite bit of its last particle

    const glm::vec3& he = cfg.spawnVolumeHalfExtents;
    p.position.x = cameraPos.x + particleRandomFloat(-he.x, he.x);
    p.position.z = cameraPos.z + particleRandomFloat(-he.z, he.z);

    float yRand = particleRandomFloat(0.0f, 1.0f);
    if (yRand < cfg.spawnVolumeTopBias) {
        p.position.y = cameraPos.y + he.y * particleRandomFloat(0.6f, 1.0f);
    } else {
        p.position.y = cameraPos.y + particleRandomFloat(-he.y, he.y * 0.6f);
    }

    p.velocity.x = cfg.velocityBase.x + particleRandomFloat(-cfg.velocitySpread.x, cfg.velocitySpread.x);
    p.velocity.y = cfg.velocityBase.y + particleRandomFloat(-cfg.velocitySpread.y, cfg.velocitySpread.y);
    p.velocity.z = cfg.velocityBase.z + particleRandomFloat(-cfg.velocitySpread.z, cfg.velocitySpread.z);

    p.maxLifetime = particleRandomFloat(cfg.lifetimeMin, cfg.lifetimeMax);
    p.age = 0.0f;
    p.colorStart = cfg.colorStart;
    p.colorEnd = cfg.colorEnd;
    p.color = cfg.colorStart;

    if (cfg.blendMode == UnifiedBlendMode::ADDITIVE) {
        float alphaVar = particleRandomFloat(0.3f, cfg.colorStart.a);
        p.colorStart.a = alphaVar;
        p.color.a = alphaVar;
    }

    if (cfg.eddSprite.active) {
        // B1149 (#469): the client's sprite size (0x10061262..0x100612a8): the
        // height from one draw, the width from the same draw when +0x18C is
        // set, else its own; world units, constant but for the grow-in /
        // shrink-out times.
        const auto& es = cfg.eddSprite;
        const float u = particleRandomFloat(0.0f, 1.0f);
        const float v = es.sameRandom ? u : particleRandomFloat(0.0f, 1.0f);
        p.sizeStart = (es.widthMin + (es.widthMax - es.widthMin) * v) * es.scale;
        p.sizeEnd = p.sizeStart;
        p.eddBaseHeight = (es.heightMin + (es.heightMax - es.heightMin) * u) * es.scale;
        p.eddGrowIn = es.growIn;
        p.eddShrinkOut = es.shrinkOut;
        p.eddHeightShrink = es.heightShrink;
        p.eddWidthShrink = es.widthShrink;
        p.setEddSprite(true);
        p.age = 0.0f;
        p.eddSpriteSize();
    } else {
        p.sizeStart = particleRandomFloat(cfg.sizeStartMin, cfg.sizeStartMax);
        p.sizeEnd = particleRandomFloat(cfg.sizeEndMin, cfg.sizeEndMax);
        p.size = p.sizeStart;
        p.setEddSprite(false);
    }

    if (cfg.sizeSpeedCorrelation > 0.0f) {
        float sizeRange = cfg.sizeStartMax - cfg.sizeStartMin;
        float sizeFactor = (sizeRange > 0.0f) ? (p.sizeStart - cfg.sizeStartMin) / sizeRange : 0.0f;
        p.velocity.y *= (1.0f - sizeFactor * cfg.sizeSpeedCorrelation);
    }

    p.drag = cfg.drag;
    p.motionType = cfg.motionType;
    p.phase = particleRandomFloat(0.0f, 6.28318f);

    if (cfg.driftAmplitude == 0.0f && cfg.windResponse > 0.0f) {
        float windAngle = std::atan2(windDir.x, windDir.y);
        p.rotation = windAngle * cfg.windResponse * windStrength * 0.3f;
    } else {
        p.rotation = 0.0f;
    }

    if (cfg.textureRegionCount > 1) {
        p.textureIndex = cfg.textureRegions[particleRandomInt(0, cfg.textureRegionCount - 1)];
    } else {
        p.textureIndex = cfg.textureRegions[0];
    }

    p.emitterID = emitterID;
    p.setAlive(true);
    p.setBlendMode(cfg.blendMode);
    p.setFamily(ParticleFamily::Environment);  // B995: weather
}

// B1151 (#471): one frame of the record's motion words. The per-frame
// integration, each step beside its decompile site (EQGraphicsDX9.dll, TSV
// rows in review/B1141/edd_fields_from_code.tsv):
//   accel   v1 += dt*+0xF4 (0x100626f9)   v2 += dt*+0x100 (0x1006271d)
//           v3 += dt*+0x10C (0x100627cd)  vr += dt*+0x118 (0x1006287d)
//           w  += dt*+0x124 (0x100628d6)  g  += dt*+0x128 (0x100628f6)
//   pos     h  += v1*s*dt (0x100620fd)    offset += basis2*v2*s*dt (+0xF8 row)
//           offset += basis3*v3*s*dt (+0x104 row)
//           r += vr*dt*n_r*s, h += vr*dt*n_h*s (0x10062172..0x10062181)
//           theta += w*dt (512ths, 0x10062199..0x100621a1)
//           world z -= g*s*dt (0x100628f6 row)   world x += s*+0x12C*dt (0x10062917)
// s is the attach scale (the +0x6C read path, TRACE.md (a)). The position is
// RE-DERIVED from the emitter-frame state (h, r, theta, the axis offsets and
// the two world integrals) rather than integrated, so the swirl and the
// radial motion stay on their circle and the +0x198 rule can read r.
void SimulationWorker::updateEddMotion(UnifiedParticle& p, const ActiveEmitter& emitter, float dt) {
    const EmitterConfig::EddMotion& m = emitter.config.eddMotion;

    // +0x60 (orient) / +0x74 (follow): the emitter's frame is re-derived from
    // its CURRENT direction (the T395 per-frame entity refresh) and the
    // record's tilts; the particle's basis follows it. A level, static
    // harness emitter is the identity of this.
    if (m.orientToAttach || m.follow) {
        glm::vec3 e1(0.0f, 1.0f, 0.0f), e2(1.0f, 0.0f, 0.0f), e3(0.0f, 0.0f, 1.0f);
        if (emitter.useDynamicDirection && emitter.attachEntityID != 0) {
            glm::vec3 d(emitter.dynamicDirection.x, 0.0f, emitter.dynamicDirection.z);
            if (glm::length(d) > 0.01f) {
                e2 = glm::normalize(d);
                e3 = glm::normalize(glm::cross(e1, e2));
            }
        }
        const Environment::EddBasis b = Environment::computeEddBasis(
            e1, e2, e3, emitter.config.eddShape.tiltA, emitter.config.eddShape.tiltB);
        p.eddBasis1 = b.b1;
        p.eddBasis2 = b.b2;
        p.eddBasis3 = b.b3;
    }

    // The accel words (the client integrates before it moves).
    p.eddV1 += dt * m.upAccel;
    p.eddV2 += dt * m.axis2Accel;
    p.eddV3 += dt * m.axis3Accel;
    p.eddVr += dt * m.radialAccel;
    p.eddSwirl += dt * m.swirlAccel;
    p.eddGv += dt * m.gravity;

    const float s = m.scale;
    // Axis 1 (height) and the radial/normal split (0x100620fd, +0x110 row).
    p.eddH += (p.eddV1 * s + p.eddVr * p.eddNH * s) * dt;
    p.eddR += p.eddVr * p.eddNR * s * dt;
    // The per-axis offsets (+0xF8/+0x104 rows).
    p.eddO2 += p.eddV2 * s * dt;
    p.eddO3 += p.eddV3 * s * dt;
    // Swirl (512ths of a turn; 512 = full turn, +0x11C row).
    p.eddTheta512 += p.eddSwirl * dt;
    // World gravity (z up, EQ) and +X drift (the world-integral words).
    p.eddGy += p.eddGv * s * dt;
    p.eddDx += m.driftX * s * dt;

    // +0x74 (0x10062936): the particle re-copies its origin from the emitter
    // (the +0xB8.. offsets and +0xC4/+0xC8 tilts re-applied via the basis
    // above); otherwise it holds the base it spawned from.
    const glm::vec3 base = m.follow
        ? emitter.position + emitter.attachOffset + p.eddFrameOffset
        : p.eddBase;

    const float th = p.eddTheta512 * (6.283185307179586f / 512.0f);
    const float cs = std::cos(th), sn = std::sin(th);
    p.position = base
        + p.eddBasis1 * p.eddH
        + p.eddBasis2 * (p.eddR * cs + p.eddO2)
        + p.eddBasis3 * (p.eddR * sn + p.eddO3)
        + glm::vec3(p.eddDx, -p.eddGy, 0.0f);  // EQ z-up -> Irrlicht y-down
}

void SimulationWorker::spawnSpellParticle(const EmitterConfig& cfg, uint16_t emitterID,
                                           const glm::vec3& emitterPos, const glm::vec3* dynamicDir,
                                           ParticleFamily family, int batchIndex, int batchSize) {
    int idx = allocateParticle();
    if (idx < 0) return;

    UnifiedParticle& p = particlePool_[idx];
    p.flags = 0;  // B1149: a reused slot keeps no EDD colour/sprite bit of its last particle

    p.position = emitterPos;
    if (cfg.eddShape.active) {
        // B1150 (#470): the spellsnew.edd +0xA8 shape (spell_spawn_shape.h: the
        // ten official cases, each with its Render address). The emitter frame
        // is the actor's: EDD axis 1 = the shape's height/"up" axis = the world
        // up; EDD axis 2 = the actor's facing (horizontal); EDD axis 3 = the
        // horizontal perpendicular (the attach matrix the client gives a
        // model-attached emitter; a level actor is un-tilted). The record's
        // offsets/tilts/shape words place the particle in that frame; the
        // config's spawnShape/spawnExtents are NOT read for a record emitter.
        // (spell_spawn_shape.h's abstract point: x along EDD axis 1, y along
        // axis 2, z along axis 3.)
        Environment::EddShapeParams sp;
        sp.shape = cfg.eddShape.shape;
        sp.dimX = cfg.eddShape.dimX;
        sp.dimY = cfg.eddShape.dimY;
        sp.dimZ = cfg.eddShape.dimZ;
        sp.offX = cfg.eddShape.offX;
        sp.offY = cfg.eddShape.offY;
        sp.offZ = cfg.eddShape.offZ;
        sp.tiltA = cfg.eddShape.tiltA;
        sp.tiltB = cfg.eddShape.tiltB;
        sp.scale = cfg.eddShape.scale;
        const Environment::EddShapePoint sppt = Environment::computeEddShapePoint(
            sp, batchIndex, batchSize > 0 ? batchSize : 1,
            [this]() { return particleRandomFloat(0.0f, 1.0f); });
        glm::vec3 e1(0.0f, 1.0f, 0.0f);  // EDD axis 1 -> world up
        glm::vec3 e2(1.0f, 0.0f, 0.0f);  // EDD axis 2 -> the facing
        glm::vec3 e3(0.0f, 0.0f, 1.0f);  // EDD axis 3 -> the perpendicular
        if (dynamicDir) {
            glm::vec3 d(dynamicDir->x, 0.0f, dynamicDir->z);
            if (glm::length(d) > 0.01f) {
                e2 = glm::normalize(d);
                e3 = glm::normalize(glm::cross(e1, e2));
            }
        }
        // B1151: the POST-tilt basis (spell_spawn_shape.h computeEddBasis,
        // the same rotation the shape applied) -- the motion words integrate
        // along these very axes.
        const Environment::EddBasis eddBasis =
            Environment::computeEddBasis(e1, e2, e3, sp.tiltA, sp.tiltB);
        p.position += eddBasis.b1 * sppt.point.x + eddBasis.b2 * sppt.point.y + eddBasis.b3 * sppt.point.z;
        // +0xD0: the renderer subtracts it from the view depth (0x10063069/0x10063198).
        p.eddDepthBias = cfg.eddShape.depthBias;
        if (cfg.eddMotion.active) {
            // B1151 (#471): the record's motion words (TSV rows in
            // review/B1141/edd_fields_from_code.tsv). Emitter-frame state:
            // h along basis 1, (r, theta) in the basis 2/3 plane. The shape
            // point is already in POST-tilt coordinates, so its axis-1
            // component is the height and (axis 2, axis 3) the radial plane.
            const EmitterConfig::EddMotion& m = cfg.eddMotion;
            p.setEddMotion(true);
            p.velocity = glm::vec3(0.0f);  // the file's motion drives position
            p.eddBasis1 = eddBasis.b1;
            p.eddBasis2 = eddBasis.b2;
            p.eddBasis3 = eddBasis.b3;
            p.eddBase = emitterPos;
            p.eddFrameOffset = p.position - emitterPos;
            p.eddH = sppt.point.x;
            p.eddR = std::hypot(sppt.point.y, sppt.point.z);
            p.eddTheta512 = std::atan2(sppt.point.z, sppt.point.y) * (512.0f / 6.283185307179586f);
            // The +0x110 rule: n = the spawn normal (shapes 5/6, B1150) in
            // (r, h); else (1, 0) = horizontal. The abstract normal's x
            // component is the height, (y, z) the radial plane.
            if (sppt.hasNormal) {
                p.eddNH = sppt.normal.x;
                p.eddNR = std::hypot(sppt.normal.y, sppt.normal.z);
                if (p.eddNR <= 1e-6f && std::fabs(p.eddNH) <= 1e-6f) {
                    p.eddNR = 1.0f;
                    p.eddNH = 0.0f;
                }
            } else {
                p.eddNR = 1.0f;
                p.eddNH = 0.0f;
            }
            // The client's per-particle draws (0x100620fd..0x100621a1).
            p.eddV1 = particleRandomFloat(m.upMin, m.upMax);        // 0x100620fd/0x10062108
            p.eddV2 = particleRandomFloat(m.axis2Min, m.axis2Max);  // 0x10062124/0x1006212f
            p.eddV3 = particleRandomFloat(m.axis3Min, m.axis3Max);  // 0x1006214b/0x10062156
            p.eddVr = particleRandomFloat(m.radialMin, m.radialMax);  // 0x10062172/0x10062181
            p.eddSwirl = particleRandomFloat(m.swirlMin, m.swirlMax); // 0x10062199/0x100621a1
            p.eddSpin = particleRandomFloat(m.spinMin, m.spinMax);   // 0x10061239/0x10061241/0x10061250
            // +0x174 (TSV row): !=0 -> start angle = rand()/32767*1000 (512ths).
            p.eddStart512 = m.randomStartSpin ? particleRandomFloat(0.0f, 1000.0f) : 0.0f;
            p.eddAlignWord = m.spinAlign;       // +0x178
            p.eddFrameCount = m.frameCount;     // +0x130
            p.eddFrameRate = m.frameRate;       // +0x134
            p.eddSpriteMode = static_cast<uint8_t>(m.spriteMode);  // +0x70
            p.eddNoDepthWrite = m.noDepthWrite ? 1 : 0;            // +0x64
            p.eddDrawNegativeRadius = m.drawNegativeRadius ? 1 : 0;  // +0x198
        }
    } else if (cfg.spawnShape == SpawnShape::BOX) {
        p.position.x += particleRandomFloat(-cfg.spawnExtents.x, cfg.spawnExtents.x);
        p.position.y += particleRandomFloat(-cfg.spawnExtents.y, cfg.spawnExtents.y);
        p.position.z += particleRandomFloat(-cfg.spawnExtents.z, cfg.spawnExtents.z);
    } else if (cfg.spawnShape == SpawnShape::SPHERE) {
        float r = particleRandomFloat(0.0f, cfg.spawnExtents.x);
        float theta = particleRandomFloat(0.0f, 6.28318f);
        float phi = particleRandomFloat(-1.5708f, 1.5708f);
        p.position.x += r * std::cos(phi) * std::cos(theta);
        p.position.y += r * std::sin(phi);
        p.position.z += r * std::cos(phi) * std::sin(theta);
    } else if (cfg.spawnShape == SpawnShape::RING) {
        float angle = particleRandomFloat(0.0f, 6.28318f);
        p.position.x += cfg.spawnExtents.x * std::cos(angle);
        p.position.z += cfg.spawnExtents.x * std::sin(angle);
    } else if (cfg.spawnShape == SpawnShape::DISC) {
        // B1131: record 22's halo -- uniform over the disc's area.
        float angle = particleRandomFloat(0.0f, 6.28318f);
        float r = cfg.spawnExtents.x * std::sqrt(particleRandomFloat(0.0f, 1.0f));
        p.position.x += r * std::cos(angle);
        p.position.z += r * std::sin(angle);
        p.position.y += particleRandomFloat(-cfg.spawnExtents.y, cfg.spawnExtents.y);
    }

    if (dynamicDir) {
        float speed = glm::length(cfg.velocityBase);
        if (speed < 0.01f) speed = 4.0f;
        p.velocity = (*dynamicDir) * speed;
        p.velocity.x += particleRandomFloat(-cfg.velocitySpread.x, cfg.velocitySpread.x);
        p.velocity.y += particleRandomFloat(-cfg.velocitySpread.y, cfg.velocitySpread.y);
        p.velocity.z += particleRandomFloat(-cfg.velocitySpread.z, cfg.velocitySpread.z);
    } else if (cfg.motionType == MotionType::RADIAL_EXPAND) {
        float angle = particleRandomFloat(0.0f, 6.28318f);
        p.velocity.x = std::cos(angle) * cfg.expandSpeed + particleRandomFloat(-cfg.velocitySpread.x, cfg.velocitySpread.x);
        p.velocity.z = std::sin(angle) * cfg.expandSpeed + particleRandomFloat(-cfg.velocitySpread.z, cfg.velocitySpread.z);
        p.velocity.y = cfg.velocityBase.y + particleRandomFloat(-cfg.velocitySpread.y, cfg.velocitySpread.y);
    } else if (cfg.motionType == MotionType::BURST) {
        float angle = particleRandomFloat(0.0f, 6.28318f);
        p.velocity.x = std::cos(angle) * cfg.velocityBase.x + particleRandomFloat(-cfg.velocitySpread.x, cfg.velocitySpread.x);
        p.velocity.z = std::sin(angle) * cfg.velocityBase.x + particleRandomFloat(-cfg.velocitySpread.z, cfg.velocitySpread.z);
        p.velocity.y = cfg.velocityBase.y + particleRandomFloat(-cfg.velocitySpread.y, cfg.velocitySpread.y);
    } else {
        p.velocity.x = cfg.velocityBase.x + particleRandomFloat(-cfg.velocitySpread.x, cfg.velocitySpread.x);
        p.velocity.y = cfg.velocityBase.y + particleRandomFloat(-cfg.velocitySpread.y, cfg.velocitySpread.y);
        p.velocity.z = cfg.velocityBase.z + particleRandomFloat(-cfg.velocitySpread.z, cfg.velocitySpread.z);
    }

    p.maxLifetime = particleRandomFloat(cfg.lifetimeMin, cfg.lifetimeMax);
    p.age = 0.0f;
    p.colorStart = cfg.colorStart;
    p.colorEnd = cfg.colorEnd;
    p.color = cfg.colorStart;

    if (cfg.eddSprite.active) {
        // B1149 (#469): the client's sprite size (0x10061262..0x100612a8): the
        // height from one draw, the width from the same draw when +0x18C is
        // set, else its own; world units, constant but for the grow-in /
        // shrink-out times.
        const auto& es = cfg.eddSprite;
        const float u = particleRandomFloat(0.0f, 1.0f);
        const float v = es.sameRandom ? u : particleRandomFloat(0.0f, 1.0f);
        p.sizeStart = (es.widthMin + (es.widthMax - es.widthMin) * v) * es.scale;
        p.sizeEnd = p.sizeStart;
        p.eddBaseHeight = (es.heightMin + (es.heightMax - es.heightMin) * u) * es.scale;
        p.eddGrowIn = es.growIn;
        p.eddShrinkOut = es.shrinkOut;
        p.eddHeightShrink = es.heightShrink;
        p.eddWidthShrink = es.widthShrink;
        p.setEddSprite(true);
        p.age = 0.0f;
        p.eddSpriteSize();
    } else {
        p.sizeStart = particleRandomFloat(cfg.sizeStartMin, cfg.sizeStartMax);
        p.sizeEnd = particleRandomFloat(cfg.sizeEndMin, cfg.sizeEndMax);
        p.size = p.sizeStart;
        p.setEddSprite(false);
    }

    p.drag = cfg.drag;
    p.motionType = cfg.motionType;
    p.phase = particleRandomFloat(0.0f, 6.28318f);
    p.rotation = 0.0f;

    if (cfg.motionType == MotionType::ORBITAL) {
        p.radius = cfg.orbitalRadius;
        p.angularVelocity = cfg.orbitalAngularVelocity;
        p.position.x = emitterPos.x + p.radius * std::cos(p.phase);
        p.position.z = emitterPos.z + p.radius * std::sin(p.phase);
    }

    // The same one random draw as before B1146, so a white emitter's
    // particles (and the RNG stream every later particle sees) are unchanged.
    const int region = cfg.textureRegionCount > 1
                           ? particleRandomInt(0, cfg.textureRegionCount - 1)
                           : 0;
    p.textureIndex = cfg.textureRegions[region];

    p.emitterID = emitterID;
    p.setAlive(true);
    p.setBlendMode(cfg.blendMode);
    p.setFamily(family);  // B995

    // B1146 (#466): the emitter that names this texture carries spellsnew.edd
    // colour words -- the particle takes its start -> end RGB (EQGraphicsDX9
    // 0x100605b0). With `clientAlpha` (B1149: every spellsnew.eff emitter,
    // white too) it also takes the client's fade x peak alpha and its +0x68
    // blend; without it the alpha and blend stay the style's (see
    // SpellPresets::applyStageEmitterColours()).
    const auto& rc = cfg.regionColours[region];
    if (rc.active) {
        p.colorStart = glm::vec4(rc.start, p.colorStart.a);
        p.colorEnd = glm::vec4(rc.end, p.colorEnd.a);
        p.color = p.colorStart;
        if (rc.clientAlpha) {
            p.setEddColour(true);
            p.eddFadeIn = rc.fadeIn;
            p.eddFadeOut = rc.fadeOut;
            p.eddPeakOpacity = rc.peakOpacity;
            p.color.a = p.eddAlpha();
            p.setBlendMode(rc.additive ? UnifiedBlendMode::ADDITIVE : UnifiedBlendMode::ALPHA);
        }
    }
}

// ============================================================================
// Spell VFX Computation (desktop GL worker-driven path)
// ============================================================================

void SimulationWorker::computeSpellVFX(const SimulationInput& input, SimulationOutput& output) {
    auto& svi = input.spellVfxInput;
    auto& svo = output.spellVfxOutput;

    if (!svi.initialized) {
        svo.valid = false;
        return;
    }

    svo.effectUpdates.clear();
    svo.createEvents.clear();
    svo.removeEvents.clear();
    svo.impactEvents.clear();
    svo.valid = true;

    float dt = svi.deltaTime;

    // SpellFXType values (must match EQ::SpellFXType enum)
    constexpr uint8_t FX_CAST_GLOW = 1;
    constexpr uint8_t FX_PROJECTILE = 2;
    constexpr uint8_t FX_IMPACT = 3;
    constexpr uint8_t FX_AURA = 4;
    constexpr uint8_t FX_RAIN = 5;
    constexpr uint8_t FX_GROUND_CIRCLE = 7;

    // Process commands
    for (const auto& cmd : svi.commands) {
        switch (cmd.type) {
            case SpellVFXCommand::CreateEffect: {
                WorkerSpellEffect wfx;
                wfx.effectId = cmd.effectId;
                wfx.type = cmd.fxType;
                wfx.spellId = cmd.spellId;
                wfx.sourceEntity = cmd.sourceEntity;
                wfx.targetEntity = cmd.targetEntity;
                wfx.lifetime = cmd.lifetime;
                wfx.scale = cmd.scale;
                wfx.colorA = cmd.colorA;
                wfx.colorR = cmd.colorR;
                wfx.colorG = cmd.colorG;
                wfx.colorB = cmd.colorB;
                wfx.posX = cmd.posX;
                wfx.posY = cmd.posY;
                wfx.posZ = cmd.posZ;
                wfx.targetPosX = cmd.targetPosX;
                wfx.targetPosY = cmd.targetPosY;
                wfx.targetPosZ = cmd.targetPosZ;
                wfx.elapsed = 0;
                wfx.active = true;

                // Resolve initial positions from workerEntities_ if entity-attached
                if (wfx.sourceEntity != 0 && wfx.posX == 0 && wfx.posY == 0 && wfx.posZ == 0) {
                    auto it = workerEntities_.find(wfx.sourceEntity);
                    if (it != workerEntities_.end()) {
                        const auto& we = it->second;
                        wfx.posX = we.lastX;
                        wfx.posY = we.lastZ + we.modelYOffset;  // EQ Z → Irrlicht Y
                        wfx.posZ = we.lastY;                     // EQ Y → Irrlicht Z
                    }
                }
                if (wfx.targetEntity != 0 && wfx.targetPosX == 0 && wfx.targetPosY == 0 && wfx.targetPosZ == 0) {
                    auto it = workerEntities_.find(wfx.targetEntity);
                    if (it != workerEntities_.end()) {
                        const auto& we = it->second;
                        wfx.targetPosX = we.lastX;
                        wfx.targetPosY = we.lastZ + we.modelYOffset;
                        wfx.targetPosZ = we.lastY;
                    }
                }

                // Emit create event for main thread
                SimulationOutput::SpellVFXOutput::CreateEvent ce;
                ce.effectId = wfx.effectId;
                ce.fxType = wfx.type;
                ce.spellId = wfx.spellId;
                ce.sourceEntity = wfx.sourceEntity;
                ce.targetEntity = wfx.targetEntity;
                ce.posX = wfx.posX;
                ce.posY = wfx.posY;
                ce.posZ = wfx.posZ;
                ce.targetPosX = wfx.targetPosX;
                ce.targetPosY = wfx.targetPosY;
                ce.targetPosZ = wfx.targetPosZ;
                ce.lifetime = wfx.lifetime;
                ce.scale = wfx.scale;
                ce.colorA = wfx.colorA;
                ce.colorR = wfx.colorR;
                ce.colorG = wfx.colorG;
                ce.colorB = wfx.colorB;
                svo.createEvents.push_back(ce);

                spellVfxEffects_.push_back(wfx);
                break;
            }
            case SpellVFXCommand::RemoveCastGlow: {
                for (size_t i = 0; i < spellVfxEffects_.size(); ) {
                    if (spellVfxEffects_[i].type == FX_CAST_GLOW &&
                        spellVfxEffects_[i].sourceEntity == cmd.sourceEntity) {
                        svo.removeEvents.push_back({spellVfxEffects_[i].effectId});
                        if (i != spellVfxEffects_.size() - 1)
                            spellVfxEffects_[i] = std::move(spellVfxEffects_.back());
                        spellVfxEffects_.pop_back();
                    } else {
                        ++i;
                    }
                }
                break;
            }
            case SpellVFXCommand::ClearAll: {
                for (auto& fx : spellVfxEffects_) {
                    svo.removeEvents.push_back({fx.effectId});
                }
                spellVfxEffects_.clear();
                break;
            }
        }
    }

    // Update active effects
    constexpr float PROJECTILE_SPEED = 500.0f;

    for (size_t i = 0; i < spellVfxEffects_.size(); ) {
        auto& fx = spellVfxEffects_[i];
        if (!fx.active) {
            svo.removeEvents.push_back({fx.effectId});
            if (i != spellVfxEffects_.size() - 1)
                spellVfxEffects_[i] = std::move(spellVfxEffects_.back());
            spellVfxEffects_.pop_back();
            continue;
        }

        fx.elapsed += dt;

        // Check lifetime expiration
        if (fx.lifetime > 0 && fx.elapsed >= fx.lifetime) {
            svo.removeEvents.push_back({fx.effectId});
            if (i != spellVfxEffects_.size() - 1)
                spellVfxEffects_[i] = std::move(spellVfxEffects_.back());
            spellVfxEffects_.pop_back();
            continue;
        }

        switch (fx.type) {
            case FX_PROJECTILE: {
                // Update target position from entity
                if (fx.targetEntity != 0) {
                    auto it = workerEntities_.find(fx.targetEntity);
                    if (it != workerEntities_.end()) {
                        const auto& we = it->second;
                        fx.targetPosX = we.lastX;
                        fx.targetPosY = we.lastZ + we.modelYOffset;
                        fx.targetPosZ = we.lastY;
                    }
                }

                // Move toward target
                float dx = fx.targetPosX - fx.posX;
                float dy = fx.targetPosY - fx.posY;
                float dz = fx.targetPosZ - fx.posZ;
                float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

                if (dist < 3.0f) {
                    // Arrived — emit impact event and deactivate
                    svo.impactEvents.push_back({fx.targetEntity, fx.spellId});
                    fx.active = false;
                    // Will be removed next iteration
                } else {
                    float invDist = 1.0f / dist;
                    float moveAmount = PROJECTILE_SPEED * dt;
                    if (moveAmount > dist) moveAmount = dist;
                    fx.posX += dx * invDist * moveAmount;
                    fx.posY += dy * invDist * moveAmount;
                    fx.posZ += dz * invDist * moveAmount;

                    SimulationOutput::SpellVFXOutput::EffectUpdate eu;
                    eu.effectId = fx.effectId;
                    eu.posX = fx.posX;
                    eu.posY = fx.posY;
                    eu.posZ = fx.posZ;
                    eu.hasBillboardUpdate = true;
                    eu.hasParticleUpdate = false;
                    eu.hasColorUpdate = false;
                    eu.billboardWidth = 0;
                    eu.billboardHeight = 0;
                    svo.effectUpdates.push_back(eu);
                }
                break;
            }
            case FX_CAST_GLOW: {
                // Follow source entity
                if (fx.sourceEntity != 0) {
                    auto it = workerEntities_.find(fx.sourceEntity);
                    if (it != workerEntities_.end()) {
                        const auto& we = it->second;
                        fx.posX = we.lastX;
                        fx.posY = we.lastZ + we.modelYOffset;
                        fx.posZ = we.lastY;
                    }
                }

                // Compute sine pulse
                float pulse = 0.7f + 0.3f * std::sin(fx.elapsed * 6.0f);
                float alpha = 80.0f + 40.0f * std::sin(fx.elapsed * 6.0f);

                SimulationOutput::SpellVFXOutput::EffectUpdate eu;
                eu.effectId = fx.effectId;
                // Billboard at chest height (+3 Irrlicht Y)
                eu.posX = fx.posX;
                eu.posY = fx.posY + 3.0f;
                eu.posZ = fx.posZ;
                eu.billboardWidth = 12.0f * pulse;
                eu.billboardHeight = 12.0f * pulse;
                eu.hasBillboardUpdate = true;
                // Particle system at base height (+1 Irrlicht Y)
                eu.particlePosX = fx.posX;
                eu.particlePosY = fx.posY + 1.0f;
                eu.particlePosZ = fx.posZ;
                eu.hasParticleUpdate = true;
                // Color with pulsing alpha
                eu.colorA = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, alpha)));
                eu.colorR = fx.colorR;
                eu.colorG = fx.colorG;
                eu.colorB = fx.colorB;
                eu.hasColorUpdate = true;
                svo.effectUpdates.push_back(eu);
                break;
            }
            case FX_AURA: {
                // Follow source entity
                if (fx.sourceEntity != 0) {
                    auto it = workerEntities_.find(fx.sourceEntity);
                    if (it != workerEntities_.end()) {
                        const auto& we = it->second;
                        fx.posX = we.lastX;
                        fx.posY = we.lastZ + we.modelYOffset;
                        fx.posZ = we.lastY;
                    }
                }

                SimulationOutput::SpellVFXOutput::EffectUpdate eu;
                eu.effectId = fx.effectId;
                eu.particlePosX = fx.posX;
                eu.particlePosY = fx.posY;
                eu.particlePosZ = fx.posZ;
                eu.hasBillboardUpdate = false;
                eu.hasParticleUpdate = true;
                eu.hasColorUpdate = false;
                svo.effectUpdates.push_back(eu);
                break;
            }
            default:
                // ImpactBurst, RainEffect, GroundCircle — no per-frame update needed
                // (Irrlicht internal particle systems drive them)
                break;
        }

        ++i;
    }
}

} // namespace Graphics
} // namespace EQT
