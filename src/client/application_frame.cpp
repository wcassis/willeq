#include "client/application.h"
#include <json/json.h>  // Must be before eq.h → logging.h so JSONCPP_VERSION_STRING is defined
#include "client/eq.h"
#include "client/help/merged_help.h"
// T310 Leg A: --fpcam. No renderer dependency -- the tunable store is a plain
// process-global of floats (see the header), so this include is safe in an
// EQT_GRAPHICS=OFF build too.
#include "client/graphics/first_person_tunables.h"
#include "client/help/renderer_command_catalog.h"
#include "client/eq_action_handler.h"
#include "client/combat.h"
#include "client/inventory/inventory_manager.h"
#include "client/mode/automated_mode.h"
#include "client/mode/headless_mode.h"
#include "client/mode/graphical_mode.h"
#include "client/spell/spell_manager.h"
#include "client/spell/spell_constants.h"
#include "common/util/json_config.h"
#include "common/logging.h"
#include "common/performance_metrics.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <thread>
#include <algorithm>
#include <set>
#include <chrono>
#include <cctype>
#include <sys/stat.h>

#ifndef _WIN32
#include <signal.h>
#endif

// P28: GameStateBridge/ConsoleBridge are the headless bridge pair — needed
// unconditionally (ConsoleBridge is attached in headless mode; m_bridge is
// used from updateScript() regardless of graphics). Only the renderer/input
// headers that actually require Irrlicht stay gated.
#include "client/bridge/console_bridge.h"
#include "client/bridge/game_state_bridge.h"
#include "client/bridge/game_state_sync_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/state/event_bus.h"
#include "client/hotbar/hotbar_model.h"
#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/pose_log_format.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/input/hotkey_manager.h"
#include "client/input/graphics_input_handler.h"
#include "client/bridge/irrlicht_bridge.h"
#endif

#ifdef WITH_AUDIO
#include "client/audio/audio_manager.h"
#endif

