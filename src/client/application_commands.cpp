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

void Application::processNetworkEvents() {
    if (m_eqClient) {
        m_eqClient->TickNetwork();
    }
}

void Application::processInput(float deltaTime) {
    // Update game mode input only when graphics are NOT handling input.
    // When graphics are active, IrrlichtRenderer handles all input through evdev/X11.
    // The game mode's ConsoleInputHandler must not run in parallel because in DRM mode
    // the same keystrokes reach both evdev and stdin, causing ESC to trigger both
    // ClearTarget (evdev) and Quit (stdin) simultaneously.
    if (m_gameMode && !m_graphicsInitialized) {
        m_gameMode->update(deltaTime);
    }

    // Process input through the action bridge.
    // In graphics mode, GraphicsInputHandler drains the renderer's bridgeQueue_ (evdev/X11
    // key events) — this is safe because it doesn't touch stdin. The game mode update above
    // is still gated to headless-only to prevent ConsoleInputHandler stdin duplicate reads.
    // Skip during loading — no collision detection means movement would drift the player
    // off the map, and the server would interpolate the unbounded position.
    if (m_inputBridge && m_fullyConnected) {
        m_inputBridge->update(deltaTime);
    } else if (!m_fullyConnected) {
        // T30: the gate above holds the ENTIRE console command queue
        // (including /quit) until the client is fully connected -- with
        // no server available, that never happens, and a piped script
        // hangs forever with nothing to show for it (see
        // refactor/T30_headless_shutdown_fix.md). This narrow intercept
        // handles only client-local lifecycle commands (/quit, /help,
        // ...) directly, and makes anything else held observable on
        // stdout -- it must NOT become a second, wider command channel.
        processPreConnectionLifecycleCommands();
    }

    // H03: Advance the scripted command tour, if one was loaded via --script.
    updateScript(deltaTime);
}

void Application::processPreConnectionLifecycleCommands() {
    if (!m_consoleInputHandler) {
        return;
    }

    // Only ever look at (and, if matched, pop) the FRONT of the queue.
    // If it's not a lifecycle command, stop -- leave it and everything
    // behind it exactly where it was, for the normal gated path once
    // m_fullyConnected flips true. This is what keeps this an intercept
    // for a narrow, fixed vocabulary rather than a parallel dispatch
    // path for everything (the plan's STOP condition).
    auto peeked = m_consoleInputHandler->peekRawCommand();
    if (!peeked) {
        return;
    }

    std::string raw = *peeked;
    size_t start = raw.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        // Blank line -- consume and drop it silently, same as the
        // fully-connected path would (DispatchSlashCommand() also
        // treats an all-whitespace line as a no-op).
        m_consoleInputHandler->consumeRawCommand();
        return;
    }
    size_t end = raw.find_last_not_of(" \t\r\n");
    std::string trimmed = raw.substr(start, end - start + 1);
    std::string bare = (trimmed[0] == '/') ? trimmed.substr(1) : trimmed;

    std::string token = bare;
    size_t spacePos = token.find(' ');
    if (spacePos != std::string::npos) {
        token = token.substr(0, spacePos);
    }
    std::string lowerToken = token;
    std::transform(lowerToken.begin(), lowerToken.end(), lowerToken.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // The narrow class: client-local lifecycle commands that have no
    // gameplay effect and no dependency on a live connection. Deliberately
    // does NOT include movement/combat/chat/etc -- opening this gate wider
    // would defeat the loading-drift protection it exists alongside (see
    // the comment on the m_fullyConnected gate above). /quit and its
    // registered aliases /exit and /q (see eq_commands_movement.cpp's
    // "quit" Command) terminate the process; /help and its alias /? are
    // read-only and safe to answer at any time.
    //
    // "q" is here per Will's ruling 2026-08-03 ("/q is a shortcut for
    // /quit and should have the same functionality: quit the application
    // immediately without camping"). Keep this set in sync with that
    // Command's alias list -- if they diverge, /q gets held here while
    // /quit exits, which is exactly the asymmetry the ruling removed.
    static const std::set<std::string> kPreConnectionLifecycleCommands = {
        "quit", "exit", "q", "help", "?"
    };

    if (kPreConnectionLifecycleCommands.count(lowerToken) == 0) {
        // Not a lifecycle command -- hold it (do not consume), but make
        // the wait observable: previously a script piping into a client
        // that never connects got no stdout at all for this, just
        // silence until something killed the process.
        if (!m_preConnectionHeldAnnounced || m_preConnectionHeldLastCmd != trimmed) {
            CommandDispatchResult held;
            held.command = trimmed;
            held.registry = "pending";
            held.recognized = false;
            held.held = true;
            held.message = "waiting for connection";
            printCommandResult(held);
            m_preConnectionHeldAnnounced = true;
            m_preConnectionHeldLastCmd = trimmed;
        }
        return;
    }

    // Recognized lifecycle command -- now actually consume it.
    m_consoleInputHandler->consumeRawCommand();
    m_preConnectionHeldAnnounced = false;
    m_preConnectionHeldLastCmd.clear();

    if (lowerToken == "quit" || lowerToken == "exit") {
        // No live session to protect pre-connection (that's what
        // EverQuest's own "/quit" handler's advisory "use /camp or /q"
        // message is for -- it stays untouched, still reachable once
        // connected). Here there's nothing to safely log out of, so
        // /quit is a direct, immediate shutdown request -- the same
        // controlled path SIGTERM and window-close already use (P36).
        CommandDispatchResult result;
        result.command = trimmed;
        result.registry = "lifecycle";
        result.recognized = true;
        result.success = true;
        result.message = "pre-connection quit";
        printCommandResult(result);
        requestQuit();
    } else {
        // /help, /?: read-only and safe regardless of connection state --
        // route through the normal unified dispatcher so it gets the same
        // registry-1 handler (and stdout ack) it would once connected.
        printCommandResult(DispatchSlashCommand(trimmed));
    }
}

