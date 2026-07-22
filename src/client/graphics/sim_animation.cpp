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
// Fire Light Flicker
// ============================================================================

void SimulationWorker::computeFireFlicker(const SimulationInput& input, SimulationOutput& output) {
    size_t lightCount = zoneData_.objectLights.size();
    if (lightCount == 0) return;

    if (output.objectLightColors.size() != lightCount) {
        output.objectLightColors.resize(lightCount);
    }
    if (flickerPhases_.size() != lightCount) {
        flickerPhases_.resize(lightCount, 0.0f);
    }

    for (size_t i = 0; i < lightCount; ++i) {
        const auto& ol = zoneData_.objectLights[i];

        float r = ol.originalColor.r;
        float g = ol.originalColor.g;
        float b = ol.originalColor.b;

        if (ol.isFireSource) {
            // Advance flicker phase
            flickerPhases_[i] += input.deltaTime * ol.flickerSpeed;

            // Fire flicker formula: base + two sine waves at different frequencies
            float phase = flickerPhases_[i];
            float flicker = 0.85f + 0.10f * std::sin(phase * 6.7f) + 0.05f * std::sin(phase * 13.1f);

            r *= flicker;
            g *= flicker;
            b *= flicker;
        }

        output.objectLightColors[i].r = r;
        output.objectLightColors[i].g = g;
        output.objectLightColors[i].b = b;
    }

    // Also update flicker colors in selected lights
    for (int i = 0; i < output.activeLightCount; ++i) {
        auto& sl = output.selectedLights[i];
        if (!sl.valid || sl.isZoneLight || sl.isPlayerLight) continue;
        if (sl.sourceIndex < lightCount) {
            const auto& fc = output.objectLightColors[sl.sourceIndex];
            sl.diffuseColor.r = fc.r;
            sl.diffuseColor.g = fc.g;
            sl.diffuseColor.b = fc.b;
        }
    }
}

// ============================================================================
// Tree Wind Animation
// ============================================================================

irr::core::vector3df SimulationWorker::computeTreeWindDisplacement(
    const irr::core::vector3df& worldPos, float normalizedHeight,
    float meshSeed, const SimulationInput::TreeWindState& wind) const
{
    if (!wind.enabled || wind.weatherMultiplier < 0.001f) {
        return irr::core::vector3df(0, 0, 0);
    }

    // Height-based influence (power curve from influenceStartHeight to 1.0)
    if (normalizedHeight <= wind.influenceStartHeight) {
        return irr::core::vector3df(0, 0, 0);
    }
    float heightRange = 1.0f - wind.influenceStartHeight;
    if (heightRange <= 0.0f) return irr::core::vector3df(0, 0, 0);
    float heightFactor = (std::min(normalizedHeight, 1.0f) - wind.influenceStartHeight) / heightRange;
    float influence = std::pow(heightFactor, wind.influenceExponent);
    if (influence < 0.001f) return irr::core::vector3df(0, 0, 0);

    constexpr float TWO_PI = 6.28318f;

    // Primary sway
    float primaryPhase = wind.time * wind.baseFrequency * TWO_PI + meshSeed;
    float primarySway = std::sin(primaryPhase) * wind.baseStrength;

    // Secondary gust
    float gustPhase = wind.time * wind.gustFrequency * TWO_PI + meshSeed * 1.7f;
    float gustSway = std::sin(gustPhase * 3.0f + std::sin(gustPhase)) * wind.gustStrength;

    // Turbulence
    float turbX = std::sin(wind.time * 5.0f + worldPos.X * 0.1f + meshSeed) * wind.turbulence;
    float turbZ = std::cos(wind.time * 4.3f + worldPos.Z * 0.1f + meshSeed) * wind.turbulence;

    float totalStrength = (primarySway + gustSway) * wind.weatherMultiplier * influence;

    float dispX = totalStrength * wind.windDirX + turbX * influence * wind.weatherMultiplier;
    float dispY = 0.0f;
    float dispZ = totalStrength * 0.7f * wind.windDirY + turbZ * influence * wind.weatherMultiplier;

    return irr::core::vector3df(dispX, dispY, dispZ);
}

