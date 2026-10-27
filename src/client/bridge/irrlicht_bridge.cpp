#include "client/bridge/irrlicht_bridge.h"
#include "client/bridge/spell_cast_fx.h"
#include "client/core/memory_ledger.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/spell_visual_fx.h"
#include "client/chat/chat_message_buffer.h"
#include "client/chat/chat_text_colors.h"   // B993
#include "client/graphics/ui/confirmation_dialog.h"   // B442b
#include "client/graphics/ui/cursor_attachment.h"
#include "client/graphics/ui/merchant_panel.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/static_panels.h"  // B1076 fix-2: scribeGaugeClear()
#include "client/inventory/inventory_constants.h"
#include "client/inventory/inventory_manager.h"
#include "common/logging.h"
#include "client/graphics/ui/ui_layout_table.h"  // B1075: closesOnOpen()
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
            // B912 (GitLab #231): the flag was dropped here, so a walk the
            // game thread drives (/move, /moveto, /follow) was invisible to
            // everything renderer-side that asks "is the player moving".
            renderer_->setPlayerGameDrivenMoving(d.isMoving);
        }
        break;
    case state::GameEventType::PlayerStatsChanged:
        if (renderer_) {
            auto& d = std::get<state::PlayerStatsChangedData>(event.data);
            // U03d: Cache for new static UI
            auto& cached = renderer_->Frame().cachedPlayerStats_;
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
    case state::GameEventType::EntitySizeChanged:
        if (renderer_) {
            auto& d = std::get<state::EntitySizeChangedData>(event.data);
            renderer_->setEntitySize(d.spawnId, d.size);
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
                                  d.heading, d.incline, d.size, d.opentype, d.isOpen,
                                  d.doorParam);  // T438 (B422): a lift's travel
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
            // B993: the [TextColors] row the chat panel draws it in.
            msg.userColor = static_cast<uint8_t>(eqt::ui::userColorFor(msg.channel, d.chatType));
            // U04: Feed new UI
            if (renderer_->Frame().newUIChatBuffer_) {
                renderer_->Frame().newUIChatBuffer_->addMessage(std::move(msg));
            }
        }
        break;
    case state::GameEventType::SystemMessage:
        if (renderer_) {
            auto& d = std::get<state::ChatMessageData>(event.data);
            // U04: Feed new UI
            if (renderer_->Frame().newUIChatBuffer_) {
                renderer_->Frame().newUIChatBuffer_->addSystemMessage(d.message);
            }
        }
        break;

    // Combat events (D10)
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
            auto& ct = renderer_->Frame().cachedTargetInfo_;
            ct.spawnId = d.spawnId;
            ct.name = d.name;
            ct.level = d.level;
            ct.hpPercent = d.hpPercent;
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
            renderer_->Frame().groupPanelState_.inGroup = d.inGroup;
            renderer_->Frame().groupPanelState_.isLeader = d.isLeader;
            renderer_->Frame().groupPanelState_.leaderName = d.leaderName;
            renderer_->Frame().groupPanelState_.memberCount = d.memberCount;
            if (!d.inGroup) {
                for (int i = 0; i < EQT::Graphics::GroupPanelState::MAX_MEMBERS; ++i)
                    renderer_->Frame().groupPanelState_.members[i] = {};
            }
        }
        break;
    case state::GameEventType::GroupMemberUpdated:
        if (renderer_) {
            auto& d = std::get<state::GroupMemberUpdatedData>(event.data);
            // U06e: New UI
            if (d.memberIndex >= 0 && d.memberIndex < EQT::Graphics::GroupPanelState::MAX_MEMBERS) {
                auto& m = renderer_->Frame().groupPanelState_.members[d.memberIndex];
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
            renderer_->Frame().petPanelState_.hasPet = true;
            // B430: OGP_PetWindowPopupCheckbox -- a new pet opens the pet
            // window when the option is on (default on, the oracle capture);
            // off leaves the player's own Alt+F1 choice alone.
            if (EQT::Graphics::optionsSettingStep(
                    renderer_->Frame().optionsState_,
                    EQT::Graphics::OptionsLayout::Setting::PetWindowPopup) != 0) {
                renderer_->Frame().petPanelState_.isOpen = true;
            }
            renderer_->Frame().petPanelState_.name = d.name;
            renderer_->Frame().petPanelState_.level = d.level;
            renderer_->Frame().petPanelState_.hpPercent = 100;
            std::fill(std::begin(renderer_->Frame().petPanelState_.buttonStates),
                      std::end(renderer_->Frame().petPanelState_.buttonStates), false);
        }
        break;
    case state::GameEventType::PetRemoved:
        if (renderer_) {
            // U06f: New UI
            // T368 (B101): clear the pet, KEEP the window's open flag. A
            // wholesale `= {}` reset isOpen back to its default true, so a
            // player who closed the pet window (Alt+F1 since T382) had it
            // silently reopened by the next charm break.
            const bool wasOpen = renderer_->Frame().petPanelState_.isOpen;
            renderer_->Frame().petPanelState_ = {};
            renderer_->Frame().petPanelState_.isOpen = wasOpen;
        }
        break;
    case state::GameEventType::PetStatsChanged:
        if (renderer_) {
            auto& d = std::get<state::PetStatsChangedData>(event.data);
            // U06f: New UI
            renderer_->Frame().petPanelState_.hpPercent = d.hpPercent;
        }
        break;
    case state::GameEventType::PetButtonStateChanged:
        if (renderer_) {
            auto& d = std::get<state::PetButtonStateChangedData>(event.data);
            // U06f: New UI
            if (d.button < EQT::Graphics::PetPanelState::BUTTON_COUNT) {
                renderer_->Frame().petPanelState_.buttonStates[d.button] = d.state;
            }
        }
        break;

    // Window events (D11b, D11c, D12)
    //
    // T436 (board B429): these five were LOG_TRACE stubs that DISCARDED their
    // payloads, which is why there was no merchant window and no bank window
    // in the tree at all -- the same shape T290 found the four loot arms in.
    // The bodies live in merchant_panel.cpp / bank_panel.cpp as free
    // functions so a test can drive the REAL publisher's data struct through
    // the REAL applier with no Irrlicht device.
    case state::GameEventType::VendorWindowOpened:
        if (renderer_) {
            auto& d = std::get<state::WindowOpenedData>(event.data);
            // B1075 (#395): the co-open table, not "centre popups are
            // mutually exclusive", decides -- inventory+merchant is a
            // required set (sell from the bags), so the inventory stays.
            if (EQT::Graphics::closesOnOpen(EQT::Graphics::UIWindow::Merchant,
                                            EQT::Graphics::UIWindow::Inventory)) {
                renderer_->Frame().inventoryState_.activePopup =
                    EQT::Graphics::PopupType::None;
            }
            renderer_->Frame().inventoryState_.selectedSlot = -1;  // B442b
            EQT::Graphics::applyVendorWindowOpened(
                renderer_->Frame().merchantState_, d);
        }
        break;
    case state::GameEventType::VendorWindowClosed:
        if (renderer_) {
            auto& d = std::get<state::WindowClosedData>(event.data);
            // B442b (#152): the sale selection ends with the session.
            renderer_->Frame().inventoryState_.selectedSlot = -1;
            EQT::Graphics::applyVendorWindowClosed(
                renderer_->Frame().merchantState_, d);
        }
        break;
    case state::GameEventType::VendorItemAdded:
        if (renderer_) {
            auto& d = std::get<state::VendorItemAddedData>(event.data);
            EQT::Graphics::applyVendorItemAdded(
                renderer_->Frame().merchantState_, d);
        }
        break;
    case state::GameEventType::BankWindowOpened:
        if (renderer_) {
            auto& d = std::get<state::WindowOpenedData>(event.data);
            EQT::Graphics::applyBankWindowOpened(
                renderer_->Frame().bankState_, d);
        }
        break;
    case state::GameEventType::BankWindowClosed:
        if (renderer_) {
            auto& d = std::get<state::WindowClosedData>(event.data);
            EQT::Graphics::applyBankWindowClosed(
                renderer_->Frame().bankState_, d);
        }
        break;
    // B898 (#214): both were LOG_TRACEs that discarded the decoded
    // OP_GMTraining reply. They now feed the trainer window (trainer_panel.cpp).
    case state::GameEventType::TrainerWindowOpened:
        if (renderer_) {
            EQT::Graphics::applyTrainerWindowOpened(
                renderer_->Frame().trainerState_,
                std::get<state::TrainerWindowOpenedData>(event.data));
        }
        break;
    case state::GameEventType::TrainerWindowClosed:
        if (renderer_) {
            EQT::Graphics::applyTrainerWindowClosed(
                renderer_->Frame().trainerState_,
                std::get<state::WindowClosedData>(event.data));
        }
        break;
    // T415 (board B321): both arms were LOG_TRACEs that discarded their
    // payloads, because fable had no container window. They now feed one.
    // B899 (#215, Will 2026-09-23): both arms feed the era CONTAINER window
    // (EQUI_Container.xml), world and carried alike -- no longer T415's
    // recipe window, whose recipe half is out of era. The container window is
    // not a centre popup (its own <Location>), so opening one closes nothing.
    case state::GameEventType::TradeskillContainerOpened:
        if (renderer_) {
            auto& d = std::get<state::TradeskillContainerOpenedEvent>(event.data);
            EQT::Graphics::applyContainerOpened(renderer_->Frame().containerState_, d);
        }
        break;
    case state::GameEventType::TradeskillContainerClosed:
        if (renderer_) {
            auto& d = std::get<state::TradeskillContainerClosedEvent>(event.data);
            EQT::Graphics::applyContainerClosed(renderer_->Frame().containerState_, d);
        }
        break;
    // T419 (board B335): the recipe list. `Cleared` is the client's own
    // OP_RecipesSearch / OP_RecipesFavorite going out (the server marks no
    // list boundary); `Received` is one OP_RecipeReply, of which the server
    // sends one per matching recipe.
    case state::GameEventType::TradeskillRecipeListCleared:
        if (renderer_) {
            auto& d = std::get<state::TradeskillRecipeListClearedData>(event.data);
            EQT::Graphics::applyTradeskillRecipeListCleared(
                renderer_->Frame().tradeskillState_, d);
        }
        break;
    case state::GameEventType::TradeskillRecipeReceived:
        if (renderer_) {
            auto& d = std::get<state::TradeskillRecipeReceivedData>(event.data);
            EQT::Graphics::applyTradeskillRecipeReceived(
                renderer_->Frame().tradeskillState_, d);
        }
        break;

    // T421 (board B298, item half): the item display window's contents. The
    // lines arrive already composed -- BuildItemDisplayText() runs on the
    // GAME thread, where the ItemInstance lives.
    case state::GameEventType::ItemDisplayShown:
        if (renderer_) {
            auto& d = std::get<state::ItemDisplayShownData>(event.data);
            EQT::Graphics::applyItemDisplayShown(
                renderer_->Frame().itemDisplayState_, d);
        }
        break;

    // Inventory events (D11a)
    case state::GameEventType::InventorySlotChanged: {
        // U05/S2: update cached inventory state from event data only — the
        // event carries the display fields, so no cross-thread read of the
        // game-thread InventoryManager is needed.
        if (renderer_) {
            auto& d = std::get<state::InventorySlotChangedData>(event.data);
            // Equipment slots: 0-21, General inventory: 22-29 (mapped to slots 22-29)
            if (d.slotId >= 0 && d.slotId < EQT::Graphics::InventoryPanelState::EQUIP_SLOTS) {
                auto& slot = renderer_->Frame().inventoryState_.equipSlots[d.slotId];
                slot.hasItem = d.hasItem;
                slot.itemName = d.hasItem ? d.itemName : "";
                slot.iconId = d.hasItem ? d.iconId : 0;
                slot.itemId = d.hasItem ? d.itemId : 0;  // B1025-H3
                slot.quantity = d.hasItem ? d.quantity : 0;
                slot.bagSlots = d.hasItem ? d.bagSlots : 0;  // B442b
                slot.scrollSpellId = d.hasItem ? d.scrollSpellId : 0;  // B1076
                // T60: an equip-slot change usually means the player's
                // EntityAppearance changed too (WearChange) — mark the
                // paperdoll dirty so it picks it up next time it renders.
                renderer_->markPaperdollDirty();
            } else if (d.slotId >= 22 && d.slotId < 30) {
                int idx = d.slotId - 22;
                auto& slot = renderer_->Frame().inventoryState_.generalSlots[idx];
                slot.hasItem = d.hasItem;
                slot.itemName = d.hasItem ? d.itemName : "";
                slot.iconId = d.hasItem ? d.iconId : 0;
                slot.itemId = d.hasItem ? d.itemId : 0;  // B1025-H3
                slot.quantity = d.hasItem ? d.quantity : 0;
                // B442b (#151 rows 19-21): a bag here is right-click toggleable.
                slot.bagSlots = d.hasItem ? d.bagSlots : 0;
                slot.scrollSpellId = d.hasItem ? d.scrollSpellId : 0;  // B1076
            } else if (EQT::Graphics::applyCursorSlotChanged(
                           renderer_->Frame().cursorState_, d)) {
                // T436 (board B429): the CURSOR slot (30, or the wire's 33).
                // The event has always carried the icon; nothing consumed it,
                // so a pick-up was invisible. The applier returns false for
                // every other slot id, so this arm is exact.
            } else if (EQT::Graphics::applyBankSlotChanged(
                           renderer_->Frame().bankState_, d)) {
                // T436 (board B429): the BANK range, 2000-2007. Published by
                // the generic ItemPacket choke point in zone_dispatch.cpp --
                // "the single choke point for vendor-buy item placement and
                // bank-window population", as its own comment says -- and
                // dropped here until now.
            } else if (EQT::Graphics::applyContainerSlotChanged(
                           renderer_->Frame().containerState_, d)) {
                // B899 (#215): the WORLD container range, 4000-4009, and the
                // general BAG range, 251-330 -- the container windows' cells.
                // The applier returns false for every other slot id.
            } else if (EQT::Graphics::applyTradeSlotChanged(
                           renderer_->Frame().tradeState_, d)) {
                // B896 (#212): our own trade slots, 3000-3007 -- published by
                // the cursor gesture that placed the item, never by this side.
            } else {
                // T415 (board B321): the WORLD container range, 4000-4009.
                // eqemu sends a tradeskill container's contents as
                // ITEM_PACKET_WORLD_CONTAINER; InventoryManager maps index i
                // to WORLD_BEGIN + i (T282) and zone_dispatch publishes an
                // enriched InventorySlotChanged for it. This branch is where
                // that event was being dropped -- the if/else above covered
                // only 0..29, so an open forge's contents crossed the bridge
                // and went nowhere. applyTradeskillSlotChanged() ignores any
                // slot id outside the world range, so this stays a no-op for
                // bank and cursor slots.
                EQT::Graphics::applyTradeskillSlotChanged(
                    renderer_->Frame().tradeskillState_, d);
            }
            // B442b (#152): the item selected for sale follows its slot.
            EQT::Graphics::applyMerchantSelectionSlotChanged(
                renderer_->Frame().merchantState_, renderer_->Frame().inventoryState_, d);
        }
        break;
    }
    // B442b (#151 row 29): the coin on the cursor, drawn by the attachment.
    case state::GameEventType::CursorCoinChanged:
        if (renderer_) {
            EQT::Graphics::applyCursorCoinChanged(
                renderer_->Frame().cursorState_,
                std::get<state::CursorCoinChangedData>(event.data));
        }
        break;
    // B1025-H3 (GitLab #345): the /hotbutton command's hot button, a social
    // button the game thread produced (eqstr 13278-13283). It lands on the
    // cursor and waits for a click on a hotbar slot; the render thread clears
    // it locally on a place, so the game thread never republishes a clear.
    case state::GameEventType::HotbarAssignPending:
        if (renderer_) {
            EQT::Graphics::applyHotbarAssignPending(
                renderer_->Frame().cursorState_,
                std::get<state::HotbarAssignPendingData>(event.data));
        }
        break;
    // B442b (#151 rows 31-35): a drop / destroy prompt the game composed.
    case state::GameEventType::ConfirmationRequested:
        if (renderer_ && renderer_->Frame().staticUIInput_) {
            EQT::Graphics::applyConfirmationRequested(
                renderer_->Frame().staticUIInput_->confirmationDialog(),
                std::get<state::ConfirmationRequestedData>(event.data));
        }
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
        // T393 (board B201): this was the LOG_TRACE above -- "deferred,
        // monolithic renderer API" -- which is why the player's purse was
        // never drawn anywhere and T386 could not watch it move after a
        // corpse loot. The four counts now land on the inventory popup's
        // panel state through the same free-function applier the loot arms
        // use, so no monolithic stats call is needed to display them.
        if (renderer_) {
            auto& d = std::get<state::CurrencyChangedData>(event.data);
            LOG_DEBUG(MOD_GRAPHICS, "Bridge: CurrencyChanged {}p {}g {}s {}c",
                d.platinum, d.gold, d.silver, d.copper);
            EQT::Graphics::applyCurrencyChanged(
                renderer_->Frame().inventoryState_, d);
        }
        break;
    case state::GameEventType::BankCurrencyChanged:
        // T436 (board B429): the bank window's four BW_Money rows. This was a
        // LOG_TRACE stub for the same reason CurrencyChanged was one before
        // T393 -- there was no window to put the counts in.
        if (renderer_) {
            auto& d = std::get<state::BankCurrencyChangedData>(event.data);
            EQT::Graphics::applyBankCurrencyChanged(
                renderer_->Frame().bankState_, d);
        }
        break;
    case state::GameEventType::VolumesChanged:
        // T439 (board B432): the applied music/effects volume coming back
        // from the audio manager, on the oracle's 0..20 step. This arm is
        // what makes the Options sliders agree with /musicvolume: before it,
        // the only volume traffic was renderer->game and the renderer had to
        // shadow what it last sent.
        if (renderer_) {
            auto& d = std::get<state::VolumesChangedData>(event.data);
            EQT::Graphics::applyVolumesChanged(
                renderer_->Frame().optionsState_, d);
        }
        break;
    case state::GameEventType::GameOptionChanged:
        // B430: a server-owned Options setting as the game thread now holds
        // it (profile at zone-in, /anon, /roleplay, or the window's own
        // GameOptionIntent) -- the same read-back shape as VolumesChanged.
        if (renderer_) {
            auto& d = std::get<state::GameOptionChangedData>(event.data);
            EQT::Graphics::applyGameOptionChanged(
                renderer_->Frame().optionsState_, d.option, d.value);
        }
        break;
    case state::GameEventType::TrackingListChanged:
        // B895 (GitLab #211): the Tracking Window's whole list + settings.
        if (renderer_) {
            auto& d = std::get<state::TrackingListChangedData>(event.data);
            EQT::Graphics::applyTrackingListChanged(
                renderer_->Frame().trackingState_, d, renderer_->Frame().uiLayout_);
        }
        break;
    case state::GameEventType::SocialListsChanged:
        // B897 (GitLab #213): the friends/ignore lists, whole. The window
        // keeps a copy and never edits it -- its buttons push
        // SocialListIntent and the result comes back here.
        if (renderer_) {
            auto& d = std::get<state::SocialListsChangedData>(event.data);
            EQT::Graphics::applySocialListsChanged(renderer_->Frame().friendsState_, d);
        }
        break;
    case state::GameEventType::EntityWeaponSkillsChanged:
        if (renderer_) {
            auto& d = std::get<state::EntityWeaponSkillsChangedData>(event.data);
            renderer_->setEntityWeaponSkills(d.spawnId, d.primaryWeaponSkill, d.secondaryWeaponSkill);
        }
        break;

    // Loot events (D11b; T290 gave them bodies)
    //
    // These four arms were LOG_TRACE stubs that DISCARDED their payloads,
    // which is why no renderer-side corpse contents existed at all after
    // U07c2 deleted LootWindow (T283's audit). The bodies live in
    // loot_panel.cpp as free functions so tests can drive the real
    // publisher through the real applier without an Irrlicht device.
    case state::GameEventType::LootWindowOpened:
        if (renderer_) {
            auto& d = std::get<state::LootWindowOpenedData>(event.data);
            LOG_DEBUG(MOD_GRAPHICS, "Bridge: LootWindowOpened corpse={} '{}'",
                d.corpseId, d.corpseName);
            EQT::Graphics::applyLootWindowOpened(renderer_->Frame().lootState_, d);
            // B1075 (#395): the co-open table decides what the loot window
            // closes. inventory+loot is a required set, so the inventory
            // stays open; the spellbook and skills windows share no set with
            // it and close, as before.
            {
                using EQT::Graphics::UIWindow;
                using EQT::Graphics::closesOnOpen;
                auto& frame = renderer_->Frame();
                if (closesOnOpen(UIWindow::Loot, UIWindow::Inventory))
                    frame.inventoryState_.activePopup = EQT::Graphics::PopupType::None;
                if (closesOnOpen(UIWindow::Loot, UIWindow::Spellbook))
                    frame.spellbookState_.isOpen = false;
                if (closesOnOpen(UIWindow::Loot, UIWindow::Skills))
                    frame.skillsPopupState_.isOpen = false;
            }
        }
        break;
    case state::GameEventType::LootWindowClosed:
        if (renderer_) {
            auto& d = std::get<state::LootWindowClosedData>(event.data);
            LOG_DEBUG(MOD_GRAPHICS, "Bridge: LootWindowClosed corpse={}", d.corpseId);
            EQT::Graphics::applyLootWindowClosed(renderer_->Frame().lootState_, d);
        }
        break;
    case state::GameEventType::LootItemAdded:
        if (renderer_) {
            auto& d = std::get<state::LootItemAddedData>(event.data);
            LOG_DEBUG(MOD_GRAPHICS, "Bridge: LootItemAdded corpse={} slot={} '{}'",
                d.corpseId, d.slot, d.itemName);
            EQT::Graphics::applyLootItemAdded(renderer_->Frame().lootState_, d);
        }
        break;
    case state::GameEventType::LootItemRemoved:
        if (renderer_) {
            auto& d = std::get<state::LootItemRemovedData>(event.data);
            LOG_DEBUG(MOD_GRAPHICS, "Bridge: LootItemRemoved corpse={} slot={}",
                d.corpseId, d.slot);
            EQT::Graphics::applyLootItemRemoved(renderer_->Frame().lootState_, d);
        }
        break;

    // Trade events (D11c). B896 (GitLab #212): the arms were LOG_TRACEs that
    // dropped every payload, because there was no window. They feed the
    // trade / give window now (trade_panel.cpp). MEASURED behaviour,
    // review/UIGAP-ORACLE/212/trade.tsv: the window opens with the INVENTORY
    // beside it (row 1) and both close at completion (row 7) and on cancel
    // (row 8, A.R21_after_B_cancel.png).
    case state::GameEventType::TradeRequestReceived:
        // The official client acks a request by itself (row 1); the window
        // opens on the TradeStarted that ack produces, so there is no prompt.
        LOG_TRACE(MOD_GRAPHICS, "Bridge: TradeRequestReceived");
        break;
    case state::GameEventType::TradeStarted:
        if (renderer_) {
            auto& d = std::get<state::TradeStartedData>(event.data);
            auto& frame = renderer_->Frame();
            EQT::Graphics::applyTradeStarted(frame.tradeState_, d, frame.cachedPlayerStats_.name);
            // Row 1: the Inventory opens with it (inventory+trade is a
            // required co-open set, B1075), and whatever the co-open table
            // says the inventory cannot share the screen with closes.
            frame.inventoryState_.activePopup = EQT::Graphics::PopupType::Inventory;
            if (EQT::Graphics::closesOnOpen(EQT::Graphics::UIWindow::Inventory,
                                            EQT::Graphics::UIWindow::Spellbook))
                frame.spellbookState_.isOpen = false;
            if (EQT::Graphics::closesOnOpen(EQT::Graphics::UIWindow::Inventory,
                                            EQT::Graphics::UIWindow::Skills))
                frame.skillsPopupState_.isOpen = false;
        }
        break;
    case state::GameEventType::TradeItemUpdated:
        if (renderer_) {
            EQT::Graphics::applyTradeItemUpdated(
                renderer_->Frame().tradeState_, std::get<state::TradeItemUpdatedData>(event.data));
        }
        break;
    case state::GameEventType::TradeMoneyUpdated:
        if (renderer_) {
            EQT::Graphics::applyTradeMoneyUpdated(
                renderer_->Frame().tradeState_, std::get<state::TradeMoneyUpdatedData>(event.data));
        }
        break;
    case state::GameEventType::TradeAcceptStateChanged:
        if (renderer_) {
            EQT::Graphics::applyTradeAcceptStateChanged(
                renderer_->Frame().tradeState_,
                std::get<state::TradeAcceptStateChangedData>(event.data));
        }
        break;
    case state::GameEventType::TradeCancelled:
    case state::GameEventType::TradeCompleted:
        if (renderer_) {
            auto& frame = renderer_->Frame();
            // Rows 7 and 8: the window AND the inventory close. Only a window
            // that was open closes the inventory (a declined request that
            // never opened one leaves the inventory as the player had it).
            if (frame.tradeState_.isOpen &&
                frame.inventoryState_.activePopup == EQT::Graphics::PopupType::Inventory) {
                frame.inventoryState_.activePopup = EQT::Graphics::PopupType::None;
            }
            EQT::Graphics::applyTradeEnded(frame.tradeState_);
        }
        break;

    // Spell events (D12)
    case state::GameEventType::SpellGemChanged:
        if (renderer_) {
            auto& d = std::get<state::SpellGemChangedData>(event.data);
            if (d.gemSlot < EQT::VISIBLE_SPELL_GEM_COUNT) {  // T483: the ninth is stored, not shown
                auto& gem = renderer_->Frame().spellGemState_.gems[d.gemSlot];
                gem.spellId = d.spellId;
                gem.gemState = d.gemState;
                gem.spellName = d.spellName;
                gem.iconId = d.iconId;
                gem.cooldownRemainingMs = d.cooldownRemainingMs;
                gem.cooldownTotalMs = d.cooldownTotalMs;
                gem.lastUpdateTime = std::chrono::steady_clock::now();
            }
        }
        break;
    // T413 (board B296): both of these arms used to discard their payload
    // with a comment pointing at WindowManager, a class deleted in 0f25cb57.
    // The static UI has drawn both gauges for units; only the wiring was
    // missing, so every field the game thread computed for them
    // (castTimeRemainingMs/castTimeTotalMs, progressMs/totalMs) was write-only.
    case state::GameEventType::CastingStateChanged:
        if (renderer_) {
            EQT::Graphics::applyCastingStateChanged(
                renderer_->Frame().castingBarState_,
                std::get<state::CastingStateChangedData>(event.data));
        }
        break;
    case state::GameEventType::SpellMemorizing:
        if (renderer_) {
            EQT::Graphics::applySpellMemorizing(
                renderer_->Frame().spellbookState_,
                std::get<state::SpellMemorizingData>(event.data));
        }
        break;
    case state::GameEventType::BuffUpdated:
        if (renderer_) {
            auto& d = std::get<state::BuffUpdatedData>(event.data);
            if (d.slot < EQT::Graphics::BuffBarState::MAX_BUFFS) {
                auto& buff = renderer_->Frame().buffBarState_.buffs[d.slot];
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
                renderer_->Frame().buffBarState_.buffs[d.slot] = {};
            }
        }
        break;
    case state::GameEventType::VisionChanged:
        if (renderer_) {
            // T334 (board B046): the payload is the buff set RECOMPUTED by
            // EverQuest's BuffManager fade callback (core/everquest.cpp), so
            // it is absolute -- 0 means "no vision buffs left", and
            // setVisionType() now applies it over the racial base instead of
            // refusing every decrease. Pinned by
            // tests/test_vision_buff_fade_wire.cpp.
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
            for (auto& s : renderer_->Frame().skillsPopupState_.skills) {
                if (s.skillId == static_cast<uint8_t>(d.skillId)) {
                    s.value = d.value;
                    break;
                }
            }
            // T415: a skill going from 0 to non-zero is a NEW ability row.
            EQT::Graphics::rebuildCombatAbilities(
                renderer_->Frame().combatAbilityState_,
                renderer_->Frame().skillsPopupState_.skills);
        }
        break;
    case state::GameEventType::SkillsRefreshed:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: SkillsRefreshed");
        break;
    case state::GameEventType::SkillsSnapshot:
        if (renderer_) {
            auto& d = std::get<state::SkillsSnapshotData>(event.data);
            renderer_->Frame().skillsPopupState_.skills.clear();
            renderer_->Frame().skillsPopupState_.scrollOffset = 0;
            for (const auto& s : d.skills) {
                EQT::Graphics::SkillDisplayItem item;
                item.skillId = s.skillId;
                item.name = s.name;
                item.value = s.value;
                item.maxValue = s.maxValue;
                renderer_->Frame().skillsPopupState_.skills.push_back(std::move(item));
            }
            // T415 (board B320): the combat abilities window's eight rows are
            // a VIEW of this same list -- no second source of skill data.
            EQT::Graphics::rebuildCombatAbilities(
                renderer_->Frame().combatAbilityState_,
                renderer_->Frame().skillsPopupState_.skills);
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
            renderer_->setSwimmingState(d.isSwimming, d.swimSpeed);
        }
        break;
    case state::GameEventType::LevitationChanged:
        // B892 (GitLab #208): the game thread's own-levitation flip (buff
        // with SPA 57, or a self flymode 2/5 the server sent). The hover
        // physics runs per-frame from this flag; this arm only records it.
        if (renderer_) {
            auto& d = std::get<state::LevitationChangedData>(event.data);
            renderer_->setLevitationState(d.isLevitating);
        }
        break;

    // Zone lifecycle events (D13)
    case state::GameEventType::CollisionMapChanged:
        if (renderer_) {
            auto& d = std::get<state::CollisionMapChangedData>(event.data);
            renderer_->setCollisionMap(d.map);
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
            renderer_->Frame().xpBarState_.progress = d.progress;
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
            renderer_->Frame().cachedPlayerStats_.name = d.name;
            renderer_->Frame().cachedPlayerStats_.level = d.level;
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
            renderer_->Frame().spellbookState_.spells.push_back(std::move(entry));
            // B1076 fix-2, review finding 2: the scribe gauge's lifecycle
            // ends here -- the attempt is done, the gauge empties.
            EQT::Graphics::scribeGaugeClear(renderer_->Frame().spellbookState_);
        }
        break;
    case state::GameEventType::SpellbookSnapshot:
        if (renderer_) {
            auto& d = std::get<state::SpellbookSnapshotData>(event.data);
            renderer_->Frame().spellbookState_.spells.clear();
            renderer_->Frame().spellbookState_.currentPage = 0;
            // B1076 fix-2: a snapshot is a fresh book -- any in-flight gauge
            // belongs to the old one and goes with it.
            EQT::Graphics::scribeGaugeClear(renderer_->Frame().spellbookState_);
            for (const auto& s : d.spells) {
                EQT::Graphics::SpellbookDisplayEntry entry;
                entry.slot = s.slot;
                entry.spellId = s.spellId;
                entry.name = s.name;
                entry.iconId = s.iconId;
                entry.level = s.level;
                renderer_->Frame().spellbookState_.spells.push_back(std::move(entry));
            }
        }
        break;
    case state::GameEventType::HotbarSlotAssigned:
        if (renderer_) {
            // T156: one shared transform (static_panels.cpp), so the tests
            // that drive HotbarModel -> StaticUIInput exercise this exact hop.
            EQT::Graphics::applyHotbarSlotAssigned(
                renderer_->Frame().hotbarState_,
                std::get<state::HotbarSlotAssignedData>(event.data));
        }
        break;
    case state::GameEventType::HotbarCooldownStarted:
        if (renderer_) {
            auto& d = std::get<state::HotbarCooldownStartedData>(event.data);
            // U06a: New UI
            if (d.index >= 0 && d.index < EQT::Graphics::HotbarPanelState::SLOT_COUNT) {
                auto& slot = renderer_->Frame().hotbarState_.slots[d.index];
                slot.cooldownDurationMs = d.durationMs;
                slot.cooldownEndTime = std::chrono::steady_clock::now()
                    + std::chrono::milliseconds(d.durationMs);
            }
        }
        break;
    // T415 (board B320): the arm that discarded the whole payload. Its
    // cooldownMs now drives CAW_CombatEffectTimeRemainingGauge.
    case state::GameEventType::SkillActivationFeedback:
        if (renderer_) {
            auto& d = std::get<state::SkillActivationFeedbackData>(event.data);
            EQT::Graphics::applyCombatAbilityFeedback(
                renderer_->Frame().combatAbilityState_, d);
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

    // ========================================================================
    // D20e1: Spell visual FX events
    // ========================================================================
    case state::GameEventType::SpellCastVisualStarted:
        if (renderer_) {
            auto& d = std::get<state::SpellCastVisualStartedData>(event.data);
            // B1138: the FX half is shared with tools/model_viewer's FX harness
            // (spell_cast_fx.cpp) so the harness draws this exact sequence.
            ApplySpellCastStartedFx(castGlowRoute_, renderer_->getSpellVisualFX(), d,
                                    CastGlowRoute::Clock::now());
            // B1157 (#476): every casting entity -- the player included --
            // plays the spell's CastingAnim track on its model (startEntityCast
            // now animates; the entity cast BAR below it still skips the
            // player).
            if (renderer_->getEntityRenderer()) {
                renderer_->getEntityRenderer()->startEntityCast(
                    d.casterId, d.spellId, d.spellName, d.castTimeMs, d.castAnim);
            }
            if (d.isPlayerCast) {
                // U06d: New UI casting bar
                renderer_->Frame().castingBarState_.isCasting = true;
                renderer_->Frame().castingBarState_.spellName = d.spellName;
                renderer_->Frame().castingBarState_.castTimeMs = d.castTimeMs;
                renderer_->Frame().castingBarState_.castStartTime = std::chrono::steady_clock::now();
            }
        }
        break;

    case state::GameEventType::SpellCastVisualComplete:
        if (renderer_) {
            auto& d = std::get<state::SpellCastVisualCompleteData>(event.data);
            // B1138: B1131's release (or B1105's action-only glow) and B1132's
            // createLanding on the cast-animation packet, shared with the FX
            // harness (spell_cast_fx.cpp).
            ApplySpellCastCompleteFx(castGlowRoute_, renderer_->getSpellVisualFX(), d,
                                     CastGlowRoute::Clock::now());
            // B1174 (#493): the cast-animation packet plays the spell's
            // CastingAnim on its standing source (eqgame 0x47f4af) -- before
            // completeEntityCast, so a source still casting keeps B1157's
            // end-of-cast rule and only a /disc starts the track.
            if (d.isCastAnimation && renderer_->getEntityRenderer()) {
                renderer_->getEntityRenderer()->actionEntityCastAnim(
                    d.casterId, d.castAnim, d.spellVisual.spellAffectIndex, d.sourceClass, d.instrumentMod);
            }
            // Complete entity casting bar
            if (renderer_->getEntityRenderer()) {
                renderer_->getEntityRenderer()->completeEntityCast(d.casterId);
            }
            // Play completion animation on NPC casters
            if (!d.completionAnim.empty()) {
                renderer_->setEntityAnimation(d.casterId, d.completionAnim, false, true);
            }
            // Complete player's casting bar
            if (d.isPlayerCast) {
                // U06d: New UI
                renderer_->Frame().castingBarState_.isCasting = false;
            }
        }
        break;

    case state::GameEventType::SpellCastVisualInterrupted:
        if (renderer_) {
            auto& d = std::get<state::SpellCastVisualInterruptedData>(event.data);
            ApplySpellCastInterruptedFx(castGlowRoute_, renderer_->getSpellVisualFX(), d);
            // Cancel entity casting bar
            if (renderer_->getEntityRenderer()) {
                renderer_->getEntityRenderer()->cancelEntityCast(d.casterId);
            }
            // Cancel player's casting bar
            if (d.isPlayerCast) {
                // U06d: New UI
                renderer_->Frame().castingBarState_.isCasting = false;
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
        if (renderer_) {
            auto& d = std::get<state::NavmeshChangedData>(event.data);
            renderer_->setNavmesh(d.navmesh);
        }
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
            ext.mallocArenaBytes = d.mallocArenaBytes;
            ext.mallocInUseBytes = d.mallocInUseBytes;
            ext.audioAvailable = d.audioAvailable;
            ext.soundBufferCacheBytes = d.soundBufferCacheBytes;
            ext.soundBufferCacheMaxBytes = d.soundBufferCacheMaxBytes;
            ext.soundFontEstimateBytes = d.soundFontEstimateBytes;
            ext.musicDecodedBytes = d.musicDecodedBytes;
            ext.audioPfsArchiveBytes = d.audioPfsArchiveBytes;
            ext.sfxCacheBytes = d.sfxCacheBytes;
            ext.zoneEmitterCount = d.zoneEmitterCount;
            ext.activeEmitterCount = d.activeEmitterCount;
            ext.audioCallbackCount = d.audioCallbackCount;
            ext.audioLateCallbackCount = d.audioLateCallbackCount;
            ext.audioWorstCallbackDeltaUs = d.audioWorstCallbackDeltaUs;
            ext.audioPeakVoices = d.audioPeakVoices;
            ext.audioNoteOnFailures = d.audioNoteOnFailures;
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
            // T491: the allocator-level ledger, same schema as the lifecycle
            // log lines and the benchmark JSON, after the T69 estimates.
            {
                const auto snap = eqt::memory::snapshot(
                    d.label.empty() ? std::string("pmem") : d.label);
                const std::string text = eqt::memory::toReportText(snap);
                size_t start = 0;
                while (start < text.size()) {
                    size_t nl = text.find('\n', start);
                    if (nl == std::string::npos) nl = text.size();
                    report.push_back(text.substr(start, nl - start));
                    start = nl + 1;
                }
                LOG_INFO(MOD_MAIN, "{}", eqt::memory::toLogLine(snap));
                // T491 addendum (board B801): /pmem also answers "what IS the
                // untracked memory" by MAPPING. The biggest owners at the
                // startup baseline (driver/EGL, thread stacks, shared-library
                // data) never go through malloc, so no allocator hook can ever
                // see them -- /proc/self/smaps is the only tool that can.
                const auto maps = eqt::memory::readMappings();
                const std::string mapText = eqt::memory::mappingsToLogLines(
                    d.label.empty() ? std::string("pmem") : d.label, maps, 12);
                size_t mstart = 0;
                while (mstart < mapText.size()) {
                    size_t nl = mapText.find('\n', mstart);
                    if (nl == std::string::npos) nl = mapText.size();
                    report.push_back(mapText.substr(mstart, nl - mstart));
                    mstart = nl + 1;
                }
                LOG_INFO(MOD_MAIN, "{}", mapText);
            }
            for (const auto& line : report) {
                LOG_INFO(MOD_MAIN, "{}", line);
                // T108: mirror to in-game chat -- the old renderer-side
                // "/pmem" handler this replaced did both LOG_INFO and chat;
                // preserve that visibility now that the real (non-empty)
                // report reaches players via this path instead.
                if (renderer_->Frame().newUIChatBuffer_) {
                    renderer_->Frame().newUIChatBuffer_->addSystemMessage(line);
                }
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
