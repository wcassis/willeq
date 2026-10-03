// zone_bench_modes.cpp — gles2_zone_benchmark's four early-return diagnostic
// modes: --tree-test, --composite-entity-test, --pose-search and the H02b
// static --camera-pose capture the CI gate's golden frames come from.
//
// T387: bodies moved verbatim (dedented only) out of main(); see
// zone_bench_modes.h for the shape and why `data` is the unique_ptr.

#include "zone_bench_modes.h"

#include "zone_bench_frame.h"
#include "zone_bench_camera.h"
#include "zone_bench_histogram.h"
#include "zone_bench_merge.h"
#include "zone_bench_memory.h"
#include "tool_race_db.h"
#include "common/logging.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_set>
#include <utility>
#include <vector>

#include <irrlicht.h>

#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/zone_shader.h"
#include "client/graphics/frustum_culler.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/portal_system.h"
#include "client/graphics/software_occlusion_culler.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/zone_bsp.h"

#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include "gl_call_counters.h"
#include "gpu_sync_probe.h"
#include "gpu_sync_bench_hooks.h"
#endif

// ── Tree test: render one tree, screenshot, exit ──
int runTreeTest(std::unique_ptr<ZoneBench::ZoneBenchData>& data, const Options& opts) {
    printf("=== Tree Test ===\n");

    // Find TREE6 or TREE1 by name
    ZoneBench::ZoneBenchData::PlacedObject* treeObj = nullptr;
    for (auto& obj : data->objects) {
        if (!obj.mesh) continue;
        if (obj.name == "TREE6" || obj.name == "TREE1") {
            treeObj = &obj;
            break;
        }
    }

    if (!treeObj) {
        printf("No tree object found with alpha material\n");
        return 1;
    }

    printf("Tree '%s' found: %d buffers, pos=(%.1f, %.1f, %.1f)\n",
           treeObj->name.c_str(), treeObj->mesh->getMeshBufferCount(),
           treeObj->eqX, treeObj->eqY, treeObj->eqZ);
    for (irr::u32 i = 0; i < treeObj->mesh->getMeshBufferCount(); i++) {
        auto* buf = treeObj->mesh->getMeshBuffer(i);
        const auto& mat = buf->getMaterial();
        printf("  buf[%u]: verts=%u idx=%u matType=%d tex0=%p tex1=%p\n",
               i, buf->getVertexCount(), buf->getIndexCount(),
               (int)mat.MaterialType, (void*)mat.getTexture(0), (void*)mat.getTexture(1));
    }

    // Compute bounding box from ALL mesh buffers to frame the whole tree
    irr::core::vector3df bbMin(1e9f, 1e9f, 1e9f), bbMax(-1e9f, -1e9f, -1e9f);
    for (irr::u32 bi = 0; bi < treeObj->mesh->getMeshBufferCount(); bi++) {
        auto* buf = treeObj->mesh->getMeshBuffer(bi);
        auto* verts = (irr::video::S3DVertex*)buf->getVertices();
        for (irr::u32 vi = 0; vi < buf->getVertexCount(); vi++) {
            bbMin.X = std::min(bbMin.X, verts[vi].Pos.X);
            bbMin.Y = std::min(bbMin.Y, verts[vi].Pos.Y);
            bbMin.Z = std::min(bbMin.Z, verts[vi].Pos.Z);
            bbMax.X = std::max(bbMax.X, verts[vi].Pos.X);
            bbMax.Y = std::max(bbMax.Y, verts[vi].Pos.Y);
            bbMax.Z = std::max(bbMax.Z, verts[vi].Pos.Z);
        }
    }
    irr::core::vector3df treeCenter = (bbMin + bbMax) * 0.5f;
    float treeHeight = bbMax.Y - bbMin.Y;
    float treeWidth = std::max(bbMax.X - bbMin.X, bbMax.Z - bbMin.Z);
    float treeDiag = sqrtf(treeHeight * treeHeight + treeWidth * treeWidth);

    printf("[TREE-TEST] Bounding box: (%.1f,%.1f,%.1f) to (%.1f,%.1f,%.1f)\n",
           bbMin.X, bbMin.Y, bbMin.Z, bbMax.X, bbMax.Y, bbMax.Z);
    printf("[TREE-TEST] Center: (%.1f,%.1f,%.1f), height=%.1f, width=%.1f\n",
           treeCenter.X, treeCenter.Y, treeCenter.Z, treeHeight, treeWidth);
    fflush(stdout);

    // Camera: back up enough to see the whole tree, look at center
    float camDist = treeDiag * 1.5f;
    irr::core::vector3df camPos = treeCenter + irr::core::vector3df(camDist, 0.0f, 0.0f);
    irr::core::vector3df lookAt = treeCenter;

    printf("[TREE-TEST] Camera at (%.1f,%.1f,%.1f) looking at (%.1f,%.1f,%.1f), dist=%.1f\n",
           camPos.X, camPos.Y, camPos.Z, lookAt.X, lookAt.Y, lookAt.Z, camDist);
    fflush(stdout);

    // Build view and projection matrices manually (don't rely on scene manager)
    irr::core::matrix4 viewMat;
    viewMat.buildCameraLookAtMatrixLH(camPos, lookAt, irr::core::vector3df(0, 1, 0));
    data->driver->setTransform(irr::video::ETS_VIEW, viewMat);

    float aspect = (float)opts.width / (float)opts.height;
    irr::core::matrix4 projMat;
    projMat.buildProjectionMatrixPerspectiveFovLH(0.8f, aspect, 0.1f, 1000.0f);
    data->driver->setTransform(irr::video::ETS_PROJECTION, projMat);

    // Set up lighting — bright, no fog, so tree is clearly visible
    if (data->zoneShader) {
        data->zoneShader->beginFrame();
        data->zoneShader->setAmbientColor(0.8f, 0.8f, 0.8f);
        data->zoneShader->setSunDirection(0.3f, -0.8f, 0.2f);
        data->zoneShader->setSunColor(0.5f, 0.5f, 0.4f);
        data->zoneShader->setTintColor(1.0f, 1.0f, 1.0f);
        data->zoneShader->setFog(9000.0f, 9999.0f, 0.5f, 0.5f, 0.5f, 1.0f);  // fog far away
        data->zoneShader->setNumPointLights(0);
        data->zoneShader->setPlayerLightPos(camPos.X, camPos.Y, camPos.Z);
        data->zoneShader->setPlayerLightColor(1.0f, 0.95f, 0.8f);
        data->zoneShader->setPlayerLightAtten(1.0f, 0.0f, 0.00001f);  // wide radius
    }

    irr::core::matrix4 identity;

    // Helper: save screenshot to BMP
    auto saveScreenshot = [&](const char* filename) {
#ifdef EQT_HAS_GLES2
        glFinish();
        GLint vp[4];
        glGetIntegerv(GL_VIEWPORT, vp);
        int scrW = vp[2], scrH = vp[3];
        std::vector<uint8_t> pixels(scrW * scrH * 4);
        glReadPixels(0, 0, scrW, scrH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

        // Count non-clear pixels
        int nonClear = 0;
        for (int p = 0; p < scrW*scrH; p++) {
            uint8_t r = pixels[p*4], g = pixels[p*4+1], b = pixels[p*4+2];
            if (!(r == 100 && g == 150 && b == 255)) nonClear++;
        }
        printf("[TREE-TEST] %s: %d/%d non-clear pixels (%.1f%%)\n",
               filename, nonClear, scrW*scrH, 100.0*nonClear/(scrW*scrH));

        FILE* f = fopen(filename, "wb");
        if (f) {
            uint32_t rowBytes = scrW * 3;
            uint32_t rowPad = (4 - (rowBytes % 4)) % 4;
            uint32_t dataSize = (rowBytes + rowPad) * scrH;
            uint32_t fileSize = 54 + dataSize;
            uint8_t hdr[54] = {};
            hdr[0] = 'B'; hdr[1] = 'M';
            memcpy(hdr + 2, &fileSize, 4);
            uint32_t off = 54; memcpy(hdr + 10, &off, 4);
            uint32_t dibSize = 40; memcpy(hdr + 14, &dibSize, 4);
            int32_t sw = scrW, sh = scrH;
            memcpy(hdr + 18, &sw, 4); memcpy(hdr + 22, &sh, 4);
            uint16_t planes = 1; memcpy(hdr + 26, &planes, 2);
            uint16_t bpp = 24; memcpy(hdr + 28, &bpp, 2);
            fwrite(hdr, 54, 1, f);
            uint8_t pad[3] = {};
            for (int y = 0; y < scrH; y++) {
                for (int x = 0; x < scrW; x++) {
                    int idx = (y * scrW + x) * 4;
                    uint8_t bgr[3] = { pixels[idx+2], pixels[idx+1], pixels[idx+0] };
                    fwrite(bgr, 3, 1, f);
                }
                if (rowPad) fwrite(pad, rowPad, 1, f);
            }
            fclose(f);
            printf("[TREE-TEST] Saved %s (%dx%d)\n", filename, scrW, scrH);
        }
        fflush(stdout);
#endif
    };

    // === Screenshot 1: SOLID shader (no alpha discard) ===
    data->driver->beginScene(true, true, irr::video::SColor(255, 100, 150, 255));
    if (data->zoneShader) data->zoneShader->beginFrame();

    data->driver->setTransform(irr::video::ETS_WORLD, identity);
    for (irr::u32 i = 0; i < treeObj->mesh->getMeshBufferCount(); i++) {
        irr::video::SMaterial mat = treeObj->mesh->getMeshBuffer(i)->getMaterial();
        mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(
            data->zoneShader->getActiveSolid());
        data->driver->setMaterial(mat);
        data->driver->drawMeshBuffer(treeObj->mesh->getMeshBuffer(i));
    }
    saveScreenshot("tree_test_solid.bmp");
    data->driver->endScene();

    // === Screenshot 2: FULLY RAW GL alpha test (bypass Irrlicht completely) ===
#ifdef EQT_HAS_GLES2
    {
        auto compile = [](GLenum type, const char* src) -> GLuint {
            GLuint s = glCreateShader(type);
            glShaderSource(s, 1, &src, nullptr);
            glCompileShader(s);
            GLint ok = 0;
            glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char log[512]; glGetShaderInfoLog(s, 512, nullptr, log);
                printf("[TREE-TEST] Shader error: %s\n", log);
            }
            return s;
        };

        // Attribute locations matching Irrlicht's S3DVertex layout
        // Position=0, Normal=1, Color=2, TexCoord0=3
        const char* rawVS = R"(
            attribute vec3 aPos;
            attribute vec3 aNormal;
            attribute vec4 aColor;
            attribute vec2 aTexCoord;
            uniform mat4 uWVP;
            varying vec2 vTexCoord;
            void main() {
                gl_Position = uWVP * vec4(aPos, 1.0);
                vTexCoord = aTexCoord;
            }
        )";
        const char* rawFS = R"(
            precision mediump float;
            uniform sampler2D uTexture;
            uniform sampler2D uAlphaTexture;
            varying vec2 vTexCoord;
            void main() {
                float alpha = texture2D(uAlphaTexture, vTexCoord).r;
                if (alpha < 0.5) discard;
                gl_FragColor = texture2D(uTexture, vTexCoord);
            }
        )";

        GLuint vs = compile(GL_VERTEX_SHADER, rawVS);
        GLuint fs = compile(GL_FRAGMENT_SHADER, rawFS);
        GLuint rawProg = glCreateProgram();
        glAttachShader(rawProg, vs);
        glAttachShader(rawProg, fs);
        glBindAttribLocation(rawProg, 0, "aPos");
        glBindAttribLocation(rawProg, 1, "aNormal");
        glBindAttribLocation(rawProg, 2, "aColor");
        glBindAttribLocation(rawProg, 3, "aTexCoord");
        glLinkProgram(rawProg);
        glDeleteShader(vs);
        glDeleteShader(fs);

        GLint locWVP = glGetUniformLocation(rawProg, "uWVP");
        GLint locTex = glGetUniformLocation(rawProg, "uTexture");
        GLint locAlpha = glGetUniformLocation(rawProg, "uAlphaTexture");
        printf("[TREE-TEST] Raw program: id=%u, uWVP=%d, uTex=%d, uAlpha=%d\n",
               rawProg, locWVP, locTex, locAlpha);
        fflush(stdout);

        // Clear
        glClearColor(100/255.0f, 150/255.0f, 255/255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);

        glUseProgram(rawProg);
        irr::core::matrix4 wvp = projMat * viewMat;
        glUniformMatrix4fv(locWVP, 1, GL_FALSE, wvp.pointer());
        glUniform1i(locTex, 0);
        glUniform1i(locAlpha, 1);

        // Draw each mesh buffer using raw GL
        for (irr::u32 bi = 0; bi < treeObj->mesh->getMeshBufferCount(); bi++) {
            auto* buf = treeObj->mesh->getMeshBuffer(bi);
            const auto& mat = buf->getMaterial();

            // Use Irrlicht to bind textures, then query GL for the bound names
            data->driver->setMaterial(mat);
            GLint glTex0 = 0, glTex1 = 0;
            glActiveTexture(GL_TEXTURE0);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &glTex0);
            glActiveTexture(GL_TEXTURE1);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &glTex1);
            glActiveTexture(GL_TEXTURE0);

            // Also check what's ACTUALLY bound after switching program
            glUseProgram(rawProg);
            GLint finalTex0 = 0, finalTex1 = 0;
            glActiveTexture(GL_TEXTURE0);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &finalTex0);
            glActiveTexture(GL_TEXTURE1);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &finalTex1);
            glActiveTexture(GL_TEXTURE0);

            printf("[TREE-TEST] Raw draw buf[%u]: verts=%u, idx=%u, afterSetMat tex0=%d tex1=%d, afterProgSwitch tex0=%d tex1=%d\n",
                   bi, buf->getVertexCount(), buf->getIndexCount(), glTex0, glTex1, finalTex0, finalTex1);
            // Log UV coordinates for foliage buffer
            if (buf->getVertexCount() <= 32) {
                const irr::video::S3DVertex* v = (const irr::video::S3DVertex*)buf->getVertices();
                for (irr::u32 vi = 0; vi < buf->getVertexCount(); vi++) {
                    printf("[TREE-TEST]   v[%u] pos=(%.2f,%.2f,%.2f) uv=(%.4f,%.4f)\n",
                           vi, v[vi].Pos.X, v[vi].Pos.Y, v[vi].Pos.Z,
                           v[vi].TCoords.X, v[vi].TCoords.Y);
                }
            }
            fflush(stdout);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, glTex0);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, glTex1);
            glActiveTexture(GL_TEXTURE0);

            // Bind VBO/EBO from Irrlicht's HW buffer
            // Use client-side arrays from the mesh buffer
            const irr::video::S3DVertex* verts = (const irr::video::S3DVertex*)buf->getVertices();
            const irr::u16* indices = buf->getIndices();
            GLsizei stride = sizeof(irr::video::S3DVertex);

            // Unbind VBO to use client pointers
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, &verts[0].Pos);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, &verts[0].Normal);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, &verts[0].Color);
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, &verts[0].TCoords);
            glEnableVertexAttribArray(3);

            glDrawElements(GL_TRIANGLES, buf->getIndexCount(), GL_UNSIGNED_SHORT, indices);
        }

        glFinish();
        saveScreenshot("tree_test_alpha.bmp");

        // === Screenshot 3: Raw GL, NO discard (confirm geometry renders) ===
        const char* noDiscardFS = R"(
            precision mediump float;
            uniform sampler2D uTexture;
            varying vec2 vTexCoord;
            void main() {
                gl_FragColor = texture2D(uTexture, vTexCoord);
            }
        )";
        GLuint ndFS = compile(GL_FRAGMENT_SHADER, noDiscardFS);
        GLuint ndVS = compile(GL_VERTEX_SHADER, rawVS);
        GLuint ndProg = glCreateProgram();
        glAttachShader(ndProg, ndVS);
        glAttachShader(ndProg, ndFS);
        glBindAttribLocation(ndProg, 0, "aPos");
        glBindAttribLocation(ndProg, 1, "aNormal");
        glBindAttribLocation(ndProg, 2, "aColor");
        glBindAttribLocation(ndProg, 3, "aTexCoord");
        glLinkProgram(ndProg);
        glDeleteShader(ndVS);
        glDeleteShader(ndFS);

        glClearColor(100/255.0f, 150/255.0f, 255/255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(ndProg);
        GLint ndWVP = glGetUniformLocation(ndProg, "uWVP");
        GLint ndTex = glGetUniformLocation(ndProg, "uTexture");
        glUniformMatrix4fv(ndWVP, 1, GL_FALSE, wvp.pointer());
        glUniform1i(ndTex, 0);

        // Draw only foliage buffer (buf[0]) with tex0 only
        {
            auto* buf = treeObj->mesh->getMeshBuffer(0);
            const auto& mat = buf->getMaterial();
            data->driver->setMaterial(mat);  // binds textures
            glUseProgram(ndProg);  // switch back to our program
            glUniformMatrix4fv(ndWVP, 1, GL_FALSE, wvp.pointer());
            glUniform1i(ndTex, 0);

            const irr::video::S3DVertex* verts = (const irr::video::S3DVertex*)buf->getVertices();
            const irr::u16* indices = buf->getIndices();
            GLsizei stride = sizeof(irr::video::S3DVertex);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, &verts[0].Pos);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, &verts[0].TCoords);
            glEnableVertexAttribArray(3);
            glDrawElements(GL_TRIANGLES, buf->getIndexCount(), GL_UNSIGNED_SHORT, indices);
        }
        glFinish();
        saveScreenshot("tree_test_rawsolid.bmp");
        glDeleteProgram(ndProg);

        // Swap to show on screen too
        eglSwapBuffers(eglGetCurrentDisplay(), eglGetCurrentSurface(EGL_DRAW));

        glDeleteProgram(rawProg);
    }
