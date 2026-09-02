// sky_renderer.cpp - sky dome construction and geometry.
// Constructor, teardown, initialization and sky-type selection, the dome
// mesh precompute, dome creation from precomputed and from WLD geometry,
// and the custom sky dome builder.
// Split move-only from the original 1,770-line sky_renderer.cpp by
// refactor/J30_sky_renderer_split.md; companions are
// sky_renderer_celestial.cpp and sky_renderer_colors.cpp.
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

SkyRenderer::SkyRenderer(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver,
                         irr::io::IFileSystem* fileSystem)
    : smgr_(smgr)
    , driver_(driver)
    , fileSystem_(fileSystem)
{
}

SkyRenderer::~SkyRenderer() {
    if (constrainedCache_) {
        constrainedCache_->removeEvictionListener(this);
    }
    clearSkyNodes();
    // Don't call driver_->removeTexture() — cache owns the textures now
    textureCache_.clear();
}

void SkyRenderer::setConstrainedTextureCache(ConstrainedTextureCache* cache) {
    if (constrainedCache_) {
        constrainedCache_->removeEvictionListener(this);
    }
    constrainedCache_ = cache;
    if (constrainedCache_) {
        constrainedCache_->addEvictionListener(this);
    }
}

void SkyRenderer::onTextureEvicted(const std::string& name, irr::video::ITexture* /*texture*/) {
    auto it = textureCache_.find(name);
    if (it != textureCache_.end()) {
        it->second = nullptr;
    }
}

bool SkyRenderer::initialize(const std::string& eqClientPath) {
    if (initialized_) {
        return true;
    }

    // Create loaders
    skyLoader_ = std::make_unique<SkyLoader>();
    skyConfig_ = std::make_unique<SkyConfig>();

    // Load sky.s3d (skyLoader adds /sky.s3d to the path)
    if (!skyLoader_->load(eqClientPath)) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to load sky.s3d from: {}", eqClientPath);
        return false;
    }

    // Load sky.ini
    std::string skyIniPath = eqClientPath + "/sky.ini";
    if (!skyConfig_->loadFromFile(skyIniPath)) {
        LOG_WARN(MOD_GRAPHICS, "Failed to load sky.ini from: {}, using defaults", skyIniPath);
        // Continue without ini - will use defaults
    }

    const auto& skyData = skyLoader_->getSkyData();
    if (skyData) {
        LOG_INFO(MOD_GRAPHICS, "SkyRenderer initialized: {} layers, {} celestial bodies",
                 skyData->layers.size(),
                 skyData->celestialBodies.size());
    }

    initialized_ = true;
    return true;
}

bool SkyRenderer::initializeFromPreloaded(std::unique_ptr<SkyLoader> loader,
                                           std::unique_ptr<SkyConfig> config) {
    if (initialized_) return true;

    skyLoader_ = std::move(loader);
    skyConfig_ = std::move(config);

    if (!skyLoader_ || !skyLoader_->getSkyData()) {
        LOG_ERROR(MOD_GRAPHICS, "SkyRenderer: pre-loaded sky data is null");
        return false;
    }

    const auto& skyData = skyLoader_->getSkyData();
    LOG_INFO(MOD_GRAPHICS, "SkyRenderer initialized from preloaded data: {} layers, {} celestial bodies",
             skyData->layers.size(), skyData->celestialBodies.size());

    initialized_ = true;
    return true;
}

void SkyRenderer::setSkyType(uint8_t skyTypeId, const std::string& zoneName) {
    prepareSkyType(skyTypeId, zoneName);
    applySkyType();
}

