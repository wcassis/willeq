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

bool StormCloudLayer::initialize(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver) {
    if (initialized_) return true;

    smgr_ = smgr;
    driver_ = driver;

    if (!smgr_ || !driver_) {
        LOG_ERROR(MOD_GRAPHICS, "StormCloudLayer: Scene manager or driver is null");
        return false;
    }

    // Create the dome mesh
    createDomeMesh();

    // Generate procedural cloud texture
    generateCloudTexture();

    if (!domeMesh_ || !cloudTexture_) {
        LOG_ERROR(MOD_GRAPHICS, "StormCloudLayer: Failed to create mesh or texture");
        return false;
    }

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
    domeNode_->setMaterialTexture(0, cloudTexture_);

    // Start invisible
    domeNode_->setVisible(false);

    initialized_ = true;
    LOG_INFO(MOD_GRAPHICS, "StormCloudLayer: Initialized with {}x{} texture, {} segments",
             settings_.textureSize, settings_.textureSize, settings_.domeSegments);
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

    // Texture is managed by driver, don't drop it
    cloudTexture_ = nullptr;

    initialized_ = false;
}

void StormCloudLayer::setSettings(const StormCloudSettings& settings) {
    bool needsRebuild = (settings_.domeRadius != settings.domeRadius ||
                         settings_.domeHeight != settings.domeHeight ||
                         settings_.domeSegments != settings.domeSegments ||
                         settings_.textureSize != settings.textureSize ||
                         settings_.octaves != settings.octaves ||
                         settings_.persistence != settings.persistence ||
                         settings_.cloudScale != settings.cloudScale);

    settings_ = settings;

    if (needsRebuild && initialized_) {
        // Rebuild mesh and texture with new settings
        if (domeNode_) {
            domeNode_->remove();
            domeNode_ = nullptr;
        }
        if (domeMesh_) {
            domeMesh_->drop();
            domeMesh_ = nullptr;
        }

        createDomeMesh();
        generateCloudTexture();

        if (domeMesh_ && cloudTexture_) {
            domeNode_ = smgr_->addMeshSceneNode(domeMesh_);
            if (domeNode_) {
                domeNode_->setMaterialType(irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL);
                domeNode_->setMaterialFlag(irr::video::EMF_LIGHTING, false);
                domeNode_->setMaterialFlag(irr::video::EMF_BACK_FACE_CULLING, false);
                domeNode_->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE, false);
                domeNode_->setMaterialTexture(0, cloudTexture_);
            }
        }
    }
}

void StormCloudLayer::update(float deltaTime, const glm::vec3& playerPos,
                              const glm::vec3& windDirection, float windStrength,
                              int stormIntensity) {
    if (!initialized_ || !enabled_ || !settings_.enabled) {
        return;
    }

    // Update opacity based on storm intensity
    updateOpacity(deltaTime, stormIntensity);

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
       << " UV(" << uvOffset_.x << "," << uvOffset_.y << ")";
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

void StormCloudLayer::generateCloudTexture() {
    int size = settings_.textureSize;

    // Create image
    irr::video::IImage* image = driver_->createImage(irr::video::ECF_A8R8G8B8,
                                                      irr::core::dimension2d<irr::u32>(size, size));
    if (!image) {
        LOG_ERROR(MOD_GRAPHICS, "StormCloudLayer: Failed to create image");
        return;
    }

    // Generate cloud pattern using Perlin noise
    uint8_t r = static_cast<uint8_t>(settings_.cloudColorR * 255.0f);
    uint8_t g = static_cast<uint8_t>(settings_.cloudColorG * 255.0f);
    uint8_t b = static_cast<uint8_t>(settings_.cloudColorB * 255.0f);

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            // Scale coordinates for noise
            float nx = static_cast<float>(x) / size * 4.0f;
            float ny = static_cast<float>(y) / size * 4.0f;

            // Get noise value (0 to 1)
            float noise = perlinNoise2D(nx, ny, settings_.octaves, settings_.persistence);

            // Apply cloud shaping - make it more cloud-like
            // Clamp and enhance contrast
            noise = std::max(0.0f, noise - 0.3f) * 1.5f;
            noise = std::min(1.0f, noise);

            // Alpha based on noise (clouds are transparent where noise is low)
            uint8_t alpha = static_cast<uint8_t>(noise * 255.0f);

            image->setPixel(x, y, irr::video::SColor(alpha, r, g, b));
        }
    }

    // Create texture from image
    cloudTexture_ = driver_->addTexture("storm_cloud_procedural", image);
    image->drop();

    if (cloudTexture_) {
        LOG_DEBUG(MOD_GRAPHICS, "StormCloudLayer: Generated {}x{} cloud texture", size, size);
    }
}

