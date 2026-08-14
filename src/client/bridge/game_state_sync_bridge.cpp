#include "client/bridge/game_state_sync_bridge.h"

#include "client/state/game_state.h"
#include "client/inventory/inventory_constants.h"
#include "common/logging.h"

namespace eqt {
namespace bridge {

void GameStateSyncBridge::applyEvent(const state::GameEvent& event) {
    // T40: narrow switch, pet events only. Every other event delegates
    // untouched -- do not pre-build for later syncs here.
    switch (event.type) {
        case state::GameEventType::PetCreated: {
            const auto& d = std::get<state::PetCreatedData>(event.data);
            m_state.pet().setPet(d.spawnId, d.name, d.level);
            break;
        }
        case state::GameEventType::PetRemoved: {
            m_state.pet().clearPet();
            break;
        }
        case state::GameEventType::PetStatsChanged: {
            const auto& d = std::get<state::PetStatsChangedData>(event.data);
            m_state.pet().updatePetStats(d.hpPercent, d.manaPercent);
            break;
        }
        case state::GameEventType::PetButtonStateChanged: {
            const auto& d = std::get<state::PetButtonStateChangedData>(event.data);
            m_state.pet().setButtonState(d.button, d.state);
            break;
        }
        case state::GameEventType::PlayerSpawnIdSet: {
            // T44: needed so the spell cases below can tell "this is my own
            // cast" from "someone else's cast" -- SpellCastStarted/
            // SpellCastComplete carry a casterId that's any entity, not just
            // us. Published reliably at zone-in (T41's full-state snapshot),
            // before any cast could occur.
            const auto& d = std::get<state::PlayerSpawnIdSetData>(event.data);
            m_state.player().setSpawnId(d.spawnId);
            break;
        }
        case state::GameEventType::SpellGemChanged: {
            // T44: replaces the gem-loop half of Application::syncSpellState().
            // Every gem-state transition (Casting/Refresh/Ready/Empty/
            // MemorizeProgress) already publishes this event from a single
            // choke point (SpellManager::publishGemChanged(), 9 call sites,
            // one per transition -- never per tick), so gem occupancy/state is
            // fully reconstructible. Cooldown is set once, on the transition
            // into Refresh; SpellState computes remaining/progress on read
            // from there (see spell_state.h's T44 comments), so this does not
            // need to fire again while the cooldown ticks down.
            const auto& d = std::get<state::SpellGemChangedData>(event.data);
            auto gemState = static_cast<state::SpellGemState>(d.gemState);
            m_state.spells().setGem(d.gemSlot, d.spellId, gemState);
            if (gemState == state::SpellGemState::Refresh) {
                m_state.spells().setGemCooldown(d.gemSlot, d.cooldownRemainingMs, d.cooldownTotalMs);
            }
            break;
        }
        case state::GameEventType::SpellCastStarted: {
            // T44: replaces the casting-onset half of syncSpellState(). Fired
            // both for our own cast (SpellManager::publishCastStarted(), the
            // instant the cast begins locally -- no network round-trip lag)
            // and, generically, for any caster via eq_player_info.cpp's
            // ZoneProcessBeginCast -- filter to ours.
            const auto& d = std::get<state::SpellCastStartedData>(event.data);
            if (d.casterId == m_state.player().spawnId()) {
                m_state.spells().setCasting(true, d.spellId, d.targetId, d.castTimeMs);
            }
            break;
        }
        case state::GameEventType::SpellCastComplete: {
            // T44: SpellCastComplete was defined but never published before
            // this unit -- the one coverage gap found in casting-state
            // reconstruction (SpellManager::publishCastComplete(), 3 call
            // sites: cast success, player-initiated interrupt, server-driven
            // interrupt/fizzle -- every place m_is_casting flips back to
            // false for our own cast).
            const auto& d = std::get<state::SpellCastCompleteData>(event.data);
            if (d.casterId == m_state.player().spawnId()) {
                m_state.spells().clearCasting();
            }
            break;
        }
        case state::GameEventType::VendorWindowOpened: {
            // T49: replaces the vendor half of
            // Application::syncNPCInteractionState(). Published from
            // eq_merchant.cpp's ZoneProcessShopRequest at the same site that
            // sets m_vendor_npc_id/m_vendor_sell_rate/m_vendor_name -- the
            // poll's only mutation source for vendor state. Carries the
            // *real* sell rate (m_vendor_sell_rate, read from the merchant
            // packet) where the poll hardcoded 1.0f, and npcName is already
            // EQT::toDisplayName()'d with a "Merchant" fallback where the
            // poll used the raw entity name with a "Vendor" fallback -- see
            // refactor/T49_npc_interaction_state.md for the measured deltas.
            const auto& d = std::get<state::WindowOpenedData>(event.data);
            m_state.player().setVendor(d.npcId, d.sellRate, d.npcName);
            break;
        }
        case state::GameEventType::VendorWindowClosed: {
            m_state.player().clearVendor();
            break;
        }
        case state::GameEventType::BankWindowOpened: {
            // T49: replaces the banker half of syncNPCInteractionState().
            // setBanker() takes only npcId (PlayerState has no bank name
            // field), so WindowOpenedData's npcName/sellRate are unused here
            // -- symmetric with the poll, which never read a bank name
            // either.
            const auto& d = std::get<state::WindowOpenedData>(event.data);
            m_state.player().setBanker(d.npcId);
            break;
        }
        case state::GameEventType::BankWindowClosed: {
            m_state.player().clearBanker();
            break;
        }
        case state::GameEventType::TrainerWindowOpened: {
            // T49: replaces the trainer half of syncNPCInteractionState().
            // TrainerWindowOpenedData carries the full trainable-skill list
            // (T20b1's richer payload); PlayerState::setTrainer() only needs
            // npcId/npcName, same fields the poll mirrored.
            const auto& d = std::get<state::TrainerWindowOpenedData>(event.data);
            m_state.player().setTrainer(d.npcId, d.npcName);
            break;
        }
        case state::GameEventType::TrainerWindowClosed: {
            m_state.player().clearTrainer();
            break;
        }
        case state::GameEventType::EntityStateSnapshot: {
            // T81: replaces the never-built entities() poll -- nothing ever
            // populated Application::m_gameState->entities() before this.
            // Add-or-replace, mirroring EverQuest::SyncEntityToGameState()'s
            // own copy semantics exactly (eq_player_info.cpp:261): the
            // sender always holds and sends the complete struct, so there is
            // no accumulation state to track here -- a stale/partial spawn
            // self-heals on the next snapshot.
            const auto& d = std::get<state::EntityStateSnapshotData>(event.data);
            if (m_state.entities().hasEntity(d.entity.spawnId)) {
                auto* existing = m_state.entities().getEntityMutable(d.entity.spawnId);
                if (existing) {
                    *existing = d.entity;
                }
            } else {
                m_state.entities().addEntity(d.entity);
            }
            break;
        }
        case state::GameEventType::EntityDespawned: {
            // T81: already published end-to-end for graphics (eq_bridge.cpp/
            // eq_entities.cpp, same call site as RemoveEntityFromGameState())
            // -- the gap was that nothing fed it into this projection.
            const auto& d = std::get<state::EntityDespawnedData>(event.data);
            m_state.entities().removeEntity(d.spawnId);
            break;
        }
        case state::GameEventType::EntityMoved: {
            // T81: D24 already coalesces this per spawn_id before delivery,
            // so this is the volume-bounded path -- no full snapshot per
            // move. No-op if the entity hasn't been seen yet (a movement
            // packet arriving before/without a spawn is not expected, and
            // if it happens the next full snapshot recovers).
            const auto& d = std::get<state::EntityMovedData>(event.data);
            m_state.entities().updateEntityPosition(
                d.spawnId, d.x, d.y, d.z, d.heading, d.dx, d.dy, d.dz,
                static_cast<uint8_t>(d.animation));
            break;
        }
        case state::GameEventType::EntityAppearanceChanged: {
            // T81: covers the one case SyncEntityToGameState() itself
            // doesn't -- ZoneProcessWearChange() (eq_entities.cpp) mutates
            // m_entities' equipment slot directly without re-invoking
            // SyncEntityToGameState(), so a spawn-time-only snapshot alone
            // would leave post-spawn equipment/appearance changes stale in
            // the projection (matching a pre-existing staleness the source
            // EverQuest::m_game_state itself also has for this path -- see
            // this unit's Review). Partial field update, not a full
            // replace; no-op if the entity hasn't been seen yet.
            const auto& d = std::get<state::EntityAppearanceChangedData>(event.data);
            auto* existing = m_state.entities().getEntityMutable(d.spawnId);
            if (existing) {
                existing->raceId = d.raceId;
                existing->gender = d.gender;
                existing->face = d.face;
                existing->haircolor = d.haircolor;
                existing->hairstyle = d.hairstyle;
                existing->beardcolor = d.beardcolor;
                existing->beard = d.beard;
                existing->equipChest2 = d.texture;
                existing->helm = d.helm;
                for (int i = 0; i < 9; ++i) {
                    existing->equipment[i] = d.equipment[i];
                    existing->equipmentTint[i] = d.equipmentTint[i];
                }
            }
            break;
        }
        case state::GameEventType::ZoneUnloading: {
            // T81: the source-side clear (eq_zone.cpp:270,
            // EverQuest::CleanupZone()) only fires entity-manager events on
            // EverQuest::m_game_state's own private bus (nothing production
            // subscribes to it, T78 census) -- it never reaches this
            // decorator as a per-entity despawn. ZoneUnloading is already
            // published from the same function, before the clear, so it's
            // the one reliable signal this projection has that the zone is
            // about to change; without this the projection would keep every
            // entity from the old zone forever.
            m_state.entities().clear();
            break;
        }
        case state::GameEventType::GroupChanged: {
            // T99: was entirely missing -- Application::m_gameState's
            // GroupState never received a single update from any group
            // event before this unit, discovered live while verifying the
            // T99 pilot's group-invite-accept fix (a real invite arrived on
            // the wire, EverQuest's OWN m_game_state/m_has_pending_invite
            // updated correctly and the local chat line printed, but
            // CommandProcessor's m_state.group().hasPendingInvite() stayed
            // false forever -- the exact "two GameState instances" trap
            // T49 documented, this time on the write side rather than the
            // read side: nothing was writing to the copy ActionDispatcher/
            // CommandProcessor actually read). Same pattern as every other
            // case above (Pet/Spell/Vendor/Bank/Trainer/Entity/Inventory).
            const auto& d = std::get<state::GroupChangedData>(event.data);
            if (d.inGroup) {
                m_state.group().setLeaderName(d.leaderName);
                m_state.group().setInGroup(true);
                m_state.group().setIsLeader(d.isLeader);
            } else {
                // GroupState::setInGroup(false) only runs its side effects
                // (clearGroup(), which also clears the pending-invite flag)
                // on a value TRANSITION -- a no-op if we were already not in
                // a group. That matters here: eq_group.cpp's
                // ZoneProcessGroupCancelInvite publishes GroupChanged with
                // the CURRENT (already-false) inGroup value solely to
                // signal "hide the pending-invite banner"
                // ("GroupChanged to trigger bridge hidePendingInvite"), so
                // relying on setInGroup(false)'s transition-only side
                // effect would silently drop that case (caught by
                // GroupChangedCancelInviteClearsPendingInviteWithoutTouching
                // Group in tests/test_game_state_sync_bridge.cpp -- red
                // without this explicit call, green with it). Clear the
                // pending invite unconditionally instead.
                m_state.group().setInGroup(false);
                m_state.group().clearPendingInvite();
            }
            break;
        }
        case state::GameEventType::GroupMemberUpdated: {
            const auto& d = std::get<state::GroupMemberUpdatedData>(event.data);
            state::GroupMember member;
            member.name = d.name;
            member.spawnId = d.spawnId;
            member.level = d.level;
            member.classId = d.classId;
            member.hpPercent = d.hpPercent;
            member.manaPercent = d.manaPercent;
            member.inZone = d.inZone;
            member.isLeader = (!d.name.empty() && d.name == m_state.group().leaderName());
            m_state.group().setMember(d.memberIndex, member);
            break;
        }
        case state::GameEventType::GroupInviteReceived: {
            const auto& d = std::get<state::GroupInviteReceivedData>(event.data);
            m_state.group().setPendingInvite(d.inviterName);
            break;
        }
        case state::GameEventType::TradeRequestReceived: {
            // T103: was entirely unconsumed -- TradeManager has published
            // this (eq_merchant.cpp's SetupTradeWindowCallbacks) since
            // before this unit; IrrlichtBridge only ever LOG_TRACE'd it.
            // Same shape as T99's GroupChanged gap: the data existed, the
            // projection just never read it.
            const auto& d = std::get<state::TradeRequestReceivedData>(event.data);
            m_state.trade().setPendingRequest(d.spawnId, d.name);
            break;
        }
        case state::GameEventType::TradeStarted: {
            const auto& d = std::get<state::TradeStartedData>(event.data);
            m_state.trade().startTrade(d.partnerId, d.partnerName, d.isNpc);
            break;
        }
        case state::GameEventType::TradeItemUpdated: {
            // T103: TradeItemUpdatedData::who==0 (own side) is never
            // actually published in production -- TradeManager's own-item
            // path (addItemToTrade()/removeItemFromTrade()) has zero
            // callers; own items move via the ordinary inventory MoveItem
            // flow instead (see trade_state.h's class comment). Only the
            // partner side (who==1) is handled; an own-side event, if one
            // ever appeared, would be silently ignored here rather than
            // guessed at.
            const auto& d = std::get<state::TradeItemUpdatedData>(event.data);
            if (d.who == 1) {
                m_state.trade().setPartnerItem(d.slot, d.itemId, d.itemName);
            }
            break;
        }
        case state::GameEventType::TradeMoneyUpdated: {
            const auto& d = std::get<state::TradeMoneyUpdatedData>(event.data);
            state::TradeMoney money;
            money.platinum = d.platinum;
            money.gold = d.gold;
            money.silver = d.silver;
            money.copper = d.copper;
            m_state.trade().setMoney(d.who == 0, money);
            break;
        }
        case state::GameEventType::TradeAcceptStateChanged: {
            const auto& d = std::get<state::TradeAcceptStateChangedData>(event.data);
            m_state.trade().setAcceptState(d.ownAccepted, d.partnerAccepted);
            break;
        }
        case state::GameEventType::TradeCancelled: {
            m_state.trade().reset();
            break;
        }
        case state::GameEventType::TradeCompleted: {
            m_state.trade().reset();
            break;
        }
        case state::GameEventType::TargetChanged: {
            // T104: replaces the target-identity/name/HP/level half of
            // Application::syncCombatState(). Published from
            // CombatManager::SetTarget(uint16_t)/ClearTarget() (combat.cpp,
            // the sole choke points that mutate m_current_target_id -- every
            // caller, direct or via the pre-existing TargetIntent/despawn
            // paths, routes through them) AND, for HP-while-still-targeted,
            // from the pre-existing ZoneProcessMobHealth() re-publish
            // (eq_entities.cpp) when the updated entity is the current
            // target -- that correlation ("is this entity my target?")
            // already lived at the one place that owns both m_current_target_id
            // and the entity map, so this unit reuses it rather than
            // duplicating it here or in CombatState. setTarget() copies
            // spawnId/name/hpPercent/level unconditionally on every call
            // (event_bus/combat_state.cpp), so re-applying the same id with
            // a new hpPercent is exactly the "republish the full struct"
            // pattern T81 established -- no separate HP-update path needed.
            const auto& d = std::get<state::TargetChangedData>(event.data);
            if (d.spawnId != 0) {
                m_state.combat().setTarget(d.spawnId, d.name, d.hpPercent, d.level);
                LOG_DEBUG(MOD_COMBAT, "[T104] GameStateSyncBridge: TargetChanged -> projection "
                          "spawnId={} name='{}' hp={}% level={}",
                          d.spawnId, d.name, d.hpPercent, d.level);
            } else {
                m_state.combat().clearTarget();
                LOG_DEBUG(MOD_COMBAT, "[T104] GameStateSyncBridge: TargetChanged(clear) -> projection cleared");
            }
            break;
        }
        case state::GameEventType::AutoAttackChanged: {
            // T104: replaces the auto-attack-flag half of
            // Application::syncCombatState(). Published from
            // CombatManager::EnableAutoAttack()/DisableAutoAttack()
            // (combat.cpp), the sole choke points that mutate
            // m_auto_attack_enabled.
            const auto& d = std::get<state::AutoAttackChangedData>(event.data);
            m_state.combat().setAutoAttacking(d.enabled);
            LOG_DEBUG(MOD_COMBAT, "[T104] GameStateSyncBridge: AutoAttackChanged -> projection enabled={}",
                      d.enabled);
            break;
        }
        case state::GameEventType::InventorySlotChanged: {
            // T42: replaces Application::syncInventoryState() (a once-per-tick
            // poll of InventoryManager). Piece 1 of this unit closed the three
            // mutation paths (vendor buy, vendor sell, bank-window population)
            // that used to mutate slots without publishing this event, so the
            // event stream is now a complete substitute for the poll.
            const auto& d = std::get<state::InventorySlotChangedData>(event.data);
            if (eqt::inventory::isEquipmentSlot(d.slotId)) {
                m_state.inventory().setEquipmentOccupied(d.slotId, d.hasItem);
            } else if (eqt::inventory::isGeneralSlot(d.slotId)) {
                m_state.inventory().setGeneralOccupied(d.slotId - eqt::inventory::GENERAL_BEGIN, d.hasItem);
            } else if (eqt::inventory::isBankSlot(d.slotId)) {
                m_state.inventory().setBankOccupied(d.slotId - eqt::inventory::BANK_BEGIN, d.hasItem);
            }
            break;
        }
        default:
            break;
    }

    m_wrapped.applyEvent(event);
}

} // namespace bridge
} // namespace eqt
