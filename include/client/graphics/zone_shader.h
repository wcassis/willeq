#ifndef EQT_GRAPHICS_ZONE_SHADER_H
#define EQT_GRAPHICS_ZONE_SHADER_H

#include <irrlicht.h>
#include <cstdint>
#include <cstring>
#include <vector>

namespace EQT {
namespace Graphics {

// ---------------------------------------------------------------------------
// T408 (board B110) -- the zone's baked per-vertex lighting.
//
// EQ bakes static light into the zone WLD's 0x36 colour array: 1,915 region
// meshes in qeynos2, one BGRA colour per vertex, min 0 / max 255, mean RGB
// byte 44.4/255 (tests/test_baked_vertex_lighting.cpp re-takes that census
// every ctest run). Until T408 `parseFragment36` stepped over the array and
// every zone vertex reached the GPU as opaque white, which is why fable's
// night had no lamp pools while the official client's did.
//
// The three constants below are the composition, SOLVED LIVE against the
// official Titanium client at two spots and two hours on the pinned T405 rig
// -- never argued. The probe that produced them is the `/bakedlight` slash
// command and the table in refactor/T408_baked_vertex_lighting.md §Review.
// A change to any of them moves every zone pixel golden.
// ---------------------------------------------------------------------------
enum BakedLightMode {
    kBakedLightOff        = 0,  // pre-T408: the baked colours are ignored
    kBakedLightAdditive   = 1,  // ambient*tint + gain*baked
    kBakedLightMultiply   = 2,  // ambient*tint*baked
    kBakedLightMax        = 3   // max(ambient*tint, gain*baked)
};

// THE SOLVE REACHED ITS STOP, AND THE COMMITTED COMPOSITION IS THEREFORE
// `kBakedLightOff` -- NOT because the baked colours are unavailable (they are
// loaded, and this build ships them in every zone vertex) but because the
// live A/B REFUTED the premise that they are what separates fable from the
// official client at these spots. Measured T408 on two spots x two hours x
// two clients, weather and heading pinned, on a GEOMETRY-ONLY crop
// (560x110+200+350 -- T361's 700x420+40+40 world crop is about half sky, and
// no zone-lighting term touches the sky):
//
//   L = (day/night at the lamp-free spot) / (day/night at the lamp spot)
//     = how much more a lamp is worth at night than by day, with each spot's
//       own composition divided out. It is invariant to a UNIFORM error in a
//       client's night ambient, which is exactly the error board B280 records.
//
//   oracle (two sessions, agreeing to 0.5%)     L = 2.281
//   fable, baked light OFF (pre-T408)           L = 2.476   (+8.5%)
//   fable, additive  gain 0.25 / 0.50 / 1.00    L = 2.826 / 3.096 / 3.622
//   fable, multiplicative                       L = 0.655
//   fable, max       gain 0.50 / 1.00           L = 3.461 / 4.683
//
// Every baked composition moves fable AWAY from the oracle, and gain 0 is the
// best fit in the family. The reason is in the residual's SHAPE: to reproduce
// the oracle's own day/night ratio fable's night needs x2.27 at the lamp-free
// spot and x2.09 at the lamp spot -- the SAME factor at both, i.e. a UNIFORM
// deficit. A baked-vertex-light term is lamp-shaped by construction (at gain
// 1.0 it is worth x1.03 in the open and x2.48 next to a lamp), so it cannot
// supply a uniform deficit without breaking the spot it already fits. That
// uniform deficit is board B280 (the night ambient), not B110.
//
// So these constants may not be moved off Off by argument. The probe that
// produced the table above is `/bakedlight <mode> [gain] [skyWeight]`; re-run
// it after B280 is solved, because moving the night ambient moves every row.
static constexpr int   kBakedLightMode      = kBakedLightOff;
static constexpr float kBakedLightGain      = 0.0f;
static constexpr float kBakedLightSkyWeight = 0.0f;

// GLSL shader pipeline for combined fog, directional lighting, point lights,
// and day/night tinting.
// Targets OpenGL 2.1 / GLSL 1.20 (Mali 400 Lima driver on Orange Pi One).
class ZoneShaderManager {
public:
    static constexpr int MAX_POINT_LIGHTS = 8;
    static constexpr int MAX_FIRE_GLOW_LIGHTS = 8;

