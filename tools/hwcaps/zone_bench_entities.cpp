// zone_bench_entities.cpp — entity PLACEMENT and the non-LOD entity render paths
//
// T387: extracted verbatim from tools/hwcaps/zone_bench_loader.cpp, which was
// 3,280 lines (the 1,500 ceiling M00 enforces tree-wide, and which
// check_file_sizes.sh did not yet scan under tools/ -- board B167). Function
// bodies are unchanged; only the include set and the file boundary are new.
// The public declarations for everything here stay in zone_bench_loader.h,
// which remains this module's single interface header.

#include "zone_bench_loader.h"
#include "zone_bench_draw_util.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <cstdlib>
#include <mutex>
#include <set>
#include <unordered_map>

#include "common/logging.h"

#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/zone_shader.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/frustum_culler.h"
#include "client/graphics/portal_system.h"
#include "client/zone_bsp.h"

#include "client/graphics/software_occlusion_culler.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eqe1_loader.h"
#include "client/graphics/eqe1_texture_index.h"
#include "client/graphics/load_mem_audit.h"

// The direct-GL entity paths call GLES2 entry points. In the pre-split TU
// this include sat mid-file, INSIDE namespace ZoneBench (it still does, for
// zone_bench_lod.cpp); at file scope here it declares exactly the same
// extern "C" entry points.
#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#endif

using namespace EQT::Graphics;

