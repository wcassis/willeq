#include "client/graphics/environment/storm_cloud_layer.h"
#include "common/logging.h"
#include <irrlicht.h>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace EQT {
namespace Graphics {
namespace Environment {

StormCloudLayer::StormCloudLayer() {
}

StormCloudLayer::~StormCloudLayer() {
    shutdown();
}

bool StormCloudLayer::initialize(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver,
                                  const std::string& eqClientPath) {
    if (initialized_) return true;

    smgr_ = smgr;
    driver_ = driver;
    eqClientPath_ = eqClientPath;

    if (!smgr_ || !driver_) {
        LOG_ERROR(MOD_GRAPHICS, "StormCloudLayer: Scene manager or driver is null");
        return false;
    }

    // Create the dome mesh
    createDomeMesh();

    // Generate all cloud texture frames
    generateCloudTextures();

    if (!domeMesh_ || cloudFrames_.empty()) {
        LOG_ERROR(MOD_GRAPHICS, "StormCloudLayer: Failed to create mesh or textures");
        return false;
    }

    // Create working image for blending
    int size = settings_.textureSize;
    blendImage_ = driver_->createImage(irr::video::ECF_A8R8G8B8,
                                        irr::core::dimension2d<irr::u32>(size, size));

    // Create initial blended texture
    blendedTexture_ = driver_->addTexture("storm_cloud_blended", blendImage_);

    // Create scene node
    domeNode_ = smgr_->addMeshSceneNode(domeMesh_);
    if (!domeNode_) {
        LOG_ERROR(MOD_GRAPHICS, "StormCloudLayer: Failed to create scene node");
        return false;
    }

    // Set up material
    domeNode_->setMaterialType(irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL);
    domeNode_->setMaterialFlag(irr::video::EMF_LIGHTING, false);
    domeNode_->setMaterialFlag(irr::video::EMF_BACK_FACE_CULLING, false);
    domeNode_->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE, false);
    domeNode_->setMaterialTexture(0, blendedTexture_);

    // Start invisible
    domeNode_->setVisible(false);

    // Initialize frame animation
    currentFrame_ = 0;
    nextFrame_ = 1 % static_cast<int>(cloudFrames_.size());
    frameTimer_ = 0.0f;
    blendFactor_ = 0.0f;