    ZoneShaderManager(irr::video::IVideoDriver* driver,
                      irr::video::IGPUProgrammingServices* gpu);
    ~ZoneShaderManager();

    bool isAvailable() const { return available_; }

    irr::s32 getMaterialTypeSolid() const { return materialSolid_; }
    irr::s32 getMaterialTypeAlphaTest() const { return materialAlphaTest_; }

    // H02d (Option A): alpha-test variant that reads alpha from the main
    // texture's own alpha channel (uTexture.a) instead of the ETC1 companion
    // texture on unit 1. Used for masked textures on the direct RGBA/DDS
    // decode path, which never builds a companion texture. -1 if compilation
    // failed (callers fall back to fixed-function EMT_TRANSPARENT_ALPHA_CHANNEL_REF,
    // which has always read the main texture's alpha directly and is
    // unaffected by this).
    irr::s32 getMaterialTypeAlphaTestRGBA() const { return materialAlphaTestRGBA_; }
    bool isAlphaTestRGBAAvailable() const { return materialAlphaTestRGBA_ >= 0; }
    // P01: lightweight counterpart, needed to classify draws by material
    // family in the zone benchmark's --material-counts diagnostic.
    irr::s32 getMaterialTypeLWAlphaTestRGBA() const { return materialLWAlphaTestRGBA_; }

    // Atlas-specific material types (per-pixel lighting, atlas UV transform)
    irr::s32 getMaterialTypeAtlasSolid() const { return materialAtlasSolid_; }
    irr::s32 getMaterialTypeAtlasAlpha() const { return materialAtlasAlpha_; }
    bool isAtlasAvailable() const { return materialAtlasSolid_ >= 0; }

    // Wind material types (vertex shader wind displacement for trees)
    irr::s32 getMaterialTypeWindAlphaTest() const { return materialWindAlphaTest_; }
    bool isWindAvailable() const { return materialWindAlphaTest_ >= 0; }
    // H02d (Option A) wind counterpart: reads uTexture.a directly for masked
    // tree foliage on the direct RGBA/DDS decode path (no ETC1 companion
    // texture exists). Mirrors getMaterialTypeAlphaTestRGBA() but with wind
    // vertex displacement. -1 if compilation failed (callers fall back to
    // the base wind alpha-test variant, matching the non-wind RGBA fallback
    // pattern).
    bool isWindAlphaTestRGBAAvailable() const { return materialWindAlphaTestRGBA_ >= 0; }
    // H02d (Option A): wind variant for NON-masked tree materials (e.g. tree
    // trunk textures that carry no alpha), so wind vertex displacement still
    // applies without dragging in alpha-test discard semantics. -1 if
    // compilation failed (callers fall back to the base wind alpha-test
    // variant, matching the other wind-family fallbacks).
    bool isWindSolidAvailable() const { return materialWindSolid_ >= 0; }

    // Lightweight material types (no per-pixel player light, 3 varyings)
    irr::s32 getMaterialTypeLWSolid() const { return materialLWSolid_; }
    irr::s32 getMaterialTypeLWAlphaTest() const { return materialLWAlphaTest_; }
    irr::s32 getMaterialTypeLWAtlasSolid() const { return materialLWAtlasSolid_; }
    irr::s32 getMaterialTypeLWAtlasAlpha() const { return materialLWAtlasAlpha_; }
    irr::s32 getMaterialTypeLWWindAlphaTest() const { return materialLWWindAlphaTest_; }
    bool isLightweightAvailable() const { return materialLWSolid_ >= 0; }

