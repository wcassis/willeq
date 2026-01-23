#include "client/graphics/environment/rain_overlay.h"
#include "common/logging.h"
#include <cmath>
#include <vector>
#include <string>

namespace EQT {
namespace Graphics {
namespace Environment {

RainOverlay::RainOverlay() = default;

RainOverlay::~RainOverlay() {
    // Texture is managed by Irrlicht driver, don't delete manually
}

bool RainOverlay::initialize(irr::video::IVideoDriver* driver,
                              irr::scene::ISceneManager* smgr,
                              const std::string& eqClientPath) {
    driver_ = driver;
    smgr_ = smgr;

    if (!driver_ || !smgr_) {
        LOG_ERROR(MOD_GRAPHICS, "RainOverlay::initialize - null driver or scene manager");
        return false;
    }

    std::vector<std::string> basePaths = {
        "data/textures/",
        "../data/textures/",
        "../../data/textures/"
    };

    auto loadTexture = [&](const std::string& texName) -> irr::video::ITexture* {
        for (const auto& basePath : basePaths) {
            std::string fullPath = basePath + texName;
            irr::video::ITexture* tex = driver_->getTexture(fullPath.c_str());
            if (tex) {
                LOG_INFO(MOD_GRAPHICS, "RainOverlay: Loaded '{}'", fullPath);
                return tex;
            }
        }
        LOG_WARN(MOD_GRAPHICS, "RainOverlay: Failed to load '{}'", texName);
        return nullptr;
    };

    // Load 10 intensity-based foreground textures (long streaks, varying density)
    intensityTextures_.clear();
    intensityTextures_.reserve(10);
    for (int i = 1; i <= 10; ++i) {
        char filename[32];
        snprintf(filename, sizeof(filename), "rain_intensity_%02d.png", i);
        irr::video::ITexture* tex = loadTexture(filename);
        intensityTextures_.push_back(tex);
    }

    // Load background layer textures
    midLayerTexture_ = loadTexture("rain_layer_mid.png");
    farLayerTexture_ = loadTexture("rain_layer_far.png");

    // Fallback: if intensity textures missing, try legacy texture
    irr::video::ITexture* fallbackTex = nullptr;
    for (auto* tex : intensityTextures_) {
        if (tex) {
            fallbackTex = tex;
            break;
        }
    }
    if (!fallbackTex) {
        fallbackTex = loadTexture("rain_streaks.png");
    }

    if (!fallbackTex) {
        LOG_ERROR(MOD_GRAPHICS, "RainOverlay: Failed to load any rain textures");
        return false;
    }

    // Fill in missing intensity textures with fallback
    for (auto& tex : intensityTextures_) {
        if (!tex) tex = fallbackTex;
    }

    // Fill in missing layer textures with fallback
    if (!midLayerTexture_) midLayerTexture_ = fallbackTex;
    if (!farLayerTexture_) farLayerTexture_ = fallbackTex;

    initialized_ = true;
    LOG_INFO(MOD_GRAPHICS, "RainOverlay initialized with {} intensity textures", intensityTextures_.size());
    return true;
}

void RainOverlay::setIntensity(uint8_t intensity) {
    uint8_t newIntensity = std::min(intensity, static_cast<uint8_t>(10));
    if (newIntensity != intensity_) {
        LOG_INFO(MOD_GRAPHICS, "RainOverlay::setIntensity: {} -> {}", intensity_, newIntensity);
    }
    intensity_ = newIntensity;
}

void RainOverlay::update(float deltaTime, const glm::vec3& cameraPos, const glm::vec3& cameraDir) {
    if (!isActive()) return;

    cameraPos_ = cameraPos;
    cameraDir_ = cameraDir;

    // Calculate scroll speed based on intensity
    float scrollSpeed = settings_.scrollSpeedBase + settings_.scrollSpeedIntensity * intensity_;

    // Update scroll offset (wraps at 1.0 for seamless tiling)
    scrollOffset_ += scrollSpeed * deltaTime;
    while (scrollOffset_ > 1.0f) {
        scrollOffset_ -= 1.0f;
    }
}

void RainOverlay::render() {
    if (!isActive() || intensityTextures_.empty()) {
        return;
    }

    // Get screen dimensions
    irr::core::dimension2d<irr::u32> screenSize = driver_->getScreenSize();
    int screenW = static_cast<int>(screenSize.Width);
    int screenH = static_cast<int>(screenSize.Height);

    // Calculate opacity based on intensity (1-10)
    float t = (intensity_ - 1) / 9.0f;  // 0 at intensity 1, 1 at intensity 10
    float baseOpacity = settings_.baseOpacity + t * (settings_.maxOpacity - settings_.baseOpacity);

    // Determine number of layers based on intensity
    // Intensity 1-3: 1 layer (foreground only)
    // Intensity 4-6: 2 layers (foreground + mid)
    // Intensity 7-10: 3 layers (foreground + mid + far)
    int numLayers = 1;
    if (intensity_ >= 7) {
        numLayers = 3;
    } else if (intensity_ >= 4) {
        numLayers = 2;
    }

    // Render layers from back to front (far -> mid -> foreground)
    // This ensures closer rain renders on top
    for (int layerIdx = numLayers - 1; layerIdx >= 0; --layerIdx) {
        // Layer 0 = foreground (closest, fastest, long streaks, intensity-based density)
        // Layer 1 = mid (medium distance, medium speed, medium streaks)
        // Layer 2 = far (farthest, slowest, short streaks)

        irr::video::ITexture* layerTexture = nullptr;
        float layerSpeed = 1.0f;
        float layerOpacity = baseOpacity;
        float layerScale = 1.0f;

        if (layerIdx == 0) {
            // Foreground layer: use intensity-specific texture
            int texIdx = std::max(0, std::min(static_cast<int>(intensity_) - 1, 9));
            layerTexture = intensityTextures_[texIdx];
            layerSpeed = settings_.layerSpeedMax;  // Fastest
            layerOpacity = baseOpacity;            // Full opacity
            layerScale = settings_.layerScaleMin;  // Largest (closest)
        } else if (layerIdx == 1) {
            // Mid layer: medium streaks
            layerTexture = midLayerTexture_;
            layerSpeed = (settings_.layerSpeedMax + settings_.layerSpeedMin) / 2.0f;  // Medium speed
            layerOpacity = baseOpacity * 0.7f;     // Dimmer
            layerScale = (settings_.layerScaleMin + settings_.layerScaleMax) / 2.0f;
        } else {
            // Far layer: short streaks (distant drops)
            layerTexture = farLayerTexture_;
            layerSpeed = settings_.layerSpeedMin;  // Slowest
            layerOpacity = baseOpacity * 0.5f;     // Dimmest
            layerScale = settings_.layerScaleMax;  // Smallest (farthest)
        }

        if (!layerTexture) continue;

        // Get texture dimensions
        irr::core::dimension2d<irr::u32> texSize = layerTexture->getOriginalSize();
        int texW = static_cast<int>(texSize.Width);
        int texH = static_cast<int>(texSize.Height);

        // Calculate scaled texture size for this layer
        int scaledTexW = static_cast<int>(static_cast<float>(texW) / layerScale);
        int scaledTexH = static_cast<int>(static_cast<float>(texH) / layerScale);

        // Ensure minimum size
        if (scaledTexW < 1) scaledTexW = 1;
        if (scaledTexH < 1) scaledTexH = 1;

        // Calculate how many tiles we need to cover the screen
        int tilesX = (screenW / scaledTexW) + 3;
        int tilesY = (screenH / scaledTexH) + 3;

        // Per-layer scroll offset with speed multiplier
        float layerScrollOffset = std::fmod(scrollOffset_ * layerSpeed, 1.0f);
        int scrollPixelsY = static_cast<int>(layerScrollOffset * static_cast<float>(scaledTexH));

        // Alpha for this layer
        irr::u32 alpha = static_cast<irr::u32>(layerOpacity * 255.0f);
        irr::video::SColor color(alpha, 255, 255, 255);
        irr::video::SColor colors[4] = {color, color, color, color};

        // Draw tiled rain texture covering entire screen
        for (int tx = 0; tx < tilesX + 1; ++tx) {
            for (int ty = 0; ty < tilesY + 2; ++ty) {
                int drawX = tx * scaledTexW;
                int drawY = -scaledTexH + ty * scaledTexH + scrollPixelsY;

                // Skip tiles completely off-screen
                if (drawY + scaledTexH <= 0) continue;
                if (drawY >= screenH) continue;

                // Calculate visible portion
                int visibleTop = std::max(0, drawY);
                int visibleBottom = std::min(screenH, drawY + scaledTexH);
                int visibleLeft = std::max(0, drawX);
                int visibleRight = std::min(screenW, drawX + scaledTexW);

                if (visibleTop >= visibleBottom || visibleLeft >= visibleRight) continue;

                // Calculate source texture coordinates
                float scaleX = static_cast<float>(texW) / static_cast<float>(scaledTexW);
                float scaleY = static_cast<float>(texH) / static_cast<float>(scaledTexH);

                int srcLeft = static_cast<int>((visibleLeft - drawX) * scaleX);
                int srcTop = static_cast<int>((visibleTop - drawY) * scaleY);
                int srcRight = static_cast<int>((visibleRight - drawX) * scaleX);
                int srcBottom = static_cast<int>((visibleBottom - drawY) * scaleY);

                srcLeft = std::max(0, std::min(srcLeft, texW));
                srcTop = std::max(0, std::min(srcTop, texH));
                srcRight = std::max(0, std::min(srcRight, texW));
                srcBottom = std::max(0, std::min(srcBottom, texH));

                irr::core::rect<irr::s32> destRect(visibleLeft, visibleTop, visibleRight, visibleBottom);
                irr::core::rect<irr::s32> srcRect(srcLeft, srcTop, srcRight, srcBottom);

                driver_->draw2DImage(layerTexture, destRect, srcRect, nullptr, colors, true);
            }
        }
    }
}

bool RainOverlay::getFogSettings(float& outFogStart, float& outFogEnd) const {
    if (!settings_.fogReductionEnabled || intensity_ == 0) {
        return false;
    }

    // Interpolate fog based on intensity (1-10)
    // Higher intensity = shorter fog distance
    float t = (intensity_ - 1) / 9.0f;  // 0 at intensity 1, 1 at intensity 10

    outFogStart = settings_.fogStartMax - t * (settings_.fogStartMax - settings_.fogStartMin);
    outFogEnd = settings_.fogEndMax - t * (settings_.fogEndMax - settings_.fogEndMin);

    return true;
}

bool RainOverlay::getDaylightMultiplier(float& outMultiplier) const {
    if (!settings_.daylightReductionEnabled || intensity_ == 0) {
        static bool loggedOnce = false;
        if (!loggedOnce) {
            LOG_DEBUG(MOD_GRAPHICS, "RainOverlay::getDaylightMultiplier: skipped (enabled={}, intensity={})",
                      settings_.daylightReductionEnabled, intensity_);
            loggedOnce = true;
        }
        return false;
    }

    // Interpolate daylight based on intensity (1-10)
    // Higher intensity = darker (lower multiplier)
    // Intensity 1 -> daylightMax (50%), Intensity 10 -> daylightMin (5%)
    float t = (intensity_ - 1) / 9.0f;  // 0 at intensity 1, 1 at intensity 10

    outMultiplier = settings_.daylightMax - t * (settings_.daylightMax - settings_.daylightMin);

    static uint8_t lastLoggedIntensity = 255;
    if (intensity_ != lastLoggedIntensity) {
        LOG_DEBUG(MOD_GRAPHICS, "RainOverlay::getDaylightMultiplier: intensity={}, t={:.3f}, daylightMin={:.3f}, daylightMax={:.3f}, result={:.3f}",
                  intensity_, t, settings_.daylightMin, settings_.daylightMax, outMultiplier);
        lastLoggedIntensity = intensity_;
    }

    return true;
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
