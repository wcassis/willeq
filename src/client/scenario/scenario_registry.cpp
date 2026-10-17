#include "client/scenario/scenarios.h"

#include <algorithm>

namespace eqt {
namespace scenario {

namespace {

struct Entry {
    const char* name;
    std::unique_ptr<IScenario> (*make)(const ScenarioOptions&);
};

const Entry kScenarios[] = {
    {"zone_in", &MakeZoneInScenario},
    // B925 (issue #244/#258): the single-client zone-link hold.
    {"zone_link_hold", &MakeZoneLinkHoldScenario},
    {"two_client_survive", &MakeTwoClientSurviveScenario},
    {"cast_lands", &MakeCastLandsScenario},
    // B894 (issue #210): the forage/fishing landing pair.
    {"forage_lands", &MakeForageLandsScenario},
    // B892 (issue #208): cast Levitate on ourselves and wait for the
    // client's OWN levitation (the buff + the LevitationChanged state).
    {"levitate_applies", &MakeLevitateAppliesScenario},
    // T494 (board B796): a PAIR. equip_observed is the verdict; equip_actor is
    // the peer scripts/scenario.sh launches for it.
    {"equip_observed", &MakeEquipObservedScenario},
    {"equip_actor", &MakeEquipActorScenario},
    {"equip_actor_return", &MakeEquipActorReturnScenario},
    // B630 (issue #48): the interrupted-cast pair. cast_interrupt_chain is the
    // GM caster (legs A/D1/D2/D3 in one zone-in); cast_interrupt_observe is the
    // peer scripts/scenario.sh launches for it; cast_fizzle is the non-GM
    // buddy's leg B, run as its own scenario on the second account.
    {"cast_interrupt", &MakeCastInterruptScenario},
    {"cast_interrupt_chain", &MakeCastInterruptChainScenario},
    {"cast_success_tail", &MakeCastSuccessTailScenario},
    {"cast_recast_stop", &MakeCastRecastStopScenario},
    {"cast_buff_notake", &MakeCastBuffNotakeScenario},
    {"cast_interrupt_observe", &MakeCastInterruptObserveScenario},
    {"cast_fizzle", &MakeCastFizzleScenario},
    // B760 (issue #45): the repeated cast-tail lane -- one caster, two
    // conditions (its peer is chosen by scripts/scenario.sh from the name).
    {"cast_tail_repeat_quiet", &MakeCastTailRepeatQuietScenario},
    {"cast_tail_repeat_traffic", &MakeCastTailRepeatTrafficScenario},
    {"cast_tail_quiet_peer", &MakeCastTailQuietPeerScenario},
    {"cast_tail_traffic_peer", &MakeCastTailTrafficPeerScenario},
    // UIGAP-live: the wave-1 UI units' live legs (#151 #215 #214 #213 #211).
    // Pairs: click_equip_observed <- click_equip_actor; friends_who /
    // ignore_tell <- social_peer; track_list <- track_peer
    // (scripts/scenario.sh launches the peer).
    {"pickup_roundtrip", &MakePickupRoundtripScenario},
    {"click_equip_observed", &MakeClickEquipObservedScenario},
    {"click_equip_actor", &MakeClickEquipActorScenario},
    {"combine_sewing", &MakeCombineSewingScenario},
    {"train_skill", &MakeTrainSkillScenario},
    {"friends_who", &MakeFriendsWhoScenario},
    {"ignore_tell", &MakeIgnoreTellScenario},
    {"social_peer", &MakeSocialPeerScenario},
    {"track_list", &MakeTrackListScenario},
    {"track_peer", &MakeTrackPeerScenario},
    // B888 (issue #204): needs --create-character.
    {"create_character", &MakeCreateCharacterScenario},
    // UIGAP-live2 (#212 #151 #26). Pair: trade_roundtrip <- trade_peer
    // (scripts/scenario.sh launches the peer).
    {"trade_roundtrip", &MakeTradeRoundtripScenario},
    {"trade_peer", &MakeTradePeerScenario},
    {"give_npc", &MakeGiveNpcScenario},
    {"drop_destroy_coin", &MakeDropDestroyCoinScenario},
    {"bank_change", &MakeBankChangeScenario},
    // B1005 (#324): /camp to character select, and /camp desktop.
    {"camp_to_charselect", &MakeCampToCharselectScenario},
    {"camp_desktop", &MakeCampDesktopScenario},
    // B1072 (#392): the zone-in connect bound, live.
    {"zone_connect_timeout", &MakeZoneConnectTimeoutScenario},
    // B1081 (#401): no character-select flash on a zone change.
    {"zone_change_no_flash", &MakeZoneChangeNoFlashScenario},
    // B893 (#209): fall damage -- the client sends OP_EnvDamage and the
    // server's HP change comes back.
    {"fall_damage", &MakeFallDamageScenario},
    // B900 (#216): the single ranged attack, live. The packet half's lane leg.
    {"ranged_single_shot", &MakeRangedSingleShotScenario},
    // B1088 (#408): the loading screen's "Entering <zone long name>..." line.
    {"loading_name", &MakeLoadingNameScenario},
};

}  // namespace

std::unique_ptr<IScenario> MakeScenario(const std::string& name, const ScenarioOptions& options) {
    for (const auto& entry : kScenarios) {
        if (name == entry.name) {
            return entry.make(options);
        }
    }
    return nullptr;
}

std::vector<std::string> ScenarioNames() {
    std::vector<std::string> names;
    names.reserve(sizeof(kScenarios) / sizeof(kScenarios[0]));
    for (const auto& entry : kScenarios) {
        names.emplace_back(entry.name);
    }
    return names;
}

bool IsScenarioName(const std::string& name) {
    return std::any_of(std::begin(kScenarios), std::end(kScenarios),
                       [&name](const Entry& e) { return name == e.name; });
}

}  // namespace scenario
}  // namespace eqt