    // Active material resolution — returns lightweight or per-pixel variant
    irr::s32 getActiveSolid() const;
    irr::s32 getActiveAlphaTest() const;
    irr::s32 getActiveAlphaTestRGBA() const;  // H02d: RGBA main-texture-alpha variant
    irr::s32 getActiveAtlasSolid() const;
    irr::s32 getActiveAtlasAlpha() const;
    irr::s32 getActiveWindAlphaTest() const;
    irr::s32 getActiveWindAlphaTestRGBA() const;  // H02d: wind RGBA main-texture-alpha variant
    irr::s32 getActiveWindSolid() const;  // H02d: wind variant for non-masked tree materials
    void setPerPixelPlayerLight(bool enabled);
    bool isPerPixelPlayerLight() const { return perPixelPlayerLight_; }

    // Shader debug mode (0=normal, 1=texColor, 2=vColor, 3=LUT, 4=pLight,
    // 5=worldNormal, 6=lightDir, 7=alpha, 8=texColor*vColor no pLight,
    // 9=UV, 10=depth, 11=rawTexAlpha, 12=companionAlpha (H02d))
    void setShaderDebug(int mode) { shaderDebug_ = mode; }
    int shaderDebug() const { return shaderDebug_; }

    // Unified lighting mode: multiply pointLighting by aColor instead of additive
    void setUnifiedLighting(bool enabled) { unifiedLighting_ = enabled; }
    bool unifiedLighting() const { return unifiedLighting_; }

    // T408 (B110): the zone's baked per-vertex lighting.
    //   [0] gain      -- how much of the WLD's baked colour is light
    //   [1] skyWeight -- how much the baked alpha byte masks the sun term
    //   [2] mode      -- 0 off (pre-T408), 1 additive, 2 multiplicative, 3 max
    // These are solved LIVE against the official client (T408 Review); the
    // committed defaults are kBakedLight* below and are pinned by
    // tests/test_baked_vertex_lighting.cpp. The setter exists so one live
    // session can sweep the family via /bakedlight instead of costing a
    // rebuild per candidate -- the probe that produced the constants.
    void setBakedLight(float gain, float skyWeight, float mode) {
        bakedLight_[0] = gain; bakedLight_[1] = skyWeight; bakedLight_[2] = mode;
    }
    const float* bakedLight() const { return bakedLight_; }

    // Debug shader program (separate FS with visualization branches)
    irr::s32 getDebugMaterialType() const { return materialDebug_; }
    bool isDebugShaderAvailable() const { return materialDebug_ >= 0; }

    // Wind uniform state (updated per frame by renderer)
    void setWindTime(float t) { windTime_ = t; }
    float windTime() const { return windTime_; }
    void setWindParams(float baseStrength, float baseFreq, float gustStrength, float gustFreq) {
        windParams_[0] = baseStrength; windParams_[1] = baseFreq;
        windParams_[2] = gustStrength; windParams_[3] = gustFreq;
    }
    const float* windParams() const { return windParams_; }

    // Update uniform values (call once per frame before rendering)
    void setFog(float fogStart, float fogEnd, float r, float g, float b, float a);
    void setSunDirection(float x, float y, float z);
    void setSunColor(float r, float g, float b);
    void setAmbientColor(float r, float g, float b);
    void setTintColor(float r, float g, float b);

    // Call once per frame before drawAll() to advance frame counter
    // Also flushes attenuation LUT if dirty (GLES2)
    void beginFrame();
    uint64_t frameId() const { return frameId_; }

    // Attenuation LUT texture (GLES2 per-pixel player light optimization)
    uint32_t attenLutTexture() const { return attenLutTexture_; }
    float lutScale() const { return lutScale_; }

    // Camera position for atlas per-pixel lighting (Irrlicht world space, Y-up)
    void setCameraPos(float x, float y, float z);

    // Set point light data directly (called from updateObjectLights)
    // Positions are in Irrlicht world space (Y-up)
    void setPointLight(int index, float px, float py, float pz,
                       float cr, float cg, float cb,
                       float attenConst, float attenLinear, float attenQuad) {
        if (index < 0 || index >= MAX_POINT_LIGHTS) return;
        int i3 = index * 3;
        lightPos_[i3] = px; lightPos_[i3+1] = py; lightPos_[i3+2] = pz;
        lightColor_[i3] = cr; lightColor_[i3+1] = cg; lightColor_[i3+2] = cb;
        lightAtten_[i3] = attenConst; lightAtten_[i3+1] = attenLinear; lightAtten_[i3+2] = attenQuad;
    }
    void setNumPointLights(int count) {
        numPointLights_ = (count < 0) ? 0 : (count > MAX_POINT_LIGHTS ? MAX_POINT_LIGHTS : count);
    }

