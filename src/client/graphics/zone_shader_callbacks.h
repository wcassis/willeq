#pragma once
// The three ZoneShaderManager shader-constant callbacks, split out of
// zone_shader.cpp by Batch J unit J09. PRIVATE internal header: this file must
// be included by zone_shader.cpp and NOTHING ELSE.
// See refactor/J09_zone_shader_split.md.

#include "client/graphics/zone_shader.h"
#include "client/graphics/zone_depth_tie_break.h"
#include "common/logging.h"
#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#elif defined(EQT_HAS_DRM)
#include <GL/gl.h>
#endif
#include <cstring>

namespace EQT {
namespace Graphics {

// ============================================================================
// Shader callbacks
// ============================================================================

// Per-vertex lighting callback with cached uniform locations and per-frame skip
class ShaderCallback : public irr::video::IShaderConstantSetCallBack {
public:
    ShaderCallback(ZoneShaderManager* owner) : owner_(owner) {}

    // T408 (B110): SMaterial::MaterialTypeParam is the per-material
    // "these vertices carry the WLD's baked lighting" flag. These programs
    // are shared with every entity mesh, whose aColor is opaque white, so
    // the flag -- not the material type -- is what keeps the pre-T408
    // equation exact for them. The field was unused on this callback.
    void OnSetMaterial(const irr::video::SMaterial& material) override {
        bakedFlag_ = (material.MaterialTypeParam > 0.5f) ? 1.0f : 0.0f;
        // B976 (#294): MaterialTypeParam2 is the zone-region depth tie-break
        // flag (stamped by ZoneMeshBuilder::setZoneGeometryDepthTie). Entities
        // and other shared-program meshes carry 0, so they are never biased;
        // the region programs are the only ones that even declare uDepthTieBias.
        depthTieBias_ = DepthTie::uDepthTieBiasForFlag(material.MaterialTypeParam2);
    }