void SkyRenderer::prepareSkyType(uint8_t skyTypeId, const std::string& zoneName) {
    skyPrepared_ = false;

    if (!initialized_) {
        return;
    }

    // Check if sky is enabled for this zone
    if (!skyConfig_->isSkyEnabledForZone(zoneName)) {
        LOG_DEBUG(MOD_GRAPHICS, "Sky disabled for zone: {}", zoneName);
        clearSkyNodes();
        currentSkyType_ = 0;
        return;
    }

    // Get weather type for zone and map to sky type
    std::string weatherType = skyConfig_->getWeatherTypeForZone(zoneName);
    int mappedSkyType = skyConfig_->getSkyTypeIdForWeather(weatherType);

    if (mappedSkyType < 0) {
        // Sky disabled for this weather type
        clearSkyNodes();
        currentSkyType_ = 0;
        return;
    }

    // Use mapped sky type if available, otherwise use provided skyTypeId
    uint8_t effectiveSkyType = (mappedSkyType > 0) ? static_cast<uint8_t>(mappedSkyType) : skyTypeId;

    currentSkyType_ = effectiveSkyType;
    currentSkyCategory_ = determineSkyCategory(effectiveSkyType);

    // Store zone config for horizon fade parameters
    currentZoneConfig_ = skyConfig_->getConfigForZone(zoneName);

    LOG_INFO(MOD_GRAPHICS, "Prepared sky type {} (weather: {}, category: {}) for zone: {} "
             "(opaqueAngle={:.2f} transparentAngle={:.2f} minAngle={:.2f} maxAngle={:.2f} "
             "minWidth={:.2f} maxWidth={:.2f} minCameraZ={:.1f} maxCameraZ={:.1f})",
             effectiveSkyType, weatherType, static_cast<int>(currentSkyCategory_), zoneName,
             currentZoneConfig_.opaqueAngle, currentZoneConfig_.transparentAngle,
             currentZoneConfig_.minAngle, currentZoneConfig_.maxAngle,
             currentZoneConfig_.minWidth, currentZoneConfig_.maxWidth,
             currentZoneConfig_.minCameraZ, currentZoneConfig_.maxCameraZ);

    skyPrepared_ = true;
}

void SkyRenderer::applySkyType() {
    if (!initialized_ || !skyPrepared_) {
        return;
    }

    skyPrepared_ = false;

    // Rebuild sky scene nodes
    clearSkyNodes();
    createSkyDome();
    createCelestialBodies();
    applyInitialColors();
}

void SkyRenderer::applyInitialColors() {
    updateCelestialPositions();

    // Initialize colors for the current time
    float timeOfDay = static_cast<float>(currentHour_) + static_cast<float>(currentMinute_) / 60.0f;
    if (hasDayNightCycle(currentSkyCategory_)) {
        currentSkyColors_ = calculateSkyColors(timeOfDay);
    } else {
        currentSkyColors_ = getSpecialSkyColors(currentSkyCategory_);
    }
    updateSkyLayerColors();
}

void SkyRenderer::precomputeDomeMesh(std::vector<irr::video::S3DVertex>& outVertices,
                                      std::vector<irr::u16>& outIndices) {
    const int hSegs = SKY_DOME_HORI_SEGMENTS;
    const int vRings = SKY_DOME_VERT_RINGS;
    const float bottomPitch = SKY_DOME_BOTTOM_PITCH;
    const float topPitch = static_cast<float>(M_PI) / 2.0f;
    const float pitchRange = topPitch - bottomPitch;

    const int vertCount = (vRings + 1) * (hSegs + 1);
    const int indexCount = vRings * hSegs * 6;

    outVertices.resize(vertCount);
    outIndices.resize(indexCount);

    // Generate vertices (same math as createCustomSkyDome)
    int vi = 0;
    for (int ring = 0; ring <= vRings; ++ring) {
        float pitchFrac = static_cast<float>(ring) / static_cast<float>(vRings);
        float pitch = bottomPitch + pitchFrac * pitchRange;
        float cosPitch = std::cos(pitch);
        float sinPitch = std::sin(pitch);
        float v = 1.0f - pitchFrac;

        for (int seg = 0; seg <= hSegs; ++seg) {
            float thetaFrac = static_cast<float>(seg) / static_cast<float>(hSegs);
            float theta = thetaFrac * 2.0f * static_cast<float>(M_PI);
            float cosTheta = std::cos(theta);
            float sinTheta = std::sin(theta);

            irr::video::S3DVertex& vert = outVertices[vi];
            vert.Pos.X = SKY_DOME_RADIUS * cosPitch * sinTheta;
            vert.Pos.Y = SKY_DOME_RADIUS * sinPitch;
            vert.Pos.Z = SKY_DOME_RADIUS * cosPitch * cosTheta;

            vert.Normal.X = -cosPitch * sinTheta;
            vert.Normal.Y = -sinPitch;
            vert.Normal.Z = -cosPitch * cosTheta;

            vert.TCoords.X = thetaFrac * 3.0f;
            vert.TCoords.Y = v * 2.0f;

            vert.Color = irr::video::SColor(255, 255, 255, 255);
            ++vi;
        }
    }

    // Generate indices (viewed from inside: clockwise winding = front face)
    int ii = 0;
    for (int ring = 0; ring < vRings; ++ring) {
        for (int seg = 0; seg < hSegs; ++seg) {
            int topLeft = ring * (hSegs + 1) + seg;
            int topRight = topLeft + 1;
            int botLeft = topLeft + (hSegs + 1);
            int botRight = botLeft + 1;

            outIndices[ii++] = topLeft;
            outIndices[ii++] = botLeft;
            outIndices[ii++] = topRight;

            outIndices[ii++] = topRight;
            outIndices[ii++] = botLeft;
            outIndices[ii++] = botRight;
        }
    }
}

