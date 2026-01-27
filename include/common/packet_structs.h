/*
 * WillEQ Packet Structures
 *
 * These structures define the Titanium client packet formats.
 * All structures are packed (1-byte aligned) to match network protocol.
 *
 * Based on EQEmu titanium_structs.h but standalone for the headless client.
 */

#ifndef EQT_PACKET_STRUCTS_H
#define EQT_PACKET_STRUCTS_H

#include <cstdint>
#include <cstring>

namespace EQT {

// Constants
static const uint32_t BUFF_COUNT = 25;
static const uint32_t MAX_PP_SKILL = 100;
static const uint32_t MAX_PP_LANGUAGE = 28;
static const uint32_t MAX_PP_AA_ARRAY = 240;
static const uint32_t MAX_PP_DISCIPLINES = 100;
static const uint32_t MAX_PLAYER_TRIBUTES = 5;
static const uint32_t MAX_RECAST_TYPES = 20;
static const uint32_t SPELLBOOK_SIZE = 400;
static const uint32_t SPELL_GEM_COUNT = 9;
static const uint32_t BANDOLIERS_SIZE = 4;
static const uint32_t BANDOLIER_ITEM_COUNT = 4;
static const uint32_t POTION_BELT_SIZE = 4;
static const uint32_t TEXTURE_COUNT = 9;  // materialCount

// Compile-time struct packing
#pragma pack(push, 1)

/*
 * Color/Tint structure
 * Size: 4 bytes
 */
struct Tint_Struct {
    union {
        struct {
            uint8_t Blue;
            uint8_t Green;
            uint8_t Red;
            uint8_t UseTint;  // 0xFF if tinted
        };
        uint32_t Color;
    };
};

/*
 * Equipment tint profile
 * Size: 36 bytes (9 slots * 4 bytes)
 */
struct TintProfile {
    union {
        struct {
            Tint_Struct Head;
            Tint_Struct Chest;
            Tint_Struct Arms;
            Tint_Struct Wrist;
            Tint_Struct Hands;
            Tint_Struct Legs;
            Tint_Struct Feet;
            Tint_Struct Primary;
            Tint_Struct Secondary;
        };
        Tint_Struct Slot[TEXTURE_COUNT];
    };
};

/*
 * Texture/Material structure
 * Size: 4 bytes
 */
struct Texture_Struct {
    uint32_t Material;
};

/*
 * Equipment texture profile
 * Size: 36 bytes (9 slots * 4 bytes)
 */
struct TextureProfile {
    union {
        struct {
            Texture_Struct Head;
            Texture_Struct Chest;
            Texture_Struct Arms;
            Texture_Struct Wrist;
            Texture_Struct Hands;
            Texture_Struct Legs;
            Texture_Struct Feet;
            Texture_Struct Primary;
            Texture_Struct Secondary;
        };
        Texture_Struct Slot[TEXTURE_COUNT];
    };
};

/*
 * Login Info structure
 * Size: 464 bytes
 */
struct LoginInfo_Struct {
/*000*/ char    login_info[64];
/*064*/ uint8_t unknown064[124];
/*188*/ uint8_t zoning;           // 01 if zoning, 00 if not
/*189*/ uint8_t unknown189[275];
/*464*/
};

/*
 * Enter World structure
 * Size: 72 bytes
 */
struct EnterWorld_Struct {
/*000*/ char     name[64];
/*064*/ uint32_t tutorial;       // 01 on "Enter Tutorial", 00 if not
/*068*/ uint32_t return_home;    // 01 on "Return Home", 00 if not
};

/*
 * Entity ID structure
 * Size: 4 bytes
 */
struct EntityId_Struct {
/*00*/ uint32_t entity_id;
/*04*/
};

/*
 * Spawn Structure (Titanium)
 * Size: 385 bytes
 * Used in: ZoneSpawns, NewSpawn
 */
struct Spawn_Struct {
/*0000*/ uint8_t  unknown0000;
/*0001*/ uint8_t  gm;              // 0=no, 1=gm
/*0002*/ uint8_t  unknown0003;
/*0003*/ uint8_t  aaitle;          // 0=none, 1=general, 2=archtype, 3=class
/*0004*/ uint8_t  unknown0004;
/*0005*/ uint8_t  anon;            // 0=normal, 1=anon, 2=roleplay
/*0006*/ uint8_t  face;            // Face id for players
/*0007*/ char     name[64];        // Player's Name
/*0071*/ uint16_t deity;           // Player's Deity
/*0073*/ uint16_t unknown0073;
/*0075*/ float    size;            // Model size
/*0079*/ uint32_t unknown0079;
/*0083*/ uint8_t  NPC;             // 0=player,1=npc,2=pc corpse,3=npc corpse
/*0084*/ uint8_t  invis;           // Invis (0=not, 1=invis)
/*0085*/ uint8_t  haircolor;       // Hair color
/*0086*/ uint8_t  curHp;           // Current hp percent
/*0087*/ uint8_t  max_hp;          // Usually 100, 110, or 120
/*0088*/ uint8_t  findable;        // 0=can't be found, 1=can be found
/*0089*/ uint8_t  unknown0089[5];
/*0094*/ // Bitfield containing position data:
         // deltaHeading:10, x:19, padding:3
         // y:19, animation:10, padding:3
         // z:19, deltaY:13
         // deltaX:13, heading:12, padding:7
         // deltaZ:13, padding:19
         uint32_t position_bitfield[5];  // 20 bytes of bitfield position data
/*0114*/ uint8_t  eyecolor1;       // Player's left eye color
/*0115*/ uint8_t  unknown0115[24];
/*0139*/ uint8_t  showhelm;        // 0=no, 1=yes
/*0140*/ uint8_t  unknown0140[4];
/*0144*/ uint8_t  is_npc;          // 0=no, 1=yes
/*0145*/ uint8_t  hairstyle;       // Hair style
/*0146*/ uint8_t  beardcolor;      // Beard color
/*0147*/ uint8_t  unknown0147[4];
/*0151*/ uint8_t  level;           // Spawn Level
/*0152*/ uint32_t PlayerState;     // Animation state flags
/*0156*/ uint8_t  beard;           // Beard style
/*0157*/ char     suffix[32];      // Player's suffix
/*0189*/ uint32_t petOwnerId;      // Pet owner spawn id
/*0193*/ uint8_t  guildrank;       // 0=normal, 1=officer, 2=leader
/*0194*/ uint8_t  unknown0194[3];
/*0197*/ TextureProfile equipment; // 36 bytes
/*0233*/ float    runspeed;        // Speed when running
/*0237*/ uint8_t  afk;             // 0=no, 1=afk
/*0238*/ uint32_t guildID;         // Current guild
/*0242*/ char     title[32];       // Title
/*0274*/ uint8_t  unknown0274;
/*0275*/ uint8_t  helm;            // Helm texture
/*0276*/ uint8_t  set_to_0xFF[8];  // Placeholder (all ff)
/*0284*/ uint32_t race;            // Spawn race
/*0288*/ uint32_t unknown0288;
/*0292*/ char     lastName[32];    // Player's Lastname
/*0324*/ float    walkspeed;       // Speed when walking
/*0328*/ uint8_t  unknown0328;
/*0329*/ uint8_t  is_pet;          // 0=no, 1=yes
/*0330*/ uint8_t  light;           // Spawn's lightsource
/*0331*/ uint8_t  class_;          // Player's class
/*0332*/ uint8_t  eyecolor2;       // Right eye color
/*0333*/ uint8_t  flymode;
/*0334*/ uint8_t  gender;          // Gender (0=male, 1=female)
/*0335*/ uint8_t  bodytype;        // Bodytype
/*0336*/ uint8_t  unknown0336[3];
/*0339*/ uint8_t  equip_chest2;    // Second chest texture / mount color
/*0340*/ uint32_t spawnId;         // Spawn Id
/*0344*/ float    bounding_radius; // Used in melee range calculation
/*0348*/ TintProfile equipment_tint; // 36 bytes
/*0384*/ uint8_t  lfg;             // 0=off, 1=lfg on
/*0385*/
};
static_assert(sizeof(Spawn_Struct) == 385, "Spawn_Struct must be 385 bytes");

/*
 * New Spawn wrapper
 */
struct NewSpawn_Struct {
    Spawn_Struct spawn;
};

/*
 * Client Zone Entry
 * Size: 68 bytes
 */
struct ClientZoneEntry_Struct {
/*0000*/ uint32_t unknown00;
/*0004*/ char     char_name[64];
};

/*
 * Server Zone Entry
 */
struct ServerZoneEntry_Struct {
    NewSpawn_Struct player;
};

/*
 * New Zone structure
 * Size: 700 bytes
 */
struct NewZone_Struct {
/*0000*/ char     char_name[64];
/*0064*/ char     zone_short_name[32];
/*0096*/ char     zone_long_name[278];
/*0374*/ uint8_t  ztype;
/*0375*/ uint8_t  fog_red[4];
/*0379*/ uint8_t  fog_green[4];
/*0383*/ uint8_t  fog_blue[4];
/*0387*/ uint8_t  unknown323;
/*0388*/ float    fog_minclip[4];
/*0404*/ float    fog_maxclip[4];
/*0420*/ float    gravity;
/*0424*/ uint8_t  time_type;
/*0425*/ uint8_t  rain_chance[4];
/*0429*/ uint8_t  rain_duration[4];
/*0433*/ uint8_t  snow_chance[4];
/*0437*/ uint8_t  snow_duration[4];
/*0441*/ uint8_t  unknown360[33];
/*0474*/ uint8_t  sky;
/*0475*/ uint8_t  unknown331[13];
/*0488*/ float    zone_exp_multiplier;
/*0492*/ float    safe_y;
/*0496*/ float    safe_x;
/*0500*/ float    safe_z;
/*0504*/ float    max_z;
/*0508*/ float    underworld;
/*0512*/ float    minclip;
/*0516*/ float    maxclip;
/*0520*/ uint8_t  unknown_end[84];
/*0604*/ char     zone_short_name2[68];
/*0672*/ char     unknown672[12];
/*0684*/ uint16_t zone_id;
/*0686*/ uint16_t zone_instance;
/*0688*/ uint32_t unknown688;
/*0692*/ uint8_t  unknown692[8];
/*0700*/
};

/*
 * Spawn Appearance structure
 * Size: 8 bytes
 */
struct SpawnAppearance_Struct {
/*0000*/ uint16_t spawn_id;
/*0002*/ uint16_t type;
/*0004*/ uint32_t parameter;
/*0008*/
};

// Appearance types
enum AppearanceType : uint16_t {
    AT_Die          = 0,
    AT_WhoLevel     = 1,
    AT_MaxHP        = 2,
    AT_Invis        = 3,
    AT_PVP          = 4,
    AT_Light        = 5,
    AT_Anim         = 14,
    AT_Sneak        = 15,
    AT_SpawnID      = 16,
    AT_HP           = 17,
    AT_Linkdead     = 18,
    AT_Levitate     = 19,
    AT_GM           = 20,
    AT_Anon         = 21,
    AT_GuildID      = 22,
    AT_GuildRank    = 23,
    AT_AFK          = 24,
    AT_Pet          = 25,
    AT_Summoned     = 27,
    AT_Split        = 28,
    AT_Size         = 29,
    AT_NPC          = 30,
    AT_NPCName      = 31,
    AT_DamageState  = 44,
    AT_Trader       = 300,
    AT_Buyer        = 301
};

/*
 * Illusion structure
 * Used when illusion/disguise spells are cast
 * Size: 168 bytes
 */
struct Illusion_Struct {
/*0000*/ uint32_t spawnid;
/*0004*/ char     charname[64];
/*0068*/ int32_t  race;
/*0072*/ uint8_t  gender;
/*0073*/ uint8_t  texture;
/*0074*/ uint8_t  helmtexture;
/*0075*/ uint8_t  unknown075;
/*0076*/ uint32_t face;
/*0080*/ uint8_t  hairstyle;
/*0081*/ uint8_t  haircolor;
/*0082*/ uint8_t  beard;
/*0083*/ uint8_t  beardcolor;
/*0084*/ float    size;
/*0088*/ uint8_t  unknown088[80];
/*0168*/
};

/*
 * Spell Buff structure (in profile)
 * Size: 20 bytes
 */
struct SpellBuff_Struct {
/*000*/ uint8_t  effect_type;    // 0=no buff, 2=buff, 4=inverse
/*001*/ uint8_t  level;
/*002*/ uint8_t  bard_modifier;
/*003*/ uint8_t  unknown003;
/*004*/ uint32_t spellid;
/*008*/ int32_t  duration;
/*012*/ uint32_t counters;
/*016*/ uint32_t player_id;      // Caster ID
/*020*/
};

/*
 * Pet Buff Window structure
 * Sent when pet buffs change or on zone-in
 * Size: varies (30 spell slots)
 */
static constexpr int PET_BUFF_COUNT = 30;

struct PetBuff_Struct {
/*000*/ uint32_t petid;                        // Spawn ID of the pet
/*004*/ uint32_t spellid[PET_BUFF_COUNT];      // Array of spell IDs (0xFFFFFFFF = empty)
/*124*/ int32_t  ticsremaining[PET_BUFF_COUNT]; // Remaining ticks for each buff
/*244*/ uint32_t buffcount;                    // Number of active buffs
/*248*/
};

/*
 * Consider structure
 * Size: 24 bytes
 */
struct Consider_Struct {
/*000*/ uint32_t playerid;
/*004*/ uint32_t targetid;
/*008*/ uint32_t faction;
/*012*/ uint32_t level;
/*016*/ int32_t  cur_hp;
/*020*/ int32_t  max_hp;
/*024*/
};

/*
 * Action structure (combat)
 * Size: 44 bytes
 */
struct Action_Struct {
/*00*/ uint32_t target;
/*04*/ uint32_t source;
/*08*/ uint32_t level;
/*12*/ uint32_t instrument_mod;
/*16*/ float    force;
/*20*/ float    hit_heading;
/*24*/ float    hit_pitch;
/*28*/ uint32_t type;
/*32*/ uint32_t spell;
/*36*/ uint8_t  level2;
/*37*/ uint8_t  effect_flag;
/*38*/ uint8_t  padding[2];
/*40*/ uint32_t unknown_action;
/*44*/
};

/*
 * Combat Damage structure
 * Size: 32 bytes
 */
struct CombatDamage_Struct {
/*00*/ uint16_t target;
/*02*/ uint16_t source;
/*04*/ uint8_t  type;
/*05*/ uint8_t  unknown05;
/*06*/ uint16_t spellid;
/*08*/ uint32_t damage;
/*12*/ float    force;
/*16*/ float    meleepush_xy;
/*20*/ float    meleepush_z;
/*24*/ uint32_t unknown24;
/*28*/ uint32_t unknown28;
/*32*/
};

/*
 * Combat Ability structure (kick, bash, taunt, etc.)
 * Size: 12 bytes
 */
struct CombatAbility_Struct {
/*00*/ uint32_t target;      // Target entity ID
/*04*/ uint32_t attack;      // Attack value (usually 0)
/*08*/ uint32_t skill;       // Skill ID (e.g., SKILL_KICK, SKILL_BASH)
/*12*/
};

/*
 * Money on Corpse structure
 * Size: 20 bytes
 */
struct MoneyOnCorpse_Struct {
/*00*/ uint8_t  response;
/*01*/ uint8_t  unknown01[3];
/*04*/ uint32_t platinum;
/*08*/ uint32_t gold;
/*12*/ uint32_t silver;
/*16*/ uint32_t copper;
/*20*/
};

/*
 * Channel Message structure
 * Variable size
 */
struct ChannelMessage_Struct {
/*000*/ char     targetname[64];
/*064*/ char     sender[64];
/*128*/ uint32_t language;
/*132*/ uint32_t chan_num;
/*136*/ uint32_t cm_unknown4;
/*140*/ uint32_t skill_in_language;
/*144*/ char     message[0];
};

/*
 * Death structure
 * Size: 32 bytes
 */
struct Death_Struct {
/*000*/ uint32_t spawn_id;
/*004*/ uint32_t killer_id;
/*008*/ uint32_t corpseid;
/*012*/ uint32_t bindzoneid;
/*016*/ uint32_t spell_id;
/*020*/ uint32_t attack_skill;
/*024*/ int32_t  damage;
/*028*/ uint32_t is_PC;
/*032*/
};

/*
 * HP Update structure
 * Size: 8 bytes
 */
struct HPUpdate_Struct {
/*00*/ uint32_t spawn_id;
/*04*/ int32_t  cur_hp;
/*08*/
};

/*
 * Mob Health structure (percentage)
 * Size: 6 bytes
 */
struct MobHealth_Struct {
/*00*/ uint16_t spawn_id;
/*02*/ uint8_t  hp;
/*03*/ uint8_t  unknown03[3];
/*06*/
};

/*
 * Delete Spawn structure
 * Size: 4 bytes
 */
struct DeleteSpawn_Struct {
/*00*/ uint32_t spawn_id;
/*04*/
};

/*
 * Client Position Update (sent by client)
 * Variable size based on animation field
 */
struct PlayerPositionUpdateClient_Struct {
/*0000*/ uint16_t spawn_id;
/*0002*/ uint16_t sequence;
/*0004*/ // Position bitfield follows (same format as Spawn_Struct)
};

/*
 * Server Position Update
 */
struct PlayerPositionUpdateServer_Struct {
/*0000*/ uint16_t spawn_id;
/*0002*/ int8_t   delta_heading;
/*0003*/ int8_t   padding003;
/*0004*/ int8_t   delta_y;
/*0005*/ int8_t   padding005;
/*0006*/ int8_t   delta_z;
/*0007*/ int8_t   padding007;
/*0008*/ int8_t   delta_x;
/*0009*/ int8_t   padding009;
/*0010*/ int32_t  y;
/*0014*/ int32_t  x;
/*0018*/ int16_t  heading;
/*0020*/ int16_t  padding020;
/*0022*/ int32_t  z;
/*0026*/ uint32_t animation;
/*0030*/
};

/*
 * Bind structure
 * Size: 20 bytes
 */
struct BindStruct {
/*000*/ uint32_t zone_id;
/*004*/ float    x;
/*008*/ float    y;
/*012*/ float    z;
/*016*/ float    heading;
/*020*/
};

/*
 * AA Array entry
 * Size: 8 bytes
 */
struct AA_Array {
    uint32_t AA;
    uint32_t value;
};

/*
 * Disciplines structure
 * Size: 400 bytes
 */
struct Disciplines_Struct {
    uint32_t values[MAX_PP_DISCIPLINES];
};

/*
 * Tribute structure
 * Size: 8 bytes
 */
struct Tribute_Struct {
    uint32_t tribute;
    uint32_t tier;
};

/*
 * Bandolier item
 * Size: 72 bytes
 */
struct BandolierItem_Struct {
    uint32_t ID;
    uint32_t Icon;
    char     Name[64];
};

/*
 * Bandolier set
 * Size: 320 bytes
 */
struct Bandolier_Struct {
    char     Name[32];
    BandolierItem_Struct Items[BANDOLIER_ITEM_COUNT];
};

/*
 * Potion belt item
 * Size: 72 bytes
 */
struct PotionBeltItem_Struct {
    uint32_t ID;
    uint32_t Icon;
    char     Name[64];
};

/*
 * Potion belt
 * Size: 288 bytes
 */
struct PotionBelt_Struct {
    PotionBeltItem_Struct Items[POTION_BELT_SIZE];
};

/*
 * Auto Attack structure
 * Size: 4 bytes
 */
struct Attack_Struct {
/*00*/ uint32_t target_id;
/*04*/
};

/*
 * Target structure (OP_TargetMouse)
 * Size: 4 bytes
 */
struct Target_Struct {
/*00*/ uint32_t target_id;
/*04*/
};

/*
 * Begin Cast structure
 * Size: 8 bytes
 */
struct BeginCast_Struct {
/*000*/ uint16_t caster_id;
/*002*/ uint16_t spell_id;
/*004*/ uint32_t cast_time;  // in milliseconds
/*008*/
};

/*
 * Cast Spell structure
 * Size: 20 bytes
 */
struct CastSpell_Struct {
/*00*/ uint32_t slot;
/*04*/ uint32_t spell_id;
/*08*/ uint32_t inventoryslot;  // 0xFFFF for normal cast
/*12*/ uint32_t target_id;
/*16*/ uint8_t  cs_unknown[4];
/*20*/
};

/*
 * Memorize Spell structure
 * Used for memorizing spells to gem slots and forgetting them
 * Size: 16 bytes
 */
struct MemorizeSpell_Struct {
/*00*/ uint32_t slot;       // Gem slot (0-7)
/*04*/ uint32_t spell_id;   // Spell ID
/*08*/ uint32_t scribing;   // 1=memorize, 2=forget, 3=spellbar
/*12*/ uint32_t unknown0;   // 742 for memorize, 0 for forget
/*16*/
};

/*
 * Mana Change structure
 * Size: 16 bytes
 */
struct ManaChange_Struct {
/*00*/ uint32_t new_mana;
/*04*/ uint32_t stamina;
/*08*/ uint32_t spell_id;
/*12*/ uint8_t  keepcasting;
/*13*/ uint8_t  padding[3];
/*16*/
};

/*
 * Emote structure
 * Variable size
 */
struct Emote_Struct {
/*000*/ uint32_t type;
/*004*/ char     message[0];
};

/*
 * Animation structure
 * Size: 8 bytes
 */
struct Animation_Struct {
/*00*/ uint16_t spawn_id;
/*02*/ uint16_t action;
/*04*/ uint8_t  speed;
/*05*/ uint8_t  padding[3];
/*08*/
};

/*
 * Zone Change structure
 * Size: 88 bytes
 */
struct ZoneChange_Struct {
/*000*/ char     char_name[64];
/*064*/ uint16_t zone_id;
/*066*/ uint16_t instance_id;
/*068*/ float    y;
/*072*/ float    x;
/*076*/ float    z;
/*080*/ uint32_t zone_reason;  // 0x0A == death
/*084*/ int32_t  success;      // 0 = client->server request, 1 = server->client response, -X = error
/*088*/
};

/*
 * Loot Request structure
 * Size: 4 bytes
 */
struct LootRequest_Struct {
/*00*/ uint32_t corpse_id;
/*04*/
};

/*
 * Loot Item structure
 * Size: 12 bytes
 */
struct LootItem_Struct {
/*00*/ uint32_t corpse_id;
/*04*/ uint32_t slot_id;
/*08*/ uint32_t auto_loot;
/*12*/
};

/*
 * Zone Point Entry structure (Titanium)
 * Size: 24 bytes
 * Sent in HC_OP_SendZonepoints packet
 */
struct ZonePoint_Entry {
/*0000*/ uint32_t iterator;      // Zone point number/ID
/*0004*/ float    y;             // Target Y coordinate
/*0008*/ float    x;             // Target X coordinate
/*0012*/ float    z;             // Target Z coordinate
/*0016*/ float    heading;       // Target heading
/*0020*/ uint16_t zoneid;        // Target zone ID
/*0022*/ uint16_t zoneinstance;  // Instance ID (LDoN)
/*0024*/
};

/*
 * Zone Points header structure
 * Variable size: 4 + count * sizeof(ZonePoint_Entry)
 */
struct ZonePoints_Header {
/*0000*/ uint32_t count;
/*0004*/ // Followed by count ZonePoint_Entry structures
};

/*
 * Door structure (Titanium)
 * Size: 80 bytes
 * Used in: SpawnDoor packet (array of doors)
 */
struct Door_Struct {
/*0000*/ char     name[32];        // Door model name (matches zone object)
/*0032*/ float    yPos;            // Y position (EQ coords)
/*0036*/ float    xPos;            // X position
/*0040*/ float    zPos;            // Z position
/*0044*/ float    heading;         // Door orientation (EQ 512 format, convert: * 360/512)
/*0048*/ uint32_t incline;         // Open rotation offset (EQ 512 format)
/*0052*/ uint16_t size;            // Scale (100 = normal, 50 = half, 200 = double)
/*0054*/ uint8_t  unknown0054[6];  // Padding
/*0060*/ uint8_t  doorId;          // Door's unique identifier in zone
/*0061*/ uint8_t  opentype;        // Door behavior type (5=door, 55=board, 56=chest, etc.)
/*0062*/ uint8_t  state_at_spawn;  // Initial state (0=closed, 1=open)
/*0063*/ uint8_t  invert_state;    // If 1, door normally spawns open
/*0064*/ uint32_t door_param;      // Lock type / key item ID
/*0068*/ uint8_t  unknown0068[12]; // Padding
/*0080*/
};
static_assert(sizeof(Door_Struct) == 80, "Door_Struct must be 80 bytes");

/*
 * Move Door structure (Titanium)
 * Size: 2 bytes
 * Used in: MoveDoor packet (door state change)
 */
struct MoveDoor_Struct {
/*0000*/ uint8_t doorid;   // Door ID to animate
/*0001*/ uint8_t action;   // 0x02=close, 0x03=open
/*0002*/
};
static_assert(sizeof(MoveDoor_Struct) == 2, "MoveDoor_Struct must be 2 bytes");

/*
 * Click Door structure (Titanium)
 * Size: 16 bytes
 * Used in: ClickDoor packet (client requests door activation)
 */
struct ClickDoor_Struct {
/*0000*/ uint8_t  doorid;          // Door to activate
/*0001*/ uint8_t  unknown001[3];   // Padding
/*0004*/ uint8_t  picklockskill;   // 0 for normal open
/*0005*/ uint8_t  unknown005[3];   // Padding
/*0008*/ uint32_t item_id;         // Key item ID (0 for normal open)
/*0012*/ uint16_t player_id;       // Player's spawn ID
/*0014*/ uint16_t unknown014;      // Padding
/*0016*/
};
static_assert(sizeof(ClickDoor_Struct) == 16, "ClickDoor_Struct must be 16 bytes");

// ============================================================================
// World Object / Tradeskill Structures (from titanium_structs.h)
// ============================================================================

/**
 * Object Struct
 * Used for forges, ovens, ground spawns, items dropped to ground, etc.
 * Size: 100 bytes (Titanium)
 * Used in: OP_GroundSpawn packet (server->client)
 */
struct Object_Struct {
/*0000*/ uint32_t linked_list_addr[2];  // Prev and next pointers (client-side linked list)
/*0008*/ float    size;                  // Object scale
/*0012*/ uint16_t solid_type;            // Collision type
/*0014*/ uint16_t padding014;            // Padding
/*0016*/ uint32_t drop_id;               // Unique object id for zone
/*0020*/ uint16_t zone_id;               // Zone the object appears in
/*0022*/ uint16_t zone_instance;         // Zone instance
/*0024*/ uint32_t incline;               // Rotation/incline
/*0028*/ uint32_t unknown024;            // Unknown
/*0032*/ float    tilt_x;                // X-axis tilt
/*0036*/ float    tilt_y;                // Y-axis tilt
/*0040*/ float    heading;               // Heading/rotation (0-360)
/*0044*/ float    z;                     // Z coordinate
/*0048*/ float    x;                     // X coordinate
/*0052*/ float    y;                     // Y coordinate
/*0056*/ char     object_name[32];       // Object name (e.g., "IT63_ACTORDEF")
/*0088*/ uint32_t unknown076;            // Unknown
/*0092*/ uint32_t object_type;           // Type of object (tradeskill type, etc.)
/*0096*/ uint32_t unknown084;            // Set to 0xFF for tradeskill containers
/*0100*/
};
static_assert(sizeof(Object_Struct) == 100, "Object_Struct must be 100 bytes");

/**
 * Click Object Struct
 * Client clicking on a zone object (forge, groundspawn, etc.)
 * Size: 8 bytes
 * Used in: OP_ClickObject packet (client->server)
 */
struct ClickObject_Struct {
/*0000*/ uint32_t drop_id;      // Unique object id for zone (from Object_Struct)
/*0004*/ uint32_t player_id;    // Player's spawn ID
/*0008*/
};
static_assert(sizeof(ClickObject_Struct) == 8, "ClickObject_Struct must be 8 bytes");

/**
 * Click Object Action Struct
 * Server response to client clicking on a World Container (forge, loom, etc.)
 * Also sent by client when they close the container.
 * Size: 92 bytes
 * Used in: OP_ClickObjectAction packet (server->client, client->server for close)
 */
struct ClickObjectAction_Struct {
/*0000*/ uint32_t player_id;        // Entity ID of player who clicked object
/*0004*/ uint32_t drop_id;          // Zone-specified unique object identifier
/*0008*/ uint32_t open;             // 1=opening, 0=closing
/*0012*/ uint32_t type;             // Object type (see TradeskillContainerType)
/*0016*/ uint32_t unknown16;        // Set to 0xA
/*0020*/ uint32_t icon;             // Icon to display for tradeskill containers
/*0024*/ uint32_t unknown24;        // Unknown
/*0028*/ char     object_name[64];  // Object name to display
/*0092*/
};
static_assert(sizeof(ClickObjectAction_Struct) == 92, "ClickObjectAction_Struct must be 92 bytes");

/**
 * New Combine Struct
 * Client requesting to perform a tradeskill combine
 * Size: 4 bytes
 * Used in: OP_TradeSkillCombine packet (client->server)
 */
struct NewCombine_Struct {
/*0000*/ int16_t container_slot;     // 1000 for world container, or bag slot ID
/*0002*/ int16_t guildtribute_slot;  // Usually 0
/*0004*/
};
static_assert(sizeof(NewCombine_Struct) == 4, "NewCombine_Struct must be 4 bytes");

/**
 * Apply Poison Struct
 * Used for applying poison to weapons (Rogue skill)
 * Size: 8 bytes
 * Used in: OP_ApplyPoison packet (bidirectional)
 */
struct ApplyPoison_Struct {
/*0000*/ uint32_t inventorySlot;  // Inventory slot of the poison item
/*0004*/ uint32_t success;        // 0 = failed, 1 = success (set by server in response)
/*0008*/
};
static_assert(sizeof(ApplyPoison_Struct) == 8, "ApplyPoison_Struct must be 8 bytes");

/**
 * Track Entry Struct
 * Single entry in tracking results
 * Size: 8 bytes (Titanium format)
 */
struct Track_Struct {
/*0000*/ uint32_t entityid;     // Spawn ID of trackable entity
/*0004*/ float distance;        // Distance to entity
/*0008*/
};
static_assert(sizeof(Track_Struct) == 8, "Track_Struct must be 8 bytes");

/**
 * Resurrect Struct
 * Used for resurrection offers and responses
 * Size: 228 bytes (Titanium format)
 * Used in: OP_RezzRequest (S->C), OP_RezzAnswer (C->S), OP_RezzComplete (S->C)
 */
struct Resurrect_Struct {
/*0000*/ uint32_t unknown00;          // Unknown, always 0
/*0004*/ uint16_t zone_id;            // Zone to resurrect in
/*0006*/ uint16_t instance_id;        // Instance ID (0 for non-instanced)
/*0008*/ float    y;                  // Y coordinate to rez at
/*0012*/ float    x;                  // X coordinate to rez at
/*0016*/ float    z;                  // Z coordinate to rez at
/*0020*/ char     your_name[64];      // Name of the person being rezzed
/*0084*/ uint32_t unknown88;          // Unknown
/*0088*/ char     rezzer_name[64];    // Name of the person casting rez
/*0152*/ uint32_t spellid;            // Spell ID of the resurrection spell
/*0156*/ char     corpse_name[64];    // Name of the corpse (with "'s corpse")
/*0220*/ uint32_t action;             // 0=decline, 1=accept (in OP_RezzAnswer)
/*0224*/
};
static_assert(sizeof(Resurrect_Struct) == 224, "Resurrect_Struct must be 224 bytes");

/**
 * Who All Request Struct
 * Used to request /who results
 * Size: 152 bytes (Titanium format)
 * Used in: OP_WhoAllRequest (C->S)
 */
struct Who_All_Struct {
/*0000*/ char     whom[64];       // Name filter (empty = all)
/*0064*/ uint32_t wrace;          // Race filter (0xFFFFFFFF = all)
/*0068*/ uint32_t wclass;         // Class filter (0xFFFFFFFF = all)
/*0072*/ uint32_t lvllow;         // Minimum level (0xFFFFFFFF = no min)
/*0076*/ uint32_t lvlhigh;        // Maximum level (0xFFFFFFFF = no max)
/*0080*/ uint32_t gmlookup;       // GM lookup flag (0xFFFFFFFF = normal)
/*0084*/ uint32_t unknown084;     // Unknown
/*0088*/ uint8_t  unknown088[64]; // Padding
/*0152*/
};
static_assert(sizeof(Who_All_Struct) == 152, "Who_All_Struct must be 152 bytes");

/**
 * Who All Return Header
 * Header for /who response (followed by variable player entries)
 * Used in: OP_WhoAllResponse (S->C)
 */
struct WhoAllReturnHeader {
/*0000*/ uint32_t id;                   // Unknown
/*0004*/ uint32_t playerineqstring;     // String ID for "Players in EverQuest"
/*0008*/ char     line[27];             // Header text
/*0035*/ uint8_t  unknown035;           // Padding (0x0A)
/*0036*/ uint32_t unknown036;           // Unknown (0)
/*0040*/ uint32_t playersinzonestring;  // String ID for "Players in zone"
/*0044*/ uint32_t unknown044[2];        // Unknown (0s)
/*0052*/ uint32_t unknown052;           // Unknown (1)
/*0056*/ uint32_t unknown056;           // Unknown (1)
/*0060*/ uint32_t playercount;          // Number of players in response
/*0064*/
};
static_assert(sizeof(WhoAllReturnHeader) == 64, "WhoAllReturnHeader must be 64 bytes");

/**
 * Inspect Request Struct
 * Used to request inspect or notify being inspected
 * Size: 8 bytes (Titanium format)
 * Used in: OP_InspectRequest (C->S and S->C)
 */
struct Inspect_Struct {
/*0000*/ uint32_t TargetID;    // Entity ID of target being inspected
/*0004*/ uint32_t PlayerID;    // Entity ID of player doing the inspect
/*0008*/
};
static_assert(sizeof(Inspect_Struct) == 8, "Inspect_Struct must be 8 bytes");

/**
 * Inspect Response Struct
 * Contains equipment info and inspect message
 * Size: 1792 bytes (Titanium format)
 * Used in: OP_InspectAnswer (C->S and S->C)
 */
struct InspectResponse_Struct {
/*0000*/ uint32_t TargetID;           // Entity ID being inspected
/*0004*/ uint32_t PlayerID;           // Entity ID doing the inspect
/*0008*/ char     itemnames[22][64];  // Equipment slot names (22 slots * 64 chars)
/*1416*/ uint32_t itemicons[22];      // Equipment slot icon IDs
/*1504*/ char     text[288];          // Inspect message
/*1792*/
};
static_assert(sizeof(InspectResponse_Struct) == 1792, "InspectResponse_Struct must be 1792 bytes");

// ============================================================================
// Group Structures (from titanium_structs.h)
// ============================================================================

// Group action constants
enum GroupAction : uint32_t {
    GROUP_ACT_JOIN = 0,
    GROUP_ACT_LEAVE = 1,
    GROUP_ACT_DISBAND = 6,
    GROUP_ACT_UPDATE = 7,
    GROUP_ACT_MAKE_LEADER = 8,
    GROUP_ACT_INVITE_INITIAL = 9,
    GROUP_ACT_AA_UPDATE = 10
};

// Generic group structure (used for simple operations)
struct GroupGeneric_Struct {
/*0000*/ char name1[64];
/*0064*/ char name2[64];
/*0128*/
};
static_assert(sizeof(GroupGeneric_Struct) == 128, "GroupGeneric_Struct must be 128 bytes");

// Group invite packet (client <-> server)
struct GroupInvite_Struct {
/*0000*/ char invitee_name[64];
/*0064*/ char inviter_name[64];
/*0128*/
};
static_assert(sizeof(GroupInvite_Struct) == 128, "GroupInvite_Struct must be 128 bytes");

// Group cancel invite
struct GroupCancel_Struct {
/*0000*/ char name1[64];
/*0064*/ char name2[64];
/*0128*/ uint8_t toggle;
/*0129*/
};
static_assert(sizeof(GroupCancel_Struct) == 129, "GroupCancel_Struct must be 129 bytes");

// Group join notification (server -> client, single member add/remove)
struct GroupJoin_Struct {
/*0000*/ uint32_t action;
/*0004*/ char yourname[64];
/*0068*/ char membername[64];
/*0132*/ uint8_t unknown[84];
/*0216*/
};
static_assert(sizeof(GroupJoin_Struct) == 216, "GroupJoin_Struct must be 216 bytes");

// Full group update (server -> client, complete group state)
struct GroupUpdate_Struct {
/*0000*/ uint32_t action;
/*0004*/ char yourname[64];
/*0068*/ char membername[5][64];  // 5 other members (excluding self)
/*0388*/ char leadersname[64];
/*0452*/
};
static_assert(sizeof(GroupUpdate_Struct) == 452, "GroupUpdate_Struct must be 452 bytes");

// Group follow request (accept invite, client -> server)
struct GroupFollow_Struct {
/*0000*/ char name1[64];  // Inviter name
/*0064*/ char name2[64];  // Invitee name (you)
/*0128*/
};
static_assert(sizeof(GroupFollow_Struct) == 128, "GroupFollow_Struct must be 128 bytes");

// Group disband (client -> server)
struct GroupDisband_Struct {
/*0000*/ char name1[64];
/*0064*/ char name2[64];
/*0128*/
};
static_assert(sizeof(GroupDisband_Struct) == 128, "GroupDisband_Struct must be 128 bytes");

// ============================================================================
// Vendor/Merchant Structures
// ============================================================================

// Merchant open request (client -> server)
struct Merchant_Click_Struct {
/*0000*/ uint32_t npc_id;      // NPC spawn ID
/*0004*/ uint32_t player_id;   // Player entity ID
/*0008*/ uint32_t unknown08;   // Unknown (always 0)
/*0012*/ uint32_t unknown12;   // Unknown (always 0)
/*0016*/
};
static_assert(sizeof(Merchant_Click_Struct) == 16, "Merchant_Click_Struct must be 16 bytes");

// Merchant open response (server -> client)
struct Merchant_Open_Struct {
/*0000*/ uint32_t npc_id;      // NPC spawn ID
/*0004*/ uint32_t unknown04;   // Always 0
/*0008*/ uint32_t action;      // 1 = success
/*0012*/ float    sell_rate;   // Price multiplier (e.g., 1.0191)
/*0016*/
};
static_assert(sizeof(Merchant_Open_Struct) == 16, "Merchant_Open_Struct must be 16 bytes");

// Merchant purchase action
enum MerchantAction : uint32_t {
    MERCHANT_BUY = 2,
    MERCHANT_SELL = 3
};

// Merchant buy/sell request (client <-> server)
struct Merchant_Purchase_Struct {
/*0000*/ uint32_t npc_id;      // NPC spawn ID
/*0004*/ uint32_t player_id;   // Player entity ID
/*0008*/ uint32_t itemslot;    // Item slot in merchant inventory (0-based)
/*0012*/ uint32_t unknown12;   // Always 0
/*0016*/ uint32_t quantity;    // Quantity to buy (1 for single, 20 for stack)
/*0020*/ uint32_t action;      // MerchantAction enum (2 = buy, 3 = sell)
/*0024*/
};
static_assert(sizeof(Merchant_Purchase_Struct) == 24, "Merchant_Purchase_Struct must be 24 bytes");

// Merchant session end (client -> server)
struct Merchant_End_Struct {
/*0000*/ uint32_t npc_id;      // NPC spawn ID
/*0004*/ uint32_t player_id;   // Player entity ID
/*0008*/
};
static_assert(sizeof(Merchant_End_Struct) == 8, "Merchant_End_Struct must be 8 bytes");

// Merchant sell request (client -> server)
// Note: Titanium uses a simpler 16-byte structure for sells (no player_id, unknown12, action)
struct Merchant_Sell_Struct {
/*0000*/ uint32_t npc_id;      // NPC spawn ID
/*0004*/ uint32_t itemslot;    // Player inventory slot (Titanium format)
/*0008*/ uint32_t quantity;    // Quantity to sell
/*0012*/ uint32_t unknown12;   // Unknown value (client sends timestamp-like value)
/*0016*/
};
static_assert(sizeof(Merchant_Sell_Struct) == 16, "Merchant_Sell_Struct must be 16 bytes");

// Merchant sell response (server -> client)
struct Merchant_Sell_Response_Struct {
/*0000*/ uint32_t npc_id;      // NPC spawn ID
/*0004*/ uint32_t itemslot;    // Player inventory slot (Titanium format)
/*0008*/ uint32_t quantity;    // Quantity sold
/*0012*/ uint32_t price;       // Total price in copper
/*0016*/
};
static_assert(sizeof(Merchant_Sell_Response_Struct) == 16, "Merchant_Sell_Response_Struct must be 16 bytes");

// Money update (server -> client) - sent after sell transactions
struct MoneyUpdate_Struct {
/*0000*/ int32_t platinum;
/*0004*/ int32_t gold;
/*0008*/ int32_t silver;
/*0012*/ int32_t copper;
/*0016*/
};
static_assert(sizeof(MoneyUpdate_Struct) == 16, "MoneyUpdate_Struct must be 16 bytes");

// Item packet type constants (first 4 bytes of OP_ItemPacket)
enum ItemPacketType : uint32_t {
    ITEM_PACKET_MERCHANT = 100,     // Item from merchant inventory
    ITEM_PACKET_TRADE_VIEW = 101,   // Item in partner's trade window (slot 0-3)
    ITEM_PACKET_INVENTORY = 103     // Item in player inventory
};

/*
 * Trade packet structures
 * Used for player-to-player trading
 */

// OP_TradeRequest - Initiate a trade request
// Size: 8 bytes
struct TradeRequest_Struct {
    uint32_t target_spawn_id;  // Who to trade with
    uint32_t from_spawn_id;    // Who is requesting the trade
};

// OP_TradeRequestAck - Accept a trade request
// Size: 8 bytes
// NOTE: Server uses same struct as TradeRequest_Struct - first field is recipient
struct TradeRequestAck_Struct {
    uint32_t target_spawn_id;  // Who initiated the trade (packet forwarded to them)
    uint32_t from_spawn_id;    // Who is accepting
};

// Coin type constants for MoveCoin_Struct
static const int32_t COINTYPE_CP = 0;  // Copper
static const int32_t COINTYPE_SP = 1;  // Silver
static const int32_t COINTYPE_GP = 2;  // Gold
static const int32_t COINTYPE_PP = 3;  // Platinum

// Coin slot constants for MoveCoin_Struct
static const int32_t COINSLOT_CURSOR = 0;
static const int32_t COINSLOT_INVENTORY = 1;
static const int32_t COINSLOT_BANK = 2;
static const int32_t COINSLOT_TRADE = 3;
static const int32_t COINSLOT_SHAREDBANK = 4;

// OP_MoveCoin - Move coins between locations (cursor, inventory, trade, etc.)
// Size: 20 bytes
struct MoveCoin_Struct {
    int32_t from_slot;    // Source: 0=cursor, 1=inventory, 2=bank, 3=trade, 4=shared bank
    int32_t to_slot;      // Destination: same as above
    int32_t cointype1;    // Source coin type: 0=copper, 1=silver, 2=gold, 3=platinum
    int32_t cointype2;    // Destination coin type (usually same as cointype1)
    int32_t amount;       // Amount of coins to move
};

// OP_TradeCoins - Set coin amounts in trade window (Server->Client notification)
// Size: 12 bytes
struct TradeCoins_Struct {
    uint32_t spawn_id;         // Player modifying coins
    uint8_t  slot;             // Coin type: 0=copper, 1=silver, 2=gold, 3=platinum
    uint8_t  unknown[3];       // Magic bytes: must be {0xD2, 0x4F, 0x00} for server to accept
    uint32_t amount;           // Amount of this coin type
};

// OP_TradeAcceptClick - Click accept button in trade
// Size: 8 bytes
struct TradeAcceptClick_Struct {
    uint32_t spawn_id;         // Player clicking accept
    uint8_t  accepted;         // 1 = accepted, 0 = not accepted
    uint8_t  unknown[3];       // Possibly trade ID or padding
};

// OP_FinishTrade - Trade completed signal
// Size: 2 bytes
struct FinishTrade_Struct {
    uint8_t  unknown[2];       // Signal only, no meaningful data
};

// OP_CancelTrade - Cancel or reject a trade
// Size: 8 bytes
struct CancelTrade_Struct {
    uint32_t spawn_id;         // Player canceling/rejecting
    uint32_t action;           // Action code (0x07 observed for cancel)
};

// OP_ReadBook - Request to read a book/note item (C->S)
// Variable length - txtfile is null-terminated string
struct BookRequest_Struct {
    uint8_t window;            // Display window (0xFF = new window)
    uint8_t type;              // 0=scroll, 1=book, 2=item info
    char txtfile[1];           // Variable length - text file name from items table
};

// OP_ReadBook - Book/note text response (S->C)
// Variable length - booktext is null-terminated string
struct BookText_Struct {
    uint8_t window;            // Display window (0xFF = new window)
    uint8_t type;              // 0=scroll, 1=book, 2=item info
    char booktext[1];          // Variable length - text content (use '^' for newlines)
};

/*
 * Skill Training packet structures
 * Used for GM/class trainer skill training
 */

// OP_GMTraining - Skill training request/response (bidirectional)
// Client sends to request training window, server responds with skill caps
// Size: 448 bytes
struct GMTrainee_Struct {
/*0000*/ uint32_t npcid;                   // Trainer NPC entity ID
/*0004*/ uint32_t playerid;                // Player entity ID
/*0008*/ uint32_t skills[MAX_PP_SKILL];    // Array of max trainable skill values (100 skills)
/*0408*/ uint8_t  unknown408[40];          // Unknown padding/data
/*0448*/
};
static_assert(sizeof(GMTrainee_Struct) == 448, "GMTrainee_Struct must be 448 bytes");

// OP_GMEndTraining - End training session (client -> server)
// Size: 8 bytes
struct GMTrainEnd_Struct {
/*0000*/ uint32_t npcid;                   // Trainer NPC entity ID
/*0004*/ uint32_t playerid;                // Player entity ID
/*0008*/
};
static_assert(sizeof(GMTrainEnd_Struct) == 8, "GMTrainEnd_Struct must be 8 bytes");

// OP_GMTrainSkill - Request to train a specific skill (client -> server)
// Size: 12 bytes
struct GMSkillChange_Struct {
/*0000*/ uint16_t npcid;                   // Trainer NPC entity ID (truncated to 16-bit)
/*0002*/ uint8_t  unknown1[2];             // Session identifier or padding
/*0004*/ uint16_t skillbank;               // 0 = normal skills, 1 = languages
/*0006*/ uint8_t  unknown2[2];             // Padding
/*0008*/ uint16_t skill_id;                // Skill ID to train
/*0010*/ uint8_t  unknown3[2];             // Padding
/*0012*/
};
static_assert(sizeof(GMSkillChange_Struct) == 12, "GMSkillChange_Struct must be 12 bytes");

// ============================================================================
// Pet Command Structures
// ============================================================================

// OP_PetCommands - Pet command sent from client to server
// Size: 8 bytes
// Used in: OP_PetCommands packet (client->server)
struct PetCommand_Struct {
/*0000*/ uint32_t command;     // Pet command ID (see pet_constants.h PetCommand enum)
/*0004*/ uint32_t target;      // Target spawn ID (for attack commands, 0 otherwise)
/*0008*/
};
static_assert(sizeof(PetCommand_Struct) == 8, "PetCommand_Struct must be 8 bytes");

// OP_PetCommandState - Pet button state update from server
// Size: 8 bytes
// Used in: OP_PetCommandState packet (server->client)
// Note: This opcode may be unmapped for Titanium (shows 0x0000 in some captures)
struct PetCommandState_Struct {
/*0000*/ uint32_t button_id;   // Pet button ID (see pet_constants.h PetButton enum)
/*0004*/ uint32_t state;       // Button state: 0 = off/unpressed, 1 = on/pressed
/*0008*/
};
static_assert(sizeof(PetCommandState_Struct) == 8, "PetCommandState_Struct must be 8 bytes");

#pragma pack(pop)

// ============================================================================
// Guild Structures (from titanium_structs.h)
// ============================================================================

/**
 * Guild Command Struct
 * Used for guild invites
 * Size: 134 bytes (Titanium format)
 * Used in: OP_GuildInvite (C->S)
 */
struct GuildCommand_Struct {
/*0000*/ char     othername[64];  // Target player name
/*0064*/ char     myname[64];     // Inviter name
/*0128*/ uint16_t guildeqid;      // Guild ID
/*0130*/ uint8_t  unknown130[2];  // Padding
/*0132*/ uint32_t officer;        // Inviter rank (0=member, 1=officer, 2=leader)
/*0136*/
};
static_assert(sizeof(GuildCommand_Struct) == 136, "GuildCommand_Struct must be 136 bytes");

/**
 * Guild Invite Accept Struct
 * Used when accepting guild invites
 * Size: 140 bytes (Titanium format)
 * Used in: OP_GuildInviteAccept (C->S)
 */
struct GuildInviteAccept_Struct {
/*0000*/ char     inviter[64];    // Who sent the invite
/*0064*/ char     newmember[64];  // Player accepting
/*0128*/ uint32_t response;       // 0=decline, 1=accept
/*0132*/ uint32_t guildeqid;      // Guild ID
/*0136*/
};
static_assert(sizeof(GuildInviteAccept_Struct) == 136, "GuildInviteAccept_Struct must be 136 bytes");

/**
 * Guild Remove Struct
 * Used for removing/kicking guild members
 * Size: 136 bytes (Titanium format)
 * Used in: OP_GuildRemove (C->S)
 */
struct GuildRemove_Struct {
/*0000*/ char     target[64];     // Player being removed
/*0064*/ char     name[64];       // Player doing the removal
/*0128*/ uint32_t unknown128;     // Unknown
/*0132*/ uint32_t leaderstatus;   // Remover's rank
/*0136*/
};
static_assert(sizeof(GuildRemove_Struct) == 136, "GuildRemove_Struct must be 136 bytes");

/**
 * Guild Demote Struct
 * Used for demoting guild members
 * Size: 128 bytes (Titanium format)
 * Used in: OP_GuildDemote (C->S)
 */
struct GuildDemote_Struct {
/*0000*/ char     name[64];       // Player doing the demotion
/*0064*/ char     target[64];     // Player being demoted
/*0128*/
};
static_assert(sizeof(GuildDemote_Struct) == 128, "GuildDemote_Struct must be 128 bytes");

/**
 * Guild Make Leader Struct
 * Used for transferring guild leadership
 * Size: 128 bytes (Titanium format)
 * Used in: OP_GuildLeader (C->S)
 */
struct GuildMakeLeader_Struct {
/*0000*/ char     name[64];       // Current leader name
/*0064*/ char     target[64];     // New leader name
/*0128*/
};
static_assert(sizeof(GuildMakeLeader_Struct) == 128, "GuildMakeLeader_Struct must be 128 bytes");

/**
 * Guild MOTD Struct
 * Used for get/set guild MOTD
 * Size: 648 bytes (Titanium format)
 * Used in: OP_GetGuildMOTDReply (S->C), OP_SetGuildMOTD (C->S)
 */
struct GuildMOTD_Struct {
/*0000*/ uint32_t unknown0;       // Unknown
/*0004*/ char     name[64];       // Guild name or player name
/*0068*/ char     setby_name[64]; // Who set the MOTD
/*0132*/ uint32_t unknown132;     // Unknown
/*0136*/ char     motd[512];      // MOTD text
/*0648*/
};
static_assert(sizeof(GuildMOTD_Struct) == 648, "GuildMOTD_Struct must be 648 bytes");

/**
 * Guild Member Update Struct
 * Used for member status updates (zone change, etc.)
 * Size: 76 bytes (Titanium format)
 * Used in: OP_GuildMemberUpdate (S->C)
 */
struct GuildMemberUpdate_Struct {
/*0000*/ uint32_t guild_id;        // Guild ID
/*0004*/ char     member_name[64]; // Member name
/*0068*/ uint16_t zone_id;         // Zone ID
/*0070*/ uint16_t instance_id;     // Instance ID
/*0072*/ uint32_t unknown072;      // Unknown
/*0076*/
};
static_assert(sizeof(GuildMemberUpdate_Struct) == 76, "GuildMemberUpdate_Struct must be 76 bytes");

// ============================================================================
// Phase 3: Corpse Management Structures
// ============================================================================

/**
 * Corpse Drag Struct
 * Used for dragging corpses
 * Size: 152 bytes (Titanium format)
 * Used in: OP_CorpseDrag, OP_CorpseDrop (C->S)
 */
struct CorpseDrag_Struct {
/*0000*/ char     CorpseName[64];     // Name of corpse being dragged
/*0064*/ char     DraggerName[64];    // Name of player dragging
/*0128*/ uint8_t  Unknown128[24];     // Unknown padding
/*0152*/
};
static_assert(sizeof(CorpseDrag_Struct) == 152, "CorpseDrag_Struct must be 152 bytes");

// ============================================================================
// Phase 3: Consent System Structures
// ============================================================================

/**
 * Consent Request Struct
 * Used for granting consent to loot corpse
 * Variable size (name is null-terminated string)
 * Used in: OP_Consent, OP_ConsentDeny (C->S)
 */
struct Consent_Struct {
/*0000*/ char     name[1];            // Variable length player name
};

/**
 * Consent Response Struct
 * Used for consent response from server
 * Size: 161 bytes (Titanium format)
 * Used in: OP_ConsentResponse, OP_DenyResponse (S->C)
 */
struct ConsentResponse_Struct {
/*0000*/ char     grantname[64];      // Player granted consent
/*0064*/ char     ownername[64];      // Corpse owner name
/*0128*/ uint8_t  permission;         // 1=granted, 0=denied
/*0129*/ char     zonename[32];       // Zone where corpse is
/*0161*/
};
static_assert(sizeof(ConsentResponse_Struct) == 161, "ConsentResponse_Struct must be 161 bytes");

// ============================================================================
// Phase 3: Travel System Structures
// ============================================================================

/**
 * Control Boat Struct
 * Used for controlling boat direction
 * Size: 20 bytes (Titanium format)
 * Used in: OP_ControlBoat (C->S)
 */
struct ControlBoat_Struct {
/*0000*/ uint32_t boatid;             // Boat entity ID
/*0004*/ float    heading;            // Target heading
/*0008*/ uint8_t  type;               // Control type (0=no input, 1=left, 2=right)
/*0009*/ uint8_t  unknown[11];        // Padding
/*0020*/
};
static_assert(sizeof(ControlBoat_Struct) == 20, "ControlBoat_Struct must be 20 bytes");

// ============================================================================
// Phase 3: Group Split Structures
// ============================================================================

/**
 * Split Money Struct
 * Used for splitting money with group
 * Size: 16 bytes (Titanium format)
 * Used in: OP_Split (C->S)
 */
struct Split_Struct {
/*0000*/ uint32_t platinum;           // Platinum to split
/*0004*/ uint32_t gold;               // Gold to split
/*0008*/ uint32_t silver;             // Silver to split
/*0012*/ uint32_t copper;             // Copper to split
/*0016*/
};
static_assert(sizeof(Split_Struct) == 16, "Split_Struct must be 16 bytes");

// ============================================================================
// Phase 3: LFG System Structures
// ============================================================================

/**
 * LFG Appearance Struct
 * Used for LFG flag updates from server
 * Size: 8 bytes (Titanium format)
 * Used in: OP_LFGAppearance (S->C)
 */
struct LFG_Appearance_Struct {
/*0000*/ uint32_t spawn_id;           // Player spawn ID
/*0004*/ uint32_t lfg;                // 0=off, 1=lfg
/*0008*/
};
static_assert(sizeof(LFG_Appearance_Struct) == 8, "LFG_Appearance_Struct must be 8 bytes");

// ============================================================================
// Phase 3: Combat Abilities Structures
// ============================================================================

/**
 * Shielding Struct
 * Used for warrior/paladin shielding ability
 * Size: 4 bytes (Titanium format)
 * Used in: OP_Shielding (C->S)
 */
struct Shielding_Struct {
/*0000*/ uint32_t target_id;          // Target to shield
/*0004*/
};
static_assert(sizeof(Shielding_Struct) == 4, "Shielding_Struct must be 4 bytes");

/**
 * Environmental Damage Struct
 * Used for damage from environment (falling, lava, etc.)
 * Size: 32 bytes (Titanium format)
 * Used in: OP_EnvDamage (S->C)
 */
struct EnvDamage2_Struct {
/*0000*/ uint32_t id;                 // Entity ID
/*0004*/ uint16_t damage;             // Amount of damage
/*0006*/ uint8_t  unknown06[10];      // Unknown
/*0016*/ uint8_t  dmgtype;            // Damage type (250=fall, 251=drown, 252=burn, 253=lava)
/*0017*/ uint8_t  unknown17[14];      // Unknown
/*0031*/ uint8_t  constant;           // Usually 0xFE
/*0032*/
};
static_assert(sizeof(EnvDamage2_Struct) == 32, "EnvDamage2_Struct must be 32 bytes");

// ============================================================================
// Phase 3: Discipline System Structures
// (Disciplines_Struct is already defined with MAX_PP_DISCIPLINES)
// ============================================================================

/**
 * Discipline Timer Struct
 * Used for discipline reuse timers
 * Size: 8 bytes (Titanium format)
 * Used in: OP_DisciplineTimer (S->C)
 */
struct DisciplineTimer_Struct {
/*0000*/ uint32_t timer_id;           // Timer slot ID
/*0004*/ uint32_t timer_value;        // Remaining time in seconds
/*0008*/
};
static_assert(sizeof(DisciplineTimer_Struct) == 8, "DisciplineTimer_Struct must be 8 bytes");

// ============================================================================
// Phase 3: Banking Structures
// ============================================================================

/**
 * Banker Change Struct
 * Used for banker/currency updates
 * Size: 32 bytes (Titanium format)
 * Used in: OP_BankerChange (S->C)
 */
struct BankerChange_Struct {
/*0000*/ uint32_t platinum;           // Bank platinum
/*0004*/ uint32_t gold;               // Bank gold
/*0008*/ uint32_t silver;             // Bank silver
/*0012*/ uint32_t copper;             // Bank copper
/*0016*/ uint32_t platinum_bank;      // On-hand platinum (duplicate?)
/*0020*/ uint32_t gold_bank;          // On-hand gold (duplicate?)
/*0024*/ uint32_t silver_bank;        // On-hand silver (duplicate?)
/*0028*/ uint32_t copper_bank;        // On-hand copper (duplicate?)
/*0032*/
};
static_assert(sizeof(BankerChange_Struct) == 32, "BankerChange_Struct must be 32 bytes");

// ============================================================================
// Phase 3: Misc Structures
// ============================================================================

/**
 * Popup Response Struct
 * Used for responding to popup dialogs
 * Size: 12 bytes (Titanium format)
 * Used in: OP_PopupResponse (C->S)
 */
struct PopupResponse_Struct {
/*0000*/ uint32_t sender;             // NPC/entity that sent popup
/*0004*/ uint32_t popup_id;           // Popup ID from PopupText
/*0008*/ uint32_t response;           // Button clicked (1=yes, 2=no)
/*0012*/
};
static_assert(sizeof(PopupResponse_Struct) == 12, "PopupResponse_Struct must be 12 bytes");

// ============================================================================
// Phase 4: Dueling Structures
// ============================================================================

/**
 * Duel Struct
 * Used for duel requests and acceptance
 * Size: 8 bytes (Titanium format)
 * Used in: OP_RequestDuel (Bidirectional), OP_DuelAccept (C->S)
 */
struct Duel_Struct {
/*0000*/ uint32_t duel_initiator;     // ID of initiator
/*0004*/ uint32_t duel_target;        // ID of target
/*0008*/
};
static_assert(sizeof(Duel_Struct) == 8, "Duel_Struct must be 8 bytes");

/**
 * Duel Response Struct
 * Used for declining duels
 * Size: 12 bytes (Titanium format)
 * Used in: OP_DuelDecline (C->S)
 */
struct DuelResponse_Struct {
/*0000*/ uint32_t target_id;          // Player being declined
/*0004*/ uint32_t entity_id;          // Initiator of the duel
/*0008*/ uint32_t unknown;            // Unknown purpose
/*0012*/
};
static_assert(sizeof(DuelResponse_Struct) == 12, "DuelResponse_Struct must be 12 bytes");

// ============================================================================
// Phase 4: Skills Structures
// ============================================================================

/**
 * Bind Wound Struct
 * Used for bind wound skill
 * Size: 8 bytes (Titanium format)
 * Used in: OP_BindWound (C->S)
 */
struct BindWound_Struct {
/*0000*/ uint16_t to;                 // Target ID
/*0002*/ uint16_t unknown2;           // Placeholder
/*0004*/ uint16_t type;               // Bind wound type
/*0006*/ uint16_t unknown6;           // Unknown
/*0008*/
};
static_assert(sizeof(BindWound_Struct) == 8, "BindWound_Struct must be 8 bytes");

// ============================================================================
// Phase 4: Tradeskill Recipe Structures
// ============================================================================

/**
 * Recipe Search Struct
 * Used for searching recipes
 * Size: 80 bytes (Titanium format)
 * Used in: OP_RecipesSearch (C->S)
 */
struct RecipesSearch_Struct {
/*0000*/ uint32_t object_type;        // Object type
/*0004*/ uint32_t some_id;            // Object ID
/*0008*/ uint32_t mintrivial;         // Minimum trivial
/*0012*/ uint32_t maxtrivial;         // Maximum trivial
/*0016*/ char     query[56];          // Search query string
/*0072*/ uint32_t unknown4;           // Set to 0x00030000
/*0076*/ uint32_t unknown5;           // Set to 0x0012DD4C
/*0080*/
};
static_assert(sizeof(RecipesSearch_Struct) == 80, "RecipesSearch_Struct must be 80 bytes");

/**
 * Recipe Reply Struct
 * Used for recipe search results
 * Size: 84 bytes (Titanium format)
 * Used in: OP_RecipeReply (S->C)
 */
struct RecipeReply_Struct {
/*0000*/ uint32_t object_type;        // Object type
/*0004*/ uint32_t some_id;            // Object ID
/*0008*/ uint32_t component_count;    // Number of components
/*0012*/ uint32_t recipe_id;          // Recipe ID
/*0016*/ uint32_t trivial;            // Trivial value
/*0020*/ char     recipe_name[64];    // Recipe name
/*0084*/
};
static_assert(sizeof(RecipeReply_Struct) == 84, "RecipeReply_Struct must be 84 bytes");

/**
 * Recipe Auto-Combine Struct
 * Used for auto-combining recipes
 * Size: 20 bytes (Titanium format)
 * Used in: OP_RecipeAutoCombine (Bidirectional)
 */
struct RecipeAutoCombine_Struct {
/*0000*/ uint32_t object_type;        // Object type
/*0004*/ uint32_t some_id;            // Object ID
/*0008*/ uint32_t unknown1;           // Echoed in reply
/*0012*/ uint32_t recipe_id;          // Recipe ID
/*0016*/ uint32_t reply_code;         // Request: 0xe16493, Reply: 0=success, 0xfffff5=failure
/*0020*/
};
static_assert(sizeof(RecipeAutoCombine_Struct) == 20, "RecipeAutoCombine_Struct must be 20 bytes");

// ============================================================================
// Phase 4: Cosmetic Structures
// ============================================================================

/**
 * Surname Struct
 * Used for setting surname
 * Size: 100 bytes (Titanium format)
 * Used in: OP_Surname (C->S)
 */
struct Surname_Struct {
/*0000*/ char     name[64];           // Character name
/*0064*/ uint32_t unknown0064;        // Unknown
/*0068*/ char     lastname[32];       // Surname to set
/*0100*/
};
static_assert(sizeof(Surname_Struct) == 100, "Surname_Struct must be 100 bytes");

/**
 * Face Change Struct
 * Used for changing face appearance
 * Size: 8 bytes (Titanium format, padded)
 * Used in: OP_FaceChange (C->S)
 */
struct FaceChange_Struct {
/*0000*/ uint8_t haircolor;           // Hair color
/*0001*/ uint8_t beardcolor;          // Beard color
/*0002*/ uint8_t eyecolor1;           // Left eye color
/*0003*/ uint8_t eyecolor2;           // Right eye color
/*0004*/ uint8_t hairstyle;           // Hair style
/*0005*/ uint8_t beard;               // Beard style
/*0006*/ uint8_t face;                // Face type
/*0007*/ uint8_t unused;              // Padding
/*0008*/
};
static_assert(sizeof(FaceChange_Struct) == 8, "FaceChange_Struct must be 8 bytes");

// ============================================================================
// Phase 4: Misc Structures
// ============================================================================

/**
 * Random Request Struct
 * Used for /random command
 * Size: 8 bytes (Titanium format)
 * Used in: OP_RandomReq (C->S)
 */
struct RandomReq_Struct {
/*0000*/ uint32_t low;                // Minimum value
/*0004*/ uint32_t high;               // Maximum value
/*0008*/
};
static_assert(sizeof(RandomReq_Struct) == 8, "RandomReq_Struct must be 8 bytes");

/**
 * Random Reply Struct
 * Used for /random result
 * Size: 76 bytes (Titanium format)
 * Used in: OP_RandomReply (S->C)
 */
struct RandomReply_Struct {
/*0000*/ uint32_t low;                // Minimum value
/*0004*/ uint32_t high;               // Maximum value
/*0008*/ uint32_t result;             // Random result
/*0012*/ char     name[64];           // Player name
/*0076*/
};
static_assert(sizeof(RandomReply_Struct) == 76, "RandomReply_Struct must be 76 bytes");

/**
 * Find Person Point
 * Used in find person path
 * Size: 12 bytes
 */
struct FindPerson_Point {
/*0000*/ float y;                     // Y coordinate
/*0004*/ float x;                     // X coordinate
/*0008*/ float z;                     // Z coordinate
/*0012*/
};
static_assert(sizeof(FindPerson_Point) == 12, "FindPerson_Point must be 12 bytes");

/**
 * Find Person Request Struct
 * Size: 16 bytes (Titanium format)
 * Used in: OP_FindPersonRequest (C->S)
 */
struct FindPersonRequest_Struct {
/*0000*/ uint32_t npc_id;             // NPC to find
/*0004*/ FindPerson_Point client_pos; // Client position
/*0016*/
};
static_assert(sizeof(FindPersonRequest_Struct) == 16, "FindPersonRequest_Struct must be 16 bytes");

/**
 * Camera Effect Struct
 * Used for camera shake/effects
 * Size: 8 bytes (Titanium format)
 * Used in: OP_CameraEffect (S->C)
 */
struct Camera_Struct {
/*0000*/ uint32_t duration;           // Duration in milliseconds
/*0004*/ float    intensity;          // Effect intensity
/*0008*/
};
static_assert(sizeof(Camera_Struct) == 8, "Camera_Struct must be 8 bytes");

// ============================================================================
// Phase 4: GM Command Structures
// ============================================================================

/**
 * GM Zone Request Struct
 * Size: 88 bytes (Titanium format)
 * Used in: OP_GMZoneRequest (Bidirectional)
 */
struct GMZoneRequest_Struct {
/*0000*/ char     charname[64];       // Character name
/*0064*/ uint32_t zone_id;            // Zone ID
/*0068*/ float    x;                  // X coordinate
/*0072*/ float    y;                  // Y coordinate
/*0076*/ float    z;                  // Z coordinate
/*0080*/ float    heading;            // Heading
/*0084*/ uint32_t success;            // Success flag
/*0088*/
};
static_assert(sizeof(GMZoneRequest_Struct) == 88, "GMZoneRequest_Struct must be 88 bytes");

/**
 * GM Summon Struct
 * Size: 104 bytes (Titanium format)
 * Used in: OP_GMSummon, OP_GMGoto, OP_GMFind (Bidirectional)
 */
struct GMSummon_Struct {
/*0000*/ char     charname[64];       // Target character name
/*0064*/ char     gmname[64];         // GM name
/*0128*/ uint32_t success;            // Success flag
/*0132*/ uint32_t zoneID;             // Zone ID
/*0136*/ int32_t  y;                  // Y coordinate
/*0140*/ int32_t  x;                  // X coordinate
/*0144*/ int32_t  z;                  // Z coordinate
/*0148*/ uint32_t unknown2;           // Unknown
/*0152*/
};
static_assert(sizeof(GMSummon_Struct) == 152, "GMSummon_Struct must be 152 bytes");

/**
 * GM Kick/Kill Struct
 * Size: 132 bytes (Titanium format)
 * Used in: OP_GMKick, OP_GMKill (Bidirectional)
 */
struct GMKick_Struct {
/*0000*/ char     name[64];           // Target name
/*0064*/ char     gmname[64];         // GM name
/*0128*/ uint8_t  unknown;            // Unknown
/*0129*/ uint8_t  padding[3];         // Padding
/*0132*/
};
static_assert(sizeof(GMKick_Struct) == 132, "GMKick_Struct must be 132 bytes");

/**
 * GM Toggle Struct
 * Size: 68 bytes (Titanium format)
 * Used in: OP_GMToggle (C->S)
 */
struct GMToggle_Struct {
/*0000*/ char     unknown0[64];       // Unknown
/*0064*/ uint32_t toggle;             // Toggle value
/*0068*/
};
static_assert(sizeof(GMToggle_Struct) == 68, "GMToggle_Struct must be 68 bytes");

/**
 * Become NPC Struct
 * Size: 8 bytes (Titanium format)
 * Used in: OP_GMBecomeNPC (C->S)
 */
struct BecomeNPC_Struct {
/*0000*/ uint32_t id;                 // NPC ID
/*0004*/ int32_t  maxlevel;           // Max level
/*0008*/
};
static_assert(sizeof(BecomeNPC_Struct) == 8, "BecomeNPC_Struct must be 8 bytes");

/**
 * GM Last Name Struct
 * Size: 200 bytes (Titanium format)
 * Used in: OP_GMLastName (Bidirectional)
 */
struct GMLastName_Struct {
/*0000*/ char     name[64];           // Character name
/*0064*/ char     gmname[64];         // GM name
/*0128*/ char     lastname[64];       // Last name to set
/*0192*/ uint16_t unknown[4];         // Unknown
/*0200*/
};
static_assert(sizeof(GMLastName_Struct) == 200, "GMLastName_Struct must be 200 bytes");

// ============================================================================
// Phase 4: Petition Structures
// ============================================================================

/**
 * Petition Update Struct
 * Size: 112 bytes (Titanium format)
 * Used in: OP_PetitionUpdate (S->C), OP_PetitionDelete (C->S)
 */
struct PetitionUpdate_Struct {
/*0000*/ uint32_t petnumber;          // Petition number
/*0004*/ uint32_t color;              // Status color (0=green, 1=yellow, 2=red)
/*0008*/ uint32_t status;             // Status code
/*0012*/ uint32_t senttime;           // Timestamp
/*0016*/ char     accountid[32];      // Account name
/*0048*/ char     gmsenttoo[64];      // GM assigned to
/*0112*/
};
static_assert(sizeof(PetitionUpdate_Struct) == 112, "PetitionUpdate_Struct must be 112 bytes");

// Helper functions for position bitfield extraction
namespace Position {
    // Extract signed 19-bit value from bitfield
    inline float ExtractCoord(uint32_t field, int shift) {
        int32_t raw = (field >> shift) & 0x7FFFF;
        if (raw & 0x40000) raw |= 0xFFF80000;  // Sign extend
        return static_cast<float>(raw) / 8.0f;
    }

    // Extract 12-bit heading
    inline float ExtractHeading(uint32_t field, int shift) {
        return static_cast<float>((field >> shift) & 0xFFF) * 360.0f / 4096.0f;
    }

    // Extract 10-bit animation
    inline uint16_t ExtractAnimation(uint32_t field, int shift) {
        return static_cast<uint16_t>((field >> shift) & 0x3FF);
    }
}

} // namespace EQT

#endif // EQT_PACKET_STRUCTS_H