bool Application::loadScriptFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        LOG_ERROR(MOD_MAIN, "Script file not found: {}", path);
        return false;
    }

    std::string line;
    while (std::getline(in, line)) {
        // Trim leading/trailing whitespace.
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) {
            continue;  // blank line
        }
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);

        if (line.empty() || line[0] == '#') {
            continue;  // comment
        }

        std::istringstream iss(line);
        std::string first;
        iss >> first;

        ScriptStep step;
        if (first == "wait") {
            float seconds = 0.0f;
            iss >> seconds;
            step.isWait = true;
            step.waitSeconds = std::max(0.0f, seconds);
        } else {
            step.isWait = false;
            step.commandText = line;
        }
        m_scriptSteps.push_back(step);
    }

    return true;
}

void Application::updateScript(float deltaTime) {
    if (m_scriptIndex >= m_scriptSteps.size()) {
        return;
    }

    ScriptStep& step = m_scriptSteps[m_scriptIndex];

    if (step.isWait) {
        if (!m_scriptWaitStarted) {
            m_scriptWaitRemaining = step.waitSeconds;
            m_scriptWaitStarted = true;
            LOG_INFO(MOD_MAIN, "Script: wait {:.1f}s (step {}/{})",
                step.waitSeconds, m_scriptIndex + 1, m_scriptSteps.size());
        }
        m_scriptWaitRemaining -= deltaTime;
        if (m_scriptWaitRemaining <= 0.0f) {
            m_scriptWaitStarted = false;
            ++m_scriptIndex;
        }
        return;
    }

    // Command step: hold here (without consuming it) until fully zoned in, so a
    // script that starts with a command line rather than "wait" doesn't drop it.
    if (!m_fullyConnected) {
        // T238: say so ONCE per held step, with the exact line T30's
        // piped-stdin intercept prints (processPreConnectionLifecycleCommands),
        // so a no-server tour is distinguishable from a hang
        // (review/T233/script_run_no_server.log logged nothing at all).
        if (!m_scriptHeldAnnounced) {
            CommandDispatchResult held;
            held.command = step.commandText;
            held.registry = "pending";
            held.recognized = false;
            held.held = true;
            held.message = "waiting for connection";
            printCommandResult(held);
            m_scriptHeldAnnounced = true;
        }
        return;
    }
    m_scriptHeldAnnounced = false;

    LOG_INFO(MOD_MAIN, "Script: executing '{}' (step {}/{})",
        step.commandText, m_scriptIndex + 1, m_scriptSteps.size());
    // T29: --script now goes through the same unified dispatch order as
    // piped stdin (registry 1: EverQuest::m_command_registry, THEN registry
    // 2: action::CommandProcessor, THEN registry 3: the renderer command
    // bridge -- see DispatchSlashCommand()'s doc comment). Previously this
    // only tried action::CommandProcessor and fell back straight to the
    // renderer bridge, never consulting EverQuest::m_command_registry at
    // all -- exactly the hole that left /combat (and the rest of registry
    // 1) unreachable from --script.
    printCommandResult(DispatchSlashCommand(step.commandText));
    ++m_scriptIndex;

    if (m_scriptIndex >= m_scriptSteps.size()) {
        LOG_INFO(MOD_MAIN, "Script: complete ({} steps)", m_scriptSteps.size());
    }
}