void SkyRenderer::createSkyDomeFromPrecomputed(const std::vector<irr::video::S3DVertex>& vertices,
                                                 const std::vector<irr::u16>& indices) {
    if (!initialized_ || !smgr_ || !driver_ || vertices.empty()) {
        return;
    }

    // Find dome texture from sky type config (same logic as createSkyDome)
    irr::video::ITexture* texture = nullptr;
    const auto& skyData = skyLoader_->getSkyData();
    if (skyData) {
        auto skyTypeIt = skyData->skyTypes.find(currentSkyType_);
        if (skyTypeIt == skyData->skyTypes.end()) {
            skyTypeIt = skyData->skyTypes.find(0);
        }
        if (skyTypeIt != skyData->skyTypes.end()) {
            const SkyType& skyType = skyTypeIt->second;
            if (!skyType.backgroundLayers.empty()) {
                int layerNum = skyType.backgroundLayers[0];
                auto layerIt = skyData->layers.find(layerNum);
                if (layerIt != skyData->layers.end() && layerIt->second) {
                    texture = loadSkyTexture(layerIt->second->textureName);
                }
            }
        }
    }

    // Build mesh from pre-computed data (memcpy, no trig)
    irr::scene::SMesh* mesh = new irr::scene::SMesh();
    irr::scene::SMeshBuffer* buffer = new irr::scene::SMeshBuffer();

    buffer->Vertices.set_used(vertices.size());
    buffer->Indices.set_used(indices.size());

    memcpy(buffer->Vertices.pointer(), vertices.data(),
           vertices.size() * sizeof(irr::video::S3DVertex));
    memcpy(buffer->Indices.pointer(), indices.data(),
           indices.size() * sizeof(irr::u16));

    buffer->recalculateBoundingBox();
    mesh->addMeshBuffer(buffer);
    mesh->recalculateBoundingBox();
    buffer->drop();

    skyDomeMeshNode_ = smgr_->addMeshSceneNode(mesh);
    mesh->drop();

    if (!skyDomeMeshNode_) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to create sky dome mesh node from pre-computed data");
        return;
    }

    skyDomeMeshBuffer_ = static_cast<irr::scene::SMeshBuffer*>(
        skyDomeMeshNode_->getMesh()->getMeshBuffer(0));

    // Set material properties
    if (texture) {
        skyDomeMeshNode_->setMaterialTexture(0, texture);
    }
    skyDomeMeshNode_->setMaterialFlag(irr::video::EMF_LIGHTING, false);
    skyDomeMeshNode_->setMaterialFlag(irr::video::EMF_ZBUFFER, true);
    skyDomeMeshNode_->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE, false);
    skyDomeMeshNode_->setMaterialFlag(irr::video::EMF_BACK_FACE_CULLING, false);
    skyDomeMeshNode_->setMaterialFlag(irr::video::EMF_BILINEAR_FILTER, true);
    skyDomeMeshNode_->setMaterialType(irr::video::EMT_TRANSPARENT_VERTEX_ALPHA);

    irr::video::SMaterial& mat = skyDomeMeshNode_->getMaterial(0);
    mat.TextureLayer[0].TextureWrapU = irr::video::ETC_REPEAT;
    mat.TextureLayer[0].TextureWrapV = irr::video::ETC_REPEAT;
    mat.TextureLayer[0].BilinearFilter = true;

    skyDomeMeshNode_->setMaterialFlag(irr::video::EMF_FOG_ENABLE, false);
    skyDomeMeshNode_->setPosition(lastCameraPos_);

    updateDomeVertexAlpha();

    LOG_INFO(MOD_GRAPHICS, "Created sky dome from pre-computed data: {} vertices, {} triangles",
             vertices.size(), indices.size() / 3);
}

