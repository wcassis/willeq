// sky_renderer_colors.cpp - sky colour derivation and the GLES2 strip
// upload path.
// Sky colour computation and interpolation, sky-layer and sun-glow colours,
// sky category selection, the per-category special colours, cloud
// scrolling, and the incremental strip-upload family.
// Split move-only from the original 1,770-line sky_renderer.cpp by
// refactor/J30_sky_renderer_split.md; companions are sky_renderer.cpp and
// sky_renderer_celestial.cpp.
//
// This TU owns the file's trailing conditional-compilation region - the one
// guarded on the GLES2 feature macro, originally lines 1644-1767 - WHOLE,
// and it must stay whole. The region spans complete method definitions; a
// cut inside it would leave one file with an unmatched closing directive
// and the other with an unterminated opening one, and BOTH halves would
// still compile in the default configuration, so the breakage would surface
// only in a configuration the acceptance bar does not build.
#include "client/graphics/sky_renderer.h"
#include "client/graphics/eq/sky_loader.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/sky_config.h"
#include "client/graphics/gles2_vertex_color.h"
#include "common/logging.h"
#include <cmath>
#include <algorithm>

#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
// Bridge function defined in COpenGLES2Driver.cpp
extern void* gles2WrapTexture(void* driver, const char* name, unsigned int glTexName,
                                unsigned int width, unsigned int height,
                                unsigned int gpuBytes = 0);
#endif

