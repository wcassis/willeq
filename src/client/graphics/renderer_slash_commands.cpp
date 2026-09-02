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

namespace {
// T66 -- /camera mode-name parsing. Case-insensitive, spaces/hyphens/
// underscores ignored, so it accepts anything getCameraModeString() already
// produces ("Free", "Follow", "First Person") as a set-then-read round trip,
// plus compact spellings ("firstperson", "fp") for scripts that would
// rather not quote a two-word argument.
std::string NormalizeCameraModeToken(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == ' ' || c == '-' || c == '_') continue;
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool ParseCameraModeName(const std::string& args, EQT::Graphics::IrrlichtRenderer::CameraMode& out) {
    std::string norm = NormalizeCameraModeToken(args);
    if (norm == "free") { out = EQT::Graphics::IrrlichtRenderer::CameraMode::Free; return true; }
    if (norm == "follow") { out = EQT::Graphics::IrrlichtRenderer::CameraMode::Follow; return true; }
    if (norm == "firstperson" || norm == "fp") { out = EQT::Graphics::IrrlichtRenderer::CameraMode::FirstPerson; return true; }
    return false;
}
} // namespace

// D20d: Handle renderer-specific slash commands forwarded from game thread
void IrrlichtRenderer::processSlashCommand(const std::string& command) {
    // Parse command name and args
    std::string cmd, args;
    size_t spacePos = command.find(' ');
    if (spacePos != std::string::npos) {
        cmd = command.substr(0, spacePos);
        args = command.substr(spacePos + 1);
    } else {
        cmd = command;
    }

    // Helper to push chat feedback via bridge. T233: this is registry 3's
    // counterpart of EverQuest::AddChatSystemMessage() (eq_chat.cpp), which
    // pushes the SystemMessage AND echoes to the console via
    // LOG_INFO(MOD_MAIN, ...). Until T233 only the push happened here, so
    // a --script run / log grep saw nothing for renderer commands (T142).
    // Same module and shape as the game-registry echo so "[MAIN] <reply>"
    // greps work identically for both registries.
    auto chat = [this](const std::string& msg) {
        if (bridge_) {
            eqt::state::ChatMessageData data;
            data.message = msg;
            data.channelType = 0;
            data.channelName = "system";
            bridge_->pushEvent(eqt::state::GameEvent(
                eqt::state::GameEventType::SystemMessage, std::move(data)));
        }
        LOG_INFO(MOD_MAIN, "{}", msg);
    };

    // --- Visual rendering ---
    if (cmd == "/sort") {
        toggleManualZoneDraw();
        chat(fmt::format("Front-to-back zone sorting: {}", isManualZoneDrawEnabled() ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/mergedraws") {
        toggleMergeZoneDraws();
        chat(fmt::format("Zone draw merge: {}", isMergeZoneDrawsEnabled() ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/screenshot") {
        // P10: smallest additive hook for the in-app A/B capture (no other
        // scriptable screenshot command existed — F12 is a raw key event,
        // not reachable from --script's slash-command tour list). Explicit
        // filename arg so a tour script can capture deterministic
        // before/after pairs at a fixed pose.
        std::string path = args.empty() ? "screenshot.png" : args;
        bool ok = saveScreenshot(path);
        chat(fmt::format("Screenshot {}: {}", path, ok ? "saved" : "FAILED"));
    } else if (cmd == "/camera") {
        // T66 -- instance 7 of "reachable only through a UI gesture, invisible
        // to automation" (refactor/deferred.md): cycleCameraMode()/
        // setCameraMode()/getCameraModeString() already existed, but the ONLY
        // producer was HA::ToggleCameraMode, a raw Ctrl+F5 key event -- a
        // scripted/piped session could never enter first person. Modelled on
        // /screenshot immediately above: same registry-3 renderer command,
        // same chat()-reply idiom, no new dispatch path invented. No-arg
        // reports the current mode (so a script can assert rather than
        // assume); a named mode sets it explicitly, round-tripping with
        // getCameraModeString()'s own output -- deliberately NOT just
        // exposing cycleCameraMode(), which would force a script to cycle
        // blind and count (T51 documented exactly that trap for /attack).
        // Cycling is additionally exposed via "cycle" since it was free.
        if (args.empty()) {
            chat(fmt::format("Camera mode: {}", getCameraModeString()));
        } else if (args == "cycle") {
            cycleCameraMode();
            chat(fmt::format("Camera mode: {}", getCameraModeString()));
        } else {
            CameraMode mode;
            if (ParseCameraModeName(args, mode)) {
                setCameraMode(mode);
                chat(fmt::format("Camera mode: {}", getCameraModeString()));
            } else {
                chat(fmt::format(
                    "Unknown camera mode '{}'. Valid: Free, Follow, \"First Person\" "
                    "(or firstperson/fp); no argument reports the current mode; "
                    "'cycle' advances Free->Follow->First Person->Free.", args));
            }
        }
    } else if (cmd == "/fpcam") {
        // T310 Leg A -- the first-person camera's numbers, settable at
        // runtime so solving the pose against the official client does not
        // cost a rebuild per candidate value (Will, 2026-08-29: "This should
        // not require recompiling"). Registry 3, because the camera lives on
        // this object and must be driven from the RENDER thread
        // (application.cpp:1204 -- never call renderer methods from the game
        // thread). The catalog row is in
        // src/client/help/renderer_command_catalog.cpp; without it
        // FindRendererCommand() reports registry=none and nothing is queued.
        //
        // *** A SWEEP MUST NOT ASSERT ON `outcome`. *** Registry 3 is
        // fire-and-forget with no ack path, so printCommandResult() always
        // says outcome=queued whatever happened here. The result is the
        // [FPCAM] / [FPCAM-DUMP] line chat() emits below, which is also
        // LOG_INFO(MOD_MAIN) so a --script run or a log grep sees it.
        std::string sub = args;
        {
            const size_t sp = sub.find(' ');
            if (sp != std::string::npos) sub = sub.substr(0, sp);
        }
        for (char& c : sub) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (sub == "dump") {
            for (const auto& line : buildFirstPersonCameraDumpLines()) chat(line);
        } else {
            for (const auto& line : EQT::Graphics::FirstPersonTunablesCommandLines(args)) {
                chat(line);
            }
            // A set/reset must take effect on the CURRENT frame, not on the
            // next time something else happens to move the player.
            if (sub == "set" || sub == "reset") {
                applyCameraProjectionForMode();
                repositionFirstPersonCamera();
            }
        }
    } else if (cmd == "/upload") {
        toggleGPUUploadThread();
        chat(fmt::format("GPU upload thread: {}", isGPUUploadThreadEnabled() ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/portal") {
        if (args == "debug") {
            togglePortalDebugDraw();
            chat(fmt::format("Portal debug overlay: {}", isPortalDebugDrawEnabled() ? "ENABLED" : "DISABLED"));
        } else {
            togglePortalOcclusion();
            chat(fmt::format("Portal occlusion: {}", isPortalOcclusionEnabled() ? "ENABLED" : "DISABLED"));
        }
    } else if (cmd == "/stencil") {
        toggleStencilDebugDraw();
        chat(fmt::format("Stencil debug overlay: {}", isStencilDebugDrawEnabled() ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/plight") {
        togglePlayerLight();
        chat(fmt::format("Per-pixel player light: {}", isPlayerLightEnabled() ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/olight") {
        toggleObjectLights();
        chat(fmt::format("Object lights: {}", isObjectLightsEnabled() ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/zlight") {
        toggleDirectionalLight();
        chat(fmt::format("Directional light: {}", isDirectionalLightEnabled() ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/lightmode") {
        if (zoneShader_) {
            bool unified = !zoneShader_->unifiedLighting();
            zoneShader_->setUnifiedLighting(unified);
            chat(fmt::format("Lighting mode: {}", unified ? "UNIFIED (pointLight * aColor)" : "ADDITIVE (pointLight + base*aColor)"));
            LOG_INFO(MOD_SHADER, "Lighting mode toggled to {}", unified ? "unified" : "additive");
        }
    } else if (cmd == "/shaderdebug" || cmd == "/sd") {
        if (zoneShader_) {
            int mode = 0;
            if (!args.empty()) {
                if (args == "off") mode = 0;
                else mode = std::atoi(args.c_str());
            }
            int prevMode = zoneShader_->shaderDebug();
            zoneShader_->setShaderDebug(mode);

            // Swap materials to/from debug program
            irr::s32 debugMat = zoneShader_->getDebugMaterialType();
            if (debugMat >= 0) {
                if (mode > 0 && prevMode == 0) {
                    // Entering debug: swap all per-pixel materials to debug program
                    swapToDebugShader(debugMat);
                    LOG_INFO(MOD_SHADER, "Swapped all per-pixel materials to debug program {}", debugMat);
                } else if (mode == 0 && prevMode > 0) {
                    // Leaving debug: restore production materials
                    swapFromDebugShader(debugMat);
                    LOG_INFO(MOD_SHADER, "Restored production materials from debug program");
                }
            }

            const char* modeNames[] = {"normal", "texColor", "vColor", "LUT(r,a)", "pLight",
                                        "worldNormal", "lightDir", "alpha", "tex*vColor",
                                        "UV", "depth", "rawTexAlpha", "companionAlpha"};
            const char* name = (mode >= 0 && mode <= 12) ? modeNames[mode] : "unknown";
            chat(fmt::format("Shader debug mode: {} ({})", mode, name));
            LOG_INFO(MOD_SHADER, "Shader debug mode set to {} ({})", mode, name);
        }
    } else if (cmd == "/fire") {
        if (auto* pm = getParticleManager()) {
            pm->toggleUnifiedFire();
            chat("Fire particles toggled");
        }
    } else if (cmd == "/sky") {
        toggleSky();
        chat(fmt::format("Sky rendering: {}", isSkyEnabled() ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/skytype") {
        if (!args.empty()) {
            int type = std::atoi(args.c_str());
            forceSkyType(static_cast<uint8_t>(type));
            chat(fmt::format("Sky type forced to {}", type));
        } else {
            chat("Usage: /skytype <0-255>");
        }
    }
    // --- Distance ---
    // T139: /renderdist's clipplane/viewdist and /clipdist's clip/farplane
    // aliases were only ever resolved by the game-registry Command::aliases
    // field (eq_commands_movement.cpp, now deleted) -- registry-3's fallback
    // forwards raw text and only ever matched literal names here. Added so
    // deleting the game-registry copy doesn't silently drop them (T139
    // plan's "THE TRAP"; see refactor/T139_delete_duplicate_commands.md
    // Review for the full alias audit).
    else if (cmd == "/renderdist" || cmd == "/clipplane" || cmd == "/viewdist") {
        if (!args.empty()) {
            float dist = std::atof(args.c_str());
            if (dist >= 50.0f && dist <= 10000.0f) {
                setRenderDistance(dist);
                chat(fmt::format("Render distance: {:.0f}", dist));
            } else {
                chat("Render distance must be between 50 and 10000");
            }
        } else {
            chat(fmt::format("Render distance: {:.0f}", getRenderDistance()));
        }
    } else if (cmd == "/clipdist" || cmd == "/clip" || cmd == "/farplane") {
        if (!args.empty()) {
            float dist = std::atof(args.c_str());
            setClipDistance(dist);
            chat(fmt::format("Clip distance: {:.0f}", dist));
        } else {
            chat(fmt::format("Clip distance: {:.0f}", getClipDistance()));
        }
    }
    // --- Profiling/debug ---
    // T139: frametime/ft, sp/scenebreak, ds, sw were game-registry aliases
    // (eq_commands_movement.cpp, deleted) resolved only there -- added here
    // for the same reason as the renderdist/clipdist aliases above.
    else if (cmd == "/frametiming" || cmd == "/frametime" || cmd == "/ft") {
        bool newState = !isFrameTimingEnabled();
        setFrameTimingEnabled(newState);
        if (newState) {
            chat("Frame timing profiler ENABLED - check console for breakdown every ~2 seconds");
        } else {
            chat("Frame timing profiler DISABLED");
        }
    } else if (cmd == "/sceneprofile" || cmd == "/sp" || cmd == "/scenebreak") {
        runSceneProfile();
        chat("Scene profile running - check console for breakdown");
    } else if (cmd == "/dumpscene" || cmd == "/ds") {
        dumpScene();
        chat("Scene dump written to console log");
    } else if (cmd == "/simworker" || cmd == "/sw") {
        auto info = getSimWorkerDebugInfo();
        for (const auto& line : info) chat(line);
    }
    // T108: "/pmem" moved off this empty-MemoryReportInput{} path (runLoadDiagnostics
    // had zero callers, per T105/T106) — EverQuest::runPmemDiagnostics() is now
    // called directly by the command handler; see irrlicht_bridge.cpp.
    // --- UI ---
    // T301 Leg D: the scriptable half of the UI-hide toggle. F11
    // (HotkeyAction::ToggleAllUI) has hidden the UI since long before this,
    // but a raw key event is not reachable from a --script run or from any
    // capture harness that does not own an X server -- the same gap /camera
    // closed for camera modes in T66. Every visual gate since T63 has fought
    // the overlay; T300 Leg C had to record its 285-frame combat negative
    // with the chat box and hotbar opaque over the bottom of every frame.
    else if (cmd == "/hideui" || cmd == "/toggleui" || cmd == "/ui") {
        toggleAllUI();
        chat(fmt::format("UI: {}", isAllUIHidden() ? "HIDDEN" : "VISIBLE"));
    }
    // T313: the scriptable half of the static-UI scroll, for the same
    // reason /hideui exists (T301 Leg D) -- a --script run or a headless
    // capture harness cannot generate a mouse wheel or a key event, and
    // before this unit chat scrollback, the skills list and the loot list
    // had no writer of any kind. Routing is by CURSOR POSITION exactly as
    // the wheel's is, so `/scroll up` with the pointer over the chat box
    // does the same thing as a wheel notch there; an explicit panel name
    // overrides that for a script that never moves the pointer.
    else if (cmd == "/scroll") {
        chat(EQT::Graphics::StaticUIInput::runScrollCommand(
            args,
            eventReceiver_ ? eventReceiver_->getMouseX() : 0,
            eventReceiver_ ? eventReceiver_->getMouseY() : 0,
            uiLayout_, chatPanelState_, skillsPopupState_, lootState_));
    }
    else if (cmd == "/newui") {
        newUIEnabled_ = !newUIEnabled_;
        chat(fmt::format("New static UI: {}", newUIEnabled_ ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/newbook") {
        spellbookState_.isOpen = !spellbookState_.isOpen;
        if (spellbookState_.isOpen) {
            inventoryState_.activePopup = PopupType::None;  // Close other popups
        }
        chat(fmt::format("New UI spellbook: {}", spellbookState_.isOpen ? "OPEN" : "CLOSED"));
    } else if (cmd == "/newbookprev") {
        if (spellbookState_.currentPage > 0) spellbookState_.currentPage--;
    } else if (cmd == "/newbooknext") {
        if (spellbookState_.currentPage < spellbookState_.pageCount() - 1) spellbookState_.currentPage++;
    } else if (cmd == "/newskills") {
        skillsPopupState_.isOpen = !skillsPopupState_.isOpen;
        if (skillsPopupState_.isOpen) {
            inventoryState_.activePopup = PopupType::None;
            spellbookState_.isOpen = false;
        }
        chat(fmt::format("New UI skills: {}", skillsPopupState_.isOpen ? "OPEN" : "CLOSED"));
    } else if (cmd == "/newinv") {
        // T294: ONE body, shared with the `I` keybind — the toggle used to
        // be written out here and IrrlichtRenderer::toggleInventory() (what
        // `I` actually reaches) was an empty stub, so the key did nothing.
        toggleInventory();
        chat(fmt::format("New UI inventory: {}",
            isInventoryOpen() ? "OPEN" : "CLOSED"));
    } else if (cmd == "/timestamp" || cmd == "/timestamps" || cmd == "/ts") {
        // T139: dead stub on both ends (confirmed T138) -- "ts" alias added
        // for consistency with the other aliases in this unit even though
        // the command itself does nothing; see Review for why the stub
        // itself was kept rather than deleted (registry-3's catch-all
        // reports "queued" regardless of whether anything is behind the
        // name, so removing this reply would only lose information, not
        // fix the outcome= mislabeling).
        chat("Timestamps command not available (WindowManager removed)");
    } else if (cmd == "/filter") {
        chat("Filter command not available (WindowManager removed)");
    } else if (cmd == "/reloadeffects" || cmd == "/particlereload" || cmd == "/reloadfx" || cmd == "/reloadparticles") {
        if (auto* pm = getParticleManager()) {
            pm->reloadSettings();
            chat("Environment effects settings reloaded");
        } else {
            chat("Particle system not available");
        }
    } else if (cmd == "/reloadweather" || cmd == "/weatherreload" || cmd == "/weather_reload") {
        if (auto* we = getWeatherEffects()) {
            if (we->reloadConfig()) {
                chat("Weather effects settings reloaded");
            } else {
                chat("Failed to reload weather settings");
            }
        } else {
            chat("Weather effects system not available");
        }
    } else if (cmd == "/spellreload" || cmd == "/reloadspells" || cmd == "/reloadspellfx") {
        // T142: moved from eq_commands_movement.cpp -- was one of the two
        // documented exceptions calling a graphics-only singleton directly
        // from the game thread (T138 found no lock protecting it against
        // this, the render thread). Now the mutation happens here, on the
        // render thread, so the race dissolves by construction.
        EQT::Graphics::Environment::SpellEffectsConfig::instance().reload();
        chat("Spell effect settings reloaded");
    } else if (cmd == "/weatherquality" || cmd == "/wq" || cmd == "/weatherpreset") {
        // T142: moved from eq_commands_movement.cpp -- see /spellreload
        // above for why (same lock-free cross-thread mutation defect,
        // same fix).
        auto& loader = EQT::Graphics::WeatherConfigLoader::instance();

        if (args.empty()) {
            // Show current preset
            auto preset = loader.getQualityPreset();
            std::string presetName = EQT::Graphics::WeatherQualityManager::presetToString(preset);

            chat(fmt::format("Weather quality: {}", presetName));
            chat("Available: low, medium, high, ultra, custom");
        } else {
            // Set new preset
            if (loader.setQualityPreset(args)) {
                auto preset = loader.getQualityPreset();
                std::string presetName = EQT::Graphics::WeatherQualityManager::presetToString(preset);
                chat(fmt::format("Weather quality set to: {}", presetName));
            } else {
                chat("Invalid preset. Use: low, medium, high, ultra, custom");
            }
        }
    }
    // --- Detail ---
    // T139: detaildensity/grass were game-registry aliases (eq_commands_
    // spells.cpp, deleted) resolved only there.
    else if (cmd == "/detail" || cmd == "/detaildensity" || cmd == "/grass") {
        if (auto* dm = getDetailManager()) {
            if (!args.empty()) {
                float density = std::atof(args.c_str()) / 100.0f;
                dm->setDensity(std::max(0.0f, std::min(1.0f, density)));
                chat(fmt::format("Detail density: {}%", static_cast<int>(dm->getDensity() * 100)));
            } else {
                chat(fmt::format("Detail density: {}%", static_cast<int>(dm->getDensity() * 100)));
            }
        }
    } else if (cmd == "/togglegrass") {
        if (auto* dm = getDetailManager()) {
            using DC = Detail::DetailCategory;
            dm->setCategoryEnabled(DC::Grass, !dm->isCategoryEnabled(DC::Grass));
            chat(fmt::format("Grass: {}", dm->isCategoryEnabled(DC::Grass) ? "ON" : "OFF"));
        }
    } else if (cmd == "/toggleplants") {
        if (auto* dm = getDetailManager()) {
            using DC = Detail::DetailCategory;
            dm->setCategoryEnabled(DC::Plants, !dm->isCategoryEnabled(DC::Plants));
            chat(fmt::format("Plants: {}", dm->isCategoryEnabled(DC::Plants) ? "ON" : "OFF"));
        }
    } else if (cmd == "/togglerocks") {
        if (auto* dm = getDetailManager()) {
            using DC = Detail::DetailCategory;
            dm->setCategoryEnabled(DC::Rocks, !dm->isCategoryEnabled(DC::Rocks));
            chat(fmt::format("Rocks: {}", dm->isCategoryEnabled(DC::Rocks) ? "ON" : "OFF"));
        }
    } else if (cmd == "/toggledebris") {
        if (auto* dm = getDetailManager()) {
            using DC = Detail::DetailCategory;
            dm->setCategoryEnabled(DC::Debris, !dm->isCategoryEnabled(DC::Debris));
            chat(fmt::format("Debris: {}", dm->isCategoryEnabled(DC::Debris) ? "ON" : "OFF"));
        }
    } else if (cmd == "/season") {
        if (auto* dm = getDetailManager()) {
            if (!args.empty()) {
                Detail::Season season = Detail::Season::Default;
                if (args == "snow" || args == "winter") season = Detail::Season::Snow;
                else if (args == "autumn" || args == "fall") season = Detail::Season::Autumn;
                else if (args == "desert") season = Detail::Season::Desert;
                else if (args == "swamp") season = Detail::Season::Swamp;
                dm->setSeasonOverride(season);
                chat(fmt::format("Season override: {}", args));
            } else {
                chat("Usage: /season [snow|autumn|desert|swamp|default]");
            }
        }
    } else if (cmd == "/detailinfo") {
        if (auto* dm = getDetailManager()) {
            chat(dm->getDebugInfo());
        }
    }
    // --- Governor ---
    // T139: gov/budget were game-registry aliases (eq_commands_movement.cpp,
    // deleted) resolved only there.
    else if (cmd == "/governor" || cmd == "/gov" || cmd == "/budget") {
        auto* gov = getGovernor();
        if (!gov) { chat("Governor not available"); return; }
        if (args.empty() || args == "status") {
            chat(fmt::format("Governor: {} | avg {:.1f}ms | target {:.1f}ms ({:.0f} fps) | ratio {:.2f}{}",
                gov->getStateName(), gov->getAverageFrameTimeMs(),
                gov->getTargetFrameTimeMs(), gov->getTargetFps(),
                gov->getBudgetRatio(), gov->isForced() ? " (FORCED)" : ""));
        } else if (args.substr(0, 4) == "fps ") {
            try {
                float fps = std::stof(args.substr(4));
                if (fps < 10.0f || fps > 120.0f) { chat("FPS must be between 10 and 120"); return; }
                gov->setTargetFps(fps);
                chat(fmt::format("Governor target FPS set to {:.0f} ({:.1f}ms budget)", fps, gov->getTargetFrameTimeMs()));
            } catch (...) { chat("Usage: /governor fps <10-120>"); }
        } else if (args.substr(0, 6) == "force ") {
            std::string state = args.substr(6);
            if (state == "green") { gov->forceState(EQT::Graphics::BudgetState::Green); chat("Governor forced to GREEN (all loading allowed)"); }
            else if (state == "yellow") { gov->forceState(EQT::Graphics::BudgetState::Yellow); chat("Governor forced to YELLOW (light loading only)"); }
            else if (state == "red") { gov->forceState(EQT::Graphics::BudgetState::Red); chat("Governor forced to RED (critical loading only)"); }
            else { chat("Usage: /governor force green|yellow|red"); }
        } else if (args == "auto") {
            gov->clearForcedState();
            chat("Governor set to AUTO (state determined by frame times)");
        } else {
            chat("Usage: /governor [status|fps <N>|force green|yellow|red|auto]");
        }
    } else if (cmd == "/placeholder") {
        if (getGovernor()) {
            chat("Zone placeholder is managed automatically during progressive loading");
        }
    }
    else {
        LOG_WARN(MOD_GRAPHICS, "Unknown renderer command: {}", command);
    }
}


} // namespace Graphics
} // namespace EQT