float StormCloudLayer::noise2D(float x, float y) const {
    // Simple hash-based noise
    int xi = static_cast<int>(std::floor(x)) & 255;
    int yi = static_cast<int>(std::floor(y)) & 255;

    // Hash function
    int n = xi + yi * 57 + noiseSeed_;
    n = (n << 13) ^ n;
    return (1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);
}

float StormCloudLayer::smoothNoise2D(float x, float y) const {
    // Corners
    float corners = (noise2D(x - 1, y - 1) + noise2D(x + 1, y - 1) +
                     noise2D(x - 1, y + 1) + noise2D(x + 1, y + 1)) / 16.0f;
    // Sides
    float sides = (noise2D(x - 1, y) + noise2D(x + 1, y) +
                   noise2D(x, y - 1) + noise2D(x, y + 1)) / 8.0f;
    // Center
    float center = noise2D(x, y) / 4.0f;

    return corners + sides + center;
}

float StormCloudLayer::interpolatedNoise2D(float x, float y) const {
    int intX = static_cast<int>(std::floor(x));
    int intY = static_cast<int>(std::floor(y));
    float fracX = x - intX;
    float fracY = y - intY;

    // Cosine interpolation
    float fx = (1.0f - std::cos(fracX * 3.14159f)) * 0.5f;
    float fy = (1.0f - std::cos(fracY * 3.14159f)) * 0.5f;

    float v1 = smoothNoise2D(static_cast<float>(intX), static_cast<float>(intY));
    float v2 = smoothNoise2D(static_cast<float>(intX + 1), static_cast<float>(intY));
    float v3 = smoothNoise2D(static_cast<float>(intX), static_cast<float>(intY + 1));
    float v4 = smoothNoise2D(static_cast<float>(intX + 1), static_cast<float>(intY + 1));

    float i1 = v1 * (1.0f - fx) + v2 * fx;
    float i2 = v3 * (1.0f - fx) + v4 * fx;

    return i1 * (1.0f - fy) + i2 * fy;
}

float StormCloudLayer::perlinNoise2D(float x, float y, int octaves, float persistence) const {
    float total = 0.0f;
    float frequency = 1.0f;
    float amplitude = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        total += interpolatedNoise2D(x * frequency, y * frequency) * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= 2.0f;
    }

    // Normalize to 0-1 range
    return (total / maxValue + 1.0f) * 0.5f;
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

void StormCloudLayer::updateMeshNode(const glm::vec3& playerPos) {
    if (!domeNode_) return;

    // Show/hide based on opacity
    bool shouldBeVisible = currentOpacity_ > 0.01f;
    domeNode_->setVisible(shouldBeVisible);

    if (!shouldBeVisible) return;

    // Position dome centered on player (EQ to Irrlicht coordinate conversion)
    domeNode_->setPosition(irr::core::vector3df(playerPos.x, playerPos.z, playerPos.y));

    // Update material with current opacity and UV offset
    irr::video::SMaterial& material = domeNode_->getMaterial(0);

    // Set vertex alpha for transparency
    uint8_t alpha = static_cast<uint8_t>(currentOpacity_ * 255.0f);
    material.DiffuseColor.setAlpha(alpha);

    // Apply UV offset via texture matrix
    irr::core::matrix4 texMatrix;
    texMatrix.setTextureTranslate(uvOffset_.x, uvOffset_.y);
    material.setTextureMatrix(0, texMatrix);
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
