#include "client/application.h"
#include "client/core/memory_ledger.h"  // T504: instant_scene ledger point
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
#include "client/corpse_predicate.h"  // T456 (B478): the ONE corpse predicate
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

// =============================================================================
// D20e2: Zone Loading (moved from EverQuest)
// =============================================================================

#ifdef EQT_HAS_GRAPHICS
void Application::startLoadingThread() {
    if (!m_renderer || !m_graphicsInitialized) return;
    if (m_loadingThread && m_loadingThread->isRunning()) {
        LOG_WARN(MOD_GRAPHICS, "startLoadingThread: loading thread already running");
        return;
    }

    // B917: take the posted snapshot OUT of the handoff. From here until
    // checkLoadingComplete() releases it, m_activeLoadSnapshot is this load's
    // private copy -- a zone change the game thread handles mid-load posts a
    // new snapshot into the (now empty) handoff slot and never touches this.
    if (!m_zoneLoadHandoff.take(m_activeLoadSnapshot, m_activeLoadGeneration)) {
        return;
    }
    LOG_INFO(MOD_GRAPHICS, "startLoadingThread: zone load #{} for '{}'",
             m_activeLoadGeneration, m_activeLoadSnapshot.zoneName);

    // T432 (board B275): zone-in is where the character name and the server
    // name are both known on this (the render) thread -- the snapshot the
    // game thread just published carries the name, and m_config carries the
    // server. Hand them to the renderer; its frame tick loads the saved
    // window states and camera mode on the next frame. Cheap and idempotent:
    // a re-zone hands over the same pair and the renderer ignores it.
    if (!m_activeLoadSnapshot.playerName.empty()) {
        m_renderer->setUiPreferencesIdentity(m_activeLoadSnapshot.playerName,
                                             m_config.server);
    }

    // Extract GL handles while main thread still owns context
    m_glHandles = EQT::Graphics::LoadingThread::extractGLHandles(
        m_renderer->getDevice(), m_renderer->getDriver());

    // Reset loading status
    m_loadingStatus.reset();

    // Get font for loading screen
    irr::gui::IGUIFont* font = nullptr;
    if (m_renderer->getGUIEnvironment())
        font = m_renderer->getGUIEnvironment()->getBuiltInFont();

    // Release GL context on main thread
    EQT::Graphics::LoadingThread::releaseContext(m_glHandles);

    // Set the renderer loading flag so main thread skips GL calls
    m_renderer->setLoading(true);

    // Start loading thread with active phase callback
    m_loadingThread = std::make_unique<EQT::Graphics::LoadingThread>();
    m_loadingThread->start(
        m_renderer->getDevice(), m_renderer->getDriver(), font, m_glHandles,
        m_loadingStatus,
        [this](EQT::Graphics::LoadingStatus& status) {
            loadZoneGraphicsOnThread(status);
        });

    LOG_INFO(MOD_GRAPHICS, "startLoadingThread: loading thread started (backend={})",
             m_glHandles.backend == EQT::Graphics::GLContextHandles::Backend::GLX ? "GLX" :
             m_glHandles.backend == EQT::Graphics::GLContextHandles::Backend::EGL ? "EGL" : "software");
}

void Application::joinLoadingThread() {
    if (!m_loadingThread) return;

    m_loadingThread->join();
    m_loadingThread.reset();

    // Reacquire GL context on main thread
    EQT::Graphics::LoadingThread::acquireContext(m_glHandles);

    // Clear loading flag
    if (m_renderer)
        m_renderer->setLoading(false);

    LOG_INFO(MOD_GRAPHICS, "joinLoadingThread: loading thread joined, GL context reacquired");
}

