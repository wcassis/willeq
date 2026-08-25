#pragma once

#include "client/state/game_state.h"
#include "client/mode/game_mode.h"
#include "client/mode/graphical_mode.h"
#include "client/action/action_dispatcher.h"
#include "client/action/input_action_bridge.h"
#include "client/action/command_processor.h"
#include "client/input/input_handler.h"

#include "client/graphics/loading_thread.h"
#include "client/zone_load_snapshot.h"

#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>

// Forward declarations
class EverQuest;
namespace EQT { namespace Graphics { class IrrlichtRenderer; class ConstrainedTextureCache; struct BspTree; } }
namespace eqt { namespace bridge { class IrrlichtBridge; class ConsoleBridge; class GameStateBridge; class GameStateSyncBridge; } }

namespace eqt {

// Forward declarations
class EqActionHandler;
namespace input { class GraphicsInputHandler; }

/**
 * ApplicationConfig - Configuration for the Application.
 *
 * Contains all settings needed to initialize and run the application,
 * including connection info, paths, display settings, and mode selection.
 */
struct ApplicationConfig {
    // Connection settings
    std::string host;
    int port = 5998;
    std::string user;
    std::string pass;
    std::string server;
    std::string character;

    // Path settings
    std::string configFile = "willeq.json";
    std::string navmeshPath;
    std::string mapsPath;
    std::string eqClientPath;

    // Display settings
    int displayWidth = 800;
    int displayHeight = 600;
    bool fullscreen = false;

    // Mode settings
    mode::OperatingMode operatingMode = mode::OperatingMode::GraphicalInteractive;

    // Renderer backend (--renderer CLI arg). GLES2 is the only supported
    // renderer (docs/decisions.md 2026-07-28 addendum, P32 Phase 4) —
    // "software"/"opengl" were removed and now fail CLI parsing fast
    // (see parseArguments' fatalArgError handling). This flag no longer
    // changes anything at runtime (GLES2 is used regardless); it is kept
    // so `--renderer gles2` keeps working as a documented explicit no-op.
    bool rendererGles2Requested = false;

    // Feature flags
    bool pathfindingEnabled = true;
    bool graphicsEnabled = true;

    // Logging
    int debugLevel = 0;

    // Audio settings (guarded by WITH_AUDIO at usage sites)
    bool audioEnabled = true;
    float audioMasterVolume = 1.0f;
    float audioMusicVolume = 0.5f;
    float audioEffectsVolume = 1.0f;
    std::string audioSoundfont;
    std::string audioVendorMusic = "gl.xmi";

    // RDP settings (guarded by WITH_RDP at usage sites)
    bool rdpEnabled = false;
    uint16_t rdpPort = 3389;

    // DRM/KMS settings (guarded by EQT_HAS_DRM at usage sites)
    bool useDRM = false;

    // P25: X11-windowed EGL surface for the x86 GLES2 device (--egl-x11).
    // Explicit opt-in only — never derived from $DISPLAY. No effect outside
    // the x86 EQT_HAS_GLES2 build.
    bool useEglX11 = false;

    // Constrained rendering
    std::string constrainedPreset;
    std::string eqe1Path;   // Directory containing .eqe1 ETC1 flat pack files
    std::string compositeEntityPath;  // Directory containing composite entity texture sheets
    std::string regionMapsPath = "data/region_maps";  // Directory containing per-zone region classification JSON
    int backgroundThreadCount = 0;  // 0 = use preset default
    int zoneLoadMode = -1;  // -1 = use preset default, 0 = manual (all at load), 1 = automatic (progressive)
    bool loadMemAudit = false;  // P04: --load-mem-audit — per-step RSS logging during zone load
    std::string textureUploadLedgerPath;  // P13: --texture-ledger <path> — texture upload/eviction ledger dump

    // Profiling
    bool frameTimingEnabled = false;
    bool sceneProfileEnabled = false;

    // R01: Session recording (--record-session <file>)
    std::string recordSessionPath;

    // H03: Scripted command tour (--script <file>)
    std::string scriptPath;

