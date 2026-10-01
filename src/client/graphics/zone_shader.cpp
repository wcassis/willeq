#include "client/graphics/zone_shader.h"
#include "common/logging.h"
#include "client/core/gem_owner_census.h"  // B962
#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#elif defined(EQT_HAS_DRM)
#include <GL/gl.h>
#endif
#include <cstring>
#include "client/graphics/zone_shader_sources_gles2.h"
// T326: the companion desktop-GL sources header was deleted here. Its whole
// body sat behind a build configuration P30/P32 retired, so no supported
// build ever parsed it. The preprocessor gates below are left exactly as
// they were -- collapsing them would change behaviour in that retired
// configuration. Recover with:
//   git show bcbf2ddc:src/client/graphics/zone_shader_sources_gl.h
#include "client/graphics/zone_shader_callbacks.h"

namespace EQT {
namespace Graphics {

// ============================================================================
// FP16 attenuation LUT helpers (GLES2)
// ============================================================================
#ifdef EQT_HAS_GLES2

static uint16_t floatToHalf(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    uint32_t sign = (bits >> 16) & 0x8000;
    int32_t exp = ((bits >> 23) & 0xFF) - 127;
    uint32_t mant = bits & 0x7FFFFF;
    if (exp > 15) return sign | 0x7C00;      // Inf
    if (exp < -14) return sign;                // Zero (flush denormals)
    return sign | ((exp + 15) << 10) | (mant >> 13);
}

// Create or update the 256x1 FP16 LUMINANCE_ALPHA attenuation LUT.
// L = inversesqrt(d² + eps), A = 1/(c + q*d² + eps)
static void fillAttenLUT(uint16_t* data, float attenConst, float attenQuad, float maxD2) {
    const float eps = 0.0001f;
    for (int i = 0; i < 256; ++i) {
        float d2 = maxD2 * (static_cast<float>(i) + 0.5f) / 256.0f;
        float invD = 1.0f / std::sqrt(d2 + eps);
        float atten = 1.0f / (attenConst + attenQuad * d2 + eps);
        data[i * 2]     = floatToHalf(invD);
        data[i * 2 + 1] = floatToHalf(atten);
    }
}

#endif // EQT_HAS_GLES2

// ============================================================================
// ZoneShaderManager constructor
// ============================================================================

ZoneShaderManager::ZoneShaderManager(irr::video::IVideoDriver* driver,
                                     irr::video::IGPUProgrammingServices* gpu) {
    if (!driver || !gpu) {
        LOG_WARN(MOD_GRAPHICS, "ZoneShaderManager: No GPU programming services available");
        return;
    }

    // Check for shader support
    if (!driver->queryFeature(irr::video::EVDF_VERTEX_SHADER_1_1) ||
        !driver->queryFeature(irr::video::EVDF_PIXEL_SHADER_1_1)) {
        LOG_WARN(MOD_GRAPHICS, "ZoneShaderManager: Vertex/pixel shaders not supported");
        return;
    }

#ifdef EQT_HAS_GLES2
    // Constant-threshold alpha shaders on every GLES2 build. B958 measured WHY
    // the fwidth() variants cannot be used: fwidth() itself is correct on Lima,
    // but Lima runs a discard whose comparison operand depends on a derivative
    // INVERTED (opaque texels dropped, transparent ones drawn) --
    // refactor/B958/README.md, tools/hwcaps/gles2_alpha_test_probe.cpp. The
    // driver's built-in AlphaTest3D uses the same constant threshold since B958.
    const char* fragmentAlphaSrc = FRAGMENT_SHADER_ALPHA_SRC_BASE;
    const char* atlasFragmentAlphaSrc = ATLAS_FRAGMENT_SHADER_ALPHA_SRC_BASE;
    const char* fragmentAlphaRGBASrc = FRAGMENT_SHADER_ALPHA_RGBA_SRC;
#else
    const char* fragmentAlphaSrc = FRAGMENT_SHADER_ALPHA_SRC;
    const char* atlasFragmentAlphaSrc = ATLAS_FRAGMENT_SHADER_ALPHA_SRC;
    // Desktop's alpha-test shader already reads the main texture's own alpha
    // channel directly (no companion texture involved) — semantically
    // identical to the RGBA variant, so reuse it rather than duplicating it.
    const char* fragmentAlphaRGBASrc = FRAGMENT_SHADER_ALPHA_SRC;
#endif

    // Separate callbacks per material — each caches its own GL program's uniform locations
    ShaderCallback* solidCallback = new ShaderCallback(this);

    // Create solid material type
    materialSolid_ = gpu->addHighLevelShaderMaterial(
        VERTEX_SHADER_SRC, "main", irr::video::EVST_VS_1_1,
        FRAGMENT_SHADER_SOLID_SRC, "main", irr::video::EPST_PS_1_1,
        solidCallback,
        irr::video::EMT_SOLID);
    solidCallback->drop();

    if (materialSolid_ < 0) {
        LOG_ERROR(MOD_GRAPHICS, "ZoneShaderManager: Failed to compile solid shader");
        return;
    }

    // Create alpha-test material type (separate callback for its own location cache)
    ShaderCallback* alphaCallback = new ShaderCallback(this);
    materialAlphaTest_ = gpu->addHighLevelShaderMaterial(
        VERTEX_SHADER_SRC, "main", irr::video::EVST_VS_1_1,
        fragmentAlphaSrc, "main", irr::video::EPST_PS_1_1,
        alphaCallback,
        irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF);
    alphaCallback->drop();

    if (materialAlphaTest_ < 0) {
        LOG_ERROR(MOD_GRAPHICS, "ZoneShaderManager: Failed to compile alpha-test shader");
        materialSolid_ = -1;
        return;
    }
    available_ = true;
    LOG_INFO(MOD_GRAPHICS, "ZoneShaderManager: GLSL shaders compiled (solid={}, alphaTest={})",
             materialSolid_, materialAlphaTest_);

    // Create RGBA alpha-test material type (H02d, Option A): reads uTexture.a
    // directly, for masked textures on the direct RGBA/DDS decode path (no
    // ETC1 companion texture exists or is needed). Non-fatal if it fails to
    // compile — callers fall back to fixed-function EMT_TRANSPARENT_ALPHA_CHANNEL_REF.
    ShaderCallback* alphaRGBACallback = new ShaderCallback(this);
    materialAlphaTestRGBA_ = gpu->addHighLevelShaderMaterial(
        VERTEX_SHADER_SRC, "main", irr::video::EVST_VS_1_1,
        fragmentAlphaRGBASrc, "main", irr::video::EPST_PS_1_1,
        alphaRGBACallback,
        irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF);
    alphaRGBACallback->drop();

    if (materialAlphaTestRGBA_ < 0) {
        LOG_WARN(MOD_GRAPHICS, "ZoneShaderManager: Failed to compile RGBA alpha-test shader");
    } else {
        LOG_INFO(MOD_GRAPHICS, "ZoneShaderManager: RGBA alpha-test shader compiled (alphaTestRGBA={})",
                 materialAlphaTestRGBA_);
    }

    // Compile atlas shaders (separate callbacks for solid and alpha)
    AtlasShaderCallback* atlasSolidCallback = new AtlasShaderCallback(this, false);
    materialAtlasSolid_ = gpu->addHighLevelShaderMaterial(
        ATLAS_VERTEX_SHADER_SRC, "main", irr::video::EVST_VS_1_1,
        ATLAS_FRAGMENT_SHADER_SOLID_SRC, "main", irr::video::EPST_PS_1_1,
        atlasSolidCallback,
        irr::video::EMT_SOLID);
    atlasSolidCallback->drop();

    if (materialAtlasSolid_ < 0) {
        LOG_WARN(MOD_GRAPHICS, "ZoneShaderManager: Failed to compile atlas solid shader");
    } else {
        AtlasShaderCallback* atlasAlphaCallback = new AtlasShaderCallback(this, true);
        materialAtlasAlpha_ = gpu->addHighLevelShaderMaterial(
            ATLAS_VERTEX_SHADER_SRC, "main", irr::video::EVST_VS_1_1,
            atlasFragmentAlphaSrc, "main", irr::video::EPST_PS_1_1,
            atlasAlphaCallback,
            irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF);
        atlasAlphaCallback->drop();

        if (materialAtlasAlpha_ < 0) {
            LOG_WARN(MOD_GRAPHICS, "ZoneShaderManager: Failed to compile atlas alpha shader");
            materialAtlasSolid_ = -1;  // Disable atlas shaders entirely
        } else {
            LOG_INFO(MOD_GRAPHICS, "ZoneShaderManager: Atlas GLSL shaders compiled (solid={}, alpha={})",
                     materialAtlasSolid_, materialAtlasAlpha_);
        }
    }

    // Compile wind shader (alpha-test only — trees always use alpha for leaf transparency)
    LOG_DEBUG(MOD_SHADER, "Compiling wind alpha-test shader (VS=WIND_VERTEX_SHADER_SRC, FS=fragmentAlphaSrc)...");
    WindShaderCallback* windCallback = new WindShaderCallback(this);
    materialWindAlphaTest_ = gpu->addHighLevelShaderMaterial(
        WIND_VERTEX_SHADER_SRC, "main", irr::video::EVST_VS_1_1,
        fragmentAlphaSrc, "main", irr::video::EPST_PS_1_1,
        windCallback,
        irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF);
    windCallback->drop();

    if (materialWindAlphaTest_ < 0) {
        LOG_WARN(MOD_GRAPHICS, "ZoneShaderManager: Failed to compile wind alpha-test shader");
    } else {
        LOG_INFO(MOD_GRAPHICS, "ZoneShaderManager: Wind GLSL shader compiled (alphaTest={})",
                 materialWindAlphaTest_);
    }

    // Wind RGBA alpha-test material (H02d, Option A): reads uTexture.a
    // directly — the wind counterpart of materialAlphaTestRGBA_, for masked
    // tree foliage on the direct RGBA/DDS decode path (no ETC1 companion
    // texture exists or is needed). Non-fatal if it fails to compile —
    // callers fall back to the base wind alpha-test variant.
    WindShaderCallback* windRGBACallback = new WindShaderCallback(this);
    materialWindAlphaTestRGBA_ = gpu->addHighLevelShaderMaterial(
        WIND_VERTEX_SHADER_SRC, "main", irr::video::EVST_VS_1_1,
        fragmentAlphaRGBASrc, "main", irr::video::EPST_PS_1_1,
        windRGBACallback,
        irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF);
    windRGBACallback->drop();

    if (materialWindAlphaTestRGBA_ < 0) {
        LOG_WARN(MOD_GRAPHICS, "ZoneShaderManager: Failed to compile wind RGBA alpha-test shader");
    } else {
        LOG_INFO(MOD_GRAPHICS, "ZoneShaderManager: Wind RGBA GLSL shader compiled (alphaTestRGBA={})",
                 materialWindAlphaTestRGBA_);
    }

    // Wind solid material (H02d, Option A): for tree materials that are NOT
    // masked (e.g. trunk textures) — wind vertex displacement without
    // alpha-test discard semantics. Non-fatal if it fails to compile —
    // callers fall back to the base wind alpha-test variant.
    WindShaderCallback* windSolidCallback = new WindShaderCallback(this);
    materialWindSolid_ = gpu->addHighLevelShaderMaterial(
        WIND_VERTEX_SHADER_SRC, "main", irr::video::EVST_VS_1_1,
        FRAGMENT_SHADER_SOLID_SRC, "main", irr::video::EPST_PS_1_1,
        windSolidCallback,
        irr::video::EMT_SOLID);
    windSolidCallback->drop();

    if (materialWindSolid_ < 0) {
        LOG_WARN(MOD_GRAPHICS, "ZoneShaderManager: Failed to compile wind solid shader");
    } else {
        LOG_INFO(MOD_GRAPHICS, "ZoneShaderManager: Wind solid GLSL shader compiled (windSolid={})",
                 materialWindSolid_);
    }

#ifdef EQT_HAS_GLES2
    // Compile lightweight variants (no per-pixel player light, 3 varyings only)
    // These reuse existing ShaderCallback/AtlasShaderCallback/WindShaderCallback —
    // the loc*_ fields for removed uniforms resolve to -1, and if (loc >= 0) guards skip them.
    ShaderCallback* lwSolidCb = new ShaderCallback(this);
    materialLWSolid_ = gpu->addHighLevelShaderMaterial(
        VERTEX_SHADER_LIGHTWEIGHT_SRC, "main", irr::video::EVST_VS_1_1,
        FRAGMENT_SHADER_LIGHTWEIGHT_SOLID_SRC, "main", irr::video::EPST_PS_1_1,
        lwSolidCb, irr::video::EMT_SOLID);
    lwSolidCb->drop();

    if (materialLWSolid_ < 0) {
        LOG_WARN(MOD_GRAPHICS, "ZoneShaderManager: Failed to compile lightweight solid shader");
    } else {
        ShaderCallback* lwAlphaCb = new ShaderCallback(this);
        materialLWAlphaTest_ = gpu->addHighLevelShaderMaterial(
            VERTEX_SHADER_LIGHTWEIGHT_SRC, "main", irr::video::EVST_VS_1_1,
            FRAGMENT_SHADER_LIGHTWEIGHT_ALPHA_SRC, "main", irr::video::EPST_PS_1_1,
            lwAlphaCb, irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF);
        lwAlphaCb->drop();

        if (materialLWAlphaTest_ < 0) {
            LOG_WARN(MOD_GRAPHICS, "ZoneShaderManager: Failed to compile lightweight alpha shader");
            materialLWSolid_ = -1;
        }
    }

    if (materialLWSolid_ >= 0) {
        // Lightweight RGBA alpha-test (H02d, Option A) — main-texture-alpha
        // equivalent of materialLWAlphaTest_. Non-fatal if it fails.
        ShaderCallback* lwAlphaRGBACb = new ShaderCallback(this);
        materialLWAlphaTestRGBA_ = gpu->addHighLevelShaderMaterial(
            VERTEX_SHADER_LIGHTWEIGHT_SRC, "main", irr::video::EVST_VS_1_1,
            FRAGMENT_SHADER_LIGHTWEIGHT_ALPHA_RGBA_SRC, "main", irr::video::EPST_PS_1_1,
            lwAlphaRGBACb, irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF);
        lwAlphaRGBACb->drop();

        if (materialLWAlphaTestRGBA_ < 0) {
            LOG_WARN(MOD_GRAPHICS, "ZoneShaderManager: Failed to compile lightweight RGBA alpha-test shader");
        }
    }

    if (materialLWSolid_ >= 0) {
        // Lightweight atlas solid
        AtlasShaderCallback* lwAtlasSolidCb = new AtlasShaderCallback(this, false);
        materialLWAtlasSolid_ = gpu->addHighLevelShaderMaterial(
            ATLAS_VERTEX_SHADER_LIGHTWEIGHT_SRC, "main", irr::video::EVST_VS_1_1,
            FRAGMENT_SHADER_LIGHTWEIGHT_SOLID_SRC, "main", irr::video::EPST_PS_1_1,
            lwAtlasSolidCb, irr::video::EMT_SOLID);
        lwAtlasSolidCb->drop();

        // Lightweight atlas alpha
        AtlasShaderCallback* lwAtlasAlphaCb = new AtlasShaderCallback(this, true);
        materialLWAtlasAlpha_ = gpu->addHighLevelShaderMaterial(
            ATLAS_VERTEX_SHADER_LIGHTWEIGHT_SRC, "main", irr::video::EVST_VS_1_1,
            ATLAS_FRAGMENT_SHADER_LIGHTWEIGHT_ALPHA_SRC, "main", irr::video::EPST_PS_1_1,
            lwAtlasAlphaCb, irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF);
        lwAtlasAlphaCb->drop();

        if (materialLWAtlasSolid_ < 0 || materialLWAtlasAlpha_ < 0) {
            LOG_WARN(MOD_GRAPHICS, "ZoneShaderManager: Failed to compile lightweight atlas shaders");
            materialLWAtlasSolid_ = -1;
            materialLWAtlasAlpha_ = -1;
        }

        // Lightweight wind alpha-test
        LOG_DEBUG(MOD_SHADER, "Compiling lightweight wind alpha-test shader...");
        WindShaderCallback* lwWindCb = new WindShaderCallback(this);
        materialLWWindAlphaTest_ = gpu->addHighLevelShaderMaterial(
            WIND_VERTEX_SHADER_LIGHTWEIGHT_SRC, "main", irr::video::EVST_VS_1_1,
            FRAGMENT_SHADER_LIGHTWEIGHT_ALPHA_SRC, "main", irr::video::EPST_PS_1_1,
            lwWindCb, irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF);
        lwWindCb->drop();

        if (materialLWWindAlphaTest_ < 0) {
            LOG_WARN(MOD_GRAPHICS, "ZoneShaderManager: Failed to compile lightweight wind shader");
        }

        // Lightweight wind RGBA alpha-test (H02d, Option A) — main-texture-alpha
        // equivalent of materialLWAlphaTestRGBA_, with wind vertex displacement.
        // Non-fatal if it fails.
        WindShaderCallback* lwWindRGBACb = new WindShaderCallback(this);
        materialLWWindAlphaTestRGBA_ = gpu->addHighLevelShaderMaterial(
            WIND_VERTEX_SHADER_LIGHTWEIGHT_SRC, "main", irr::video::EVST_VS_1_1,
            FRAGMENT_SHADER_LIGHTWEIGHT_ALPHA_RGBA_SRC, "main", irr::video::EPST_PS_1_1,
            lwWindRGBACb, irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF);
        lwWindRGBACb->drop();

        if (materialLWWindAlphaTestRGBA_ < 0) {
            LOG_WARN(MOD_GRAPHICS, "ZoneShaderManager: Failed to compile lightweight wind RGBA shader");
        }

        // Lightweight wind solid (H02d, Option A) — non-masked tree material
        // equivalent of materialWindSolid_. Non-fatal if it fails.
        WindShaderCallback* lwWindSolidCb = new WindShaderCallback(this);
        materialLWWindSolid_ = gpu->addHighLevelShaderMaterial(
            WIND_VERTEX_SHADER_LIGHTWEIGHT_SRC, "main", irr::video::EVST_VS_1_1,
            FRAGMENT_SHADER_LIGHTWEIGHT_SOLID_SRC, "main", irr::video::EPST_PS_1_1,
            lwWindSolidCb, irr::video::EMT_SOLID);
        lwWindSolidCb->drop();

        if (materialLWWindSolid_ < 0) {
            LOG_WARN(MOD_GRAPHICS, "ZoneShaderManager: Failed to compile lightweight wind solid shader");
        }

        LOG_INFO(MOD_GRAPHICS, "ZoneShaderManager: Lightweight shaders compiled (solid={}, alpha={}, "
                 "atlasSolid={}, atlasAlpha={}, wind={}, windRGBA={}, windSolid={})",
                 materialLWSolid_, materialLWAlphaTest_,
                 materialLWAtlasSolid_, materialLWAtlasAlpha_, materialLWWindAlphaTest_,
                 materialLWWindAlphaTestRGBA_, materialLWWindSolid_);
    }

    // Compile debug visualization shader (expensive FS with branches — only used with /shaderdebug)
    {
        ShaderCallback* debugCb = new ShaderCallback(this);
        materialDebug_ = gpu->addHighLevelShaderMaterial(
            VERTEX_SHADER_SRC, "main", irr::video::EVST_VS_1_1,
            FRAGMENT_SHADER_DEBUG_SRC, "main", irr::video::EPST_PS_1_1,
            debugCb, irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF);
        debugCb->drop();
        if (materialDebug_ >= 0) {
            LOG_INFO(MOD_SHADER, "Debug visualization shader compiled (material={})", materialDebug_);
        } else {
            LOG_WARN(MOD_SHADER, "Debug visualization shader compilation failed");
        }
    }

    // Create FP16 attenuation LUT texture for per-pixel player light
    // Replaces inversesqrt + division with a single texture lookup (1.78ms savings on Mali 400)
    {
        glGenTextures(1, &attenLutTexture_);
        if (attenLutTexture_ > 0) {
            glBindTexture(GL_TEXTURE_2D, attenLutTexture_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            // Initialize with default attenuation (will be updated on first beginFrame)
            uint16_t data[256 * 2];
            fillAttenLUT(data, playerLightAtten_[0], playerLightAtten_[2], lutMaxD2_);
            eqt::memory::GemTag gemTag("tex:light-lut 256x1");
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, 256, 1, 0,
                         GL_LUMINANCE_ALPHA, GL_HALF_FLOAT_OES, data);
            lutBakedConst_ = playerLightAtten_[0];
            lutBakedQuad_ = playerLightAtten_[2];
            lutDirty_ = false;
            glBindTexture(GL_TEXTURE_2D, 0);
            LOG_INFO(MOD_GRAPHICS, "ZoneShaderManager: FP16 attenuation LUT created (256x1, maxD²={}, scale={})",
                     lutMaxD2_, lutScale_);
        }
    }
#endif // EQT_HAS_GLES2
}

ZoneShaderManager::~ZoneShaderManager() {
#ifdef EQT_HAS_GLES2
    if (attenLutTexture_ > 0) {
        glDeleteTextures(1, &attenLutTexture_);
        attenLutTexture_ = 0;
    }
#endif
}

void ZoneShaderManager::beginFrame() {
    ++frameId_;
    LOG_TRACE(MOD_SHADER, "beginFrame: frameId={} perPixel={} windMat={} lwWindMat={} activeWind={}",
              frameId_, perPixelPlayerLight_, materialWindAlphaTest_, materialLWWindAlphaTest_,
              getActiveWindAlphaTest());
#ifdef EQT_HAS_GLES2
    // Regenerate LUT if player light attenuation params changed
    if (lutDirty_ && attenLutTexture_ > 0) {
        uint16_t data[256 * 2];
        fillAttenLUT(data, playerLightAtten_[0], playerLightAtten_[2], lutMaxD2_);
        glBindTexture(GL_TEXTURE_2D, attenLutTexture_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1,
                        GL_LUMINANCE_ALPHA, GL_HALF_FLOAT_OES, data);
        glBindTexture(GL_TEXTURE_2D, 0);
        lutBakedConst_ = playerLightAtten_[0];
        lutBakedQuad_ = playerLightAtten_[2];
        lutDirty_ = false;
    }
#endif
}

// ============================================================================
// Active material getters (lightweight vs per-pixel)
// ============================================================================

irr::s32 ZoneShaderManager::getActiveSolid() const {
    if (perPixelPlayerLight_ || materialLWSolid_ < 0) return materialSolid_;
    return materialLWSolid_;
}

irr::s32 ZoneShaderManager::getActiveAlphaTest() const {
    if (perPixelPlayerLight_ || materialLWAlphaTest_ < 0) return materialAlphaTest_;
    return materialLWAlphaTest_;
}

irr::s32 ZoneShaderManager::getActiveAlphaTestRGBA() const {
    if (perPixelPlayerLight_ || materialLWAlphaTestRGBA_ < 0) return materialAlphaTestRGBA_;
    return materialLWAlphaTestRGBA_;
}

irr::s32 ZoneShaderManager::getActiveAtlasSolid() const {
    if (perPixelPlayerLight_ || materialLWAtlasSolid_ < 0) return materialAtlasSolid_;
    return materialLWAtlasSolid_;
}

irr::s32 ZoneShaderManager::getActiveAtlasAlpha() const {
    if (perPixelPlayerLight_ || materialLWAtlasAlpha_ < 0) return materialAtlasAlpha_;
    return materialLWAtlasAlpha_;
}

irr::s32 ZoneShaderManager::getActiveWindAlphaTest() const {
    if (perPixelPlayerLight_ || materialLWWindAlphaTest_ < 0) return materialWindAlphaTest_;
    return materialLWWindAlphaTest_;
}

irr::s32 ZoneShaderManager::getActiveWindAlphaTestRGBA() const {
    if (perPixelPlayerLight_ || materialLWWindAlphaTestRGBA_ < 0) return materialWindAlphaTestRGBA_;
    return materialLWWindAlphaTestRGBA_;
}

irr::s32 ZoneShaderManager::getActiveWindSolid() const {
    if (perPixelPlayerLight_ || materialLWWindSolid_ < 0) return materialWindSolid_;
    return materialLWWindSolid_;
}

void ZoneShaderManager::setPerPixelPlayerLight(bool enabled) {
    LOG_DEBUG(MOD_SHADER, "setPerPixelPlayerLight: {} -> {} (wind: pp={} lw={} active={})",
              perPixelPlayerLight_ ? "on" : "off", enabled ? "on" : "off",
              materialWindAlphaTest_, materialLWWindAlphaTest_, getActiveWindAlphaTest());
    perPixelPlayerLight_ = enabled;
}

// ============================================================================
// Uniform setters
// ============================================================================

void ZoneShaderManager::setFog(float start, float end, float r, float g, float b, float a) {
    fogStart_ = start;
    fogEnd_ = end;
    fogColor_[0] = r;
    fogColor_[1] = g;
    fogColor_[2] = b;
    fogColor_[3] = a;
}

void ZoneShaderManager::setSunDirection(float x, float y, float z) {
    sunDir_[0] = x;
    sunDir_[1] = y;
    sunDir_[2] = z;
}

void ZoneShaderManager::setSunColor(float r, float g, float b) {
    sunColor_[0] = r;
    sunColor_[1] = g;
    sunColor_[2] = b;
}

void ZoneShaderManager::setAmbientColor(float r, float g, float b) {
    ambientColor_[0] = r;
    ambientColor_[1] = g;
    ambientColor_[2] = b;
}

void ZoneShaderManager::setTintColor(float r, float g, float b) {
    tintColor_[0] = r;
    tintColor_[1] = g;
    tintColor_[2] = b;
}

void ZoneShaderManager::setCameraPos(float x, float y, float z) {
    cameraPos_[0] = x;
    cameraPos_[1] = y;
    cameraPos_[2] = z;
}

void ZoneShaderManager::setFireGlowLight(int index, float px, float py, float pz,
                                          float cr, float cg, float cb,
                                          float radius, float intensity) {
    if (index < 0 || index >= MAX_FIRE_GLOW_LIGHTS) return;
    int i3 = index * 3;
    fireGlowLightPos_[i3] = px; fireGlowLightPos_[i3+1] = py; fireGlowLightPos_[i3+2] = pz;
    fireGlowLightColor_[i3] = cr; fireGlowLightColor_[i3+1] = cg; fireGlowLightColor_[i3+2] = cb;
    fireGlowLightRadius_[index] = radius;
    fireGlowLightIntensity_[index] = intensity;
}

void ZoneShaderManager::setNumFireGlowLights(int count) {
    numFireGlowLights_ = (count < 0) ? 0 : (count > MAX_FIRE_GLOW_LIGHTS ? MAX_FIRE_GLOW_LIGHTS : count);
}

void ZoneShaderManager::clearFireGlowLights() {
    numFireGlowLights_ = 0;
    std::memset(fireGlowLightPos_, 0, sizeof(fireGlowLightPos_));
    std::memset(fireGlowLightColor_, 0, sizeof(fireGlowLightColor_));
    std::memset(fireGlowLightRadius_, 0, sizeof(fireGlowLightRadius_));
    std::memset(fireGlowLightIntensity_, 0, sizeof(fireGlowLightIntensity_));
}

} // namespace Graphics
} // namespace EQT