// T29: registries 2+3 of the unified dispatch order. Shared by
// DispatchSlashCommand() (stage 1 already tried and failed) and the
// fallback wired into EverQuest for the live chat window (registry 1
// already tried and failed there too, via ProcessChatInput's own call to
// EverQuest::DispatchSlashCommand). See refactor/T29_unified_command_channel.md.
bool Application::dispatchToActionOrRenderer(const std::string& cmdBody, CommandDispatchResult& result) {
    // T143: registry-aware /help. /help (and aliases h, ?) is no longer
    // registered in registry 1 (EverQuest::m_command_registry) precisely so
    // it reaches HERE -- Application is the only object holding pointers to
    // both registry 1 (via m_eqClient->GetCommandRegistry()) and registry 2
    // (m_commandProcessor), so it is the only place that can build a merged
    // view. Intercepted before the registry-2 name check below so a /help
    // that also happens to collide with a real registry-2 command name
    // (it doesn't today, but nothing enforces that) can never be shadowed.
    // See include/client/help/merged_help.h and
    // refactor/T143_registry_aware_help.md.
    if (m_eqClient) {
        eqt::help::HelpResponse helpResponse = eqt::help::BuildHelpResponse(
            cmdBody, m_eqClient->GetCommandRegistry(), m_commandProcessor.get());
        if (helpResponse.handled) {
            for (const auto& line : helpResponse.lines) {
                m_eqClient->AddChatSystemMessage(line);
            }
            result.recognized = true;
            result.registry = "help";
            result.success = true;
            result.message = fmt::format("{} lines", helpResponse.lines.size());
            return true;
        }
    }

    // Registry 2: action::CommandProcessor. Only attempt it if it actually
    // recognizes the command name -- calling processCommand() unconditionally
    // would also swallow "Unknown command" as a (failed) CommandProcessor
    // result for names that were really meant for registry 3, which would
    // stop them from ever reaching the renderer bridge.
    std::string name = cmdBody;
    size_t spacePos = name.find(' ');
    if (spacePos != std::string::npos) {
        name = name.substr(0, spacePos);
    }
    if (m_commandProcessor && m_commandProcessor->getCommandInfo(name).has_value()) {
        eqt::action::ActionResult ar = m_commandProcessor->processCommand(cmdBody);
        result.recognized = true;
        result.registry = "action";
        result.success = ar.success;
        result.message = ar.message;
        return true;
    }

    // Registry 3: renderer debug commands, via the RendererCommand bridge
    // queue -- drained on the RENDER thread (IrrlichtBridge's event switch).
    // P10: never call m_renderer->processSlashCommand() (or any renderer
    // method) directly from here -- this runs on the GAME thread
    // (processInput() <- gameThreadLoop()), and calling renderer/GL code
    // off the render thread runs without the EGL context current. P10 hit
    // this and got deterministic-but-garbage screenshot pixels instead of a
    // crash, because Mesa doesn't fail loudly on the misuse. Going through
    // m_bridge instead reuses the existing, already-thread-safe path.
    // This stage is a fire-and-forget push, not a call -- there is no ack
    // path back from the render thread, so it is honestly reported as
    // "queued", never "applied": we cannot know here whether the renderer
    // recognized it either (the renderer-side registry has no query
    // interface reachable from the game thread).
    //
    // T223: registry 3 is now classified by name against its static catalog
    // (include/client/help/renderer_command_catalog.h, drift-tested against
    // the if/else chain) BEFORE anything is pushed. Two consequences:
    //   - a name registry 3 does not know is reported unrecognized
    //     (registry=none) instead of "queued" to a handler that would only
    //     log "Unknown renderer command" on the render thread (the T139
    //     "queued lie");
    //   - with no renderer (a --no-graphics run, or an EQT_GRAPHICS=OFF
    //     build -- m_graphicsInitialized is false in both), a known renderer
    //     command is REFUSED with the message the deleted game-registry
    //     copies used to print (T139/T142), instead of being queued onto the
    //     ConsoleBridge where nothing listens. The refusal goes to the chat
    //     window too, via AddChatSystemMessage(), which also echoes it to
    //     the console log.
    if (!eqt::help::FindRendererCommand(name)) {
        result.registry = "none";
        return false;
    }
    if (!m_graphicsInitialized) {
        result.recognized = true;
        result.registry = "renderer";
        result.success = false;
        result.message = fmt::format(
            "/{} requires graphics mode (not available in this headless build).", name);
        if (m_eqClient) {
            m_eqClient->AddChatSystemMessage(result.message);
        }
        return true;
    }
    if (m_bridge) {
        std::string fullCmd = "/" + cmdBody;
        m_bridge->pushEvent(eqt::state::GameEvent(
            eqt::state::GameEventType::RendererCommand,
            eqt::state::RendererCommandData{fullCmd}));
        result.recognized = true;
        result.registry = "renderer";
        result.success = false;  // "queued", not "applied" -- see comment above
        result.queued = true;
        result.message = "queued to renderer command bridge (async, no ack)";
        return true;
    }

    result.registry = "none";
    return false;
}