namespace ZoneBench {

void placeEntitiesAlongPath(ZoneBenchData& data,
                             const std::vector<glm::vec3>& pathPositions) {
    if (data.entities.empty() || pathPositions.empty()) return;

    int n = static_cast<int>(data.entities.size());
    int pathLen = static_cast<int>(pathPositions.size());

    for (int i = 0; i < n; ++i) {
        // Distribute evenly along path
        int pathIdx = (i * pathLen / n) % pathLen;
        const auto& pos = pathPositions[pathIdx];

        // Offset slightly to the side so entities aren't on the camera path
        float offsetX = (i % 2 == 0) ? 5.0f : -5.0f;
        float offsetY = ((i / 2) % 2 == 0) ? 3.0f : -3.0f;

        data.entities[i].eqX = pos.x + offsetX;
        data.entities[i].eqY = pos.y + offsetY;
        data.entities[i].eqZ = pos.z - 6.0f;  // Remove eye height, place on ground

        // Offset upward so feet touch ground (not model center)
        float meshOffsetY = 0;
        if (data.entities[i].mesh) {
            auto bb = data.entities[i].mesh->getBoundingBox();
            // Irrlicht Y-up: MinEdge.Y is typically negative (below origin)
            meshOffsetY = -bb.MinEdge.Y;
        }

        // Irrlicht coords: EQ (x,y,z) -> Irrlicht (x,z,y)
        data.entities[i].position = irr::core::vector3df(
            data.entities[i].eqX,
            data.entities[i].eqZ + meshOffsetY,
            data.entities[i].eqY);

        // Position animated scene node to match (if animation is enabled)
        if (i < static_cast<int>(data.animatedNodes.size()) && data.animatedNodes[i]) {
            data.animatedNodes[i]->setPosition(data.entities[i].position);
            data.animatedNodes[i]->updateAbsolutePosition();
        }

        // Assign priority — simulate realistic mix
        // Entity 0: current target, 1-5: group, 6-10: raid, rest: default/idle
        if (i == 0) data.entities[i].priority = ZoneBenchData::EntityPriority::Target;
        else if (i <= 5) data.entities[i].priority = ZoneBenchData::EntityPriority::Group;
        else if (i <= 10) data.entities[i].priority = ZoneBenchData::EntityPriority::Raid;
        else if (i % 5 == 0) data.entities[i].priority = ZoneBenchData::EntityPriority::Idle;
        else data.entities[i].priority = ZoneBenchData::EntityPriority::Default;
    }

    printf("  Placed %d entities along camera path (1 target, 5 group, 5 raid, rest default/idle)\n", n);
    for (int i = 0; i < n; ++i) printf("    entity[%d] EQ pos: (%.1f, %.1f, %.1f)\n", i, data.entities[i].eqX, data.entities[i].eqY, data.entities[i].eqZ);
}

int renderBatchedEntities(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    bool frustumCull,
    int& outEntityPolys) {

    outEntityPolys = 0;
    float renderDistSq = renderDistance * renderDistance;

    // Collect visible entity mesh buffers grouped by material key
    struct BatchEntry {
        irr::scene::IMeshBuffer* buffer;
        irr::core::vector3df entityPos;
    };
    std::map<uint32_t, std::vector<BatchEntry>> materialBuckets;
    irr::video::SMaterial firstMaterial;  // Track first material per bucket
    std::map<uint32_t, irr::video::SMaterial> bucketMaterials;

    for (const auto& ent : data.entities) {
        if (!ent.mesh) continue;

        float dx = ent.eqX - cam.eqX;
        float dy = ent.eqY - cam.eqY;
        float dz = ent.eqZ - cam.eqZ;
        if (dx * dx + dy * dy + dz * dz > renderDistSq) continue;

        if (frustumCull && data.frustumCuller && data.frustumCuller->isEnabled()) {
            if (!data.frustumCuller->testSphere(ent.eqX, ent.eqY, ent.eqZ, 5.0f))
                continue;
        }

        for (irr::u32 i = 0; i < ent.mesh->getMeshBufferCount(); ++i) {
            irr::scene::IMeshBuffer* buf = ent.mesh->getMeshBuffer(i);
            if (!buf || buf->getVertexCount() == 0) continue;

            const irr::video::SMaterial& mat = buf->getMaterial();
            uint32_t texId = textureGroupId(mat);  // H02b: address-independent (see textureGroupId)
            uint32_t matKey = (static_cast<uint32_t>(mat.MaterialType) << 16) | texId;

            materialBuckets[matKey].push_back({buf, ent.position});
            if (bucketMaterials.find(matKey) == bucketMaterials.end())
                bucketMaterials[matKey] = mat;
        }
    }

    if (materialBuckets.empty()) return 0;

    // For each material bucket: merge vertices with baked transforms, single draw
    irr::core::matrix4 identityMat;
    data.driver->setTransform(irr::video::ETS_WORLD, identityMat);

    std::vector<irr::video::S3DVertex> mergedVerts;
    std::vector<irr::u16> mergedIndices;
    int drawCalls = 0;

    for (auto& [matKey, entries] : materialBuckets) {
        mergedVerts.clear();
        mergedIndices.clear();

        for (const auto& entry : entries) {
            irr::scene::IMeshBuffer* buf = entry.buffer;
            irr::u32 baseVertex = static_cast<irr::u32>(mergedVerts.size());

            // Check 16-bit index overflow
            if (baseVertex + buf->getVertexCount() > 65535) break;

            // Copy vertices with world transform baked in
            const irr::video::S3DVertex* srcVerts =
                static_cast<const irr::video::S3DVertex*>(buf->getVertices());
            for (irr::u32 v = 0; v < buf->getVertexCount(); ++v) {
                irr::video::S3DVertex vert = srcVerts[v];
                vert.Pos += entry.entityPos;
                mergedVerts.push_back(vert);
            }

            // Copy indices with base offset
            const irr::u16* srcIdx = buf->getIndices();
            for (irr::u32 idx = 0; idx < buf->getIndexCount(); ++idx) {
                mergedIndices.push_back(static_cast<irr::u16>(srcIdx[idx] + baseVertex));
            }
        }

        if (mergedIndices.empty()) continue;

        data.driver->setMaterial(bucketMaterials[matKey]);
        data.driver->drawVertexPrimitiveList(
            mergedVerts.data(), mergedVerts.size(),
            mergedIndices.data(), mergedIndices.size() / 3,
            irr::video::EVT_STANDARD,
            irr::scene::EPT_TRIANGLES,
            irr::video::EIT_16BIT);

        outEntityPolys += mergedIndices.size() / 3;
        drawCalls++;
    }

    return drawCalls;
}

int renderDirectGLEntities(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    bool frustumCull,
    int& outEntityPolys) {

    outEntityPolys = 0;
#ifndef EQT_HAS_GLES2
    // Direct GL mode only works with GLES2 driver
    (void)data; (void)cam; (void)renderDistance; (void)frustumCull;
    return 0;
#else
    float renderDistSq = renderDistance * renderDistance;

    // Get current shader program and uniform locations once
    GLint currentProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
    GLint uWVP = glGetUniformLocation(currentProgram, "mWorldViewProj");
    GLint uWorld = glGetUniformLocation(currentProgram, "mWorld");

    // Pre-compute view-projection matrix (Irrlicht stores these)
    irr::core::matrix4 viewProj = data.driver->getTransform(irr::video::ETS_PROJECTION);
    viewProj *= data.driver->getTransform(irr::video::ETS_VIEW);

    // Minimal state: enable depth test, disable blend, backface cull off
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    // Vertex attribute layout: S3DVertex = pos(12) + normal(12) + color(4) + uv(8) = 36
    const GLsizei stride = 36;

    // Save Irrlicht's GL state so we can restore it after direct GL calls
    GLint savedProgram = 0, savedTexture = 0, savedVBO = 0, savedEBO = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &savedProgram);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &savedTexture);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &savedVBO);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &savedEBO);

    GLuint lastTexture = 0;
    int drawCalls = 0;

    for (const auto& ent : data.entities) {
        if (!ent.mesh) continue;

        float dx = ent.eqX - cam.eqX;
        float dy = ent.eqY - cam.eqY;
        float dz = ent.eqZ - cam.eqZ;
        if (dx * dx + dy * dy + dz * dz > renderDistSq) continue;

        if (frustumCull && data.frustumCuller && data.frustumCuller->isEnabled()) {
            if (!data.frustumCuller->testSphere(ent.eqX, ent.eqY, ent.eqZ, 5.0f))
                continue;
        }

        // Compute WVP for this entity
        irr::core::matrix4 worldMat;
        worldMat.setTranslation(ent.position);
        irr::core::matrix4 wvp = viewProj;
        wvp *= worldMat;

        // Upload matrices once per entity
        if (uWVP >= 0) glUniformMatrix4fv(uWVP, 1, GL_FALSE, wvp.pointer());
        if (uWorld >= 0) glUniformMatrix4fv(uWorld, 1, GL_FALSE, worldMat.pointer());

        // Draw all mesh buffers with minimal state changes
        for (irr::u32 i = 0; i < ent.mesh->getMeshBufferCount(); ++i) {
            irr::scene::IMeshBuffer* buf = ent.mesh->getMeshBuffer(i);
            if (!buf || buf->getVertexCount() == 0) continue;

            // Texture bind — only if changed
            const irr::video::SMaterial& mat = buf->getMaterial();
            if (mat.getTexture(0)) {
                GLuint texId = static_cast<GLuint>(
                    mat.getTexture(0)->getDriverTextureHandle());
                if (texId != lastTexture) {
                    glBindTexture(GL_TEXTURE_2D, texId);
                    lastTexture = texId;
                }
            }

            // For HW-buffered meshes, bind VBO/EBO directly
            // We need access to the HW buffer map — use driver's handle
            GLuint vbo = static_cast<GLuint>(buf->getHardwareMappingHint_Vertex());
            GLuint ebo = static_cast<GLuint>(buf->getHardwareMappingHint_Index());

            // Fallback: use client-side arrays if no HW buffer
            // (entities typically use client-side arrays)
            const void* vertexData = buf->getVertices();
            const void* indexData = buf->getIndices();

            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, vertexData);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                                  static_cast<const char*>(vertexData) + 12);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                                  static_cast<const char*>(vertexData) + 24);
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride,
                                  static_cast<const char*>(vertexData) + 28);
            glEnableVertexAttribArray(3);

            glDrawElements(GL_TRIANGLES, buf->getIndexCount(),
                           GL_UNSIGNED_SHORT, indexData);

            outEntityPolys += buf->getIndexCount() / 3;
            drawCalls++;
        }
    }

    // Restore Irrlicht's GL state so it doesn't crash on next frame
    glUseProgram(savedProgram);
    glBindTexture(GL_TEXTURE_2D, savedTexture);
    glBindBuffer(GL_ARRAY_BUFFER, savedVBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, savedEBO);

    return drawCalls;