bool Application::checkLoadingComplete() {
    if (!m_loadingThread) return false;
    if (m_loadingStatus.loadingComplete.load(std::memory_order_acquire)) {
        LOG_INFO(MOD_GRAPHICS, "checkLoadingComplete: loading thread signaled done, joining...");
        joinLoadingThread();

        // Check if loading failed — if so, quit instead of finalizing.
        // B918: recorded in the handoff, which main() reads to return
        // kClientFatalExit instead of 0.
        const bool failed = m_loadingStatus.quitRequested.load(std::memory_order_acquire);
        std::shared_ptr<EQT::Graphics::BspTree> bsp;
        if (!failed && m_renderer) {
            LOG_INFO(MOD_GRAPHICS, "checkLoadingComplete: joined, extracting BSP tree...");
            bsp = m_renderer->getZoneBspTree();
        }
        // D21b + B917: publish the completion for the game thread, tagged with
        // THIS load's generation and the zone it actually built (T260's label,
        // from the loader's own snapshot -- never from a slot a mid-load zone
        // change can have overwritten).
        const bool published = m_zoneLoadHandoff.finishLoad(
            m_activeLoadGeneration, failed, m_activeLoadSnapshot.zoneName, std::move(bsp),
            failed ? "zone loading thread failed for '" + m_activeLoadSnapshot.zoneName + "'"
                   : std::string());
        // P04: the snapshot's entity/door/zone-line copies are dead once the
        // loader is joined; release them now rather than at the next load.
        m_activeLoadSnapshot = eqt::ZoneLoadSnapshot{};

        if (!published) {
            LOG_FATAL(MOD_GRAPHICS, "checkLoadingComplete: loading thread failed — shutting down "
                      "(B918: non-zero exit, {} outside --scenario)", eqt::scenario::kClientFatalExit);
            m_running.store(false);
            return true;
        }
        LOG_INFO(MOD_GRAPHICS, "checkLoadingComplete: zone load #{} published for the game thread",
                 m_activeLoadGeneration);
        return true;
    }
    return false;
}

