#include "client/graphics/constrained_renderer_config.h"
#include "common/logging.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>

#include <fstream>
#include <json/json.h>

namespace EQT {
namespace Graphics {

void ConstrainedRendererConfig::calculateMemoryLimits() {
    if (totalMemoryBudgetBytes > 0) {
        lazyPfsLoading = true;
        releaseTextureDataAfterUpload = true;

        // Sound buffer cache: min(8MB, totalBudget/16)
        size_t eightMB = 8 * 1024 * 1024;
        soundBufferCacheBytes = std::min(eightMB, totalMemoryBudgetBytes / 16);

        // Mesh cache: derive from total if not explicitly set by preset
        if (meshMemoryBytes == 0) {
            meshMemoryBytes = totalMemoryBudgetBytes / 5;
        }

        // Default: keep at most 4 other-zone _chr.s3d caches (overridable via JSON)
        if (chrCacheMaxEntries == 0) {
            chrCacheMaxEntries = 4;
        }
    }
}

void ConstrainedRendererConfig::calculateMaxResolution() {
    // Calculate bytes per pixel for framebuffer
    // Front buffer + back buffer + depth-stencil
    int colorBytes = colorDepthBits / 8;
    int depthStencilBytes = enableStencilBuffer ? 4 : 2;  // D24S8 or D16
    int bytesPerPixel = (2 * colorBytes) + depthStencilBytes;  // front + back + depth-stencil

    // Calculate max pixels that fit in framebuffer memory
    size_t maxPixels = framebufferMemoryBytes / bytesPerPixel;

    // Calculate max resolution assuming 4:3 aspect ratio
    // width * height = maxPixels
    // width / height = 4/3
    // width = height * 4/3
    // height * 4/3 * height = maxPixels
    // height^2 = maxPixels * 3/4
    // height = sqrt(maxPixels * 3/4)
    double height = std::sqrt(static_cast<double>(maxPixels) * 3.0 / 4.0);
    double width = height * 4.0 / 3.0;

    // Round down to nearest multiple of 8 (common for graphics hardware)
    maxResolutionWidth = (static_cast<int>(width) / 8) * 8;
    maxResolutionHeight = (static_cast<int>(height) / 8) * 8;

    // Ensure we don't exceed memory with the rounded values
    while (calculateFramebufferUsage(maxResolutionWidth, maxResolutionHeight) > framebufferMemoryBytes) {
        maxResolutionWidth -= 8;
        maxResolutionHeight = (maxResolutionWidth * 3) / 4;
    }
}

bool ConstrainedRendererConfig::clampResolution(int& width, int& height) const {
    bool clamped = false;

    // Check if requested resolution exceeds max
    if (width > maxResolutionWidth) {
        width = maxResolutionWidth;
        clamped = true;
    }
    if (height > maxResolutionHeight) {
        height = maxResolutionHeight;
        clamped = true;
    }

    // Also check total pixel count (for non-4:3 aspect ratios)
    size_t requestedUsage = calculateFramebufferUsage(width, height);
    if (requestedUsage > framebufferMemoryBytes) {
        // Scale down proportionally
        double scale = std::sqrt(static_cast<double>(framebufferMemoryBytes) / requestedUsage);
        width = (static_cast<int>(width * scale) / 8) * 8;
        height = (static_cast<int>(height * scale) / 8) * 8;
        clamped = true;
    }

    return clamped;
}

size_t ConstrainedRendererConfig::calculateFramebufferUsage(int width, int height) const {
    int colorBytes = colorDepthBits / 8;
    // Depth-stencil: D24S8 (4 bytes) when stencil enabled, D16 (2 bytes) otherwise
    int depthStencilBytes = enableStencilBuffer ? 4 : 2;
    int bytesPerPixel = (2 * colorBytes) + depthStencilBytes;  // front + back + depth-stencil
    return static_cast<size_t>(width) * static_cast<size_t>(height) * bytesPerPixel;
}

std::string ConstrainedRendererConfig::presetName(ConstrainedRenderingPreset preset) {
    switch (preset) {
        case ConstrainedRenderingPreset::Voodoo1: return "Voodoo1";
        case ConstrainedRenderingPreset::Voodoo2: return "Voodoo2";
        case ConstrainedRenderingPreset::TNT:      return "TNT";
        case ConstrainedRenderingPreset::OrangePi: return "OrangePi";
        case ConstrainedRenderingPreset::Custom:  return "Custom";
        default:                                   return "Unknown";
    }
}

ConstrainedRendererConfig ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset preset) {
    ConstrainedRendererConfig config;

    switch (preset) {
        case ConstrainedRenderingPreset::Voodoo1:
            // 3dfx Voodoo 1: 2MB FBI + 2MB TMU
            // Very constrained - use 64x64 textures to fit ~128 textures in 2MB
            config.renderingBackend = RenderingBackend::Software;
            config.framebufferMemoryBytes = 2 * 1024 * 1024;  // 2MB
            config.textureMemoryBytes = 2 * 1024 * 1024;      // 2MB
            config.colorDepthBits = 16;
            config.maxTextureDimension = 64;  // 16KB per texture = 128 max
            // Render distance and geometry budgets
            config.clipDistance = 300.0f;
            config.entityRenderDistance = 150.0f;
            config.maxVisibleEntities = 30;
            config.maxPolygonsPerFrame = 30000;
            // System RAM budget
            config.totalMemoryBudgetBytes = 32 * 1024 * 1024;  // 32MB
            config.meshMemoryBytes = 4 * 1024 * 1024;  // 4MB mesh cache
            config.backgroundThreadCount = 1;
            config.targetFps = 30.0f;
            break;

        case ConstrainedRenderingPreset::Voodoo2:
            // 3dfx Voodoo 2: 4MB FBI + 8MB TMU (with SLI could be more)
            // Use 128x128 textures to fit ~128 textures in 8MB
            config.renderingBackend = RenderingBackend::Software;
            config.framebufferMemoryBytes = 4 * 1024 * 1024;  // 4MB
            config.textureMemoryBytes = 8 * 1024 * 1024;      // 8MB
            config.colorDepthBits = 16;
            config.maxTextureDimension = 128;  // 64KB per texture = 128 max
            // Render distance and geometry budgets
            config.clipDistance = 500.0f;
            config.entityRenderDistance = 250.0f;
            config.maxVisibleEntities = 50;
            config.maxPolygonsPerFrame = 50000;
            // System RAM budget
            config.totalMemoryBudgetBytes = 64 * 1024 * 1024;  // 64MB
            config.meshMemoryBytes = 8 * 1024 * 1024;  // 8MB mesh cache
            config.backgroundThreadCount = 1;
            config.targetFps = 30.0f;
            break;

        case ConstrainedRenderingPreset::TNT:
            // NVIDIA RIVA TNT: unified memory, typically 16MB total
            config.renderingBackend = RenderingBackend::OpenGL;
            config.framebufferMemoryBytes = 8 * 1024 * 1024;   // 8MB for framebuffer
            config.textureMemoryBytes = 16 * 1024 * 1024;      // 16MB for textures
            config.colorDepthBits = 16;
            config.maxTextureDimension = 512;
            // Render distance and geometry budgets
            config.clipDistance = 800.0f;
            config.entityRenderDistance = 400.0f;
            config.maxVisibleEntities = 75;
            config.maxPolygonsPerFrame = 100000;
            // GPU feature flags
            config.enableMipmaps = true;
            config.enableShaders = true;
            // System RAM budget
            config.totalMemoryBudgetBytes = 128 * 1024 * 1024;  // 128MB
            config.meshMemoryBytes = 16 * 1024 * 1024;  // 16MB mesh cache
            config.backgroundThreadCount = 2;
            config.targetFps = 60.0f;
            break;

        case ConstrainedRenderingPreset::OrangePi:
            // Orange Pi One: Allwinner H3, Mali 400 (Lima/Mesa GL 2.1), 512MB shared RAM
            // 10MB framebuffer: 1280x720 @ 16-bit + D24S8 = 7.03MB + headroom for FBO RTTs
            // (MSAA is free on tile-based Mali 400 — resolved in on-chip tile SRAM)
            // 64MB texture budget: 512x512 textures with mipmaps = ~349KB each, ~187 textures
#ifdef EQT_HAS_GLES2
            config.renderingBackend = RenderingBackend::GLES2;
#else
            config.renderingBackend = RenderingBackend::OpenGL;
#endif
            config.useDRM = true;
            config.framebufferMemoryBytes = 10 * 1024 * 1024;  // 10MB
            config.textureMemoryBytes = 64 * 1024 * 1024;      // 64MB
            config.colorDepthBits = 16;
            config.maxTextureDimension = 512;
            // Render distance and geometry budgets
            config.clipDistance = 300.0f;
            config.entityRenderDistance = 300.0f;
            config.maxVisibleEntities = 40;
            config.maxPolygonsPerFrame = 80000;
            // Software occlusion culling disabled — portal BFS walk handles
            // entity visibility, and PVS + frustum handles region visibility.
            // The CPU rasterizer costs 130ms+ per Tier2 frame on ARM.
            config.occlusionBufferWidth = 0;
            config.occlusionBufferHeight = 0;
            config.occlusionMaxOccluderRegions = 0;
            // GPU feature flags (Mali 400 via Lima supports these)
            config.enableMipmaps = true;
            config.enableNPOT = true;
            config.enableStencilBuffer = true;
            config.enableAlphaToCoverage = true;
            config.enableShaders = true;
            config.enableCompressedTextures = false;  // Mali 400 via Lima software-decodes S3TC; no GPU savings, extra CPU cost
            config.enableTextureAtlas = true;  // Use ETC1-compressed atlas files (Mali 400 hardware ETC1 decode)
            config.antiAliasLevel = 4;
            config.anisotropicFilterLevel = 4;
            // System RAM budget
            config.totalMemoryBudgetBytes = 128 * 1024 * 1024;  // 128MB
            config.meshMemoryBytes = 24 * 1024 * 1024;  // 24MB mesh cache
            // Deferred asset loading (progressive mesh building during gameplay)
            config.deferredAssetLoading = true;
            config.enableItemIcons = false;
            config.skipObjectBuild = true;
            config.backgroundThreadCount = 1;
            config.targetFps = 30.0f;
            // Entity prep: queue entities up to 2 portal hops away
            config.entityPrepMaxPvsDepth = 2;
            config.terrainPrepMaxPvsDepth = 5;
            config.objectPrepMaxPvsDepth = 3;
            config.pvsNeighborhoodHops = 3;
            // Use lower-poly WLD dome mesh instead of procedural hemisphere
            config.skyDomeMode = SkyDomeMode::Original;
            break;

        case ConstrainedRenderingPreset::Custom:
        default:
            // Custom: use default values, caller should override
            break;
    }

    // Calculate derived limits
    config.calculateMaxResolution();
    config.calculateMemoryLimits();

    return config;
}

ConstrainedRenderingPreset ConstrainedRendererConfig::parsePreset(const std::string& name) {
    // Convert to lowercase for case-insensitive comparison
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower == "voodoo1" || lower == "voodoo" || lower == "v1") {
        return ConstrainedRenderingPreset::Voodoo1;
    }
    if (lower == "voodoo2" || lower == "v2") {
        return ConstrainedRenderingPreset::Voodoo2;
    }
    if (lower == "tnt" || lower == "riva" || lower == "rivatnt") {
        return ConstrainedRenderingPreset::TNT;
    }
    if (lower == "orangepi" || lower == "opi") {
        return ConstrainedRenderingPreset::OrangePi;
    }
    if (lower == "custom") {
        return ConstrainedRenderingPreset::Custom;
    }
    if (lower == "max" || lower == "none" || lower == "off" || lower == "disabled") {
        LOG_WARN(MOD_GRAPHICS, "Preset '{}' is deprecated, mapping to OrangePi", name);
        return ConstrainedRenderingPreset::OrangePi;
    }

