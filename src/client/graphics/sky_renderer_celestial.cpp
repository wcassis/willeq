// sky_renderer_celestial.cpp - celestial bodies, cloud layers and the
// per-frame dome updates.
// Dome vertex alpha, mesh construction from zone geometry, celestial body
// creation / positioning / sizing, sky visibility, sky node teardown and
// the current-colour and fog accessors.
// Split move-only from the original 1,770-line sky_renderer.cpp by
// refactor/J30_sky_renderer_split.md; companions are sky_renderer.cpp and
// sky_renderer_colors.cpp.
#include "client/graphics/sky_renderer.h"
#include "client/graphics/eq/sky_loader.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/sky_config.h"
#include "common/logging.h"
#include <cmath>
#include <algorithm>

#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
// Bridge function defined in COpenGLES2Driver.cpp
extern void* gles2WrapTexture(void* driver, const char* name, unsigned int glTexName,
                                unsigned int width, unsigned int height,
                                unsigned int gpuBytes = 0);
#endif

namespace EQT {
namespace Graphics {
void SkyRenderer::updateDomeVertexAlpha() {
    if (!skyDomeMeshBuffer_) {
        return;
    }

    // Compute camera-height-interpolated parameters
    float cameraZ = lastCameraZ_;
    float camRange = currentZoneConfig_.maxCameraZ - currentZoneConfig_.minCameraZ;
    float t = 0.0f;
    if (camRange != 0.0f) {
        t = (cameraZ - currentZoneConfig_.minCameraZ) / camRange;
        t = std::max(0.0f, std::min(1.0f, t));
    }

    float interpAngle = currentZoneConfig_.minAngle + (currentZoneConfig_.maxAngle - currentZoneConfig_.minAngle) * t;
    float interpWidth = currentZoneConfig_.minWidth + (currentZoneConfig_.maxWidth - currentZoneConfig_.minWidth) * t;

    float effectiveOpaqueAngle = currentZoneConfig_.opaqueAngle + interpAngle;
    float effectiveFadeWidth = currentZoneConfig_.transparentAngle + interpWidth;
    float effectiveTransparentBoundary = effectiveOpaqueAngle - effectiveFadeWidth;

    // Update vertex alpha based on position-derived pitch angle.
    // Uses atan2(Y, horizontalDist) which works for both procedural and WLD dome topologies,
    // since both are centered at origin in Irrlicht Y-up coordinates.
    const int vertCount = skyDomeMeshBuffer_->Vertices.size();
    for (int i = 0; i < vertCount; ++i) {
        irr::video::S3DVertex& vert = skyDomeMeshBuffer_->Vertices[i];
        // Compute pitch from vertex position (Irrlicht Y-up)
        float hDist = std::sqrt(vert.Pos.X * vert.Pos.X + vert.Pos.Z * vert.Pos.Z);
        float pitch = std::atan2(vert.Pos.Y, hDist);

        // Compute alpha based on pitch relative to fade zone
        irr::u32 alpha;
        if (pitch >= effectiveOpaqueAngle) {
            alpha = 255;
        } else if (pitch <= effectiveTransparentBoundary) {
            alpha = 0;
        } else {
            float fadeRange = effectiveOpaqueAngle - effectiveTransparentBoundary;
            alpha = (fadeRange > 0.0001f)
                ? static_cast<irr::u32>(255.0f * (pitch - effectiveTransparentBoundary) / fadeRange)
                : 255;
        }
        vert.Color.setAlpha(alpha);
    }

    // Mark buffer dirty so Irrlicht re-uploads vertices to GPU
    skyDomeMeshBuffer_->setDirty(irr::scene::EBT_VERTEX);
}

irr::scene::SMesh* SkyRenderer::createMeshFromGeometry(const ZoneGeometry* geometry) {
    if (!geometry || geometry->vertices.empty() || geometry->triangles.empty()) {
        return nullptr;
    }

    irr::scene::SMesh* mesh = new irr::scene::SMesh();
    irr::scene::SMeshBuffer* buffer = new irr::scene::SMeshBuffer();

    // Convert vertices
    buffer->Vertices.set_used(geometry->vertices.size());
    for (size_t i = 0; i < geometry->vertices.size(); ++i) {
        const Vertex3D& v = geometry->vertices[i];
        irr::video::S3DVertex& iv = buffer->Vertices[i];

        // EQ to Irrlicht coordinate conversion: (x, y, z) -> (x, z, y)
        iv.Pos.X = v.x;
        iv.Pos.Y = v.z;
        iv.Pos.Z = v.y;

        iv.Normal.X = v.nx;
        iv.Normal.Y = v.nz;
        iv.Normal.Z = v.ny;

        iv.TCoords.X = v.u;
        iv.TCoords.Y = v.v;

        iv.Color = irr::video::SColor(255, 255, 255, 255);
    }

    // Convert triangles to indices
    buffer->Indices.set_used(geometry->triangles.size() * 3);
    for (size_t i = 0; i < geometry->triangles.size(); ++i) {
        const Triangle& tri = geometry->triangles[i];
        buffer->Indices[i * 3 + 0] = tri.v1;
        buffer->Indices[i * 3 + 1] = tri.v2;
        buffer->Indices[i * 3 + 2] = tri.v3;
    }

    buffer->recalculateBoundingBox();
    mesh->addMeshBuffer(buffer);
    mesh->recalculateBoundingBox();
    buffer->drop();

    return mesh;
}

void SkyRenderer::createCelestialBodies() {
    if (!initialized_ || !smgr_) {
        return;
    }

    const auto& skyData = skyLoader_->getSkyData();
    if (!skyData) {
        return;
    }

    // Find sun and moon in celestial bodies
    for (const auto& [name, body] : skyData->celestialBodies) {
        if (!body) continue;

        if (body->isSun && !sunNode_) {
            // Store track data for orbital animation
            sunTrack_ = body->track;

            // Create sun glow billboard (larger, behind sun, additive blending)
            sunGlowNode_ = smgr_->addBillboardSceneNode(
                nullptr,
                irr::core::dimension2d<irr::f32>(SUN_BASE_SIZE * GLOW_SIZE_MULTIPLIER,
                                                  SUN_BASE_SIZE * GLOW_SIZE_MULTIPLIER),
                irr::core::vector3df(0, CELESTIAL_DISTANCE - 10.0f, 0)  // Slightly behind sun
            );

            if (sunGlowNode_) {
                // Use same sun texture for glow but with lower opacity effect
                irr::video::ITexture* texture = loadSkyTexture(body->textureName);
                if (texture) {
                    sunGlowNode_->setMaterialTexture(0, texture);
                }
                sunGlowNode_->setMaterialFlag(irr::video::EMF_LIGHTING, false);
                sunGlowNode_->setMaterialFlag(irr::video::EMF_ZBUFFER, true);
                sunGlowNode_->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE, false);
                sunGlowNode_->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);
                // Reduce glow intensity with vertex color
                sunGlowNode_->getMaterial(0).DiffuseColor = irr::video::SColor(100, 255, 200, 100);
            }

            // Create sun billboard
            sunNode_ = smgr_->addBillboardSceneNode(
                nullptr,
                irr::core::dimension2d<irr::f32>(SUN_BASE_SIZE, SUN_BASE_SIZE),
                irr::core::vector3df(0, CELESTIAL_DISTANCE, 0)
            );

            if (sunNode_) {
                irr::video::ITexture* texture = loadSkyTexture(body->textureName);
                if (texture) {
                    sunNode_->setMaterialTexture(0, texture);
                }
                sunNode_->setMaterialFlag(irr::video::EMF_LIGHTING, false);
                sunNode_->setMaterialFlag(irr::video::EMF_ZBUFFER, true);
                sunNode_->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE, false);
                sunNode_->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);