    initialized_ = true;
    LOG_INFO(MOD_GRAPHICS, "StormCloudLayer: Initialized with {}x{} texture, {} frames, {} segments",
             settings_.textureSize, settings_.textureSize, cloudFrames_.size(), settings_.domeSegments);
    return true;
}

void StormCloudLayer::shutdown() {
    if (domeNode_) {
        domeNode_->remove();
        domeNode_ = nullptr;
    }

    if (domeMesh_) {
        domeMesh_->drop();
        domeMesh_ = nullptr;
    }

    // Textures are managed by driver, don't drop them
    cloudFrames_.clear();
    blendedTexture_ = nullptr;

    if (blendImage_) {
        blendImage_->drop();
        blendImage_ = nullptr;
    }

    initialized_ = false;
}

void StormCloudLayer::setSettings(const StormCloudSettings& settings) {
    bool needsRebuild = (settings_.domeRadius != settings.domeRadius ||
                         settings_.domeHeight != settings.domeHeight ||
                         settings_.domeSegments != settings.domeSegments ||
                         settings_.textureSize != settings.textureSize ||
                         settings_.octaves != settings.octaves ||
                         settings_.persistence != settings.persistence ||
                         settings_.cloudScale != settings.cloudScale ||
                         settings_.frameCount != settings.frameCount);

    settings_ = settings;

    if (needsRebuild && initialized_) {
        // Rebuild mesh and textures with new settings
        shutdown();
        initialize(smgr_, driver_);
    }
}

void StormCloudLayer::update(float deltaTime, const glm::vec3& playerPos,
                              const glm::vec3& windDirection, float windStrength,
                              int stormIntensity, float timeOfDay) {
    if (!initialized_ || !enabled_ || !settings_.enabled) {
        return;
    }

    // Store time of day for color calculations
    currentTimeOfDay_ = timeOfDay;

    // Decay lightning flash
    if (lightningFlashIntensity_ > 0.0f) {
        lightningFlashIntensity_ -= deltaTime * 3.0f;  // Fast decay
        lightningFlashIntensity_ = std::max(0.0f, lightningFlashIntensity_);
    }

    // Update opacity based on storm intensity
    updateOpacity(deltaTime, stormIntensity);

    // Update frame animation and blending
    if (currentOpacity_ > 0.01f) {
        updateFrameAnimation(deltaTime);
    }

    // Update UV scrolling based on wind
    updateScrolling(deltaTime, windDirection, windStrength);

    // Update mesh node position
    updateMeshNode(playerPos);
}

void StormCloudLayer::setEnabled(bool enabled) {
    enabled_ = enabled;
    if (domeNode_ && !enabled) {
        domeNode_->setVisible(false);
    }
}

void StormCloudLayer::onZoneEnter(const std::string& zoneName, bool isIndoor) {
    isIndoorZone_ = isIndoor;

    // Reset opacity for new zone
    currentOpacity_ = 0.0f;
    targetOpacity_ = 0.0f;

    if (domeNode_) {
        domeNode_->setVisible(false);
    }

    LOG_DEBUG(MOD_GRAPHICS, "StormCloudLayer: Zone enter '{}', indoor={}", zoneName, isIndoor);
}

void StormCloudLayer::onZoneLeave() {
    currentOpacity_ = 0.0f;
    targetOpacity_ = 0.0f;

    if (domeNode_) {
        domeNode_->setVisible(false);
    }
}

std::string StormCloudLayer::getDebugInfo() const {
    std::ostringstream ss;
    ss << "Clouds: " << std::fixed << std::setprecision(2)
       << currentOpacity_ << "/" << targetOpacity_
       << " frame=" << currentFrame_ << "->" << nextFrame_
       << " blend=" << blendFactor_;
    return ss.str();
}

void StormCloudLayer::createDomeMesh() {
    // Create a hemisphere mesh for the cloud dome
    int segments = settings_.domeSegments;
    int rings = segments / 2;  // Half sphere

    // Calculate vertex and index counts
    int vertexCount = (segments + 1) * (rings + 1);
    int indexCount = segments * rings * 6;

    // Create mesh buffer
    irr::scene::SMeshBuffer* buffer = new irr::scene::SMeshBuffer();
    buffer->Vertices.reallocate(vertexCount);
    buffer->Indices.reallocate(indexCount);

    float radius = settings_.domeRadius;
    float height = settings_.domeHeight;

    // Generate vertices
    for (int ring = 0; ring <= rings; ++ring) {
        // Angle from top (0) to equator (PI/2)
        float phi = (static_cast<float>(ring) / rings) * (3.14159f * 0.5f);
        float sinPhi = std::sin(phi);
        float cosPhi = std::cos(phi);

        for (int seg = 0; seg <= segments; ++seg) {
            float theta = (static_cast<float>(seg) / segments) * (3.14159f * 2.0f);
            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);

            // Position on hemisphere
            // In Irrlicht Y is up
            float x = radius * sinPhi * cosTheta;
            float y = height * cosPhi;  // Height scaled separately
            float z = radius * sinPhi * sinTheta;

            // UV coordinates with tiling
            float u = (static_cast<float>(seg) / segments) * settings_.cloudScale;
            float v = (static_cast<float>(ring) / rings) * settings_.cloudScale;

            // Normal pointing inward (we're inside the dome)
            irr::core::vector3df normal(-sinPhi * cosTheta, -cosPhi, -sinPhi * sinTheta);
            normal.normalize();

            irr::video::S3DVertex vertex;
            vertex.Pos = irr::core::vector3df(x, y, z);
            vertex.Normal = normal;
            vertex.TCoords = irr::core::vector2df(u, v);
            vertex.Color = irr::video::SColor(255, 255, 255, 255);

            buffer->Vertices.push_back(vertex);
        }
    }

    // Generate indices (inside-facing triangles)
    for (int ring = 0; ring < rings; ++ring) {
        for (int seg = 0; seg < segments; ++seg) {
            int current = ring * (segments + 1) + seg;
            int next = current + segments + 1;

            // First triangle (reversed winding for inside view)
            buffer->Indices.push_back(current);
            buffer->Indices.push_back(current + 1);
            buffer->Indices.push_back(next);

            // Second triangle
            buffer->Indices.push_back(next);
            buffer->Indices.push_back(current + 1);
            buffer->Indices.push_back(next + 1);
        }
    }

    buffer->recalculateBoundingBox();

    // Create mesh
    irr::scene::SMesh* mesh = new irr::scene::SMesh();
    mesh->addMeshBuffer(buffer);
    mesh->recalculateBoundingBox();
    buffer->drop();

    domeMesh_ = mesh;
}

