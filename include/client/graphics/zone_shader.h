#ifndef EQT_GRAPHICS_ZONE_SHADER_H
#define EQT_GRAPHICS_ZONE_SHADER_H

#include <irrlicht.h>
#include <cstdint>
#include <cstring>
#include <vector>

namespace EQT {
namespace Graphics {

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

    // Atlas-specific material types (per-pixel lighting, atlas UV transform)
    irr::s32 getMaterialTypeAtlasSolid() const { return materialAtlasSolid_; }
    irr::s32 getMaterialTypeAtlasAlpha() const { return materialAtlasAlpha_; }
    bool isAtlasAvailable() const { return materialAtlasSolid_ >= 0; }

    // Wind material types (vertex shader wind displacement for trees)
    irr::s32 getMaterialTypeWindAlphaTest() const { return materialWindAlphaTest_; }
    bool isWindAvailable() const { return materialWindAlphaTest_ >= 0; }

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
    irr::s32 getActiveAtlasSolid() const;
    irr::s32 getActiveAtlasAlpha() const;
    irr::s32 getActiveWindAlphaTest() const;
    void setPerPixelPlayerLight(bool enabled);
    bool isPerPixelPlayerLight() const { return perPixelPlayerLight_; }

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
    void clearPointLights() {
        numPointLights_ = 0;
        std::memset(lightPos_, 0, sizeof(lightPos_));
        std::memset(lightColor_, 0, sizeof(lightColor_));
        // Set default attenuation to (1,0,0) so inactive lights have atten=1/(1+epsilon)
        // rather than 1/(0+epsilon)=10000 which wastes precision
        for (int i = 0; i < MAX_POINT_LIGHTS; ++i) {
            lightAtten_[i * 3] = 1.0f;
            lightAtten_[i * 3 + 1] = 0.0f;
            lightAtten_[i * 3 + 2] = 0.0f;
        }
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
        // Mark LUT dirty if attenuation params changed
        if (c != lutBakedConst_ || q != lutBakedQuad_) lutDirty_ = true;
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

    // Atlas tile scale (TILE_INNER / ATLAS_WIDTH, e.g. 248.0/2048.0)
    void setAtlasTileScale(float scale) { atlasTileScale_ = scale; }
    float atlasTileScale() const { return atlasTileScale_; }

    // Set atlas page texture handles for the shader callback to bind
    void setAtlasPageTextures(const std::vector<uint32_t>& pageTextures) {
        atlasPageTextures_ = pageTextures;
    }
    // Append additional atlas page textures (e.g. object atlas after zone atlas).
    // Returns the starting offset for the appended pages.
    int appendAtlasPageTextures(const std::vector<uint32_t>& pageTextures) {
        int offset = static_cast<int>(atlasPageTextures_.size());
        atlasPageTextures_.insert(atlasPageTextures_.end(), pageTextures.begin(), pageTextures.end());
        return offset;
    }
    uint32_t getAtlasPageTexture(uint16_t pageIndex) const {
        if (pageIndex < atlasPageTextures_.size()) return atlasPageTextures_[pageIndex];
        return 0;
    }

private:
    bool available_ = false;
    irr::s32 materialSolid_ = -1;
    irr::s32 materialAlphaTest_ = -1;
    irr::s32 materialAtlasSolid_ = -1;
    irr::s32 materialAtlasAlpha_ = -1;
    irr::s32 materialWindAlphaTest_ = -1;

    // Lightweight material types (no per-pixel player light, 3 varyings)
    irr::s32 materialLWSolid_ = -1;
    irr::s32 materialLWAlphaTest_ = -1;
    irr::s32 materialLWAtlasSolid_ = -1;
    irr::s32 materialLWAtlasAlpha_ = -1;
    irr::s32 materialLWWindAlphaTest_ = -1;
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

    // Atlas tile scale uniform
    float atlasTileScale_ = 248.0f / 2048.0f;

    // Wind uniforms (for tree vertex shader animation)
    float windTime_ = 0.0f;
    float windParams_[4] = {0.3f, 0.4f, 0.5f, 0.1f}; // baseStr, baseFreq, gustStr, gustFreq

    // Frame counter for per-frame uniform skip optimization
    uint64_t frameId_ = 0;

    // Attenuation LUT for per-pixel player light (GLES2)
    uint32_t attenLutTexture_ = 0;
    float lutScale_ = 1.0f / 4096.0f;
    float lutMaxD2_ = 4096.0f;
    float lutBakedConst_ = -1.0f;
    float lutBakedQuad_ = -1.0f;
    bool lutDirty_ = true;

    // Atlas page GL texture handles (set by renderer, used by shader callback)
    std::vector<uint32_t> atlasPageTextures_;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_ZONE_SHADER_H
