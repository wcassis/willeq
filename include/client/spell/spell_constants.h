/*
 * WillEQ Spell Constants
 *
 * Constants, enums, and type definitions for the spell system.
 * Covers Classic through Velious expansions.
 */

#ifndef EQT_SPELL_CONSTANTS_H
#define EQT_SPELL_CONSTANTS_H

#include <cstdint>

namespace EQ {

// ============================================================================
// Spell System Limits
// ============================================================================

// Maximum spell gems (Classic/Velious = 8, Titanium client supports 9)
constexpr uint8_t MAX_SPELL_GEMS = 8;

// Maximum spells in spellbook
constexpr uint16_t MAX_SPELLBOOK_SLOTS = 400;

// Maximum buff slots per entity
constexpr uint8_t MAX_BUFF_SLOTS = 25;

// Maximum effect slots per spell
constexpr uint8_t MAX_SPELL_EFFECTS = 12;

// Maximum reagent slots per spell
constexpr uint8_t MAX_SPELL_REAGENTS = 4;

// Tick duration in milliseconds (server tick = 6 seconds)
constexpr uint32_t TICK_DURATION_MS = 6000;

// Tick duration in seconds (for buff duration calculations)
constexpr uint32_t BUFF_TICK_SECONDS = 6;

// Invalid/unknown spell ID
constexpr uint32_t SPELL_UNKNOWN = 0xFFFFFFFF;

// Number of player classes (for level requirements array)
constexpr uint8_t NUM_CLASSES = 16;

// ============================================================================
// Spell Target Types
// ============================================================================

enum class SpellTargetType : uint8_t {
    TargetOptional          = 0,   // Target optional
    AEClientV1              = 1,   // AE (client v1)
    GroupV1                 = 2,   // Group (v1)
    AECaster                = 3,   // AE caster (PB AE)
    GroupTeleport           = 4,   // Group teleport
    Single                  = 5,   // Single target
    Self                    = 6,   // Self only
    AETarget                = 8,   // AE at target location
    Animal                  = 9,   // Animal only
    Undead                  = 10,  // Undead only
    Summoned                = 11,  // Summoned only
    InstrumentBard          = 13,  // Bard instrument targets
    Pet                     = 14,  // Pet only
    Corpse                  = 15,  // Corpse only
    Plant                   = 16,  // Plant only
    Giant                   = 17,  // Giant only
    Dragon                  = 18,  // Dragon only
    TargetAETap             = 20,  // Targeted AE tap
    UndeadAE                = 24,  // AE undead
    SummonedAE              = 25,  // AE summoned
    SummonPC                = 32,  // Summon player
    GroupV2                 = 33,  // Group (v2) - used by most group spells
    GroupNoPets             = 34,  // Group v2 no pets
    AEBard                  = 35,  // Bard AE
    SingleInGroup           = 36,  // Single target must be in group
    GroupedClients          = 37,  // Grouped clients
    DirectionalAE           = 38,  // Directional AE cone
    GroupClientsPets        = 39,  // Group clients and pets
    BeanTarget              = 40,  // Bean target (alchemy)
    NoBuffSelf              = 41,  // No buff to self
    TargetAENoNPCs          = 44,  // Target AE no NPCs
    TargetRing              = 45,  // Target ring (ground target)
    FreeTarget              = 46,  // Free target
    SingleFriendly          = 47,  // Single friendly
    TargetAEUndeadPC        = 50,  // Target AE undead player
};

// ============================================================================
// Spell Schools
// ============================================================================

enum class SpellSchool : uint8_t {
    Abjuration   = 0,   // Protective spells, shields, wards
    Alteration   = 1,   // Heals, buffs, debuffs, crowd control
    Conjuration  = 2,   // Summoning, DoTs, some nukes
    Divination   = 3,   // Invisibility, vision, movement spells
    Evocation    = 4,   // Direct damage (nukes)
};

// ============================================================================
// Resist Types
// ============================================================================

enum class ResistType : uint8_t {
    None         = 0,   // Unresistable / no resist check
    Magic        = 1,   // Magic resist
    Fire         = 2,   // Fire resist
    Cold         = 3,   // Cold resist
    Poison       = 4,   // Poison resist
    Disease      = 5,   // Disease resist
    Chromatic    = 6,   // Lowest of all resists
    Prismatic    = 7,   // Average of all resists
    Physical     = 8,   // Physical resist
    Corruption   = 9,   // Corruption resist (post-Velious)
};

// ============================================================================
// Spell Effect Types (SE_ values)
// Classic through Velious subset
// ============================================================================

enum class SpellEffect : int16_t {
    // Core effects (0-50)
    CurrentHP               = 0,    // Damage/Heal - ticks if in buff
    ArmorClass              = 1,    // AC modification
    ATK                     = 2,    // Attack modification
    MovementSpeed           = 3,    // SoW, SoC, snare
    STR                     = 4,    // Strength
    DEX                     = 5,    // Dexterity
    AGI                     = 6,    // Agility
    STA                     = 7,    // Stamina
    INT                     = 8,    // Intelligence
    WIS                     = 9,    // Wisdom
    CHA                     = 10,   // Charisma
    AttackSpeed             = 11,   // Haste/Slow
    Invisibility            = 12,   // Normal invisibility
    SeeInvis                = 13,   // See invisible
    WaterBreathing          = 14,   // Enduring breath
    CurrentMana             = 15,   // Mana drain/restore
    NPCFrenzy               = 17,   // NPC frenzy (aggro)
    Lull                    = 18,   // Reaction radius reduction
    AddFaction              = 19,   // Faction modification
    Blind                   = 20,   // Blindness
    Stun                    = 21,   // Stun effect
    Charm                   = 22,   // Charm/Dominate
    Fear                    = 23,   // Fear
    Fatigue                 = 24,   // Stamina drain
    BindAffinity            = 25,   // Set bind point
    Gate                    = 26,   // Gate to bind
    CancelMagic             = 27,   // Dispel magic
    InvisVsUndead           = 28,   // Invis vs undead
    InvisVsAnimals          = 29,   // Invis vs animals
    ChangeFrenzyRadius      = 30,   // Frenzy radius mod
    Mez                     = 31,   // Mesmerize
    SummonItem              = 32,   // Summon item
    SummonPet               = 33,   // Summon pet
    Confuse                 = 34,   // Confusion
    DiseaseCounter          = 35,   // Disease counter
    PoisonCounter           = 36,   // Poison counter
    DetectHostile           = 37,   // Detect hostile
    DetectMagic             = 38,   // Detect magic
    DetectPoison            = 39,   // Detect poison
    DivineAura              = 40,   // Invulnerability
    Destroy                 = 41,   // Destroy target
    ShadowStep              = 42,   // Shadow step
    Berserk                 = 43,   // Berserk state
    Lycanthropy             = 44,   // Lycanthropy
    Vampirism               = 45,   // Vampirism
    ResistFire              = 46,   // Fire resist buff
    ResistCold              = 47,   // Cold resist buff
    ResistPoison            = 48,   // Poison resist buff
    ResistDisease           = 49,   // Disease resist buff
    ResistMagic             = 50,   // Magic resist buff

