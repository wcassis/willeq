#ifndef EQT_GRAPHICS_ZONE_SHADER_H
#define EQT_GRAPHICS_ZONE_SHADER_H

#include <irrlicht.h>
#include <cstring>

namespace EQT {
namespace Graphics {

// GLSL shader pipeline for combined fog, directional lighting, point lights,
// and day/night tinting.
// Targets OpenGL 2.1 / GLSL 1.20 (Mali 400 Lima driver on Orange Pi One).
class ZoneShaderManager {
public:
    static constexpr int MAX_POINT_LIGHTS = 8;

    ZoneShaderManager(irr::video::IVideoDriver* driver,
                      irr::video::IGPUProgrammingServices* gpu);
    ~ZoneShaderManager() = default;

    bool isAvailable() const { return available_; }

    irr::s32 getMaterialTypeSolid() const { return materialSolid_; }
    irr::s32 getMaterialTypeAlphaTest() const { return materialAlphaTest_; }

    // Update uniform values (call once per frame before rendering)
    void setFog(float fogStart, float fogEnd, float r, float g, float b, float a);
    void setSunDirection(float x, float y, float z);
    void setSunColor(float r, float g, float b);
    void setAmbientColor(float r, float g, float b);
    void setTintColor(float r, float g, float b);

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

private:
    bool available_ = false;
    irr::s32 materialSolid_ = -1;
    irr::s32 materialAlphaTest_ = -1;
    // Note: driver_ removed - point light data is fed directly via setPointLight()

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
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_ZONE_SHADER_H
