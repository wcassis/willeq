#include "client/graphics/simulation_worker.h"
#include "client/graphics/portal_system.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/environment/unified_particle.h"
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
        particleRng_.seed(std::random_device{}());
        particlePoolInitialized_ = true;
        LOG_INFO(MOD_GRAPHICS, "SimulationWorker: Particle pool initialized (size={})", poolSize);
    }

    const auto& pi = input.particleInput;
    float deltaTime = pi.deltaTime;
    if (deltaTime <= 0.0f || deltaTime > 1.0f) {
        output.particleOutput.valid = true;
        output.particleOutput.activeCount = particleActiveCount_;
        return;
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
            case ParticleCommand::CreateSpellEffectAtPos: {
                SpellEffectInstance inst;
                inst.effectID = cmd.preAssignedEffectID;
                inst.spellID = 0;
                inst.casterEntityID = 0;
                inst.targetEntityID = 0;
                inst.groundTarget = cmd.worldPos;
                inst.age = 0.0f;
                inst.maxDuration = cmd.duration;
                inst.def = cmd.spellDef;
                for (auto& e : inst.def.emitters) {
                    if (e.attach == SpellAttach::CASTER) {
                        e.attach = SpellAttach::GROUND_TARGET;
                    }
                }
                for (int i = 0; i < static_cast<int>(cmd.spellDef.emitters.size()); ++i) {
                    SpellEffectInstance::EmitterState es;
                    es.defIndex = i;
                    es.activeEmitterID = 0;
                    es.triggered = false;
                    inst.emitterStates.push_back(es);
                }
                particleSpellEffects_.push_back(std::move(inst));
                if (particleNextSpellEffectID_ <= cmd.preAssignedEffectID) {
                    particleNextSpellEffectID_ = cmd.preAssignedEffectID + 1;
                }
                break;
            }
            case ParticleCommand::RemoveSpellEffect: {
                for (auto it = particleSpellEffects_.begin(); it != particleSpellEffects_.end(); ++it) {
                    if (it->effectID == cmd.effectID) {
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
                        particleSpellEffects_.erase(it);
                        break;
                    }
                }
                break;
            }
            case ParticleCommand::RemoveSpellEffectsEntity: {
                for (auto it = particleSpellEffects_.begin(); it != particleSpellEffects_.end(); ) {
                    if (it->casterEntityID == cmd.entityID || it->targetEntityID == cmd.entityID) {
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

            int effectiveTarget = static_cast<int>(emitter.config.targetCount * emitter.transitionAlpha);
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
            if (emitter.config.burstCount > 0 && !emitter.isBurstSpawned) {
                for (int s = 0; s < emitter.config.burstCount; ++s) {
                    spawnSpellParticle(emitter.config, emitter.emitterID,
                                       emitter.position + emitter.attachOffset, dirPtr);
                }
                emitter.isBurstSpawned = true;
            }

            // Spawn-rate spawning
            if (emitter.config.spawnRate > 0.0f) {
                emitter.spawnAccumulator += emitter.config.spawnRate * deltaTime;
                int toSpawn = static_cast<int>(emitter.spawnAccumulator);
                emitter.spawnAccumulator -= static_cast<float>(toSpawn);
                for (int s = 0; s < toSpawn; ++s) {
                    spawnSpellParticle(emitter.config, emitter.emitterID,
                                       emitter.position + emitter.attachOffset, dirPtr);
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
        p.size = glm::mix(p.sizeStart, p.sizeEnd, t);

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
                            case SpellAttach::GROUND_TARGET:
                                attachPos = effect.groundTarget;
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

    p.sizeStart = particleRandomFloat(cfg.sizeStartMin, cfg.sizeStartMax);
    p.sizeEnd = particleRandomFloat(cfg.sizeEndMin, cfg.sizeEndMax);
    p.size = p.sizeStart;

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
}

void SimulationWorker::spawnSpellParticle(const EmitterConfig& cfg, uint16_t emitterID,
                                           const glm::vec3& emitterPos, const glm::vec3* dynamicDir) {
    int idx = allocateParticle();
    if (idx < 0) return;

    UnifiedParticle& p = particlePool_[idx];

    p.position = emitterPos;
    if (cfg.spawnShape == SpawnShape::BOX) {
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

    p.sizeStart = particleRandomFloat(cfg.sizeStartMin, cfg.sizeStartMax);
    p.sizeEnd = particleRandomFloat(cfg.sizeEndMin, cfg.sizeEndMax);
    p.size = p.sizeStart;

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

    if (cfg.textureRegionCount > 1) {
        p.textureIndex = cfg.textureRegions[particleRandomInt(0, cfg.textureRegionCount - 1)];
    } else {
        p.textureIndex = cfg.textureRegions[0];
    }

    p.emitterID = emitterID;
    p.setAlive(true);
    p.setBlendMode(cfg.blendMode);
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
            case SpellVFXCommand::RemoveBuffAura: {
                for (size_t i = 0; i < spellVfxEffects_.size(); ) {
                    if (spellVfxEffects_[i].type == FX_AURA &&
                        spellVfxEffects_[i].sourceEntity == cmd.sourceEntity &&
                        spellVfxEffects_[i].spellId == cmd.spellId) {
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
            case SpellVFXCommand::RemoveAllForEntity: {
                for (size_t i = 0; i < spellVfxEffects_.size(); ) {
                    if (spellVfxEffects_[i].sourceEntity == cmd.sourceEntity ||
                        spellVfxEffects_[i].targetEntity == cmd.sourceEntity) {
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