    // T64: video-capture evidence tool (--dump-frames-dir/--dump-every/
    // --dump-max-frames). Off by default (empty dir = disabled) -- does not
    // touch production behavior. Writes numbered PNGs via the existing
    // saveScreenshot() machinery every Nth rendered frame, on the render
    // thread, from Application::render(). Intended use: recording a temporal
    // sequence (an attack swing, a cast, a zone transition, particle
    // behavior) that stills can't capture, then encoding it with
    // scripts/ci/encode_capture.sh. Costs real time/disk per frame written --
    // see dumpFramesMaxCount below.
    std::string dumpFramesDir;
    int dumpFramesEvery = 0;  // 0 = disabled
    // T64: hard cap on frames written, regardless of session length. Frame
    // dumping is unbounded time-wise otherwise, which is a footgun on
    // constrained targets -- the Orange Pi's /tmp tmpfs is ~242MB, and T61
    // already had to deploy stripped binaries to make room there. At 800x600
    // PNG, frames run a few hundred KB each, so this default keeps a capture
    // session comfortably under that budget even on the Pi. Dumping stops
    // (with one clear log line) once the cap is hit; the flags remain a
    // complete no-op when dumpFramesDir is empty or dumpFramesEvery is 0.
    int dumpFramesMaxCount = 600;

    // Help flag
    bool showHelp = false;

    // P32: set when parseArguments() rejects a CLI value outright (fail
    // fast) — e.g. --renderer software/opengl. The offending message is
    // already printed to stderr; main() checks this and exits non-zero
    // without proceeding to config load / app init.
    bool parseError = false;
};

/**
 * H03: One step of a --script tour file: either a timed pause ("wait N")
 * or a command line to feed through CommandProcessor::processCommand()
 * verbatim (e.g. "/moveto loc" or "/camp").
 */
struct ScriptStep {
    bool isWait = false;
    float waitSeconds = 0.0f;
    std::string commandText;
};

/**
 * T29 — result of one Application::DispatchSlashCommand() call.
 *
 * This is the automation-facing contract: "recognized"/"registry"/
 * "success" answer whether a command was APPLIED (a registry found it and
 * ran its local handler), not merely accepted as text. See
 * refactor/T29_unified_command_channel.md for what a script can and
 * cannot legitimately synchronise on -- in short: "applied" means the
 * local handler ran (and, for registries 1/2, returned success), NOT that
 * the server has confirmed the effect; most handlers just send a packet
 * and the server answers asynchronously later. Registry 3 (the renderer
 * command bridge) is honestly reported as "queued", never "applied" --
 * it's a fire-and-forget push onto a queue drained on the render thread,
 * with no ack path back at all.
 */
struct CommandDispatchResult {
    std::string command;      // The raw input line, as given.
    bool recognized = false;  // Did ANY registry (or the chat fallback) accept it?
    std::string registry;     // "chat" | "game" | "action" | "renderer" | "help" | "none" | "pending"
    bool success = false;     // Registries 1/2 only: the handler's own reported outcome.
    std::string message;      // Optional detail (failure reason, echoed usage, ...).

    // T30: true if this command was NOT dispatched because the client
    // isn't fully connected yet (see Application::processInput()'s
    // pre-connection lifecycle intercept). Distinct from "unrecognized"
    // (no registry claimed it) -- "held" means dispatch was deliberately
    // deferred, and the command is still queued to run once connected.
    // printCommandResult() reports this as outcome=held so a script
    // piping commands into a client that never connects gets an
    // observable "waiting" signal on stdout instead of total silence
    // (refactor/T30_headless_shutdown_fix.md).
    bool held = false;