    // Unrecognized preset name — default to OrangePi
    if (!lower.empty()) {
        LOG_WARN(MOD_GRAPHICS, "Unknown preset '{}', defaulting to OrangePi", name);
    }
    return ConstrainedRenderingPreset::OrangePi;
}

bool ConstrainedRendererConfig::parseMemorySpec(const std::string& spec, ConstrainedRendererConfig& outConfig) {
    int totalMB = 0, texMB = 0, fbMB = 0;

    // Parse NxNxN pattern (case-insensitive 'x' or 'X')
    if (std::sscanf(spec.c_str(), "%dx%dx%d", &totalMB, &texMB, &fbMB) != 3 &&
        std::sscanf(spec.c_str(), "%dX%dX%d", &totalMB, &texMB, &fbMB) != 3) {
        return false;
    }

    if (totalMB <= 0 || texMB <= 0 || fbMB <= 0) {
        return false;
    }

    outConfig.totalMemoryBudgetBytes = static_cast<size_t>(totalMB) * 1024 * 1024;
    outConfig.textureMemoryBytes = static_cast<size_t>(texMB) * 1024 * 1024;
    outConfig.framebufferMemoryBytes = static_cast<size_t>(fbMB) * 1024 * 1024;

    // Set sensible defaults based on total memory budget
    if (totalMB <= 64) {
        outConfig.colorDepthBits = 16;
        outConfig.maxTextureDimension = 64;
        outConfig.clipDistance = 300.0f;
        outConfig.entityRenderDistance = 150.0f;
        outConfig.maxVisibleEntities = 30;
        outConfig.maxPolygonsPerFrame = 30000;
    } else if (totalMB <= 128) {
        outConfig.colorDepthBits = 16;
        outConfig.maxTextureDimension = 128;
        outConfig.clipDistance = 400.0f;
        outConfig.entityRenderDistance = 200.0f;
        outConfig.maxVisibleEntities = 40;
        outConfig.maxPolygonsPerFrame = 40000;
    } else if (totalMB <= 256) {
        outConfig.colorDepthBits = 16;
        outConfig.maxTextureDimension = 256;
        outConfig.clipDistance = 600.0f;
        outConfig.entityRenderDistance = 300.0f;
        outConfig.maxVisibleEntities = 60;
        outConfig.maxPolygonsPerFrame = 75000;
    } else {
        outConfig.colorDepthBits = 16;
        outConfig.maxTextureDimension = 512;
        outConfig.clipDistance = 800.0f;
        outConfig.entityRenderDistance = 400.0f;
        outConfig.maxVisibleEntities = 75;
        outConfig.maxPolygonsPerFrame = 100000;
    }

    outConfig.calculateMaxResolution();
    outConfig.calculateMemoryLimits();
    return true;
}