    // Accessors for current uniform state (for callback)
    float fogStart() const { return fogStart_; }
    float fogEnd() const { return fogEnd_; }
    const float* fogColor() const { return fogColor_; }
    const float* sunDir() const { return sunDir_; }
    const float* sunColor() const { return sunColor_; }
    const float* ambientColor() const { return ambientColor_; }
    const float* tintColor() const { return tintColor_; }
    int numPointLights() const { return numPointLights_; }
    const float* lightPos() const { return lightPos_; }
    const float* lightColor() const { return lightColor_; }
    const float* lightAtten() const { return lightAtten_; }
    const float* cameraPos() const { return cameraPos_; }

    // Per-pixel player light (FS uniforms, GLES2 only)
    void setPlayerLightPos(float x, float y, float z) {
        playerLightPos_[0] = x; playerLightPos_[1] = y; playerLightPos_[2] = z;
    }
    void setPlayerLightColor(float r, float g, float b) {
        playerLightColor_[0] = r; playerLightColor_[1] = g; playerLightColor_[2] = b;
    }
    void setPlayerLightAtten(float c, float l, float q) {
        playerLightAtten_[0] = c; playerLightAtten_[1] = l; playerLightAtten_[2] = q;
        // Mark LUT dirty if attenuation params changed (all three: since B968
        // the LUT carries the linear term too)
        if (c != lutBakedAtten_[0] || l != lutBakedAtten_[1] || q != lutBakedAtten_[2]) lutDirty_ = true;
    }
    const float* playerLightPos() const { return playerLightPos_; }
    const float* playerLightColor() const { return playerLightColor_; }
    const float* playerLightAtten() const { return playerLightAtten_; }

    // Fire glow lighting (per-vertex warm glow from fire sources)
    void setFireGlowLight(int index, float px, float py, float pz,
                          float cr, float cg, float cb,
                          float radius, float intensity);
    void setNumFireGlowLights(int count);
    void clearFireGlowLights();
    int numFireGlowLights() const { return numFireGlowLights_; }
    const float* fireGlowLightPos() const { return fireGlowLightPos_; }
    const float* fireGlowLightColor() const { return fireGlowLightColor_; }
    const float* fireGlowLightRadius() const { return fireGlowLightRadius_; }
    const float* fireGlowLightIntensity() const { return fireGlowLightIntensity_; }

    // B494 (#169): the atlas page texture handles used to be pushed here by the
    // (now-deleted) zone-atlas object manager via setAtlasPageTextures()/
    // appendAtlasPageTextures(); with those gone nothing writes atlasPageTextures_
    // anymore, so getAtlasPageTexture() always returns 0. Kept (as a FOLLOW-UP
    // residue with the atlas shader callback) until the AtlasShaderCallback and
    // the driver's Atlas*3D programs are retired.
    uint32_t getAtlasPageTexture(uint16_t pageIndex) const {
        if (pageIndex < atlasPageTextures_.size()) return atlasPageTextures_[pageIndex];
        return 0;
    }

private:
    bool available_ = false;
    irr::s32 materialSolid_ = -1;
    irr::s32 materialAlphaTest_ = -1;
    irr::s32 materialAlphaTestRGBA_ = -1;  // H02d: main-texture-alpha variant (heavy/per-pixel)
    irr::s32 materialAtlasSolid_ = -1;
    irr::s32 materialAtlasAlpha_ = -1;
    irr::s32 materialWindAlphaTest_ = -1;
    irr::s32 materialWindAlphaTestRGBA_ = -1;  // H02d: wind main-texture-alpha variant (heavy/per-pixel)
    irr::s32 materialWindSolid_ = -1;  // H02d: wind variant for non-masked tree materials (heavy/per-pixel)

