#pragma once

// GameEventType -- the one enumerator list every event carries.
//
// T451: split out of the 1,269-line include/client/state/event_bus.h.
// Every declaration is a contiguous line range of that file, moved verbatim;
// "client/state/event_bus.h" still includes all of them, so no include site
// changed.

namespace eqt {
namespace state {


// Event types enum
enum class GameEventType {
    // Player events
    PlayerMoved,
    PlayerStatsChanged,
    PlayerPositionStateChanged,
    PlayerMovementModeChanged,

    // Entity events
    EntitySpawned,
    EntityDespawned,
    EntityMoved,
    EntityStatsChanged,
    EntityAppearanceChanged,
    EntitySizeChanged,
    EntityLightChanged,
    EntityAnimationEvent,
    EntityWeaponSkillsChanged,
    EntityPoseStateChanged,
    EntityDeathAnimation,
    CorpseDecayStarted,
    CombatAnimation,
    CombatSkillAnimation,
    ReceivedDamageAnimation,
    PlayerSpawnIdSet,
    NetworkReady,

    // Door events
    DoorSpawned,
    DoorStateChanged,

    // Zone events
    ZoneChanged,
    ZoneLoading,
    ZoneLoaded,

    // Chat events
    ChatMessage,
    SystemMessage,

    // Combat events
    TargetChanged,
    DamageEvent,
    SpellCastStarted,
    SpellCastComplete,

    // Group events
    GroupChanged,
    GroupMemberUpdated,
    GroupInviteReceived,  // Uses GroupInviteReceivedData (not GroupChangedData)

    // Time events
    TimeOfDayChanged,

    // Pet events
    PetCreated,
    PetRemoved,
    PetStatsChanged,
    PetButtonStateChanged,

    // Window events (vendor, bank, trainer, tradeskill)
    VendorWindowOpened,
    VendorWindowClosed,
    VendorItemAdded,
    BankWindowOpened,
    BankWindowClosed,
    TrainerWindowOpened,
    TrainerWindowClosed,
    TradeskillContainerOpened,
    TradeskillContainerClosed,
    TradeskillRecipeListCleared,
    TradeskillRecipeReceived,

    // Inventory events
    InventorySlotChanged,
    ItemDisplayShown,
    CursorItemChanged,
    EquipmentStatsChanged,
    CurrencyChanged,
    BankCurrencyChanged,

    // Loot events
    LootWindowOpened,
    LootWindowClosed,
    LootItemAdded,
    LootItemRemoved,

    // Trade events
    TradeRequestReceived,
    TradeStarted,
    TradeItemUpdated,
    TradeMoneyUpdated,
    TradeAcceptStateChanged,
    TradeCancelled,
    TradeCompleted,

    // Spell events
    SpellGemChanged,
    CastingStateChanged,
    SpellMemorizing,
    BuffUpdated,
    BuffRemoved,
    VisionChanged,

    // Skill events
    SkillValueChanged,
    SkillsRefreshed,
    SkillsSnapshot,  // U06h: full skills list sent once at zone-in

    // World/environment events
    WeatherChanged,
    SwimmingStateChanged,

    // Zone lifecycle events
    CollisionMapChanged,
    ZoneLineBoundingBoxes,

    // UI/misc events
    ExpProgressChanged,
    CharacterInfoChanged,
    WorldObjectSpawned,
    NoteWindowOpened,
    SpellScribeCompleted,
    SpellbookSnapshot,  // U06g: full spellbook sent once at zone-in
    HotbarCooldownStarted,
    HotbarSlotAssigned,  // U00: hotbar slot content changed (client-side config)
    SkillActivationFeedback,
    RendererCommand,  // D20d: slash command forwarded to renderer

    // D20e1: Spell visual events (replaces direct SpellManager → Renderer calls)
    SpellCastVisualStarted,
    SpellCastVisualComplete,
    SpellCastVisualInterrupted,
    SpellMemorizeVisualStarted,
    SpellMemorizeVisualComplete,

    // D20e1: Zone lifecycle events (replaces direct EverQuest → Renderer calls)
    ZoneUnloading,

    // D20e1: Navmesh changed (replaces direct m_renderer->setNavmesh calls)
    NavmeshChanged,

    // D20e1: Diagnostics (replaces direct m_renderer->getMemoryReport/dumpScene calls)
    DiagnosticsMemoryReport,
    DiagnosticsSceneDump,

    // T439 (board B432): audio. The APPLIED music/effects volume, on the
    // oracle's own 0..20 step, published by whichever of the four sites
    // moved it -- the config apply, the two volume intents, or
    // /musicvolume and /effectsvolume. The Options window's two sliders
    // read it back, so a slash command and a drag can no longer disagree.
    VolumesChanged,

    // B430: a server-owned Options-window setting (anonymous/roleplay,
    // autosplit, the three auto-consents) as the game thread now holds it.
    GameOptionChanged,
    // B895 (GitLab #211): the Tracking Window's list and settings, published
    // whole by eqt::tracking::TrackingController on an OP_Track reply and on
    // every filter/sort/players/target change.
    TrackingListChanged,
    // B897 (GitLab #213): the friends and ignore lists changed (or were
    // loaded for a new character). Whole lists, SocialListsChangedData.
    SocialListsChanged,
};


} // namespace state
} // namespace eqt
