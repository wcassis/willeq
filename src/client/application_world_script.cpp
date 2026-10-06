// B888 (GitLab #204): the world-stage character script, from the command line
// to LoginWorldComponent. Its own TU (M00). Grammar and rules:
// include/client/login_world/world_script.h.

#include "client/application.h"
#include "client/eq.h"
#include "client/login_world/char_create_data.h"
#include "client/login_world/world_script.h"
#include "common/logging.h"

namespace eqt {

bool Application::applyWorldScript(const ApplicationConfig& config) {
    const int requested = (config.createCharacterSpec.empty() ? 0 : 1) +
                          (config.deleteCharacterName.empty() ? 0 : 1) +
                          (config.listCharacters ? 1 : 0);
    if (requested == 0) return true;
    if (requested > 1) {
        LOG_FATAL(MOD_MAIN, "--create-character, --delete-character and --list-characters "
                            "are exclusive; pass one");
        return false;
    }
    charcreate::WorldScript script;
    if (config.listCharacters) {
        script.kind = charcreate::WorldScript::Kind::List;
    } else if (!config.deleteCharacterName.empty()) {
        script.kind = charcreate::WorldScript::Kind::Delete;
        script.name = config.deleteCharacterName;
    } else {
        charcreate::CharCreateData data;
        if (!data.loadShipped()) {
            LOG_FATAL(MOD_MAIN, "--create-character: {}", data.error());
            return false;
        }
        std::string err;
        if (!charcreate::ParseCreateSpec(config.createCharacterSpec, data, script.create, err)) {
            LOG_FATAL(MOD_MAIN, "--create-character '{}': {}", config.createCharacterSpec, err);
            return false;
        }
        script.kind = charcreate::WorldScript::Kind::Create;
        script.name = script.create.name;
        LOG_INFO(MOD_MAIN, "--create-character: {} race={} gender={} class={} deity={} city={}",
                 script.create.name, script.create.race, script.create.gender, script.create.cls,
                 script.create.deity, script.create.startZone);
    }
    m_eqClient->LoginWorld().SetWorldScript(script);
    return true;
}

}  // namespace eqt