                LOG_DEBUG(MOD_GRAPHICS, "Created sun billboard (texture: {}, hasTrack: {})",
                         body->textureName, sunTrack_ ? "yes" : "no");
            }
        }
        else if (body->isMoon && !moonNode_) {
            // Store track data for orbital animation
            moonTrack_ = body->track;

            // Create moon billboard
            moonNode_ = smgr_->addBillboardSceneNode(
                nullptr,
                irr::core::dimension2d<irr::f32>(MOON_BASE_SIZE, MOON_BASE_SIZE),
                irr::core::vector3df(0, CELESTIAL_DISTANCE, 0)
            );

            if (moonNode_) {
                irr::video::ITexture* texture = loadSkyTexture(body->textureName);
                if (texture) {
                    moonNode_->setMaterialTexture(0, texture);
                }
                moonNode_->setMaterialFlag(irr::video::EMF_LIGHTING, false);
                moonNode_->setMaterialFlag(irr::video::EMF_ZBUFFER, true);
                moonNode_->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE, false);
                // Use ADD_COLOR like sun - BMP textures don't have alpha channel
                // This makes bright parts of moon visible against dark sky
                moonNode_->setMaterialType(irr::video::EMT_TRANSPARENT_ADD_COLOR);

                LOG_DEBUG(MOD_GRAPHICS, "Created moon billboard (texture: {}, hasTrack: {})",
                         body->textureName, moonTrack_ ? "yes" : "no");
            }
        }
    }
}

