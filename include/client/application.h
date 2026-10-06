#pragma once

#include "client/state/game_state.h"
#include "client/mode/game_mode.h"
#include "client/mode/graphical_mode.h"
#include "client/action/action_dispatcher.h"
#include "client/action/input_action_bridge.h"
#include "client/action/command_processor.h"
#include "client/input/input_handler.h"

#include "client/graphics/loading_thread.h"
#include "client/graphics/pose_log_writer.h"  // B874: header-only, no Irrlicht
#include "client/zone_load_snapshot.h"
#include "client/zone_load_handoff.h"
#include "client/fatal_exit.h"
#include "client/scenario/scenario_runner.h"

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
    bool audioUse3D = true;  // K15 (B037): "audio.use_3d"

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

    // T410 (board B138): the scenario lane. --scenario <name> puts the client
    // in Automated mode (no graphics, no stdin) with one ScenarioRunner on
    // AutomatedMode's per-tick automation callback, and makes the PROCESS
    // EXIT CODE the result: 0 pass / 1 fail / 2 timeout / 3 could-not-start.
    // Empty name = the lane is off and nothing below is read.
    std::string scenarioName;
    double scenarioTimeoutSeconds = 300.0;      // --timeout <s>
    double scenarioStartDeadlineSeconds = 120.0;  // zone-in deadline -> exit 3
    double scenarioHoldSeconds = 300.0;         // --scenario-hold <s>
    unsigned int scenarioSpellId = 288;         // --scenario-spell <id> (B484: 288 is castable by the config's class; 46 was not)
    long long scenarioExpectMaterial = -1;      // --scenario-expect-material <n> (T498)
    // B630 (issue #48): the two-client interrupted-cast lane.
    std::string scenarioCastTarget;             // --scenario-cast-target <name>
    unsigned int scenarioSuccessSpellId = 372;  // --scenario-success-spell <id> (D1)
    unsigned int scenarioRecastStopSpellId = 293; // --scenario-recast-stop-spell <id> (D2)
    unsigned int scenarioBuffNotakeSpellId = 293; // --scenario-buff-notake-spell <id> (D3: the caster's own beneficial buff; the "does not take hold" variant is recorded, not forced, in this rig)
    unsigned int scenarioFizzleSpellId = 0;     // --scenario-fizzle-spell <id> (B leg, non-GM caster)

    // B888 (#204): the world-stage character script (world_script.h). At most
    // one of the three; the spec is parsed against data/char_create.tsv in
    // initialize() so a bad one fails before any connection is made.
    std::string createCharacterSpec;  // --create-character "Name,Race,Gender,Class,Deity,City[,STR=n..]"
    std::string deleteCharacterName;  // --delete-character <Name>
    bool listCharacters = false;      // --list-characters

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

    // B874 Deliverable B: --pose-log <path> TSV writer. Every Nth rendered
    // frame (N = poseLogEvery, 1 by default, --pose-every), one 14-column
    // TSV row of exact-float camera/player pose data; poseLogMaxCount caps
    // the rows (--pose-max-frames, 600 by default). Zero cost when off (no
    // file handle open). Row format:
    // include/client/graphics/pose_log_format.h. Nothing here is silent:
    // parseArguments() rejects a non-positive or missing --pose-every /
    // --pose-max-frames value, either knob without --pose-log, and
    // --pose-log in a run with no renderer; main() refuses a path it cannot
    // open (Application::preflightPoseLog) before anything connects.
    std::string poseLogPath;
    int poseLogEvery = 0;  // 0 = disabled
    int poseLogMaxCount = 600;

    // T310 Leg A: --fpcam name=value[,name=value...] (repeatable) -- the
    // startup form of the /fpcam command, so a camera sweep can launch a
    // fresh process per candidate value instead of typing into a running one.
    // Applied by Application::initialize() before any renderer exists; an
    // empty vector is a complete no-op and every parameter keeps its compiled
    // default. See include/client/graphics/first_person_tunables.h.
    std::vector<std::string> fpCamOverrides;

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
// T238: test peer for the --script held/advance path (tests/
// test_headless_command_refusal.cpp). Reaches updateScript() and the two
// members it gates on; nothing else.
struct ApplicationScriptTestPeer;

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

    // T227: Application no longer owns a state::GameState. The one instance
    // is EverQuest::m_game_state (m_eqClient->GetGameState()); every holder
    // below takes a reference to it.

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
     * T410: the --scenario lane's result.
     *
     * scenarioRequested() is false for every ordinary run, and main() then
     * returns 0 exactly as it always did. When it IS true, main() returns
     * scenarioExitCode() -- which is deliberately readable AFTER shutdown():
     * the runner outlives m_gameMode so an external quit (SIGTERM, /quit)
     * still reports a non-zero "no verdict" rather than a false success.
     */
    bool scenarioRequested() const { return m_scenarioRunner != nullptr; }
    int scenarioExitCode() const;
    std::string scenarioReason() const;
    std::string scenarioName() const;

    /**
     * B918 (issue #237): true when the run ended because of a FATAL that
     * takes the ordinary quit path -- the zone loading thread failing, or
     * LoadZoneSources() failing (eqt::RecordFatalExit(), fatal_exit.h).
     * main() turns it into a non-zero exit code via
     * eqt::scenario::ProcessExitCode(); before this both returned 0 outside
     * --scenario, the same as a clean /q.
     */
    bool fatalShutdown() const {
        return m_zoneLoadHandoff.loaderFailed() || eqt::FatalExitRecorded();
    }
    std::string fatalShutdownReason() const {
        return m_zoneLoadHandoff.loaderFailed() ? m_zoneLoadHandoff.loaderFailureReason()
                                                : eqt::FatalExitReason();
    }

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
     * B874 audit: refuse a --pose-log path that cannot be written, BEFORE
     * anything connects. Opens it exactly as the render loop will (truncate,
     * header) and closes it again. Returns true when pose logging is off or
     * the path opened; false with the reason in `err` otherwise. main()
     * prints `err` and exits 1 -- a capture that would silently write nothing
     * must not start.
     */
    static bool preflightPoseLog(const ApplicationConfig& config, std::string& err);

    /**
     * Load configuration from a JSON file.
     *
     * @param configFile Path to the config file
     * @param config Output configuration (will be modified)
     * @return true if loading succeeded, false otherwise
     */
    static bool loadConfigFile(const std::string& configFile, ApplicationConfig& config);

    /**
     * T357 (B057): the slash-command vocabulary for the chat box's Tab key.
     *
     * Application is the only object that holds the registries the chat
     * box's submit path reaches -- registry 1 (eqt::ui::CommandRegistry, via
     * ChatSubmitIntent -> ChatComponent::DispatchSlashCommand), registry 2
     * (action::CommandProcessor, via the unrecognized-command fallback
     * installed in wireUnifiedCommandDispatch()) and registry 3 (the static
     * renderer catalog, the fallback after both) -- so it is the only place
     * that can answer the question the renderer asks. This is deliberately
     * the SAME set BuildHelpResponse() merges for /help: a Tab that offered
     * a different set than /help lists would be a defect.
     *
     * T365 (B091): it WAS such a defect. Registry 3 had no completion source,
     * so Tab offered registries 1+2 while /help listed all three -- the whole
     * ~70-command Graphics category was untabbable. All three are unioned now.
     *
     * PUBLIC since T365, so the union can be asserted end-to-end rather than
     * pinned as source text. It is a const query over maps that are filled
     * during construction/initialize() and never mutated afterwards; it holds
     * no invariant a caller could break.
     *
     * Called on the RENDER thread through the closure
     * initRendererAndGraphics() installs on the renderer. Safe without a lock
     * only because both maps are filled during construction/initialize() and
     * nothing registers a command afterwards.
     */
    std::vector<std::string> commandCompletions(const std::string& partial) const;