    // Effects 51-100
    DetectAnim              = 51,   // Detect animal
    DetectSummoned          = 52,   // Detect summoned
    DetectUndead            = 53,   // Detect undead
    DetectAll               = 54,   // Sense all
    Rune                    = 55,   // Damage absorption
    TrueNorth               = 56,   // True north (compass)
    Levitate                = 57,   // Levitation
    Illusion                = 58,   // Illusion/disguise
    DamageShield            = 59,   // Damage shield (thorns)
    TransferItem            = 60,   // Transfer item
    Identify                = 61,   // Identify item
    ItemID                  = 62,   // Item identification
    WipeHateList            = 63,   // Memory blur
    SpinTarget              = 64,   // Spin stun
    InfraVision             = 65,   // Infravision
    UltraVision             = 66,   // Ultravision
    EyeOfZomm               = 67,   // Eye of Zomm
    ReclaimPet              = 68,   // Reclaim energy
    TotalHP                 = 69,   // Max HP modification
    CorpseBomb              = 70,   // Corpse bomb
    NecPet                  = 71,   // Necro pet summon
    PreserveCorpse          = 72,   // Preserve corpse
    BindSight               = 73,   // Bind sight
    FeignDeath              = 74,   // Feign death
    VoiceGraft              = 75,   // Voice graft
    Sentinel                = 76,   // Sentinel
    LocateCorpse            = 77,   // Locate corpse
    AbsorbMagicAtt          = 78,   // Absorb magic attack
    CurrentHPOnce           = 79,   // Non-repeating damage/heal
    EnchantLight            = 80,   // Enchant light
    Revive                  = 81,   // Resurrection
    SummonPC                = 82,   // Summon player (COH)
    Teleport                = 83,   // Teleport/Translocate
    TossUp                  = 84,   // Toss up
    AddWeaponProc           = 85,   // Weapon proc buff
    Harmony                 = 86,   // Harmony (pacify)
    MagnifyVision           = 87,   // Magnify vision
    Succor                  = 88,   // Succor/Evac
    ModelSize               = 89,   // Size change
    Cloak                   = 90,   // Cloak (hide)
    SummonCorpse            = 91,   // Summon corpse
    Calm                    = 92,   // Calm
    StopRain                = 93,   // Stop rain
    NegateIfCombat          = 94,   // Negate if in combat
    Sacrifice               = 95,   // Sacrifice
    Silence                 = 96,   // Silence
    ManaPool                = 97,   // Max mana modification
    AttackSpeed2            = 98,   // Haste v2
    Root                    = 99,   // Root
    HealOverTime            = 100,  // Heal over time