#endif

    printf("Tree test complete.\n");
    return 0;
}

// ── Composite entity test mode ──
int runCompositeEntityTest(std::unique_ptr<ZoneBench::ZoneBenchData>& data, const Options& opts) {
    const char* variantNames[] = {"naked", "cloth", "chain", "plate", "leather"};
    const char* variantName = (opts.testVariant >= 0 && opts.testVariant <= 4)
        ? variantNames[opts.testVariant] : "unknown";
    printf("=== Composite Entity Test (variant %d: %s) ===\n",
           opts.testVariant, variantName);

    // Build entity list: either one specific race (male+female) or all 12
    struct RaceGender { uint16_t raceId; uint8_t gender; const char* label; };
    std::vector<RaceGender> testEntities;

    RaceGender allRaces[] = {
        {1,0,"HUM"},{1,1,"HUF"},{2,0,"BAM"},{2,1,"BAF"},
        {3,0,"ERM"},{3,1,"ERF"},{4,0,"ELM"},{4,1,"ELF"},
        {5,0,"HIM"},{5,1,"HIF"},{6,0,"DAM"},{6,1,"DAF"},
        {7,0,"HAM"},{7,1,"HAF"},{8,0,"DWM"},{8,1,"DWF"},
        {9,0,"TRM"},{9,1,"TRF"},{10,0,"OGM"},{10,1,"OGF"},
        {11,0,"HOM"},{11,1,"HOF"},{12,0,"GNM"},{12,1,"GNF"},
    };

    if (opts.testRace > 0 && opts.testRace <= 12) {
        // Just male + female of the specified race
        testEntities.push_back(allRaces[(opts.testRace - 1) * 2]);
        testEntities.push_back(allRaces[(opts.testRace - 1) * 2 + 1]);
        printf("  Testing race %d: %s / %s\n", opts.testRace,
               testEntities[0].label, testEntities[1].label);
    } else {
        for (const auto& r : allRaces) testEntities.push_back(r);
        printf("  Testing all %zu race/gender combos\n", testEntities.size());
    }

    int numEntities = static_cast<int>(testEntities.size());

    // Clear any model data from the generic preload (which used variant 0)
    // so we can reload with the requested variant.
    data->raceModelLoader->clearCache();

    // Preload composite data for all genders being tested
    printf("  Preloading composite data (variant %d)...\n", opts.testVariant);
    for (int i = 0; i < numEntities; ++i) {
        data->raceModelLoader->preloadModelData(
            testEntities[i].raceId, testEntities[i].gender, opts.testVariant);
    }
    data->raceModelLoader->promotePreparedModels();
    data->textureCache->uploadAllPending();

    data->entities.resize(numEntities);
    data->animatedNodes.resize(numEntities, nullptr);

    int loadedCount = 0;
    for (int i = 0; i < numEntities; ++i) {
        auto* mesh = data->raceModelLoader->getMeshForRace(
            testEntities[i].raceId, testEntities[i].gender);
        data->textureCache->uploadAllPending();

        if (!mesh) {
            printf("    %s: NO MESH\n", testEntities[i].label);
            continue;
        }
        data->entities[i].mesh = mesh;

        // Create animated node if requested
        if (opts.animate) {
            auto* node = data->raceModelLoader->createAnimatedNode(
                testEntities[i].raceId, testEntities[i].gender,
                data->smgr->getRootSceneNode());
            if (node) {
                data->animatedNodes[i] = node;
                node->playAnimation("o01", true);
                data->textureCache->uploadAllPending();
                if (node->getInstanceMesh())
                    data->entities[i].mesh = node->getInstanceMesh();
            }
        }

        int bufCount = data->entities[i].mesh->getMeshBufferCount();
        const char* texName = "(none)";
        if (bufCount > 0) {
            auto* tex = data->entities[i].mesh->getMeshBuffer(0)->getMaterial().getTexture(0);
            if (tex) texName = tex->getName().getPath().c_str();
        }
        printf("    %s: %d buffers, tex0='%s'\n", testEntities[i].label, bufCount, texName);
        loadedCount++;
    }
    printf("  Loaded %d/%d entities\n", loadedCount, numEntities);

    // Place entities side by side, spaced apart
    float surfX, surfY, surfZ;
    ZoneBench::findSurfacePosition(*data, surfX, surfY, surfZ);

    float spacing = 20.0f;
    float totalWidth = (numEntities - 1) * spacing;
    float startX = surfX - totalWidth * 0.5f;
    float entityY = surfY + 40.0f;  // 40 units in front of camera

    for (int i = 0; i < numEntities; ++i) {
        if (!data->entities[i].mesh) continue;

        float eqX = startX + i * spacing;
        data->entities[i].eqX = eqX;
        data->entities[i].eqY = entityY;
        data->entities[i].eqZ = surfZ - 6.0f;

        float meshOffsetY = -data->entities[i].mesh->getBoundingBox().MinEdge.Y;
        data->entities[i].position = irr::core::vector3df(
            eqX, data->entities[i].eqZ + meshOffsetY, entityY);

        if (data->animatedNodes[i]) {
            data->animatedNodes[i]->setPosition(data->entities[i].position);
            data->animatedNodes[i]->updateAbsolutePosition();
        }
        data->entities[i].priority = ZoneBench::ZoneBenchData::EntityPriority::Target;

        printf("    %s placed at EQ (%.1f, %.1f, %.1f)\n",
               testEntities[i].label, eqX, entityY, data->entities[i].eqZ);
    }

    // Camera: behind and looking at entities
    float camEqX = surfX;
    float camEqY = surfY;
    float camEqZ = surfZ;
    float camDirX = 0, camDirY = 1, camDirZ = 0;

    printf("  Camera at EQ (%.1f, %.1f, %.1f) looking +Y toward entities at Y=%.1f\n",
           camEqX, camEqY, camEqZ, entityY);

    // Bright daytime lighting
    if (data->zoneShader) {
        data->zoneShader->setAmbientColor(0.6f, 0.6f, 0.6f);
        data->zoneShader->setSunDirection(0.3f, -0.8f, 0.2f);
        data->zoneShader->setSunColor(0.5f, 0.5f, 0.4f);
        data->zoneShader->setTintColor(1.0f, 1.0f, 1.0f);
        data->zoneShader->setFog(9000.0f, 9999.0f, 0.5f, 0.5f, 0.5f, 1.0f);
        data->zoneShader->setNumPointLights(0);
    }

    // Render loop — runs until --frames count
    int testFrames = opts.frames;
    printf("  Rendering %d frames (%.1f sec at 30fps)...\n",
           testFrames, testFrames / 30.0f);

    irr::core::matrix4 identityMat;
    for (int frame = 0; frame < testFrames; ++frame) {
        setupCamera(*data, camEqX, camEqY, camEqZ, camDirX, camDirY, camDirZ);
        data->driver->beginScene(true, true, irr::video::SColor(255, 80, 80, 120));
        if (data->zoneShader) data->zoneShader->beginFrame();

        // Animate
        if (opts.animate) {
            irr::u32 simTimeMs = static_cast<irr::u32>(frame * 33.3f);
            for (int i = 0; i < numEntities; ++i) {
                if (!data->animatedNodes[i]) continue;
                data->animatedNodes[i]->OnAnimate(simTimeMs);
                auto* instMesh = data->animatedNodes[i]->getInstanceMesh();
                if (instMesh) data->entities[i].mesh = instMesh;
            }
        }

        // Draw each entity (composite = 1 buffer, multi-buffer = merge with first mat)
        for (int i = 0; i < numEntities; ++i) {
            auto* mesh = data->entities[i].mesh;
            if (!mesh) continue;

            std::vector<irr::video::S3DVertex> verts;
            std::vector<irr::u16> indices;
            irr::video::SMaterial mat;
            bool hasMat = false;

            for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
                auto* buf = mesh->getMeshBuffer(b);
                if (!buf || buf->getVertexCount() == 0) continue;
                if (!hasMat) { mat = buf->getMaterial(); hasMat = true; }

                irr::u32 base = static_cast<irr::u32>(verts.size());
                if (base + buf->getVertexCount() > 65535) break;

                auto* sv = static_cast<const irr::video::S3DVertex*>(buf->getVertices());
                for (irr::u32 v = 0; v < buf->getVertexCount(); ++v) {
                    irr::video::S3DVertex vert = sv[v];
                    vert.Pos += data->entities[i].position;
                    verts.push_back(vert);
                }
                auto* si = buf->getIndices();
                for (irr::u32 idx = 0; idx < buf->getIndexCount(); ++idx)
                    indices.push_back(static_cast<irr::u16>(si[idx] + base));
            }

            if (!indices.empty() && hasMat) {
                data->driver->setTransform(irr::video::ETS_WORLD, identityMat);
                data->driver->setMaterial(mat);
                data->driver->drawVertexPrimitiveList(
                    verts.data(), verts.size(),
                    indices.data(), indices.size() / 3,
                    irr::video::EVT_STANDARD, irr::scene::EPT_TRIANGLES,
                    irr::video::EIT_16BIT);
            }
        }

        if (frame == testFrames - 1 && !opts.dumpPngPath.empty()) {
            irr::video::IImage* shot = data->driver->createScreenShot();
            if (shot) {
                data->driver->writeImageToFile(shot, opts.dumpPngPath.c_str());
                shot->drop();
            }
        }
        data->driver->endScene();
    }

    printf("Composite entity test complete (%d frames rendered).\n", testFrames);
    return 0;
}

