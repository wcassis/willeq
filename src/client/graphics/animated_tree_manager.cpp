#include "client/graphics/animated_tree_manager.h"
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/dds_decoder.h"
#include "common/logging.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace EQT {
namespace Graphics {

AnimatedTreeManager::AnimatedTreeManager(irr::scene::ISceneManager* smgr,
                                         irr::video::IVideoDriver* driver)
    : smgr_(smgr)
    , driver_(driver)
{
}

AnimatedTreeManager::~AnimatedTreeManager() {
    cleanup();
}

void AnimatedTreeManager::initialize(const std::vector<ObjectInstance>& objects,
                                     const std::map<std::string, std::shared_ptr<TextureInfo>>& textures) {
    if (!smgr_ || !driver_) {
        LOG_ERROR(MOD_GRAPHICS, "AnimatedTreeManager: Cannot initialize - missing dependencies");
        return;
    }

    cleanup();

    // Load config (uses defaults if no config file found)
    loadConfig();

    // Update distances from config
    const auto& config = windController_.getConfig();
    updateDistance_ = config.updateDistance;
    lodDistance_ = config.lodDistance;

    // Identify and create animated trees from placeables
    identifyTrees(objects, textures);

    initialized_ = true;

    LOG_INFO(MOD_GRAPHICS, "AnimatedTreeManager: Initialized with {} animated trees",
             animatedTrees_.size());
}

void AnimatedTreeManager::loadConfig(const std::string& configPath, const std::string& zoneName) {
    windController_.loadConfig(configPath, zoneName);
    treeIdentifier_.loadZoneOverrides(zoneName);

    // Update distances from config
    const auto& config = windController_.getConfig();
    updateDistance_ = config.updateDistance;
    lodDistance_ = config.lodDistance;
}

void AnimatedTreeManager::setWeather(WeatherType weather) {
    windController_.setWeather(weather);
}

void AnimatedTreeManager::update(float deltaTime, const irr::core::vector3df& cameraPos) {
    if (!initialized_ || !windController_.isEnabled()) {
        return;
    }

    // Update wind controller time
    windController_.update(deltaTime);

    // Update each tree based on distance from camera
    for (auto& tree : animatedTrees_) {
        if (!tree.node || tree.buffers.empty()) {
            continue;
        }

        // Calculate distance from camera to tree's world position
        float dist = tree.worldPosition.getDistanceFrom(cameraPos);

        // Hide if beyond render distance (respects global render distance setting)
        if (dist > renderDistance_) {
            tree.node->setVisible(false);
            continue;
        }

        // Show the tree
        tree.node->setVisible(true);

        // Only animate if within animation distance
        if (dist <= updateDistance_) {
            updateTreeAnimation(tree);
        }
    }
}

void AnimatedTreeManager::cleanup() {
    for (auto& tree : animatedTrees_) {
        if (tree.node) {
            tree.node->remove();
            tree.node = nullptr;
        }
        if (tree.mesh) {
            tree.mesh->drop();
            tree.mesh = nullptr;
        }
        tree.buffers.clear();  // Buffers are owned by mesh
    }
    animatedTrees_.clear();
    textureCache_.clear();  // Clear texture cache (driver still owns the textures)
    initialized_ = false;
}

void AnimatedTreeManager::identifyTrees(const std::vector<ObjectInstance>& objects,
                                        const std::map<std::string, std::shared_ptr<TextureInfo>>& textures) {
    LOG_DEBUG(MOD_GRAPHICS, "AnimatedTreeManager: Checking {} placeable objects for trees", objects.size());

    // Log first few object names for debugging
    size_t logCount = 0;
    for (const auto& obj : objects) {
        if (obj.placeable && logCount < 10) {
            std::string tex = (obj.geometry && !obj.geometry->textureNames.empty()) ?
                              obj.geometry->textureNames[0] : "(none)";
            LOG_DEBUG(MOD_GRAPHICS, "AnimatedTreeManager: Sample obj[{}]: name='{}' tex='{}' pos=({:.1f},{:.1f},{:.1f})",
                      logCount, obj.placeable->getName(), tex,
                      obj.placeable->getX(), obj.placeable->getY(), obj.placeable->getZ());
            logCount++;
        }
    }

    size_t treesFound = 0;
    size_t treesCreated = 0;

    for (const auto& obj : objects) {
        if (!obj.placeable || !obj.geometry || obj.geometry->vertices.empty()) {
            continue;
        }

        // Get placeable name (e.g., "TREE1", "TREE6")
        const std::string& objName = obj.placeable->getName();

        // Get primary texture name for this geometry
        std::string primaryTexture;
        if (!obj.geometry->textureNames.empty()) {
            primaryTexture = obj.geometry->textureNames[0];
        }

        // Check if this is a tree
        if (treeIdentifier_.isTreeMesh(objName, primaryTexture)) {
            treesFound++;
            LOG_DEBUG(MOD_GRAPHICS, "AnimatedTreeManager: Found tree object '{}' at ({:.1f},{:.1f},{:.1f}) tex='{}'",
                      objName, obj.placeable->getX(), obj.placeable->getY(), obj.placeable->getZ(),
                      primaryTexture);

            // Create animated copy
            createAnimatedTree(obj, textures);
            treesCreated++;
        }
    }

    LOG_INFO(MOD_GRAPHICS, "AnimatedTreeManager: Found {} tree objects, created {} animated copies",
             treesFound, treesCreated);
}

void AnimatedTreeManager::createAnimatedTree(const ObjectInstance& object,
                                             const std::map<std::string, std::shared_ptr<TextureInfo>>& textures) {
    if (!object.placeable || !object.geometry || object.geometry->vertices.empty()) {
        return;
    }

    const auto& geometry = object.geometry;
    const auto& placeable = object.placeable;

    // Build the animated mesh
    irr::scene::SMesh* mesh = buildAnimatedMesh(*geometry, textures);
    if (!mesh || mesh->getMeshBufferCount() == 0) {
        if (mesh) mesh->drop();
        LOG_WARN(MOD_GRAPHICS, "AnimatedTreeManager: Failed to build mesh for tree '{}'",
                 placeable->getName());
        return;
    }

    // Create the animated tree structure
    AnimatedTree tree;
    tree.mesh = mesh;
    tree.name = placeable->getName();

    // Get world position from placeable (EQ coords -> Irrlicht coords)
    // EQ (x, y, z) -> Irrlicht (x, z, y)
    tree.worldPosition = irr::core::vector3df(
        placeable->getX(),
        placeable->getZ(),
        placeable->getY()
    );

    // Generate unique seed based on position
    tree.meshSeed = generateTreeSeed(tree.worldPosition);

    // Find Y range in Irrlicht coordinates (local mesh bounds)
    // For placeable objects, the geometry is in local space with center at or near origin
    float minY = geometry->minZ;  // EQ Z -> Irrlicht Y
    float maxY = geometry->maxZ;

    // Process each mesh buffer
    size_t totalVerts = 0;
    for (irr::u32 bufIdx = 0; bufIdx < mesh->getMeshBufferCount(); ++bufIdx) {
        irr::scene::SMeshBuffer* buffer = static_cast<irr::scene::SMeshBuffer*>(
            mesh->getMeshBuffer(bufIdx));

        if (!buffer || buffer->getVertexCount() == 0) {
            continue;
        }

        AnimatedBuffer animBuf;
        animBuf.buffer = buffer;

        irr::u32 vertCount = buffer->getVertexCount();
        animBuf.basePositions.resize(vertCount);
        animBuf.vertexHeights.resize(vertCount);

        irr::video::S3DVertex* verts = static_cast<irr::video::S3DVertex*>(buffer->getVertices());

        for (irr::u32 i = 0; i < vertCount; ++i) {
            animBuf.basePositions[i] = verts[i].Pos;

            // Calculate normalized height (0 = base, 1 = top)
            // Vertex Y is in local Irrlicht space (EQ Z transformed)
            animBuf.vertexHeights[i] = calculateVertexHeight(
                verts[i].Pos, minY, maxY);
        }

        // Use dynamic hint for vertex buffer since we'll be updating it
        buffer->setHardwareMappingHint(irr::scene::EHM_DYNAMIC, irr::scene::EBT_VERTEX);

        totalVerts += vertCount;
        tree.buffers.push_back(std::move(animBuf));
    }

    if (tree.buffers.empty()) {
        mesh->drop();
        return;
    }

    // Calculate bounds
    tree.bounds = mesh->getBoundingBox();

    // Create scene node
    tree.node = smgr_->addMeshSceneNode(mesh);
    if (tree.node) {
        // Position at placeable location
        tree.node->setPosition(tree.worldPosition);

        // Apply placeable rotation - same mapping as static objects in irrlicht_renderer.cpp
        // Internal rotation format matches eqsage Location
        float rotX = placeable->getRotateX();  // Always 0
        float rotY = placeable->getRotateY();  // Yaw (matches eqsage Location.rotateY)
        float rotZ = placeable->getRotateZ();  // Secondary rotation (matches eqsage Location.rotateZ)
        tree.node->setRotation(irr::core::vector3df(rotX, rotY, rotZ));

        // Apply placeable scale - same mapping as static objects
        // EQ scale (x, y, z) -> Irrlicht scale (x, z, y)
        float scaleX = placeable->getScaleX();
        float scaleY = placeable->getScaleY();
        float scaleZ = placeable->getScaleZ();
        tree.node->setScale(irr::core::vector3df(scaleX, scaleZ, scaleY));

        // Set material properties with alpha transparency for tree leaves
        for (irr::u32 i = 0; i < tree.node->getMaterialCount(); ++i) {
            auto& mat = tree.node->getMaterial(i);
            mat.Lighting = false;
            mat.BackfaceCulling = false;
            mat.FogEnable = true;
            // Use alpha test for transparent leaves (alpha channel cutout)
            mat.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;
            mat.MaterialTypeParam = 0.5f;  // Alpha threshold (0-1)
        }

        animatedTrees_.push_back(std::move(tree));

        LOG_DEBUG(MOD_GRAPHICS, "AnimatedTreeManager: Created animated tree '{}' at ({:.1f}, {:.1f}, {:.1f}) "
                  "scale=({:.2f},{:.2f},{:.2f}) with {} vertices in {} buffers",
                  placeable->getName(), tree.worldPosition.X, tree.worldPosition.Y, tree.worldPosition.Z,
                  scaleX, scaleY, scaleZ, totalVerts, tree.buffers.size());
    } else {
        mesh->drop();
    }
}

void AnimatedTreeManager::updateTreeAnimation(AnimatedTree& tree) {
    if (tree.buffers.empty()) {
        return;
    }

    // Update each buffer
    for (auto& animBuf : tree.buffers) {
        if (!animBuf.buffer || animBuf.basePositions.empty()) {
            continue;
        }

        irr::u32 vertCount = animBuf.buffer->getVertexCount();
        if (vertCount == 0 || vertCount > animBuf.basePositions.size()) {
            continue;
        }

        irr::video::S3DVertex* verts = static_cast<irr::video::S3DVertex*>(animBuf.buffer->getVertices());

        bool anyChanged = false;

        for (irr::u32 i = 0; i < vertCount; ++i) {
            float height = animBuf.vertexHeights[i];

            // Get base position
            irr::core::vector3df basePos = animBuf.basePositions[i];

            // Calculate world position for wind calculation
            // (add tree world position since basePos is relative to center)
            irr::core::vector3df worldPos = basePos + tree.worldPosition;

            // Get wind displacement
            irr::core::vector3df displacement = windController_.getDisplacement(
                worldPos, height, tree.meshSeed);

            // Apply displacement to base position
            irr::core::vector3df newPos = basePos + displacement;

            if (verts[i].Pos != newPos) {
                verts[i].Pos = newPos;
                anyChanged = true;
            }
        }

        // Mark buffer as dirty if vertices changed
        if (anyChanged) {
            animBuf.buffer->setDirty(irr::scene::EBT_VERTEX);
        }
    }
}

float AnimatedTreeManager::calculateVertexHeight(const irr::core::vector3df& vertex,
                                                  float minY, float maxY) const {
    float range = maxY - minY;
    if (range < 0.001f) {
        return 0.5f;  // Default to middle if no range
    }

    float normalized = (vertex.Y - minY) / range;
    return std::clamp(normalized, 0.0f, 1.0f);
}

float AnimatedTreeManager::generateTreeSeed(const irr::core::vector3df& position) const {
    // Generate a pseudo-random seed based on position
    // This ensures the same tree always gets the same seed
    float seed = std::sin(position.X * 12.9898f + position.Z * 78.233f) * 43758.5453f;
    seed = seed - std::floor(seed);  // Get fractional part
    return seed * 6.28318f;  // Scale to 0-2PI for use as phase offset
}

irr::video::ITexture* AnimatedTreeManager::getOrLoadTexture(
    const std::string& name,
    const std::map<std::string, std::shared_ptr<TextureInfo>>& textures) {

    if (name.empty() || !driver_) {
        return nullptr;
    }

    // Check cache first
    auto cacheIt = textureCache_.find(name);
    if (cacheIt != textureCache_.end()) {
        return cacheIt->second;
    }

    // Find texture data
    auto texIt = textures.find(name);
    if (texIt == textures.end() || !texIt->second || texIt->second->data.empty()) {
        // Try lowercase
        std::string lowerName = name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
        texIt = textures.find(lowerName);
        if (texIt == textures.end() || !texIt->second || texIt->second->data.empty()) {
            textureCache_[name] = nullptr;
            return nullptr;
        }
    }

    const auto& data = texIt->second->data;
    irr::video::ITexture* texture = nullptr;

    // Check if DDS format
    if (DDSDecoder::isDDS(data)) {
        DecodedImage decoded = DDSDecoder::decode(data);
        if (decoded.isValid()) {
            // Convert RGBA to ARGB for Irrlicht
            std::vector<uint32_t> argbPixels(decoded.width * decoded.height);
            for (uint32_t i = 0; i < decoded.width * decoded.height; ++i) {
                uint8_t r = decoded.pixels[i * 4 + 0];
                uint8_t g = decoded.pixels[i * 4 + 1];
                uint8_t b = decoded.pixels[i * 4 + 2];
                uint8_t a = decoded.pixels[i * 4 + 3];
                argbPixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
            }

            // Create Irrlicht image
            irr::core::dimension2d<irr::u32> size(decoded.width, decoded.height);
            irr::video::IImage* image = driver_->createImageFromData(
                irr::video::ECF_A8R8G8B8, size, argbPixels.data(), false, false);

            if (image) {
                // Generate unique texture name
                std::string texName = "tree_" + name;
                texture = driver_->addTexture(texName.c_str(), image);
                image->drop();
            }
        }
    }

    textureCache_[name] = texture;
    return texture;
}

irr::scene::SMesh* AnimatedTreeManager::buildAnimatedMesh(
    const ZoneGeometry& geometry,
    const std::map<std::string, std::shared_ptr<TextureInfo>>& textures) {

    if (geometry.vertices.empty() || geometry.triangles.empty()) {
        return nullptr;
    }

    // Group triangles by texture
    std::map<size_t, std::vector<size_t>> trianglesByTexture;
    for (size_t i = 0; i < geometry.triangles.size(); ++i) {
        const auto& tri = geometry.triangles[i];

        // Skip invisible textures
        if (tri.textureIndex < geometry.textureInvisible.size() &&
            geometry.textureInvisible[tri.textureIndex]) {
            continue;
        }

        trianglesByTexture[tri.textureIndex].push_back(i);
    }

    if (trianglesByTexture.empty()) {
        return nullptr;
    }

    irr::scene::SMesh* mesh = new irr::scene::SMesh();

    // Create a mesh buffer for each texture
    for (const auto& [texIdx, triIndices] : trianglesByTexture) {
        if (triIndices.empty()) continue;

        irr::scene::SMeshBuffer* buffer = new irr::scene::SMeshBuffer();

        // Get texture for this group
        std::string texName;
        if (texIdx < geometry.textureNames.size()) {
            texName = geometry.textureNames[texIdx];
        }

        irr::video::ITexture* texture = getOrLoadTexture(texName, textures);

        // Set material
        buffer->Material.BackfaceCulling = false;
        buffer->Material.Lighting = false;
        buffer->Material.FogEnable = true;

        if (texture) {
            buffer->Material.setTexture(0, texture);
            buffer->Material.MaterialType = irr::video::EMT_SOLID;
            buffer->Material.setFlag(irr::video::EMF_BILINEAR_FILTER, true);
            buffer->Material.TextureLayer[0].TextureWrapU = irr::video::ETC_REPEAT;
            buffer->Material.TextureLayer[0].TextureWrapV = irr::video::ETC_REPEAT;
        } else {
            buffer->Material.MaterialType = irr::video::EMT_SOLID;
        }

        // Track vertices added to this buffer
        std::unordered_map<size_t, irr::u16> globalToLocal;

        for (size_t triIdx : triIndices) {
            const auto& tri = geometry.triangles[triIdx];

            for (size_t vidx : {tri.v1, tri.v2, tri.v3}) {
                if (globalToLocal.find(vidx) == globalToLocal.end()) {
                    if (vidx >= geometry.vertices.size()) continue;

                    const auto& v = geometry.vertices[vidx];
                    irr::video::S3DVertex vertex;

                    // EQ (x, y, z) -> Irrlicht (x, z, y)
                    vertex.Pos.X = v.x;
                    vertex.Pos.Y = v.z;
                    vertex.Pos.Z = v.y;
                    vertex.Normal.X = v.nx;
                    vertex.Normal.Y = v.nz;
                    vertex.Normal.Z = v.ny;
                    vertex.TCoords.X = v.u;
                    vertex.TCoords.Y = v.v;

                    // White color for textured, green gradient for untextured
                    if (texture) {
                        vertex.Color = irr::video::SColor(255, 255, 255, 255);
                    } else {
                        float t = (geometry.maxZ > geometry.minZ) ?
                            (v.z - geometry.minZ) / (geometry.maxZ - geometry.minZ) : 0.5f;
                        irr::u8 g = static_cast<irr::u8>(100 + t * 155);
                        vertex.Color = irr::video::SColor(255, 50, g, 50);
                    }

                    globalToLocal[vidx] = static_cast<irr::u16>(buffer->Vertices.size());
                    buffer->Vertices.push_back(vertex);
                }
            }

            // Add indices
            if (globalToLocal.count(tri.v1) && globalToLocal.count(tri.v2) && globalToLocal.count(tri.v3)) {
                buffer->Indices.push_back(globalToLocal[tri.v1]);
                buffer->Indices.push_back(globalToLocal[tri.v2]);
                buffer->Indices.push_back(globalToLocal[tri.v3]);
            }
        }

        if (!buffer->Vertices.empty()) {
            buffer->recalculateBoundingBox();
            mesh->addMeshBuffer(buffer);
        }
        buffer->drop();  // Mesh now owns it (or it gets cleaned up if empty)
    }

    if (mesh->getMeshBufferCount() == 0) {
        mesh->drop();
        return nullptr;
    }

    mesh->recalculateBoundingBox();
    return mesh;
}

std::string AnimatedTreeManager::getDebugInfo() const {
    std::ostringstream ss;
    ss << "Trees: " << animatedTrees_.size();
    ss << " | Wind: " << (windController_.isEnabled() ? "ON" : "OFF");
    ss << " | Strength: " << windController_.getCurrentStrength();
    ss << " | Weather: " << static_cast<int>(windController_.getWeather());
    return ss.str();
}

} // namespace Graphics
} // namespace EQT
