#include "client/bridge/irrlicht_bridge.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/ui/window_manager.h"
#include "client/graphics/ui/chat_window.h"
#include "client/graphics/ui/chat_message_buffer.h"
#include "client/graphics/ui/item_instance.h"
#include "client/graphics/ui/skill_trainer_window.h"
#include "client/graphics/ui/group_window.h"
#include "client/graphics/ui/inventory_constants.h"
#include "common/logging.h"
#include <ctime>
#include <memory>

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
        // D10: PlayerStatsChanged carries HP/mana/endurance/level only.
        // The renderer's updateCharacterStats() requires all stats (AC, ATK,
        // attributes, resistances, weight, currency) which arrive via separate
        // events (EquipmentStatsChanged, CurrencyChanged — D11a scope).
        // Full bridge-driven stat updates will be wired when all events are combined.
        LOG_TRACE(MOD_GRAPHICS, "Bridge: PlayerStatsChanged (partial — awaiting D11a)");
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
        // Loading screen progress driven by loading thread — no bridge action needed.
        LOG_TRACE(MOD_GRAPHICS, "Bridge: ZoneLoading");
        break;
    case state::GameEventType::ZoneLoaded:
        // Zone load completion handled by loading thread callbacks — no bridge action needed.
        LOG_TRACE(MOD_GRAPHICS, "Bridge: ZoneLoaded");
        break;

    // Chat events (D10)
    case state::GameEventType::ChatMessage:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                auto* chatWindow = wm->getChatWindow();
                if (chatWindow) {
                    auto& d = std::get<state::ChatMessageData>(event.data);
                    eqt::ui::ChatMessage msg;
                    msg.sender = d.sender;
                    msg.text = d.message;
                    msg.channel = static_cast<eqt::ui::ChatChannel>(d.channelType);
                    msg.timestamp = static_cast<uint32_t>(std::time(nullptr));
                    msg.color = eqt::ui::getChannelColor(msg.channel);
                    chatWindow->addMessage(std::move(msg));
                }
            }
        }
        break;
    case state::GameEventType::SystemMessage:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                auto* chatWindow = wm->getChatWindow();
                if (chatWindow) {
                    auto& d = std::get<state::ChatMessageData>(event.data);
                    chatWindow->addSystemMessage(d.message);
                }
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
        // GroupWindow polls group state via update() each frame — no action needed.
        LOG_TRACE(MOD_GRAPHICS, "Bridge: GroupChanged");
        break;
    case state::GameEventType::GroupMemberUpdated:
        // GroupWindow polls member data via update() each frame — no action needed.
        LOG_TRACE(MOD_GRAPHICS, "Bridge: GroupMemberUpdated");
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
        // Sky renderer reads time from WorldState each frame — no bridge action needed.
        LOG_TRACE(MOD_GRAPHICS, "Bridge: TimeOfDayChanged");
        break;

    // Pet events (D12)
    case state::GameEventType::PetCreated:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                wm->openPetWindow();
            }
        }
        break;
    case state::GameEventType::PetRemoved:
        if (renderer_) {
            auto* wm = renderer_->getWindowManager();
            if (wm) {
                wm->closePetWindow();
            }
        }
        break;
    case state::GameEventType::PetStatsChanged:
        // PetWindow polls pet stats via update() each frame — no action needed.
        LOG_TRACE(MOD_GRAPHICS, "Bridge: PetStatsChanged");
        break;
    case state::GameEventType::PetButtonStateChanged:
        // PetWindow polls button state via update() each frame — no action needed.
        LOG_TRACE(MOD_GRAPHICS, "Bridge: PetButtonStateChanged");
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
    case state::GameEventType::InventorySlotChanged:
        // No dedicated renderer call — inventory UI reads state directly
        LOG_TRACE(MOD_GRAPHICS, "Bridge: InventorySlotChanged");
        break;
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
        // Spell gem panel polls gem state from SpellState each frame — no action needed.
        LOG_TRACE(MOD_GRAPHICS, "Bridge: SpellGemChanged");
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
        // Collision map is set during zone loading via setCollisionMap(). The event
        // carries a raw pointer — bridge should not duplicate zone-load infrastructure.
        LOG_TRACE(MOD_GRAPHICS, "Bridge: CollisionMapChanged");
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
    }
}

} // namespace bridge
} // namespace eqt