void Application::printCommandResult(const CommandDispatchResult& result) const {
    // T29: stable, greppable, single-line stdout ack -- this is the piece
    // that actually delivers automation (see refactor/T29_unified_command_
    // channel.md scope item 3). A script can synchronise on:
    //   - "recognized=true/false": did ANY registry accept it at all.
    //   - "registry=...": which vocabulary handled it (chat/game/action/
    //     renderer/help/none). T143: "help" is the merged registry-1 +
    //     registry-2 /help view (Application::dispatchToActionOrRenderer),
    //     not a registry of its own.
    //   - "outcome=...": applied (registries 1/2, handler ran and reported
    //     success), failed (registries 1/2, handler ran and reported
    //     failure -- see message=; T223: also registry 3 when there is no
    //     renderer to queue to, i.e. a --no-graphics run refusing a
    //     graphics command), queued (registry 3 -- fire-and-forget
    //     onto the render-thread bridge, no ack path, NOT a claim the
    //     effect landed), held (T30 -- the client isn't fully connected
    //     yet, so this command was deliberately NOT dispatched; it is
    //     still queued and will run once connected -- distinct from
    //     unrecognized, which means no registry claimed it at all), or
    //     unrecognized (registry=none).
    // A script canNOT synchronise on "the game state has updated" from this
    // line alone -- most handlers just send a packet and the server answers
    // asynchronously later (e.g. "/combat on" acks locally as applied
    // immediately, but the auto-attack/loot cycle it enables plays out over
    // subsequent server packets the script must watch for separately).
    std::string outcome;
    if (result.held) {
        outcome = "held";
    } else if (!result.recognized) {
        outcome = "unrecognized";
    } else if (result.queued) {
        outcome = "queued";
    } else {
        outcome = result.success ? "applied" : "failed";
    }

    std::string escaped;
    escaped.reserve(result.command.size());
    for (char c : result.command) {
        if (c == '"' || c == '\\') escaped += '\\';
        escaped += c;
    }

    std::cout << "CMD outcome=" << outcome
               << " registry=" << result.registry
               << " recognized=" << (result.recognized ? "true" : "false");
    if (!result.message.empty()) {
        std::string escapedMsg;
        escapedMsg.reserve(result.message.size());
        for (char c : result.message) {
            if (c == '"' || c == '\\') escapedMsg += '\\';
            escapedMsg += c;
        }
        std::cout << " message=\"" << escapedMsg << "\"";
    }
    std::cout << " cmd=\"" << escaped << "\"" << std::endl;
}

