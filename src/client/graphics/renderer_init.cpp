#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/renderer_zone_merge.h"  // ZoneMergeBuffers complete type (unique_ptr member destruction)
#include "client/willeq_database.h"
#include "client/graphics/loading_thread.h"
#include "client/graphics/entity_prep_worker.h"
#include "client/graphics/eqe1_loader.h"
#include <filesystem>
#include "client/graphics/eq/dds_decoder.h"
#include "client/graphics/eq/equipment_textures.h"
#include "client/graphics/frame_budget_governor.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/light_source.h"
#include "client/graphics/weather_effects_controller.h"
#include "client/graphics/weather_config_loader.h"
#include "client/graphics/weather_quality_preset.h"
#include "client/graphics/environment/spell_effects_config.h"
#include "client/graphics/environment/zone_biome.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/race_codes.h"
#include "client/inventory/inventory_manager.h"
#include "client/graphics/spell_visual_fx.h"
#include "client/graphics/text_batch.h"
#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/character_model_view.h"
#include "client/graphics/ui/inventory_layout.h"
#include "client/chat/chat_message_buffer.h"
#include "client/graphics/sky_renderer.h"
#include "client/graphics/eq/sky_loader.h"
#include "client/graphics/sky_config.h"
#include "client/zone_lines.h"
#include "client/hc_map.h"
#ifdef EQT_HAS_NAVMESH
#include "client/pathfinder_nav_mesh.h"
#endif
#include "client/bridge/game_state_bridge.h"
#include "client/state/event_bus.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/detail/detail_manager.h"
#include "client/graphics/detail/detail_types.h"
#include "client/graphics/detail/seasonal_controller.h"
#include "common/logging.h"
#include <fmt/format.h>
#include "common/name_utils.h"
#include "common/performance_metrics.h"
#include <cstdio>
#ifdef __linux__
#include <unistd.h>
#include <sched.h>
#include <pthread.h>
#endif

#ifdef WITH_RDP
#include "client/graphics/rdp/rdp_server.h"
#include "client/graphics/rdp/rdp_input_handler.h"
#endif
#ifdef EQT_HAS_DRM
#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#include <EGL/eglext.h>
#else
#include <GL/gl.h>
#endif
#include <EGL/egl.h>
#endif

// Bridge functions for GLES2 static VBO management.
// Defined in COpenGLES2Driver.cpp (compiled into Irrlicht library), declared here
// to avoid including the full COpenGLES2Driver.h (which depends on Irrlicht-internal headers).
#ifdef EQT_HAS_GLES2
extern void gles2DeleteAllStaticHWBuffers(void* driver);
extern size_t gles2GetHWBufferMemoryUsage(void* driver);
extern size_t gles2GetHWBufferCount(void* driver);
extern size_t gles2GetGpuTextureMemoryUsage(void* driver);
extern void gles2Draw3DLinesBatch(void* driver, const float* vertices, unsigned int lineCount);
extern void* gles2WrapTexture(void* driver, const char* name, unsigned int glTexName,
                               unsigned int width, unsigned int height,
                               unsigned int gpuBytes = 0);
extern void gles2RegisterExternalHWBuffer(void* driver, const void* meshBuffer,
                                           unsigned int vbo, unsigned int ebo,
                                           unsigned int vertexCount, unsigned int indexCount,
                                           int vertexType);
#endif
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <json/json.h>
#include <sstream>
#include <cmath>
#include <limits>
#include <set>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Stub definition for WindowManager — class has been deleted but unique_ptr member
// remains in the header (forward-declared). This allows the unique_ptr destructor to compile.
namespace eqt { namespace ui { class WindowManager { public: virtual ~WindowManager() = default; }; } }