    // Effects 101-150
    CompleteHeal            = 101,  // Complete heal
    Fearless                = 102,  // Fear immunity
    CallPet                 = 103,  // Call pet
    Translocate             = 104,  // Translocate
    AntiGate                = 105,  // Prevent gate
    SummonBSTPet            = 106,  // BST pet summon
    AlterNPCLevel           = 107,  // Alter NPC level
    Familiar                = 108,  // Summon familiar
    SummonItemIntoBag       = 109,  // Summon into bag
    IncreaseArcheryDamage   = 110,  // Archery damage
    ResistAll               = 111,  // All resist buff
    CastingLevel            = 112,  // Casting level mod
    SummonHorse             = 113,  // Summon mount
    ChangeAggro             = 114,  // Aggro modification
    Hunger                  = 115,  // Hunger/thirst
    CurseCounter            = 116,  // Curse counter
    MagicWeapon             = 117,  // Magic weapon
    Amplification           = 118,  // Spell amplification
    AttackSpeed3            = 119,  // Haste v3
    HealRate                = 120,  // Heal rate mod
    ReverseDS               = 121,  // Reverse damage shield
    ReduceSkill             = 122,  // Skill reduction
    Screech                 = 123,  // Screech
    ImprovedDamage          = 124,  // Focus: improved damage
    ImprovedHeal            = 125,  // Focus: improved heal
    SpellResistReduction    = 126,  // Resist reduction
    IncreaseSpellHaste      = 127,  // Casting speed
    IncreaseSpellDuration   = 128,  // Duration increase
    IncreaseRange           = 129,  // Range increase
    SpellHateMod            = 130,  // Hate modification
    ReduceReagentCost       = 131,  // Reagent conservation
    ReduceManaCost          = 132,  // Mana conservation
    FcStunTimeMod           = 133,  // Stun time mod
    LimitMaxLevel           = 134,  // Focus: max level limit
    LimitResist             = 135,  // Focus: resist type limit
    LimitTarget             = 136,  // Focus: target limit
    LimitEffect             = 137,  // Focus: effect limit
    LimitSpellType          = 138,  // Focus: spell type limit
    LimitSpell              = 139,  // Focus: specific spell limit
    LimitMinDur             = 140,  // Focus: min duration limit
    LimitInstant            = 141,  // Focus: instant limit
    LimitMinLevel           = 142,  // Focus: min level limit
    LimitCastTimeMin        = 143,  // Focus: cast time min limit
    Teleport2               = 144,  // Teleport v2
    ElectricityResist       = 145,  // Electricity resist
    PercentalHeal           = 147,  // Percentage heal
    StackingCommandBlock    = 148,  // Stacking: block
    StackingCommandOverwrite = 149, // Stacking: overwrite
    DeathSave               = 150,  // Death save

    // Effects 151-200
    SuspendPet              = 151,  // Suspend pet
    TemporaryPets           = 152,  // Temporary pets
    BalanceHP               = 153,  // Balance group HP
    DispelDetrimental       = 154,  // Dispel detrimental
    SpellCritDmgIncrease    = 155,  // Spell crit damage
    Reflect                 = 156,  // Spell reflect
    SpellDamageShield       = 157,  // Spell damage shield
    Screech2                = 158,  // Screech v2
    AllStats                = 159,  // All stats buff
    MakeDrunk               = 160,  // Intoxication
    MitigateSpellDamage     = 161,  // Spell mitigation
    MitigateMeleeDamage     = 162,  // Melee mitigation
    NegateAttacks           = 163,  // Negate attacks
    AppraiseLock            = 164,  // Appraise lock
    AssassinateLevel        = 165,  // Assassinate level
    FinishingBlow           = 166,  // Finishing blow
    DistanceRemoval         = 167,  // Remove by distance
    ImprovedReclaimEnergy   = 168,  // Better reclaim
    LimitCastTimeMax        = 169,  // Focus: cast time max
    SpellCritChance         = 170,  // Spell crit chance