irr::video::ITexture* SkyRenderer::loadSkyTexture(const std::string& name) {
    if (name.empty()) {
        return nullptr;
    }

    // Cache-only lookup — all textures must be pre-decoded on background thread
    // and uploaded via constrained cache getOrLoad() + uploadTexture() during Step 10.
    auto it = textureCache_.find(name);
    if (it != textureCache_.end()) {
        return it->second;
    }

    LOG_WARN(MOD_GRAPHICS, "loadSkyTexture: '{}' not in cache — pre-decode path failed, skipping", name);
    return nullptr;
}

void SkyRenderer::updateCelestialPositions() {
    if (!initialized_) {
        return;
    }

    // Calculate time as decimal hours (0.0 - 24.0)
    float timeOfDay = static_cast<float>(currentHour_) + static_cast<float>(currentMinute_) / 60.0f;

    // Update sun position (relative to camera)
    if (sunNode_) {
        irr::core::vector3df sunPos;

        // Use track data if available, otherwise use calculated arc
        if (sunTrack_ && !sunTrack_->keyframes.empty()) {
            sunPos = calculateTrackPosition(sunTrack_, timeOfDay);
        } else {
            sunPos = calculateSunPosition(timeOfDay);
        }

        // Position relative to camera so sun appears infinitely far away
        sunNode_->setPosition(lastCameraPos_ + sunPos);

        // Update glow position (slightly behind sun)
        if (sunGlowNode_) {
            irr::core::vector3df glowPos = sunPos;
            // Move glow slightly back (toward camera center)
            glowPos = glowPos * 0.998f;
            sunGlowNode_->setPosition(lastCameraPos_ + glowPos);
        }

        // Hide sun at night (below horizon)
        bool sunVisible = (timeOfDay >= 5.0f && timeOfDay <= 20.0f);
        sunNode_->setVisible(sunVisible && enabled_);
        if (sunGlowNode_) {
            sunGlowNode_->setVisible(sunVisible && enabled_);
        }
    }

    // Update moon position (relative to camera)
    if (moonNode_) {
        irr::core::vector3df moonPos;

        // Use track data if available, otherwise use calculated arc
        if (moonTrack_ && !moonTrack_->keyframes.empty()) {
            moonPos = calculateTrackPosition(moonTrack_, timeOfDay);
        } else {
            moonPos = calculateMoonPosition(timeOfDay);
        }

        // Position relative to camera so moon appears infinitely far away
        moonNode_->setPosition(lastCameraPos_ + moonPos);

        // Moon visible at night
        bool moonVisible = (timeOfDay < 6.0f || timeOfDay > 19.0f);
        moonNode_->setVisible(moonVisible && enabled_);

        // Debug logging for moon position (throttled)
        static int moonDebugCounter = 0;
        if (++moonDebugCounter >= 300) {  // Log every ~10 seconds at 30fps
            moonDebugCounter = 0;
            LOG_DEBUG(MOD_GRAPHICS, "Moon: time={:.2f} visible={} enabled={} pos=({:.1f},{:.1f},{:.1f}) cam=({:.1f},{:.1f},{:.1f})",
                      timeOfDay, moonVisible, enabled_,
                      moonPos.X, moonPos.Y, moonPos.Z,
                      lastCameraPos_.X, lastCameraPos_.Y, lastCameraPos_.Z);
        }
    }

    // Update sizes based on elevation (larger near horizon)
    updateCelestialSizes();
}

