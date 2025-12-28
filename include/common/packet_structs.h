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
    ITEM_PACKET_MERCHANT = 100,    // Item from merchant inventory
    ITEM_PACKET_INVENTORY = 103    // Item in player inventory
};

#pragma pack(pop)

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