void SkyRenderer::createSkyDomeFromWldGeometry(const std::vector<irr::video::S3DVertex>& vertices,
                                                 const std::vector<irr::u16>& indices) {
    if (!initialized_ || !smgr_ || !driver_ || vertices.empty()) {
        return;
    }

    // Find dome texture from sky type config (same logic as createSkyDomeFromPrecomputed)
    irr::video::ITexture* texture = nullptr;
    const auto& skyData = skyLoader_->getSkyData();
    if (skyData) {
        auto skyTypeIt = skyData->skyTypes.find(currentSkyType_);
        if (skyTypeIt == skyData->skyTypes.end()) {
            skyTypeIt = skyData->skyTypes.find(0);
        }
        if (skyTypeIt != skyData->skyTypes.end()) {
            const SkyType& skyType = skyTypeIt->second;
            if (!skyType.backgroundLayers.empty()) {
                int layerNum = skyType.backgroundLayers[0];
                auto layerIt = skyData->layers.find(layerNum);
                if (layerIt != skyData->layers.end() && layerIt->second) {
                    texture = loadSkyTexture(layerIt->second->textureName);
                }
            }
        }
    }

    // Build mesh from WLD geometry data (memcpy, no trig)
    irr::scene::SMesh* mesh = new irr::scene::SMesh();
    irr::scene::SMeshBuffer* buffer = new irr::scene::SMeshBuffer();

    buffer->Vertices.set_used(vertices.size());
    buffer->Indices.set_used(indices.size());

    memcpy(buffer->Vertices.pointer(), vertices.data(),
           vertices.size() * sizeof(irr::video::S3DVertex));
    memcpy(buffer->Indices.pointer(), indices.data(),
           indices.size() * sizeof(irr::u16));

    buffer->recalculateBoundingBox();
    mesh->addMeshBuffer(buffer);
    mesh->recalculateBoundingBox();
    buffer->drop();

    skyDomeMeshNode_ = smgr_->addMeshSceneNode(mesh);
    mesh->drop();

    if (!skyDomeMeshNode_) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to create sky dome mesh node from WLD geometry");
        return;
    }

    skyDomeMeshBuffer_ = static_cast<irr::scene::SMeshBuffer*>(
        skyDomeMeshNode_->getMesh()->getMeshBuffer(0));

    // Set material properties
    if (texture) {
        skyDomeMeshNode_->setMaterialTexture(0, texture);
    }
    skyDomeMeshNode_->setMaterialFlag(irr::video::EMF_LIGHTING, false);
    skyDomeMeshNode_->setMaterialFlag(irr::video::EMF_ZBUFFER, true);
    skyDomeMeshNode_->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE, false);
    skyDomeMeshNode_->setMaterialFlag(irr::video::EMF_BACK_FACE_CULLING, false);
    skyDomeMeshNode_->setMaterialFlag(irr::video::EMF_BILINEAR_FILTER, true);
    skyDomeMeshNode_->setMaterialType(irr::video::EMT_TRANSPARENT_VERTEX_ALPHA);

    irr::video::SMaterial& mat = skyDomeMeshNode_->getMaterial(0);
    mat.TextureLayer[0].TextureWrapU = irr::video::ETC_REPEAT;
    mat.TextureLayer[0].TextureWrapV = irr::video::ETC_REPEAT;
    mat.TextureLayer[0].BilinearFilter = true;

    skyDomeMeshNode_->setMaterialFlag(irr::video::EMF_FOG_ENABLE, false);
    skyDomeMeshNode_->setPosition(lastCameraPos_);

    updateDomeVertexAlpha();

    LOG_INFO(MOD_GRAPHICS, "Created sky dome from WLD geometry: {} vertices, {} triangles",
             vertices.size(), indices.size() / 3);
}