#endif
}

int renderDirectGLCompositeEntities(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    bool frustumCull,
    int& outEntityPolys) {

    outEntityPolys = 0;
#ifndef EQT_HAS_GLES2
    (void)data; (void)cam; (void)renderDistance; (void)frustumCull;
    return 0;
#else
    float renderDistSq = renderDistance * renderDistance;

    // Save Irrlicht state
    GLint savedProgram = 0, savedTexture = 0, savedVBO = 0, savedEBO = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &savedProgram);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &savedTexture);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &savedVBO);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &savedEBO);

    // Get shader uniforms
    GLint currentProgram = savedProgram;
    GLint uWVP = glGetUniformLocation(currentProgram, "mWorldViewProj");
    GLint uWorld = glGetUniformLocation(currentProgram, "mWorld");

    // Pre-compute VP matrix
    irr::core::matrix4 viewProj = data.driver->getTransform(irr::video::ETS_PROJECTION);
    viewProj *= data.driver->getTransform(irr::video::ETS_VIEW);

    // Set GL state once for all entities
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    const GLsizei stride = 36;
    GLuint lastTexture = 0;
    int drawCalls = 0;

    // Reusable merge buffers
    std::vector<irr::video::S3DVertex> mergedVerts;
    std::vector<irr::u16> mergedIndices;

    for (const auto& ent : data.entities) {
        if (!ent.mesh) continue;

        float dx = ent.eqX - cam.eqX;
        float dy = ent.eqY - cam.eqY;
        float dz = ent.eqZ - cam.eqZ;
        if (dx * dx + dy * dy + dz * dz > renderDistSq) continue;

        if (frustumCull && data.frustumCuller && data.frustumCuller->isEnabled()) {
            if (!data.frustumCuller->testSphere(ent.eqX, ent.eqY, ent.eqZ, 5.0f))
                continue;
        }

        // Merge all body parts into one buffer with baked position
        mergedVerts.clear();
        mergedIndices.clear();
        GLuint texId = 0;

        for (irr::u32 i = 0; i < ent.mesh->getMeshBufferCount(); ++i) {
            irr::scene::IMeshBuffer* buf = ent.mesh->getMeshBuffer(i);
            if (!buf || buf->getVertexCount() == 0) continue;

            if (texId == 0 && buf->getMaterial().getTexture(0)) {
                texId = static_cast<GLuint>(
                    buf->getMaterial().getTexture(0)->getDriverTextureHandle());
            }

            irr::u32 baseVertex = static_cast<irr::u32>(mergedVerts.size());
            if (baseVertex + buf->getVertexCount() > 65535) break;

            const irr::video::S3DVertex* srcVerts =
                static_cast<const irr::video::S3DVertex*>(buf->getVertices());
            for (irr::u32 v = 0; v < buf->getVertexCount(); ++v) {
                irr::video::S3DVertex vert = srcVerts[v];
                vert.Pos += ent.position;
                mergedVerts.push_back(vert);
            }

            const irr::u16* srcIdx = buf->getIndices();
            for (irr::u32 idx = 0; idx < buf->getIndexCount(); ++idx)
                mergedIndices.push_back(static_cast<irr::u16>(srcIdx[idx] + baseVertex));
        }

        if (mergedIndices.empty()) continue;

        // Upload identity world (positions baked into vertices)
        irr::core::matrix4 identityMat;
        irr::core::matrix4 wvp = viewProj;
        if (uWVP >= 0) glUniformMatrix4fv(uWVP, 1, GL_FALSE, wvp.pointer());
        if (uWorld >= 0) glUniformMatrix4fv(uWorld, 1, GL_FALSE, identityMat.pointer());

        // Bind texture only if changed
        if (texId != lastTexture) {
            glBindTexture(GL_TEXTURE_2D, texId);
            lastTexture = texId;
        }

        // Set vertex attribs and draw
        const void* vdata = mergedVerts.data();
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, vdata);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                              static_cast<const char*>(vdata) + 12);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                              static_cast<const char*>(vdata) + 24);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride,
                              static_cast<const char*>(vdata) + 28);
        glEnableVertexAttribArray(3);

        glDrawElements(GL_TRIANGLES, mergedIndices.size(),
                       GL_UNSIGNED_SHORT, mergedIndices.data());

        outEntityPolys += mergedIndices.size() / 3;
        drawCalls++;
    }

    // Restore Irrlicht state
    glUseProgram(savedProgram);
    glBindTexture(GL_TEXTURE_2D, savedTexture);
    glBindBuffer(GL_ARRAY_BUFFER, savedVBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, savedEBO);

    return drawCalls;
