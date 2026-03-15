#include "client/bridge/irrlicht_bridge.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/spell_visual_fx.h"
#include "client/graphics/ui/window_manager.h"
#include "client/graphics/ui/chat_window.h"
#include "client/graphics/ui/chat_message_buffer.h"
#include "client/graphics/ui/item_instance.h"
#include "client/graphics/ui/skill_trainer_window.h"
#include "client/graphics/ui/group_window.h"
#include "client/graphics/ui/inventory_constants.h"
#include "client/graphics/ui/spell_book_window.h"
#include "client/graphics/ui/player_status_window.h"
#include "client/graphics/ui/inventory_manager.h"
#include "client/graphics/ui/item_instance.h"
#include "client/pet_constants.h"
#include "common/logging.h"
#include <ctime>
#include <memory>

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
            auto* wm = renderer_->getWindowManager();
            if (wm && wm->getPlayerStatusWindow()) {
                wm->getPlayerStatusWindow()->setPlayerStats(
                    d.curHP, d.maxHP, d.curMana, d.maxMana, d.curEndurance, d.maxEndurance);
            }
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
            // D20b3: Track entity name for chat auto-completion
            auto* wm = renderer_->getWindowManager();
            if (wm && !d.name.empty()) {
                wm->addEntityName(d.name);
                if (d.isPlayer) wm->setPlayerName(d.name);
            }
        }
        break;
    case state::GameEventType::EntityDespawned:
        if (renderer_) {
            auto& d = std::get<state::EntityDespawnedData>(event.data);
            renderer_->removeEntity(d.spawnId);
            // D20b3: Remove entity name from chat auto-completion cache
            auto* wm = renderer_->getWindowManager();
            if (wm && !d.name.empty()) {
                wm->removeEntityName(d.name);
            }
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
            // Feed old UI
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                auto* chatWindow = wm->getChatWindow();
                if (chatWindow) chatWindow->addMessage(msg);
            }
            // U04: Feed new UI
            if (renderer_->newUIChatBuffer_) {
                renderer_->newUIChatBuffer_->addMessage(std::move(msg));
            }
        }
        break;
    case state::GameEventType::SystemMessage:
        if (renderer_) {
            auto& d = std::get<state::ChatMessageData>(event.data);
            // Feed old UI
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                auto* chatWindow = wm->getChatWindow();
                if (chatWindow) chatWindow->addSystemMessage(d.message);
            }
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
            // D20b4: Push target info to windows that need it
            auto* wm = renderer_->getWindowManager();
            if (d.spawnId == 0) {
                renderer_->clearCurrentTarget();
                if (wm) {
                    if (wm->getGroupWindow()) wm->getGroupWindow()->clearTargetInfo();
                    if (wm->getPlayerStatusWindow()) wm->getPlayerStatusWindow()->clearTarget();
                }
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
                // D20b4: Push target info to windows
                if (wm) {
                    bool isPlayer = (d.npcType == 0);
                    if (wm->getGroupWindow()) wm->getGroupWindow()->setTargetInfo(d.name, isPlayer);
                    if (wm->getPlayerStatusWindow()) {
                        wm->getPlayerStatusWindow()->setTarget(d.name, d.hpPercent, 0, 0);
                    }
                }
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
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                auto* groupWindow = wm->getGroupWindow();
                if (groupWindow) {
                    auto& d = std::get<state::GroupChangedData>(event.data);
                    groupWindow->setGroupState(d.inGroup, d.isLeader, d.leaderName, d.memberCount);
                    groupWindow->hidePendingInvite();
                }
            }
        }
        break;
    case state::GameEventType::GroupMemberUpdated:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                auto* groupWindow = wm->getGroupWindow();
                if (groupWindow) {
                    auto& d = std::get<state::GroupMemberUpdatedData>(event.data);
                    groupWindow->setMemberData(d.memberIndex, d.name, d.hpPercent,
                        d.manaPercent, d.inZone, false);
                }
            }
        }
        break;
    case state::GameEventType::GroupInviteReceived:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                auto& d = std::get<state::GroupInviteReceivedData>(event.data);
                auto* groupWindow = wm->getGroupWindow();
                if (groupWindow) {
                    groupWindow->showPendingInvite(d.inviterName);
                }
                wm->openGroupWindow();
            }
        }
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
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                auto& d = std::get<state::PetCreatedData>(event.data);
                auto* pw = wm->getPetWindow();
                if (pw) pw->setPetInfo(d.name, d.level, 100, true);
                wm->openPetWindow();
            }
        }
        break;
    case state::GameEventType::PetRemoved:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                auto* pw = wm->getPetWindow();
                if (pw) pw->setPetInfo("", 0, 0, false);
                wm->closePetWindow();
            }
        }
        break;
    case state::GameEventType::PetStatsChanged:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                auto& d = std::get<state::PetStatsChangedData>(event.data);
                auto* pw = wm->getPetWindow();
                if (pw) pw->setPetInfo("", 0, d.hpPercent, true);
            }
        }
        break;
    case state::GameEventType::PetButtonStateChanged:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                auto& d = std::get<state::PetButtonStateChangedData>(event.data);
                auto* pw = wm->getPetWindow();
                if (pw) pw->setPetButtonState(static_cast<EQT::PetButton>(d.button), d.state);
            }
        }
        break;

    // Window events (D11b, D11c, D12)
    case state::GameEventType::VendorWindowOpened:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                auto& d = std::get<state::WindowOpenedData>(event.data);
                wm->openVendorWindow(d.npcId, d.npcName, d.sellRate);
            }
        }
        break;
    case state::GameEventType::VendorWindowClosed:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                wm->closeVendorWindow();
            }
        }
        break;
    case state::GameEventType::VendorItemAdded:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                auto& d = std::get<state::VendorItemAddedData>(event.data);
                auto item = std::make_unique<eqt::inventory::ItemInstance>();
                item->itemId = d.itemId;
                item->name = d.itemName;
                item->price = d.price;
                item->quantity = d.quantity;
                wm->addVendorItem(d.vendorSlot, std::move(item));
            }
        }
        break;
    case state::GameEventType::BankWindowOpened:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                wm->openBankWindow();
            }
        }
        break;
    case state::GameEventType::BankWindowClosed:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                wm->closeBankWindow();
            }
        }
        break;
    case state::GameEventType::TrainerWindowOpened:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                auto& d = std::get<state::TrainerWindowOpenedData>(event.data);
                std::wstring trainerName(d.npcName.begin(), d.npcName.end());
                std::vector<eqt::ui::TrainerSkillEntry> skills;
                skills.reserve(d.skills.size());
                for (const auto& s : d.skills) {
                    eqt::ui::TrainerSkillEntry entry;
                    entry.skill_id = s.skillId;
                    entry.name = std::wstring(s.name.begin(), s.name.end());
                    entry.current_value = s.currentValue;
                    entry.max_trainable = s.maxTrainable;
                    entry.cost = s.cost;
                    skills.push_back(std::move(entry));
                }
                wm->openSkillTrainerWindow(d.npcId, trainerName, skills);
                wm->updateSkillTrainerMoney(d.platinum, d.gold, d.silver, d.copper);
                wm->updateSkillTrainerPracticePoints(d.practicePoints);
            }
        }
        break;
    case state::GameEventType::TrainerWindowClosed:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                wm->closeSkillTrainerWindow();
            }
        }
        break;
    case state::GameEventType::TradeskillContainerOpened:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                auto& d = std::get<state::TradeskillContainerOpenedEvent>(event.data);
                if (d.isWorldObject) {
                    wm->openTradeskillContainer(d.objectId, d.containerName,
                        d.containerType, d.slotCount);
                } else {
                    wm->openTradeskillContainerForItem(d.inventorySlot, d.containerName,
                        d.containerType, d.slotCount);
                }
            }
        }
        break;
    case state::GameEventType::TradeskillContainerClosed:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                wm->closeTradeskillContainer();
            }
        }
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
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                auto& d = std::get<state::BankCurrencyChangedData>(event.data);
                wm->updateBankCurrency(
                    static_cast<uint32_t>(d.platinum),
                    static_cast<uint32_t>(d.gold),
                    static_cast<uint32_t>(d.silver),
                    static_cast<uint32_t>(d.copper));
            }
        }
        break;
    case state::GameEventType::EntityWeaponSkillsChanged:
        if (renderer_) {
            auto& d = std::get<state::EntityWeaponSkillsChangedData>(event.data);
            renderer_->setEntityWeaponSkills(d.spawnId, d.primaryWeaponSkill, d.secondaryWeaponSkill);
        }
        break;

    // Loot events (D11b)
    case state::GameEventType::LootWindowOpened:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                auto& d = std::get<state::LootWindowOpenedData>(event.data);
                wm->openLootWindow(d.corpseId, d.corpseName);
            }
        }
        break;
    case state::GameEventType::LootWindowClosed:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                wm->closeLootWindow();
            }
        }
        break;
    case state::GameEventType::LootItemAdded:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                auto& d = std::get<state::LootItemAddedData>(event.data);
                auto item = std::make_unique<eqt::inventory::ItemInstance>();
                item->itemId = d.itemId;
                item->name = d.itemName;
                wm->addLootItem(static_cast<int16_t>(d.slot), std::move(item));
            }
        }
        break;
    case state::GameEventType::LootItemRemoved:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                auto& d = std::get<state::LootItemRemovedData>(event.data);
                wm->removeLootItem(static_cast<int16_t>(d.slot));
            }
        }
        break;

    // Trade events (D11c)
    case state::GameEventType::TradeRequestReceived:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                auto& d = std::get<state::TradeRequestReceivedData>(event.data);
                wm->showTradeRequest(d.spawnId, d.name);
            }
        }
        break;
    case state::GameEventType::TradeStarted:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                auto& d = std::get<state::TradeStartedData>(event.data);
                wm->openTradeWindow(d.partnerId, d.partnerName, d.isNpc);
            }
        }
        break;
    case state::GameEventType::TradeItemUpdated:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                auto& d = std::get<state::TradeItemUpdatedData>(event.data);
                if (d.who == 1) {
                    // Partner item update
                    if (d.itemId != 0) {
                        auto item = std::make_unique<eqt::inventory::ItemInstance>();
                        item->itemId = d.itemId;
                        item->name = d.itemName;
                        wm->setTradePartnerItem(d.slot, std::move(item));
                    } else {
                        wm->clearTradePartnerItem(d.slot);
                    }
                }
                // who==0 (self) items are managed locally by the trade window
            }
        }
        break;
    case state::GameEventType::TradeMoneyUpdated:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                auto& d = std::get<state::TradeMoneyUpdatedData>(event.data);
                if (d.who == 0) {
                    wm->setTradeOwnMoney(d.platinum, d.gold, d.silver, d.copper);
                } else {
                    wm->setTradePartnerMoney(d.platinum, d.gold, d.silver, d.copper);
                }
            }
        }
        break;
    case state::GameEventType::TradeAcceptStateChanged:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                auto& d = std::get<state::TradeAcceptStateChangedData>(event.data);
                wm->setTradeOwnAccepted(d.ownAccepted);
                wm->setTradePartnerAccepted(d.partnerAccepted);
            }
        }
        break;
    case state::GameEventType::TradeCancelled:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                wm->closeTradeWindow(false);
            }
        }
        break;
    case state::GameEventType::TradeCompleted:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                wm->closeTradeWindow(false);
            }
        }
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
        // BuffWindow polls buff data from BuffManager each frame — no action needed.
        LOG_TRACE(MOD_GRAPHICS, "Bridge: BuffUpdated");
        break;
    case state::GameEventType::BuffRemoved:
        // BuffWindow polls buff data from BuffManager each frame — no action needed.
        LOG_TRACE(MOD_GRAPHICS, "Bridge: BuffRemoved");
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
        // SkillsWindow polls skill values from SkillManager each frame — no action needed.
        LOG_TRACE(MOD_GRAPHICS, "Bridge: SkillValueChanged");
        break;
    case state::GameEventType::SkillsRefreshed:
        // SkillsWindow polls skill values from SkillManager each frame — no action needed.
        LOG_TRACE(MOD_GRAPHICS, "Bridge: SkillsRefreshed");
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
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                auto* spellBookWindow = wm->getSpellBookWindow();
                if (spellBookWindow) {
                    spellBookWindow->refresh();
                }
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
            // Old UI
            auto* wm = renderer_->getWindowManager();
            if (wm) wm->startHotbarCooldown(d.index, d.durationMs);
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
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                auto& d = std::get<state::SkillActivationFeedbackData>(event.data);
                if (d.success && d.cooldownMs > 0) {
                    wm->startSkillCooldown(d.skillId, d.cooldownMs);
                }
            }
        }
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
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                wm->toggleSkillsWindow();
            }
        }
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
                // Show player's casting bar
                auto* wm = renderer_->getWindowManager();
                if (wm) {
                    wm->startCast(d.spellName, d.castTimeMs);
                }
            } else {
                // Start entity casting bar (shows above the entity's head)
                if (renderer_->getEntityRenderer()) {
                    renderer_->getEntityRenderer()->startEntityCast(
                        d.casterId, d.spellId, d.spellName, d.castTimeMs);
                }
                // If this is our current target, show target casting bar
                if (d.isTargetCast) {
                    auto* wm = renderer_->getWindowManager();
                    if (wm) {
                        wm->startTargetCast(d.casterName, d.spellName, d.castTimeMs);
                    }
                }
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
            // Complete target casting bar if this was our target
            if (d.isTargetCast) {
                auto* wm = renderer_->getWindowManager();
                if (wm) {
                    wm->completeTargetCast();
                }
            }
            // Complete player's casting bar
            if (d.isPlayerCast) {
                auto* wm = renderer_->getWindowManager();
                if (wm) {
                    wm->completeCast();
                }
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
            // Cancel target casting bar if applicable
            if (d.isTargetCast) {
                auto* wm = renderer_->getWindowManager();
                if (wm) {
                    wm->cancelTargetCast();
                }
            }
            // Cancel player's casting bar
            if (d.isPlayerCast) {
                auto* wm = renderer_->getWindowManager();
                if (wm) {
                    wm->cancelCast();
                }
            }
        }
        break;

    case state::GameEventType::SpellMemorizeVisualStarted:
        if (renderer_) {
            auto& d = std::get<state::SpellMemorizeVisualStartedData>(event.data);
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                wm->startMemorize(d.spellName, d.durationMs);
            }
        }
        break;

    case state::GameEventType::SpellMemorizeVisualComplete:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                wm->completeMemorize();
            }
        }
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