    // T223: true iff this command was pushed onto the renderer command
    // bridge (registry 3, fire-and-forget). printCommandResult() reports
    // outcome=queued only for this case; a registry-3 name refused because
    // there is no renderer (--no-graphics / EQT_GRAPHICS=OFF) is
    // registry=renderer with queued=false and prints outcome=failed.
    bool queued = false;
};

/**
 * Application - Main application class that orchestrates all components.
 *
 * This class brings together:
 * - GameState: Single source of truth for all game data
 * - IGameMode: Input/output handling for the selected operating mode
 * - ActionDispatcher: Unified action handling
 * - InputActionBridge: Translates input to actions
 * - EverQuest: Network client and game logic (via EqActionHandler adapter)
 *
 * Usage:
 *   ApplicationConfig config = parseArguments(argc, argv);
 *   Application app;
 *   if (app.initialize(config)) {
 *       app.run();
 *   }
 *   app.shutdown();
 */
class Application {
public:
    Application();
    ~Application();

    // Non-copyable, non-movable
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    /**
     * Initialize the application with the given configuration.
     *
     * @param config Application configuration
     * @return true if initialization succeeded, false otherwise
     */
    bool initialize(const ApplicationConfig& config);

    /**
     * Run the main application loop.
     *
     * This method blocks until the application is ready to exit.
     * Call requestQuit() from another thread or from within the
     * application to signal that the loop should terminate.
     */
    void run();

    /**
     * Shutdown the application and release all resources.
     */
    void shutdown();

    /**
     * Request the application to quit.
     *
     * This can be called from any thread to signal that the
     * main loop should terminate.
     */
    void requestQuit();

    /**
     * Check if the application is still running.
     */
    bool isRunning() const { return m_running.load(); }

    // ========== Component Access ==========

    /**
     * Get the game state.
     */
    state::GameState* getGameState() { return m_gameState.get(); }
    const state::GameState* getGameState() const { return m_gameState.get(); }

    /**
     * Get the current game mode.
     */
    mode::IGameMode* getGameMode() { return m_gameMode.get(); }
    const mode::IGameMode* getGameMode() const { return m_gameMode.get(); }

    /**
     * Get the action dispatcher.
     */
    action::ActionDispatcher* getActionDispatcher() { return m_dispatcher.get(); }
    const action::ActionDispatcher* getActionDispatcher() const { return m_dispatcher.get(); }

    /**
     * Get the command processor.
     */
    action::CommandProcessor* getCommandProcessor() { return m_commandProcessor.get(); }
    const action::CommandProcessor* getCommandProcessor() const { return m_commandProcessor.get(); }

    /**
     * Get the EverQuest client (for direct access when needed).
     */
    EverQuest* getEverQuestClient() { return m_eqClient.get(); }
    const EverQuest* getEverQuestClient() const { return m_eqClient.get(); }

    /**
     * T29 — the single unified slash-command dispatch entry point.
     *
     * Both automation front doors (piped/scripted stdin via
     * ConsoleInputHandler's raw-command queue, and --script) call this.
     * The live chat window reaches an equivalent cascade through
     * EverQuest::ProcessChatInput() + the fallback registered in
     * initialize() (see wireUnifiedCommandDispatch()) -- see
     * refactor/T29_unified_command_channel.md for the full rationale and
     * dispatch order (registry 1: EverQuest::m_command_registry: registry
     * 2: action::CommandProcessor; registry 3: renderer command bridge).
     *
     * Accepts a line with or without a leading '/' (tolerant, matching
     * CommandProcessor::processCommand()'s existing looseness). A line
     * with no leading '/' that no stage recognizes as a command is NOT
     * auto-sent as chat here -- unlike EverQuest::ProcessChatInput()'s
     * live chat-window convention, this entry point is for the slash
     * command vocabulary specifically, per Will's ruling (see
     * docs/decisions.md 2026-08-02). Plain chat still works via /say or
     * ConsoleInputHandler's separate bespoke "say"/"tell"/... words.
     *
     * Prints one stable, greppable result line to stdout (see
     * printCommandResult()) so a script can tell a command was applied,
     * not merely accepted.
     */
    CommandDispatchResult DispatchSlashCommand(const std::string& rawLine);

    // ========== Static Helpers ==========

    /**
     * Parse command line arguments into an ApplicationConfig.
     *
     * @param argc Argument count
     * @param argv Argument values
     * @return Parsed configuration
     */
    static ApplicationConfig parseArguments(int argc, char* argv[]);