namespace EQT {
namespace Graphics {
SkyColorSet SkyRenderer::calculateSkyColors(float timeOfDay) const {
    // Define color sets for different times of day
    // Night (0-4): Dark blue/black sky
    static const SkyColorSet NIGHT = {
        irr::video::SColor(255, 10, 10, 30),     // zenith: deep blue-black
        irr::video::SColor(255, 20, 20, 50),     // horizon: slightly lighter
        irr::video::SColor(255, 15, 15, 35),     // fog: dark blue
        0.0f,                                     // sunIntensity
        0.2f                                      // cloudBrightness: dim
    };

    // Pre-dawn (4-5): Very early light
    static const SkyColorSet PRE_DAWN = {
        irr::video::SColor(255, 25, 25, 60),     // zenith: dark blue
        irr::video::SColor(255, 60, 40, 50),     // horizon: hint of color
        irr::video::SColor(255, 35, 30, 45),     // fog
        0.05f,
        0.3f
    };

    // Dawn (5-6): Orange/pink gradient, sun rising
    static const SkyColorSet DAWN = {
        irr::video::SColor(255, 80, 100, 150),   // zenith: lightening blue
        irr::video::SColor(255, 220, 140, 90),   // horizon: orange-pink
        irr::video::SColor(255, 140, 110, 100),  // fog: warm
        0.3f,
        0.6f
    };

    // Sunrise (6-7): Golden hour
    static const SkyColorSet SUNRISE = {
        irr::video::SColor(255, 130, 160, 200),  // zenith: light blue
        irr::video::SColor(255, 255, 180, 100),  // horizon: golden
        irr::video::SColor(255, 180, 160, 140),  // fog: warm golden
        0.6f,
        0.8f
    };

    // Morning (7-10): Transitioning to day
    static const SkyColorSet MORNING = {
        irr::video::SColor(255, 140, 180, 220),  // zenith: bright blue
        irr::video::SColor(255, 200, 210, 220),  // horizon: pale blue
        irr::video::SColor(255, 180, 195, 210),  // fog: light blue
        0.85f,
        0.95f
    };

    // Day (10-16): Full daylight
    static const SkyColorSet DAY = {
        irr::video::SColor(255, 100, 150, 220),  // zenith: sky blue
        irr::video::SColor(255, 180, 200, 220),  // horizon: pale
        irr::video::SColor(255, 160, 180, 200),  // fog: bluish
        1.0f,
        1.0f
    };

    // Afternoon (16-17): Slightly warmer
    static const SkyColorSet AFTERNOON = {
        irr::video::SColor(255, 120, 160, 210),  // zenith: warm blue
        irr::video::SColor(255, 210, 200, 190),  // horizon: hint of warmth
        irr::video::SColor(255, 175, 180, 190),  // fog
        0.9f,
        1.0f
    };

    // Sunset (17-18): Golden hour
    static const SkyColorSet SUNSET = {
        irr::video::SColor(255, 130, 130, 180),  // zenith: purple-blue
        irr::video::SColor(255, 255, 160, 80),   // horizon: orange-gold
        irr::video::SColor(255, 180, 140, 120),  // fog: warm
        0.6f,
        0.8f
    };

    // Dusk (18-19): Orange/red gradient, sun setting
    static const SkyColorSet DUSK = {
        irr::video::SColor(255, 80, 60, 120),    // zenith: purple
        irr::video::SColor(255, 200, 100, 60),   // horizon: red-orange
        irr::video::SColor(255, 120, 80, 80),    // fog: reddish
        0.25f,
        0.5f
    };

    // Twilight (19-20): Transitioning to night
    static const SkyColorSet TWILIGHT = {
        irr::video::SColor(255, 40, 40, 80),     // zenith: dark blue-purple
        irr::video::SColor(255, 80, 50, 60),     // horizon: fading color
        irr::video::SColor(255, 50, 40, 55),     // fog: dark
        0.1f,
        0.35f
    };

    // Interpolate between color sets based on time
    if (timeOfDay < 4.0f) {
        // Night (0-4)
        return NIGHT;
    } else if (timeOfDay < 5.0f) {
        // Pre-dawn (4-5)
        float t = timeOfDay - 4.0f;
        return interpolateSkyColors(NIGHT, PRE_DAWN, t);
    } else if (timeOfDay < 6.0f) {
        // Dawn (5-6)
        float t = timeOfDay - 5.0f;
        return interpolateSkyColors(PRE_DAWN, DAWN, t);
    } else if (timeOfDay < 7.0f) {
        // Sunrise (6-7)
        float t = timeOfDay - 6.0f;
        return interpolateSkyColors(DAWN, SUNRISE, t);
    } else if (timeOfDay < 10.0f) {
        // Morning (7-10)
        float t = (timeOfDay - 7.0f) / 3.0f;
        return interpolateSkyColors(SUNRISE, MORNING, t);
    } else if (timeOfDay < 16.0f) {
        // Day (10-16) - no interpolation, full daylight
        if (timeOfDay < 11.0f) {
            float t = timeOfDay - 10.0f;
            return interpolateSkyColors(MORNING, DAY, t);
        }
        return DAY;
    } else if (timeOfDay < 17.0f) {
        // Afternoon (16-17)
        float t = timeOfDay - 16.0f;
        return interpolateSkyColors(DAY, AFTERNOON, t);
    } else if (timeOfDay < 18.0f) {
        // Sunset (17-18)
        float t = timeOfDay - 17.0f;
        return interpolateSkyColors(AFTERNOON, SUNSET, t);
    } else if (timeOfDay < 19.0f) {
        // Dusk (18-19)
        float t = timeOfDay - 18.0f;
        return interpolateSkyColors(SUNSET, DUSK, t);
    } else if (timeOfDay < 20.0f) {
        // Twilight (19-20)
        float t = timeOfDay - 19.0f;
        return interpolateSkyColors(DUSK, TWILIGHT, t);
    } else if (timeOfDay < 21.0f) {
        // Transition to night (20-21)
        float t = timeOfDay - 20.0f;
        return interpolateSkyColors(TWILIGHT, NIGHT, t);
    } else {
        // Night (21-24)
        return NIGHT;
    }
}

SkyColorSet SkyRenderer::interpolateSkyColors(const SkyColorSet& a, const SkyColorSet& b, float t) const {
    // Clamp t to 0-1
    t = irr::core::clamp(t, 0.0f, 1.0f);

    SkyColorSet result;

    // Interpolate colors
    result.zenith = irr::video::SColor(
        255,
        static_cast<irr::u32>(a.zenith.getRed() + (b.zenith.getRed() - a.zenith.getRed()) * t),
        static_cast<irr::u32>(a.zenith.getGreen() + (b.zenith.getGreen() - a.zenith.getGreen()) * t),
        static_cast<irr::u32>(a.zenith.getBlue() + (b.zenith.getBlue() - a.zenith.getBlue()) * t)
    );

    result.horizon = irr::video::SColor(
        255,
        static_cast<irr::u32>(a.horizon.getRed() + (b.horizon.getRed() - a.horizon.getRed()) * t),
        static_cast<irr::u32>(a.horizon.getGreen() + (b.horizon.getGreen() - a.horizon.getGreen()) * t),
        static_cast<irr::u32>(a.horizon.getBlue() + (b.horizon.getBlue() - a.horizon.getBlue()) * t)
    );

    result.fog = irr::video::SColor(
        255,
        static_cast<irr::u32>(a.fog.getRed() + (b.fog.getRed() - a.fog.getRed()) * t),
        static_cast<irr::u32>(a.fog.getGreen() + (b.fog.getGreen() - a.fog.getGreen()) * t),
        static_cast<irr::u32>(a.fog.getBlue() + (b.fog.getBlue() - a.fog.getBlue()) * t)
    );

    // Interpolate floats
    result.sunIntensity = a.sunIntensity + (b.sunIntensity - a.sunIntensity) * t;
    result.cloudBrightness = a.cloudBrightness + (b.cloudBrightness - a.cloudBrightness) * t;

    return result;
}

void SkyRenderer::updateSkyLayerColors() {
    if (!enabled_) {
        return;
    }

    // Apply tint to sky layers based on time of day
    // Use cloud brightness to modulate layer colors

    // Create a tint color that blends zenith and horizon colors
    // This simulates the sky gradient effect
    irr::video::SColor tint(
        255,
        (currentSkyColors_.zenith.getRed() + currentSkyColors_.horizon.getRed()) / 2,
        (currentSkyColors_.zenith.getGreen() + currentSkyColors_.horizon.getGreen()) / 2,
        (currentSkyColors_.zenith.getBlue() + currentSkyColors_.horizon.getBlue()) / 2
    );

    // Modulate by time-of-day brightness AND weather brightness
    // Weather darkens the sky during rain/storms
    float effectiveBrightness = currentSkyColors_.cloudBrightness * weatherBrightness_;
    tint = irr::video::SColor(
        255,
        static_cast<irr::u32>(tint.getRed() * effectiveBrightness),
        static_cast<irr::u32>(tint.getGreen() * effectiveBrightness),
        static_cast<irr::u32>(tint.getBlue() * effectiveBrightness)
    );

    // Apply tint to legacy sky dome nodes
    for (auto* node : skyDomeNodes_) {
        if (node) {
            for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                // Set diffuse color to modulate texture
                node->getMaterial(i).DiffuseColor = tint;
                node->getMaterial(i).AmbientColor = tint;
                // Ensure vertex colors are used for modulation
                node->getMaterial(i).ColorMaterial = irr::video::ECM_NONE;
            }
        }
    }