void Application::loadZoneGraphicsOnThread(EQT::Graphics::LoadingStatus& status) {
    if (!m_renderer || !m_eqClient) {
        LOG_FATAL(MOD_GRAPHICS, "loadZoneGraphicsOnThread: renderer or eqClient is null");
        status.quitRequested.store(true, std::memory_order_release);  // B918: never "ready"
        return;
    }
    // B917: this load's PRIVATE snapshot (taken out of the handoff by
    // startLoadingThread()). Until B917 this read the one shared member the
    // game thread overwrote on a mid-load zone change and cleared when it
    // finalised the previous load -- the ".s3d" / "qeynos.s3d" FATALs.
    const auto& snap = m_activeLoadSnapshot;
    LOG_INFO(MOD_GRAPHICS, "loadZoneGraphicsOnThread: starting (zone='{}', entities={}, doors={}, player='{}')",
             snap.zoneName, snap.entities.size(), snap.doors.size(), snap.playerName);

    if (snap.zoneName.empty()) {
        LOG_FATAL(MOD_GRAPHICS, "loadZoneGraphicsOnThread: snapshot has empty zone name — aborting");
        // B918: this used to return with quitRequested unset, so the game
        // thread finalised a zone that had not been loaded at all.
        status.quitRequested.store(true, std::memory_order_release);
        return;
    }

    // Temporarily clear loading flag so renderer methods work on this thread.
    m_renderer->setLoading(false);

    // P17: unload the PREVIOUS zone's renderer state (region meshes, entities,
    // constrained texture cache, etc.) synchronously, right here, before this
    // (new) zone touches any of it. This replaces relying on EverQuest's
    // queued "/unloadzone_internal" RendererCommand (pushed from
    // CleanupZone(), applied only by the render thread's bridge drain --
    // PreRenderTick -> ProcessBridgeEvents() then, forwardStagedEvents()
    // since T220/T227), which is a real bug: that drain is only reached from the
    // "normal gameplay" branch of Application::render() -- during the entire
    // network-handshake phase and the whole loading-thread-active phase of a
    // zone transition, render() takes early-return branches that never call
    // it. That queued event therefore sits unapplied until AFTER this new
    // zone's own load has already finished and normal gameplay resumes,
    // meaning the OLD zone's textures were still resident in
    // ConstrainedTextureCache the entire time this zone's textures were
    // being uploaded -- the root mechanism behind the P13/P15 cross-zone
    // eviction findings (P15: "ConstrainedTextureCache is never reset
    // across zone unloads"; true in effect, though the reset code itself
    // does exist -- it was just starved out by this ordering). Calling
    // unloadZone() here, synchronously, on the loading thread (which owns
    // the GL context for the entire duration of this function), guarantees
    // the old zone's cache/scene state is gone before a single byte of the
    // new zone uploads. See CleanupZone() (eq_zone.cpp) for the matching
    // removal of the now-redundant (and, if left in, actively harmful --
    // it would fire AFTER this zone loads and wipe ITS textures instead)
    // queued push.
    m_renderer->unloadZone();

    // 1. Collision map
    if (snap.zoneMap && m_bridge) {
        m_bridge->pushEvent(eqt::state::GameEvent(
            eqt::state::GameEventType::CollisionMapChanged,
            eqt::state::CollisionMapChangedData{snap.zoneMap}));
    }

    // 2. Zone lines
    if (!snap.zoneLineBBoxes.empty()) {
        m_renderer->setZoneLineBoundingBoxes(snap.zoneLineBBoxes);
    }

    // 3. Build HCMap placeholder + minimal collision
    eqt::memory::recordPoint("instant_scene_begin", snap.zoneName);  // T504
    m_renderer->setupInstantScene(snap.zoneName, snap.playerX, snap.playerY, snap.playerZ);
    eqt::memory::recordPoint("instant_scene", snap.zoneName);        // T504: placeholder mesh + 2 selectors

    // 4. Zone environment data
    if (!snap.zoneName.empty()) {
        m_renderer->storeZoneEnvironment(snap.skyType, snap.zoneType,
            snap.fogRed, snap.fogGreen, snap.fogBlue,
            snap.fogMinClip, snap.fogMaxClip);
    }

    // 5. Register all entities from snapshot
    if (m_renderer->getEntityRenderer())
        m_renderer->getEntityRenderer()->setPlayerLevel(snap.playerLevel);

    LOG_INFO(MOD_GRAPHICS, "loadZoneGraphicsOnThread: registering {} entities (from snapshot)", snap.entities.size());

    size_t registered = 0, failed = 0;
    for (const auto& entity : snap.entities) {
        bool isPlayer = (entity.name == snap.playerName);
        bool isNPC = (entity.npc_type == 1 || entity.npc_type == 3);
        // T456 (board B478): THE corpse predicate. The inline copy that was
        // here fell back to a bare name.find("corpse") with no trailing-id
        // check, which buries a live npc named "a corpse beetle" on sight.
        const bool isCorpse =
            IsCorpseByFields(entity.is_corpse, entity.npc_type, entity.name);

        EQT::Graphics::EntityAppearance appearance;
        appearance.face = entity.face;
        appearance.haircolor = entity.haircolor;
        appearance.hairstyle = entity.hairstyle;
        appearance.beardcolor = entity.beardcolor;
        appearance.beard = entity.beard;
        appearance.texture = entity.equip_chest2;
        appearance.helm = entity.helm;
        for (int i = 0; i < 9; i++) {
            appearance.equipment[i] = entity.equipment[i];
            appearance.equipment_tint[i] = entity.equipment_tint[i];
        }

        bool ok = m_renderer->registerEntity(entity.spawn_id, entity.race_id, entity.name,
                                   entity.x, entity.y, entity.z, entity.heading,
                                   isPlayer, entity.gender, appearance, isNPC, isCorpse, entity.size,
                                   entity.level);
        if (ok) registered++; else failed++;

        if (isPlayer) {
            m_renderer->setPlayerSpawnId(snap.playerSpawnId);
            if (entity.light > 0)
                m_renderer->setEntityLight(entity.spawn_id, entity.light);
            m_renderer->updatePlayerAppearance(entity.race_id, entity.gender, appearance);
        } else if (entity.light > 0) {
            m_renderer->setEntityLight(entity.spawn_id, entity.light);
        }
    }

    LOG_INFO(MOD_GRAPHICS, "loadZoneGraphicsOnThread: {} registered, {} failed", registered, failed);

    // 6. Register doors from snapshot
    for (const auto& door : snap.doors) {
        m_renderer->registerDoor(door.doorId, door.name, door.x, door.y, door.z,
                                 door.heading, door.incline, door.size, door.opentype,
                                 door.isOpen, door.doorParam);  // T438 (B422)
    }

    // 7. Camera
    float heading512 = snap.playerHeading * 512.0f / 360.0f;
    m_renderer->placeCameraForZoneIn(snap.playerX, snap.playerY, snap.playerZ, heading512);

    m_eqClient->Zone().SetPlayerGraphicsEntityPending(true);

    // 8. Sequential zone asset loading
    m_renderer->loadZoneSequential(snap.eqClientPath, status);

    if (m_renderer->hasLoadFailed()) {
        LOG_FATAL(MOD_GRAPHICS, "loadZoneGraphicsOnThread: sequential loader failed — requesting quit");
        status.quitRequested.store(true, std::memory_order_release);
        return;
    }

    LOG_INFO(MOD_GRAPHICS, "loadZoneGraphicsOnThread: complete for zone '{}'", snap.zoneName);

    // Restore loading flag — will be cleared by joinLoadingThread on main thread
    m_renderer->setLoading(true);
}