void SimulationWorker::computeTreeAnimation(const SimulationInput& input, SimulationOutput& output) {
    if (zoneData_.trees.empty() || !input.treeWind.enabled) return;

    // Ensure shadow buffers are allocated
    if (output.treeShadows.size() != zoneData_.trees.size()) {
        output.treeShadows.resize(zoneData_.trees.size());
        for (size_t t = 0; t < zoneData_.trees.size(); ++t) {
            output.treeShadows[t].resize(zoneData_.trees[t].buffers.size());
            for (size_t b = 0; b < zoneData_.trees[t].buffers.size(); ++b) {
                output.treeShadows[t][b].positions.resize(
                    zoneData_.trees[t].buffers[b].basePositions.size());
            }
        }
    }

    // Distance culling threshold for trees
    float treeRenderDistSq = input.renderDistance * input.renderDistance;

    for (size_t t = 0; t < zoneData_.trees.size(); ++t) {
        const auto& tree = zoneData_.trees[t];

        // Distance cull (Irrlicht Y-up)
        float dx = tree.worldPosition.X - input.cameraPos.X;
        float dy = tree.worldPosition.Y - input.cameraPos.Y;
        float dz = tree.worldPosition.Z - input.cameraPos.Z;
        if (dx*dx + dy*dy + dz*dz > treeRenderDistSq) {
            for (auto& bs : output.treeShadows[t]) bs.dirty = false;
            continue;
        }

        for (size_t b = 0; b < tree.buffers.size(); ++b) {
            const auto& buf = tree.buffers[b];
            auto& shadow = output.treeShadows[t][b];

            for (size_t v = 0; v < buf.basePositions.size(); ++v) {
                const auto& basePos = buf.basePositions[v];
                irr::core::vector3df worldPos = basePos + tree.worldPosition;
                irr::core::vector3df displacement = computeTreeWindDisplacement(
                    worldPos, buf.vertexHeights[v], tree.meshSeed, input.treeWind);
                shadow.positions[v] = basePos + displacement;
            }
            shadow.dirty = true;
        }
    }
}

// ============================================================================
// Vertex Animations (flags, banners)
// ============================================================================

void SimulationWorker::computeVertexAnimations(const SimulationInput& input, SimulationOutput& output) {
    if (zoneData_.vertexAnims.empty()) return;

    // Ensure state and output vectors
    if (vertAnimStates_.size() != zoneData_.vertexAnims.size()) {
        vertAnimStates_.resize(zoneData_.vertexAnims.size());
    }
    if (output.vertexAnims.size() != zoneData_.vertexAnims.size()) {
        output.vertexAnims.resize(zoneData_.vertexAnims.size());
    }

    float deltaMs = input.vertAnimDeltaMs;

    for (size_t i = 0; i < zoneData_.vertexAnims.size(); ++i) {
        const auto& vad = zoneData_.vertexAnims[i];
        auto& state = vertAnimStates_[i];
        auto& result = output.vertexAnims[i];

        if (vad.frameCount == 0 || vad.delayMs <= 0) {
            result.frameChanged = false;
            continue;
        }

        state.elapsedMs += deltaMs;
        if (state.elapsedMs >= static_cast<float>(vad.delayMs)) {
            state.elapsedMs = std::fmod(state.elapsedMs, static_cast<float>(vad.delayMs));
            state.currentFrame = (state.currentFrame + 1) % static_cast<int>(vad.frameCount);
            result.currentFrame = state.currentFrame;
            result.frameChanged = true;
        } else {
            result.currentFrame = state.currentFrame;
            result.frameChanged = false;
        }
    }
}

// ============================================================================
// Detail Wind/Disturbance Animation
// ============================================================================