    void OnSetConstants(irr::video::IMaterialRendererServices* services,
                        irr::s32 userData) override {
        irr::video::IVideoDriver* driver = services->getVideoDriver();

#if defined(EQT_HAS_DRM) && !defined(EQT_HAS_GLES2)
        glEnable(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
#endif

#ifdef EQT_HAS_GLES2
        // Resolve uniform locations once on first call
        if (locWVP_ == -2) {
            GLuint prog;
            glGetIntegerv(GL_CURRENT_PROGRAM, reinterpret_cast<GLint*>(&prog));
            LOG_DEBUG(MOD_SHADER, "ShaderCallback: resolving uniform locations for program {}", prog);
            locWVP_        = glGetUniformLocation(prog, "mWorldViewProj");
            locWorld_      = glGetUniformLocation(prog, "mWorld");
            locSunDir_     = glGetUniformLocation(prog, "uSunDir");
            locSunColor_   = glGetUniformLocation(prog, "uSunColor");
            locAmbient_    = glGetUniformLocation(prog, "uAmbientColor");
            locTint_       = glGetUniformLocation(prog, "uTintColor");
            locFogStart_   = glGetUniformLocation(prog, "uFogStart");
            locFogEnd_     = glGetUniformLocation(prog, "uFogEnd");
            locLightPos_   = glGetUniformLocation(prog, "uLightPos[0]");
            locLightColor_ = glGetUniformLocation(prog, "uLightColor[0]");
            locLightAtten_ = glGetUniformLocation(prog, "uLightAtten[0]");
            locFogColor_   = glGetUniformLocation(prog, "uFogColor");
            locTexture_    = glGetUniformLocation(prog, "uTexture");
            locAlphaTex_   = glGetUniformLocation(prog, "uAlphaTexture");
            locPlayerLightPos_   = glGetUniformLocation(prog, "uPlayerLightPos");
            locPlayerLightColor_ = glGetUniformLocation(prog, "uPlayerLightColor");
            locPlayerLightAtten_ = glGetUniformLocation(prog, "uPlayerLightAtten");
            locAttenLUT_         = glGetUniformLocation(prog, "uAttenLUT");
            locLUTScale_         = glGetUniformLocation(prog, "uLUTScale");
            locNumPointLights_   = glGetUniformLocation(prog, "uNumPointLights");
            locFireGlowLightPos_    = glGetUniformLocation(prog, "uFireGlowLightPos[0]");
            locFireGlowLightColor_  = glGetUniformLocation(prog, "uFireGlowLightColor[0]");
            locFireGlowLightRadius_ = glGetUniformLocation(prog, "uFireGlowLightRadius[0]");
            locFireGlowLightIntensity_ = glGetUniformLocation(prog, "uFireGlowLightIntensity[0]");
            locNumFireGlowLights_   = glGetUniformLocation(prog, "uNumFireGlowLights");
            locShaderDebug_         = glGetUniformLocation(prog, "uShaderDebug");
            locUnifiedLighting_     = glGetUniformLocation(prog, "uUnifiedLighting");
            locBakedLight_          = glGetUniformLocation(prog, "uBakedLight");
            locBakedFlag_           = glGetUniformLocation(prog, "uBakedFlag");
            // B976: -1 (not 0) on programs without the uniform (atlas/wind
            // don't declare it), so the per-material upload below is skipped.
            locDepthTieBias_        = glGetUniformLocation(prog, "uDepthTieBias");
            LOG_DEBUG(MOD_SHADER, "ShaderCallback: uniforms resolved — WVP={} World={} SunDir={} "
                      "LightPos={} LightColor={} LightAtten={} PlayerLightPos={} Texture={} AlphaTex={} "
                      "NumPointLights={} NumFireGlow={}",
                      locWVP_, locWorld_, locSunDir_, locLightPos_, locLightColor_, locLightAtten_,
                      locPlayerLightPos_, locTexture_, locAlphaTex_, locNumPointLights_, locNumFireGlowLights_);
        }

        // Per-node uniforms (change every node — depend on World matrix)
        irr::core::matrix4 worldViewProj = driver->getTransform(irr::video::ETS_PROJECTION);
        worldViewProj *= driver->getTransform(irr::video::ETS_VIEW);
        worldViewProj *= driver->getTransform(irr::video::ETS_WORLD);
        if (locWVP_ >= 0)
            glUniformMatrix4fv(locWVP_, 1, GL_FALSE, worldViewProj.pointer());

        irr::core::matrix4 world = driver->getTransform(irr::video::ETS_WORLD);
        if (locWorld_ >= 0)
            glUniformMatrix4fv(locWorld_, 1, GL_FALSE, world.pointer());

        // T408: baked-light knobs. uBakedFlag is PER MATERIAL, so it cannot
        // live in the per-frame block below.
        if (locBakedFlag_ >= 0)  glUniform1f(locBakedFlag_, bakedFlag_);
        if (locBakedLight_ >= 0) glUniform3fv(locBakedLight_, 1, owner_->bakedLight());
        // B976: depth tie-break bias is also PER MATERIAL (from Param2).
        if (locDepthTieBias_ >= 0) glUniform1f(locDepthTieBias_, depthTieBias_);

        // Per-frame uniforms (same for every node — skip if already set this frame)
        if (lastFrameId_ != owner_->frameId()) {
            lastFrameId_ = owner_->frameId();

            if (!firstFrameLogged_) {
                firstFrameLogged_ = true;
                LOG_DEBUG(MOD_SHADER, "ShaderCallback: first per-frame uniforms frame={} "
                          "sun=({:.2f},{:.2f},{:.2f}) sunColor=({:.2f},{:.2f},{:.2f}) "
                          "ambient=({:.2f},{:.2f},{:.2f}) tint=({:.2f},{:.2f},{:.2f}) "
                          "playerLightPos=({:.1f},{:.1f},{:.1f}) playerLightColor=({:.2f},{:.2f},{:.2f}) "
                          "playerLightAtten=({:.3f},{:.3f},{:.3f}) lutTex={} lutScale={:.4f} "
                          "numLights={} numFireGlow={} fogStart={:.1f} fogEnd={:.1f}",
                          owner_->frameId(),
                          owner_->sunDir()[0], owner_->sunDir()[1], owner_->sunDir()[2],
                          owner_->sunColor()[0], owner_->sunColor()[1], owner_->sunColor()[2],
                          owner_->ambientColor()[0], owner_->ambientColor()[1], owner_->ambientColor()[2],
                          owner_->tintColor()[0], owner_->tintColor()[1], owner_->tintColor()[2],
                          owner_->playerLightPos()[0], owner_->playerLightPos()[1], owner_->playerLightPos()[2],
                          owner_->playerLightColor()[0], owner_->playerLightColor()[1], owner_->playerLightColor()[2],
                          owner_->playerLightAtten()[0], owner_->playerLightAtten()[1], owner_->playerLightAtten()[2],
                          owner_->attenLutTexture(), owner_->lutScale(),
                          owner_->numPointLights(), owner_->numFireGlowLights(),
                          owner_->fogStart(), owner_->fogEnd());
            } else {
                LOG_TRACE(MOD_SHADER, "ShaderCallback: per-frame uniforms frame={} numLights={} numFireGlow={}",
                          owner_->frameId(), owner_->numPointLights(), owner_->numFireGlowLights());
            }

            if (locSunDir_ >= 0)     glUniform3fv(locSunDir_, 1, owner_->sunDir());
            if (locSunColor_ >= 0)   glUniform3fv(locSunColor_, 1, owner_->sunColor());
            if (locAmbient_ >= 0)    glUniform3fv(locAmbient_, 1, owner_->ambientColor());
            if (locTint_ >= 0)       glUniform3fv(locTint_, 1, owner_->tintColor());
            if (locFogStart_ >= 0)   glUniform1f(locFogStart_, owner_->fogStart());
            if (locFogEnd_ >= 0)     glUniform1f(locFogEnd_, owner_->fogEnd());
            if (locLightPos_ >= 0)   glUniform3fv(locLightPos_, ZoneShaderManager::MAX_POINT_LIGHTS, owner_->lightPos());
            if (locLightColor_ >= 0) glUniform3fv(locLightColor_, ZoneShaderManager::MAX_POINT_LIGHTS, owner_->lightColor());
            if (locLightAtten_ >= 0) glUniform3fv(locLightAtten_, ZoneShaderManager::MAX_POINT_LIGHTS, owner_->lightAtten());
            if (locFogColor_ >= 0)   glUniform4fv(locFogColor_, 1, owner_->fogColor());
            if (locTexture_ >= 0)    glUniform1i(locTexture_, 0);
            if (locAlphaTex_ >= 0)   glUniform1i(locAlphaTex_, 1);

            // Per-pixel player light (FS uniforms + LUT texture)
            if (locPlayerLightPos_ >= 0)   glUniform3fv(locPlayerLightPos_, 1, owner_->playerLightPos());
            if (locPlayerLightColor_ >= 0) glUniform3fv(locPlayerLightColor_, 1, owner_->playerLightColor());
            if (locPlayerLightAtten_ >= 0) glUniform3fv(locPlayerLightAtten_, 1, owner_->playerLightAtten());
            if (locAttenLUT_ >= 0 && owner_->attenLutTexture() > 0) {
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, owner_->attenLutTexture());
                glActiveTexture(GL_TEXTURE0);
                glUniform1i(locAttenLUT_, 2);
            }
            if (locLUTScale_ >= 0) glUniform1f(locLUTScale_, owner_->lutScale());
            if (locNumPointLights_ >= 0)   glUniform1i(locNumPointLights_, owner_->numPointLights());

            // Fire glow fog emitters
            if (locFireGlowLightPos_ >= 0)    glUniform3fv(locFireGlowLightPos_, ZoneShaderManager::MAX_FIRE_GLOW_LIGHTS, owner_->fireGlowLightPos());
            if (locFireGlowLightColor_ >= 0)  glUniform3fv(locFireGlowLightColor_, ZoneShaderManager::MAX_FIRE_GLOW_LIGHTS, owner_->fireGlowLightColor());
            if (locFireGlowLightRadius_ >= 0) glUniform1fv(locFireGlowLightRadius_, ZoneShaderManager::MAX_FIRE_GLOW_LIGHTS, owner_->fireGlowLightRadius());
            if (locFireGlowLightIntensity_ >= 0) glUniform1fv(locFireGlowLightIntensity_, ZoneShaderManager::MAX_FIRE_GLOW_LIGHTS, owner_->fireGlowLightIntensity());
            if (locNumFireGlowLights_ >= 0)   glUniform1i(locNumFireGlowLights_, owner_->numFireGlowLights());
            if (locShaderDebug_ >= 0)         glUniform1i(locShaderDebug_, owner_->shaderDebug());
            if (locUnifiedLighting_ >= 0)    glUniform1i(locUnifiedLighting_, owner_->unifiedLighting() ? 1 : 0);
        }
#else
        // Desktop GL path — use string-based setVertexShaderConstant (no perf concern)
        irr::core::matrix4 worldViewProj = driver->getTransform(irr::video::ETS_PROJECTION);
        worldViewProj *= driver->getTransform(irr::video::ETS_VIEW);
        worldViewProj *= driver->getTransform(irr::video::ETS_WORLD);
        services->setVertexShaderConstant("mWorldViewProj", worldViewProj.pointer(), 16);

        irr::core::matrix4 world = driver->getTransform(irr::video::ETS_WORLD);
        services->setVertexShaderConstant("mWorld", world.pointer(), 16);

        services->setVertexShaderConstant("uSunDir", owner_->sunDir(), 3);
        services->setVertexShaderConstant("uSunColor", owner_->sunColor(), 3);
        services->setVertexShaderConstant("uAmbientColor", owner_->ambientColor(), 3);
        services->setVertexShaderConstant("uTintColor", owner_->tintColor(), 3);

        float fogStart = owner_->fogStart();
        float fogEnd = owner_->fogEnd();
        services->setVertexShaderConstant("uFogStart", &fogStart, 1);
        services->setVertexShaderConstant("uFogEnd", &fogEnd, 1);

        services->setVertexShaderConstant("uLightPos[0]", owner_->lightPos(), ZoneShaderManager::MAX_POINT_LIGHTS * 3);
        services->setVertexShaderConstant("uLightColor[0]", owner_->lightColor(), ZoneShaderManager::MAX_POINT_LIGHTS * 3);
        services->setVertexShaderConstant("uLightAtten[0]", owner_->lightAtten(), ZoneShaderManager::MAX_POINT_LIGHTS * 3);

        float fogColor[4];
        std::memcpy(fogColor, owner_->fogColor(), sizeof(fogColor));
        services->setPixelShaderConstant("uFogColor", fogColor, 4);

        irr::s32 texUnit = 0;
        services->setPixelShaderConstant("uTexture", &texUnit, 1);

        // Per-pixel player light (FS uniforms)
        services->setPixelShaderConstant("uPlayerLightPos", owner_->playerLightPos(), 3);
        services->setPixelShaderConstant("uPlayerLightColor", owner_->playerLightColor(), 3);
        services->setPixelShaderConstant("uPlayerLightAtten", owner_->playerLightAtten(), 3);

        irr::s32 numLights = owner_->numPointLights();
        services->setVertexShaderConstant("uNumPointLights", &numLights, 1);

        // Fire glow fog emitters
        services->setVertexShaderConstant("uFireGlowLightPos[0]", owner_->fireGlowLightPos(), ZoneShaderManager::MAX_FIRE_GLOW_LIGHTS * 3);
        services->setVertexShaderConstant("uFireGlowLightColor[0]", owner_->fireGlowLightColor(), ZoneShaderManager::MAX_FIRE_GLOW_LIGHTS * 3);
        services->setVertexShaderConstant("uFireGlowLightRadius[0]", owner_->fireGlowLightRadius(), ZoneShaderManager::MAX_FIRE_GLOW_LIGHTS);
        services->setVertexShaderConstant("uFireGlowLightIntensity[0]", owner_->fireGlowLightIntensity(), ZoneShaderManager::MAX_FIRE_GLOW_LIGHTS);
        irr::s32 numFireGlowLights = owner_->numFireGlowLights();
        services->setVertexShaderConstant("uNumFireGlowLights", &numFireGlowLights, 1);
        irr::s32 unifiedLighting = owner_->unifiedLighting() ? 1 : 0;
        services->setVertexShaderConstant("uUnifiedLighting", &unifiedLighting, 1);
        services->setVertexShaderConstant("uBakedLight", owner_->bakedLight(), 3);
        services->setVertexShaderConstant("uBakedFlag", &bakedFlag_, 1);
        // B976: depth tie-break bias, per material.
        services->setVertexShaderConstant("uDepthTieBias", &depthTieBias_, 1);
#endif
    }

private:
    ZoneShaderManager* owner_;
    float bakedFlag_ = 0.0f;   // T408: set by OnSetMaterial, uploaded per node
    float depthTieBias_ = 0.0f; // B976: set by OnSetMaterial from MaterialTypeParam2
#ifdef EQT_HAS_GLES2
    GLint locBakedLight_ = -2, locBakedFlag_ = -2, locDepthTieBias_ = -2;
    // Cached uniform locations (-2 = not yet resolved)
    GLint locWVP_ = -2, locWorld_ = -2;
    GLint locSunDir_ = -2, locSunColor_ = -2, locAmbient_ = -2, locTint_ = -2;
    GLint locFogStart_ = -2, locFogEnd_ = -2;
    GLint locLightPos_ = -2, locLightColor_ = -2, locLightAtten_ = -2;
    GLint locFogColor_ = -2, locTexture_ = -2, locAlphaTex_ = -2;
    GLint locPlayerLightPos_ = -2, locPlayerLightColor_ = -2, locPlayerLightAtten_ = -2;
    GLint locAttenLUT_ = -2, locLUTScale_ = -2;
    GLint locNumPointLights_ = -2;
    GLint locFireGlowLightPos_ = -2, locFireGlowLightColor_ = -2;
    GLint locFireGlowLightRadius_ = -2, locFireGlowLightIntensity_ = -2;
    GLint locNumFireGlowLights_ = -2;
    GLint locShaderDebug_ = -2;
    GLint locUnifiedLighting_ = -2;
    uint64_t lastFrameId_ = 0;
    bool firstFrameLogged_ = false;
#endif
};

// Atlas shader callback — per-vertex lighting, atlas texture binding
// Cached uniform locations and per-frame skip for GLES2
class AtlasShaderCallback : public irr::video::IShaderConstantSetCallBack {
public:
    AtlasShaderCallback(ZoneShaderManager* owner, bool hasAlphaTexture)
        : owner_(owner), hasAlphaTexture_(hasAlphaTexture) {}