    // Unused/invalid effect
    UnusedEffect            = 254,  // Placeholder
    InvalidEffect           = -1,   // Invalid/no effect
};

// ============================================================================
// Gem State
// ============================================================================

enum class GemState : uint8_t {
    Empty               = 0,   // No spell memorized
    Ready               = 1,   // Spell ready to cast
    Casting             = 2,   // Currently casting this spell
    Refresh             = 3,   // On cooldown after cast
    MemorizeProgress    = 4,   // Being memorized
};

// ============================================================================
// Cast Result Codes
// ============================================================================

enum class CastResult : uint8_t {
    Success             = 0,   // Spell cast successfully
    Fizzle              = 1,   // Spell fizzled
    Interrupted         = 2,   // Cast was interrupted
    NotEnoughMana       = 3,   // Insufficient mana
    OutOfRange          = 4,   // Target out of range
    InvalidTarget       = 5,   // Invalid target type
    ComponentMissing    = 6,   // Missing reagent
    NoLineOfSight       = 7,   // No LOS to target
    Stunned             = 8,   // Can't cast while stunned
    Feared              = 9,   // Can't cast while feared
    Silenced            = 10,  // Can't cast while silenced
    NotMemorized        = 11,  // Spell not in gem
    GemCooldown         = 12,  // Gem on cooldown
    AlreadyCasting      = 13,  // Already casting
    SpellNotReady       = 14,  // Spell not ready
    InvalidSpell        = 15,  // Spell doesn't exist
};

// ============================================================================
// Buff Effect Type (from SpellBuff_Struct)
// ============================================================================

enum class BuffEffectType : uint8_t {
    None        = 0,   // No buff in slot
    Buff        = 2,   // Normal buff
    Inverse     = 4,   // Debuff/detrimental
};

// ============================================================================
// Duration Formulas
// ============================================================================

enum class DurationFormula : uint8_t {
    None                = 0,   // No duration (instant)
    LevelDiv2           = 1,   // level / 2
    LevelDiv2Plus5      = 2,   // (level / 2) + 5 ticks (3 min base)
    Level30             = 3,   // level * 30 ticks (3 hours at 60)
    Fixed50             = 4,   // 50 ticks (5 minutes)
    Fixed2              = 5,   // 2 ticks (12 seconds)
    LevelDiv2Plus2      = 6,   // (level / 2) + 2
    Fixed6              = 7,   // 6 ticks (36 seconds)
    LevelPlus10         = 8,   // level + 10 ticks
    Level2Plus10        = 9,   // (level * 2) + 10
    Level3Plus10        = 10,  // (level * 3) + 10
    Level3Plus30        = 11,  // (level + 3) * 30
    LevelDiv4           = 12,  // level / 4
    Fixed1              = 13,  // 1 tick (6 seconds)
    LevelDiv3Plus5      = 14,  // (level / 3) + 5
    Fixed0              = 15,  // 0 ticks / permanent
    Permanent           = 50,  // Permanent (songs/auras)
    MaxDuration         = 51,  // Use buff duration cap
};

// ============================================================================
// Player Classes (for level requirements)
// ============================================================================

enum class PlayerClass : uint8_t {
    Unknown     = 0,
    Warrior     = 1,
    Cleric      = 2,
    Paladin     = 3,
    Ranger      = 4,
    ShadowKnight = 5,
    Druid       = 6,
    Monk        = 7,
    Bard        = 8,
    Rogue       = 9,
    Shaman      = 10,
    Necromancer = 11,
    Wizard      = 12,
    Magician    = 13,
    Enchanter   = 14,
    Beastlord   = 15,  // Post-Velious
    Berserker   = 16,  // Post-Velious
};

// ============================================================================
// Skill Types (for casting skill)
// ============================================================================

enum class CastingSkill : uint8_t {
    OneHandBlunt    = 0,
    OneHandSlash    = 1,
    TwoHandBlunt    = 2,
    TwoHandSlash    = 3,
    Abjuration      = 4,
    Alteration      = 5,
    ApplyPoison     = 6,
    Archery         = 7,
    Backstab        = 8,
    BindWound       = 9,
    Bash            = 10,
    Block           = 11,
    BrassInst       = 12,
    Channeling      = 13,
    Conjuration     = 14,
    Defense         = 15,
    Disarm          = 16,
    DisarmTraps     = 17,
    Divination      = 18,
    Dodge           = 19,
    DoubleAttack    = 20,
    DragonPunch     = 21,
    DualWield       = 22,
    EagleStrike     = 23,
    Evocation       = 24,
    FeignDeath      = 25,
    FlyingKick      = 26,
    Forage          = 27,
    HandToHand      = 28,
    Hide            = 29,
    Kick            = 30,
    Meditate        = 31,
    Mend            = 32,
    Offense         = 33,
    Parry           = 34,
    PickLock        = 35,
    Piercing        = 36,
    Riposte         = 37,
    RoundKick       = 38,
    SafeFall        = 39,
    SenseHeading    = 40,
    Singing         = 41,
    Sneak           = 42,
    SpecializeAbjure = 43,
    SpecializeAlteration = 44,
    SpecializeConjuration = 45,
    SpecializeDivination = 46,
    SpecializeEvocation = 47,
    PickPocket      = 48,
    StringedInst    = 49,
    Swimming        = 50,
    Throwing        = 51,
    TigerClaw       = 52,
    Tracking        = 53,
    WindInst        = 54,
    Fishing         = 55,
    MakePoison      = 56,
    Tinkering       = 57,
    Research        = 58,
    Alchemy         = 59,
    Baking          = 60,
    Tailoring       = 61,
    SenseTraps      = 62,
    Blacksmithing   = 63,
    Fletching       = 64,
    Brewing         = 65,
    AlcoholTolerance = 66,
    Begging         = 67,
    JewelryMaking   = 68,
    Pottery         = 69,
    PercussionInst  = 70,
    Intimidation    = 71,
    Berserking      = 72,
    Taunt           = 73,
    SingingSkill    = 74,  // Bard specific
};

// ============================================================================
// Field Indices for spells_us.txt parsing
// ============================================================================

namespace SpellField {
    constexpr int ID                = 0;
    constexpr int Name              = 1;
    constexpr int PlayerTag         = 2;
    constexpr int TeleportZone      = 3;
    constexpr int CastOnYou         = 4;
    constexpr int CastOnOther       = 5;
    constexpr int CastOnYouFail     = 6;
    constexpr int SpellFades        = 7;
    constexpr int AuraText          = 8;
    constexpr int Range             = 9;
    constexpr int AoERange          = 10;
    constexpr int PushBack          = 11;
    constexpr int PushUp            = 12;
    constexpr int CastTime          = 13;
    constexpr int RecoveryTime      = 14;
    constexpr int RecastTime        = 15;
    constexpr int DurationFormula   = 16;
    constexpr int DurationCap       = 17;
    constexpr int AoEDuration       = 18;
    constexpr int ManaCost          = 19;