void StormCloudLayer::generateCloudTextures() {
    cloudFrames_.clear();

    int frameCount = std::max(1, settings_.frameCount);

    for (int i = 0; i < frameCount; ++i) {
        std::string texPath = "data/textures/storm_cloud_" + std::to_string(i) + ".png";
        irr::video::ITexture* tex = driver_->getTexture(texPath.c_str());
        if (!tex) {
            LOG_WARN(MOD_GRAPHICS, "StormCloudLayer: Cloud texture {} not found, disabling storm clouds. Run generate_textures tool.", texPath);
            cloudFrames_.clear();
            enabled_ = false;
            return;
        }
        cloudFrames_.push_back(tex);
    }
    LOG_INFO(MOD_GRAPHICS, "StormCloudLayer: Loaded {} cloud textures", cloudFrames_.size());
}

void StormCloudLayer::updateScrolling(float deltaTime, const glm::vec3& windDirection, float windStrength) {
    // Calculate scroll direction based on wind
    float windX = windDirection.x * settings_.windInfluence + (1.0f - settings_.windInfluence);
    float windY = windDirection.y * settings_.windInfluence;

    // Calculate scroll speed with variance
    float speed = settings_.scrollSpeedBase + settings_.scrollSpeedVariance * windStrength;

    // Update UV offset
    uvOffset_.x += windX * speed * deltaTime;
    uvOffset_.y += windY * speed * deltaTime;

    // Wrap UV offset to prevent precision issues
    uvOffset_.x = std::fmod(uvOffset_.x, 1.0f);
    uvOffset_.y = std::fmod(uvOffset_.y, 1.0f);
}

void StormCloudLayer::updateOpacity(float deltaTime, int stormIntensity) {
    // Determine target opacity based on storm intensity
    if (isIndoorZone_ || stormIntensity < settings_.intensityThreshold) {
        targetOpacity_ = 0.0f;
    } else {
        // Scale opacity with intensity above threshold
        float intensityFactor = static_cast<float>(stormIntensity - settings_.intensityThreshold) /
                               (10.0f - settings_.intensityThreshold);
        targetOpacity_ = settings_.maxOpacity * std::min(1.0f, intensityFactor);
    }

    // Smoothly interpolate current opacity toward target
    if (currentOpacity_ < targetOpacity_) {
        currentOpacity_ += settings_.fadeInSpeed * deltaTime;
        currentOpacity_ = std::min(currentOpacity_, targetOpacity_);
    } else if (currentOpacity_ > targetOpacity_) {
        currentOpacity_ -= settings_.fadeOutSpeed * deltaTime;
        currentOpacity_ = std::max(currentOpacity_, targetOpacity_);
    }
}

void StormCloudLayer::updateFrameAnimation(float deltaTime) {
    if (cloudFrames_.size() <= 1) return;

    frameTimer_ += deltaTime;

    // Calculate blend factor based on frame duration
    float cycleTime = settings_.frameDuration;
    float blendTime = settings_.blendDuration;

    // During hold time, blend is 0 (show current frame)
    // During blend time, blend goes from 0 to 1
    float holdTime = cycleTime - blendTime;

    if (frameTimer_ < holdTime) {
        // Holding on current frame
        blendFactor_ = 0.0f;
    } else {
        // Blending to next frame
        float blendProgress = (frameTimer_ - holdTime) / blendTime;
        blendFactor_ = std::min(1.0f, blendProgress);
    }

    // Advance to next frame when cycle complete
    if (frameTimer_ >= cycleTime) {
        frameTimer_ -= cycleTime;
        currentFrame_ = nextFrame_;
        nextFrame_ = (nextFrame_ + 1) % static_cast<int>(cloudFrames_.size());
        blendFactor_ = 0.0f;
    }

    // Update the blended texture
    updateBlendedTexture();
}