    /**
     * Load configuration from a JSON file.
     *
     * @param configFile Path to the config file
     * @param config Output configuration (will be modified)
     * @return true if loading succeeded, false otherwise
     */
    static bool loadConfigFile(const std::string& configFile, ApplicationConfig& config);

private:
    // ========== Main Loop Stages ==========

    void mainLoop();          // Render thread (main) — render loop
    void gameThreadLoop();    // D21b: Game thread — network + game state
    void processNetworkEvents();
    void processInput(float deltaTime);
    void updateGameState(float deltaTime);
    void render(float deltaTime);

    // ========== Synchronization ==========

    // syncGameStateFromClient() was deleted (T104) -- it had shrunk to a
    // single call to syncCombatState() (see below), and once that was
    // deleted too there was nothing left to sync from this hook. This
    // closes the state-layer poll sweep started at T39: pet (T40),
    // inventory (T42), spells (T44), NPC interaction (T49), and now
    // combat (T104) are all event-fed via GameStateSyncBridge.

    // syncNPCInteractionState() was deleted (T49) -- vendor/banker/trainer
    // state is now fed from GameStateSyncBridge via bridge events
    // (VendorWindowOpened/Closed, BankWindowOpened/Closed,
    // TrainerWindowOpened/Closed), constructed in initialize() alongside the
    // T40/T42/T44 pet/inventory/spell cases.

    // syncSpellState() was deleted (T44) -- spell state (casting, gem
    // occupancy/state, gem cooldown) is now fed from GameStateSyncBridge via
    // bridge events (SpellCastStarted, SpellCastComplete, SpellGemChanged),
    // constructed in initialize() alongside the T40/T42 pet/inventory cases.

    // syncCombatState() was deleted (T104) -- CombatManager remains
    // authoritative; its target id/auto-attack flag now publish
    // TargetChanged/AutoAttackChanged bridge events at their sole choke
    // points (CombatManager::SetTarget/ClearTarget/EnableAutoAttack/
    // DisableAutoAttack, combat.cpp), fed into GameState::combat() by
    // GameStateSyncBridge -- the same pattern as every case above. Target
    // HP-while-still-targeted is covered by the pre-existing
    // ZoneProcessMobHealth() re-publish of TargetChanged when the updated
    // entity is the current target (eq_entities.cpp) -- see
    // refactor/T104_combat_poll_to_bridge.md's Review for the
    // reconstructibility argument.

    /**
     * Update loading progress for the renderer.
     */
    void updateLoadingProgress();

    /**
     * Connect renderer callbacks to action dispatcher.
     */
    void connectRendererCallbacks();

    // ========== D20e2: Zone Loading (moved from EverQuest) ==========

    void startLoadingThread();
    void joinLoadingThread();
    void loadZoneGraphicsOnThread(EQT::Graphics::LoadingStatus& status);
    void loadZoneGraphics();  // Synchronous path
#ifdef EQT_HAS_GRAPHICS
    bool isLoadingThreadActive() const { return m_loadingThread != nullptr; }
#else
    bool isLoadingThreadActive() const { return false; }
#endif
    bool checkLoadingComplete();
    void setupHotbarCallback();  // Wire hotbar changed callback to renderer
    void setupRDPAudio(void* rdpServer);  // D20g: Wire RDP audio streaming
    // U07b: publishHotbarSnapshot removed — HotbarModel::publishAllSlots() handles this

    // ========== H03: Scripted command tour (--script <file>) ==========

    // Parses a script file ("wait N" lines + slash-command lines, '#' comments,
    // blank lines ignored) into m_scriptSteps. Returns false (logs an error) if
    // the file cannot be opened. Called once from initialize().
    bool loadScriptFile(const std::string& path);

    // Advances the loaded script by one game tick. Wait steps count down using
    // deltaTime; command steps are held (not consumed) until m_fullyConnected is
    // true, then dispatched through m_commandProcessor->processCommand() exactly
    // as a live slash command would be. No-op if no script was loaded. Called
    // from processInput() on the game thread.
    void updateScript(float deltaTime);