    // Apply tint to custom dome mesh vertex colors
    // Set vertex RGB to tint color while preserving per-vertex alpha (horizon fade).
    // T484 (B726): through toGles2VertexColor(), because the GLES2 driver uploads
    // SColor's stored bytes unconverted and the shader would otherwise receive
    // the tint with red and blue exchanged -- a tan daylight sky. See the header.
    if (skyDomeMeshBuffer_) {
        const irr::video::SColor vertexTint = toGles2VertexColor(tint);
        for (irr::u32 i = 0; i < skyDomeMeshBuffer_->Vertices.size(); ++i) {
            irr::video::S3DVertex& vert = skyDomeMeshBuffer_->Vertices[i];
            irr::u32 existingAlpha = vert.Color.getAlpha();
            vert.Color = vertexTint;
            vert.Color.setAlpha(existingAlpha);
        }
        skyDomeMeshBuffer_->setDirty(irr::scene::EBT_VERTEX);
    }

    // Set the background/clear color to match the tint for proper day/night effect
    if (driver_) {
        // Use a darker version of the tint as the background color
        // This shows through the semi-transparent sky at night
        irr::video::SColor clearColor(
            255,
            tint.getRed() / 2,
            tint.getGreen() / 2,
            tint.getBlue() / 2
        );
        // Store for use by beginScene
        currentClearColor_ = clearColor;
    }
}

void SkyRenderer::updateSunGlowColor() {
    if (!sunGlowNode_) {
        return;
    }

    // Adjust sun glow color based on time of day
    // Dawn/dusk: warm orange-red glow
    // Day: warm yellow-white glow
    // Night: no glow (handled by visibility)

    float timeOfDay = static_cast<float>(currentHour_) + static_cast<float>(currentMinute_) / 60.0f;

    irr::video::SColor glowColor;

    if (timeOfDay < 5.0f || timeOfDay > 20.0f) {
        // Night - no glow (but node should be hidden anyway)
        glowColor = irr::video::SColor(0, 255, 200, 100);
    } else if (timeOfDay < 7.0f) {
        // Dawn - warm orange-red glow
        float t = (timeOfDay - 5.0f) / 2.0f;  // 0 at 5am, 1 at 7am
        irr::u32 alpha = static_cast<irr::u32>(60 + 60 * t);  // Increasing intensity
        glowColor = irr::video::SColor(alpha, 255, 150 + static_cast<irr::u32>(50 * t), 80);
    } else if (timeOfDay < 17.0f) {
        // Day - warm yellow-white glow
        glowColor = irr::video::SColor(100, 255, 230, 180);
    } else if (timeOfDay < 19.0f) {
        // Sunset - warm orange-red glow, fading
        float t = (timeOfDay - 17.0f) / 2.0f;  // 0 at 5pm, 1 at 7pm
        irr::u32 alpha = static_cast<irr::u32>(100 - 60 * t);  // Decreasing intensity
        glowColor = irr::video::SColor(alpha, 255, 180 - static_cast<irr::u32>(30 * t), 100);
    } else {
        // Twilight - fading glow
        float t = (timeOfDay - 19.0f) / 1.0f;
        irr::u32 alpha = static_cast<irr::u32>(40 * (1.0f - t));
        glowColor = irr::video::SColor(alpha, 255, 150, 100);
    }

    // Apply weather brightness to sun glow (dim/hide sun during storms)
    if (weatherBrightness_ < 1.0f) {
        irr::u32 alpha = static_cast<irr::u32>(glowColor.getAlpha() * weatherBrightness_);
        glowColor.setAlpha(alpha);
    }

    sunGlowNode_->getMaterial(0).DiffuseColor = glowColor;
}

SkyCategory SkyRenderer::determineSkyCategory(uint8_t skyTypeId) const {
    // Map sky type IDs to categories based on SkyLoader definitions
    switch (skyTypeId) {
        case 0:  // DEFAULT
        case 1:  // NORMAL
        case 2:  // DESERT
        case 3:  // AIR (classic)
        case 4:  // COTTONY
            return SkyCategory::Normal;

        case 5:  // FEAR / RED
            return SkyCategory::PoWar;  // Red-tinted, no day/night

        case 6:  // LUCLIN
        case 7:  // LUCLIN2
        case 8:  // LUCLIN3
        case 9:  // LUCLIN4
            return SkyCategory::Luclin;

        case 10: // THE GREY
            return SkyCategory::TheGrey;

        case 11: // POFIRE
            return SkyCategory::PoFire;

        case 12: // POSTORMS
            return SkyCategory::PoStorms;

        case 14: // POWAR
            return SkyCategory::PoWar;

        case 15: // POAIR1
        case 17: // POAIR2
            return SkyCategory::PoAir;

        case 16: // POTRANQ
            return SkyCategory::PoTranq;

        default:
            return SkyCategory::Normal;
    }
}

SkyColorSet SkyRenderer::getSpecialSkyColors(SkyCategory category) const {
    // Return fixed colors for special sky types that don't have day/night cycles
    switch (category) {
        case SkyCategory::PoFire:
            // Plane of Fire - red/orange inferno
            return {
                irr::video::SColor(255, 180, 60, 30),    // zenith: dark orange-red
                irr::video::SColor(255, 255, 120, 40),   // horizon: bright orange
                irr::video::SColor(255, 200, 80, 40),    // fog: orange-red
                0.8f,   // sunIntensity
                0.9f    // cloudBrightness
            };

        case SkyCategory::PoStorms:
            // Plane of Storms - dark grey, stormy
            return {
                irr::video::SColor(255, 40, 45, 55),     // zenith: dark grey-blue
                irr::video::SColor(255, 70, 75, 85),     // horizon: lighter grey
                irr::video::SColor(255, 50, 55, 65),     // fog: grey
                0.3f,   // sunIntensity (dim)
                0.5f    // cloudBrightness
            };

        case SkyCategory::PoAir:
            // Plane of Air - light, ethereal blue
            return {
                irr::video::SColor(255, 180, 210, 255),  // zenith: bright light blue
                irr::video::SColor(255, 220, 240, 255),  // horizon: almost white
                irr::video::SColor(255, 200, 225, 255),  // fog: light blue
                1.0f,   // sunIntensity
                1.0f    // cloudBrightness
            };

        case SkyCategory::PoWar:
            // Plane of War / Fear - dark red
            return {
                irr::video::SColor(255, 100, 30, 30),    // zenith: dark red
                irr::video::SColor(255, 150, 50, 40),    // horizon: blood red
                irr::video::SColor(255, 120, 40, 35),    // fog: red
                0.5f,   // sunIntensity
                0.6f    // cloudBrightness
            };

        case SkyCategory::TheGrey:
            // The Grey / Nightmare / Disease - uniform grey, oppressive
            return {
                irr::video::SColor(255, 80, 80, 85),     // zenith: grey
                irr::video::SColor(255, 100, 100, 105),  // horizon: slightly lighter
                irr::video::SColor(255, 90, 90, 95),     // fog: grey
                0.4f,   // sunIntensity (no visible sun)
                0.4f    // cloudBrightness
            };

        case SkyCategory::PoTranq:
            // Plane of Tranquility - soft, peaceful colors
            return {
                irr::video::SColor(255, 140, 160, 200),  // zenith: soft blue
                irr::video::SColor(255, 200, 210, 230),  // horizon: pale
                irr::video::SColor(255, 170, 185, 210),  // fog: soft blue
                0.8f,   // sunIntensity
                0.9f    // cloudBrightness
            };

        case SkyCategory::Luclin:
            // Luclin has day/night cycle but with different base colors
            // This case shouldn't normally be called since hasDayNightCycle returns true
            return {
                irr::video::SColor(255, 60, 50, 100),    // zenith: purple-ish
                irr::video::SColor(255, 100, 80, 120),   // horizon: purple
                irr::video::SColor(255, 70, 60, 100),    // fog: purple
                0.8f,
                0.9f
            };

        case SkyCategory::Indoor:
            // Indoor - no sky visible
            return {
                irr::video::SColor(255, 30, 30, 40),
                irr::video::SColor(255, 40, 40, 50),
                irr::video::SColor(255, 35, 35, 45),
                0.0f,
                0.0f
            };

        case SkyCategory::Normal:
        default:
            // This shouldn't be called for Normal since it has day/night cycle
            // Return midday colors as fallback
            return {
                irr::video::SColor(255, 100, 150, 220),
                irr::video::SColor(255, 180, 200, 220),
                irr::video::SColor(255, 160, 180, 200),
                1.0f,
                1.0f
            };
    }
}

bool SkyRenderer::hasDayNightCycle(SkyCategory category) const {
    switch (category) {
        case SkyCategory::Normal:
        case SkyCategory::Luclin:
            return true;

        case SkyCategory::PoFire:
        case SkyCategory::PoStorms:
        case SkyCategory::PoAir:
        case SkyCategory::PoWar:
        case SkyCategory::TheGrey:
        case SkyCategory::PoTranq:
        case SkyCategory::Indoor:
            return false;

        default:
            return true;
    }
}

void SkyRenderer::updateCloudScrolling() {
    // Apply UV scrolling to cloud layer nodes (legacy)
    for (auto* node : cloudLayerNodes_) {
        if (!node) continue;
        for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
            irr::core::matrix4 texMat;
            texMat.setTextureTranslate(cloudScrollOffset_, cloudScrollOffset_ * 0.5f);
            node->getMaterial(i).setTextureMatrix(0, texMat);
        }
    }