void StormCloudLayer::updateBlendedTexture() {
    if (!blendImage_ || !blendedTexture_ || cloudFrames_.empty()) return;

    int size = settings_.textureSize;

    // Get source textures
    irr::video::ITexture* tex1 = cloudFrames_[currentFrame_];
    irr::video::ITexture* tex2 = cloudFrames_[nextFrame_];

    if (!tex1 || !tex2) return;

    // Lock textures to read pixel data
    // Note: This is slow but works with software renderer
    // For better performance, we'd pre-compute blended frames

    // Get images from textures (requires locking)
    void* data1 = tex1->lock(irr::video::ETLM_READ_ONLY);
    void* data2 = tex2->lock(irr::video::ETLM_READ_ONLY);

    if (!data1 || !data2) {
        if (data1) tex1->unlock();
        if (data2) tex2->unlock();
        return;
    }

    // Blend the textures
    irr::video::ECOLOR_FORMAT format = tex1->getColorFormat();
    int pitch1 = tex1->getPitch();
    int pitch2 = tex2->getPitch();

    float t = blendFactor_;
    float invT = 1.0f - t;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            // Read pixels from both textures
            irr::video::SColor c1, c2;

            if (format == irr::video::ECF_A8R8G8B8) {
                uint32_t* row1 = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(data1) + y * pitch1);
                uint32_t* row2 = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(data2) + y * pitch2);
                c1 = irr::video::SColor(row1[x]);
                c2 = irr::video::SColor(row2[x]);
            } else {
                // Fallback for other formats
                c1 = irr::video::SColor(255, 128, 128, 128);
                c2 = c1;
            }

            // Blend colors
            uint8_t a = static_cast<uint8_t>(c1.getAlpha() * invT + c2.getAlpha() * t);
            uint8_t r = static_cast<uint8_t>(c1.getRed() * invT + c2.getRed() * t);
            uint8_t g = static_cast<uint8_t>(c1.getGreen() * invT + c2.getGreen() * t);
            uint8_t b = static_cast<uint8_t>(c1.getBlue() * invT + c2.getBlue() * t);

            blendImage_->setPixel(x, y, irr::video::SColor(a, r, g, b));
        }
    }

    tex1->unlock();
    tex2->unlock();

    // Update the blended texture from the image
    // Remove old texture and create new one
    driver_->removeTexture(blendedTexture_);
    blendedTexture_ = driver_->addTexture("storm_cloud_blended", blendImage_);

    // Update scene node material
    if (domeNode_ && blendedTexture_) {
        domeNode_->setMaterialTexture(0, blendedTexture_);
    }
}

void StormCloudLayer::updateMeshNode(const glm::vec3& playerPos) {
    if (!domeNode_) return;

    // Show/hide based on opacity
    bool shouldBeVisible = currentOpacity_ > 0.01f;
    domeNode_->setVisible(shouldBeVisible);

    if (!shouldBeVisible) return;

    // Position dome centered on player (EQ to Irrlicht coordinate conversion)
    domeNode_->setPosition(irr::core::vector3df(playerPos.x, playerPos.z, playerPos.y));

    // Calculate time-of-day brightness factor using config settings
    // Night: very dark clouds (only visible during lightning)
    // Dawn/Dusk: transitional brightness
    // Day: configured brightness (storm clouds darken the scene)
    float brightness = settings_.dayBrightness;
    float nightBright = settings_.nightBrightness;
    float dayBright = settings_.dayBrightness;
    float brightRange = dayBright - nightBright;

    if (currentTimeOfDay_ < settings_.dawnStartHour || currentTimeOfDay_ >= settings_.duskEndHour) {
        // Night: use night brightness
        brightness = nightBright;
    } else if (currentTimeOfDay_ < settings_.dawnEndHour) {
        // Dawn: fade from night to day
        float dawnDuration = settings_.dawnEndHour - settings_.dawnStartHour;
        float t = (currentTimeOfDay_ - settings_.dawnStartHour) / dawnDuration;
        brightness = nightBright + t * brightRange;
    } else if (currentTimeOfDay_ >= settings_.duskStartHour) {
        // Dusk: fade from day to night
        float duskDuration = settings_.duskEndHour - settings_.duskStartHour;
        float t = (currentTimeOfDay_ - settings_.duskStartHour) / duskDuration;
        brightness = dayBright - t * brightRange;
    }

    // Lightning flash temporarily illuminates clouds
    float flashBrightness = lightningFlashIntensity_ * settings_.lightningFlashMultiplier;
    float totalBrightness = std::min(1.0f, brightness + flashBrightness);

    // Update material with current opacity and UV offset
    irr::video::SMaterial& material = domeNode_->getMaterial(0);

    // Set vertex color with time-adjusted brightness
    // The cloud texture has alpha for transparency, we modulate RGB for brightness
    uint8_t brightnessVal = static_cast<uint8_t>(totalBrightness * 255.0f);
    uint8_t alpha = static_cast<uint8_t>(currentOpacity_ * 255.0f);
    material.DiffuseColor = irr::video::SColor(alpha, brightnessVal, brightnessVal, brightnessVal);

    // Also set ambient and emissive to control how the clouds appear
    // At night, clouds should block light (appear as dark shapes), not emit light
    material.AmbientColor = irr::video::SColor(255, brightnessVal, brightnessVal, brightnessVal);
    material.EmissiveColor = irr::video::SColor(0, 0, 0, 0);  // No self-illumination

    // Apply UV offset via texture matrix
    irr::core::matrix4 texMatrix;
    texMatrix.setTextureTranslate(uvOffset_.x, uvOffset_.y);
    material.setTextureMatrix(0, texMatrix);
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