private:
    friend struct ApplicationScriptTestPeer;  // T238
    friend struct ApplicationCommandOutputTestPeer;  // T308

    // ========== G04: initialize() subsystem steps ==========
    //
    // initialize() is the ordered list of these; each is a verbatim slice of
    // the pre-G04 body cut at the banner it already carried, and each returns
    // false exactly where the original returned false. THE ORDER IS
    // LOAD-BEARING -- see the banner above the definitions in
    // src/client/application.cpp -- and is pinned against a golden extracted
    // from the pre-G04 source by tests/test_application_init_order.cpp.
    // initGraphics() is a no-op returning true in an EQT_GRAPHICS=OFF build.

    bool initFpCamOverrides(const ApplicationConfig& config);
    bool initConfigValidation(const ApplicationConfig& config);
    bool initFrameDumpDirectory(const ApplicationConfig& config);
    bool initRequiredFileValidation(const ApplicationConfig& config);
    bool initSignalHandlers(const ApplicationConfig& config);
    bool initEverQuestClient(const ApplicationConfig& config);
    // B888 (#204): --create-character / --delete-character / --list-characters
    // -> LoginWorldComponent::SetWorldScript(). Body: application_world_script.cpp.
    bool applyWorldScript(const ApplicationConfig& config);
    bool initSpellSystem(const ApplicationConfig& config);
    bool initActionLayer(const ApplicationConfig& config);
    bool initScriptedCommandTour(const ApplicationConfig& config);
    bool initGameMode(const ApplicationConfig& config);
    bool initInputBridge(const ApplicationConfig& config);
    bool initCommandOutputRouting(const ApplicationConfig& config);
    bool initGraphics(const ApplicationConfig& config);
    bool initConstrainedRendererConfig(const ApplicationConfig& config);
    bool initRendererAndGraphics(const ApplicationConfig& config);
    bool initConsoleBridge(const ApplicationConfig& config);
    // T410: builds the scenario world + runner and registers the runner on
    // AutomatedMode::setAutomationCallback (board B139's unreached seam).
    bool initScenario(const ApplicationConfig& config);
    bool initLoginConnection(const ApplicationConfig& config);

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
    // combat (T104) were all event-fed via GameStateSyncBridge's projection
    // -- until T227, when the projected copy itself was deleted: the action
    // layer now reads EverQuest::m_game_state, which the packet handlers
    // (and the same choke points that publish the events) write directly.

    // syncNPCInteractionState() was deleted (T49); vendor/banker/trainer
    // state is written at the source in eq_merchant.cpp (T227).

    // syncSpellState() was deleted (T44); spell state (casting, gem
    // occupancy/state, gem cooldown) is written at the source by
    // SpellManager (T221).

    // syncCombatState() was deleted (T104) -- CombatManager remains
    // authoritative; its target id/auto-attack flag publish
    // TargetChanged/AutoAttackChanged bridge events at their sole choke
    // points (CombatManager::SetTarget/ClearTarget/EnableAutoAttack/
    // DisableAutoAttack, combat.cpp) and write GameState::combat() there
    // (T221). Target HP-while-still-targeted is covered by the pre-existing
    // ZoneProcessMobHealth() re-publish of TargetChanged when the updated
    // entity is the current target (eq_entities.cpp) -- see
    // refactor/T104_combat_poll_to_bridge.md's Review for the
    // reconstructibility argument.

    /**
     * Update loading progress for the renderer.
     */
    void updateLoadingProgress();

    // ========== D20e2: Zone Loading (moved from EverQuest) ==========

    void startLoadingThread();
    void joinLoadingThread();
    void loadZoneGraphicsOnThread(EQT::Graphics::LoadingStatus& status);
    // T470 (B622): the synchronous loadZoneGraphics() had no caller; deleted.