    void OnSetMaterial(const irr::video::SMaterial& material) override {
        currentPageIndex_ = static_cast<int>(material.MaterialTypeParam);
        if (hasAlphaTexture_) {
            currentAlphaPageIndex_ = static_cast<int>(material.MaterialTypeParam2);
        }
    }

    void OnSetConstants(irr::video::IMaterialRendererServices* services,
                        irr::s32 userData) override {
        irr::video::IVideoDriver* driver = services->getVideoDriver();

#ifdef EQT_HAS_DRM
        // Bind atlas page texture to unit 0 (skip if same page already bound)
        uint32_t rgbTex = owner_->getAtlasPageTexture(static_cast<uint16_t>(currentPageIndex_));
        if (bindLogCount_ < 5) {
            LOG_INFO(MOD_GRAPHICS, "AtlasShader: binding page {} -> GL tex {} (alpha={})",
                     currentPageIndex_, rgbTex, hasAlphaTexture_);
            ++bindLogCount_;
        }
        if (rgbTex > 0 && rgbTex != lastBoundRgbTex_) {
            glActiveTexture(GL_TEXTURE0);
#ifndef EQT_HAS_GLES2
            glEnable(GL_TEXTURE_2D);
#endif
            glBindTexture(GL_TEXTURE_2D, rgbTex);
            // Filter/wrap params are set once at texture creation — no need to re-set per draw
            lastBoundRgbTex_ = rgbTex;
        }

        if (hasAlphaTexture_) {
            uint32_t alphaTex = owner_->getAtlasPageTexture(static_cast<uint16_t>(currentAlphaPageIndex_));
            if (alphaTex > 0 && alphaTex != lastBoundAlphaTex_) {
                glActiveTexture(GL_TEXTURE1);
#ifndef EQT_HAS_GLES2
                glEnable(GL_TEXTURE_2D);
#endif
                glBindTexture(GL_TEXTURE_2D, alphaTex);
                glActiveTexture(GL_TEXTURE0);
                lastBoundAlphaTex_ = alphaTex;
            }
        }
#endif

#ifdef EQT_HAS_GLES2
        // Resolve uniform locations once on first call
        if (locWVP_ == -2) {
            GLuint prog;
            glGetIntegerv(GL_CURRENT_PROGRAM, reinterpret_cast<GLint*>(&prog));
            LOG_DEBUG(MOD_SHADER, "AtlasShaderCallback: resolving uniform locations for program {} (alpha={})",
                      prog, hasAlphaTexture_);
            locWVP_        = glGetUniformLocation(prog, "mWorldViewProj");
            locWorld_      = glGetUniformLocation(prog, "mWorld");
            locSunDir_     = glGetUniformLocation(prog, "uSunDir");
            locSunColor_   = glGetUniformLocation(prog, "uSunColor");
            locAmbient_    = glGetUniformLocation(prog, "uAmbientColor");
            locTint_       = glGetUniformLocation(prog, "uTintColor");
            locFogStart_   = glGetUniformLocation(prog, "uFogStart");
            locFogEnd_     = glGetUniformLocation(prog, "uFogEnd");
            locLightPos_   = glGetUniformLocation(prog, "uLightPos[0]");
            locLightColor_ = glGetUniformLocation(prog, "uLightColor[0]");
            locLightAtten_ = glGetUniformLocation(prog, "uLightAtten[0]");
            locFogColor_   = glGetUniformLocation(prog, "uFogColor");
            locTexture_    = glGetUniformLocation(prog, "uTexture");
            locAlphaTex_   = glGetUniformLocation(prog, "uAlphaTexture");
            locPlayerLightPos_   = glGetUniformLocation(prog, "uPlayerLightPos");
            locPlayerLightColor_ = glGetUniformLocation(prog, "uPlayerLightColor");
            locPlayerLightAtten_ = glGetUniformLocation(prog, "uPlayerLightAtten");
            locAttenLUT_         = glGetUniformLocation(prog, "uAttenLUT");
            locLUTScale_         = glGetUniformLocation(prog, "uLUTScale");
            locNumPointLights_   = glGetUniformLocation(prog, "uNumPointLights");
            locFireGlowLightPos_    = glGetUniformLocation(prog, "uFireGlowLightPos[0]");
            locFireGlowLightColor_  = glGetUniformLocation(prog, "uFireGlowLightColor[0]");
            locFireGlowLightRadius_ = glGetUniformLocation(prog, "uFireGlowLightRadius[0]");
            locFireGlowLightIntensity_ = glGetUniformLocation(prog, "uFireGlowLightIntensity[0]");
            locNumFireGlowLights_   = glGetUniformLocation(prog, "uNumFireGlowLights");
            locShaderDebug_         = glGetUniformLocation(prog, "uShaderDebug");
            locUnifiedLighting_     = glGetUniformLocation(prog, "uUnifiedLighting");
            LOG_DEBUG(MOD_SHADER, "AtlasShaderCallback: uniforms resolved — WVP={} PlayerLightPos={} "
                      "PlayerLightColor={} AttenLUT={} NumPointLights={}",
                      locWVP_, locPlayerLightPos_, locPlayerLightColor_, locAttenLUT_, locNumPointLights_);
        }

        // Per-node uniforms
        irr::core::matrix4 worldViewProj = driver->getTransform(irr::video::ETS_PROJECTION);
        worldViewProj *= driver->getTransform(irr::video::ETS_VIEW);
        worldViewProj *= driver->getTransform(irr::video::ETS_WORLD);
        if (locWVP_ >= 0)
            glUniformMatrix4fv(locWVP_, 1, GL_FALSE, worldViewProj.pointer());

        irr::core::matrix4 world = driver->getTransform(irr::video::ETS_WORLD);
        if (locWorld_ >= 0)
            glUniformMatrix4fv(locWorld_, 1, GL_FALSE, world.pointer());

        // Per-frame uniforms
        if (lastFrameId_ != owner_->frameId()) {
            lastFrameId_ = owner_->frameId();
            lastBoundRgbTex_ = 0;
            lastBoundAlphaTex_ = 0;

            if (!firstFrameLogged_) {
                firstFrameLogged_ = true;
                LOG_DEBUG(MOD_SHADER, "AtlasShaderCallback: first per-frame uniforms frame={} "
                          "playerLightPos=({:.1f},{:.1f},{:.1f}) playerLightColor=({:.2f},{:.2f},{:.2f}) "
                          "lutTex={} lutScale={:.4f} numLights={} numFireGlow={}",
                          owner_->frameId(),
                          owner_->playerLightPos()[0], owner_->playerLightPos()[1], owner_->playerLightPos()[2],
                          owner_->playerLightColor()[0], owner_->playerLightColor()[1], owner_->playerLightColor()[2],
                          owner_->attenLutTexture(), owner_->lutScale(),
                          owner_->numPointLights(), owner_->numFireGlowLights());
            }

            if (locSunDir_ >= 0)     glUniform3fv(locSunDir_, 1, owner_->sunDir());
            if (locSunColor_ >= 0)   glUniform3fv(locSunColor_, 1, owner_->sunColor());
            if (locAmbient_ >= 0)    glUniform3fv(locAmbient_, 1, owner_->ambientColor());
            if (locTint_ >= 0)       glUniform3fv(locTint_, 1, owner_->tintColor());
            if (locFogStart_ >= 0)   glUniform1f(locFogStart_, owner_->fogStart());
            if (locFogEnd_ >= 0)     glUniform1f(locFogEnd_, owner_->fogEnd());
            if (locLightPos_ >= 0)   glUniform3fv(locLightPos_, ZoneShaderManager::MAX_POINT_LIGHTS, owner_->lightPos());
            if (locLightColor_ >= 0) glUniform3fv(locLightColor_, ZoneShaderManager::MAX_POINT_LIGHTS, owner_->lightColor());
            if (locLightAtten_ >= 0) glUniform3fv(locLightAtten_, ZoneShaderManager::MAX_POINT_LIGHTS, owner_->lightAtten());
            if (locFogColor_ >= 0)   glUniform4fv(locFogColor_, 1, owner_->fogColor());
            if (locTexture_ >= 0)    glUniform1i(locTexture_, 0);
            if (hasAlphaTexture_ && locAlphaTex_ >= 0)
                glUniform1i(locAlphaTex_, 1);

            // Per-pixel player light (FS uniforms + LUT texture)
            if (locPlayerLightPos_ >= 0)   glUniform3fv(locPlayerLightPos_, 1, owner_->playerLightPos());
            if (locPlayerLightColor_ >= 0) glUniform3fv(locPlayerLightColor_, 1, owner_->playerLightColor());
            if (locPlayerLightAtten_ >= 0) glUniform3fv(locPlayerLightAtten_, 1, owner_->playerLightAtten());
            if (locAttenLUT_ >= 0 && owner_->attenLutTexture() > 0) {
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, owner_->attenLutTexture());
                glActiveTexture(GL_TEXTURE0);
                glUniform1i(locAttenLUT_, 2);
            }
            if (locLUTScale_ >= 0) glUniform1f(locLUTScale_, owner_->lutScale());
            if (locNumPointLights_ >= 0)   glUniform1i(locNumPointLights_, owner_->numPointLights());

            // Fire glow fog emitters
            if (locFireGlowLightPos_ >= 0)    glUniform3fv(locFireGlowLightPos_, ZoneShaderManager::MAX_FIRE_GLOW_LIGHTS, owner_->fireGlowLightPos());
            if (locFireGlowLightColor_ >= 0)  glUniform3fv(locFireGlowLightColor_, ZoneShaderManager::MAX_FIRE_GLOW_LIGHTS, owner_->fireGlowLightColor());
            if (locFireGlowLightRadius_ >= 0) glUniform1fv(locFireGlowLightRadius_, ZoneShaderManager::MAX_FIRE_GLOW_LIGHTS, owner_->fireGlowLightRadius());
            if (locFireGlowLightIntensity_ >= 0) glUniform1fv(locFireGlowLightIntensity_, ZoneShaderManager::MAX_FIRE_GLOW_LIGHTS, owner_->fireGlowLightIntensity());
            if (locNumFireGlowLights_ >= 0)   glUniform1i(locNumFireGlowLights_, owner_->numFireGlowLights());
            if (locShaderDebug_ >= 0)         glUniform1i(locShaderDebug_, owner_->shaderDebug());
            if (locUnifiedLighting_ >= 0)    glUniform1i(locUnifiedLighting_, owner_->unifiedLighting() ? 1 : 0);
        }
#else
        // Desktop GL path (AtlasShaderCallback)
        irr::core::matrix4 worldViewProj = driver->getTransform(irr::video::ETS_PROJECTION);
        worldViewProj *= driver->getTransform(irr::video::ETS_VIEW);
        worldViewProj *= driver->getTransform(irr::video::ETS_WORLD);
        services->setVertexShaderConstant("mWorldViewProj", worldViewProj.pointer(), 16);

