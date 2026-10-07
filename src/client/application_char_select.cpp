// B998 (GitLab #318, #204 Legs S/M) -- the character-select screen's
// Application wiring. Its own TU (M00).
//
//   game thread    serviceCharacterSelect(): one CharacterSelectService tick
//                  (login_world/character_select_service.h).
//   render thread  renderCharacterSelect(): the pre-zone frame, through
//                  EQT::Graphics::CharacterSelectHost.
//
// The model, the show/hide decision and the oracle citations:
// include/client/login_world/character_select.h.

#include "client/application.h"
#include "client/eq.h"
#include "client/login_world/character_select.h"
#include "client/player/player_component.h"
#include "common/logging.h"

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/ui/char_select_host.h"
#endif

namespace eqt {

bool Application::characterSelectWanted(const ApplicationConfig& config) {
#ifdef EQT_HAS_GRAPHICS
    const bool graphics = config.graphicsEnabled;
#else
    const bool graphics = false;
#endif
    const bool script = !config.createCharacterSpec.empty() || !config.deleteCharacterName.empty() ||
                        config.listCharacters;
    return login_world::CharacterSelectScreenWanted(config.character, config.characterSelectScreen, graphics,
                                                    script, !config.scenarioName.empty());
}

bool Application::campReturnsToCharacterSelect(const ApplicationConfig& config) {
#ifdef EQT_HAS_GRAPHICS
    const bool graphics = config.graphicsEnabled;
#else
    const bool graphics = false;
#endif
    const bool script = !config.createCharacterSpec.empty() || !config.deleteCharacterName.empty() ||
                        config.listCharacters;
    return login_world::CampReturnsToCharacterSelect(graphics, script);
}

bool Application::applyCharacterSelect(const ApplicationConfig& config) {
    // B1005 (#324): /camp lands on this screen on EVERY run that can draw it
    // (graphics, no world script), configured character or not, so the
    // channel exists from the start -- the render thread reads the pointer
    // without a lock, so it is never created later. Headless keeps /camp's
    // exit 0 (there is no screen); the scenario lane opts its own runs in
    // (scenario_application_wiring.cpp).
    if (campReturnsToCharacterSelect(config)) {
        m_charSelect = std::make_unique<login_world::CharacterSelectChannel>();
        m_eqClient->Player().SetCampReturnsToCharacterSelect(true);
    }
    if (!characterSelectWanted(config)) {
        if (config.characterSelectScreen) {
            LOG_WARN(MOD_MAIN, "--character-select ignored: the screen needs graphics, no --scenario "
                               "and no --create/--delete/--list-characters script");
        }
        return true;
    }
    m_eqClient->LoginWorld().SetCharacterSelectScreen(true);
    LOG_INFO(MOD_MAIN, "[CHARSELECT] the character-select screen will be shown ({})",
             config.character.empty() ? "no character configured" : "--character-select");
    return true;
}

void Application::serviceCharacterSelect() {
    if (!m_charSelect || !m_eqClient) return;
    EverQuest& eq = *m_eqClient;
    m_charSelectService.tick(eq.LoginWorld(), *m_charSelect,
                             [&eq](uint32_t id) { return eq.Strings().Db().getString(id); });
}

bool Application::renderCharacterSelect(float deltaTime) {
#ifdef EQT_HAS_GRAPHICS
    if (!m_charSelect->active()) {
        if (m_charSelectHost) {
            EQT::Graphics::CharacterSelectHost::release(*m_renderer);
            m_charSelectHost.reset();   // nothing of the screen outlives it
        }
        return false;
    }
    if (!m_charSelectHost) m_charSelectHost = std::make_unique<EQT::Graphics::CharacterSelectHost>();
    if (!m_charSelectHost->frame(*m_renderer, *m_charSelect, deltaTime)) {
        LOG_INFO(MOD_MAIN, "[CHARSELECT] quit from character select");
        requestQuit();
    }
    return true;
#else
    (void)deltaTime;
    return false;
#endif
}

}  // namespace eqt