#endif
}

int renderCompositeEntities(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    bool frustumCull,
    int& outEntityPolys) {

    outEntityPolys = 0;
    float renderDistSq = renderDistance * renderDistance;

    std::vector<irr::video::S3DVertex> mergedVerts;
    std::vector<irr::u16> mergedIndices;
    irr::core::matrix4 identityMat;
    int drawCalls = 0;

    for (const auto& ent : data.entities) {
        if (!ent.mesh) continue;

        float dx = ent.eqX - cam.eqX;
        float dy = ent.eqY - cam.eqY;
        float dz = ent.eqZ - cam.eqZ;
        if (dx * dx + dy * dy + dz * dz > renderDistSq) continue;

        if (frustumCull && data.frustumCuller && data.frustumCuller->isEnabled()) {
            if (!data.frustumCuller->testSphere(ent.eqX, ent.eqY, ent.eqZ, 5.0f))
                continue;
        }

        // Merge ALL mesh buffers for this entity into one draw
        mergedVerts.clear();
        mergedIndices.clear();
        irr::video::SMaterial compositeMat;
        bool hasMaterial = false;

        for (irr::u32 i = 0; i < ent.mesh->getMeshBufferCount(); ++i) {
            irr::scene::IMeshBuffer* buf = ent.mesh->getMeshBuffer(i);
            if (!buf || buf->getVertexCount() == 0) continue;

            // Use first buffer's material as the composite material
            if (!hasMaterial) {
                compositeMat = buf->getMaterial();
                hasMaterial = true;
            }

            irr::u32 baseVertex = static_cast<irr::u32>(mergedVerts.size());
            if (baseVertex + buf->getVertexCount() > 65535) break;

            const irr::video::S3DVertex* srcVerts =
                static_cast<const irr::video::S3DVertex*>(buf->getVertices());
            for (irr::u32 v = 0; v < buf->getVertexCount(); ++v) {
                irr::video::S3DVertex vert = srcVerts[v];
                vert.Pos += ent.position;
                mergedVerts.push_back(vert);
            }

            const irr::u16* srcIdx = buf->getIndices();
            for (irr::u32 idx = 0; idx < buf->getIndexCount(); ++idx) {
                mergedIndices.push_back(static_cast<irr::u16>(srcIdx[idx] + baseVertex));
            }
        }

        if (mergedIndices.empty() || !hasMaterial) continue;

        data.driver->setTransform(irr::video::ETS_WORLD, identityMat);
        data.driver->setMaterial(compositeMat);
        data.driver->drawVertexPrimitiveList(
            mergedVerts.data(), mergedVerts.size(),
            mergedIndices.data(), mergedIndices.size() / 3,
            irr::video::EVT_STANDARD,
            irr::scene::EPT_TRIANGLES,
            irr::video::EIT_16BIT);

        outEntityPolys += mergedIndices.size() / 3;
        drawCalls++;
    }

    return drawCalls;
}