void Application::wireUnifiedCommandDispatch() {
    if (!m_eqClient) {
        return;
    }
    // T29: registry 1 (EverQuest::m_command_registry, tried by
    // ProcessChatInput() itself via EverQuest::DispatchSlashCommand())
    // already failed by the time this fallback runs. cmdLine has no
    // leading '/' (EverQuest strips it before calling the fallback).
    m_eqClient->SetUnrecognizedCommandFallback([this](const std::string& cmdLine) {
        CommandDispatchResult result;
        result.command = "/" + cmdLine;
        return dispatchToActionOrRenderer(cmdLine, result);
    });

    // T308: the return path. The fallback above is how a command TYPED IN THE
    // CHAT WINDOW reaches registry 2; before this unit registry 2's answer
    // went only to the ConsoleRenderer GraphicalMode hands us -- a bare
    // std::cout -- so the player typed "/tell" with no arguments and saw
    // nothing at all while "Error: Usage: /tell <name> <message>" scrolled
    // past in a terminal they were not looking at. Registries 1 and 3 already
    // answer into the chat window (AddChatSystemMessage / the render-thread
    // bridge); this gives registry 2 the same reach. setOutputRenderer() is
    // untouched, so every stdout line a --script or piped-stdin run relied on
    // is still printed.
    if (m_commandProcessor) {
        m_commandProcessor->setMessageSink([this](const std::string& text) {
            if (m_eqClient) {
                m_eqClient->AddChatSystemMessage(text);
            }
        });
    }
}

CommandDispatchResult Application::DispatchSlashCommand(const std::string& rawLine) {
    CommandDispatchResult result;
    result.command = rawLine;

    // Trim surrounding whitespace (piped lines commonly carry trailing \r
    // from CRLF-authored script files).
    size_t start = rawLine.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        result.registry = "none";
        return result;
    }
    size_t end = rawLine.find_last_not_of(" \t\r\n");
    std::string trimmed = rawLine.substr(start, end - start + 1);

    // Accept an optional leading '/' (matches CommandProcessor::
    // processCommand()'s existing looseness) -- but unlike EverQuest::
    // ProcessChatInput()'s live chat-window convention, a line with NO
    // leading '/' is NOT auto-sent as chat here. This entry point is the
    // slash command vocabulary specifically (Will's ruling, docs/
    // decisions.md 2026-08-02); plain chat still works via an explicit
    // /say, or ConsoleInputHandler's separate bespoke "say"/"tell" words.
    std::string cmdBody = (trimmed[0] == '/') ? trimmed.substr(1) : trimmed;
    if (cmdBody.empty()) {
        result.registry = "none";
        return result;
    }

    // Stage 1: EverQuest::m_command_registry -- the SAME registry the live
    // chat window uses (EverQuest::ProcessChatInput -> DispatchSlashCommand).
    // This is what --script never consulted before T29, which is why
    // /combat (T26's blocker) couldn't be reached from automation.
    if (m_eqClient && m_eqClient->DispatchSlashCommand(cmdBody)) {
        result.recognized = true;
        result.registry = "game";
        result.success = true;
        return result;
    }

    // Stages 2/3: shared with the live chat window's fallback.
    dispatchToActionOrRenderer(cmdBody, result);
    return result;
}

} // namespace eqt