        irr::core::matrix4 world = driver->getTransform(irr::video::ETS_WORLD);
        services->setVertexShaderConstant("mWorld", world.pointer(), 16);

        services->setVertexShaderConstant("uSunDir", owner_->sunDir(), 3);
        services->setVertexShaderConstant("uSunColor", owner_->sunColor(), 3);
        services->setVertexShaderConstant("uAmbientColor", owner_->ambientColor(), 3);
        services->setVertexShaderConstant("uTintColor", owner_->tintColor(), 3);

        float fogStart = owner_->fogStart();
        float fogEnd = owner_->fogEnd();
        services->setVertexShaderConstant("uFogStart", &fogStart, 1);
        services->setVertexShaderConstant("uFogEnd", &fogEnd, 1);

        services->setVertexShaderConstant("uLightPos[0]", owner_->lightPos(), ZoneShaderManager::MAX_POINT_LIGHTS * 3);
        services->setVertexShaderConstant("uLightColor[0]", owner_->lightColor(), ZoneShaderManager::MAX_POINT_LIGHTS * 3);
        services->setVertexShaderConstant("uLightAtten[0]", owner_->lightAtten(), ZoneShaderManager::MAX_POINT_LIGHTS * 3);

        float fogColor[4];
        std::memcpy(fogColor, owner_->fogColor(), sizeof(fogColor));
        services->setPixelShaderConstant("uFogColor", fogColor, 4);