// T470 (board B622): Application::loadZoneGraphics(), the synchronous twin of
// loadZoneGraphicsOnThread() above, was deleted. It had no caller in src/,
// include/, tests/, tools/, benchmarks/ or docker/, and every call it made is
// also made by loadZoneGraphicsOnThread(), so nothing else went with it.

// U07b: publishHotbarSnapshot removed — HotbarModel::publishAllSlots() handles this

void Application::setupRDPAudio([[maybe_unused]] void* rdpServerPtr) {
#if defined(WITH_RDP) && defined(WITH_AUDIO)
    if (!m_eqClient) return;
    auto* audioMgr = m_eqClient->GetAudioManager();
    if (!audioMgr) {
        LOG_WARN(MOD_AUDIO, "Cannot setup RDP audio - audio manager not initialized");
        return;
    }
    if (!rdpServerPtr) {
        LOG_WARN(MOD_AUDIO, "Cannot setup RDP audio - RDP server not available");
        return;
    }

    auto* rdpServer = static_cast<EQT::Graphics::RDPServer*>(rdpServerPtr);
    if (audioMgr->enableLoopbackMode()) {
        audioMgr->setAudioOutputCallback(
            [rdpServer](const int16_t* samples, size_t count,
                        uint32_t sampleRate, uint8_t channels) {
                if (rdpServer && rdpServer->isRunning()) {
                    rdpServer->sendAudioSamples(samples, count, sampleRate, channels);
                }
            }
        );
        LOG_INFO(MOD_AUDIO, "RDP audio streaming enabled (loopback mode)");
    } else {
        LOG_WARN(MOD_AUDIO, "Failed to enable loopback mode for RDP audio streaming");
    }
#endif
}
#endif

// Application::syncGameStateFromClient() was deleted (T104) -- its only
// remaining body was a call to syncCombatState() (also deleted, see below);
// once that was gone there was nothing left to sync from this hook. This
// closes the state-layer poll sweep started at T39 (see application.h's
// "Synchronization" section for the full history).

// Application::syncCombatState() was deleted (T104) -- CombatManager remains
// authoritative; its target id publishes TargetChanged from its sole choke
// points (CombatManager::SetTarget/ClearTarget, combat.cpp) and, since
// T221/T227, both the target and the auto-attack flag are written into
// EverQuest::m_game_state.combat() directly at those same choke points --
// the one GameState the action layer reads. (T264: the AutoAttackChanged
// bridge event had no consumer left and was deleted.) See refactor/T104_combat_poll_to_bridge.md and
// refactor/T227_single_gamestate.md.

// Application::syncNPCInteractionState() was deleted (T49) -- vendor,
// banker, and trainer window state (m_vendor_npc_id/m_banker_npc_id/
// m_trainer_npc_id and their names/sell rate) are written into
// EverQuest::m_game_state.player() by eq_merchant.cpp at exactly the same
// mutation sites that publish the six window-open/close bridge events
// (T227 folded the writes to the source; the projection is gone).

void Application::updateLoadingProgress() {
    // Loading progress is updated by EverQuest client callbacks directly
    // This method is a placeholder for future centralized loading state management
}

} // namespace eqt
