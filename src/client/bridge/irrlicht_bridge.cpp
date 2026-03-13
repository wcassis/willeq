#include "client/bridge/irrlicht_bridge.h"
#include "client/graphics/irrlicht_renderer.h"
#include "common/logging.h"

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
        LOG_TRACE(MOD_GRAPHICS, "Bridge: PlayerStatsChanged");
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
            renderer_->createEntity(d.spawnId, d.raceId, d.name,
                d.x, d.y, d.z, d.heading, false,
                d.gender, EQT::Graphics::EntityAppearance(),
                d.npcType == 1, d.isCorpse);
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

    // ========================================================================
    // Stubs — to be wired in D10-D13
    // ========================================================================

    // Door events (D12)
    case state::GameEventType::DoorSpawned:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: DoorSpawned");
        break;
    case state::GameEventType::DoorStateChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: DoorStateChanged");
        break;

    // Zone events (D13)
    case state::GameEventType::ZoneChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: ZoneChanged");
        break;
    case state::GameEventType::ZoneLoading:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: ZoneLoading");
        break;
    case state::GameEventType::ZoneLoaded:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: ZoneLoaded");
        break;

    // Chat events (D10)
    case state::GameEventType::ChatMessage:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: ChatMessage");
        break;
    case state::GameEventType::SystemMessage:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: SystemMessage");
        break;

    // Combat events (D10)
    case state::GameEventType::CombatEvent:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: CombatEvent");
        break;
    case state::GameEventType::TargetChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: TargetChanged");
        break;
    case state::GameEventType::DamageEvent:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: DamageEvent");
        break;
    case state::GameEventType::SpellCastStarted:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: SpellCastStarted");
        break;
    case state::GameEventType::SpellCastComplete:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: SpellCastComplete");
        break;

    // Group events (D12)
    case state::GameEventType::GroupChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: GroupChanged");
        break;
    case state::GameEventType::GroupMemberUpdated:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: GroupMemberUpdated");
        break;
    case state::GameEventType::GroupInviteReceived:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: GroupInviteReceived");
        break;

    // Time events (D13)
    case state::GameEventType::TimeOfDayChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: TimeOfDayChanged");
        break;

    // Pet events (D12)
    case state::GameEventType::PetCreated:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: PetCreated");
        break;
    case state::GameEventType::PetRemoved:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: PetRemoved");
        break;
    case state::GameEventType::PetStatsChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: PetStatsChanged");
        break;
    case state::GameEventType::PetButtonStateChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: PetButtonStateChanged");
        break;
    case state::GameEventType::PetWindowOpened:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: PetWindowOpened");
        break;
    case state::GameEventType::PetWindowClosed:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: PetWindowClosed");
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
    case state::GameEventType::InventorySlotChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: InventorySlotChanged");
        break;
    case state::GameEventType::CursorItemChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: CursorItemChanged");
        break;
    case state::GameEventType::EquipmentStatsChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: EquipmentStatsChanged");
        break;
    case state::GameEventType::CurrencyChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: CurrencyChanged");
        break;
    case state::GameEventType::BankCurrencyChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: BankCurrencyChanged");
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
    case state::GameEventType::TradeStarted:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: TradeStarted");
        break;
    case state::GameEventType::TradeItemUpdated:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: TradeItemUpdated");
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
        LOG_TRACE(MOD_GRAPHICS, "Bridge: SpellGemChanged");
        break;
    case state::GameEventType::CastingStateChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: CastingStateChanged");
        break;
    case state::GameEventType::SpellMemorizing:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: SpellMemorizing");
        break;
    case state::GameEventType::BuffUpdated:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: BuffUpdated");
        break;
    case state::GameEventType::BuffRemoved:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: BuffRemoved");
        break;
    case state::GameEventType::VisionChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: VisionChanged");
        break;

    // Skill events (D12)
    case state::GameEventType::SkillValueChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: SkillValueChanged");
        break;
    case state::GameEventType::SkillsRefreshed:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: SkillsRefreshed");
        break;

    // World/environment events (D13)
    case state::GameEventType::WeatherChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: WeatherChanged");
        break;
    case state::GameEventType::SwimmingStateChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: SwimmingStateChanged");
        break;

    // Zone lifecycle events (D13)
    case state::GameEventType::CollisionMapChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: CollisionMapChanged");
        break;
    case state::GameEventType::ZoneLineBoundingBoxes:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: ZoneLineBoundingBoxes");
        break;

    // UI/misc events (D13)
    case state::GameEventType::ExpProgressChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: ExpProgressChanged");
        break;
    case state::GameEventType::CharacterInfoChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: CharacterInfoChanged");
        break;
    case state::GameEventType::WorldObjectSpawned:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: WorldObjectSpawned");
        break;
    case state::GameEventType::NoteWindowOpened:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: NoteWindowOpened");
        break;
    }
}

} // namespace bridge
} // namespace eqt