        irr::s32 texUnit0 = 0;
        services->setPixelShaderConstant("uTexture", &texUnit0, 1);
        if (hasAlphaTexture_) {
            irr::s32 texUnit1 = 1;
            services->setPixelShaderConstant("uAlphaTexture", &texUnit1, 1);
        }

        // Per-pixel player light (FS uniforms)
        services->setPixelShaderConstant("uPlayerLightPos", owner_->playerLightPos(), 3);
        services->setPixelShaderConstant("uPlayerLightColor", owner_->playerLightColor(), 3);
        services->setPixelShaderConstant("uPlayerLightAtten", owner_->playerLightAtten(), 3);

        irr::s32 numLights = owner_->numPointLights();
        services->setVertexShaderConstant("uNumPointLights", &numLights, 1);

        // Fire glow fog emitters
        services->setVertexShaderConstant("uFireGlowLightPos[0]", owner_->fireGlowLightPos(), ZoneShaderManager::MAX_FIRE_GLOW_LIGHTS * 3);
        services->setVertexShaderConstant("uFireGlowLightColor[0]", owner_->fireGlowLightColor(), ZoneShaderManager::MAX_FIRE_GLOW_LIGHTS * 3);
        services->setVertexShaderConstant("uFireGlowLightRadius[0]", owner_->fireGlowLightRadius(), ZoneShaderManager::MAX_FIRE_GLOW_LIGHTS);
        services->setVertexShaderConstant("uFireGlowLightIntensity[0]", owner_->fireGlowLightIntensity(), ZoneShaderManager::MAX_FIRE_GLOW_LIGHTS);
        irr::s32 numFireGlowLights = owner_->numFireGlowLights();
        services->setVertexShaderConstant("uNumFireGlowLights", &numFireGlowLights, 1);
        irr::s32 unifiedLighting = owner_->unifiedLighting() ? 1 : 0;
        services->setVertexShaderConstant("uUnifiedLighting", &unifiedLighting, 1);
#endif
    }

private:
    ZoneShaderManager* owner_;
    bool hasAlphaTexture_;
    int currentPageIndex_ = 0;
    int currentAlphaPageIndex_ = 0;
    int bindLogCount_ = 0;
    uint32_t lastBoundRgbTex_ = 0;
    uint32_t lastBoundAlphaTex_ = 0;
#ifdef EQT_HAS_GLES2
    GLint locWVP_ = -2, locWorld_ = -2;
    GLint locSunDir_ = -2, locSunColor_ = -2, locAmbient_ = -2, locTint_ = -2;
    GLint locFogStart_ = -2, locFogEnd_ = -2;
    GLint locLightPos_ = -2, locLightColor_ = -2, locLightAtten_ = -2;
    GLint locFogColor_ = -2, locTexture_ = -2, locAlphaTex_ = -2;
    GLint locPlayerLightPos_ = -2, locPlayerLightColor_ = -2, locPlayerLightAtten_ = -2;
    GLint locAttenLUT_ = -2, locLUTScale_ = -2;
    GLint locNumPointLights_ = -2;
    GLint locFireGlowLightPos_ = -2, locFireGlowLightColor_ = -2;
    GLint locFireGlowLightRadius_ = -2, locFireGlowLightIntensity_ = -2;
    GLint locNumFireGlowLights_ = -2;
    GLint locShaderDebug_ = -2;
    GLint locUnifiedLighting_ = -2;
    uint64_t lastFrameId_ = 0;
    bool firstFrameLogged_ = false;
#endif
};

// Wind shader callback — extends ShaderCallback with wind-specific uniforms
// Reads mesh Y bounds from material.MaterialTypeParam/Param2 per node.
class WindShaderCallback : public irr::video::IShaderConstantSetCallBack {
public:
    WindShaderCallback(ZoneShaderManager* owner) : owner_(owner) {}