    // T29: registries 2+3 of the unified dispatch (action::CommandProcessor,
    // then the renderer command bridge), factored out so both
    // DispatchSlashCommand() (stdin/--script) and the fallback wired into
    // EverQuest for the live chat window (wireUnifiedCommandDispatch())
    // share one implementation rather than two copies of the same order.
    // Only fills in `result`; does not print anything (DispatchSlashCommand
    // owns printing). Returns result.recognized.
    bool dispatchToActionOrRenderer(const std::string& cmdBody, CommandDispatchResult& result);

    // Prints one stable, greppable "CMD ..." line to stdout for a
    // DispatchSlashCommand() result. See CommandDispatchResult's doc
    // comment for exactly what "applied" does and doesn't promise.
    void printCommandResult(const CommandDispatchResult& result) const;

    // Wires EverQuest::SetUnrecognizedCommandFallback() to
    // dispatchToActionOrRenderer(), so the live chat window (which calls
    // EverQuest::ProcessChatInput() directly, not DispatchSlashCommand())
    // still reaches registries 2 and 3 when registry 1 doesn't recognize a
    // command -- same order, same shared implementation, just without the
    // stdout ack (the chat window has its own UI feedback). Called once
    // from initialize().
    void wireUnifiedCommandDispatch();

    // T30: pre-connection lifecycle intercept. m_inputBridge->update() in
    // processInput() is gated on m_fullyConnected (loading-drift
    // protection -- see the comment at that call site), which means the
    // WHOLE console command queue -- including /quit -- sits undrained
    // until a zone connection completes. With no server available at
    // all, that connection never completes and a piped script hangs
    // forever (refactor/T30_headless_shutdown_fix.md). This drains only
    // the narrow class of client-local lifecycle commands (/quit, /exit,
    // /help, /?) directly off the console raw-command queue, leaving
    // every other command (movement, chat, combat, ...) untouched in the
    // queue for the normal gated path once connected -- this must NOT
    // become a second, wider command channel (that would defeat the
    // gate's purpose; see the STOP condition in the plan). Called from
    // processInput() only while !m_fullyConnected.
    void processPreConnectionLifecycleCommands();

    // ========== Components ==========

    std::unique_ptr<state::GameState> m_gameState;
    std::unique_ptr<mode::IGameMode> m_gameMode;
    std::unique_ptr<action::ActionDispatcher> m_dispatcher;
    std::unique_ptr<action::InputActionBridge> m_inputBridge;
    std::unique_ptr<action::CommandProcessor> m_commandProcessor;

    // T30: non-owning pointer to the same console input handler wired
    // into m_inputBridge (setInputHandler()/setConsoleCommandSource()) --
    // stashed here too so processPreConnectionLifecycleCommands() can
    // peek its raw-command queue directly, without routing through the
    // fully-gated bridge. Null in graphics-only configurations that
    // never construct a console handler (GraphicsInputHandler has no raw
    // commands, see its consumeRawCommand()/peekRawCommand() no-ops).
    input::IInputHandler* m_consoleInputHandler = nullptr;

    // T30: true once "CMD outcome=held ..." has been printed for the
    // command currently at the front of the console queue -- avoids
    // reprinting it every ~60Hz game tick while waiting to connect.
    // Reset whenever the front of the queue changes or the client
    // becomes fully connected.
    bool m_preConnectionHeldAnnounced = false;
    std::string m_preConnectionHeldLastCmd;

    // EverQuest client and adapter
    std::unique_ptr<EverQuest> m_eqClient;
    std::unique_ptr<EqActionHandler> m_actionHandler;

    // P28: these all require complete Irrlicht-dependent types at
    // ~Application() (unique_ptr's deleter instantiates their destructor),
    // which isn't available under EQT_GRAPHICS=OFF — guarded. ConsoleBridge/
    // GameStateBridge (the headless bridge pair) stay unconditional below.
#ifdef EQT_HAS_GRAPHICS
    // Constrained asset cache — created at startup, shared across all subsystems
    std::unique_ptr<EQT::Graphics::ConstrainedTextureCache> m_constrainedCache;

