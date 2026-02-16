#ifndef EQT_GRAPHICS_ZONE_SHADER_H
#define EQT_GRAPHICS_ZONE_SHADER_H

#include <irrlicht.h>

namespace EQT {
namespace Graphics {

// GLSL shader pipeline for combined fog, directional lighting, and day/night tinting.
// Targets OpenGL 2.1 / GLSL 1.20 (Mali 400 Lima driver on Orange Pi One).
// Replaces fixed-function fog, ambient/sun lighting, and per-material state changes
// with a single vertex+fragment shader pair.
class ZoneShaderManager {
public:
    ZoneShaderManager(irr::video::IVideoDriver* driver,
                      irr::video::IGPUProgrammingServices* gpu);
    ~ZoneShaderManager() = default;

    // Returns true if shaders compiled and are ready to use
    bool isAvailable() const { return available_; }

    // Material type IDs for use with mesh buffers
    irr::s32 getMaterialTypeSolid() const { return materialSolid_; }
    irr::s32 getMaterialTypeAlphaTest() const { return materialAlphaTest_; }

    // Update uniform values (call once per frame before rendering)
    void setFog(float fogStart, float fogEnd, float r, float g, float b, float a);
    void setSunDirection(float x, float y, float z);
    void setSunColor(float r, float g, float b);
    void setAmbientColor(float r, float g, float b);
    void setTintColor(float r, float g, float b);

    // Accessors for current uniform state (for callback)
    float fogStart() const { return fogStart_; }
    float fogEnd() const { return fogEnd_; }
    const float* fogColor() const { return fogColor_; }
    const float* sunDir() const { return sunDir_; }
    const float* sunColor() const { return sunColor_; }
    const float* ambientColor() const { return ambientColor_; }
    const float* tintColor() const { return tintColor_; }

private:
    bool available_ = false;
    irr::s32 materialSolid_ = -1;
    irr::s32 materialAlphaTest_ = -1;

    // Uniform values (updated per frame)
    float fogStart_ = 200.0f;
    float fogEnd_ = 300.0f;
    float fogColor_[4] = {0.5f, 0.5f, 0.63f, 1.0f};
    float sunDir_[3] = {0.5f, -1.0f, 0.5f};   // direction TO light (negated in shader)
    float sunColor_[3] = {1.0f, 1.0f, 0.9f};
    float ambientColor_[3] = {0.5f, 0.5f, 0.5f};
    float tintColor_[3] = {1.0f, 1.0f, 1.0f};
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_ZONE_SHADER_H
