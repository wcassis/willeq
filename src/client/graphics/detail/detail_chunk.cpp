#include "client/graphics/detail/detail_chunk.h"
#include "client/graphics/detail/wind_controller.h"
#include "client/graphics/detail/foliage_disturbance.h"
#include "common/logging.h"
#include <random>
#include <cmath>
#include <map>
#include <algorithm>

namespace EQT {
namespace Graphics {
namespace Detail {

DetailChunk::DetailChunk(const ChunkKey& key, float size,
                         irr::scene::ISceneManager* smgr,
                         irr::video::IVideoDriver* driver)
    : key_(key)
    , size_(size)
    , smgr_(smgr)
    , driver_(driver)
{
    // Calculate world-space bounds for this chunk
    float worldX = key.x * size;
    float worldZ = key.z * size;
    bounds_.MinEdge = irr::core::vector3df(worldX, -1000.0f, worldZ);
    bounds_.MaxEdge = irr::core::vector3df(worldX + size, 1000.0f, worldZ + size);

    // Create mesh and buffer
    mesh_ = new irr::scene::SMesh();
    buffer_ = new irr::scene::SMeshBuffer();
    mesh_->addMeshBuffer(buffer_);
    buffer_->drop();  // Mesh now owns the buffer
}

DetailChunk::~DetailChunk() {
    detach();
    if (mesh_) {
        mesh_->drop();
        mesh_ = nullptr;
    }
}

void DetailChunk::generatePlacements(const ZoneDetailConfig& config,
                                     GroundQueryFunc groundQuery,
                                     ExclusionCheckFunc exclusionCheck,
                                     uint32_t seed) {
    if (placementsGenerated_) return;

    placements_.clear();

    // Deterministic RNG based on chunk position
    std::mt19937 rng(seed ^ (key_.x * 73856093) ^ (key_.z * 19349663));
    std::uniform_real_distribution<float> posDist(0.0f, 1.0f);
    std::uniform_real_distribution<float> rotDist(0.0f, 6.28318f);  // 2*PI

    // Base placements per chunk (at density 1.0)
    // Target ~300 per chunk at max density for 50x50 unit chunks
    float basePerUnit = 0.12f * config.densityMultiplier;  // ~300 per 2500 sq units
    float area = size_ * size_;
    int basePlacementsTotal = static_cast<int>(area * basePerUnit);

    // Generate placements for each detail type
    for (size_t typeIdx = 0; typeIdx < config.detailTypes.size(); ++typeIdx) {
        const auto& type = config.detailTypes[typeIdx];

        int typeCount = static_cast<int>(basePlacementsTotal * type.baseDensity /
                                         config.detailTypes.size());

        for (int i = 0; i < typeCount; ++i) {
            DetailPlacement p;

            // Random position within chunk bounds
            float localX = posDist(rng) * size_;
            float localZ = posDist(rng) * size_;
            p.position.X = bounds_.MinEdge.X + localX;
            p.position.Z = bounds_.MinEdge.Z + localZ;

            // Query ground height and surface type
            irr::core::vector3df normal;
            SurfaceType surfaceType = SurfaceType::Unknown;
            if (groundQuery) {
                if (!groundQuery(p.position.X, p.position.Z, p.position.Y, normal, surfaceType)) {
                    continue;  // No valid ground here
                }

                // Add small offset to raise details above the ground surface
                // This prevents z-fighting and ensures details aren't buried
                p.position.Y += 0.1f;

                // Check slope (normal.Y is cos of angle from vertical in Irrlicht Y-up coords)
                float slope = std::acos(std::clamp(normal.Y, -1.0f, 1.0f));
                if (slope < type.minSlope || slope > type.maxSlope) {
                    continue;
                }

                // Check surface type compatibility
                if ((type.allowedSurfaces & surfaceType) == 0) {
                    continue;  // This detail type not allowed on this surface
                }
            } else {
                // No ground query - use flat ground at Y=0 for testing
                p.position.Y = 0.0f;
            }

            // Check exclusion zones
            if (exclusionCheck && exclusionCheck(p.position)) {
                continue;
            }

            // Random rotation and scale
            p.rotation = rotDist(rng);
            std::uniform_real_distribution<float> scaleDist(type.minSize, type.maxSize);
            p.scale = scaleDist(rng);

            p.typeIndex = static_cast<uint16_t>(typeIdx);
            p.seed = static_cast<uint8_t>(rng() & 0xFF);

            placements_.push_back(p);
        }
    }

    // Debug: Log placement counts and Y values per type
    std::map<uint16_t, std::vector<float>> yValuesByType;
    for (const auto& p : placements_) {
        yValuesByType[p.typeIndex].push_back(p.position.Y);
    }

    for (const auto& [typeIdx, yValues] : yValuesByType) {
        if (typeIdx < config.detailTypes.size()) {
            float minY = *std::min_element(yValues.begin(), yValues.end());
            float maxY = *std::max_element(yValues.begin(), yValues.end());
            float avgY = 0;
            for (float y : yValues) avgY += y;
            avgY /= yValues.size();

            LOG_DEBUG(MOD_GRAPHICS, "DetailChunk ({},{}): type '{}' count={}, Y range=[{:.1f}, {:.1f}], avgY={:.1f}",
                     key_.x, key_.z, config.detailTypes[typeIdx].name, yValues.size(), minY, maxY, avgY);
        }
    }

    placementsGenerated_ = true;
}

void DetailChunk::rebuildMesh(float density, uint32_t categoryMask,
                              const ZoneDetailConfig& config,
                              irr::video::ITexture* atlas) {
    // Skip if nothing changed
    if (density == lastDensity_ && categoryMask == lastCategoryMask_) {
        return;
    }

    lastDensity_ = density;
    lastCategoryMask_ = categoryMask;

    // Count placements that pass density and category filters
    std::vector<size_t> selectedIndices;
    selectedIndices.reserve(placements_.size());

    for (size_t i = 0; i < placements_.size(); ++i) {
        const auto& p = placements_[i];
        if (p.typeIndex >= config.detailTypes.size()) continue;

        const auto& type = config.detailTypes[p.typeIndex];

        // Check category
        if ((categoryMask & type.category) == 0) continue;

        // Deterministic density thinning based on placement seed
        float threshold = static_cast<float>(p.seed) / 255.0f;
        if (threshold >= density) continue;

        selectedIndices.push_back(i);
    }

    visibleCount_ = selectedIndices.size();

    // Early out if nothing to render
    if (selectedIndices.empty()) {
        buffer_->Vertices.clear();
        buffer_->Indices.clear();
        buffer_->recalculateBoundingBox();
        mesh_->setBoundingBox(bounds_);
        return;
    }

    // Build geometry
    std::vector<irr::video::S3DVertex> vertices;
    std::vector<irr::u16> indices;

    // Reserve space (crossed quads = 8 verts, 24 indices per placement)
    vertices.reserve(selectedIndices.size() * 8);
    indices.reserve(selectedIndices.size() * 24);

    bool useTestColor = (atlas == nullptr);

    for (size_t idx : selectedIndices) {
        const auto& p = placements_[idx];
        const auto& type = config.detailTypes[p.typeIndex];
        buildQuadGeometry(p, type, vertices, indices, useTestColor, config.seasonTint);
    }

    // Copy to buffer
    buffer_->Vertices.set_used(0);
    buffer_->Vertices.reallocate(vertices.size());
    for (const auto& v : vertices) {
        buffer_->Vertices.push_back(v);
    }

    buffer_->Indices.set_used(0);
    buffer_->Indices.reallocate(indices.size());
    for (const auto& i : indices) {
        buffer_->Indices.push_back(i);
    }

    buffer_->recalculateBoundingBox();
    mesh_->setBoundingBox(buffer_->getBoundingBox());

    // Setup material
    setupMaterial(atlas);

    // Store base positions and wind influence for wind animation
    basePositions_.clear();
    windInfluence_.clear();
    basePositions_.reserve(buffer_->getVertexCount());
    windInfluence_.reserve(buffer_->getVertexCount());

    // Copy base positions from buffer
    irr::video::S3DVertex* bufferVerts = static_cast<irr::video::S3DVertex*>(buffer_->getVertices());
    for (irr::u32 i = 0; i < buffer_->getVertexCount(); ++i) {
        basePositions_.push_back(bufferVerts[i].Pos);
    }

    // Calculate wind influence based on vertex pattern
    // We process vertices in groups matching the geometry we built:
    // - CrossedQuads: 8 verts (0,1,4,5 = bottom, 2,3,6,7 = top)
    // - FlatGround: 4 verts (all at ground, no wind)
    // - SingleQuad: 4 verts (0,1 = bottom, 2,3 = top)
    size_t vertIdx = 0;
    for (size_t idx : selectedIndices) {
        const auto& p = placements_[idx];
        const auto& type = config.detailTypes[p.typeIndex];

        if (type.orientation == DetailOrientation::CrossedQuads) {
            // 8 vertices: bottom (0,1), top (2,3), bottom (4,5), top (6,7)
            windInfluence_.push_back(0.0f);                        // 0: bottom
            windInfluence_.push_back(0.0f);                        // 1: bottom
            windInfluence_.push_back(type.windResponse);           // 2: top
            windInfluence_.push_back(type.windResponse);           // 3: top
            windInfluence_.push_back(0.0f);                        // 4: bottom
            windInfluence_.push_back(0.0f);                        // 5: bottom
            windInfluence_.push_back(type.windResponse);           // 6: top
            windInfluence_.push_back(type.windResponse);           // 7: top
            vertIdx += 8;
        } else if (type.orientation == DetailOrientation::FlatGround) {
            // 4 vertices: all at ground level, no wind effect
            windInfluence_.push_back(0.0f);
            windInfluence_.push_back(0.0f);
            windInfluence_.push_back(0.0f);
            windInfluence_.push_back(0.0f);
            vertIdx += 4;
        } else {
            // SingleQuad: 4 vertices (0,1 = bottom, 2,3 = top)
            windInfluence_.push_back(0.0f);                        // 0: bottom
            windInfluence_.push_back(0.0f);                        // 1: bottom
            windInfluence_.push_back(type.windResponse);           // 2: top
            windInfluence_.push_back(type.windResponse);           // 3: top
            vertIdx += 4;
        }
    }
}

void DetailChunk::buildQuadGeometry(const DetailPlacement& p,
                                    const DetailType& type,
                                    std::vector<irr::video::S3DVertex>& verts,
                                    std::vector<irr::u16>& indices,
                                    bool useTestColor,
                                    const irr::video::SColor& seasonTint) {
    irr::video::SColor baseColor = useTestColor ? type.testColor : irr::video::SColor(255, 255, 255, 255);

    // Apply seasonal tint by modulating the base color
    irr::video::SColor color(
        baseColor.getAlpha(),
        (baseColor.getRed() * seasonTint.getRed()) / 255,
        (baseColor.getGreen() * seasonTint.getGreen()) / 255,
        (baseColor.getBlue() * seasonTint.getBlue()) / 255
    );

    // Add per-instance color variation based on category
    // Use the placement seed to create deterministic but varied tints
    if (!useTestColor) {
        int seedVal = p.seed;

        if (type.category == DetailCategory::Grass) {
            // ~10% chance to be dead/dying (brown/tan) grass
            if ((seedVal % 10) == 0) {
                // Brown/tan dead grass - reduce green, increase red
                color.setRed(std::clamp(180 + ((seedVal * 7) % 40), 0, 255));   // 180-220
                color.setGreen(std::clamp(140 + ((seedVal * 11) % 40), 0, 255)); // 140-180
                color.setBlue(std::clamp(80 + ((seedVal * 13) % 30), 0, 255));   // 80-110
            } else {
                // Normal green grass with variation
                int rVar = ((seedVal * 7) % 31) - 15;
                int gVar = ((seedVal * 13) % 31) - 15;
                int bVar = ((seedVal * 19) % 21) - 10;
                color.setRed(std::clamp(static_cast<int>(color.getRed()) + rVar, 0, 255));
                color.setGreen(std::clamp(static_cast<int>(color.getGreen()) + gVar, 0, 255));
                color.setBlue(std::clamp(static_cast<int>(color.getBlue()) + bVar, 0, 255));
            }
        } else if (type.category == DetailCategory::Plants) {
            // Flowers - vary petal color slightly, ~15% chance for different colored flowers
            if ((seedVal % 7) == 0) {
                // Yellow/orange flowers
                color.setRed(std::clamp(220 + ((seedVal * 3) % 35), 0, 255));
                color.setGreen(std::clamp(180 + ((seedVal * 7) % 50), 0, 255));
                color.setBlue(std::clamp(50 + ((seedVal * 11) % 40), 0, 255));
            } else if ((seedVal % 7) == 1) {
                // Blue/purple flowers
                color.setRed(std::clamp(140 + ((seedVal * 5) % 60), 0, 255));
                color.setGreen(std::clamp(100 + ((seedVal * 9) % 50), 0, 255));
                color.setBlue(std::clamp(200 + ((seedVal * 13) % 55), 0, 255));
            } else {
                // Normal variation
                int var = ((seedVal * 7) % 21) - 10;
                color.setRed(std::clamp(static_cast<int>(color.getRed()) + var, 0, 255));
                color.setGreen(std::clamp(static_cast<int>(color.getGreen()) + var / 2, 0, 255));
                color.setBlue(std::clamp(static_cast<int>(color.getBlue()) + var, 0, 255));
            }
        } else if (type.category == DetailCategory::Rocks) {
            // Rocks - vary gray tones, some mossy
            if ((seedVal % 8) == 0) {
                // Mossy rock - add green tint
                color.setRed(std::clamp(100 + ((seedVal * 5) % 40), 0, 255));
                color.setGreen(std::clamp(130 + ((seedVal * 9) % 50), 0, 255));
                color.setBlue(std::clamp(90 + ((seedVal * 7) % 30), 0, 255));
            } else {
                // Normal rock variation - vary brightness
                int var = ((seedVal * 11) % 41) - 20;  // -20 to +20
                color.setRed(std::clamp(static_cast<int>(color.getRed()) + var, 0, 255));
                color.setGreen(std::clamp(static_cast<int>(color.getGreen()) + var, 0, 255));
                color.setBlue(std::clamp(static_cast<int>(color.getBlue()) + var, 0, 255));
            }
        } else if (type.category == DetailCategory::Mushrooms) {
            // Mushrooms - vary cap color
            if ((seedVal % 5) == 0) {
                // Brown mushroom
                color.setRed(std::clamp(160 + ((seedVal * 7) % 40), 0, 255));
                color.setGreen(std::clamp(120 + ((seedVal * 11) % 40), 0, 255));
                color.setBlue(std::clamp(80 + ((seedVal * 13) % 30), 0, 255));
            } else if ((seedVal % 5) == 1) {
                // Pale/white mushroom
                color.setRed(std::clamp(220 + ((seedVal * 3) % 35), 0, 255));
                color.setGreen(std::clamp(215 + ((seedVal * 5) % 40), 0, 255));
                color.setBlue(std::clamp(200 + ((seedVal * 7) % 40), 0, 255));
            } else {
                // Normal variation
                int var = ((seedVal * 7) % 31) - 15;
                color.setRed(std::clamp(static_cast<int>(color.getRed()) + var, 0, 255));
                color.setGreen(std::clamp(static_cast<int>(color.getGreen()) + var / 2, 0, 255));
                color.setBlue(std::clamp(static_cast<int>(color.getBlue()) + var / 2, 0, 255));
            }
        } else if (type.category == DetailCategory::Debris) {
            // Debris - vary brown tones
            int var = ((seedVal * 13) % 41) - 20;  // -20 to +20
            color.setRed(std::clamp(static_cast<int>(color.getRed()) + var, 0, 255));
            color.setGreen(std::clamp(static_cast<int>(color.getGreen()) + var * 3 / 4, 0, 255));
            color.setBlue(std::clamp(static_cast<int>(color.getBlue()) + var / 2, 0, 255));
        }
    }

    float halfSize = p.scale * 0.5f;

    // UV coordinates
    float u0 = type.u0;
    float v0 = type.v0;
    float u1 = type.u1;
    float v1 = type.v1;

    // Debug: log UV coordinates for all placements
    LOG_DEBUG(MOD_GRAPHICS, "DetailChunk UV debug: type='{}' typeIdx={} uv=({:.3f},{:.3f})-({:.3f},{:.3f})",
              type.name, p.typeIndex, u0, v0, u1, v1);

    irr::u16 baseIdx = static_cast<irr::u16>(verts.size());

    float sinR = std::sin(p.rotation);
    float cosR = std::cos(p.rotation);

    if (type.orientation == DetailOrientation::CrossedQuads) {
        // First quad (aligned with rotation)
        irr::core::vector3df right(cosR * halfSize, 0, sinR * halfSize);
        irr::core::vector3df up(0, p.scale, 0);

        // Vertices: bottom-left, bottom-right, top-right, top-left
        verts.push_back(irr::video::S3DVertex(
            p.position - right, irr::core::vector3df(0, 1, 0), color, irr::core::vector2df(u0, v1)));
        verts.push_back(irr::video::S3DVertex(
            p.position + right, irr::core::vector3df(0, 1, 0), color, irr::core::vector2df(u1, v1)));
        verts.push_back(irr::video::S3DVertex(
            p.position + right + up, irr::core::vector3df(0, 1, 0), color, irr::core::vector2df(u1, v0)));
        verts.push_back(irr::video::S3DVertex(
            p.position - right + up, irr::core::vector3df(0, 1, 0), color, irr::core::vector2df(u0, v0)));

        // Second quad (perpendicular)
        irr::core::vector3df right2(-sinR * halfSize, 0, cosR * halfSize);

        verts.push_back(irr::video::S3DVertex(
            p.position - right2, irr::core::vector3df(0, 1, 0), color, irr::core::vector2df(u0, v1)));
        verts.push_back(irr::video::S3DVertex(
            p.position + right2, irr::core::vector3df(0, 1, 0), color, irr::core::vector2df(u1, v1)));
        verts.push_back(irr::video::S3DVertex(
            p.position + right2 + up, irr::core::vector3df(0, 1, 0), color, irr::core::vector2df(u1, v0)));
        verts.push_back(irr::video::S3DVertex(
            p.position - right2 + up, irr::core::vector3df(0, 1, 0), color, irr::core::vector2df(u0, v0)));

        // Indices for both quads (both sides visible - 24 total)
        // First quad front
        indices.push_back(baseIdx + 0);
        indices.push_back(baseIdx + 1);
        indices.push_back(baseIdx + 2);
        indices.push_back(baseIdx + 0);
        indices.push_back(baseIdx + 2);
        indices.push_back(baseIdx + 3);
        // First quad back
        indices.push_back(baseIdx + 0);
        indices.push_back(baseIdx + 2);
        indices.push_back(baseIdx + 1);
        indices.push_back(baseIdx + 0);
        indices.push_back(baseIdx + 3);
        indices.push_back(baseIdx + 2);
        // Second quad front
        indices.push_back(baseIdx + 4);
        indices.push_back(baseIdx + 5);
        indices.push_back(baseIdx + 6);
        indices.push_back(baseIdx + 4);
        indices.push_back(baseIdx + 6);
        indices.push_back(baseIdx + 7);
        // Second quad back
        indices.push_back(baseIdx + 4);
        indices.push_back(baseIdx + 6);
        indices.push_back(baseIdx + 5);
        indices.push_back(baseIdx + 4);
        indices.push_back(baseIdx + 7);
        indices.push_back(baseIdx + 6);

    } else if (type.orientation == DetailOrientation::FlatGround) {
        // Flat quad on ground (slightly elevated to avoid z-fighting)
        float corners[4][2] = {
            {-halfSize * cosR - halfSize * sinR, -halfSize * sinR + halfSize * cosR},
            { halfSize * cosR - halfSize * sinR,  halfSize * sinR + halfSize * cosR},
            { halfSize * cosR + halfSize * sinR,  halfSize * sinR - halfSize * cosR},
            {-halfSize * cosR + halfSize * sinR, -halfSize * sinR - halfSize * cosR}
        };

        for (int i = 0; i < 4; ++i) {
            irr::core::vector3df pos = p.position;
            pos.X += corners[i][0];
            pos.Y += 0.02f;  // Slight elevation
            pos.Z += corners[i][1];

            float u = (i == 0 || i == 3) ? u0 : u1;
            float v = (i == 0 || i == 1) ? v0 : v1;

            verts.push_back(irr::video::S3DVertex(
                pos, irr::core::vector3df(0, 1, 0), color, irr::core::vector2df(u, v)));
        }

        // Indices (both sides)
        indices.push_back(baseIdx + 0);
        indices.push_back(baseIdx + 1);
        indices.push_back(baseIdx + 2);
        indices.push_back(baseIdx + 0);
        indices.push_back(baseIdx + 2);
        indices.push_back(baseIdx + 3);
        indices.push_back(baseIdx + 0);
        indices.push_back(baseIdx + 2);
        indices.push_back(baseIdx + 1);
        indices.push_back(baseIdx + 0);
        indices.push_back(baseIdx + 3);
        indices.push_back(baseIdx + 2);

    } else {
        // SingleQuad - single vertical quad facing rotation direction
        irr::core::vector3df right(cosR * halfSize, 0, sinR * halfSize);
        irr::core::vector3df up(0, p.scale, 0);

        verts.push_back(irr::video::S3DVertex(
            p.position - right, irr::core::vector3df(-sinR, 0, cosR), color, irr::core::vector2df(u0, v1)));
        verts.push_back(irr::video::S3DVertex(
            p.position + right, irr::core::vector3df(-sinR, 0, cosR), color, irr::core::vector2df(u1, v1)));
        verts.push_back(irr::video::S3DVertex(
            p.position + right + up, irr::core::vector3df(-sinR, 0, cosR), color, irr::core::vector2df(u1, v0)));
        verts.push_back(irr::video::S3DVertex(
            p.position - right + up, irr::core::vector3df(-sinR, 0, cosR), color, irr::core::vector2df(u0, v0)));

        // Both sides
        indices.push_back(baseIdx + 0);
        indices.push_back(baseIdx + 1);
        indices.push_back(baseIdx + 2);
        indices.push_back(baseIdx + 0);
        indices.push_back(baseIdx + 2);
        indices.push_back(baseIdx + 3);
        indices.push_back(baseIdx + 0);
        indices.push_back(baseIdx + 2);
        indices.push_back(baseIdx + 1);
        indices.push_back(baseIdx + 0);
        indices.push_back(baseIdx + 3);
        indices.push_back(baseIdx + 2);
    }
}

void DetailChunk::setupMaterial(irr::video::ITexture* atlas) {
    // Store atlas for later use in attach()
    atlasTexture_ = atlas;

    irr::video::SMaterial& mat = buffer_->Material;

    if (atlas) {
        // Use alpha channel transparency for detail sprites
        mat.MaterialType = irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL;
        mat.setTexture(0, atlas);
    } else {
        // No texture - use solid colored quads for testing
        mat.MaterialType = irr::video::EMT_SOLID;
    }

    // Enable lighting so grass responds to scene/player lighting
    mat.Lighting = true;
    mat.AmbientColor = irr::video::SColor(255, 255, 255, 255);
    mat.DiffuseColor = irr::video::SColor(255, 255, 255, 255);

    // Enable fog to match zone
    mat.FogEnable = true;

    // Both sides visible
    mat.BackfaceCulling = false;

    // Write to Z-buffer for proper sorting
    mat.ZWriteEnable = true;
    mat.ZBuffer = irr::video::ECFN_LESSEQUAL;
}

void DetailChunk::applyWind(const WindController& wind, const ZoneDetailConfig& config) {
    if (!buffer_ || basePositions_.empty() || windInfluence_.empty()) {
        return;
    }

    // Skip if no wind effect in this zone
    if (config.windStrength < 0.001f) {
        return;
    }

    irr::u32 vertCount = buffer_->getVertexCount();
    if (vertCount == 0 || vertCount > basePositions_.size()) {
        return;
    }

    // Get direct access to vertex buffer
    irr::video::S3DVertex* verts = static_cast<irr::video::S3DVertex*>(buffer_->getVertices());

    bool anyChanged = false;
    for (irr::u32 i = 0; i < vertCount; ++i) {
        float influence = windInfluence_[i];

        if (influence < 0.001f) {
            // No wind influence - keep at base position
            if (verts[i].Pos != basePositions_[i]) {
                verts[i].Pos = basePositions_[i];
                anyChanged = true;
            }
            continue;
        }

        // Calculate wind displacement for this vertex
        irr::core::vector3df displacement = wind.getDisplacement(
            basePositions_[i], influence, config.windStrength);

        irr::core::vector3df newPos = basePositions_[i] + displacement;
        if (verts[i].Pos != newPos) {
            verts[i].Pos = newPos;
            anyChanged = true;
        }
    }

    // Mark buffer as changed if any vertices were updated
    if (anyChanged) {
        buffer_->setDirty(irr::scene::EBT_VERTEX);
    }
}

void DetailChunk::applyWindAndDisturbance(const WindController& wind,
                                           const FoliageDisturbanceManager& disturbance,
                                           const ZoneDetailConfig& config) {
    if (!buffer_ || basePositions_.empty() || windInfluence_.empty()) {
        return;
    }

    irr::u32 vertCount = buffer_->getVertexCount();
    if (vertCount == 0 || vertCount > basePositions_.size()) {
        return;
    }

    // Get direct access to vertex buffer
    irr::video::S3DVertex* verts = static_cast<irr::video::S3DVertex*>(buffer_->getVertices());

    bool anyChanged = false;
    const auto& disturbConfig = disturbance.getConfig();

    for (irr::u32 i = 0; i < vertCount; ++i) {
        float influence = windInfluence_[i];

        if (influence < 0.001f) {
            // No wind/disturbance influence - keep at base position
            if (verts[i].Pos != basePositions_[i]) {
                verts[i].Pos = basePositions_[i];
                anyChanged = true;
            }
            continue;
        }

        // Calculate wind displacement
        irr::core::vector3df windDisp(0, 0, 0);
        if (config.windStrength > 0.001f) {
            windDisp = wind.getDisplacement(basePositions_[i], influence, config.windStrength);
        }

        // Calculate disturbance displacement
        irr::core::vector3df disturbDisp(0, 0, 0);
        if (disturbConfig.enabled) {
            disturbDisp = disturbance.getDisplacement(basePositions_[i], influence);
        }

        // Combine displacements - disturbance adds to wind
        // When disturbance is strong, it dominates; when weak, wind shows through
        irr::core::vector3df totalDisp = windDisp + disturbDisp;

        irr::core::vector3df newPos = basePositions_[i] + totalDisp;
        if (verts[i].Pos != newPos) {
            verts[i].Pos = newPos;
            anyChanged = true;
        }
    }

    // Mark buffer as changed if any vertices were updated
    if (anyChanged) {
        buffer_->setDirty(irr::scene::EBT_VERTEX);
    }
}

void DetailChunk::attach() {
    if (attached_ || !smgr_ || !mesh_) return;

    sceneNode_ = smgr_->addMeshSceneNode(mesh_);
    if (sceneNode_) {
        sceneNode_->setPosition(irr::core::vector3df(0, 0, 0));
        sceneNode_->setMaterialFlag(irr::video::EMF_LIGHTING, true);
        sceneNode_->setMaterialFlag(irr::video::EMF_FOG_ENABLE, true);
        sceneNode_->setMaterialFlag(irr::video::EMF_BACK_FACE_CULLING, false);

        // Apply atlas texture to scene node material if we have one
        if (atlasTexture_) {
            sceneNode_->setMaterialType(irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL);
            sceneNode_->setMaterialTexture(0, atlasTexture_);
        }

        attached_ = true;
    }
}

void DetailChunk::detach() {
    if (!attached_ || !sceneNode_) return;

    sceneNode_->remove();
    sceneNode_ = nullptr;
    attached_ = false;
}

} // namespace Detail
} // namespace Graphics
} // namespace EQT
