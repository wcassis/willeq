#include "client/bridge/irrlicht_bridge.h"
#include "common/logging.h"

namespace eqt {
namespace bridge {

void IrrlichtBridge::applyEvent(const state::GameEvent& event) {
    // Stub implementation — log event type at TRACE level.
    // Actual renderer calls will be wired in Phase 3 (D09-D13).

    switch (event.type) {
    // Player events
    case state::GameEventType::PlayerMoved:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: PlayerMoved");
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

    // Entity events
    case state::GameEventType::EntitySpawned:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: EntitySpawned");
        break;
    case state::GameEventType::EntityDespawned:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: EntityDespawned");
        break;
    case state::GameEventType::EntityMoved:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: EntityMoved");
        break;
    case state::GameEventType::EntityStatsChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: EntityStatsChanged");
        break;
    case state::GameEventType::EntityAppearanceChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: EntityAppearanceChanged");
        break;
    case state::GameEventType::EntityLightChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: EntityLightChanged");
        break;
    case state::GameEventType::EntityAnimationEvent:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: EntityAnimationEvent");
        break;
    case state::GameEventType::EntityPoseStateChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: EntityPoseStateChanged");
        break;
    case state::GameEventType::EntityDeathAnimation:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: EntityDeathAnimation");
        break;
    case state::GameEventType::CorpseDecayStarted:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: CorpseDecayStarted");
        break;
    case state::GameEventType::CombatAnimation:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: CombatAnimation");
        break;

    // Door events
    case state::GameEventType::DoorSpawned:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: DoorSpawned");
        break;
    case state::GameEventType::DoorStateChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: DoorStateChanged");
        break;

    // Zone events
    case state::GameEventType::ZoneChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: ZoneChanged");
        break;
    case state::GameEventType::ZoneLoading:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: ZoneLoading");
        break;
    case state::GameEventType::ZoneLoaded:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: ZoneLoaded");
        break;

    // Chat events
    case state::GameEventType::ChatMessage:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: ChatMessage");
        break;
    case state::GameEventType::SystemMessage:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: SystemMessage");
        break;

    // Combat events
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

    // Group events
    case state::GameEventType::GroupChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: GroupChanged");
        break;
    case state::GameEventType::GroupMemberUpdated:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: GroupMemberUpdated");
        break;
    case state::GameEventType::GroupInviteReceived:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: GroupInviteReceived");
        break;

    // Time events
    case state::GameEventType::TimeOfDayChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: TimeOfDayChanged");
        break;

    // Pet events
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

    // Window events
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

    // Inventory events
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

    // Loot events
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

    // Trade events
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

    // Spell events
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

    // Skill events
    case state::GameEventType::SkillValueChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: SkillValueChanged");
        break;
    case state::GameEventType::SkillsRefreshed:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: SkillsRefreshed");
        break;

    // World/environment events
    case state::GameEventType::WeatherChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: WeatherChanged");
        break;
    case state::GameEventType::SwimmingStateChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: SwimmingStateChanged");
        break;

    // Zone lifecycle events
    case state::GameEventType::CollisionMapChanged:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: CollisionMapChanged");
        break;
    case state::GameEventType::ZoneLineBoundingBoxes:
        LOG_TRACE(MOD_GRAPHICS, "Bridge: ZoneLineBoundingBoxes");
        break;

    // UI/misc events
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