bool ConstrainedRendererConfig::loadJsonOverrides(const std::string& presetName,
                                                   const std::string& jsonPath) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        return false;
    }

    if (!root.isMember("presets") || !root["presets"].isObject()) {
        return false;
    }

    // Case-insensitive preset lookup
    std::string lowerName = presetName;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    const Json::Value& presets = root["presets"];
    Json::Value preset;
    for (const auto& key : presets.getMemberNames()) {
        std::string lowerKey = key;
        std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (lowerKey == lowerName) {
            preset = presets[key];
            break;
        }
    }

    if (preset.isNull()) {
        return false;
    }

    // Override fields that are present in the JSON
    if (preset.isMember("maxTextureDimension"))
        maxTextureDimension = preset["maxTextureDimension"].asInt();
    if (preset.isMember("maxPolygonsPerFrame"))
        maxPolygonsPerFrame = preset["maxPolygonsPerFrame"].asInt();
    if (preset.isMember("clipDistance"))
        clipDistance = preset["clipDistance"].asFloat();
    if (preset.isMember("entityRenderDistance"))
        entityRenderDistance = preset["entityRenderDistance"].asFloat();
    if (preset.isMember("framebufferMemoryBytes"))
        framebufferMemoryBytes = static_cast<size_t>(preset["framebufferMemoryBytes"].asUInt64());
    if (preset.isMember("textureMemoryBytes"))
        textureMemoryBytes = static_cast<size_t>(preset["textureMemoryBytes"].asUInt64());
    if (preset.isMember("colorDepthBits"))
        colorDepthBits = preset["colorDepthBits"].asInt();
    if (preset.isMember("maxVisibleEntities"))
        maxVisibleEntities = preset["maxVisibleEntities"].asInt();
    if (preset.isMember("fogStartRatio"))
        fogStartRatio = preset["fogStartRatio"].asFloat();
    if (preset.isMember("fogEndRatio"))
        fogEndRatio = preset["fogEndRatio"].asFloat();
    if (preset.isMember("enableMipmaps"))
        enableMipmaps = preset["enableMipmaps"].asBool();
    if (preset.isMember("enableCompressedTextures"))
        enableCompressedTextures = preset["enableCompressedTextures"].asBool();
    if (preset.isMember("enableNPOT"))
        enableNPOT = preset["enableNPOT"].asBool();
    if (preset.isMember("enableStencilBuffer"))
        enableStencilBuffer = preset["enableStencilBuffer"].asBool();
    if (preset.isMember("enableAlphaToCoverage"))
        enableAlphaToCoverage = preset["enableAlphaToCoverage"].asBool();
    if (preset.isMember("enableShaders"))
        enableShaders = preset["enableShaders"].asBool();
    if (preset.isMember("enableTextureAtlas"))
        enableTextureAtlas = preset["enableTextureAtlas"].asBool();
    if (preset.isMember("skipManualZoneDraw"))
        skipManualZoneDraw = preset["skipManualZoneDraw"].asBool();
    if (preset.isMember("skipVBOUpload"))
        skipVBOUpload = preset["skipVBOUpload"].asBool();
    if (preset.isMember("skipEntityTextureUpload"))
        skipEntityTextureUpload = preset["skipEntityTextureUpload"].asBool();
    if (preset.isMember("skipEntityBuild"))
        skipEntityBuild = preset["skipEntityBuild"].asBool();
    if (preset.isMember("skipObjectBuild"))
        skipObjectBuild = preset["skipObjectBuild"].asBool();
    if (preset.isMember("skipConstrainedTextureUpload"))
        skipConstrainedTextureUpload = preset["skipConstrainedTextureUpload"].asBool();
    if (preset.isMember("skipSkyTextureUpload"))
        skipSkyTextureUpload = preset["skipSkyTextureUpload"].asBool();
    if (preset.isMember("atlasPath"))
        atlasPath = preset["atlasPath"].asString();
    if (preset.isMember("antiAliasLevel"))
        antiAliasLevel = preset["antiAliasLevel"].asInt();
    if (preset.isMember("anisotropicFilterLevel"))
        anisotropicFilterLevel = preset["anisotropicFilterLevel"].asInt();
    if (preset.isMember("mesh_memory_mb"))
        meshMemoryBytes = static_cast<size_t>(preset["mesh_memory_mb"].asUInt64()) * 1024 * 1024;
    if (preset.isMember("meshMemoryBytes"))
        meshMemoryBytes = static_cast<size_t>(preset["meshMemoryBytes"].asUInt64());
    if (preset.isMember("targetFps"))
        targetFps = preset["targetFps"].asFloat();
    if (preset.isMember("entityPrepMaxPvsDepth"))
        entityPrepMaxPvsDepth = preset["entityPrepMaxPvsDepth"].asInt();
    if (preset.isMember("terrainPrepMaxPvsDepth"))
        terrainPrepMaxPvsDepth = preset["terrainPrepMaxPvsDepth"].asInt();
    if (preset.isMember("objectPrepMaxPvsDepth"))
        objectPrepMaxPvsDepth = preset["objectPrepMaxPvsDepth"].asInt();
    if (preset.isMember("pvsNeighborhoodHops"))
        pvsNeighborhoodHops = preset["pvsNeighborhoodHops"].asInt();
    if (preset.isMember("deferredAssetLoading")) {
        std::string mode = preset["deferredAssetLoading"].asString();
        std::string lowerMode = mode;
        std::transform(lowerMode.begin(), lowerMode.end(), lowerMode.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        deferredAssetLoading = (lowerMode == "automatic" || lowerMode == "auto" || lowerMode == "true" || lowerMode == "1");
    }
    if (preset.isMember("backgroundThreadCount"))
        backgroundThreadCount = preset["backgroundThreadCount"].asInt();

    // Rendering backend
    if (preset.isMember("renderingBackend")) {
        std::string backend = preset["renderingBackend"].asString();
        std::string lowerBackend = backend;
        std::transform(lowerBackend.begin(), lowerBackend.end(), lowerBackend.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (lowerBackend == "software") renderingBackend = RenderingBackend::Software;
        else if (lowerBackend == "opengl" || lowerBackend == "gl") renderingBackend = RenderingBackend::OpenGL;
        else if (lowerBackend == "gles2" || lowerBackend == "opengles2") renderingBackend = RenderingBackend::GLES2;
    } else if (preset.isMember("useGLES2") && preset["useGLES2"].asBool()) {
        // Backward compatibility: "useGLES2": true → GLES2 backend
        renderingBackend = RenderingBackend::GLES2;
    }

    // Display mode
    if (preset.isMember("useDRM"))
        useDRM = preset["useDRM"].asBool();

    // Rendering feature toggles
    if (preset.isMember("fog"))
        fog = preset["fog"].asBool();
    if (preset.isMember("wireframe"))
        wireframe = preset["wireframe"].asBool();
    if (preset.isMember("frontToBackSorting"))
        frontToBackSorting = preset["frontToBackSorting"].asBool();
    if (preset.isMember("portalOcclusion"))
        portalOcclusion = preset["portalOcclusion"].asBool();
    if (preset.isMember("playerLight"))
        playerLight = preset["playerLight"].asBool();
    if (preset.isMember("objectLights"))
        objectLights = preset["objectLights"].asBool();
    if (preset.isMember("directionalLight"))
        directionalLight = preset["directionalLight"].asBool();
    if (preset.isMember("fireEffects"))
        fireEffects = preset["fireEffects"].asBool();
    if (preset.isMember("skyRendering"))
        skyRendering = preset["skyRendering"].asBool();
    if (preset.isMember("skyTextures")) {
        std::string val = preset["skyTextures"].asString();
        if (val == "original") skyDomeMode = SkyDomeMode::Original;
        else if (val == "procedural") skyDomeMode = SkyDomeMode::Procedural;
    }
    if (preset.isMember("nameTagsEnabled"))
        nameTagsEnabled = preset["nameTagsEnabled"].asBool();
    if (preset.isMember("frameTimingEnabled"))
        frameTimingEnabled = preset["frameTimingEnabled"].asBool();
    if (preset.isMember("enableItemIcons"))
        enableItemIcons = preset["enableItemIcons"].asBool();
    if (preset.isMember("chrCacheMaxEntries"))
        chrCacheMaxEntries = static_cast<size_t>(preset["chrCacheMaxEntries"].asUInt());

    return true;
}

} // namespace Graphics
} // namespace EQT