    // Apply UV scrolling to custom dome mesh via texture matrix
    if (skyDomeMeshNode_) {
        irr::core::matrix4 texMat;
        texMat.setTextureTranslate(cloudScrollOffset_, cloudScrollOffset_ * 0.5f);
        skyDomeMeshNode_->getMaterial(0).setTextureMatrix(0, texMat);
    }
}

// ============================================================================
// Strip upload for sky textures (GLES2 only)
// ============================================================================

#ifdef EQT_HAS_GLES2

bool SkyRenderer::beginStripUpload(const std::string& name, const uint8_t* argbPixels,
                                     uint32_t w, uint32_t h) {
    // Already cached?
    auto it = textureCache_.find(name);
    if (it != textureCache_.end()) {
        return true;
    }

    if (!argbPixels || w == 0 || h == 0) {
        return true;
    }

    // Initialize strip state
    stripState_ = SkyStripUploadState{};
    stripState_.textureName = name;
    stripState_.texWidth = w;
    stripState_.texHeight = h;
    stripState_.argbPixels = argbPixels;
    stripState_.totalStrips = (h + SKY_STRIP_HEIGHT - 1) / SKY_STRIP_HEIGHT;

    // Allocate GL texture with NULL data (reserves GPU memory without uploading)
    glGenTextures(1, &stripState_.glTexName);
    glBindTexture(GL_TEXTURE_2D, stripState_.glTexName);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    // Allocate reusable swizzle buffer (one strip worth of RGBA pixels)
    stripState_.swizzleBuf.resize(w * SKY_STRIP_HEIGHT * 4);

    // Upload strip 0
    uint32_t rows = std::min(static_cast<uint32_t>(SKY_STRIP_HEIGHT), h);
    uint32_t pixelCount = w * rows;
    const uint8_t* src = argbPixels;
    uint8_t* dst = stripState_.swizzleBuf.data();
    for (uint32_t i = 0; i < pixelCount; ++i) {
        dst[i * 4 + 0] = src[i * 4 + 2];  // R (from BGRA offset 2)
        dst[i * 4 + 1] = src[i * 4 + 1];  // G
        dst[i * 4 + 2] = src[i * 4 + 0];  // B (from BGRA offset 0)
        dst[i * 4 + 3] = src[i * 4 + 3];  // A
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, rows,
                    GL_RGBA, GL_UNSIGNED_BYTE, stripState_.swizzleBuf.data());

    stripState_.currentStrip = 1;
    stripState_.active = true;

    LOG_DEBUG(MOD_GRAPHICS, "beginStripUpload: {} ({}x{}, {} strips)",
              name, w, h, stripState_.totalStrips);

    return false;  // More strips to upload
}