void SkyRenderer::createCelestialBodiesOnly() {
    // Delegates to the existing createCelestialBodies() implementation
    createCelestialBodies();
}

// T14: uploadPreDecodedTexture removed. Sky textures now loaded via
// ConstrainedTextureCache::getOrLoad() + uploadTexture() + registerUploadedTexture().

void SkyRenderer::updateTimeOfDay(uint8_t hour, uint8_t minute) {
    if (!initialized_) {
        return;
    }

    currentHour_ = hour;
    currentMinute_ = minute;

    // Calculate current time as decimal hours
    float timeOfDay = static_cast<float>(hour) + static_cast<float>(minute) / 60.0f;

    // Update sky colors for current time
    // Special sky types (PoP planes, etc.) have fixed colors that don't change with time
    if (hasDayNightCycle(currentSkyCategory_)) {
        currentSkyColors_ = calculateSkyColors(timeOfDay);
    } else {
        currentSkyColors_ = getSpecialSkyColors(currentSkyCategory_);
    }

    // Update celestial body positions (only for sky types that have them)
    updateCelestialPositions();

    // Update sky layer colors based on time of day
    updateSkyLayerColors();

    // Update sun glow color
    updateSunGlowColor();
}

void SkyRenderer::setWeatherBrightness(float brightness) {
    float newBrightness = std::max(0.0f, std::min(1.0f, brightness));
    if (std::abs(newBrightness - weatherBrightness_) > 0.01f) {
        weatherBrightness_ = newBrightness;
        // Re-apply sky colors with new weather brightness
        updateSkyLayerColors();
        updateSunGlowColor();
    }
}

void SkyRenderer::update(float deltaTime) {
    if (!initialized_ || !enabled_) {
        return;
    }

    // Update cloud scrolling animation
    cloudScrollOffset_ += deltaTime * 0.01f; // Slow scroll
    if (cloudScrollOffset_ > 1.0f) {
        cloudScrollOffset_ -= 1.0f;
    }

    // Apply cloud scroll offset to cloud layer UV coordinates
    updateCloudScrolling();
}

SkyRenderer::SkyState SkyRenderer::computeState(float deltaTime) const {
    SkyState state;
    state.cloudScrollOffset = cloudScrollOffset_ + deltaTime * 0.01f;
    if (state.cloudScrollOffset > 1.0f) {
        state.cloudScrollOffset -= 1.0f;
    }
    return state;
}

void SkyRenderer::applyState(const SkyState& state) {
    if (!initialized_ || !enabled_) return;
    cloudScrollOffset_ = state.cloudScrollOffset;
    updateCloudScrolling();
}

void SkyRenderer::setCameraPosition(const irr::core::vector3df& cameraPos) {
    if (!initialized_) {
        return;
    }

    lastCameraPos_ = cameraPos;

    // Move custom dome mesh to follow camera
    if (skyDomeMeshNode_) {
        skyDomeMeshNode_->setPosition(cameraPos);
    }

    // Convert Irrlicht Y-up camera position to EQ Z-up: cameraZ = cameraPos.Y
    float newCameraZ = cameraPos.Y;
    if (std::abs(newCameraZ - lastCameraZ_) > 1.0f) {
        lastCameraZ_ = newCameraZ;
        updateDomeVertexAlpha();
    }

    updateCelestialPositions();
}

void SkyRenderer::setEnabled(bool enabled) {
    enabled_ = enabled;
    updateSkyVisibility();
}

