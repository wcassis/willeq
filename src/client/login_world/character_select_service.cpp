// B998 (GitLab #318) -- the character-select screen's game-thread tick. Its
// own TU (M00). What it does, step by step:
// include/client/login_world/character_select_service.h.

#include "client/login_world/character_select_service.h"

#include "client/login_world/char_create_data.h"
#include "client/login_world/login_world_component.h"
#include "common/logging.h"

namespace eqt {
namespace login_world {

namespace {

CharacterSelectResult toViewResult(LoginWorldComponent::CharCreateStatus::Result r) {
    using R = LoginWorldComponent::CharCreateStatus::Result;
    switch (r) {
    case R::None: return CharacterSelectResult::None;
    case R::Created: return CharacterSelectResult::Created;
    case R::NameRefused: return CharacterSelectResult::NameRefused;
    case R::CreateRefused: return CharacterSelectResult::CreateRefused;
    case R::Deleted: return CharacterSelectResult::Deleted;
    case R::DeleteFailed: return CharacterSelectResult::DeleteFailed;
    }
    return CharacterSelectResult::None;
}

}  // namespace

void CharacterSelectService::tick(LoginWorldComponent& lw, CharacterSelectChannel& channel,
                                  const StringLookup& eqstr) {
    if (!lw.CharacterSelectScreen()) return;

    for (const CharacterSelectRequest& r : channel.drain()) {
        ++handled_;
        bool ok = false;
        switch (r.kind) {
        case CharacterSelectRequest::Kind::EnterWorld: ok = lw.EnterWorldFromSelect(r.name); break;
        case CharacterSelectRequest::Kind::Delete: ok = lw.RequestDeleteCharacter(r.name); break;
        case CharacterSelectRequest::Kind::Create: ok = lw.RequestCreateCharacter(r.create); break;
        case CharacterSelectRequest::Kind::GetName: ok = lw.RequestRandomName(r.race, r.gender); break;
        }
        LOG_INFO(MOD_WORLD, "[CHARSELECT] request {} '{}': {}", static_cast<int>(r.kind), r.name,
                 ok ? "sent" : "refused");
    }

    // Down for good once OP_EnterWorld is out (from the screen, or straight
    // after a create): the render thread goes back to the loading screen, and
    // nothing of the screen is kept.
    if (lw.HasSentEnterWorld()) {
        if (channel.active()) {
            channel.setActive(false);
            channel.publish(CharacterSelectView{});
            rows_.clear();
            strings_.reset();
            published_ = CharacterSelectView{};
            LOG_INFO(MOD_WORLD, "[CHARSELECT] entering world; screen down");
        }
        return;
    }
    if (lw.CharacterListGeneration() == 0) return;   // no list yet: the loading screen stays
    // B1005: back from /camp, the screen waits for world's FRESH list.
    if (lw.AwaitingFreshCharacterList()) return;

    if (!strings_) {
        charcreate::CharCreateData data;
        auto table = std::make_shared<EqstrTable>();
        if (data.loadShipped()) {
            for (uint32_t id : CharacterSelectEqstrIds(data)) (*table)[id] = eqstr ? eqstr(id) : std::string();
        } else {
            LOG_ERROR(MOD_WORLD, "[CHARSELECT] {}", data.error());
        }
        strings_ = table;
    }
    if (lw.CharacterListGeneration() != listGeneration_) {
        listGeneration_ = lw.CharacterListGeneration();
        rows_ = BuildCharacterSelectRows(lw.GetCharacterSelectList(), LoadEraZoneLongNames());
    }

    const auto& status = lw.GetCharCreateStatus();
    CharacterSelectView view;
    view.rows = rows_;
    view.busy = status.stage != LoginWorldComponent::CharCreateStatus::Stage::Idle;
    view.lastResult = toViewResult(status.last);
    view.lastMessage = status.lastMessage;
    view.randomName = status.randomName;
    view.requestsHandled = handled_;
    view.strings = strings_;
    if (view != published_ || !channel.active()) {
        published_ = view;
        channel.publish(view);
        channel.setActive(true);
    }
}

}  // namespace login_world
}  // namespace eqt