irr::core::vector3df SkyRenderer::calculateSunPosition(float hour) const {
    // Sun rises at 6:00, peaks at 12:00, sets at 18:00
    // Map hour to arc position (0 = sunrise at east, 0.5 = noon at zenith, 1 = sunset at west)

    float dayProgress = 0.0f;
    if (hour >= 6.0f && hour <= 18.0f) {
        dayProgress = (hour - 6.0f) / 12.0f; // 0 at 6am, 1 at 6pm
    } else {
        // Sun below horizon
        return irr::core::vector3df(0, -CELESTIAL_DISTANCE, 0);
    }

    // Calculate position on arc
    // Azimuth: 0 = east, PI/2 = south, PI = west
    float azimuth = dayProgress * irr::core::PI;

    // Elevation: 0 at horizon, peaks at PI/2 (zenith) at noon
    float elevation = std::sin(dayProgress * irr::core::PI) * (irr::core::PI / 3.0f); // Max 60 degrees

    // Convert spherical to cartesian (Irrlicht Y-up)
    float x = CELESTIAL_DISTANCE * std::cos(elevation) * std::sin(azimuth);
    float y = CELESTIAL_DISTANCE * std::sin(elevation);
    float z = CELESTIAL_DISTANCE * std::cos(elevation) * std::cos(azimuth);

    return irr::core::vector3df(x, y, z);
}

irr::core::vector3df SkyRenderer::calculateMoonPosition(float hour) const {
    // Moon is roughly opposite to sun (offset by ~12 hours)
    // Simplified: moon rises at 18:00, peaks at midnight, sets at 6:00

    float nightProgress = 0.0f;
    if (hour >= 18.0f) {
        nightProgress = (hour - 18.0f) / 12.0f;
    } else if (hour <= 6.0f) {
        nightProgress = (hour + 6.0f) / 12.0f;
    } else {
        // Moon below horizon during day
        return irr::core::vector3df(0, -CELESTIAL_DISTANCE, 0);
    }

    // Calculate position on arc (similar to sun but offset)
    float azimuth = nightProgress * irr::core::PI;
    float elevation = std::sin(nightProgress * irr::core::PI) * (irr::core::PI / 4.0f); // Max 45 degrees

    // Convert spherical to cartesian
    float x = CELESTIAL_DISTANCE * std::cos(elevation) * std::sin(azimuth);
    float y = CELESTIAL_DISTANCE * std::sin(elevation);
    float z = -CELESTIAL_DISTANCE * std::cos(elevation) * std::cos(azimuth); // Negative Z for opposite direction

    return irr::core::vector3df(x, y, z);
}

irr::core::vector3df SkyRenderer::calculateTrackPosition(const std::shared_ptr<SkyTrack>& track, float hour) const {
    if (!track || track->keyframes.empty()) {
        return irr::core::vector3df(0, CELESTIAL_DISTANCE, 0);
    }

    // Map hour (0-24) to track progress (0-1)
    // Assume track represents a full day cycle
    float progress = hour / 24.0f;

    // Find the keyframes to interpolate between
    size_t frameCount = track->keyframes.size();
    float frameIndex = progress * static_cast<float>(frameCount);
    size_t frame0 = static_cast<size_t>(frameIndex) % frameCount;
    size_t frame1 = (frame0 + 1) % frameCount;
    float t = frameIndex - std::floor(frameIndex);  // Interpolation factor

    const SkyTrackKeyframe& kf0 = track->keyframes[frame0];
    const SkyTrackKeyframe& kf1 = track->keyframes[frame1];

    // Celestial body tracks use rotation to position on sky dome, not translation
    // The quaternion rotates a base direction vector to get the position
    glm::quat rot = glm::slerp(kf0.rotation, kf1.rotation, t);

    // Base direction is straight up (0, 0, 1) in EQ coordinates (Z-up)
    // Rotate it by the track quaternion to get the celestial body direction
    glm::vec3 baseDir(0.0f, 0.0f, 1.0f);
    glm::vec3 dir = rot * baseDir;

    // Scale to celestial distance and convert EQ to Irrlicht coordinates
    // EQ: (x, y, z) Z-up -> Irrlicht: (x, z, y) Y-up
    return irr::core::vector3df(
        dir.x * CELESTIAL_DISTANCE,
        dir.z * CELESTIAL_DISTANCE,  // EQ Z -> Irrlicht Y
        dir.y * CELESTIAL_DISTANCE   // EQ Y -> Irrlicht Z
    );
}

