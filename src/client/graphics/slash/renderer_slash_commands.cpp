#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/slash/slash_commands_component.h"
#include "client/data_paths.h"  // B1063: the screenshot's default path
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
#include "client/graphics/ui/ui_layout_table.h"  // B1075: closesOnOpen()
#include "client/graphics/ui/spellbook_page_turn.h"  // B1076 fix-2: the ONE spread-turn body
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
#include <ctime>  // B1063: the screenshot stamp
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

// T461 moved /camera's mode-name parsing (T66's) into
// client/graphics/player/camera_mode.h and the rest of its grammar into
// client/graphics/slash/camera_command.h, both device-free and tested there.

// D20d: Handle renderer-specific slash commands forwarded from game thread
void SlashCommandsComponent::processSlashCommand(const std::string& command) {
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
        if (ctx_.bridge) {
            eqt::state::ChatMessageData data;
            data.message = msg;
            data.channelType = 0;
            data.channelName = "system";
            ctx_.bridge->pushEvent(eqt::state::GameEvent(
                eqt::state::GameEventType::SystemMessage, std::move(data)));
        }
        LOG_INFO(MOD_MAIN, "{}", msg);
    };

    // --- Visual rendering ---
    if (cmd == "/sort") {
        ctx_.Diagnostics().toggleManualZoneDraw();
        chat(fmt::format("Front-to-back zone sorting: {}", owner_.isManualZoneDrawEnabled() ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/mergedraws") {
        ctx_.Diagnostics().toggleMergeZoneDraws();
        chat(fmt::format("Zone draw merge: {}", owner_.isMergeZoneDrawsEnabled() ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/option" || cmd == "/filter" || cmd == "/showspelleffects") {
        // B430/B433: the Options window's command half -- same model, same
        // apply body as a click (FrameComponent::runOptionsCommand).
        for (const auto& line : ctx_.Frame().runOptionsCommand(cmd, args)) chat(line);
    } else if (cmd == "/screenshot") {
        // P10: smallest additive hook for the in-app A/B capture (no other
        // scriptable screenshot command existed — F12 is a raw key event,
        // not reachable from --script's slash-command tour list). Explicit
        // filename arg so a tour script can capture deterministic
        // before/after pairs at a fixed pose.
        // B1063 (#383): the typed /screenshot and the F12 hotkey are ONE
        // body now -- runScreenshot(). No argument is the default
        // screenshots/<zone>_<stamp>.png under the cache root (previously
        // "screenshot.png" in the process's cwd); an explicit argument is
        // the path, so a tour script's fixed-pose pairs are unchanged.
        runScreenshot(args);
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
        //
        // T461: the official camera set, and EXACT placement for the oracle
        // rig that solves fable's cameras against the official client's --
        // `/camera set|nudge|get`. The grammar is slash/camera_command.h and
        // the execution PlayerCameraComponent::runCameraCommand(); every reply
        // line goes out through chat(), so a --script run can read it back.
        for (const auto& line : ctx_.PlayerCamera().runCameraCommand(args)) chat(line);
    } else if (cmd == "/fpcam") {
        // T310 Leg A -- the first-person camera's numbers, settable at
        // runtime so solving the pose against the official client does not
        // cost a rebuild per candidate value (Will, 2026-08-29: "This should
        // not require recompiling"). Registry 3, because the camera lives on
        // this object and must be driven from the RENDER thread
        // (Application::dispatchToActionOrRenderer in
        // application_commands.cpp -- never call renderer methods from the
        // game thread). The catalog row is in
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
            for (const auto& line : ctx_.PlayerCamera().buildFirstPersonCameraDumpLines()) chat(line);
        } else {
            for (const auto& line : EQT::Graphics::FirstPersonTunablesCommandLines(args)) {
                chat(line);
            }
            // A set/reset must take effect on the CURRENT frame, not on the
            // next time something else happens to move the player.
            if (sub == "set" || sub == "reset") {
                ctx_.PlayerCamera().applyCameraProjectionForMode();
                ctx_.PlayerCamera().repositionFirstPersonCamera();
            }
        }
    } else if (cmd == "/upload") {
        ctx_.ZoneLifecycle().toggleGPUUploadThread();
        chat(fmt::format("GPU upload thread: {}", ctx_.ZoneLifecycle().isGPUUploadThreadEnabled() ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/portal") {
        if (args == "debug") {
            ctx_.Diagnostics().togglePortalDebugDraw();
            chat(fmt::format("Portal debug overlay: {}", owner_.isPortalDebugDrawEnabled() ? "ENABLED" : "DISABLED"));
        } else {
            ctx_.Diagnostics().togglePortalOcclusion();
            chat(fmt::format("Portal occlusion: {}", owner_.isPortalOcclusionEnabled() ? "ENABLED" : "DISABLED"));
        }
    } else if (cmd == "/stencil") {
        ctx_.Diagnostics().toggleStencilDebugDraw();
        chat(fmt::format("Stencil debug overlay: {}", owner_.isStencilDebugDrawEnabled() ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/plight") {
        ctx_.Diagnostics().togglePlayerLight();
        chat(fmt::format("Per-pixel player light: {}", owner_.isPlayerLightEnabled() ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/olight") {
        ctx_.Diagnostics().toggleObjectLights();
        chat(fmt::format("Object lights: {}", owner_.isObjectLightsEnabled() ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/zlight") {
        ctx_.Diagnostics().toggleDirectionalLight();
        chat(fmt::format("Directional light: {}", owner_.isDirectionalLightEnabled() ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/lightmode") {
        if (ctx_.zoneShader) {
            bool unified = !ctx_.zoneShader->unifiedLighting();
            ctx_.zoneShader->setUnifiedLighting(unified);
            chat(fmt::format("Lighting mode: {}", unified ? "UNIFIED (pointLight * aColor)" : "ADDITIVE (pointLight + base*aColor)"));
            LOG_INFO(MOD_SHADER, "Lighting mode toggled to {}", unified ? "unified" : "additive");
        }
    } else if (cmd == "/bakedlight") {
        // T408 (B110): the probe that solved the baked-light composition.
        // `/bakedlight <mode> [gain] [skyweight]` -- mode 0 off (pre-T408),
        // 1 additive, 2 multiplicative, 3 max. It exists so ONE live session
        // against the official client can sweep the whole family; solving it
        // with a rebuild per candidate is what made T405 cost six sessions.
        if (ctx_.zoneShader) {
            int mode = EQT::Graphics::kBakedLightMode;
            float gain = EQT::Graphics::kBakedLightGain;
            float sky = EQT::Graphics::kBakedLightSkyWeight;
            if (!args.empty()) {
                std::istringstream iss(args);
                iss >> mode;
                if (!(iss >> gain)) gain = EQT::Graphics::kBakedLightGain;
                if (!(iss >> sky)) sky = EQT::Graphics::kBakedLightSkyWeight;
            }
            ctx_.zoneShader->setBakedLight(gain, sky, static_cast<float>(mode));
            chat(fmt::format("Baked vertex light: mode={} gain={:.4f} skyWeight={:.4f}",
                             mode, gain, sky));
            LOG_INFO(MOD_SHADER, "BakedLight probe: mode={} gain={:.4f} skyWeight={:.4f}",
                     mode, gain, sky);
        }
    } else if (cmd == "/nightambient") {
        // T428 (board B280): the probe that solved fable's night base against
        // the official client. `/nightambient <r> <g> <b>` sets the night end
        // of the day/night curve, `/nightambient reset` puts the committed
        // constant back, and no argument prints the current triple. Same
        // reason /bakedlight exists: one live session sweeps a candidate
        // list, where a compile-time knob costs a build and a session each.
        const AmbientRGB current = owner_.nightAmbientProbe();
        if (args.empty()) {
            chat(fmt::format("Night ambient: r={:.5f} g={:.5f} b={:.5f}",
                             current.r, current.g, current.b));
        } else if (args == "reset") {
            owner_.setNightAmbientProbe(AmbientRGB{
                EQT::Graphics::kNightAmbient[0], EQT::Graphics::kNightAmbient[1],
                EQT::Graphics::kNightAmbient[2]});
            const AmbientRGB now = owner_.nightAmbientProbe();
            chat(fmt::format("Night ambient reset: r={:.5f} g={:.5f} b={:.5f}",
                             now.r, now.g, now.b));
            LOG_INFO(MOD_GRAPHICS, "NightAmbient probe: r={:.5f} g={:.5f} b={:.5f}",
                     now.r, now.g, now.b);
        } else {
            AmbientRGB next = current;
            std::istringstream iss(args);
            if (!(iss >> next.r)) next.r = current.r;
            if (!(iss >> next.g)) next.g = current.g;
            if (!(iss >> next.b)) next.b = current.b;
            owner_.setNightAmbientProbe(next);
            chat(fmt::format("Night ambient: r={:.5f} g={:.5f} b={:.5f}",
                             next.r, next.g, next.b));
            LOG_INFO(MOD_GRAPHICS, "NightAmbient probe: r={:.5f} g={:.5f} b={:.5f}",
                     next.r, next.g, next.b);
        }
    } else if (cmd == "/shaderdebug" || cmd == "/sd") {
        if (ctx_.zoneShader) {
            int mode = 0;
            if (!args.empty()) {
                if (args == "off") mode = 0;
                else mode = std::atoi(args.c_str());
            }
            int prevMode = ctx_.zoneShader->shaderDebug();
            ctx_.zoneShader->setShaderDebug(mode);

            // Swap materials to/from debug program
            irr::s32 debugMat = ctx_.zoneShader->getDebugMaterialType();
            if (debugMat >= 0) {
                if (mode > 0 && prevMode == 0) {
                    // Entering debug: swap all per-pixel materials to debug program
                    ctx_.Diagnostics().swapToDebugShader(debugMat);
                    LOG_INFO(MOD_SHADER, "Swapped all per-pixel materials to debug program {}", debugMat);
                } else if (mode == 0 && prevMode > 0) {
                    // Leaving debug: restore production materials
                    ctx_.Diagnostics().swapFromDebugShader(debugMat);
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
        if (auto* pm = owner_.getParticleManager()) {
            pm->toggleUnifiedFire();
            chat("Fire particles toggled");
        }
    } else if (cmd == "/sky") {
        owner_.toggleSky();
        chat(fmt::format("Sky rendering: {}", owner_.isSkyEnabled() ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/skytype") {
        if (!args.empty()) {
            int type = std::atoi(args.c_str());
            owner_.forceSkyType(static_cast<uint8_t>(type));
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
                owner_.setRenderDistance(dist);
                chat(fmt::format("Render distance: {:.0f}", dist));
            } else {
                chat("Render distance must be between 50 and 10000");
            }
        } else {
            chat(fmt::format("Render distance: {:.0f}", owner_.getRenderDistance()));
        }
    } else if (cmd == "/clipdist" || cmd == "/clip" || cmd == "/farplane") {
        if (!args.empty()) {
            float dist = std::atof(args.c_str());
            owner_.setClipDistance(dist);
            chat(fmt::format("Clip distance: {:.0f}", dist));
        } else {
            chat(fmt::format("Clip distance: {:.0f}", owner_.getClipDistance()));
        }
    }
    // --- Profiling/debug ---
    // T139: frametime/ft, sp/scenebreak, ds, sw were game-registry aliases
    // (eq_commands_movement.cpp, deleted) resolved only there -- added here
    // for the same reason as the renderdist/clipdist aliases above.
    else if (cmd == "/frametiming" || cmd == "/frametime" || cmd == "/ft") {
        bool newState = !owner_.isFrameTimingEnabled();
        owner_.setFrameTimingEnabled(newState);
        if (newState) {
            chat("Frame timing profiler ENABLED - check console for breakdown every ~2 seconds");
        } else {
            chat("Frame timing profiler DISABLED");
        }
    } else if (cmd == "/sceneprofile" || cmd == "/sp" || cmd == "/scenebreak") {
        ctx_.Diagnostics().runSceneProfile();
        chat("Scene profile running - check console for breakdown");
    } else if (cmd == "/dumpscene" || cmd == "/ds") {
        ctx_.Diagnostics().dumpScene();
        chat("Scene dump written to console log");
    } else if (cmd == "/simworker" || cmd == "/sw") {
        auto info = ctx_.SimBridge().getSimWorkerDebugInfo();
        for (const auto& line : info) chat(line);
    }
    // T108: "/pmem" moved off this empty-MemoryReportInput{} path (runLoadDiagnostics
    // had zero callers, per T105/T106) — EverQuest::runPmemDiagnostics() is now
    // called directly by the command handler; see irrlicht_bridge.cpp.
    // --- UI ---
    // T301 Leg D: the scriptable half of the UI-hide toggle. F10
    // (HotkeyAction::ToggleAllUI; B582 moved it off F11, the official
    // netstat key) has hidden the UI since long before this,
    // but a raw key event is not reachable from a --script run or from any
    // capture harness that does not own an X server -- the same gap /camera
    // closed for camera modes in T66. Every visual gate since T63 has fought
    // the overlay; T300 Leg C had to record its 285-frame combat negative
    // with the chat box and hotbar opaque over the bottom of every frame.
    else if (cmd == "/hideui" || cmd == "/toggleui" || cmd == "/ui") {
        ctx_.Diagnostics().toggleAllUI();
        chat(fmt::format("UI: {}", owner_.isAllUIHidden() ? "HIDDEN" : "VISIBLE"));
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
            ctx_.eventReceiver ? ctx_.eventReceiver->getMouseX() : 0,
            ctx_.eventReceiver ? ctx_.eventReceiver->getMouseY() : 0,
            ctx_.Frame().uiLayout_, ctx_.Frame().chatPanelState_, ctx_.Frame().skillsPopupState_,
            ctx_.Frame().lootState_, ctx_.Frame().itemDisplayState_));
    }
    else if (cmd == "/newui") {
        ctx_.Frame().newUIEnabled_ = !ctx_.Frame().newUIEnabled_;
        chat(fmt::format("New static UI: {}", ctx_.Frame().newUIEnabled_ ? "ENABLED" : "DISABLED"));
    } else if (cmd == "/newbook") {
        // B1077: the shared body -- the Ctrl+B keybind and the gem window's
        // CSPW_SpellBook button reach the same toggleSpellbookWindow().
        const bool open = ctx_.Frame().toggleSpellbookWindow();
        chat(fmt::format("New UI spellbook: {}", open ? "OPEN" : "CLOSED"));
    } else if (cmd == "/newbookprev") {
        // The ONE spread-turn body (spellbook_page_turn.h), shared with the
        // book's edge arrows -- the arrow's SPELLS_PER_PAGE off-by-16 defect
        // could not exist here any more, because both call the same step.
        EQT::Graphics::turnSpellbookSpreadBack(ctx_.Frame().spellbookState_);
    } else if (cmd == "/newbooknext") {
        EQT::Graphics::turnSpellbookSpreadForward(ctx_.Frame().spellbookState_);
    } else if (cmd == "/skills") {
        // T363 (B058, Will's ruling 2026-09-05): renamed from /newskills, no
        // alias -- the real client's name for the window now works. The body
        // moved to FrameComponent::toggleSkillsWindow() so the K keybind
        // reaches THIS behaviour (popup mutual exclusion included) and the two
        // cannot drift again; same shape as T294's /inventory.
        chat(fmt::format("New UI skills: {}",
            ctx_.Frame().toggleSkillsWindow() ? "OPEN" : "CLOSED"));
    } else if (cmd == "/friendswindow") {
        // B897: the scriptable half of Alt+F -- one body,
        // FrameComponent::toggleFriendsWindow(), shared with the key.
        chat(fmt::format("Friends window: {}",
            ctx_.Frame().toggleFriendsWindow() ? "OPEN" : "CLOSED"));
    } else if (cmd == "/inventory") {
        // T294: ONE body, shared with the `I` keybind — the toggle used to
        // be written out here and IrrlichtRenderer::toggleInventory() (what
        // `I` actually reaches) was an empty stub, so the key did nothing.
        // T365 (B090): renamed from /newinv, no alias -- the same `new*`
        // naming Will ruled against for /skills in B058, and it is what lets
        // the bare `inventory` keyword on piped stdin reach this body. `inv`
        // is deliberately NOT an alias: registry 2 already owns it for
        // /invite and would win the dispatch.
        owner_.toggleInventory();
        chat(fmt::format("New UI inventory: {}",
            owner_.isInventoryOpen() ? "OPEN" : "CLOSED"));
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
        if (auto* pm = owner_.getParticleManager()) {
            pm->reloadSettings();
            chat("Environment effects settings reloaded");
        } else {
            chat("Particle system not available");
        }
    } else if (cmd == "/reloadweather" || cmd == "/weatherreload" || cmd == "/weather_reload") {
        if (auto* we = owner_.getWeatherEffects()) {
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
        if (auto* dm = owner_.getDetailManager()) {
            if (!args.empty()) {
                float density = std::atof(args.c_str()) / 100.0f;
                dm->setDensity(std::max(0.0f, std::min(1.0f, density)));
                chat(fmt::format("Detail density: {}%", static_cast<int>(dm->getDensity() * 100)));
            } else {
                chat(fmt::format("Detail density: {}%", static_cast<int>(dm->getDensity() * 100)));
            }
        }
    } else if (cmd == "/togglegrass") {
        if (auto* dm = owner_.getDetailManager()) {
            using DC = Detail::DetailCategory;
            dm->setCategoryEnabled(DC::Grass, !dm->isCategoryEnabled(DC::Grass));
            chat(fmt::format("Grass: {}", dm->isCategoryEnabled(DC::Grass) ? "ON" : "OFF"));
        }
    } else if (cmd == "/toggleplants") {
        if (auto* dm = owner_.getDetailManager()) {
            using DC = Detail::DetailCategory;
            dm->setCategoryEnabled(DC::Plants, !dm->isCategoryEnabled(DC::Plants));
            chat(fmt::format("Plants: {}", dm->isCategoryEnabled(DC::Plants) ? "ON" : "OFF"));
        }
    } else if (cmd == "/togglerocks") {
        if (auto* dm = owner_.getDetailManager()) {
            using DC = Detail::DetailCategory;
            dm->setCategoryEnabled(DC::Rocks, !dm->isCategoryEnabled(DC::Rocks));
            chat(fmt::format("Rocks: {}", dm->isCategoryEnabled(DC::Rocks) ? "ON" : "OFF"));
        }
    } else if (cmd == "/toggledebris") {
        if (auto* dm = owner_.getDetailManager()) {
            using DC = Detail::DetailCategory;
            dm->setCategoryEnabled(DC::Debris, !dm->isCategoryEnabled(DC::Debris));
            chat(fmt::format("Debris: {}", dm->isCategoryEnabled(DC::Debris) ? "ON" : "OFF"));
        }
    } else if (cmd == "/season") {
        if (auto* dm = owner_.getDetailManager()) {
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
        if (auto* dm = owner_.getDetailManager()) {
            chat(dm->getDebugInfo());
        }
    }
    // --- Governor ---
    // T139: gov/budget were game-registry aliases (eq_commands_movement.cpp,
    // deleted) resolved only there.
    else if (cmd == "/governor" || cmd == "/gov" || cmd == "/budget") {
        auto* gov = owner_.getGovernor();
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
        if (owner_.getGovernor()) {
            chat("Zone placeholder is managed automatically during progressive loading");
        }
    }
    else {
        LOG_WARN(MOD_GRAPHICS, "Unknown renderer command: {}", command);
    }
}

// B1063 (#383): the screenshot's ONE body. The F12 hotkey reaches it through
// FrameComponent::renderFrameTail() (the Receiver's HA::Screenshot queues
// RA::Screenshot, which FrameComponent defers to after the frame is drawn
// and before the swap); the typed /screenshot reaches it through the chain
// above. The chat line "Screenshot <path>: saved|FAILED" comes from HERE,
// so the hotkey and the command cannot disagree about WHERE the file went or
// WHICH one confirmed.
//
// An empty filename is the default: screenshots/<zone>_<stamp>.png under the
// first writable cache root (the $WILLEQ_CACHE_DIR walk, T402) -- the
// official client writes its capture next to the client and names it per
// zone, and that is the behaviour we match (a predictable folder + a chat
// confirmation), not its .jpg. An explicit filename (a tour script's fixed
// before/after pair, or /screenshot's argument) is used as given; a relative
// name's parent directory is created.
bool SlashCommandsComponent::runScreenshot(const std::string& filename) {
    std::string path;
    if (filename.empty()) {
        // screenshots/<zone>_<YYYYMMDD-HHMMSS>.png (the issue's pattern),
        // under the first writable cache root.
        const std::time_t now = std::time(nullptr);
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&now));
        const std::string leaf = sanitizeZoneName(owner_.getCurrentZoneName()) +
                                 "_" + std::string(stamp) + ".png";
        const std::string cached =
            eqt::ResolveCachePathForWrite("screenshots/" + leaf);
        if (cached.empty()) {
            // No writable cache root anywhere: write to the client's cwd
            // under screenshots/ rather than inventing a cache root.
            path = "screenshots/" + leaf;
            std::error_code ec;
            std::filesystem::create_directories("screenshots", ec);
        } else {
            path = cached;  // "screenshots/" created by the resolver
        }
    } else {
        path = filename;
        const std::filesystem::path p(path);
        if (p.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(p.parent_path(), ec);
        }
    }
    const bool ok = owner_.saveScreenshot(path);
    // Same chat idiom as the chain's chat() lambda (registry 3's SystemMessage
    // push + MOD_MAIN echo, T233) so a --script run and a log grep see it.
    if (ctx_.bridge) {
        eqt::state::ChatMessageData data;
        data.message = fmt::format("Screenshot {}: {}", path, ok ? "saved" : "FAILED");
        data.channelType = 0;
        data.channelName = "system";
        ctx_.bridge->pushEvent(eqt::state::GameEvent(
            eqt::state::GameEventType::SystemMessage, std::move(data)));
    }
    LOG_INFO(MOD_MAIN, "Screenshot {}: {}", path, ok ? "saved" : "FAILED");
    return ok;
}

// B1063: the default screenshot filename's zone component. Pure, so it is
// unit-tested without a renderer. A zone with no filesystem-safe character
// (empty, or only unsafe ones) becomes "nozone"; every other character that
// is not [A-Za-z0-9_-] is replaced by '_' (a zone name with a space still
// names itself).
std::string SlashCommandsComponent::sanitizeZoneName(const std::string& zoneName) {
    std::string safe;
    safe.reserve(zoneName.size());
    for (char c : zoneName) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-';
        safe.push_back(ok ? c : '_');
    }
    if (safe.find_first_not_of("_-") == std::string::npos) {
        return "nozone";
    }
    return safe;
}

} // namespace Graphics
} // namespace EQT