    void OnSetMaterial(const irr::video::SMaterial& material) override {
        // Unpack mesh Y bounds from material params
        meshMinY_ = material.MaterialTypeParam;
        meshMaxY_ = material.MaterialTypeParam2;
        LOG_TRACE(MOD_SHADER, "WindShaderCallback: OnSetMaterial meshYBounds=[{:.2f},{:.2f}] tex0={}",
                  meshMinY_, meshMaxY_, (void*)material.TextureLayer[0].Texture);
    }

    void OnSetConstants(irr::video::IMaterialRendererServices* services,
                        irr::s32 userData) override {
        irr::video::IVideoDriver* driver = services->getVideoDriver();

#ifdef EQT_HAS_GLES2
        if (locWVP_ == -2) {
            GLuint prog;
            glGetIntegerv(GL_CURRENT_PROGRAM, reinterpret_cast<GLint*>(&prog));
            LOG_DEBUG(MOD_SHADER, "WindShaderCallback: resolving uniform locations for program {}", prog);
            locWVP_        = glGetUniformLocation(prog, "mWorldViewProj");
            locWorld_      = glGetUniformLocation(prog, "mWorld");
            locSunDir_     = glGetUniformLocation(prog, "uSunDir");
            locSunColor_   = glGetUniformLocation(prog, "uSunColor");
            locAmbient_    = glGetUniformLocation(prog, "uAmbientColor");
            locTint_       = glGetUniformLocation(prog, "uTintColor");
            locFogStart_   = glGetUniformLocation(prog, "uFogStart");
            locFogEnd_     = glGetUniformLocation(prog, "uFogEnd");
            locLightPos_   = glGetUniformLocation(prog, "uLightPos[0]");
            locLightColor_ = glGetUniformLocation(prog, "uLightColor[0]");
            locLightAtten_ = glGetUniformLocation(prog, "uLightAtten[0]");
            locFogColor_   = glGetUniformLocation(prog, "uFogColor");
            locTexture_    = glGetUniformLocation(prog, "uTexture");
            locAlphaTex_   = glGetUniformLocation(prog, "uAlphaTexture");
            locWindTime_   = glGetUniformLocation(prog, "uWindTime");
            locWindParams_ = glGetUniformLocation(prog, "uWindParams");
            locMeshYBounds_ = glGetUniformLocation(prog, "uMeshYBounds");
            locAttenLUT_   = glGetUniformLocation(prog, "uAttenLUT");
            locLUTScale_   = glGetUniformLocation(prog, "uLUTScale");
            locNumPointLights_ = glGetUniformLocation(prog, "uNumPointLights");
            locPlayerLightPos_   = glGetUniformLocation(prog, "uPlayerLightPos");
            locPlayerLightColor_ = glGetUniformLocation(prog, "uPlayerLightColor");
            locFireGlowLightPos_    = glGetUniformLocation(prog, "uFireGlowLightPos[0]");
            locFireGlowLightColor_  = glGetUniformLocation(prog, "uFireGlowLightColor[0]");
            locFireGlowLightRadius_ = glGetUniformLocation(prog, "uFireGlowLightRadius[0]");
            locFireGlowLightIntensity_ = glGetUniformLocation(prog, "uFireGlowLightIntensity[0]");
            locNumFireGlowLights_   = glGetUniformLocation(prog, "uNumFireGlowLights");
            locShaderDebug_         = glGetUniformLocation(prog, "uShaderDebug");
            locUnifiedLighting_     = glGetUniformLocation(prog, "uUnifiedLighting");
            LOG_DEBUG(MOD_SHADER, "WindShaderCallback: uniforms resolved — WVP={} World={} SunDir={} "
                      "LightPos={} LightColor={} Texture={} WindTime={} WindParams={} MeshYBounds={} "
                      "PlayerLightPos={} AttenLUT={} NumPointLights={} NumFireGlow={}",
                      locWVP_, locWorld_, locSunDir_, locLightPos_, locLightColor_, locTexture_,
                      locWindTime_, locWindParams_, locMeshYBounds_,
                      locPlayerLightPos_, locAttenLUT_, locNumPointLights_, locNumFireGlowLights_);
        }

        // Per-node uniforms
        irr::core::matrix4 worldViewProj = driver->getTransform(irr::video::ETS_PROJECTION);
        worldViewProj *= driver->getTransform(irr::video::ETS_VIEW);
        worldViewProj *= driver->getTransform(irr::video::ETS_WORLD);
        if (locWVP_ >= 0)
            glUniformMatrix4fv(locWVP_, 1, GL_FALSE, worldViewProj.pointer());

        irr::core::matrix4 world = driver->getTransform(irr::video::ETS_WORLD);
        if (locWorld_ >= 0)
            glUniformMatrix4fv(locWorld_, 1, GL_FALSE, world.pointer());

        // Per-node: mesh Y bounds (different per tree mesh)
        if (locMeshYBounds_ >= 0) {
            float bounds[2] = { meshMinY_, meshMaxY_ };
            glUniform2fv(locMeshYBounds_, 1, bounds);
        }

        // Per-frame uniforms
        if (lastFrameId_ != owner_->frameId()) {
            lastFrameId_ = owner_->frameId();

            if (!firstFrameLogged_) {
                firstFrameLogged_ = true;
                LOG_DEBUG(MOD_SHADER, "WindShaderCallback: first per-frame uniforms frame={} "
                          "sun=({:.2f},{:.2f},{:.2f}) sunColor=({:.2f},{:.2f},{:.2f}) "
                          "ambient=({:.2f},{:.2f},{:.2f}) tint=({:.2f},{:.2f},{:.2f}) "
                          "playerLightPos=({:.1f},{:.1f},{:.1f}) playerLightColor=({:.2f},{:.2f},{:.2f}) "
                          "windTime={:.2f} windParams=({:.3f},{:.3f},{:.3f},{:.3f}) "
                          "lutTex={} lutScale={:.4f} numLights={} numFireGlow={} fogStart={:.1f} fogEnd={:.1f}",
                          owner_->frameId(),
                          owner_->sunDir()[0], owner_->sunDir()[1], owner_->sunDir()[2],
                          owner_->sunColor()[0], owner_->sunColor()[1], owner_->sunColor()[2],
                          owner_->ambientColor()[0], owner_->ambientColor()[1], owner_->ambientColor()[2],
                          owner_->tintColor()[0], owner_->tintColor()[1], owner_->tintColor()[2],
                          owner_->playerLightPos()[0], owner_->playerLightPos()[1], owner_->playerLightPos()[2],
                          owner_->playerLightColor()[0], owner_->playerLightColor()[1], owner_->playerLightColor()[2],
                          owner_->windTime(),
                          owner_->windParams()[0], owner_->windParams()[1],
                          owner_->windParams()[2], owner_->windParams()[3],
                          owner_->attenLutTexture(), owner_->lutScale(),
                          owner_->numPointLights(), owner_->numFireGlowLights(),
                          owner_->fogStart(), owner_->fogEnd());
            } else {
                LOG_TRACE(MOD_SHADER, "WindShaderCallback: per-frame uniforms frame={} numLights={} numFireGlow={}",
                          owner_->frameId(), owner_->numPointLights(), owner_->numFireGlowLights());
            }

            if (locSunDir_ >= 0)     glUniform3fv(locSunDir_, 1, owner_->sunDir());
            if (locSunColor_ >= 0)   glUniform3fv(locSunColor_, 1, owner_->sunColor());
            if (locAmbient_ >= 0)    glUniform3fv(locAmbient_, 1, owner_->ambientColor());
            if (locTint_ >= 0)       glUniform3fv(locTint_, 1, owner_->tintColor());
            if (locFogStart_ >= 0)   glUniform1f(locFogStart_, owner_->fogStart());
            if (locFogEnd_ >= 0)     glUniform1f(locFogEnd_, owner_->fogEnd());
            if (locLightPos_ >= 0)   glUniform3fv(locLightPos_, ZoneShaderManager::MAX_POINT_LIGHTS, owner_->lightPos());
            if (locLightColor_ >= 0) glUniform3fv(locLightColor_, ZoneShaderManager::MAX_POINT_LIGHTS, owner_->lightColor());
            if (locLightAtten_ >= 0) glUniform3fv(locLightAtten_, ZoneShaderManager::MAX_POINT_LIGHTS, owner_->lightAtten());
            if (locFogColor_ >= 0)   glUniform4fv(locFogColor_, 1, owner_->fogColor());
            if (locTexture_ >= 0)    glUniform1i(locTexture_, 0);
            if (locAlphaTex_ >= 0)   glUniform1i(locAlphaTex_, 1);
            if (locWindTime_ >= 0)   glUniform1f(locWindTime_, owner_->windTime());
            if (locWindParams_ >= 0) glUniform4fv(locWindParams_, 1, owner_->windParams());
            // Per-pixel player light (FS uniforms + LUT texture)
            if (locPlayerLightPos_ >= 0)   glUniform3fv(locPlayerLightPos_, 1, owner_->playerLightPos());
            if (locPlayerLightColor_ >= 0) glUniform3fv(locPlayerLightColor_, 1, owner_->playerLightColor());
            if (locAttenLUT_ >= 0 && owner_->attenLutTexture() > 0) {
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, owner_->attenLutTexture());
                glActiveTexture(GL_TEXTURE0);
                glUniform1i(locAttenLUT_, 2);
            }
            if (locLUTScale_ >= 0) glUniform1f(locLUTScale_, owner_->lutScale());
            if (locNumPointLights_ >= 0) glUniform1i(locNumPointLights_, owner_->numPointLights());

            // Fire glow fog emitters
            if (locFireGlowLightPos_ >= 0)    glUniform3fv(locFireGlowLightPos_, ZoneShaderManager::MAX_FIRE_GLOW_LIGHTS, owner_->fireGlowLightPos());
            if (locFireGlowLightColor_ >= 0)  glUniform3fv(locFireGlowLightColor_, ZoneShaderManager::MAX_FIRE_GLOW_LIGHTS, owner_->fireGlowLightColor());
            if (locFireGlowLightRadius_ >= 0) glUniform1fv(locFireGlowLightRadius_, ZoneShaderManager::MAX_FIRE_GLOW_LIGHTS, owner_->fireGlowLightRadius());
            if (locFireGlowLightIntensity_ >= 0) glUniform1fv(locFireGlowLightIntensity_, ZoneShaderManager::MAX_FIRE_GLOW_LIGHTS, owner_->fireGlowLightIntensity());
            if (locNumFireGlowLights_ >= 0)   glUniform1i(locNumFireGlowLights_, owner_->numFireGlowLights());
            if (locShaderDebug_ >= 0)         glUniform1i(locShaderDebug_, owner_->shaderDebug());
            if (locUnifiedLighting_ >= 0)    glUniform1i(locUnifiedLighting_, owner_->unifiedLighting() ? 1 : 0);
        }