float SkyRenderer::calculateCelestialSize(float baseSize, float elevation) const {
    // Apply "moon illusion" effect - celestial bodies appear larger near horizon
    // elevation: Y component of position (higher = more elevated)
    // Normalize elevation to 0-1 range (0 = horizon, 1 = zenith)

    float normalizedElevation = std::abs(elevation) / CELESTIAL_DISTANCE;
    normalizedElevation = irr::core::clamp(normalizedElevation, 0.0f, 1.0f);

    // Invert so that low elevation = larger size
    float sizeMultiplier = SIZE_SCALE_MAX - (SIZE_SCALE_MAX - SIZE_SCALE_MIN) * normalizedElevation;

    return baseSize * sizeMultiplier;
}

void SkyRenderer::updateCelestialSizes() {
    if (!initialized_) {
        return;
    }

    // Update sun size based on elevation
    if (sunNode_) {
        irr::core::vector3df sunPos = sunNode_->getPosition();
        float sunSize = calculateCelestialSize(SUN_BASE_SIZE, sunPos.Y);
        sunNode_->setSize(irr::core::dimension2d<irr::f32>(sunSize, sunSize));

        // Update glow size proportionally
        if (sunGlowNode_) {
            float glowSize = sunSize * GLOW_SIZE_MULTIPLIER;
            sunGlowNode_->setSize(irr::core::dimension2d<irr::f32>(glowSize, glowSize));
        }
    }

    // Update moon size based on elevation
    if (moonNode_) {
        irr::core::vector3df moonPos = moonNode_->getPosition();
        float moonSize = calculateCelestialSize(MOON_BASE_SIZE, moonPos.Y);
        moonNode_->setSize(irr::core::dimension2d<irr::f32>(moonSize, moonSize));
    }
}

void SkyRenderer::updateSkyVisibility() {
    // Update legacy sky dome nodes (if any)
    for (auto* node : skyDomeNodes_) {
        if (node) {
            node->setVisible(enabled_);
        }
    }

    // Update custom dome mesh node
    if (skyDomeMeshNode_) {
        skyDomeMeshNode_->setVisible(enabled_);
    }

    if (sunNode_) {
        // Sun visibility also depends on time
        float timeOfDay = static_cast<float>(currentHour_) + static_cast<float>(currentMinute_) / 60.0f;
        bool sunVisible = enabled_ && (timeOfDay >= 5.0f && timeOfDay <= 20.0f);
        sunNode_->setVisible(sunVisible);
        if (sunGlowNode_) {
            sunGlowNode_->setVisible(sunVisible);
        }
    }

    if (moonNode_) {
        float timeOfDay = static_cast<float>(currentHour_) + static_cast<float>(currentMinute_) / 60.0f;
        bool moonVisible = enabled_ && (timeOfDay < 6.0f || timeOfDay > 19.0f);
        moonNode_->setVisible(moonVisible);
    }
}

void SkyRenderer::clearSkyNodes() {
    // Remove legacy sky dome nodes (if any)
    for (auto* node : skyDomeNodes_) {
        if (node) {
            node->remove();
        }
    }
    skyDomeNodes_.clear();
    cloudLayerNodes_.clear();  // Cloud nodes are subset of skyDomeNodes_, already removed

    // Remove custom dome mesh node
    if (skyDomeMeshNode_) {
        skyDomeMeshNode_->remove();
        skyDomeMeshNode_ = nullptr;
        skyDomeMeshBuffer_ = nullptr; // owned by mesh, already freed
    }

    if (sunNode_) {
        sunNode_->remove();
        sunNode_ = nullptr;
    }

    if (sunGlowNode_) {
        sunGlowNode_->remove();
        sunGlowNode_ = nullptr;
    }

    if (moonNode_) {
        moonNode_->remove();
        moonNode_ = nullptr;
    }

    // Clear track references
    sunTrack_.reset();
    moonTrack_.reset();
}

SkyColorSet SkyRenderer::getCurrentSkyColors() const {
    return currentSkyColors_;
}

irr::video::SColor SkyRenderer::getRecommendedFogColor() const {
    return currentSkyColors_.fog;
}

} // namespace Graphics
} // namespace EQT