// ── H02b: interesting-view search (opt-in, bless-time diagnostic only) ──
// Never used by the CI gate; run manually to find candidate poses, then
// hard-code the winners as --camera-pose flags in the manifest.
int runPoseSearch(std::unique_ptr<ZoneBench::ZoneBenchData>& data, const Options& opts) {
    printf("\n=== Pose search: %d anchors x %d headings ===\n",
           opts.poseSearch, opts.poseSearchHeadings);

    // Candidate anchor points: region bounding-box centers/floors, spread
    // evenly across the (already sorted-by-key) region map so candidates
    // are guaranteed to be near real loaded geometry, not empty space.
    std::vector<const ZoneBench::RegionData*> regionList;
    regionList.reserve(data->regions.size());
    for (auto& [idx, rd] : data->regions) regionList.push_back(&rd);

    int n = static_cast<int>(regionList.size());
    int take = std::min(opts.poseSearch, n);
    if (take <= 0) {
        fprintf(stderr, "ERROR: no regions loaded, cannot pose-search\n");
        return 1;
    }

    struct Candidate {
        float x, y, z, heading, pitch;
        int draws = 0;
        size_t distinctColors = 0;
        double skyFraction = 1.0;
        double score = 0.0;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(take * opts.poseSearchHeadings);

    std::vector<ZoneBench::DrawEntry> drawEntries;
    drawEntries.reserve(2048);
    float fov = 60.0f * 3.14159265f / 180.0f;
    float aspect = static_cast<float>(opts.width) / static_cast<float>(opts.height);

    FILE* csvOut = nullptr;
    if (!opts.poseSearchCsv.empty()) {
        csvOut = fopen(opts.poseSearchCsv.c_str(), "w");
        if (csvOut) fprintf(csvOut, "x,y,z,heading,pitch,draws,distinct_colors,sky_fraction,score\n");
    }

    for (int i = 0; i < take; ++i) {
        int ridx = i * n / take;
        const ZoneBench::RegionData* rd = regionList[ridx];
        float cx = (rd->eqMinX + rd->eqMaxX) * 0.5f;
        float cy = (rd->eqMinY + rd->eqMaxY) * 0.5f;
        // Anchor near the region's floor (bbox min Z) rather than its
        // center -- a good approximation of "standing inside this chunk
        // of geometry" for both open outdoor regions and enclosed rooms.
        float cz = rd->eqMinZ + opts.poseSearchEyeHeight;

        for (int h = 0; h < opts.poseSearchHeadings; ++h) {
            float heading = 360.0f * h / opts.poseSearchHeadings;
            float pitch = opts.poseSearchPitch;

            float dirX, dirY, dirZ;
            poseToDirection(heading, pitch, dirX, dirY, dirZ);
            setupCamera(*data, cx, cy, cz, dirX, dirY, dirZ);
            if (data->zoneShader) data->zoneShader->beginFrame();
            data->driver->beginScene(true, true, irr::video::SColor(255, 40, 40, 60));
            if (data->zoneShader) data->zoneShader->beginFrame();

            ZoneBench::CameraState camState{cx, cy, cz, dirX, dirY, dirZ, fov, aspect};
            int regionsVisible = 0, regionsDrawn = 0, regionsOccluded = 0;
            drawEntries.clear();
            ZoneBench::collectDrawEntries(
                *data, camState,
                opts.materialSort, opts.pvsCull, opts.frustumCull, opts.occlusionCull,
                opts.frontToBack, opts.maxDraws, opts.maxPolys,
                drawEntries, regionsVisible, regionsDrawn, regionsOccluded);
            if (!data->objects.empty()) {
                int objDraws = 0, objPolys = 0;
                ZoneBench::collectObjectDrawEntries(
                    *data, camState, opts.objectRenderDist,
                    opts.pvsCull, opts.frustumCull, opts.occlusionCull,
                    drawEntries, objDraws, objPolys);
            }
            renderDrawEntries(*data, drawEntries, opts.depthTest);
#ifdef EQT_HAS_GLES2
            glFinish();
#endif
            Candidate c;
            c.x = cx; c.y = cy; c.z = cz; c.heading = heading; c.pitch = pitch;
            c.draws = static_cast<int>(drawEntries.size());
            computeFrameStats(*data, 40, 40, 60, c.distinctColors, c.skyFraction);
            double nonSkyFraction = 1.0 - c.skyFraction;
            c.score = static_cast<double>(c.draws) *
                      static_cast<double>(c.distinctColors) * nonSkyFraction;

            data->driver->endScene();

            if (csvOut) {
                fprintf(csvOut, "%.1f,%.1f,%.1f,%.1f,%.1f,%d,%zu,%.4f,%.1f\n",
                        c.x, c.y, c.z, c.heading, c.pitch, c.draws,
                        c.distinctColors, c.skyFraction, c.score);
            }
            candidates.push_back(c);
        }
    }
    if (csvOut) fclose(csvOut);

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

    int topK = std::min<int>(20, static_cast<int>(candidates.size()));
    printf("\nTop %d candidate poses (by draws * distinctColors * nonSkyFraction):\n", topK);
    printf("  %10s %10s %10s %8s %6s %6s %8s %8s %10s\n",
           "x", "y", "z", "heading", "pitch", "draws", "colors", "sky%", "score");
    for (int i = 0; i < topK; ++i) {
        const auto& c = candidates[i];
        printf("  %10.1f %10.1f %10.1f %8.1f %6.1f %6d %8zu %7.1f%% %10.1f  --camera-pose %.1f,%.1f,%.1f,%.1f,%.1f\n",
               c.x, c.y, c.z, c.heading, c.pitch, c.draws, c.distinctColors,
               c.skyFraction * 100.0, c.score, c.x, c.y, c.z, c.heading, c.pitch);
    }
    return 0;
}

// ── H02b: static multi-pose capture ──
// When --camera-pose was given (one or more times), skip the moving
// navmesh/orbit camera path entirely: render exactly one frame per pose,
// pose index i -> frame index i, which lines up 1:1 with the existing
// --dump-frames / manifest "designatedFrames" mechanism below with no
// changes needed to it or to the CI gate script.
int runStaticPoseCapture(std::unique_ptr<ZoneBench::ZoneBenchData>& data, const Options& opts,
                         ZoneBench::MetricsCollector& metrics) {
    // T291: --warmup applies here too -- it used to be silently ignored
    // on this path (only the moving/--path-file loop below honoured it),
    // so the first measured pose absorbed every one-time cost (shader
    // compile/link, first texture upload, first-touch faults): 20.9-25.5
    // ms vs 2.7-5.1 ms for the other two poses, and ALL of the CI gate's
    // p95 variance lived in that one cold frame. Warmup renders the whole
    // pose list opts.warmup extra times first; those passes are never
    // measured, dumped, or written to the CSV, so the 1:1 pose->frame
    // mapping the gate depends on is untouched (golden PNGs and the
    // draws/state/poly metrics verified byte-identical either way).
    // See refactor/T291_visual_gate_cpu_metric.md.
    const int warmupPasses = (opts.warmup > 0) ? opts.warmup : 0;
    printf("\n=== Static pose capture: %zu poses (%d warmup passes) ===\n",
           opts.cameraPoses.size(), warmupPasses);
    metrics.reserve(static_cast<int>(opts.cameraPoses.size()));
    metrics.setDumpPerFrame(opts.dump);

    std::vector<ZoneBench::DrawEntry> drawEntries;
    drawEntries.reserve(2048);
    float fov = 60.0f * 3.14159265f / 180.0f;
    float aspect = static_cast<float>(opts.width) / static_cast<float>(opts.height);

    for (int pass = 0; pass <= warmupPasses; ++pass) {
    const bool measuring = (pass == warmupPasses);
    for (size_t i = 0; i < opts.cameraPoses.size(); ++i) {
        const auto& pose = opts.cameraPoses[i];
        float dirX, dirY, dirZ;
        poseToDirection(pose.heading, pose.pitch, dirX, dirY, dirZ);

        setupCamera(*data, pose.x, pose.y, pose.z, dirX, dirY, dirZ);
        if (data->zoneShader) data->zoneShader->beginFrame();
        data->driver->beginScene(true, true, irr::video::SColor(255, 40, 40, 60));
        if (data->zoneShader) data->zoneShader->beginFrame();

        if (measuring) {
            metrics.beginFrame(static_cast<int>(i), pose.x, pose.y, pose.z,
                               pose.heading, pose.pitch);
        }

        ZoneBench::CameraState camState{pose.x, pose.y, pose.z, dirX, dirY, dirZ, fov, aspect};
        int regionsVisible = 0, regionsDrawn = 0, regionsOccluded = 0;
        float lightRadius = 0.0f;
        int regionsDemotedRadius = 0;
        drawEntries.clear();
        ZoneBench::collectDrawEntries(
            *data, camState,
            opts.materialSort, opts.pvsCull, opts.frustumCull, opts.occlusionCull,
            opts.frontToBack, opts.maxDraws, opts.maxPolys,
            drawEntries, regionsVisible, regionsDrawn, regionsOccluded,
            &lightRadius, &regionsDemotedRadius);
        if (measuring) metrics.setRadiusStats(lightRadius, regionsDemotedRadius);
        if (!data->objects.empty()) {
            int objDraws = 0, objPolys = 0;
            ZoneBench::collectObjectDrawEntries(
                *data, camState, opts.objectRenderDist,
                opts.pvsCull, opts.frustumCull, opts.occlusionCull,
                drawEntries, objDraws, objPolys);
        }
        int actualDraws = 0;
        int stateChanges = renderDrawEntries(*data, drawEntries, opts.depthTest,
                                              nullptr, opts.mergeZoneDraws, &actualDraws,
                                              opts.bakedUnmergedZoneDraws);
        int polyCount = countPolygons(drawEntries);
        if (measuring) {
            metrics.endDraw(actualDraws, stateChanges, polyCount,
                            regionsVisible, regionsDrawn, regionsOccluded);
        }
        if (measuring && opts.materialCounts) {
            auto counts = countMaterialFamilies(drawEntries, data->zoneShader.get());
            metrics.setMaterialCounts(counts.first, counts.second);
        }

#ifdef EQT_HAS_GLES2
        if (measuring && opts.dumpFilters > 0 && i == 0) {
            dumpFilterState(*data, drawEntries, opts.dumpFilters);
        }
        glFinish();
#endif
        if (measuring) {
            metrics.endGpuSync();
            metrics.endFrame();
        }

        if (measuring && !opts.dumpPngPrefix.empty()) {
            irr::video::IImage* shot = data->driver->createScreenShot();
            std::string path = opts.dumpPngPrefix + "_f" + std::to_string(i) + ".png";
            if (shot) {
                bool ok = data->driver->writeImageToFile(shot, path.c_str());
                shot->drop();
                printf("[dump-frames] %s: %s\n", path.c_str(), ok ? "saved" : "FAILED to save");
            } else {
                printf("[dump-frames] %s: createScreenShot() FAILED\n", path.c_str());
            }
        }

        data->driver->endScene();
        // B949: reorders the texture cache's LRU by the draw stamps this
        // pose frame recorded, so the eviction pressure of the NEXT pose's
        // loads evicts least-recently-DRAWN, not oldest-loaded.
        if (data->textureCache) data->textureCache->endFrame();
    }
    }

    metrics.printSummary();
    if (!opts.csvFile.empty()) metrics.writeCSV(opts.csvFile);

    if (data->textureCache) {
        BenchMemorySnapshot snap = computeMemorySnapshot(*data, opts.width, opts.height);
        printf("Texture cache: %zu / %zu bytes (%zu textures)\n",
               snap.textureCacheBytesUsed, snap.textureCacheBytesLimit, snap.textureCacheCount);
        printf("Total tracked: %zu bytes | RSS: %zu bytes | untracked: %zu bytes\n",
               snap.totalEstimate, snap.processRssBytes, snap.untrackedBytes);
        if (!opts.memoryJsonPath.empty()) {
            writeMemoryJson(opts.memoryJsonPath, opts.zoneName, snap);
        }
    }

    printf("Benchmark complete.\n");
    return 0;
}