#ifdef EQT_HAS_GRAPHICS
    bool isLoadingThreadActive() const { return m_loadingThread != nullptr; }
#else
    bool isLoadingThreadActive() const { return false; }
#endif
    bool checkLoadingComplete();
    // T319: setupHotbarCallback() deleted -- an empty body with a stale
    // "TODO: wire through the new UI path" that two zone-load paths called
    // for no effect. The route it was a placeholder for already exists and
    // is live: HotbarModel::setSlot()/clearSlot() call publishSlot(), which
    // pushes HotbarSlotAssigned onto the bridge, and Application calls
    // publishAllSlots() at zone-in. There is nothing left for a callback
    // to carry.
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

    // T410: the scenario lane. Both are null unless --scenario was given.
    // Declared before m_eqClient so they are DESTROYED FIRST -- the world
    // adapter holds references to Application and EverQuest.
    std::unique_ptr<scenario::IScenarioWorld> m_scenarioWorld;
    std::unique_ptr<scenario::ScenarioRunner> m_scenarioRunner;

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
    // T40/T227: decorator wrapping whichever mode bridge above is active.
    // Stages events for the render thread (T220) and delegates to the
    // wrapped bridge unchanged; since T227 it projects nothing. This -- not
    // m_irrlichtBridge/m_consoleBridge directly -- is what m_bridge points
    // at, and what both EverQuest and the renderer are wired to, so
    // events/intents flow through one queue.
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
    // B917: the snapshot the CURRENT loading thread builds from, and the
    // generation it was posted under. Owned by the render thread: written in
    // startLoadingThread() (taken out of m_zoneLoadHandoff) before the loading
    // thread starts, read by that thread only, released in
    // checkLoadingComplete() after the join. The game thread never touches it
    // -- a zone change that lands mid-load posts a NEW snapshot into the
    // handoff instead of overwriting this one (the B917 race).
    eqt::ZoneLoadSnapshot m_activeLoadSnapshot;
    uint64_t m_activeLoadGeneration = 0;
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

    // D21b: Zone load handoff (game thread posts a snapshot, render thread
    // takes it and starts the loading thread, publishes the completion with
    // the BSP tree, game thread finalises it). B917 replaced the shared
    // snapshot + two flags + pending BSP tree that used to live here: see
    // include/client/zone_load_handoff.h. The completion carries the zone the
    // loader actually BUILT (T260's label), taken from the loader's own
    // snapshot rather than from a slot the game thread may have overwritten.
    eqt::ZoneLoadHandoff m_zoneLoadHandoff;

    // Timing
    std::chrono::steady_clock::time_point m_lastGraphicsUpdate;

    // ========== Sync State Tracking ==========
    // Used to detect changes and fire events only when state actually changes

    // Pet state tracking removed (T40): pet state is written at the source
    // (eq_bridge.cpp OnPet*, T221), not polled here.

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
    // T238: true once "CMD outcome=held ..." has been printed for the
    // command step at m_scriptIndex while !m_fullyConnected -- the
    // --script twin of m_preConnectionHeldAnnounced, so a scripted tour
    // with no server says "held" ONCE instead of nothing (T233) and not
    // once per game tick. Reset when the step is dispatched.
    bool m_scriptHeldAnnounced = false;

    // T64: video-capture frame-dump state (render thread only, see render()).
    uint64_t m_dumpFrameCounter = 0;   // increments every processFrame() call
    uint64_t m_dumpFrameIndex = 0;     // increments only when a PNG is written

    // B874: pose-log state (render thread only, see render()). The writer is
    // the whole per-frame body (include/client/graphics/pose_log_writer.h);
    // m_poseLogOpenTried makes an open failure log ONCE instead of once per
    // frame (main() has already refused an unwritable path at startup).
    EQT::Graphics::PoseLogWriter m_poseLog;
    bool m_poseLogOpenTried = false;
};

} // namespace eqt
