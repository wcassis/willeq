#include "client/graphics/sky_renderer.h"
#include "client/graphics/eq/sky_loader.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/sky_config.h"
#include "common/logging.h"
#include <cmath>

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
    clearSkyNodes();
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

void SkyRenderer::setSkyType(uint8_t skyTypeId, const std::string& zoneName) {
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

    if (effectiveSkyType == currentSkyType_ && !skyDomeNodes_.empty()) {
        // Already set to this sky type
        return;
    }

    currentSkyType_ = effectiveSkyType;
    currentSkyCategory_ = determineSkyCategory(effectiveSkyType);

    LOG_INFO(MOD_GRAPHICS, "Setting sky type {} (weather: {}, category: {}) for zone: {}",
             effectiveSkyType, weatherType, static_cast<int>(currentSkyCategory_), zoneName);

    // Rebuild sky scene nodes
    clearSkyNodes();
    createSkyDome();
    createCelestialBodies();
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

void SkyRenderer::setCameraPosition(const irr::core::vector3df& cameraPos) {
    if (!initialized_) {
        return;
    }

    // Update camera position for celestial body positioning
    lastCameraPos_ = cameraPos;

    // Note: Irrlicht's built-in sky dome automatically follows the camera
    // We only need to update celestial body positions
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

    // Use Irrlicht's built-in sky dome for the background layer
    // This ensures proper rendering order (sky renders first in sky pass)
    if (!skyType.backgroundLayers.empty()) {
        int layerNum = skyType.backgroundLayers[0];
        auto layerIt = skyData->layers.find(layerNum);
        if (layerIt != skyData->layers.end() && layerIt->second) {
            const SkyLayer& layer = *layerIt->second;

            // Load the sky texture
            irr::video::ITexture* texture = loadSkyTexture(layer.textureName);
            if (texture) {
                // Check actual texture dimensions from Irrlicht
                irr::core::dimension2d<irr::u32> texSize = texture->getOriginalSize();
                LOG_INFO(MOD_GRAPHICS, "Sky texture {} actual size: {}x{}",
                         layer.textureName, texSize.Width, texSize.Height);

                if (texSize.Width == 0 || texSize.Height == 0) {
                    LOG_ERROR(MOD_GRAPHICS, "Sky texture has zero size, skipping sky box");
                } else {
                    // Use sky box instead of sky dome - better texture tiling
                    // Sky box uses the same texture on all 6 faces
                    irrlichtSkyDome_ = smgr_->addSkyBoxSceneNode(
                        texture,  // top
                        texture,  // bottom
                        texture,  // left
                        texture,  // right
                        texture,  // front
                        texture   // back
                    );

                    if (irrlichtSkyDome_) {
                        // Set texture tiling and filtering on all 6 materials
                        for (irr::u32 i = 0; i < irrlichtSkyDome_->getMaterialCount(); ++i) {
                            irr::video::SMaterial& mat = irrlichtSkyDome_->getMaterial(i);
                            // Enable lighting so material colors can affect the texture
                            mat.setFlag(irr::video::EMF_LIGHTING, true);
                            mat.setFlag(irr::video::EMF_BILINEAR_FILTER, true);
                            mat.setFlag(irr::video::EMF_TRILINEAR_FILTER, true);
                            mat.setFlag(irr::video::EMF_ANISOTROPIC_FILTER, true);
                            mat.TextureLayer[0].TextureWrapU = irr::video::ETC_REPEAT;
                            mat.TextureLayer[0].TextureWrapV = irr::video::ETC_REPEAT;
                            mat.TextureLayer[0].BilinearFilter = true;
                            mat.TextureLayer[0].TrilinearFilter = true;

                            // Set up material colors for day/night tinting
                            // Use full white as base so tinting can darken
                            mat.DiffuseColor = irr::video::SColor(255, 255, 255, 255);
                            mat.AmbientColor = irr::video::SColor(255, 255, 255, 255);
                            mat.EmissiveColor = irr::video::SColor(0, 0, 0, 0);
                            // ECM_DIFFUSE_AND_AMBIENT: material colors modulate the texture
                            mat.ColorMaterial = irr::video::ECM_DIFFUSE_AND_AMBIENT;

                            // Scale UV to tile the texture
                            irr::core::matrix4 texMat;
                            texMat.setTextureScale(3.0f, 3.0f);  // Tile 3x3 per face
                            mat.setTextureMatrix(0, texMat);
                        }

                        LOG_INFO(MOD_GRAPHICS, "Created Irrlicht sky box with texture: {} (tiled, filtered)", layer.textureName);
                    }
                }
            } else {
                LOG_ERROR(MOD_GRAPHICS, "Failed to load sky texture: {}", layer.textureName);
            }
        }
    }

    LOG_INFO(MOD_GRAPHICS, "Sky dome created using Irrlicht built-in sky dome");
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
                sunGlowNode_->setMaterialFlag(irr::video::EMF_ZBUFFER, false);
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
                sunNode_->setMaterialFlag(irr::video::EMF_ZBUFFER, false);
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
                moonNode_->setMaterialFlag(irr::video::EMF_ZBUFFER, false);
                moonNode_->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE, false);
                moonNode_->setMaterialType(irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL);

                LOG_DEBUG(MOD_GRAPHICS, "Created moon billboard (texture: {}, hasTrack: {})",
                         body->textureName, moonTrack_ ? "yes" : "no");
            }
        }
    }
}