void SimulationWorker::computeDetailAnimation(const SimulationInput& input, SimulationOutput& output) {
    const auto& di = input.detailInput;
    if (!di.initialized) {
        output.detailOutput.valid = false;
        return;
    }

    // Process commands
    for (const auto& cmd : di.commands) {
        switch (cmd.type) {
            case DetailCommand::AddChunk: {
                // Upsert: find existing chunk by key or add new
                bool found = false;
                for (auto& chunk : detailChunks_) {
                    if (chunk.keyX == cmd.chunkKeyX && chunk.keyZ == cmd.chunkKeyZ) {
                        chunk.basePositions = cmd.basePositions;
                        chunk.windInfluence = cmd.windInfluence;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    WorkerDetailChunk wdc;
                    wdc.keyX = cmd.chunkKeyX;
                    wdc.keyZ = cmd.chunkKeyZ;
                    wdc.basePositions = cmd.basePositions;
                    wdc.windInfluence = cmd.windInfluence;
                    detailChunks_.push_back(std::move(wdc));
                }
                break;
            }
            case DetailCommand::RemoveChunk: {
                detailChunks_.erase(
                    std::remove_if(detailChunks_.begin(), detailChunks_.end(),
                        [&cmd](const WorkerDetailChunk& c) {
                            return c.keyX == cmd.chunkKeyX && c.keyZ == cmd.chunkKeyZ;
                        }),
                    detailChunks_.end());
                break;
            }
            case DetailCommand::ClearAll:
                detailChunks_.clear();
                detailResiduals_.clear();
                break;
        }
    }

    if (detailChunks_.empty()) {
        output.detailOutput.chunkShadows.clear();
        output.detailOutput.valid = true;
        return;
    }

    // Advance wind time
    detailWindTime_ += di.deltaTime;

    // Update disturbance residuals
    if (di.disturbanceEnabled) {
        // Add player as source if moving
        if (di.playerMoving) {
            // Grid key for player position (Irrlicht Y-up: X, Y=up, Z=horizontal)
            int32_t gx = static_cast<int32_t>(std::floor(di.playerPosX / 1.0f));
            int32_t gz = static_cast<int32_t>(std::floor(di.playerPosZ / 1.0f));
            int64_t key = (static_cast<int64_t>(gx) << 32) | static_cast<uint32_t>(gz);

            // Push direction from velocity
            float velLen = std::sqrt(di.playerVelX * di.playerVelX + di.playerVelZ * di.playerVelZ);
            float pushDirX = 0, pushDirZ = 1.0f;
            if (velLen > 0.01f) {
                pushDirX = di.playerVelX / velLen;
                pushDirZ = di.playerVelZ / velLen;
            }

            auto it = detailResiduals_.find(key);
            if (it != detailResiduals_.end()) {
                it->second.posX = di.playerPosX;
                it->second.posY = di.playerPosY;
                it->second.posZ = di.playerPosZ;
                it->second.dirX = pushDirX;
                it->second.dirZ = pushDirZ;
                it->second.intensity = std::max(it->second.intensity, di.playerStrength);
            } else if (detailResiduals_.size() < 500) {
                WorkerResidualDisturbance r;
                r.posX = di.playerPosX;
                r.posY = di.playerPosY;
                r.posZ = di.playerPosZ;
                r.dirX = pushDirX;
                r.dirZ = pushDirZ;
                r.intensity = di.playerStrength;
                detailResiduals_[key] = r;
            }
        }

        // Fade residuals
        std::vector<int64_t> toRemove;
        for (auto& [key, res] : detailResiduals_) {
            res.intensity -= di.recoveryRate * di.deltaTime;
            if (res.intensity <= 0.0f) {
                toRemove.push_back(key);
            }
        }
        for (int64_t key : toRemove) {
            detailResiduals_.erase(key);
        }
    }

    // Ensure output has correct number of shadow entries
    output.detailOutput.chunkShadows.resize(detailChunks_.size());

    constexpr float TWO_PI = 6.28318f;

    for (size_t ci = 0; ci < detailChunks_.size(); ++ci) {
        const auto& chunk = detailChunks_[ci];
        auto& shadow = output.detailOutput.chunkShadows[ci];
        shadow.keyX = chunk.keyX;
        shadow.keyZ = chunk.keyZ;

        size_t vertCount = chunk.basePositions.size();
        if (vertCount == 0 || chunk.windInfluence.size() != vertCount) {
            shadow.positions.clear();
            shadow.dirty = false;
            continue;
        }

        shadow.positions.resize(vertCount);

        for (size_t v = 0; v < vertCount; ++v) {
            const auto& basePos = chunk.basePositions[v];
            float influence = chunk.windInfluence[v];

            if (influence < 0.001f) {
                shadow.positions[v] = basePos;
                continue;
            }

            // Wind displacement (replicates WindController::getDisplacement)
            irr::core::vector3df windDisp(0, 0, 0);
            if (di.windStrength > 0.001f) {
                float spatialPhase = (basePos.X * 0.1f + basePos.Z * 0.13f);
                float baseWave = std::sin(detailWindTime_ * di.windFrequency * TWO_PI + spatialPhase);
                float gustWave = std::sin(detailWindTime_ * di.gustFrequency * TWO_PI +
                                          spatialPhase * 0.3f) * di.gustStrength;
                float wave = (baseWave + gustWave) * di.windStrength * influence;
                float heightInfluence = influence * influence;
                wave *= heightInfluence;
                windDisp.X = wave * di.windDirX * 0.15f;
                windDisp.Y = -std::abs(wave) * 0.02f;
                windDisp.Z = wave * di.windDirY * 0.15f;
            }

            // Disturbance displacement
            irr::core::vector3df disturbDisp(0, 0, 0);
            if (di.disturbanceEnabled) {
                float heightScale = std::pow(influence, di.heightExponent);

                // Active source (player) push
                if (di.playerMoving) {
                    float dx = basePos.X - di.playerPosX;
                    float dz = basePos.Z - di.playerPosZ;
                    float dist = std::sqrt(dx * dx + dz * dz);
                    if (dist > 0.01f && dist < di.playerRadius) {
                        float falloff = 1.0f - (dist / di.playerRadius);
                        falloff = falloff * falloff;
                        // Push away from player
                        float pushDirX = dx / dist;
                        float pushDirZ = dz / dist;
                        // Blend with velocity direction
                        float velLen = std::sqrt(di.playerVelX * di.playerVelX + di.playerVelZ * di.playerVelZ);
                        if (velLen > 0.01f) {
                            float velDirX = di.playerVelX / velLen;
                            float velDirZ = di.playerVelZ / velLen;
                            pushDirX += velDirX * di.velocityInfluence;
                            pushDirZ += velDirZ * di.velocityInfluence;
                            float pLen = std::sqrt(pushDirX * pushDirX + pushDirZ * pushDirZ);
                            if (pLen > 0.01f) { pushDirX /= pLen; pushDirZ /= pLen; }
                        }
                        float disp = falloff * di.playerStrength * heightScale;
                        disturbDisp.X += pushDirX * disp * di.maxDisplacement;
                        disturbDisp.Z += pushDirZ * disp * di.maxDisplacement;
                        disturbDisp.Y -= disp * di.verticalDipFactor;
                    }
                }

                // Residual push
                float residualRadius = di.playerRadius * 0.8f;
                for (const auto& [rkey, res] : detailResiduals_) {
                    if (res.intensity < 0.01f) continue;
                    float dx = basePos.X - res.posX;
                    float dz = basePos.Z - res.posZ;
                    float dist = std::sqrt(dx * dx + dz * dz);
                    if (dist >= residualRadius) continue;
                    float falloff = 1.0f - (dist / residualRadius);
                    falloff = falloff * falloff;
                    float disp = falloff * res.intensity * heightScale;
                    disturbDisp.X += res.dirX * disp * di.maxDisplacement;
                    disturbDisp.Z += res.dirZ * disp * di.maxDisplacement;
                    disturbDisp.Y -= disp * di.verticalDipFactor;
                }
            }

            shadow.positions[v] = basePos + windDisp + disturbDisp;
        }
        shadow.dirty = true;
    }

    output.detailOutput.valid = true;
}

// ============================================================================
// Vision/Weather Zone Light Color Application
// ============================================================================

void SimulationWorker::applyVisionWeatherToZoneLights(uint8_t visionType, float weatherAmbientModifier) {
    cachedVisionType_ = visionType;
    cachedWeatherAmbientModifier_ = weatherAmbientModifier;

    if (zoneLightBaseColors_.size() != zoneData_.zoneLightNodes.size()) return;

    // Compute vision-based intensity and red shift (same logic as old updateZoneLightColors)
    float intensity = 0.25f;
    float redShift = 0.0f;
    switch (visionType) {
        case 1: // Ultravision
            intensity = 1.0f; break;
        case 2: // Infravision
            intensity = 0.75f; redShift = 0.3f; break;
        default: break;
    }
    intensity *= weatherAmbientModifier;

    // Build set of animated light indices (skip those — computeLightAnimations handles them)
    std::unordered_set<size_t> animatedLights;
    for (const auto& anim : zoneData_.zoneLightAnims) {
        animatedLights.insert(anim.lightIndex);
    }

    // Apply to all non-animated zone lights using original base colors
    for (size_t i = 0; i < zoneData_.zoneLightNodes.size(); ++i) {
        if (animatedLights.count(i)) continue;

        const auto& base = zoneLightBaseColors_[i];
        float r = base.r * intensity;
        float g = base.g * intensity * (1.0f - redShift * 0.5f);
        float b = base.b * intensity * (1.0f - redShift);
        if (redShift > 0.0f) {
            r = std::min(1.0f, r * (1.0f + redShift));
        }
        zoneData_.zoneLightNodes[i].diffuseColor = irr::video::SColorf(r, g, b, 1.0f);
    }
}

// ============================================================================
// Zone Light Animation Computation
// ============================================================================

void SimulationWorker::computeLightAnimations(const SimulationInput& input, SimulationOutput& output) {
    if (zoneData_.zoneLightAnims.empty()) return;

    const size_t animCount = zoneData_.zoneLightAnims.size();

    // Initialize state vectors on first run
    if (lightAnimStates_.size() != animCount) {
        lightAnimStates_.resize(animCount);
    }
    output.zoneLightAnimColors.resize(animCount);

    // Compute vision/weather modifiers
    float intensity = 0.25f;
    float redShift = 0.0f;
    switch (input.visionType) {
        case 1: // Ultravision
            intensity = 1.0f; break;
        case 2: // Infravision
            intensity = 0.75f; redShift = 0.3f; break;
        default: break;
    }
    intensity *= input.weatherAmbientModifier;

    float deltaMs = input.deltaTime * 1000.0f;

    for (size_t i = 0; i < animCount; ++i) {
        const auto& anim = zoneData_.zoneLightAnims[i];
        auto& state = lightAnimStates_[i];
        auto& out = output.zoneLightAnimColors[i];

        out.lightIndex = anim.lightIndex;
        out.updated = false;

        // Advance elapsed time
        state.elapsedMs += deltaMs;
        float sleepMs = static_cast<float>(anim.sleepMs);
        if (sleepMs <= 0.0f) sleepMs = 100.0f;

        if (state.elapsedMs < sleepMs) {
            // No frame change — still output current color
            continue;
        }

        // Advance frame(s), consuming elapsed time
        while (state.elapsedMs >= sleepMs) {
            state.elapsedMs -= sleepMs;
            state.currentFrame = (state.currentFrame + 1) % anim.frameCount;
        }
        out.updated = true;

        uint32_t frame = state.currentFrame;

        // Compute frame color
        float baseR, baseG, baseB;
        if (!anim.frameColors.empty() && frame < anim.frameColors.size()) {
            std::tie(baseR, baseG, baseB) = anim.frameColors[frame];
        } else if (!anim.lightLevels.empty() && frame < anim.lightLevels.size()) {
            float level = anim.lightLevels[frame];
            baseR = anim.baseR * level;
            baseG = anim.baseG * level;
            baseB = anim.baseB * level;
        } else {
            continue;
        }

        // Apply vision/weather modifiers
        float r = baseR * intensity;
        float g = baseG * intensity * (1.0f - redShift * 0.5f);
        float b = baseB * intensity * (1.0f - redShift);
        if (redShift > 0.0f) {
            r = std::min(1.0f, r * (1.0f + redShift));
        }

        out.r = r;
        out.g = g;
        out.b = b;
    }
}

} // namespace Graphics
} // namespace EQT