    // Lightweight material types (no per-pixel player light, 3 varyings)
    irr::s32 materialLWSolid_ = -1;
    irr::s32 materialLWAlphaTest_ = -1;
    irr::s32 materialLWAlphaTestRGBA_ = -1;  // H02d: main-texture-alpha variant (lightweight)
    irr::s32 materialLWAtlasSolid_ = -1;
    irr::s32 materialLWAtlasAlpha_ = -1;
    irr::s32 materialLWWindAlphaTest_ = -1;
    irr::s32 materialLWWindAlphaTestRGBA_ = -1;  // H02d: wind main-texture-alpha variant (lightweight)
    irr::s32 materialLWWindSolid_ = -1;  // H02d: wind variant for non-masked tree materials (lightweight)
    irr::s32 materialDebug_ = -1;  // Debug visualization program (expensive, only used with /shaderdebug)
    bool perPixelPlayerLight_ = false;  // Default OFF (lightweight)

    // Uniform values (updated per frame)
    float fogStart_ = 200.0f;
    float fogEnd_ = 300.0f;
    float fogColor_[4] = {0.5f, 0.5f, 0.63f, 1.0f};
    float sunDir_[3] = {0.5f, -1.0f, 0.5f};
    float sunColor_[3] = {1.0f, 1.0f, 0.9f};
    float ambientColor_[3] = {0.5f, 0.5f, 0.5f};
    float tintColor_[3] = {1.0f, 1.0f, 1.0f};

    // Point light data (set directly from updateObjectLights)
    int numPointLights_ = 0;
    float lightPos_[MAX_POINT_LIGHTS * 3] = {};
    float lightColor_[MAX_POINT_LIGHTS * 3] = {};
    float lightAtten_[MAX_POINT_LIGHTS * 3] = {};

    // Camera position (for atlas per-pixel lighting)
    float cameraPos_[3] = {0.0f, 0.0f, 0.0f};

    // Per-pixel player light FS uniforms (GLES2)
    float playerLightPos_[3] = {0.0f, 0.0f, 0.0f};
    float playerLightColor_[3] = {0.0f, 0.0f, 0.0f};
    float playerLightAtten_[3] = {1.0f, 0.0f, 0.0f};

    // Fire glow lighting
    int numFireGlowLights_ = 0;
    float fireGlowLightPos_[MAX_FIRE_GLOW_LIGHTS * 3] = {};
    float fireGlowLightColor_[MAX_FIRE_GLOW_LIGHTS * 3] = {};
    float fireGlowLightRadius_[MAX_FIRE_GLOW_LIGHTS] = {};
    float fireGlowLightIntensity_[MAX_FIRE_GLOW_LIGHTS] = {};

    // Shader debug mode
    int shaderDebug_ = 0;
    bool unifiedLighting_ = false;
    // T408: solved live against the official Titanium client -- see the
    // composition table in refactor/T408_baked_vertex_lighting.md §Review.
    float bakedLight_[3] = { kBakedLightGain, kBakedLightSkyWeight,
                             static_cast<float>(kBakedLightMode) };

    // Wind uniforms (for tree vertex shader animation)
    float windTime_ = 0.0f;
    float windParams_[4] = {0.3f, 0.4f, 0.5f, 0.1f}; // baseStr, baseFreq, gustStr, gustFreq

    // Frame counter for per-frame uniform skip optimization
    uint64_t frameId_ = 0;

    // Attenuation LUT for per-pixel player light (GLES2)
    // (B968: indexed by d / lutRange_; uLUTScale = 64 / lutRange_ because the
    // FS forms d / 64 -- include/client/graphics/player_light_shader.h)
    uint32_t attenLutTexture_ = 0;
    float lutScale_ = 64.0f / 4096.0f;
    float lutRange_ = 4096.0f;
    float lutBakedAtten_[3] = {-1.0f, -1.0f, -1.0f};
    bool lutDirty_ = true;

    // Atlas page GL texture handles (set by renderer, used by shader callback)
    std::vector<uint32_t> atlasPageTextures_;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_ZONE_SHADER_H