namespace eqt {

void Application::updateGameState(float deltaTime) {
    // D21a: Game-side tick (spell/buff updates + intent processing)
    // T45: was `m_graphicsInitialized` alone -- that flag means "the renderer
    // subsystem exists", not "the game is ready to tick", and is permanently
    // false in a --no-graphics run, so GameTick() (spell cooldowns,
    // memorization, cast timeouts, buff expiry, intent draining) never ran
    // headless at all (refactor/deferred.md, T44 finding). Fixed by OR'ing in
    // EverQuest::IsGraphicsReady() (loading phase reached COMPLETE), the same
    // game-lifecycle "state ready" signal T41 used for the full-state
    // snapshot -- true once EverQuest::OnGameStateComplete()'s headless arm
    // runs (immediately at zone-in, no graphics phase to wait on).
    //
    // Graphics mode is provably unaffected: m_graphicsInitialized is a
    // one-way latch, set once inside Init() before gameThreadLoop() (and
    // therefore this function) is ever called, and reset only by
    // ShutdownGraphics() at teardown -- see application.h's own comment on
    // the member ("Set once at init, read by both (safe)") and InitGraphics's
    // failure path returning false out of Init() entirely rather than
    // proceeding with the flag false. So in every graphics-mode run,
    // m_graphicsInitialized is already true for every single iteration of
    // this loop, including during zone loading (re-zones do not reset it) --
    // OR'ing in a second condition that can only ever help the *headless*
    // side cannot change graphics-mode timing.
    if (m_eqClient && (m_graphicsInitialized || m_eqClient->IsGraphicsReady())) {
        m_eqClient->GameTick(deltaTime);
    }

    // Update movement in EverQuest
    if (m_eqClient) {
        m_eqClient->Movement().UpdateMovement();

        // T26: advance the loot state machine every game tick, unconditionally
        // (graphics or headless) -- same rationale as UpdateMovement() above.
        // GameTick() (guarded by m_graphicsInitialized, just above) is NOT a
        // valid home: the loot path fires in headless mode too. (T245: the
        // 2 Hz m_update_thread this comment used to warn about is gone --
        // CombatManager::Update() is now ticked inside UpdateMovement() on
        // this same thread, gated on IsFullyZonedIn(); the loot machine stays
        // here because it must run before zone-in completes too -- see
        // refactor/T26_loot_state_machine.md.)
        if (CombatManager* cm = m_eqClient->GetCombatManager()) {
            cm->UpdateLootState();
        }
    }

    // syncGameStateFromClient() was deleted (T104) -- it had no remaining
    // work; see application.h's "Synchronization" section for the history.

    // Update loading progress
    updateLoadingProgress();

    // D21a: Audio update (was in PostRenderTick — game thread responsibility)
    if (m_eqClient && m_graphicsInitialized) {
        m_eqClient->PostRenderTick(deltaTime);
    }

    // D25: In headless mode, drain and apply events to the console bridge
    // (single stage: GameStateSyncBridge::applyEvent() forwards to
    // ConsoleBridge right here on the game thread).
    if (m_eqClient && m_consoleBridge && !m_graphicsInitialized) {
        m_eqClient->Bridge().ProcessBridgeEvents();
    }
#ifdef EQT_HAS_GRAPHICS
    // T220: graphics runs -- stage 1 of the split drain, on the GAME thread.
    // Drains the EverQuest->bridge event queue and stages the events for
    // the render thread. Before T220 the whole drain ran on the render
    // thread (render() -> PreRenderTick()) and carried a GameState
    // projection that raced every game-thread read (TSAN-confirmed;
    // refactor/T220_state_projection_on_game_thread.md); T227 deleted the
    // projection itself (one GameState, EverQuest::m_game_state, written and
    // read on this thread). Stage 2 is render()'s forwardStagedEvents().
    else if (m_eqClient && m_stateSyncBridge && m_graphicsInitialized) {
        m_stateSyncBridge->stagePendingEvents();
    }
#endif
}

void Application::render(float deltaTime) {
#ifdef EQT_HAS_GRAPHICS
    if (m_graphicsInitialized && m_eqClient && m_renderer) {
        auto now = std::chrono::steady_clock::now();
        float gfxDeltaTime = std::chrono::duration<float>(now - m_lastGraphicsUpdate).count();

        if (gfxDeltaTime >= 1.0f / 60.0f) {
            // Loading thread active — it owns the GL context, just check if done
            if (isLoadingThreadActive()) {
                checkLoadingComplete();
                m_lastGraphicsUpdate = now;
                return;
            }

            // Game thread posted a zone load — start the loading thread. B917:
            // startLoadingThread() TAKES the snapshot out of the handoff, so a
            // request posted while the previous load was still running is
            // simply still pending here once that load has been joined.
            if (m_zoneLoadHandoff.hasPending()) {
                startLoadingThread();
                m_lastGraphicsUpdate = now;
                return;  // Loading thread now owns GL context
            }

            // Network handshake phase (0-50%): render loading screen on main thread
            if (!m_loadingStatus.loadingComplete.load(std::memory_order_relaxed) &&
                m_loadingStatus.percent.load(std::memory_order_relaxed) < 50) {
                float progress = m_loadingStatus.percent.load(std::memory_order_relaxed) / 100.0f;
                std::wstring title = m_loadingStatus.getTitle();
                std::string textNarrow = m_loadingStatus.getText();
                std::wstring text(textNarrow.begin(), textNarrow.end());
                EQT::Graphics::LoadingThread::drawLoadingFrame(
                    m_renderer->getDriver(), m_loadingScreenFont, progress, title, text);
                m_lastGraphicsUpdate = now;
                return;
            }

            // Normal gameplay: forward the game thread's staged bridge
            // events to IrrlichtBridge (T220 stage 2 -- render-thread only,
            // never touches GameState), then render the frame.
            if (m_stateSyncBridge) {
                m_stateSyncBridge->forwardStagedEvents();
            }

            bool result = m_renderer->processFrame(gfxDeltaTime);

            if (!result) {
                LOG_DEBUG(MOD_GRAPHICS, "Graphics window closed");
                m_running.store(false);
            } else if (m_constrainedCache) {
                // B949: the texture cache's LRU now follows draw order. The
                // driver stamped every texture this frame bound (on the GL
                // thread, inside processFrame); reordering the list is a
                // cache-only operation (no GL calls), so it runs here on the
                // render thread, the only thread that reads it back from
                // here, once per frame, after the frame's binds.
                // (m_graphicsInitialized implies the cache exists -- it is
                // built in initConstrainedRendererConfig() before the renderer
                // is -- the check just keeps this line crash-free on its own.)
                m_constrainedCache->endFrame();
            }
            if (result && !m_config.dumpFramesDir.empty() && m_config.dumpFramesEvery > 0
                       && m_dumpFrameIndex < static_cast<uint64_t>(m_config.dumpFramesMaxCount)) {
                // T64: video-capture evidence tool (--dump-frames-dir/--dump-every),
                // extracted from the P12 anim video-check session and made a
                // first-class tool. Use this when the thing under review is
                // temporal -- a weapon appearing mid-swing, a cast, a zone
                // transition, particle behavior -- that a still can't show.
                // Runs on the render thread (same thread that owns the
                // EGL/GL context), same constraint /screenshot's
                // saveScreenshot() already requires. No-op (zero cost beyond
                // one string-empty + two int checks) when the flags aren't
                // passed, so production behavior is untouched by default.
                // dumpFramesMaxCount bounds total frames written regardless
                // of session length -- see the field comment in
                // application.h for why (Pi /tmp tmpfs is small).
                ++m_dumpFrameCounter;
                if (m_dumpFrameCounter % static_cast<uint64_t>(m_config.dumpFramesEvery) == 0) {
                    char filename[64];
                    std::snprintf(filename, sizeof(filename), "/frame_%06llu.png",
                        static_cast<unsigned long long>(m_dumpFrameIndex));
                    std::string path = m_config.dumpFramesDir + filename;
                    m_renderer->saveScreenshot(path);
                    LOG_INFO(MOD_GRAPHICS, "Frame dump: {} (frame counter {})", path, m_dumpFrameCounter);
                    ++m_dumpFrameIndex;
                    if (m_dumpFrameIndex >= static_cast<uint64_t>(m_config.dumpFramesMaxCount)) {
                        LOG_INFO(MOD_GRAPHICS, "Frame dump: reached --dump-max-frames cap ({}), stopping capture",
                            m_config.dumpFramesMaxCount);
                    }
                }
            }

            // B874: pose-log TSV writer. One 14-column TSV row per Nth
            // rendered frame; the row format lives in
            // include/client/graphics/pose_log_format.h (tabs on purpose --
            // the camera-mode field carries spaces) and the whole per-frame
            // body (cadence, gather, wall-clock ts, cap) in
            // include/client/graphics/pose_log_writer.h, which
            // tests/test_pose_log_e2e.cpp drives on a real renderer. The file
            // is opened on the first gameplay frame (main() already proved
            // the path writable), so off-by-default is one string test.
            if (result && !m_config.poseLogPath.empty() && !m_poseLogOpenTried) {
                m_poseLogOpenTried = true;
                std::string err;
                if (m_poseLog.open(m_config.poseLogPath, m_config.poseLogEvery,
                                   m_config.poseLogMaxCount, &err)) {
                    LOG_INFO(MOD_GRAPHICS, "pose-log: writing '{}' every {} frame(s), capped at {} row(s)",
                             m_config.poseLogPath, m_config.poseLogEvery, m_config.poseLogMaxCount);
                } else {
                    LOG_ERROR(MOD_GRAPHICS, "pose-log: {} -- NO pose rows will be written", err);
                }
            }
            if (result && m_poseLog.onRenderedFrame(*m_renderer) && m_poseLog.capReached()) {
                LOG_INFO(MOD_GRAPHICS, "pose-log: reached --pose-max-frames cap ({}), stopping capture",
                         m_config.poseLogMaxCount);
            }
            m_lastGraphicsUpdate = now;
        }
    }
#else
    (void)deltaTime;
#endif
}

} // namespace eqt