        // Per-draw-call GL state dump (first 5 calls only)
        if (drawCallCount_ < 5) {
            GLint boundTex0 = 0, boundTex2 = 0, activeUnit = 0, curProg = 0;
            glGetIntegerv(GL_ACTIVE_TEXTURE, &activeUnit);
            glGetIntegerv(GL_CURRENT_PROGRAM, &curProg);
            glActiveTexture(GL_TEXTURE0);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTex0);
            glActiveTexture(GL_TEXTURE2);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTex2);
            glActiveTexture(activeUnit);  // restore
            LOG_DEBUG(MOD_SHADER, "WindShaderCallback draw[{}]: prog={} tex0={} tex2(LUT)={} "
                      "meshY=[{:.2f},{:.2f}] debug={}",
                      drawCallCount_, curProg, boundTex0, boundTex2,
                      meshMinY_, meshMaxY_, owner_->shaderDebug());
        }
        ++drawCallCount_;
#else
        // Desktop GL path (WindShaderCallback)
        irr::core::matrix4 worldViewProj = driver->getTransform(irr::video::ETS_PROJECTION);
        worldViewProj *= driver->getTransform(irr::video::ETS_VIEW);
        worldViewProj *= driver->getTransform(irr::video::ETS_WORLD);
        services->setVertexShaderConstant("mWorldViewProj", worldViewProj.pointer(), 16);

        irr::core::matrix4 world = driver->getTransform(irr::video::ETS_WORLD);
        services->setVertexShaderConstant("mWorld", world.pointer(), 16);

        services->setVertexShaderConstant("uSunDir", owner_->sunDir(), 3);
        services->setVertexShaderConstant("uSunColor", owner_->sunColor(), 3);
        services->setVertexShaderConstant("uAmbientColor", owner_->ambientColor(), 3);
        services->setVertexShaderConstant("uTintColor", owner_->tintColor(), 3);

        float fogStart = owner_->fogStart();
        float fogEnd = owner_->fogEnd();
        services->setVertexShaderConstant("uFogStart", &fogStart, 1);
        services->setVertexShaderConstant("uFogEnd", &fogEnd, 1);

        services->setVertexShaderConstant("uLightPos[0]", owner_->lightPos(), ZoneShaderManager::MAX_POINT_LIGHTS * 3);
        services->setVertexShaderConstant("uLightColor[0]", owner_->lightColor(), ZoneShaderManager::MAX_POINT_LIGHTS * 3);
        services->setVertexShaderConstant("uLightAtten[0]", owner_->lightAtten(), ZoneShaderManager::MAX_POINT_LIGHTS * 3);

        float windTime = owner_->windTime();
        services->setVertexShaderConstant("uWindTime", &windTime, 1);
        services->setVertexShaderConstant("uWindParams", owner_->windParams(), 4);

        float meshBounds[2] = { meshMinY_, meshMaxY_ };
        services->setVertexShaderConstant("uMeshYBounds", meshBounds, 2);

        float fogColor[4];
        std::memcpy(fogColor, owner_->fogColor(), sizeof(fogColor));
        services->setPixelShaderConstant("uFogColor", fogColor, 4);

        irr::s32 texUnit = 0;
        services->setPixelShaderConstant("uTexture", &texUnit, 1);

        irr::s32 numLights = owner_->numPointLights();
        services->setVertexShaderConstant("uNumPointLights", &numLights, 1);

        // Fire glow fog emitters
        services->setVertexShaderConstant("uFireGlowLightPos[0]", owner_->fireGlowLightPos(), ZoneShaderManager::MAX_FIRE_GLOW_LIGHTS * 3);
        services->setVertexShaderConstant("uFireGlowLightColor[0]", owner_->fireGlowLightColor(), ZoneShaderManager::MAX_FIRE_GLOW_LIGHTS * 3);
        services->setVertexShaderConstant("uFireGlowLightRadius[0]", owner_->fireGlowLightRadius(), ZoneShaderManager::MAX_FIRE_GLOW_LIGHTS);
        services->setVertexShaderConstant("uFireGlowLightIntensity[0]", owner_->fireGlowLightIntensity(), ZoneShaderManager::MAX_FIRE_GLOW_LIGHTS);
        irr::s32 numFireGlowLights = owner_->numFireGlowLights();
        services->setVertexShaderConstant("uNumFireGlowLights", &numFireGlowLights, 1);
        irr::s32 unifiedLighting = owner_->unifiedLighting() ? 1 : 0;
        services->setVertexShaderConstant("uUnifiedLighting", &unifiedLighting, 1);