void placeEntitiesTrain(ZoneBenchData& data, const glm::vec3& centerPos) {
    if (data.entities.empty()) return;

    int n = static_cast<int>(data.entities.size());
    // Pack entities in a tight grid around the center
    // ~3 units apart (shoulder-to-shoulder mob pack)
    float spacing = 3.0f;
    int gridSize = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(n))));

    for (int i = 0; i < n; ++i) {
        int gx = i % gridSize;
        int gy = i / gridSize;
        float offsetX = (gx - gridSize / 2) * spacing;
        float offsetY = (gy - gridSize / 2) * spacing;

        data.entities[i].eqX = centerPos.x + offsetX;
        data.entities[i].eqY = centerPos.y + offsetY;
        data.entities[i].eqZ = centerPos.z - 6.0f;  // Remove eye height

        float meshOffsetY = 0;
        if (data.entities[i].mesh) {
            auto bb = data.entities[i].mesh->getBoundingBox();
            meshOffsetY = -bb.MinEdge.Y;
        }

        data.entities[i].position = irr::core::vector3df(
            data.entities[i].eqX,
            data.entities[i].eqZ + meshOffsetY,
            data.entities[i].eqY);

        // Position animated scene node to match (if animation is enabled)
        if (i < static_cast<int>(data.animatedNodes.size()) && data.animatedNodes[i]) {
            data.animatedNodes[i]->setPosition(data.entities[i].position);
            data.animatedNodes[i]->updateAbsolutePosition();
        }

        // Train entities are all hostile mobs — mostly default priority
        if (i == 0) data.entities[i].priority = ZoneBenchData::EntityPriority::Target;
        else data.entities[i].priority = ZoneBenchData::EntityPriority::Default;
    }

    float extent = gridSize * spacing;
    printf("  Train: %d entities in %.0fx%.0f grid at (%.0f, %.0f, %.0f)\n",
           n, extent, extent, centerPos.x, centerPos.y, centerPos.z);
}

} // namespace ZoneBench