    // D20b5: Renderer and bridge owned by Application
    std::unique_ptr<EQT::Graphics::IrrlichtRenderer> m_renderer;
    std::unique_ptr<eqt::bridge::IrrlichtBridge> m_irrlichtBridge;
#endif
    std::unique_ptr<eqt::bridge::ConsoleBridge> m_consoleBridge;  // D25: headless mode
    // T40: decorator wrapping whichever mode bridge above is active. Feeds
    // pet state into m_gameState from bridge events, then delegates to the
    // wrapped bridge unchanged. This -- not m_irrlichtBridge/m_consoleBridge
    // directly -- is what m_bridge points at, and what both EverQuest and
    // the renderer are wired to, so events/intents flow through one queue.
    std::unique_ptr<eqt::bridge::GameStateSyncBridge> m_stateSyncBridge;
    eqt::bridge::GameStateBridge* m_bridge = nullptr;

#ifdef EQT_HAS_GRAPHICS
    // Graphics input handler (bridges RendererEventReceiver → InputActionBridge)
    std::unique_ptr<input::GraphicsInputHandler> m_graphicsInputHandler;

    // D20e2: Loading thread (moved from EverQuest)
    std::unique_ptr<EQT::Graphics::LoadingThread> m_loadingThread;
#endif
    EQT::Graphics::LoadingStatus m_loadingStatus;
    EQT::Graphics::GLContextHandles m_glHandles;
    eqt::ZoneLoadSnapshot m_zoneLoadSnapshot;
#ifdef EQT_HAS_GRAPHICS
    irr::gui::IGUIFont* m_loadingScreenFont = nullptr;  // Cached for render thread loading screen
#endif

    // ========== State ==========

    std::atomic<bool> m_running{false};
    bool m_fullyConnected = false;           // Game thread only
    bool m_graphicsInitialized = false;      // Set once at init, read by both (safe)
    ApplicationConfig m_config;

    // D21b: Game thread
    std::unique_ptr<std::thread> m_gameThread;

    // D21b: Zone load handoff (game thread creates snapshot, render thread starts loading)
    std::mutex m_zoneLoadMutex;
    std::atomic<bool> m_zoneLoadReady{false};          // Set by game thread, consumed by render thread
    std::atomic<bool> m_graphicsCompleteReady{false};  // Set by render thread, consumed by game thread
    std::shared_ptr<EQT::Graphics::BspTree> m_pendingBspTree;  // Protected by m_zoneLoadMutex

    // Timing
    std::chrono::steady_clock::time_point m_lastUpdate;
    std::chrono::steady_clock::time_point m_lastGraphicsUpdate;

    // ========== Sync State Tracking ==========
    // Used to detect changes and fire events only when state actually changes

    // Pet state tracking removed (T40): pet state is now fed by
    // GameStateSyncBridge from bridge events, not polled here.

    // NPC interaction tracking members (m_lastVendorNpcId/m_lastBankerNpcId/
    // m_lastTrainerNpcId) were deleted (T49) along with
    // syncNPCInteractionState() -- see above.

    // Spell state tracking members (m_lastIsCasting/m_lastCastingSpellId)
    // were deleted (T44) along with syncSpellState() -- see above.

    // Combat state tracking members (m_lastCombatTargetId/
    // m_lastCombatTargetHp/m_lastAutoAttacking) were deleted (T104) along
    // with syncCombatState() -- see "Synchronization" above.

    // H03: Scripted command tour state (game thread only)
    std::vector<ScriptStep> m_scriptSteps;
    size_t m_scriptIndex = 0;
    float m_scriptWaitRemaining = 0.0f;
    bool m_scriptWaitStarted = false;

    // T64: video-capture frame-dump state (render thread only, see render()).
    uint64_t m_dumpFrameCounter = 0;   // increments every processFrame() call
    uint64_t m_dumpFrameIndex = 0;     // increments only when a PNG is written
};

} // namespace eqt