#endif
    }

private:
    ZoneShaderManager* owner_;
    float meshMinY_ = 0.0f;
    float meshMaxY_ = 1.0f;
#ifdef EQT_HAS_GLES2
    GLint locWVP_ = -2, locWorld_ = -2;
    GLint locSunDir_ = -2, locSunColor_ = -2, locAmbient_ = -2, locTint_ = -2;
    GLint locFogStart_ = -2, locFogEnd_ = -2;
    GLint locLightPos_ = -2, locLightColor_ = -2, locLightAtten_ = -2;
    GLint locFogColor_ = -2, locTexture_ = -2, locAlphaTex_ = -2;
    GLint locWindTime_ = -2, locWindParams_ = -2, locMeshYBounds_ = -2;
    GLint locAttenLUT_ = -2, locLUTScale_ = -2;
    GLint locNumPointLights_ = -2;
    GLint locPlayerLightPos_ = -2, locPlayerLightColor_ = -2;
    GLint locFireGlowLightPos_ = -2, locFireGlowLightColor_ = -2;
    GLint locFireGlowLightRadius_ = -2, locFireGlowLightIntensity_ = -2;
    GLint locNumFireGlowLights_ = -2;
    GLint locShaderDebug_ = -2;
    GLint locUnifiedLighting_ = -2;
    uint64_t lastFrameId_ = 0;
    bool firstFrameLogged_ = false;
    int drawCallCount_ = 0;
#endif
};

} // namespace Graphics
} // namespace EQT