void SkyRenderer::createSkyDome() {
    if (!initialized_ || !smgr_ || !driver_) {
        return;
    }

    const auto& skyData = skyLoader_->getSkyData();
    if (!skyData) {
        return;
    }

    // Find the sky type configuration
    auto skyTypeIt = skyData->skyTypes.find(currentSkyType_);
    if (skyTypeIt == skyData->skyTypes.end()) {
        // Use default (type 0) if not found
        skyTypeIt = skyData->skyTypes.find(0);
    }

    if (skyTypeIt == skyData->skyTypes.end()) {
        LOG_WARN(MOD_GRAPHICS, "No sky type configuration found for type {}", currentSkyType_);
        return;
    }

    const SkyType& skyType = skyTypeIt->second;

    // Load sky texture from the first background layer
    if (!skyType.backgroundLayers.empty()) {
        int layerNum = skyType.backgroundLayers[0];
        auto layerIt = skyData->layers.find(layerNum);
        if (layerIt != skyData->layers.end() && layerIt->second) {
            const SkyLayer& layer = *layerIt->second;

            irr::video::ITexture* texture = loadSkyTexture(layer.textureName);
            if (texture) {
                irr::core::dimension2d<irr::u32> texSize = texture->getOriginalSize();
                LOG_INFO(MOD_GRAPHICS, "Sky texture {} actual size: {}x{}",
                         layer.textureName, texSize.Width, texSize.Height);

                if (texSize.Width > 0 && texSize.Height > 0) {
                    createCustomSkyDome(texture);
                } else {
                    LOG_ERROR(MOD_GRAPHICS, "Sky texture has zero size, skipping sky dome");
                }
            } else {
                LOG_ERROR(MOD_GRAPHICS, "Failed to load sky texture: {}", layer.textureName);
            }
        }
    }
}