    // Effect base values (12 slots): fields 20-31
    constexpr int BaseValue1        = 20;
    // Effect base2 values (12 slots): fields 32-43
    constexpr int Base2Value1       = 32;
    // Effect max values (12 slots): fields 44-55
    constexpr int MaxValue1         = 44;

    // Reagent info
    constexpr int ReagentId1        = 58;
    constexpr int ReagentCount1     = 62;
    constexpr int NoExpendReagent1  = 66;

    // Effect formulas (12 slots): fields 70-81
    constexpr int Formula1          = 70;

    constexpr int GoodEffect        = 83;
    constexpr int Activated         = 84;
    constexpr int ResistType        = 85;

    // Effect IDs (12 slots): fields 86-97
    constexpr int EffectId1         = 86;

    constexpr int TargetType        = 98;
    constexpr int BaseDifficulty    = 99;
    constexpr int CastingSkill      = 100;
    constexpr int ZoneType          = 101;
    constexpr int EnvironmentType   = 102;
    constexpr int TimeOfDay         = 103;

    // Class minimum levels (16 classes): fields 104-119
    constexpr int ClassLevel1       = 104;

    constexpr int CastAnim          = 120;
    constexpr int TargetAnim        = 121;
    constexpr int TravelType        = 122;
    constexpr int SpellAffectIndex  = 123;

    constexpr int DisallowSit       = 127;

    // Deity restrictions: fields 142-158 (17 deities)
    constexpr int Deity1            = 142;

    constexpr int NewIcon           = 144;
    constexpr int SpellIcon         = 143;
    constexpr int NoBuffBlock       = 161;
    constexpr int Dispellable       = 175;

    constexpr int SpellGroup        = 207;
    constexpr int SpellRank         = 208;

    // Total expected fields (approximate)
    constexpr int FieldCount        = 217;
}

} // namespace EQ

#endif // EQT_SPELL_CONSTANTS_H
