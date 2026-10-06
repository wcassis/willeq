/*
 * The friends/ignore lists on disk -- see include/client/state/social_lists_io.h
 * (B897, GitLab #213).
 */

#include "client/state/social_lists_io.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

#include "client/data_paths.h"
#include "common/logging.h"

namespace eqt {
namespace state {

namespace {

bool ReadAll(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buf;
    buf << in.rdbuf();
    out = buf.str();
    return true;
}

}  // namespace

bool LoadSocialLists(const std::string& character, const std::string& server,
                     SocialListsModel& model) {
    const std::string path =
        eqt::ResolveCachePathForRead(SocialListsCacheName(character, server));
    if (path.empty()) {
        LOG_DEBUG(MOD_UI, "Social lists: no file for '{}' on '{}' -- empty lists",
                  character, server);
        return false;
    }
    std::string text;
    if (!ReadAll(path, text)) return false;
    std::vector<std::string> friends;
    std::vector<std::string> ignored;
    ParseSocialListsIni(text, friends, ignored);
    model.assign(friends, ignored);
    LOG_INFO(MOD_UI, "Social lists loaded from {} ({} friends, {} ignored)", path,
             model.friends().size(), model.ignored().size());
    return true;
}

bool SaveSocialLists(const std::string& character, const std::string& server,
                     const SocialListsModel& model) {
    const std::string name = SocialListsCacheName(character, server);
    const std::string path = eqt::ResolveCachePathForWrite(name);
    if (path.empty()) {
        LOG_DEBUG(MOD_UI, "Social lists: no writable cache directory");
        return false;
    }
    // Keep whatever else the file holds (a player's copied official file).
    std::string existing;
    ReadAll(path, existing);

    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            LOG_WARN(MOD_UI, "Social lists: could not write {}", tmp);
            return false;
        }
        out << MergeSocialListsIni(existing, model);
        if (!out) return false;
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        LOG_WARN(MOD_UI, "Social lists: could not rename {} -> {}", tmp, path);
        std::remove(tmp.c_str());
        return false;
    }
    LOG_DEBUG(MOD_UI, "Social lists saved to {}", path);
    return true;
}

bool SocialListsSync::tick(SocialListsModel& model, const std::string& character,
                           const std::string& server) {
    if (character.empty()) return false;
    const std::string identity = character + '\n' + server;
    if (!bound_ || identity != identity_) {
        bound_ = true;
        identity_ = identity;
        model.clear();
        LoadSocialLists(character, server, model);
        syncedRevision_ = model.revision();
        return true;
    }
    if (model.revision() == syncedRevision_) return false;
    SaveSocialLists(character, server, model);
    syncedRevision_ = model.revision();
    return true;
}

}  // namespace state
}  // namespace eqt
