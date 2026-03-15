#include "client/bridge/irrlicht_bridge.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/spell_visual_fx.h"
#include "client/graphics/ui/chat_message_buffer.h"
#include "client/graphics/ui/inventory_constants.h"
#include "client/graphics/ui/inventory_manager.h"
#include "common/logging.h"
#include <ctime>

class HCMap;

namespace eqt {
namespace bridge {

void IrrlichtBridge::applyEvent(const state::GameEvent& event) {
    switch (event.type) {
    // ========================================================================
    // Player events (D09)
    // ========================================================================
    case state::GameEventType::PlayerMoved:
        if (renderer_) {
            auto& d = std::get<state::PlayerMovedData>(event.data);
            renderer_->setPlayerPosition(d.x, d.y, d.z, d.heading);
        }
        break;
    case state::GameEventType::PlayerStatsChanged:
        if (renderer_) {
            auto& d = std::get<state::PlayerStatsChangedData>(event.data);
            // U03d: Cache for new static UI
            auto& cached = renderer_->cachedPlayerStats_;
            cached.curHP = d.curHP; cached.maxHP = d.maxHP;
            cached.curMana = d.curMana; cached.maxMana = d.maxMana;
            cached.curEndurance = d.curEndurance; cached.maxEndurance = d.maxEndurance;
            cached.level = d.level;
        }
        break;
    case state::GameEventType::PlayerPositionStateChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: PlayerPositionStateChanged");
        break;
    case state::GameEventType::PlayerMovementModeChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: PlayerMovementModeChanged");
        break;

    // ========================================================================
    // Entity events (D09)
    // ========================================================================
    case state::GameEventType::EntitySpawned:
        if (renderer_) {
            auto& d = std::get<state::EntitySpawnedData>(event.data);
            EQT::Graphics::EntityAppearance appearance;
            appearance.face = d.face;
            appearance.haircolor = d.haircolor;
            appearance.hairstyle = d.hairstyle;
            appearance.beardcolor = d.beardcolor;
            appearance.beard = d.beard;
            appearance.texture = d.texture;
            appearance.helm = d.helm;
            for (int i = 0; i < 9; i++) {
                appearance.equipment[i] = d.equipment[i];
                appearance.equipment_tint[i] = d.equipmentTint[i];
            }
            bool isNPC = (d.npcType == 1 || d.npcType == 3);
            renderer_->registerEntity(d.spawnId, d.raceId, d.name,
                d.x, d.y, d.z, d.heading, d.isPlayer,
                d.gender, appearance, isNPC, d.isCorpse, d.serverSize,
                d.level);
            if (d.isPlayer) {
                renderer_->setPlayerSpawnId(d.spawnId);
                renderer_->updatePlayerAppearance(d.raceId, d.gender, appearance);
            }
        }
        break;
    case state::GameEventType::EntityDespawned:
        if (renderer_) {
            auto& d = std::get<state::EntityDespawnedData>(event.data);
            renderer_->removeEntity(d.spawnId);
        }
        break;
    case state::GameEventType::EntityMoved:
        if (renderer_) {
            auto& d = std::get<state::EntityMovedData>(event.data);
            renderer_->updateEntity(d.spawnId, d.x, d.y, d.z, d.heading,
                d.dx, d.dy, d.dz, d.animation);
        }
        break;
    case state::GameEventType::EntityStatsChanged:
        // HP bars are drawn from entity data, no dedicated renderer call
        LOG_TRACE(MOD_GRAPHICS, "Bridge: EntityStatsChanged");
        break;
    case state::GameEventType::EntityAppearanceChanged:
        if (renderer_) {
            auto& d = std::get<state::EntityAppearanceChangedData>(event.data);
            EQT::Graphics::EntityAppearance appearance;
            appearance.face = d.face;
            appearance.haircolor = d.haircolor;
            appearance.hairstyle = d.hairstyle;
            appearance.beardcolor = d.beardcolor;
            appearance.beard = d.beard;
            appearance.texture = d.texture;
            appearance.helm = d.helm;
            for (int i = 0; i < 9; i++) {
                appearance.equipment[i] = d.equipment[i];
                appearance.equipment_tint[i] = d.equipmentTint[i];
            }
            renderer_->updateEntityAppearance(d.spawnId, d.raceId, d.gender, appearance);
            if (d.isPlayer) {
                renderer_->updatePlayerAppearance(d.raceId, d.gender, appearance);
            }
        }
        break;
    case state::GameEventType::EntityLightChanged:
        if (renderer_) {
            auto& d = std::get<state::EntityLightChangedData>(event.data);
            renderer_->setEntityLight(d.spawnId, d.lightLevel);
        }
        break;
    case state::GameEventType::EntityAnimationEvent:
        if (renderer_) {
            auto& d = std::get<state::EntityAnimationEventData>(event.data);
            if (!d.animName.empty()) {
                renderer_->setEntityAnimation(d.spawnId, d.animName, d.loop, d.playThrough);
            }
        }
        break;
    case state::GameEventType::EntityPoseStateChanged:
        if (renderer_) {
            auto& d = std::get<state::EntityPoseStateChangedData>(event.data);
            renderer_->setEntityPoseState(d.spawnId,
                static_cast<EQT::Graphics::IrrlichtRenderer::EntityPoseState>(d.poseState));
        }
        break;
    case state::GameEventType::EntityDeathAnimation:
        if (renderer_) {
            auto& d = std::get<state::EntityDeathAnimationData>(event.data);
            renderer_->playEntityDeathAnimation(d.spawnId);
        }
        break;
    case state::GameEventType::CorpseDecayStarted:
        if (renderer_) {
            auto& d = std::get<state::CorpseDecayStartedData>(event.data);
            renderer_->startCorpseDecay(d.spawnId);
        }
        break;
    case state::GameEventType::CombatAnimation:
        if (renderer_) {
            auto& d = std::get<state::CombatAnimationData>(event.data);
            renderer_->queueCombatAnimation(d.sourceId, d.targetId,
                d.damageType, d.damageAmount, d.damagePercent);
        }
        break;
    case state::GameEventType::CombatSkillAnimation:
        if (renderer_) {
            auto& d = std::get<state::CombatSkillAnimationData>(event.data);
            renderer_->queueSkillAnimation(d.spawnId, d.animCode);
        }
        break;
    case state::GameEventType::ReceivedDamageAnimation:
        if (renderer_) {
            auto& d = std::get<state::ReceivedDamageAnimationData>(event.data);
            renderer_->queueReceivedDamageAnimation(d.spawnId);
        }
        break;
    case state::GameEventType::PlayerSpawnIdSet:
        if (renderer_) {
            auto& d = std::get<state::PlayerSpawnIdSetData>(event.data);
            renderer_->setPlayerSpawnId(d.spawnId);
        }
        break;
    case state::GameEventType::NetworkReady:
        if (renderer_) {
            auto& d = std::get<state::NetworkReadyData>(event.data);
            renderer_->setExpectedEntityCount(d.expectedEntityCount);
            renderer_->setNetworkReady(d.ready);
        }
        break;

    // ========================================================================
    // Stubs — to be wired in D11-D13
    // ========================================================================

    // Door events (D12)
    case state::GameEventType::DoorSpawned:
        if (renderer_) {
            auto& d = std::get<state::DoorSpawnedData>(event.data);
            renderer_->createDoor(d.doorId, d.name, d.x, d.y, d.z,
                                  d.heading, d.incline, d.size, d.opentype, d.isOpen);
        }
        break;
    case state::GameEventType::DoorStateChanged:
        if (renderer_) {
            auto& d = std::get<state::DoorStateChangedData>(event.data);
            renderer_->setDoorState(d.doorId, d.isOpen, d.userInitiated);
        }
        break;

    // Zone events (D13)
    case state::GameEventType::ZoneChanged:
        // Zone change triggers full zone reload via loading thread — no bridge action needed.
        LOG_TRACE(MOD_GRAPHICS, "Bridge: ZoneChanged");
        break;
    case state::GameEventType::ZoneLoading:
        if (renderer_) {
            auto& d = std::get<state::ZoneLoadingData>(event.data);
            std::wstring wstatus(d.statusMessage.begin(), d.statusMessage.end());
            renderer_->setLoadingProgress(d.progress, wstatus);
        }
        break;
    case state::GameEventType::ZoneLoaded:
        if (renderer_) {
            renderer_->setZoneReady(true);
            renderer_->hideLoadingScreen();
        }
        break;

    // Chat events (D10)
    case state::GameEventType::ChatMessage:
        if (renderer_) {
            auto& d = std::get<state::ChatMessageData>(event.data);
            eqt::ui::ChatMessage msg;
            msg.sender = d.sender;
            msg.text = d.message;
            msg.channel = static_cast<eqt::ui::ChatChannel>(d.channelType);
            msg.timestamp = static_cast<uint32_t>(std::time(nullptr));
            msg.color = eqt::ui::getChannelColor(msg.channel);
            // U04: Feed new UI
            if (renderer_->newUIChatBuffer_) {
                renderer_->newUIChatBuffer_->addMessage(std::move(msg));
            }
        }
        break;
    case state::GameEventType::SystemMessage:
        if (renderer_) {
            auto& d = std::get<state::ChatMessageData>(event.data);
            // U04: Feed new UI
            if (renderer_->newUIChatBuffer_) {
                renderer_->newUIChatBuffer_->addSystemMessage(d.message);
            }
        }
        break;

    // Combat events (D10)
    case state::GameEventType::CombatEvent:
        // CombatEvent carries combat text (hit/miss/dodge etc.) — no dedicated
        // renderer call; combat text is routed through chat window by eq.cpp
        LOG_TRACE(MOD_GRAPHICS, "Bridge: CombatEvent");
        break;
    case state::GameEventType::TargetChanged:
        if (renderer_) {
            auto& d = std::get<state::TargetChangedData>(event.data);
            if (d.spawnId == 0) {
                renderer_->clearCurrentTarget();
            } else {
                EQT::Graphics::TargetInfo info;
                info.spawnId = d.spawnId;
                info.name = d.name;
                info.level = d.level;
                info.hpPercent = d.hpPercent;
                info.raceId = d.raceId;
                info.gender = d.gender;
                info.classId = d.classId;
                info.bodyType = d.bodyType;
                info.npcType = d.npcType;
                info.helm = d.helm;
                info.showHelm = d.showHelm;
                info.texture = d.texture;
                for (int i = 0; i < 9; i++) {
                    info.equipment[i] = d.equipment[i];
                    info.equipmentTint[i] = d.equipmentTint[i];
                }
                renderer_->setCurrentTargetInfo(info);
            }
            // U03d: Cache for new static UI
            auto& ct = renderer_->cachedTargetInfo_;
            ct.spawnId = d.spawnId;
            ct.name = d.name;
            ct.level = d.level;
            ct.hpPercent = d.hpPercent;
        }
        break;
    case state::GameEventType::TargetHPUpdated:
        if (renderer_) {
            auto& d = std::get<state::TargetHPUpdatedData>(event.data);
            renderer_->updateCurrentTargetHP(d.hpPercent);
            // U03d: Cache for new static UI
            renderer_->cachedTargetInfo_.hpPercent = d.hpPercent;
        }
        break;
    case state::GameEventType::DamageEvent:
        if (renderer_) {
            auto& d = std::get<state::DamageEventData>(event.data);
            renderer_->queueReceivedDamageAnimation(d.targetId);
        }
        break;
    case state::GameEventType::SpellCastStarted:
        // No direct renderer call — spell casting UI is handled through
        // SpellManager callbacks and CastingStateChanged events (D12)
        LOG_TRACE(MOD_GRAPHICS, "Bridge: SpellCastStarted");
        break;
    case state::GameEventType::SpellCastComplete:
        // No direct renderer call — spell completion UI is handled through
        // SpellManager callbacks and CastingStateChanged events (D12)
        LOG_TRACE(MOD_GRAPHICS, "Bridge: SpellCastComplete");
        break;

    // Group events (D12)
    case state::GameEventType::GroupChanged:
        if (renderer_) {
            auto& d = std::get<state::GroupChangedData>(event.data);
            // U06e: New UI
            renderer_->groupPanelState_.inGroup = d.inGroup;
            renderer_->groupPanelState_.isLeader = d.isLeader;
            renderer_->groupPanelState_.leaderName = d.leaderName;
            renderer_->groupPanelState_.memberCount = d.memberCount;
            if (!d.inGroup) {
                for (int i = 0; i < EQT::Graphics::GroupPanelState::MAX_MEMBERS; ++i)
                    renderer_->groupPanelState_.members[i] = {};
            }
        }
        break;
    case state::GameEventType::GroupMemberUpdated:
        if (renderer_) {
            auto& d = std::get<state::GroupMemberUpdatedData>(event.data);
            // U06e: New UI
            if (d.memberIndex >= 0 && d.memberIndex < EQT::Graphics::GroupPanelState::MAX_MEMBERS) {
                auto& m = renderer_->groupPanelState_.members[d.memberIndex];
                m.name = d.name;
                m.hpPercent = d.hpPercent;
                m.manaPercent = d.manaPercent;
                m.inZone = d.inZone;
            }
        }
        break;
    case state::GameEventType::GroupInviteReceived:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: GroupInviteReceived");
        break;

    // Time events (D13)
    case state::GameEventType::TimeOfDayChanged:
        if (renderer_) {
            auto& d = std::get<state::TimeOfDayChangedData>(event.data);
            renderer_->updateTimeOfDay(d.hour, d.minute);
        }
        break;

    // Pet events (D12)
    case state::GameEventType::PetCreated:
        if (renderer_) {
            auto& d = std::get<state::PetCreatedData>(event.data);
            // U06f: New UI
            renderer_->petPanelState_.hasPet = true;
            renderer_->petPanelState_.name = d.name;
            renderer_->petPanelState_.level = d.level;
            renderer_->petPanelState_.hpPercent = 100;
            std::fill(std::begin(renderer_->petPanelState_.buttonStates),
                      std::end(renderer_->petPanelState_.buttonStates), false);
        }
        break;
    case state::GameEventType::PetRemoved:
        if (renderer_) {
            // U06f: New UI
            renderer_->petPanelState_ = {};
        }
        break;
    case state::GameEventType::PetStatsChanged:
        if (renderer_) {
            auto& d = std::get<state::PetStatsChangedData>(event.data);
            // U06f: New UI
            renderer_->petPanelState_.hpPercent = d.hpPercent;
        }
        break;
    case state::GameEventType::PetButtonStateChanged:
        if (renderer_) {
            auto& d = std::get<state::PetButtonStateChangedData>(event.data);
            // U06f: New UI
            if (d.button < EQT::Graphics::PetPanelState::BUTTON_COUNT) {
                renderer_->petPanelState_.buttonStates[d.button] = d.state;
            }
        }
        break;

    // Window events (D11b, D11c, D12)
    case state::GameEventType::VendorWindowOpened:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: VendorWindowOpened");
        break;
    case state::GameEventType::VendorWindowClosed:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: VendorWindowClosed");
        break;
    case state::GameEventType::VendorItemAdded:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: VendorItemAdded");
        break;
    case state::GameEventType::BankWindowOpened:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: BankWindowOpened");
        break;
    case state::GameEventType::BankWindowClosed:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: BankWindowClosed");
        break;
    case state::GameEventType::TrainerWindowOpened:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: TrainerWindowOpened");
        break;
    case state::GameEventType::TrainerWindowClosed:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: TrainerWindowClosed");
        break;
    case state::GameEventType::TradeskillContainerOpened:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: TradeskillContainerOpened");
        break;
    case state::GameEventType::TradeskillContainerClosed:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: TradeskillContainerClosed");
        break;

    // Inventory events (D11a)
    case state::GameEventType::InventorySlotChanged: {
        // U05: Update cached inventory state for new static UI
        if (renderer_ && renderer_->inventoryManager_) {
            auto& d = std::get<state::InventorySlotChangedData>(event.data);
            auto* mgr = renderer_->inventoryManager_;
            // Equipment slots: 0-21, General inventory: 22-29 (mapped to slots 22-29)
            if (d.slotId >= 0 && d.slotId < EQT::Graphics::InventoryPanelState::EQUIP_SLOTS) {
                auto& slot = renderer_->inventoryState_.equipSlots[d.slotId];
                const auto* item = mgr->getItem(d.slotId);
                slot.hasItem = (item != nullptr);
                slot.itemName = item ? item->name : "";
                slot.iconId = item ? item->icon : 0;
                slot.quantity = item ? item->quantity : 0;
            } else if (d.slotId >= 22 && d.slotId < 30) {
                int idx = d.slotId - 22;
                auto& slot = renderer_->inventoryState_.generalSlots[idx];
                const auto* item = mgr->getItem(d.slotId);
                slot.hasItem = (item != nullptr);
                slot.itemName = item ? item->name : "";
                slot.iconId = item ? item->icon : 0;
                slot.quantity = item ? item->quantity : 0;
            }
        }
        break;
    }
    case state::GameEventType::CursorItemChanged:
        // No dedicated renderer call — cursor state read directly by UI
        LOG_TRACE(MOD_GRAPHICS, "Bridge: CursorItemChanged");
        break;
    case state::GameEventType::EquipmentStatsChanged:
        // Equipment stats (AC, ATK, HP, mana, weight) are part of the monolithic
        // updateCharacterStats() call. Bridge-driven stat updates require combining
        // PlayerStatsChanged + EquipmentStatsChanged + CurrencyChanged into a
        // single renderer call. Deferred until Phase 5 refactors the renderer
        // to accept granular stat updates.
        LOG_TRACE(MOD_GRAPHICS, "Bridge: EquipmentStatsChanged (deferred — monolithic renderer API)");
        break;
    case state::GameEventType::CurrencyChanged:
        // Currency display is context-dependent (vendor window, inventory window).
        // The monolithic updateCharacterStats() includes currency. Deferred with
        // EquipmentStatsChanged above.
        LOG_TRACE(MOD_GRAPHICS, "Bridge: CurrencyChanged (deferred — monolithic renderer API)");
        break;
    case state::GameEventType::BankCurrencyChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: BankCurrencyChanged");
        break;
    case state::GameEventType::EntityWeaponSkillsChanged:
        if (renderer_) {
            auto& d = std::get<state::EntityWeaponSkillsChangedData>(event.data);
            renderer_->setEntityWeaponSkills(d.spawnId, d.primaryWeaponSkill, d.secondaryWeaponSkill);
        }
        break;

    // Loot events (D11b)
    case state::GameEventType::LootWindowOpened:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: LootWindowOpened");
        break;
    case state::GameEventType::LootWindowClosed:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: LootWindowClosed");
        break;
    case state::GameEventType::LootItemAdded:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: LootItemAdded");
        break;
    case state::GameEventType::LootItemRemoved:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: LootItemRemoved");
        break;

    // Trade events (D11c)
    case state::GameEventType::TradeRequestReceived:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: TradeRequestReceived");
        break;
    case state::GameEventType::TradeStarted:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: TradeStarted");
        break;
    case state::GameEventType::TradeItemUpdated:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: TradeItemUpdated");
        break;
    case state::GameEventType::TradeMoneyUpdated:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: TradeMoneyUpdated");
        break;
    case state::GameEventType::TradeAcceptStateChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: TradeAcceptStateChanged");
        break;
    case state::GameEventType::TradeCancelled:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: TradeCancelled");
        break;
    case state::GameEventType::TradeCompleted:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: TradeCompleted");
        break;

    // Spell events (D12)
    case state::GameEventType::SpellGemChanged:
        if (renderer_) {
            auto& d = std::get<state::SpellGemChangedData>(event.data);
            if (d.gemSlot < EQT::Graphics::SpellGemPanelState::GEM_COUNT) {
                auto& gem = renderer_->spellGemState_.gems[d.gemSlot];
                gem.spellId = d.spellId;
                gem.gemState = d.gemState;
                gem.spellName = d.spellName;
                gem.iconId = d.iconId;
                gem.cooldownRemainingMs = d.cooldownRemainingMs;
                gem.cooldownTotalMs = d.cooldownTotalMs;
                gem.memorizeTotalMs = d.memorizeTotalMs;
                gem.lastUpdateTime = std::chrono::steady_clock::now();
            }
        }
        break;
    case state::GameEventType::CastingStateChanged:
        // Casting bar is driven by direct SpellManager → WindowManager calls
        // (startCast/cancelCast/completeCast). Event struct lacks spell name
        // required by startCast(). Deferred to Phase 5 renderer refactor.
        LOG_TRACE(MOD_GRAPHICS, "Bridge: CastingStateChanged");
        break;
    case state::GameEventType::SpellMemorizing:
        // Memorize bar is driven by direct SpellManager → WindowManager calls
        // (startMemorize/cancelMemorize/completeMemorize). Event struct lacks
        // spell name. Deferred to Phase 5 renderer refactor.
        LOG_TRACE(MOD_GRAPHICS, "Bridge: SpellMemorizing");
        break;
    case state::GameEventType::BuffUpdated:
        if (renderer_) {
            auto& d = std::get<state::BuffUpdatedData>(event.data);
            if (d.slot < EQT::Graphics::BuffBarState::MAX_BUFFS) {
                auto& buff = renderer_->buffBarState_.buffs[d.slot];
                buff.spellId = d.spellId;
                buff.spellName = d.spellName;
                buff.iconId = d.iconId;
                buff.ticksLeft = d.ticksLeft;
                buff.updateTime = std::chrono::steady_clock::now();
            }
        }
        break;
    case state::GameEventType::BuffRemoved:
        if (renderer_) {
            auto& d = std::get<state::BuffRemovedData>(event.data);
            if (d.slot < EQT::Graphics::BuffBarState::MAX_BUFFS) {
                renderer_->buffBarState_.buffs[d.slot] = {};
            }
        }
        break;
    case state::GameEventType::VisionChanged:
        if (renderer_) {
            auto& d = std::get<state::VisionChangedData>(event.data);
            renderer_->setVisionType(
                static_cast<EQT::Graphics::VisionType>(d.visionType));
        }
        break;

    // Skill events (D12)
    case state::GameEventType::SkillValueChanged:
        if (renderer_) {
            auto& d = std::get<state::SkillValueChangedData>(event.data);
            // Update cached skill value in new UI
            for (auto& s : renderer_->skillsPopupState_.skills) {
                if (s.skillId == static_cast<uint8_t>(d.skillId)) {
                    s.value = d.value;
                    break;
                }
            }
        }
        break;
    case state::GameEventType::SkillsRefreshed:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: SkillsRefreshed");
        break;
    case state::GameEventType::SkillsSnapshot:
        if (renderer_) {
            auto& d = std::get<state::SkillsSnapshotData>(event.data);
            renderer_->skillsPopupState_.skills.clear();
            renderer_->skillsPopupState_.scrollOffset = 0;
            for (const auto& s : d.skills) {
                EQT::Graphics::SkillDisplayItem item;
                item.skillId = s.skillId;
                item.name = s.name;
                item.value = s.value;
                item.maxValue = s.maxValue;
                renderer_->skillsPopupState_.skills.push_back(std::move(item));
            }
        }
        break;

    // World/environment events (D13)
    case state::GameEventType::WeatherChanged:
        if (renderer_) {
            auto& d = std::get<state::WeatherChangedData>(event.data);
            renderer_->setWeather(d.type, d.intensity);
        }
        break;
    case state::GameEventType::SwimmingStateChanged:
        if (renderer_) {
            auto& d = std::get<state::SwimmingStateChangedData>(event.data);
            renderer_->setSwimmingState(d.isSwimming, d.swimSpeed, d.isLevitating);
        }
        break;

    // Zone lifecycle events (D13)
    case state::GameEventType::CollisionMapChanged:
        if (renderer_) {
            auto& d = std::get<state::CollisionMapChangedData>(event.data);
            renderer_->setCollisionMap(static_cast<HCMap*>(d.map));
        }
        break;
    case state::GameEventType::ZoneLineBoundingBoxes:
        // Zone line bounding boxes set during zone loading via setZoneLineBoundingBoxes().
        // Bridge should not duplicate zone-load infrastructure.
        LOG_TRACE(MOD_GRAPHICS, "Bridge: ZoneLineBoundingBoxes");
        break;

    // UI/misc events (D13)
    case state::GameEventType::ExpProgressChanged:
        if (renderer_) {
            auto& d = std::get<state::ExpProgressChangedData>(event.data);
            renderer_->setExpProgress(d.progress);
            // U06i: New UI
            renderer_->xpBarState_.progress = d.progress;
        }
        break;
    case state::GameEventType::CharacterInfoChanged:
        if (renderer_) {
            auto& d = std::get<state::CharacterInfoChangedData>(event.data);
            std::wstring wname(d.name.begin(), d.name.end());
            std::wstring wclass(d.className.begin(), d.className.end());
            std::wstring wdeity(d.deity.begin(), d.deity.end());
            renderer_->setCharacterInfo(wname, d.level, wclass);
            renderer_->setCharacterDeity(wdeity);
            // U03d: Cache for new static UI
            renderer_->cachedPlayerStats_.name = d.name;
            renderer_->cachedPlayerStats_.level = d.level;
        }
        break;
    case state::GameEventType::WorldObjectSpawned:
        if (renderer_) {
            auto& d = std::get<state::WorldObjectSpawnedData>(event.data);
            if (eqt::inventory::isTradeskillContainerType(d.objectType)) {
                renderer_->addWorldObject(d.dropId, d.x, d.y, d.z,
                    d.objectType, d.modelName);
            }
        }
        break;
    case state::GameEventType::NoteWindowOpened:
        if (renderer_) {
            auto& d = std::get<state::NoteWindowOpenedData>(event.data);
            renderer_->showNoteWindow(d.text, d.type);
        }
        break;
    case state::GameEventType::SpellScribeCompleted:
        if (renderer_) {
            auto& d = std::get<state::SpellScribeCompletedData>(event.data);
            // U06g: Add to new UI spellbook
            EQT::Graphics::SpellbookDisplayEntry entry;
            entry.slot = d.slot;
            entry.spellId = d.spellId;
            entry.name = d.spellName;
            entry.iconId = d.iconId;
            renderer_->spellbookState_.spells.push_back(std::move(entry));
        }
        break;
    case state::GameEventType::SpellbookSnapshot:
        if (renderer_) {
            auto& d = std::get<state::SpellbookSnapshotData>(event.data);
            renderer_->spellbookState_.spells.clear();
            renderer_->spellbookState_.currentPage = 0;
            for (const auto& s : d.spells) {
                EQT::Graphics::SpellbookDisplayEntry entry;
                entry.slot = s.slot;
                entry.spellId = s.spellId;
                entry.name = s.name;
                entry.iconId = s.iconId;
                entry.level = s.level;
                renderer_->spellbookState_.spells.push_back(std::move(entry));
            }
        }
        break;
    case state::GameEventType::HotbarSlotAssigned:
        if (renderer_) {
            auto& d = std::get<state::HotbarSlotAssignedData>(event.data);
            if (d.index >= 0 && d.index < EQT::Graphics::HotbarPanelState::SLOT_COUNT) {
                auto& slot = renderer_->hotbarState_.slots[d.index];
                slot.type = d.type;
                slot.name = d.name;
                slot.iconId = d.iconId;
            }
        }
        break;
    case state::GameEventType::HotbarCooldownStarted:
        if (renderer_) {
            auto& d = std::get<state::HotbarCooldownStartedData>(event.data);
            // U06a: New UI
            if (d.index >= 0 && d.index < EQT::Graphics::HotbarPanelState::SLOT_COUNT) {
                auto& slot = renderer_->hotbarState_.slots[d.index];
                slot.cooldownDurationMs = d.durationMs;
                slot.cooldownEndTime = std::chrono::steady_clock::now()
                    + std::chrono::milliseconds(d.durationMs);
            }
        }
        break;
    case state::GameEventType::SkillActivationFeedback:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: SkillActivationFeedback");
        break;
    case state::GameEventType::RendererCommand:
        if (renderer_) {
            auto& d = std::get<state::RendererCommandData>(event.data);
            // D20e3: Internal commands not exposed as slash commands
            if (d.command == "/unloadzone_internal") {
                renderer_->unloadZone();
            } else {
                renderer_->processSlashCommand(d.command);
            }
        }
        break;
    case state::GameEventType::ToggleSkillsWindow:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: ToggleSkillsWindow");
        break;

    // ========================================================================
    // D20e1: Spell visual FX events
    // ========================================================================
    case state::GameEventType::SpellCastVisualStarted:
        if (renderer_) {
            auto& d = std::get<state::SpellCastVisualStartedData>(event.data);
            // Create cast glow effect
            if (renderer_->getSpellVisualFX()) {
                renderer_->getSpellVisualFX()->createCastGlow(d.casterId, d.spellId, d.castTimeMs);
            }
            if (d.isPlayerCast) {
                // U06d: New UI casting bar
                renderer_->castingBarState_.isCasting = true;
                renderer_->castingBarState_.spellName = d.spellName;
                renderer_->castingBarState_.castTimeMs = d.castTimeMs;
                renderer_->castingBarState_.castStartTime = std::chrono::steady_clock::now();
            } else {
                // Start entity casting bar (shows above the entity's head)
                if (renderer_->getEntityRenderer()) {
                    renderer_->getEntityRenderer()->startEntityCast(
                        d.casterId, d.spellId, d.spellName, d.castTimeMs);
                }
                // If this is our current target, show target casting bar
                // (target casting bar not yet in new UI)
            }
        }
        break;

    case state::GameEventType::SpellCastVisualComplete:
        if (renderer_) {
            auto& d = std::get<state::SpellCastVisualCompleteData>(event.data);
            // Remove cast glow from caster
            if (renderer_->getSpellVisualFX()) {
                renderer_->getSpellVisualFX()->removeCastGlow(d.casterId);
            }
            // Complete entity casting bar
            if (renderer_->getEntityRenderer()) {
                renderer_->getEntityRenderer()->completeEntityCast(d.casterId);
            }
            // Spell completion visual effects (only on success packet)
            if (d.isSuccess && renderer_->getSpellVisualFX()) {
                renderer_->getSpellVisualFX()->createSpellComplete(d.casterId, d.spellId);
                renderer_->getSpellVisualFX()->createImpact(d.targetId, d.spellId);
            }
            // Play completion animation on NPC casters
            if (!d.completionAnim.empty()) {
                renderer_->setEntityAnimation(d.casterId, d.completionAnim, false, true);
            }
            // Complete player's casting bar
            if (d.isPlayerCast) {
                // U06d: New UI
                renderer_->castingBarState_.isCasting = false;
            }
        }
        break;

    case state::GameEventType::SpellCastVisualInterrupted:
        if (renderer_) {
            auto& d = std::get<state::SpellCastVisualInterruptedData>(event.data);
            // Remove cast glow
            if (renderer_->getSpellVisualFX()) {
                renderer_->getSpellVisualFX()->removeCastGlow(d.casterId);
            }
            // Cancel entity casting bar
            if (renderer_->getEntityRenderer()) {
                renderer_->getEntityRenderer()->cancelEntityCast(d.casterId);
            }
            // Cancel player's casting bar
            if (d.isPlayerCast) {
                // U06d: New UI
                renderer_->castingBarState_.isCasting = false;
            }
        }
        break;

    case state::GameEventType::SpellMemorizeVisualStarted:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: SpellMemorizeVisualStarted");
        break;

    case state::GameEventType::SpellMemorizeVisualComplete:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: SpellMemorizeVisualComplete");
        break;

    // ========================================================================
    // D20e1: Zone lifecycle events
    // ========================================================================
    case state::GameEventType::ZoneUnloading:
        if (renderer_) {
            renderer_->setZoneReady(false);
            renderer_->showLoadingScreen();
        }
        break;

    case state::GameEventType::NavmeshChanged:
        // Handled by renderer if navmesh visualization is active
        LOG_TRACE(MOD_GRAPHICS, "Bridge: NavmeshChanged");
        break;

    // ========================================================================
    // D20e1: Diagnostics events
    // ========================================================================
    case state::GameEventType::DiagnosticsMemoryReport:
        if (renderer_) {
            auto& d = std::get<state::DiagnosticsMemoryReportData>(event.data);
            EQT::Graphics::MemoryReportInput ext;
            ext.processRssBytes = d.processRssBytes;
            ext.processVmBytes = d.processVmBytes;
            ext.sharedLibBytes = d.sharedLibBytes;
            ext.anonBytes = d.anonBytes;
            ext.stackBytes = d.stackBytes;
            ext.audioAvailable = d.audioAvailable;
            ext.soundBufferCacheBytes = d.soundBufferCacheBytes;
            ext.soundBufferCacheMaxBytes = d.soundBufferCacheMaxBytes;
            ext.soundFontEstimateBytes = d.soundFontEstimateBytes;
            ext.musicDecodedBytes = d.musicDecodedBytes;
            ext.audioPfsArchiveBytes = d.audioPfsArchiveBytes;
            ext.sfxCacheBytes = d.sfxCacheBytes;
            ext.zoneEmitterCount = d.zoneEmitterCount;
            ext.activeEmitterCount = d.activeEmitterCount;
            ext.entityCount = d.entityCount;
            ext.entityEstimateBytes = d.entityEstimateBytes;
            ext.doorCount = d.doorCount;
            ext.doorEstimateBytes = d.doorEstimateBytes;
            ext.spellDbCount = d.spellDbCount;
            ext.spellDbEstimateBytes = d.spellDbEstimateBytes;
            for (const auto& ci : d.connections) {
                EQT::Graphics::MemoryReportInput::ConnectionInfo c;
                c.name = ci.name;
                c.recvBytes = ci.recvBytes;
                c.sentBytes = ci.sentBytes;
                c.avgPing = ci.avgPing;
                ext.connections.push_back(std::move(c));
            }
            if (!d.label.empty()) {
                LOG_INFO(MOD_MAIN, "=== /pmem [{}] ===", d.label);
            }
            auto report = renderer_->getMemoryReport(ext);
            for (const auto& line : report) {
                LOG_INFO(MOD_MAIN, "{}", line);
            }
        }
        break;

    case state::GameEventType::DiagnosticsSceneDump:
        if (renderer_) {
            renderer_->dumpScene();
        }
        break;
    }
}

} // namespace bridge
} // namespace eqt