namespace EQT {
namespace Graphics {

// Read display settings directly from config/display_settings.json
// This file is optional — missing file uses defaults (everything enabled)
static eqt::ui::DisplaySettings loadDisplaySettingsFromFile() {
    eqt::ui::DisplaySettings settings;  // defaults: everything enabled

    std::ifstream file("config/display_settings.json");
    if (!file.is_open()) {
        LOG_INFO(MOD_GRAPHICS, "No display_settings.json found, using defaults");
        return settings;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        LOG_WARN(MOD_GRAPHICS, "Failed to parse display_settings.json: {}", errors);
        return settings;
    }

    if (root.isMember("environmentEffects")) {
        const Json::Value& env = root["environmentEffects"];
        settings.atmosphericParticles = env.get("atmosphericParticles", true).asBool();
        settings.ambientCreatures = env.get("ambientCreatures", true).asBool();
        settings.rollingObjects = env.get("rollingObjects", true).asBool();
        settings.skyEnabled = env.get("skyEnabled", true).asBool();
        settings.animatedTrees = env.get("animatedTrees", true).asBool();
        settings.fireEffects = env.get("fireEffects", true).asBool();
    }
    if (root.isMember("detailObjects")) {
        const Json::Value& detail = root["detailObjects"];
        settings.detailObjectsEnabled = detail.get("enabled", true).asBool();
    }

    return settings;
}

// Log detailed OpenGL/driver information for debugging GPU issues
static void logDriverDetails(irr::video::IVideoDriver* driver, irr::IrrlichtDevice* device,
                             const irr::SIrrlichtCreationParameters& params) {
    if (!driver || !device) return;

    // Driver name and type
    const wchar_t* driverName = driver->getName();
    std::wstring wname(driverName);
    std::string name(wname.begin(), wname.end());
    LOG_INFO(MOD_GRAPHICS, "Video driver: {}", name);

    // Requested vs actual parameters
    auto screenSize = driver->getScreenSize();
    LOG_DEBUG(MOD_GRAPHICS, "[GL] Screen size: {}x{} (requested {}x{})",
              screenSize.Width, screenSize.Height,
              params.WindowSize.Width, params.WindowSize.Height);

    // Current color format
    auto colorFormat = driver->getColorFormat();
    const char* fmtName = "unknown";
    switch (colorFormat) {
        case irr::video::ECF_A1R5G5B5: fmtName = "A1R5G5B5 (16-bit)"; break;
        case irr::video::ECF_R5G6B5:   fmtName = "R5G6B5 (16-bit)"; break;
        case irr::video::ECF_R8G8B8:   fmtName = "R8G8B8 (24-bit)"; break;
        case irr::video::ECF_A8R8G8B8: fmtName = "A8R8G8B8 (32-bit)"; break;
        default: break;
    }
    LOG_DEBUG(MOD_GRAPHICS, "[GL] Color format: {} (enum={})", fmtName, static_cast<int>(colorFormat));

    // Max texture size
    auto maxTexSize = driver->getMaxTextureSize();
    LOG_DEBUG(MOD_GRAPHICS, "[GL] Max texture size: {}x{}", maxTexSize.Width, maxTexSize.Height);

    // Feature support
    LOG_DEBUG(MOD_GRAPHICS, "[GL] Feature support:");
    LOG_DEBUG(MOD_GRAPHICS, "[GL]   Hardware TL: {}", driver->queryFeature(irr::video::EVDF_HARDWARE_TL) ? "yes" : "no");
    LOG_DEBUG(MOD_GRAPHICS, "[GL]   Multitexture: {}", driver->queryFeature(irr::video::EVDF_MULTITEXTURE) ? "yes" : "no");
    LOG_DEBUG(MOD_GRAPHICS, "[GL]   Bilinear filter: {}", driver->queryFeature(irr::video::EVDF_BILINEAR_FILTER) ? "yes" : "no");
    LOG_DEBUG(MOD_GRAPHICS, "[GL]   MipMap: {}", driver->queryFeature(irr::video::EVDF_MIP_MAP) ? "yes" : "no");
    LOG_DEBUG(MOD_GRAPHICS, "[GL]   Stencil buffer: {}", driver->queryFeature(irr::video::EVDF_STENCIL_BUFFER) ? "yes" : "no");
    LOG_DEBUG(MOD_GRAPHICS, "[GL]   Vertex shader 1.1: {}", driver->queryFeature(irr::video::EVDF_VERTEX_SHADER_1_1) ? "yes" : "no");
    LOG_DEBUG(MOD_GRAPHICS, "[GL]   Pixel shader 1.1: {}", driver->queryFeature(irr::video::EVDF_PIXEL_SHADER_1_1) ? "yes" : "no");
    LOG_DEBUG(MOD_GRAPHICS, "[GL]   Render to target: {}", driver->queryFeature(irr::video::EVDF_RENDER_TO_TARGET) ? "yes" : "no");
    LOG_DEBUG(MOD_GRAPHICS, "[GL]   Alpha to coverage: {}", driver->queryFeature(irr::video::EVDF_ALPHA_TO_COVERAGE) ? "yes" : "no");
    LOG_DEBUG(MOD_GRAPHICS, "[GL]   Texture NPOT: {}", driver->queryFeature(irr::video::EVDF_TEXTURE_NPOT) ? "yes" : "no");
    LOG_DEBUG(MOD_GRAPHICS, "[GL]   Framebuffer object: {}", driver->queryFeature(irr::video::EVDF_FRAMEBUFFER_OBJECT) ? "yes" : "no");

    // Vendor info from Irrlicht (only for OpenGL driver)
    auto vendorInfo = driver->getVendorInfo();
    if (vendorInfo.size() > 0) {
        std::string vendorStr(vendorInfo.c_str());
        LOG_DEBUG(MOD_GRAPHICS, "[GL] Vendor info: {}", vendorStr);
    }

    // Available video modes
    auto* videoModeList = device->getVideoModeList();
    if (videoModeList) {
        int modeCount = videoModeList->getVideoModeCount();
        LOG_DEBUG(MOD_GRAPHICS, "[GL] Desktop resolution: {}x{} @{}bpp",
                  videoModeList->getDesktopResolution().Width,
                  videoModeList->getDesktopResolution().Height,
                  videoModeList->getDesktopDepth());
        LOG_DEBUG(MOD_GRAPHICS, "[GL] Available video modes: {}", modeCount);
        for (int i = 0; i < modeCount && i < 10; ++i) {
            auto res = videoModeList->getVideoModeResolution(i);
            int depth = videoModeList->getVideoModeDepth(i);
            LOG_TRACE(MOD_GRAPHICS, "[GL]   Mode {}: {}x{} @{}bpp", i, res.Width, res.Height, depth);
        }
    }

    // Primitive counts
    LOG_DEBUG(MOD_GRAPHICS, "[GL] Max primitive count: {}", driver->getMaximalPrimitiveCount());

    // DISPLAY environment variable (important for X11/GLX)
    const char* display = std::getenv("DISPLAY");
    LOG_DEBUG(MOD_GRAPHICS, "[GL] DISPLAY env: {}", display ? display : "(not set)");
}

// --- IrrlichtRenderer Implementation ---

// L01 (Batch L, refactor/L_renderer_component_extraction.md): the ctor had no
// initialiser list before this unit. RendererContext holds references, which
// forces one. The order below is the declaration order of RendererContext in
// include/client/graphics/renderer_context.h -- aggregate initialisation, so
// it must match exactly.
IrrlichtRenderer::IrrlichtRenderer()
    : ctx_{
        config_, entityRenderer_, driver_, constrainedTextureCache_, smgr_, zoneShader_,
        regionMeshNodes_, doorManager_, zoneBspTree_, objectNodes_, currentPvsRegion_, camera_,
        weatherEffects_, device_, currentZone_, skyRenderer_, playerX_, playerY_, playerZ_,
        particleManager_, detailManager_, currentZoneName_, boidsManager_,
        regionBoundingBoxes_, manualZoneDrawEnabled_, progressiveLoadingActive_,
        portalOcclusionEnabled_, fogEnabled_, usePvsCulling_, entityPrepReady_, wireframeMode_,
        portalOcclusionEligible_, lightingEnabled_, fireEffectsEnabled_, eventReceiver_,
        cameraController_, objectLights_, collisionMap_, animatedTextureManager_,
        objectInSceneGraph_, portalSystem_, renderDistance_, frustumCuller_, governor_,
        bridge_,
#ifdef EQT_HAS_GLES2
        gpuUploadThread_,
#endif
        tumbleweedManager_, entityPrepWorker_, occlusionCuller_, indoorRegions_,
        treeWindController_, currentHour_, currentMinute_, objectPositions_
      } {
    // L04 (Batch L): the diagnostics family.
    diagnostics_ = std::make_unique<DiagnosticsComponent>(ctx_, *this);
#ifdef WITH_RDP
    rdp_ = std::make_unique<RdpComponent>(ctx_, *this);
#endif
    eqe1_ = std::make_unique<Eqe1ArchiveComponent>(ctx_, *this);
    // L03 (Batch L): the overlays family.
    overlays_ = std::make_unique<OverlaysComponent>(ctx_, *this);
    // L05 (Batch L): the player family -- movement, collision, camera.
    playerCamera_ = std::make_unique<PlayerCameraComponent>(ctx_, *this);
    // L06 (Batch L): the slash family -- registry 3.
    slash_ = std::make_unique<SlashCommandsComponent>(ctx_, *this);
    // L07 (Batch L): the SimulationWorker bridge.
    simBridge_ = std::make_unique<SimBridgeComponent>(ctx_, *this);
}

IrrlichtRenderer::~IrrlichtRenderer() {
    shutdown();
}

bool IrrlichtRenderer::initLoadingScreen(const RendererConfig& config) {
    config_ = config;

    // Apply constrained rendering configuration
    // Constrained mode is always active; Max preset has no practical limits
    // Populate config from preset (unless custom NxNxN config was already set)
    if (config_.constrainedPreset != ConstrainedRenderingPreset::Custom) {
        config_.constrainedConfig = ConstrainedRendererConfig::fromPreset(config_.constrainedPreset);
    }
    config_.constrainedConfig.calculateMaxResolution();
    config_.constrainedConfig.calculateMemoryLimits();
    // In automatic deferred loading mode, mark progressive loading active from the start
    // so the loading screen stays visible until all assets are built.
    if (config_.constrainedConfig.deferredAssetLoading) {
        progressiveLoadingActive_ = true;
    }
    if (config_.constrainedConfig.clampResolution(config_.width, config_.height)) {
        LOG_WARN(MOD_GRAPHICS, "Resolution clamped to {}x{} (framebuffer memory limit: {} bytes)",
                 config_.width, config_.height, config_.constrainedConfig.framebufferMemoryBytes);
    }
    LOG_INFO(MOD_GRAPHICS, "Constrained rendering mode: {} ({}x{}, {}MB texture, {}MB framebuffer, {}MB RAM budget)",
             ConstrainedRendererConfig::presetName(config_.constrainedPreset),
             config_.width, config_.height,
             config_.constrainedConfig.textureMemoryBytes / (1024 * 1024),
             config_.constrainedConfig.framebufferMemoryBytes / (1024 * 1024),
             config_.constrainedConfig.totalMemoryBudgetBytes / (1024 * 1024));
    // Create frame budget governor
    if (!governor_) {
        LOG_TRACE(MOD_GRAPHICS, "Allocating FrameBudgetGovernor...");
        float targetFps = config_.constrainedConfig.targetFps;
        governor_ = std::make_unique<FrameBudgetGovernor>(targetFps);
        LOG_INFO(MOD_GRAPHICS, "Frame budget governor: target {:.0f} FPS ({:.1f}ms budget)",
                 targetFps, governor_->getTargetFrameTimeMs());
    }

    // Create shared background thread pool
    if (!backgroundThreadPool_) {
        LOG_TRACE(MOD_GRAPHICS, "Allocating BackgroundThreadPool ({} threads)...",
                  config_.constrainedConfig.backgroundThreadCount);
        backgroundThreadPool_ = std::make_unique<BackgroundThreadPool>(
            config_.constrainedConfig.backgroundThreadCount);
        LOG_INFO(MOD_GRAPHICS, "Background thread pool: {} thread(s)",
                 config_.constrainedConfig.backgroundThreadCount);
    }

    // One-time file I/O at app startup (before render loop begins).
    // Caches display settings so render-thread code never reads JSON.
    if (!displaySettingsCached_) {
        LOG_TRACE(MOD_GRAPHICS, "Loading display settings from file...");
        cachedDisplaySettings_ = loadDisplaySettingsFromFile();
        displaySettingsCached_ = true;
    }

    // One-time race mappings load (validated at startup by S03)
    if (!areRaceMappingsLoaded()) {
        LOG_TRACE(MOD_GRAPHICS, "Loading race mappings from config/race_models.json...");
        if (!loadRaceMappings("config/race_models.json")) {
            LOG_FATAL(MOD_GRAPHICS, "Failed to load race model mappings: config/race_models.json");
        }
    }

    // Load EQE1 archive configuration (which .eqe1 files to open per zone)
    if (eqe1_->archiveEntries().empty()) {
        eqe1_->loadEQE1ArchiveConfig("config/eqe1_archives.json");
    }

    // Item model mappings handled on-demand via SQLite — no pre-loading needed

    // P32: driver type is always GLES2 now (RenderingBackend::Software/
    // OpenGL removed — docs/decisions.md 2026-07-28 addendum). Collapsed
    // from a switch since RenderingBackend has exactly one value.
    irr::video::E_DRIVER_TYPE driverType = irr::video::EDT_OGLES2;
    LOG_DEBUG(MOD_GRAPHICS, "[GL] Loading screen driver: OpenGL ES 2.0");

    // Create device
    irr::SIrrlichtCreationParameters params;
    params.DriverType = driverType;
    params.WindowSize = irr::core::dimension2d<irr::u32>(config.width, config.height);
    params.Fullscreen = config.fullscreen;
    params.Stencilbuffer = config_.constrainedConfig.enableStencilBuffer;
    params.Vsync = true;
    params.AntiAlias = config_.constrainedConfig.antiAliasLevel;

#ifdef EQT_HAS_DRM
    // DRM/KMS: use framebuffer device type (renders via EGL/GBM, no X11)
    if (config_.constrainedConfig.useDRM) {
        params.DeviceType = irr::EIDT_FRAMEBUFFER;
        LOG_INFO(MOD_GRAPHICS, "[GL] Loading screen: using DRM/KMS framebuffer device");
    }
#endif

#ifdef EQT_HAS_GLES2
    // P25: X11-windowed EGL backend opt-in (x86 GLES2 dev builds only —
    // this is inert on ARM, whose CIrrDeviceFB.cpp is the untouched shared
    // DRM device and never reads this variable). Plumbed via an env var
    // rather than a direct call into CIrrDeviceFB, because that device
    // header is private to the patched Irrlicht build and isn't on this
    // translation unit's include path (only Irrlicht's public API is).
    // Explicit opt-in only: never set based on $DISPLAY.
    if (config_.constrainedConfig.useEglX11) {
        setenv("WILLEQ_EGL_X11", "1", 1);
        LOG_INFO(MOD_GRAPHICS, "[GL] Loading screen: X11-windowed EGL backend requested (--egl-x11)");
    }
#endif

    LOG_DEBUG(MOD_GRAPHICS, "[GL] Creating Irrlicht device: {}x{}, fullscreen={}, vsync={}, stencil={}, AA={}",
              config.width, config.height, config.fullscreen, true,
              config_.constrainedConfig.enableStencilBuffer,
              config_.constrainedConfig.antiAliasLevel);
    if (!config_.constrainedConfig.useDRM) {
        LOG_DEBUG(MOD_GRAPHICS, "[GL] DISPLAY={}", std::getenv("DISPLAY") ? std::getenv("DISPLAY") : "(not set)");
    }

    LOG_TRACE(MOD_GRAPHICS, "Creating Irrlicht device (createDeviceEx)...");
    device_ = irr::createDeviceEx(params);

    if (!device_) {
        // P32: the EDT_SOFTWARE retry fallback was removed along with
        // RenderingBackend::Software (docs/decisions.md 2026-07-28
        // addendum) — GLES2 device creation failure is now fatal, not
        // silently downgraded to the software renderer.
        LOG_ERROR(MOD_GRAPHICS, "Failed to create Irrlicht device with {} driver",
                  backendName(config_.constrainedConfig.renderingBackend));
        return false;
    }

    // Allow Irrlicht logging at DEBUG level, suppress at INFO
    device_->getLogger()->setLogLevel(irr::ELL_WARNING);
    device_->setWindowCaption(irr::core::stringw(config.windowTitle.c_str()).c_str());

    LOG_TRACE(MOD_GRAPHICS, "Acquiring driver, scene manager, GUI environment...");
    driver_ = device_->getVideoDriver();
    smgr_ = device_->getSceneManager();
    guienv_ = device_->getGUIEnvironment();
    if (!driver_ || !smgr_ || !guienv_) {
        LOG_FATAL(MOD_GRAPHICS, "Irrlicht device returned null subsystems: driver={} smgr={} guienv={}",
                  (void*)driver_, (void*)smgr_, (void*)guienv_);
        return false;
    }

    // Wire driver into constrained texture cache immediately (before any texture loading)
    LOG_TRACE(MOD_GRAPHICS, "Wiring driver into constrained texture cache...");
    if (driver_ && constrainedTextureCache_) {
        constrainedTextureCache_->setDriver(driver_);
        constrainedTextureCache_->setSceneManager(smgr_);
        LOG_INFO(MOD_GRAPHICS, "Texture cache wired to driver ({}KB limit, {}x{} max texture, mipmaps={})",
                 config_.constrainedConfig.textureMemoryBytes / 1024,
                 config_.constrainedConfig.maxTextureDimension,
                 config_.constrainedConfig.maxTextureDimension,
                 config_.constrainedConfig.enableMipmaps ? "yes" : "no");
    }

    // Log comprehensive driver/OpenGL details
    logDriverDetails(driver_, device_, params);

    // In DRM mode, enable and create software cursor (no hardware cursor available)
    if (config_.constrainedConfig.useDRM && device_->getCursorControl()) {
        device_->getCursorControl()->setVisible(true);
    }
    LOG_TRACE(MOD_GRAPHICS, "Creating software cursor...");
    createSoftwareCursor();

    // U01: Initialize batched text renderer
    textBatch_ = std::make_unique<TextBatch>();
    if (textBatch_->init(guienv_)) {
        LOG_INFO(MOD_GRAPHICS, "TextBatch initialized");
    } else {
        LOG_WARN(MOD_GRAPHICS, "TextBatch initialization failed — falling back to font->draw()");
        textBatch_.reset();
    }

    // U02: Load UI atlas from willeq.eqe1 flat pack (ETC1 compressed texture + JSON metadata)
    if (!config_.constrainedConfig.eqe1Path.empty() && constrainedTextureCache_) {
        // Open willeq.eqe1 early (before zone loading) so all willeq-generated assets are available
        eqe1_->getOrOpenEQE1("willeq");

        uiAtlas_ = std::make_unique<UIAtlas>();
        if (!uiAtlas_->load(driver_, constrainedTextureCache_)) {
            LOG_FATAL(MOD_GRAPHICS, "UIAtlas: willeq.eqe1 missing or incomplete in {}. "
                "Run: eqe1_builder --bmp-dir data/atlas --bmp-dir data/textures --output {} --name willeq",
                config_.constrainedConfig.eqe1Path, config_.constrainedConfig.eqe1Path);
            return false;
        }
    }

    // U03: Initialize static layout UI renderer
    LOG_TRACE(MOD_GRAPHICS, "Initializing UI renderer...");
    uiRenderer_ = std::make_unique<UIRenderer>();
    uiRenderer_->init(driver_, uiAtlas_.get(), textBatch_.get());
    uiLayout_.computeLayout(config.width, config.height);
    // U04: Create chat message buffer for new UI
    newUIChatBuffer_ = std::make_unique<eqt::ui::ChatMessageBuffer>(500);
    chatPanelState_.messageBuffer = newUIChatBuffer_.get();
    // U06j: Initialize static UI input handler
    staticUIInput_ = std::make_unique<StaticUIInput>();
    staticUIInput_->init(bridge_);
    LOG_INFO(MOD_GRAPHICS, "UIRenderer initialized (newui=off, toggle with /newui)");

    // Configure mipmap generation based on constrained config
    if (driver_) {
        driver_->setTextureCreationFlag(irr::video::ETCF_CREATE_MIP_MAPS,
                                         config_.constrainedConfig.enableMipmaps);
        LOG_DEBUG(MOD_GRAPHICS, "[GL] Mipmap generation: {}",
                  config_.constrainedConfig.enableMipmaps ? "enabled" : "disabled");
    }

    // Wire ancillary services into cache (driver/smgr already wired above)
    if (constrainedTextureCache_) {
        if (backgroundThreadPool_)
            constrainedTextureCache_->setBackgroundThreadPool(backgroundThreadPool_.get());
        // Pre-create placeholder texture before render loop starts
        constrainedTextureCache_->getPlaceholderTexture();

        // P10b: register the region-mesh texture guard so evictions can
        // never leave a dangling ITexture* in a region mesh buffer's
        // material, even for nodes detached from the scene graph for
        // front-to-back manual draw (see renderer_zone_merge.h). Bound to
        // regionMeshNodes_ (a persistent member, cleared not recreated
        // between zone visits) once here, for the renderer's lifetime. The
        // placeholder texture is resolved ONCE, right here (before any
        // eviction can occur, safe to lock the cache's mutex_) and handed
        // to the guard as a plain pointer -- the guard must NEVER call back
        // into constrainedTextureCache_ from inside onTextureEvicted() (see
        // RegionMeshTextureGuard's header comment for the self-deadlock
        // this avoids).
        regionMeshTextureGuard_ = std::make_unique<RegionMeshTextureGuard>(
            regionMeshNodes_, constrainedTextureCache_->getPlaceholderTexture());
        constrainedTextureCache_->addEvictionListener(regionMeshTextureGuard_.get());

        // P10b: softwareCursorTexture_ was already set by createSoftwareCursor()
        // above (runs earlier in this function) -- guard it against the same
        // dangling-pointer class (see SingleTexturePointerGuard's doc comment
        // in renderer_zone_merge.h; pre-existing, unrelated to mergeZoneDraws,
        // found while verifying this unit's own fix).
        softwareCursorTextureGuard_ = std::make_unique<SingleTexturePointerGuard>(softwareCursorTexture_);
        constrainedTextureCache_->addEvictionListener(softwareCursorTextureGuard_.get());

        // T123: guards detached objectNodes_ entries -- see renderer_zone_merge.h.
        objectNodeTextureGuard_ = std::make_unique<ObjectNodeTextureGuard>(objectNodes_, constrainedTextureCache_->getPlaceholderTexture());
        constrainedTextureCache_->addEvictionListener(objectNodeTextureGuard_.get());
    }

    // Create event receiver
    LOG_TRACE(MOD_GRAPHICS, "Creating event receiver...");
    eventReceiver_ = std::make_unique<RendererEventReceiver>();
    device_->setEventReceiver(eventReceiver_.get());

    // Setup camera (needed for loading screen rendering)
    LOG_TRACE(MOD_GRAPHICS, "Setting up camera...");
    setupCamera();

    // Setup lighting (basic setup)
    LOG_TRACE(MOD_GRAPHICS, "Setting up lighting...");
    setupLighting();

    // Apply initial settings from constrained config (must come BEFORE shader variant sync)
    wireframeMode_ = config_.constrainedConfig.wireframe;
    fogEnabled_ = config_.constrainedConfig.fog;
    debugPlayerLightEnabled_ = config_.constrainedConfig.playerLight;
    lightingEnabled_ = config.lighting;

    // Initialize GLSL shader pipeline (if enabled and driver supports it).
    // P32: the "!= RenderingBackend::Software" guard was removed — GLES2
    // is the only backend now, so it was always true.
    if (config_.constrainedConfig.enableShaders) {
        LOG_TRACE(MOD_GRAPHICS, "Initializing GLSL shader pipeline...");
        auto* gpu = driver_->getGPUProgrammingServices();
        if (gpu) {
            zoneShader_ = std::make_unique<ZoneShaderManager>(driver_, gpu);
            if (!zoneShader_->isAvailable()) {
                LOG_WARN(MOD_GRAPHICS, "GLSL shaders requested but compilation failed; using fixed-function fallback");
                zoneShader_.reset();
            } else if (zoneShader_->isLightweightAvailable()) {
                zoneShader_->setPerPixelPlayerLight(debugPlayerLightEnabled_);
            }
        }
    }

    // Initialize GPU upload thread (shared EGL context for async texture/VBO uploads)
#ifdef EQT_HAS_GLES2
    LOG_TRACE(MOD_GRAPHICS, "Initializing GPU upload thread (shared EGL context)...");
    initGPUUploadThread();
#endif

    // Setup HUD (needed for loading screen text)
    LOG_TRACE(MOD_GRAPHICS, "Setting up HUD...");
    overlays_->setupHUD();

    // S05: Pre-allocate renderer-lifetime subsystems (survive zone changes)

    // Create entity renderer (needed for entity model loading during zone load)
    LOG_TRACE(MOD_GRAPHICS, "Creating entity renderer...");
    createEntityRenderer();

    // P17: register the entity-mesh texture guard (constructed here, not
    // alongside regionMeshTextureGuard_/softwareCursorTextureGuard_ above,
    // because it needs entityRenderer_ to exist first). Same placeholder-
    // resolved-once-up-front pattern as the other two guards -- see
    // EntityMeshTextureGuard's doc comment in renderer_zone_merge.h.
    if (constrainedTextureCache_ && entityRenderer_) {
        entityMeshTextureGuard_ = std::make_unique<EntityMeshTextureGuard>(
            entityRenderer_->getEntities(), constrainedTextureCache_->getPlaceholderTexture());
        constrainedTextureCache_->addEvictionListener(entityMeshTextureGuard_.get());
    }

    // Create sky renderer (skyRendering preset flag controls whether it renders, not whether it exists)
    LOG_TRACE(MOD_GRAPHICS, "Creating sky renderer...");
    skyRenderer_ = std::make_unique<SkyRenderer>(smgr_, driver_, device_->getFileSystem());
    if (constrainedTextureCache_) {
        skyRenderer_->setConstrainedTextureCache(constrainedTextureCache_);
    }
    if (!skyRenderer_->initialize(config_.eqClientPath)) {
        LOG_WARN(MOD_GRAPHICS, "Sky renderer initialization failed - sky will not be rendered");
    } else {
        LOG_INFO(MOD_GRAPHICS, "Sky renderer initialized");
    }

    // Create detail manager (detailObjectsEnabled controls whether it renders, not whether it exists)
    LOG_TRACE(MOD_GRAPHICS, "Creating detail manager...");
    detailManager_ = std::make_unique<Detail::DetailManager>(smgr_, driver_);
    detailManager_->setSurfaceMapsPath("data/detail/zones");
    if (constrainedTextureCache_) {
        detailManager_->setConstrainedTextureCache(constrainedTextureCache_);
    }
    LOG_INFO(MOD_GRAPHICS, "Detail manager initialized");

    // Create simulation worker (zone data configured later via setZoneData())
    simulationWorker_ = std::make_unique<SimulationWorker>();
    LOG_INFO(MOD_GRAPHICS, "Simulation worker initialized");

    // Create render pass timer (Irrlicht-owned via ref counting)
    renderPassTimer_ = new RenderPassTimer();
    renderPassTimer_->setRenderer(this);
    LOG_INFO(MOD_GRAPHICS, "Render pass timer initialized");

    // P32: AnimatedTreeManager (CPU-instancing wrapper) deleted. Its
    // construction here did three things: setRenderDistance/
    // setConstrainedTextureCache/setShaderMaterialTypes — all three fed
    // ONLY the CPU-instancing path (createAnimatedTree/updateTreeAnimation),
    // which was reachable only via the now-removed EDT_BURNINGSVIDEO branch
    // in loadZoneStep11_Env(). treeWindController_/treeIdentifier_ are
    // plain default-constructed members needing no init here — their
    // per-zone config load happens in loadZoneStep11_Env() same as before.

    // Create weather system (needed before zone loading)
    if (!weatherSystem_) {
        LOG_TRACE(MOD_GRAPHICS, "Creating weather system...");
        weatherSystem_ = std::make_unique<WeatherSystem>();
        // Connect weather system to tree wind controller via callback
        weatherSystem_->addCallback([this](WeatherType weather) {
            treeWindController_.setWeather(weather);
        });
    }

    // Check display settings to determine which environmental managers to create.
    auto displaySettings = getDisplaySettings();

    // Always create particle manager — needed for unified fire system even when
    // atmospheric particles (dust, pollen, fireflies) are disabled
    if (!particleManager_) {
        LOG_TRACE(MOD_GRAPHICS, "Creating particle manager...");
        particleManager_ = std::make_unique<Environment::ParticleManager>(smgr_, driver_);
        particleManager_->setConstrainedTextureCache(constrainedTextureCache_);
        LOG_TRACE(MOD_GRAPHICS, "Initializing particle manager (loading atlas)...");
        if (!particleManager_->init(config_.eqClientPath)) {
            LOG_WARN(MOD_GRAPHICS, "Failed to initialize particle manager");
        }
        // Compile the point sprite shader program now (during init, under the loading
        // screen) rather than during deferred init where the ~260ms Mali 400 shader
        // compilation spikes the governor into Red. The shader has zero zone data
        // dependencies — it only needs the GLES2 context.
        particleManager_->initUnifiedRenderer();
        // Atmospheric particles setting controls billboard emitters (dust, pollen, etc.)
        // Unified fire has its own toggle and works regardless of this setting
        particleManager_->setEnabled(displaySettings.atmosphericParticles);
        LOG_INFO(MOD_GRAPHICS, "Particle manager initialized (atmospheric particles: {})",
                 displaySettings.atmosphericParticles ? "enabled" : "disabled");
    }

    // Create ambient creatures (boids) system (only if ambient creatures enabled)
    if (!boidsManager_ && displaySettings.ambientCreatures) {
        LOG_TRACE(MOD_GRAPHICS, "Creating boids manager (loading atlas)...");
        boidsManager_ = std::make_unique<Environment::BoidsManager>(smgr_, driver_);
        boidsManager_->setConstrainedTextureCache(constrainedTextureCache_);
        if (!boidsManager_->init(config_.eqClientPath)) LOG_WARN(MOD_GRAPHICS, "Failed to initialize boids manager");
        ensureBoidsTextureGuard();
        LOG_INFO(MOD_GRAPHICS, "Boids manager initialized (ambient creatures enabled in settings)");
    } else if (!boidsManager_) {
        LOG_INFO(MOD_GRAPHICS, "Boids manager skipped (ambient creatures disabled in settings)");
    }

    // Create tumbleweed manager (only if rolling objects enabled)
    if (!tumbleweedManager_ && displaySettings.rollingObjects) {
        LOG_TRACE(MOD_GRAPHICS, "Creating tumbleweed manager (loading texture)...");
        tumbleweedManager_ = std::make_unique<Environment::TumbleweedManager>(smgr_, driver_);
        tumbleweedManager_->setConstrainedTextureCache(constrainedTextureCache_);
        if (!tumbleweedManager_->init()) {
            LOG_WARN(MOD_GRAPHICS, "Failed to initialize tumbleweed manager");
        }
        LOG_INFO(MOD_GRAPHICS, "Tumbleweed manager initialized (rolling objects enabled in settings)");
    } else if (!tumbleweedManager_) {
        LOG_INFO(MOD_GRAPHICS, "Tumbleweed manager skipped (rolling objects disabled in settings)");
    }

    // Create weather effects controller (screen-space rain/snow overlays, storm clouds, lightning)
    if (!weatherEffects_) {
        LOG_TRACE(MOD_GRAPHICS, "Creating weather effects controller...");
        weatherEffects_ = std::make_unique<WeatherEffectsController>(
            smgr_, driver_, particleManager_.get(), skyRenderer_.get());
        if (constrainedTextureCache_) {
            weatherEffects_->setConstrainedTextureCache(constrainedTextureCache_);
        }
        LOG_TRACE(MOD_GRAPHICS, "Initializing weather effects (loading cloud/rain/snow textures)...");
        if (!weatherEffects_->initialize(config_.eqClientPath)) {
            LOG_WARN(MOD_GRAPHICS, "Failed to initialize weather effects controller");
        }
        // Connect weather system to weather effects
        if (weatherSystem_) {
            weatherSystem_->addListener(weatherEffects_.get());
        }
        LOG_INFO(MOD_GRAPHICS, "Weather effects initialized");
    }

    initialized_ = true;
    loadingScreenVisible_ = true;  // Show loading screen by default
    lastFpsTime_ = device_->getTimer()->getTime();

    LOG_INFO(MOD_GRAPHICS, "IrrlichtRenderer loading screen initialized: {}x{}", config.width, config.height);
    return true;
}

bool IrrlichtRenderer::loadGlobalAssets() {
    if (!initialized_) {
        LOG_ERROR(MOD_GRAPHICS, "Cannot load global assets - renderer not initialized");
        return false;
    }

    // S05: entityRenderer_ already created in initLoadingScreen()
    // loadGlobalAssets() is idempotent — use a flag to track
    if (globalAssetsLoaded_) {
        LOG_DEBUG(MOD_GRAPHICS, "Global assets already loaded, skipping");
        return true;
    }
    globalAssetsLoaded_ = true;

    // All S3D archive loading (global characters, numbered globals, equipment)
    // is handled by the background thread via DataReady_ArchiveIndex and
    // DataReady_Equipment phases, or lazily on first use. No eager file I/O here.

    // Create door manager (if not already created)
    if (!doorManager_) {
        doorManager_ = std::make_unique<DoorManager>(smgr_, driver_);
        if (constrainedTextureCache_) {
            doorManager_->setConstrainedTextureCache(constrainedTextureCache_);
        }
        // If zone was already loaded before doorManager_ was created, set it now
        if (currentZone_) {
            doorManager_->setZone(currentZone_);
        }
        if (zoneBspTree_) doorManager_->setBspTree(zoneBspTree_.get());
        doorManager_->setPvsRegion(currentPvsRegion_);
        if (frustumCuller_) doorManager_->setFrustumCuller(frustumCuller_.get());
        if (zoneShader_ && zoneShader_->isAvailable()) {
            doorManager_->setShaderMaterialTypes(zoneShader_->getActiveSolid(),
                                                 zoneShader_->getActiveAlphaTest(),
                                                 zoneShader_->getActiveAlphaTestRGBA());
        }
        ensureDoorTextureGuard();  // T124
    }

    // S05: skyRenderer_ and detailManager_ already created in initLoadingScreen()

    LOG_INFO(MOD_GRAPHICS, "Global assets loaded successfully");
    return true;
}

void IrrlichtRenderer::showLoadingScreen() {
    if (isLoading()) return;
    loadingScreenVisible_ = true;
    LOG_DEBUG(MOD_GRAPHICS, "Loading screen shown");
}

void IrrlichtRenderer::hideLoadingScreen() {
    if (isLoading()) return;
    loadingScreenVisible_ = false;
    LOG_DEBUG(MOD_GRAPHICS, "Loading screen hidden");

    // Force full visibility and lighting recalculation on the first gameplay frame.
    // During loading, Tier2 updates (PVS, object culling, lights) run and cache
    // camera positions. By the time loading completes, these caches are populated
    // at the camera's loading-phase position. Without resetting here, the movement
    // Force PVS recalculation on initial zone-in.
    forcePvsUpdate_ = true;

    // Request deferred governor reset so the 11+ second loading screen frame
    // doesn't poison the rolling average. requestReset() defers the actual reset
    // to the next beginFrame(), discarding this poisoned frame entirely.
    if (governor_) {
        governor_->requestReset();
        LOG_DEBUG(MOD_GRAPHICS, "Governor reset requested — will start GREEN next frame");
    }

    // Start SimulationWorker thread for ongoing runtime updates.
    // Worker was created and zone data set by loadZoneSequential() on the loading thread,
    // which also ran computeOnce() to populate the first frame's scene state.
    if (simulationWorker_ && !simulationWorker_->isRunning()) {
        simulationWorker_->start();
        LOG_INFO(MOD_GRAPHICS, "SimulationWorker thread started for runtime updates");
        simBridge_->registerSimulationWorkerVertexAnimData();
    } else if (!simulationWorker_) {
        // Fallback: loading thread didn't create worker (shouldn't happen)
        LOG_WARN(MOD_GRAPHICS, "SimulationWorker not created by loading thread, creating now");
        simBridge_->startSimulationWorkerEarly();
    }

    // Release duplicate character data from zone source. RaceModelLoader independently
    // loads _chr.s3d into its own cache, so currentZone_->characters is an unused copy.
    if (currentZone_) {
        currentZone_->clearCharacterData();
    }
}


} // namespace Graphics
} // namespace EQT