void SkyRenderer::createCustomSkyDome(irr::video::ITexture* texture) {
    // Build a hemisphere mesh: SKY_DOME_HORI_SEGMENTS around, SKY_DOME_VERT_RINGS from bottom to zenith
    // Bottom ring starts below horizon (SKY_DOME_BOTTOM_PITCH radians)
    // Top is zenith (PI/2 radians)

    const int hSegs = SKY_DOME_HORI_SEGMENTS;
    const int vRings = SKY_DOME_VERT_RINGS;
    const float bottomPitch = SKY_DOME_BOTTOM_PITCH;
    const float topPitch = static_cast<float>(M_PI) / 2.0f;
    const float pitchRange = topPitch - bottomPitch;

    // Vertex count: (vRings + 1) rings of (hSegs + 1) vertices (duplicated seam for UV wrapping)
    const int vertCount = (vRings + 1) * (hSegs + 1);
    // Triangle count: vRings * hSegs * 2 triangles per quad
    const int indexCount = vRings * hSegs * 6;

    irr::scene::SMesh* mesh = new irr::scene::SMesh();
    irr::scene::SMeshBuffer* buffer = new irr::scene::SMeshBuffer();

    buffer->Vertices.set_used(vertCount);
    buffer->Indices.set_used(indexCount);

    // Generate vertices
    int vi = 0;
    for (int ring = 0; ring <= vRings; ++ring) {
        float pitchFrac = static_cast<float>(ring) / static_cast<float>(vRings);
        float pitch = bottomPitch + pitchFrac * pitchRange;
        float cosPitch = std::cos(pitch);
        float sinPitch = std::sin(pitch);
        float v = 1.0f - pitchFrac; // UV V: 0 at zenith, 1 at bottom

        for (int seg = 0; seg <= hSegs; ++seg) {
            float thetaFrac = static_cast<float>(seg) / static_cast<float>(hSegs);
            float theta = thetaFrac * 2.0f * static_cast<float>(M_PI);
            float cosTheta = std::cos(theta);
            float sinTheta = std::sin(theta);

            irr::video::S3DVertex& vert = buffer->Vertices[vi];
            // Irrlicht Y-up: X = cos(pitch)*sin(theta), Y = sin(pitch), Z = cos(pitch)*cos(theta)
            vert.Pos.X = SKY_DOME_RADIUS * cosPitch * sinTheta;
            vert.Pos.Y = SKY_DOME_RADIUS * sinPitch;
            vert.Pos.Z = SKY_DOME_RADIUS * cosPitch * cosTheta;

            // Normal points inward (we view from inside)
            vert.Normal.X = -cosPitch * sinTheta;
            vert.Normal.Y = -sinPitch;
            vert.Normal.Z = -cosPitch * cosTheta;

            // UV: horizontal tiling (3x wrap), vertical from zenith to horizon
            vert.TCoords.X = thetaFrac * 3.0f;
            vert.TCoords.Y = v * 2.0f; // Stretch vertically

            // Initial color: white with alpha from pitch (will be updated by updateDomeVertexAlpha)
            vert.Color = irr::video::SColor(255, 255, 255, 255);

            ++vi;
        }
    }

    // Generate indices (viewed from inside, wind counter-clockwise)
    int ii = 0;
    for (int ring = 0; ring < vRings; ++ring) {
        for (int seg = 0; seg < hSegs; ++seg) {
            int topLeft = ring * (hSegs + 1) + seg;
            int topRight = topLeft + 1;
            int botLeft = topLeft + (hSegs + 1);
            int botRight = botLeft + 1;

            // Triangle 1 (viewed from inside: clockwise winding = front face)
            buffer->Indices[ii++] = topLeft;
            buffer->Indices[ii++] = botLeft;
            buffer->Indices[ii++] = topRight;

            // Triangle 2
            buffer->Indices[ii++] = topRight;
            buffer->Indices[ii++] = botLeft;
            buffer->Indices[ii++] = botRight;
        }
    }

    buffer->recalculateBoundingBox();
    mesh->addMeshBuffer(buffer);
    mesh->recalculateBoundingBox();
    buffer->drop(); // mesh holds the reference now

    // Create scene node
    skyDomeMeshNode_ = smgr_->addMeshSceneNode(mesh);
    mesh->drop(); // scene node holds the reference now

    if (!skyDomeMeshNode_) {
        LOG_ERROR(MOD_GRAPHICS, "Failed to create sky dome mesh node");
        return;
    }

    // Keep pointer to buffer for fast vertex updates (node owns mesh which owns buffer)
    skyDomeMeshBuffer_ = static_cast<irr::scene::SMeshBuffer*>(
        skyDomeMeshNode_->getMesh()->getMeshBuffer(0));

    // Set material properties
    skyDomeMeshNode_->setMaterialTexture(0, texture);
    skyDomeMeshNode_->setMaterialFlag(irr::video::EMF_LIGHTING, false);
    skyDomeMeshNode_->setMaterialFlag(irr::video::EMF_ZBUFFER, true);
    skyDomeMeshNode_->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE, false);
    skyDomeMeshNode_->setMaterialFlag(irr::video::EMF_BACK_FACE_CULLING, false);
    skyDomeMeshNode_->setMaterialFlag(irr::video::EMF_BILINEAR_FILTER, true);
    skyDomeMeshNode_->setMaterialType(irr::video::EMT_TRANSPARENT_VERTEX_ALPHA);

    irr::video::SMaterial& mat = skyDomeMeshNode_->getMaterial(0);
    mat.TextureLayer[0].TextureWrapU = irr::video::ETC_REPEAT;
    mat.TextureLayer[0].TextureWrapV = irr::video::ETC_REPEAT;
    mat.TextureLayer[0].BilinearFilter = true;

    // Render early (sky behind everything)
    skyDomeMeshNode_->setMaterialFlag(irr::video::EMF_FOG_ENABLE, false);

    // Position at camera (will be updated in setCameraPosition)
    skyDomeMeshNode_->setPosition(lastCameraPos_);

    // Apply initial vertex alpha from zone config
    updateDomeVertexAlpha();

    LOG_INFO(MOD_GRAPHICS, "Created custom sky dome: {} vertices, {} triangles",
             vertCount, indexCount / 3);
}

} // namespace Graphics
} // namespace EQT