bool SkyRenderer::continueStripUpload() {
    if (!stripState_.active) {
        return true;
    }

    if (stripState_.currentStrip >= stripState_.totalStrips) {
        return true;  // All strips uploaded
    }

    uint32_t w = stripState_.texWidth;
    uint32_t h = stripState_.texHeight;
    uint32_t yOffset = stripState_.currentStrip * SKY_STRIP_HEIGHT;
    uint32_t rows = std::min(static_cast<uint32_t>(SKY_STRIP_HEIGHT), h - yOffset);
    uint32_t pixelCount = w * rows;

    // Swizzle BGRA → RGBA for this strip
    const uint8_t* src = stripState_.argbPixels + yOffset * w * 4;
    uint8_t* dst = stripState_.swizzleBuf.data();
    for (uint32_t i = 0; i < pixelCount; ++i) {
        dst[i * 4 + 0] = src[i * 4 + 2];
        dst[i * 4 + 1] = src[i * 4 + 1];
        dst[i * 4 + 2] = src[i * 4 + 0];
        dst[i * 4 + 3] = src[i * 4 + 3];
    }

    glBindTexture(GL_TEXTURE_2D, stripState_.glTexName);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, yOffset, w, rows,
                    GL_RGBA, GL_UNSIGNED_BYTE, stripState_.swizzleBuf.data());

    stripState_.currentStrip++;

    return (stripState_.currentStrip >= stripState_.totalStrips);
}