irr::video::ITexture* SkyRenderer::loadSkyTexture(const std::string& name) {
    if (name.empty() || !fileSystem_ || !driver_) {
        return nullptr;
    }

    // Check cache first
    auto it = textureCache_.find(name);
    if (it != textureCache_.end()) {
        return it->second;
    }

    // Get texture data from sky loader
    std::shared_ptr<TextureInfo> texInfo = skyLoader_->getTexture(name);
    if (!texInfo || texInfo->data.empty()) {
        LOG_WARN(MOD_GRAPHICS, "Sky texture not found: {}", name);
        return nullptr;
    }

    // Load texture using memory file (works for BMP, TGA, and other formats)
    irr::video::ITexture* texture = nullptr;

    irr::io::IReadFile* memFile = fileSystem_->createMemoryReadFile(
        texInfo->data.data(),
        static_cast<irr::s32>(texInfo->data.size()),
        name.c_str(),
        false  // Don't delete data on close
    );

    if (memFile) {
        texture = driver_->getTexture(memFile);
        memFile->drop();
    }

    // Upscale small textures with bilinear interpolation to reduce pixelation
    if (texture) {
        irr::core::dimension2d<irr::u32> origSize = texture->getOriginalSize();

        if (origSize.Width <= 128 && origSize.Height <= 128 && origSize.Width > 0 && origSize.Height > 0) {
            // Create upscaled image (4x scale: 128->512)
            const irr::u32 targetSize = 512;
            irr::video::IImage* origImage = driver_->createImage(texture,
                irr::core::position2d<irr::s32>(0, 0), origSize);

            if (origImage) {
                irr::video::IImage* scaledImage = driver_->createImage(
                    irr::video::ECF_A8R8G8B8,
                    irr::core::dimension2d<irr::u32>(targetSize, targetSize));

                if (scaledImage) {
                    // Manual bilinear interpolation
                    float scaleX = static_cast<float>(origSize.Width) / targetSize;
                    float scaleY = static_cast<float>(origSize.Height) / targetSize;

                    for (irr::u32 y = 0; y < targetSize; ++y) {
                        for (irr::u32 x = 0; x < targetSize; ++x) {
                            float srcX = x * scaleX;
                            float srcY = y * scaleY;

                            irr::u32 x0 = static_cast<irr::u32>(srcX);
                            irr::u32 y0 = static_cast<irr::u32>(srcY);
                            irr::u32 x1 = std::min(x0 + 1, origSize.Width - 1);
                            irr::u32 y1 = std::min(y0 + 1, origSize.Height - 1);

                            float fx = srcX - x0;
                            float fy = srcY - y0;

                            // Get 4 neighboring pixels
                            irr::video::SColor c00 = origImage->getPixel(x0, y0);
                            irr::video::SColor c10 = origImage->getPixel(x1, y0);
                            irr::video::SColor c01 = origImage->getPixel(x0, y1);
                            irr::video::SColor c11 = origImage->getPixel(x1, y1);

                            // Bilinear interpolation
                            auto lerp = [](float a, float b, float t) { return a + t * (b - a); };

                            float r = lerp(lerp(c00.getRed(), c10.getRed(), fx),
                                          lerp(c01.getRed(), c11.getRed(), fx), fy);
                            float g = lerp(lerp(c00.getGreen(), c10.getGreen(), fx),
                                          lerp(c01.getGreen(), c11.getGreen(), fx), fy);
                            float b_col = lerp(lerp(c00.getBlue(), c10.getBlue(), fx),
                                          lerp(c01.getBlue(), c11.getBlue(), fx), fy);
                            float a = lerp(lerp(c00.getAlpha(), c10.getAlpha(), fx),
                                          lerp(c01.getAlpha(), c11.getAlpha(), fx), fy);

                            scaledImage->setPixel(x, y, irr::video::SColor(
                                static_cast<irr::u32>(a),
                                static_cast<irr::u32>(r),
                                static_cast<irr::u32>(g),
                                static_cast<irr::u32>(b_col)));
                        }
                    }

                    // Create new texture from scaled image
                    std::string scaledName = name + "_scaled";
                    irr::video::ITexture* scaledTexture = driver_->addTexture(scaledName.c_str(), scaledImage);

                    if (scaledTexture) {
                        LOG_DEBUG(MOD_GRAPHICS, "Upscaled sky texture: {} from {}x{} to {}x{} (bilinear)",
                                  name, origSize.Width, origSize.Height, targetSize, targetSize);
                        texture = scaledTexture;
                    }

                    scaledImage->drop();
                }
                origImage->drop();
            }
        }

        textureCache_[name] = texture;
        LOG_DEBUG(MOD_GRAPHICS, "Loaded sky texture: {} ({}x{})",
                  name, texture->getOriginalSize().Width, texture->getOriginalSize().Height);
    } else {
        LOG_WARN(MOD_GRAPHICS, "Failed to load sky texture: {}", name);
    }

    return texture;
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

    // Interpolate translation (position)
    glm::vec3 pos = glm::mix(kf0.translation, kf1.translation, t);

    // Scale to celestial distance and convert EQ to Irrlicht coordinates
    // EQ: Z-up -> Irrlicht: Y-up
    float scale = CELESTIAL_DISTANCE / 100.0f;  // Assume track data is in ~100 unit scale
    return irr::core::vector3df(pos.x * scale, pos.z * scale, pos.y * scale);
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

    // Update Irrlicht built-in sky dome
    if (irrlichtSkyDome_) {
        irrlichtSkyDome_->setVisible(enabled_);
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

    // Remove Irrlicht built-in sky dome
    if (irrlichtSkyDome_) {
        irrlichtSkyDome_->remove();
        irrlichtSkyDome_ = nullptr;
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

SkyColorSet SkyRenderer::calculateSkyColors(float timeOfDay) const {
    // Define color sets for different times of day
    // Night (0-4): Dark blue/black sky
    static const SkyColorSet NIGHT = {
        irr::video::SColor(255, 10, 10, 30),     // zenith: deep blue-black
        irr::video::SColor(255, 20, 20, 50),     // horizon: slightly lighter
        irr::video::SColor(255, 15, 15, 35),     // fog: dark blue
        0.0f,                                     // sunIntensity
        0.2f                                      // cloudBrightness: dim
    };

    // Pre-dawn (4-5): Very early light
    static const SkyColorSet PRE_DAWN = {
        irr::video::SColor(255, 25, 25, 60),     // zenith: dark blue
        irr::video::SColor(255, 60, 40, 50),     // horizon: hint of color
        irr::video::SColor(255, 35, 30, 45),     // fog
        0.05f,
        0.3f
    };

    // Dawn (5-6): Orange/pink gradient, sun rising
    static const SkyColorSet DAWN = {
        irr::video::SColor(255, 80, 100, 150),   // zenith: lightening blue
        irr::video::SColor(255, 220, 140, 90),   // horizon: orange-pink
        irr::video::SColor(255, 140, 110, 100),  // fog: warm
        0.3f,
        0.6f
    };

    // Sunrise (6-7): Golden hour
    static const SkyColorSet SUNRISE = {
        irr::video::SColor(255, 130, 160, 200),  // zenith: light blue
        irr::video::SColor(255, 255, 180, 100),  // horizon: golden
        irr::video::SColor(255, 180, 160, 140),  // fog: warm golden
        0.6f,
        0.8f
    };

    // Morning (7-10): Transitioning to day
    static const SkyColorSet MORNING = {
        irr::video::SColor(255, 140, 180, 220),  // zenith: bright blue
        irr::video::SColor(255, 200, 210, 220),  // horizon: pale blue
        irr::video::SColor(255, 180, 195, 210),  // fog: light blue
        0.85f,
        0.95f
    };

    // Day (10-16): Full daylight
    static const SkyColorSet DAY = {
        irr::video::SColor(255, 100, 150, 220),  // zenith: sky blue
        irr::video::SColor(255, 180, 200, 220),  // horizon: pale
        irr::video::SColor(255, 160, 180, 200),  // fog: bluish
        1.0f,
        1.0f
    };

    // Afternoon (16-17): Slightly warmer
    static const SkyColorSet AFTERNOON = {
        irr::video::SColor(255, 120, 160, 210),  // zenith: warm blue
        irr::video::SColor(255, 210, 200, 190),  // horizon: hint of warmth
        irr::video::SColor(255, 175, 180, 190),  // fog
        0.9f,
        1.0f
    };

    // Sunset (17-18): Golden hour
    static const SkyColorSet SUNSET = {
        irr::video::SColor(255, 130, 130, 180),  // zenith: purple-blue
        irr::video::SColor(255, 255, 160, 80),   // horizon: orange-gold
        irr::video::SColor(255, 180, 140, 120),  // fog: warm
        0.6f,
        0.8f
    };

    // Dusk (18-19): Orange/red gradient, sun setting
    static const SkyColorSet DUSK = {
        irr::video::SColor(255, 80, 60, 120),    // zenith: purple
        irr::video::SColor(255, 200, 100, 60),   // horizon: red-orange
        irr::video::SColor(255, 120, 80, 80),    // fog: reddish
        0.25f,
        0.5f
    };

    // Twilight (19-20): Transitioning to night
    static const SkyColorSet TWILIGHT = {
        irr::video::SColor(255, 40, 40, 80),     // zenith: dark blue-purple
        irr::video::SColor(255, 80, 50, 60),     // horizon: fading color
        irr::video::SColor(255, 50, 40, 55),     // fog: dark
        0.1f,
        0.35f
    };

    // Interpolate between color sets based on time
    if (timeOfDay < 4.0f) {
        // Night (0-4)
        return NIGHT;
    } else if (timeOfDay < 5.0f) {
        // Pre-dawn (4-5)
        float t = timeOfDay - 4.0f;
        return interpolateSkyColors(NIGHT, PRE_DAWN, t);
    } else if (timeOfDay < 6.0f) {
        // Dawn (5-6)
        float t = timeOfDay - 5.0f;
        return interpolateSkyColors(PRE_DAWN, DAWN, t);
    } else if (timeOfDay < 7.0f) {
        // Sunrise (6-7)
        float t = timeOfDay - 6.0f;
        return interpolateSkyColors(DAWN, SUNRISE, t);
    } else if (timeOfDay < 10.0f) {
        // Morning (7-10)
        float t = (timeOfDay - 7.0f) / 3.0f;
        return interpolateSkyColors(SUNRISE, MORNING, t);
    } else if (timeOfDay < 16.0f) {
        // Day (10-16) - no interpolation, full daylight
        if (timeOfDay < 11.0f) {
            float t = timeOfDay - 10.0f;
            return interpolateSkyColors(MORNING, DAY, t);
        }
        return DAY;
    } else if (timeOfDay < 17.0f) {
        // Afternoon (16-17)
        float t = timeOfDay - 16.0f;
        return interpolateSkyColors(DAY, AFTERNOON, t);
    } else if (timeOfDay < 18.0f) {
        // Sunset (17-18)
        float t = timeOfDay - 17.0f;
        return interpolateSkyColors(AFTERNOON, SUNSET, t);
    } else if (timeOfDay < 19.0f) {
        // Dusk (18-19)
        float t = timeOfDay - 18.0f;
        return interpolateSkyColors(SUNSET, DUSK, t);
    } else if (timeOfDay < 20.0f) {
        // Twilight (19-20)
        float t = timeOfDay - 19.0f;
        return interpolateSkyColors(DUSK, TWILIGHT, t);
    } else if (timeOfDay < 21.0f) {
        // Transition to night (20-21)
        float t = timeOfDay - 20.0f;
        return interpolateSkyColors(TWILIGHT, NIGHT, t);
    } else {
        // Night (21-24)
        return NIGHT;
    }
}

SkyColorSet SkyRenderer::interpolateSkyColors(const SkyColorSet& a, const SkyColorSet& b, float t) const {
    // Clamp t to 0-1
    t = irr::core::clamp(t, 0.0f, 1.0f);

    SkyColorSet result;

    // Interpolate colors
    result.zenith = irr::video::SColor(
        255,
        static_cast<irr::u32>(a.zenith.getRed() + (b.zenith.getRed() - a.zenith.getRed()) * t),
        static_cast<irr::u32>(a.zenith.getGreen() + (b.zenith.getGreen() - a.zenith.getGreen()) * t),
        static_cast<irr::u32>(a.zenith.getBlue() + (b.zenith.getBlue() - a.zenith.getBlue()) * t)
    );

    result.horizon = irr::video::SColor(
        255,
        static_cast<irr::u32>(a.horizon.getRed() + (b.horizon.getRed() - a.horizon.getRed()) * t),
        static_cast<irr::u32>(a.horizon.getGreen() + (b.horizon.getGreen() - a.horizon.getGreen()) * t),
        static_cast<irr::u32>(a.horizon.getBlue() + (b.horizon.getBlue() - a.horizon.getBlue()) * t)
    );

    result.fog = irr::video::SColor(
        255,
        static_cast<irr::u32>(a.fog.getRed() + (b.fog.getRed() - a.fog.getRed()) * t),
        static_cast<irr::u32>(a.fog.getGreen() + (b.fog.getGreen() - a.fog.getGreen()) * t),
        static_cast<irr::u32>(a.fog.getBlue() + (b.fog.getBlue() - a.fog.getBlue()) * t)
    );

    // Interpolate floats
    result.sunIntensity = a.sunIntensity + (b.sunIntensity - a.sunIntensity) * t;
    result.cloudBrightness = a.cloudBrightness + (b.cloudBrightness - a.cloudBrightness) * t;

    return result;
}

void SkyRenderer::updateSkyLayerColors() {
    if (!enabled_) {
        return;
    }

    // Apply tint to sky layers based on time of day
    // Use cloud brightness to modulate layer colors

    // Create a tint color that blends zenith and horizon colors
    // This simulates the sky gradient effect
    irr::video::SColor tint(
        255,
        (currentSkyColors_.zenith.getRed() + currentSkyColors_.horizon.getRed()) / 2,
        (currentSkyColors_.zenith.getGreen() + currentSkyColors_.horizon.getGreen()) / 2,
        (currentSkyColors_.zenith.getBlue() + currentSkyColors_.horizon.getBlue()) / 2
    );

    // Modulate by brightness
    tint = irr::video::SColor(
        255,
        static_cast<irr::u32>(tint.getRed() * currentSkyColors_.cloudBrightness),
        static_cast<irr::u32>(tint.getGreen() * currentSkyColors_.cloudBrightness),
        static_cast<irr::u32>(tint.getBlue() * currentSkyColors_.cloudBrightness)
    );

    // Apply tint to legacy sky dome nodes
    for (auto* node : skyDomeNodes_) {
        if (node) {
            for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                // Set diffuse color to modulate texture
                node->getMaterial(i).DiffuseColor = tint;
                node->getMaterial(i).AmbientColor = tint;
                // Ensure vertex colors are used for modulation
                node->getMaterial(i).ColorMaterial = irr::video::ECM_NONE;
            }
        }
    }

    // Apply tint to Irrlicht built-in sky box for day/night cycle
    // Irrlicht sky boxes don't respond well to lighting, so we use two techniques:
    // 1. Set the background clear color to match the tint (for the "base" sky color)
    // 2. Adjust sky box opacity - more transparent at night so background shows through
    if (irrlichtSkyDome_) {
        // Calculate opacity based on brightness - darker = more transparent to show background
        // At full brightness (1.0), alpha = 255 (opaque)
        // At low brightness (0.2), alpha = ~100 (semi-transparent, background shows through)
        float brightness = currentSkyColors_.cloudBrightness;
        irr::u32 alpha = static_cast<irr::u32>(100 + 155 * brightness);  // Range: 100-255

        for (irr::u32 i = 0; i < irrlichtSkyDome_->getMaterialCount(); ++i) {
            irr::video::SMaterial& mat = irrlichtSkyDome_->getMaterial(i);
            // Use vertex alpha for transparency
            mat.DiffuseColor = irr::video::SColor(alpha, tint.getRed(), tint.getGreen(), tint.getBlue());
            mat.AmbientColor = irr::video::SColor(alpha, tint.getRed(), tint.getGreen(), tint.getBlue());
            // Use transparent material type to enable alpha blending
            mat.MaterialType = irr::video::EMT_TRANSPARENT_VERTEX_ALPHA;
            mat.ColorMaterial = irr::video::ECM_DIFFUSE_AND_AMBIENT;
        }
    }

    // Set the background/clear color to match the tint for proper day/night effect
    if (driver_) {
        // Use a darker version of the tint as the background color
        // This shows through the semi-transparent sky at night
        irr::video::SColor clearColor(
            255,
            tint.getRed() / 2,
            tint.getGreen() / 2,
            tint.getBlue() / 2
        );
        // Store for use by beginScene
        currentClearColor_ = clearColor;
    }
}

void SkyRenderer::updateSunGlowColor() {
    if (!sunGlowNode_) {
        return;
    }

    // Adjust sun glow color based on time of day
    // Dawn/dusk: warm orange-red glow
    // Day: warm yellow-white glow
    // Night: no glow (handled by visibility)

    float timeOfDay = static_cast<float>(currentHour_) + static_cast<float>(currentMinute_) / 60.0f;

    irr::video::SColor glowColor;

    if (timeOfDay < 5.0f || timeOfDay > 20.0f) {
        // Night - no glow (but node should be hidden anyway)
        glowColor = irr::video::SColor(0, 255, 200, 100);
    } else if (timeOfDay < 7.0f) {
        // Dawn - warm orange-red glow
        float t = (timeOfDay - 5.0f) / 2.0f;  // 0 at 5am, 1 at 7am
        irr::u32 alpha = static_cast<irr::u32>(60 + 60 * t);  // Increasing intensity
        glowColor = irr::video::SColor(alpha, 255, 150 + static_cast<irr::u32>(50 * t), 80);
    } else if (timeOfDay < 17.0f) {
        // Day - warm yellow-white glow
        glowColor = irr::video::SColor(100, 255, 230, 180);
    } else if (timeOfDay < 19.0f) {
        // Sunset - warm orange-red glow, fading
        float t = (timeOfDay - 17.0f) / 2.0f;  // 0 at 5pm, 1 at 7pm
        irr::u32 alpha = static_cast<irr::u32>(100 - 60 * t);  // Decreasing intensity
        glowColor = irr::video::SColor(alpha, 255, 180 - static_cast<irr::u32>(30 * t), 100);
    } else {
        // Twilight - fading glow
        float t = (timeOfDay - 19.0f) / 1.0f;
        irr::u32 alpha = static_cast<irr::u32>(40 * (1.0f - t));
        glowColor = irr::video::SColor(alpha, 255, 150, 100);
    }

    sunGlowNode_->getMaterial(0).DiffuseColor = glowColor;
}

SkyCategory SkyRenderer::determineSkyCategory(uint8_t skyTypeId) const {
    // Map sky type IDs to categories based on SkyLoader definitions
    switch (skyTypeId) {
        case 0:  // DEFAULT
        case 1:  // NORMAL
        case 2:  // DESERT
        case 3:  // AIR (classic)
        case 4:  // COTTONY
            return SkyCategory::Normal;

        case 5:  // FEAR / RED
            return SkyCategory::PoWar;  // Red-tinted, no day/night

        case 6:  // LUCLIN
        case 7:  // LUCLIN2
        case 8:  // LUCLIN3
        case 9:  // LUCLIN4
            return SkyCategory::Luclin;

        case 10: // THE GREY
            return SkyCategory::TheGrey;

        case 11: // POFIRE
            return SkyCategory::PoFire;

        case 12: // POSTORMS
            return SkyCategory::PoStorms;

        case 14: // POWAR
            return SkyCategory::PoWar;

        case 15: // POAIR1
        case 17: // POAIR2
            return SkyCategory::PoAir;

        case 16: // POTRANQ
            return SkyCategory::PoTranq;

        default:
            return SkyCategory::Normal;
    }
}

SkyColorSet SkyRenderer::getSpecialSkyColors(SkyCategory category) const {
    // Return fixed colors for special sky types that don't have day/night cycles
    switch (category) {
        case SkyCategory::PoFire:
            // Plane of Fire - red/orange inferno
            return {
                irr::video::SColor(255, 180, 60, 30),    // zenith: dark orange-red
                irr::video::SColor(255, 255, 120, 40),   // horizon: bright orange
                irr::video::SColor(255, 200, 80, 40),    // fog: orange-red
                0.8f,   // sunIntensity
                0.9f    // cloudBrightness
            };

        case SkyCategory::PoStorms:
            // Plane of Storms - dark grey, stormy
            return {
                irr::video::SColor(255, 40, 45, 55),     // zenith: dark grey-blue
                irr::video::SColor(255, 70, 75, 85),     // horizon: lighter grey
                irr::video::SColor(255, 50, 55, 65),     // fog: grey
                0.3f,   // sunIntensity (dim)
                0.5f    // cloudBrightness
            };

        case SkyCategory::PoAir:
            // Plane of Air - light, ethereal blue
            return {
                irr::video::SColor(255, 180, 210, 255),  // zenith: bright light blue
                irr::video::SColor(255, 220, 240, 255),  // horizon: almost white
                irr::video::SColor(255, 200, 225, 255),  // fog: light blue
                1.0f,   // sunIntensity
                1.0f    // cloudBrightness
            };

        case SkyCategory::PoWar:
            // Plane of War / Fear - dark red
            return {
                irr::video::SColor(255, 100, 30, 30),    // zenith: dark red
                irr::video::SColor(255, 150, 50, 40),    // horizon: blood red
                irr::video::SColor(255, 120, 40, 35),    // fog: red
                0.5f,   // sunIntensity
                0.6f    // cloudBrightness
            };

        case SkyCategory::TheGrey:
            // The Grey / Nightmare / Disease - uniform grey, oppressive
            return {
                irr::video::SColor(255, 80, 80, 85),     // zenith: grey
                irr::video::SColor(255, 100, 100, 105),  // horizon: slightly lighter
                irr::video::SColor(255, 90, 90, 95),     // fog: grey
                0.4f,   // sunIntensity (no visible sun)
                0.4f    // cloudBrightness
            };

        case SkyCategory::PoTranq:
            // Plane of Tranquility - soft, peaceful colors
            return {
                irr::video::SColor(255, 140, 160, 200),  // zenith: soft blue
                irr::video::SColor(255, 200, 210, 230),  // horizon: pale
                irr::video::SColor(255, 170, 185, 210),  // fog: soft blue
                0.8f,   // sunIntensity
                0.9f    // cloudBrightness
            };

        case SkyCategory::Luclin:
            // Luclin has day/night cycle but with different base colors
            // This case shouldn't normally be called since hasDayNightCycle returns true
            return {
                irr::video::SColor(255, 60, 50, 100),    // zenith: purple-ish
                irr::video::SColor(255, 100, 80, 120),   // horizon: purple
                irr::video::SColor(255, 70, 60, 100),    // fog: purple
                0.8f,
                0.9f
            };

        case SkyCategory::Indoor:
            // Indoor - no sky visible
            return {
                irr::video::SColor(255, 30, 30, 40),
                irr::video::SColor(255, 40, 40, 50),
                irr::video::SColor(255, 35, 35, 45),
                0.0f,
                0.0f
            };

        case SkyCategory::Normal:
        default:
            // This shouldn't be called for Normal since it has day/night cycle
            // Return midday colors as fallback
            return {
                irr::video::SColor(255, 100, 150, 220),
                irr::video::SColor(255, 180, 200, 220),
                irr::video::SColor(255, 160, 180, 200),
                1.0f,
                1.0f
            };
    }
}

bool SkyRenderer::hasDayNightCycle(SkyCategory category) const {
    switch (category) {
        case SkyCategory::Normal:
        case SkyCategory::Luclin:
            return true;

        case SkyCategory::PoFire:
        case SkyCategory::PoStorms:
        case SkyCategory::PoAir:
        case SkyCategory::PoWar:
        case SkyCategory::TheGrey:
        case SkyCategory::PoTranq:
        case SkyCategory::Indoor:
            return false;

        default:
            return true;
    }
}

void SkyRenderer::updateCloudScrolling() {
    if (cloudLayerNodes_.empty()) {
        return;
    }

    // Apply UV scrolling to cloud layers via texture matrix
    // This creates a slow drifting cloud effect
    for (auto* node : cloudLayerNodes_) {
        if (!node) continue;

        for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
            irr::video::SMaterial& mat = node->getMaterial(i);

            // Create a texture matrix with UV offset for scrolling
            irr::core::matrix4 texMat;
            texMat.setTextureTranslate(cloudScrollOffset_, cloudScrollOffset_ * 0.5f);

            // Apply to texture layer 0
            mat.setTextureMatrix(0, texMat);
        }
    }
}

} // namespace Graphics
} // namespace EQT