void SkyRenderer::finalizeStripUpload() {
    if (!stripState_.active || stripState_.glTexName == 0) {
        return;
    }

    // Wrap the GL texture into Irrlicht's ITexture via bridge function
    irr::video::ITexture* tex = static_cast<irr::video::ITexture*>(
        gles2WrapTexture(driver_, stripState_.textureName.c_str(),
                         stripState_.glTexName, stripState_.texWidth, stripState_.texHeight));

    if (tex) {
        textureCache_[stripState_.textureName] = tex;
        if (constrainedCache_) {
            constrainedCache_->registerTexture(stripState_.textureName, tex,
                static_cast<size_t>(stripState_.texWidth) * stripState_.texHeight * 4, false);
        }
        LOG_DEBUG(MOD_GRAPHICS, "finalizeStripUpload: {} ({}x{})",
                  stripState_.textureName, stripState_.texWidth, stripState_.texHeight);
    } else {
        LOG_WARN(MOD_GRAPHICS, "finalizeStripUpload: failed to wrap {} — deleting orphaned GL texture",
                 stripState_.textureName);
        glDeleteTextures(1, &stripState_.glTexName);
    }

    // Clear strip state
    stripState_ = SkyStripUploadState{};
}

bool SkyRenderer::isStripActive() const {
    return stripState_.active;
}

#endif // EQT_HAS_GLES2

} // namespace Graphics
} // namespace EQT
