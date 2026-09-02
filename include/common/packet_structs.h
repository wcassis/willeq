/*
 * WillEQ Packet Structures
 *
 * These structures define the Titanium client packet formats.
 * All structures are packed (1-byte aligned) to match network protocol.
 *
 * Based on EQEmu titanium_structs.h but standalone for the headless client.
 *
 * ---------------------------------------------------------------------------
 * RULES FOR THIS FILE (T147, 2026-08-19)
 *
 * 1. The `#pragma pack(push, 1)` at the top is popped at the very BOTTOM of
 *    the file. It used to be popped ~700 lines early, which left roughly 40
 *    structs compiling at natural alignment. That is the mechanism behind
 *    this file's whole wire-rot class: a struct written correctly against
 *    eqemu silently gains padding, someone "fixes" the static_assert to the
 *    padded value, and the result is a permanently-passing assert on a wrong
 *    wire layout (T145 hit exactly this with EnvDamage2_Struct: 36 instead of
 *    31). Do not add another pop.
 *
 * 2. Every struct here must be a WIRE layout. In-memory DTOs do not belong in
 *    this file -- see EQT::SpellActionInfo in include/client/spell/
 *    spell_manager.h, which used to live here as `Action_Struct` complete
 *    with authoritative-looking byte-offset comments while being 44 bytes
 *    against a 31-byte wire packet.
 *
 * 3. A struct that is wrong AND has no production consumer is a trap, not
 *    documentation: the next person to reinterpret_cast it gets the T134
 *    failure mode. Delete it rather than repairing it. T147 deleted 11 on
 *    that basis, plus FinishTrade_Struct -- which did have consumers, but
 *    described a payload eqemu sends with ZERO length.
 *
 * 4. `static_assert` values here are derived from eqemu's field lists, not
 *    from what the compiler happens to produce. If an assert fails, find out
 *    which side is wrong before touching the number.
 * ---------------------------------------------------------------------------
 */

#ifndef EQT_PACKET_STRUCTS_H
#define EQT_PACKET_STRUCTS_H

#include <cstddef>
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
static_assert(sizeof(Tint_Struct) == 4, "Tint_Struct must be 4 bytes");  // eqemu titanium_structs.h:141

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
static_assert(sizeof(TintProfile) == 36, "TintProfile must be 36 bytes");  // eqemu titanium_structs.h:154

/*
 * Texture/Material structure
 * Size: 4 bytes
 */
struct Texture_Struct {
    uint32_t Material;
};
static_assert(sizeof(Texture_Struct) == 4, "Texture_Struct must be 4 bytes");  // eqemu titanium_structs.h:171

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
static_assert(sizeof(TextureProfile) == 36, "TextureProfile must be 36 bytes");  // eqemu titanium_structs.h:176

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
static_assert(sizeof(LoginInfo_Struct) == 464, "LoginInfo_Struct must be 464 bytes");  // eqemu titanium_structs.h:40 (its trailing /*488*/ comment is stale; the fields sum to 464)

/*
 * Enter World structure
 * Size: 72 bytes
 */
struct EnterWorld_Struct {
/*000*/ char     name[64];
/*064*/ uint32_t tutorial;       // 01 on "Enter Tutorial", 00 if not
/*068*/ uint32_t return_home;    // 01 on "Return Home", 00 if not
};
static_assert(sizeof(EnterWorld_Struct) == 72, "EnterWorld_Struct must be 72 bytes");  // eqemu titanium_structs.h:48

/*
 * Entity ID structure
 * Size: 4 bytes
 */
struct EntityId_Struct {
/*00*/ uint32_t entity_id;
/*04*/
};
static_assert(sizeof(EntityId_Struct) == 4, "EntityId_Struct must be 4 bytes");  // eqemu titanium_structs.h:87

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
static_assert(sizeof(NewSpawn_Struct) == 385, "NewSpawn_Struct must be 385 bytes");  // eqemu titanium_structs.h:345

/*
 * Client Zone Entry
 * Size: 68 bytes
 */
struct ClientZoneEntry_Struct {
/*0000*/ uint32_t unknown00;
/*0004*/ char     char_name[64];
};
static_assert(sizeof(ClientZoneEntry_Struct) == 68, "ClientZoneEntry_Struct must be 68 bytes");  // eqemu titanium_structs.h:350

/*
 * Server Zone Entry
 */
struct ServerZoneEntry_Struct {
    NewSpawn_Struct player;
};
static_assert(sizeof(ServerZoneEntry_Struct) == 385, "ServerZoneEntry_Struct must be 385 bytes");  // eqemu titanium_structs.h:361

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
static_assert(sizeof(NewZone_Struct) == 700, "NewZone_Struct must be 700 bytes");  // eqemu titanium_structs.h:366

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
static_assert(sizeof(SpawnAppearance_Struct) == 8, "SpawnAppearance_Struct must be 8 bytes");  // eqemu titanium_structs.h:484

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
 * Consider structure (OP_Consider)
 * Size: 28 bytes
 *
 * Verified against eqemu 2026-08-19 (T147): titanium.cpp has no
 * ENCODE/DECODE for OP_Consider, so the base struct is the wire format --
 * common/eq_packet_structs.h:1351, which is byte-identical to
 * common/patches/titanium_structs.h:1160. Both carry `pvpcon` + a 3-byte
 * tail that this struct was missing (24 instead of 28).
 *
 * NOT wire-observed. eq_combat_actions.cpp:40 parses OP_Consider with a
 * private local copy of this layout and a `p.Length() < 30` guard (2 opcode
 * + 28), which is the correct size -- so the hand-rolled parser was right and
 * this shared struct was the one that had rotted.
 */
struct Consider_Struct {
/*000*/ uint32_t playerid;
/*004*/ uint32_t targetid;
/*008*/ uint32_t faction;
/*012*/ uint32_t level;
/*016*/ int32_t  cur_hp;
/*020*/ int32_t  max_hp;
/*024*/ uint8_t  pvpcon;        // Pvp con flag 0/1
/*025*/ uint8_t  unknown3[3];
/*028*/
};
static_assert(sizeof(Consider_Struct) == 28, "Consider_Struct must be 28 bytes");

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
static_assert(sizeof(CombatAbility_Struct) == 12, "CombatAbility_Struct must be 12 bytes");  // eqemu titanium_structs.h:1572

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
static_assert(sizeof(MoneyOnCorpse_Struct) == 20, "MoneyOnCorpse_Struct must be 20 bytes");  // eqemu moneyOnCorpseStruct, titanium_structs.h:1451

/*
 * Channel Message structure (OP_ChannelMessage)
 * Variable size; fixed header is 148 bytes.
 *
 * Verified against eqemu 2026-08-19 (T147):
 * common/patches/titanium_structs.h:1020 (titanium.cpp has ENCODE and DECODE
 * for OP_ChannelMessage, so the Titanium struct is the arbiter; the base
 * struct at common/eq_packet_structs.h:1189 is byte-identical anyway).
 * `cm_unknown4` is uint32[2], not uint32 -- the message body starts at 148,
 * not 144.
 *
 * NOT wire-observed. src/client/eq_chat.cpp:31-32 already documents the
 * correct `unknown(8)` layout and parses at the right offsets by hand; this
 * shared struct was the copy that drifted, and it contradicted the correct
 * code sitting next to it.
 */
struct ChannelMessage_Struct {
/*000*/ char     targetname[64];
/*064*/ char     sender[64];
/*128*/ uint32_t language;
/*132*/ uint32_t chan_num;
/*136*/ uint32_t cm_unknown4[2];
/*144*/ uint32_t skill_in_language;
/*148*/ char     message[0];
};
static_assert(sizeof(ChannelMessage_Struct) == 148,
              "ChannelMessage_Struct fixed header must be 148 bytes");
static_assert(offsetof(ChannelMessage_Struct, message) == 148,
              "ChannelMessage_Struct message must start at offset 148");

/*
 * Death structure (OP_Death)
 * Size: 32 bytes
 *
 * !! FIELD ORDER UNSETTLED -- DO NOT "HARMONISE" THIS WITHOUT A CAPTURE !!
 * (T147, 2026-08-19)
 *
 * The size is certain (8 x uint32 in every eqemu variant). The order of
 * bindzoneid / spell_id / attack_skill is NOT:
 *   - common/eq_packet_structs.h:1367 has bindzoneid@12, spell_id@16,
 *     attack_skill@20 -- which is what this struct has;
 *   - common/patches/titanium_structs.h:1176 has attack_skill@12,
 *     spell_id@16, bindzoneid@20 -- exactly reversed.
 * titanium.cpp has no ENCODE(OP_Death), so the arbiter rule says the base
 * struct wins and our order is right. That is reasoning, not observation.
 * Only a live capture settles it; recorded in refactor/deferred.md.
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
static_assert(sizeof(Death_Struct) == 32, "Death_Struct must be 32 bytes");  // eqemu eq_packet_structs.h:1367 -- FIELD ORDER UNSETTLED, see comment

/*
 * Delete Spawn structure
 * Size: 4 bytes
 */
struct DeleteSpawn_Struct {
/*00*/ uint32_t spawn_id;
/*04*/
};
static_assert(sizeof(DeleteSpawn_Struct) == 4, "DeleteSpawn_Struct must be 4 bytes");  // eqemu titanium_structs.h:1008 (DECODE(OP_DeleteSpawn) exists, so Titanium wins over the 5-byte base struct)

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
static_assert(sizeof(BindStruct) == 20, "BindStruct must be 20 bytes");  // eqemu titanium_structs.h:743

/*
 * AA Array entry
 * Size: 8 bytes
 */
struct AA_Array {
    uint32_t AA;
    uint32_t value;
};
static_assert(sizeof(AA_Array) == 8, "AA_Array must be 8 bytes");  // eqemu titanium_structs.h:620

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
static_assert(sizeof(Tribute_Struct) == 8, "Tribute_Struct must be 8 bytes");  // eqemu titanium_structs.h:635

/*
 * Bandolier item
 * Size: 72 bytes
 */
struct BandolierItem_Struct {
    uint32_t ID;
    uint32_t Icon;
    char     Name[64];
};
static_assert(sizeof(BandolierItem_Struct) == 72, "BandolierItem_Struct must be 72 bytes");  // eqemu titanium_structs.h:650

/*
 * Bandolier set
 * Size: 320 bytes
 */
struct Bandolier_Struct {
    char     Name[32];
    BandolierItem_Struct Items[BANDOLIER_ITEM_COUNT];
};
static_assert(sizeof(Bandolier_Struct) == 320, "Bandolier_Struct must be 320 bytes");  // eqemu titanium_structs.h:658

/*
 * Potion belt item
 * Size: 72 bytes
 */
struct PotionBeltItem_Struct {
    uint32_t ID;
    uint32_t Icon;
    char     Name[64];
};
static_assert(sizeof(PotionBeltItem_Struct) == 72, "PotionBeltItem_Struct must be 72 bytes");  // eqemu titanium_structs.h:665

/*
 * Potion belt
 * Size: 288 bytes
 */
struct PotionBelt_Struct {
    PotionBeltItem_Struct Items[POTION_BELT_SIZE];
};
static_assert(sizeof(PotionBelt_Struct) == 288, "PotionBelt_Struct must be 288 bytes");  // eqemu titanium_structs.h:673

/*
 * Target structure (OP_TargetMouse)
 * Size: 4 bytes
 */
struct Target_Struct {
/*00*/ uint32_t target_id;
/*04*/
};
static_assert(sizeof(Target_Struct) == 4, "Target_Struct must be 4 bytes");  // eqemu ClientTarget_Struct, titanium_structs.h:985

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
static_assert(sizeof(BeginCast_Struct) == 8, "BeginCast_Struct must be 8 bytes");  // eqemu titanium_structs.h:460

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
static_assert(sizeof(ManaChange_Struct) == 16, "ManaChange_Struct must be 16 bytes");  // eqemu titanium_structs.h:443 (ENCODE(OP_ManaChange) exists, so Titanium wins over the 20-byte base struct)

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
static_assert(sizeof(ZoneChange_Struct) == 88, "ZoneChange_Struct must be 88 bytes");  // eqemu titanium_structs.h:1088

/*
 * RequestClientZoneChange_Struct
 * Server requests client to initiate a zone change
 * Size: 24 bytes
 */
struct RequestClientZoneChange_Struct {
/*00*/ uint16_t zone_id;
/*02*/ uint16_t instance_id;
/*04*/ float    y;
/*08*/ float    x;
/*12*/ float    z;
/*16*/ float    heading;
/*20*/ uint32_t type;  // Usually 0x01, exact meaning unknown
/*24*/
};
static_assert(sizeof(RequestClientZoneChange_Struct) == 24, "RequestClientZoneChange_Struct must be 24 bytes");  // eqemu titanium_structs.h:1100

/*
 * Loot Request structure
 * Size: 4 bytes
 */
struct LootRequest_Struct {
/*00*/ uint32_t corpse_id;
/*04*/
};
static_assert(sizeof(LootRequest_Struct) == 4, "LootRequest_Struct must be 4 bytes");  // eqemu zone/client_packet.cpp:10303 demands exactly sizeof(uint32)

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
static_assert(sizeof(ZonePoint_Entry) == 24, "ZonePoint_Entry must be 24 bytes");  // eqemu titanium_structs.h:1799

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
 * Size: 92 bytes (Titanium wire format)
 * Used in: OP_GroundSpawn packet (server->client)
 *
 * T144: this was 100 bytes and wrong from offset 8 onwards. It had been
 * derived from eqemu's SERVER-INTERNAL Object_Struct
 * (common/eq_packet_structs.h), which carries size/solid_type/incline/
 * tilt_x/tilt_y -- fields that do not exist in the Titanium wire format at
 * all -- plus an invented padding014. The authority is
 * Titanium::structs::Object_Struct (common/patches/titanium_structs.h) as
 * populated field-by-field by ENCODE(OP_GroundSpawn) in
 * common/patches/titanium.cpp. Confirmed against captured bytes from a live
 * qeynos session; see review/T144/ and tests/test_packet_structs.cpp's
 * GroundSpawn captured-bytes cases.
 *
 * NOTE: OP_GroundSpawn is a VARIABLE-LENGTH packet. ENCODE sizes it
 * `strlen(object_name) + sizeof(Object_Struct) - 1`, i.e. strlen + 91, and
 * only strcpy's the name -- the rest of the 32-byte object_name field is
 * whatever was in the freshly-allocated (uninitialised) buffer. Never
 * length-check an OP_GroundSpawn against sizeof(Object_Struct); check only
 * that it reaches the field you are about to read.
 */
struct Object_Struct {
/*0000*/ uint32_t linked_list_addr[2];  // Prev and next pointers (client-side linked list)
/*0008*/ uint16_t unknown008[2];        // Unknown (zeroed by the server)
/*0012*/ uint32_t drop_id;              // Unique object id for zone (an ENTITY id, not the DB object id)
/*0016*/ uint16_t zone_id;              // Zone the object appears in
/*0018*/ uint16_t zone_instance;        // Zone instance
/*0020*/ uint32_t unknown020;           // Unknown (zeroed by the server)
/*0024*/ uint32_t unknown024;           // Unknown (zeroed by the server)
/*0028*/ float    heading;              // Heading/rotation (EQ 512 format)
/*0032*/ float    z;                    // Z coordinate
/*0036*/ float    x;                    // X coordinate
/*0040*/ float    y;                    // Y coordinate
/*0044*/ char     object_name[32];      // Object name (e.g., "IT63_ACTORDEF"); only strlen+1 bytes are meaningful
/*0076*/ uint32_t unknown076;           // Unknown (zeroed by the server)
/*0080*/ uint32_t object_type;          // Type of object (tradeskill container type, etc.)
/*0084*/ uint32_t unknown084;           // Set to 0xFFFFFFFF by the server
/*0088*/ uint32_t spawn_id;             // Spawn id of client interacting with object (0 from the server)
/*0092*/
};
static_assert(sizeof(Object_Struct) == 92, "Object_Struct must be 92 bytes");

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
// T134: this struct was missing a 4-byte field (unknown020) right after z,
// found live -- a real OP_RezzRequest packet captured from the eqemu zone
// log had "Testmelee" (your_name) starting at byte offset 24, not 20 as
// this struct assumed, shifting every field after z by 4 bytes. Confirmed
// byte-for-byte against eqemu's own Resurrect_Struct
// (common/eq_packet_structs.h): total size 228, not 224. Before this fix,
// every resurrection offer parsed with an empty rezzer_name, a garbled
// corpse_name, and spellid=0 (LOG_INFO's "Resurrection offer from  for
// corpse '<garbage>' (spell 0)" was this bug, not a formatting issue), and
// every OP_RezzAnswer sent back had `action` written at the wrong byte
// offset, so the server could not reliably tell accept from decline.
struct Resurrect_Struct {
/*0000*/ uint32_t unknown00;          // Unknown, always 0
/*0004*/ uint16_t zone_id;            // Zone to resurrect in
/*0006*/ uint16_t instance_id;        // Instance ID (0 for non-instanced)
/*0008*/ float    y;                  // Y coordinate to rez at
/*0012*/ float    x;                  // X coordinate to rez at
/*0016*/ float    z;                  // Z coordinate to rez at
/*0020*/ uint32_t unknown020;         // Unknown, always 0 (T134: the missing field)
/*0024*/ char     your_name[64];      // Name of the person being rezzed
/*0088*/ uint32_t unknown88;          // Unknown
/*0092*/ char     rezzer_name[64];    // Name of the person casting rez
/*0156*/ uint32_t spellid;            // Spell ID of the resurrection spell
/*0160*/ char     corpse_name[64];    // Name of the corpse (with "'s corpse")
/*0224*/ uint32_t action;             // 0=decline, 1=accept (in OP_RezzAnswer)
/*0228*/
};
static_assert(sizeof(Resurrect_Struct) == 228, "Resurrect_Struct must be 228 bytes");

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
 * Size: 8 bytes (Titanium format)
 * Used in: OP_InspectRequest (C->S when we inspect, S->C when we are inspected)
 *
 * Ground truth: titanium_structs.h:1969. NOTE that eqemu's INTERNAL
 * Inspect_Struct (eq_packet_structs.h:2687) is two uint16s, i.e. 4 bytes, and
 * Handle_OP_InspectRequest length-checks against THAT. It is not a
 * contradiction and this struct is not wrong: titanium.cpp's
 * DECODE(OP_InspectRequest) converts 8 -> 4 on the way in and
 * ENCODE(OP_InspectRequest) converts 4 -> 8 on the way out. Do not "fix" this
 * to 4 bytes -- that would break the wire.
 */
struct Inspect_Struct {
/*0000*/ uint32_t TargetID;    // Entity ID of the player being inspected
/*0004*/ uint32_t PlayerID;    // Entity ID of the player doing the inspecting
/*0008*/
};
static_assert(sizeof(Inspect_Struct) == 8, "Inspect_Struct must be 8 bytes");

/**
 * Inspect Response Struct
 * Size: 1792 bytes (Titanium format)
 * Used in: OP_InspectAnswer (C->S when we answer, S->C when we receive one)
 *
 * ⚠ THE TWO ID FIELDS MEAN THE OPPOSITE OF Inspect_Struct'S. T170 found the
 * receive path reading them the request's way, which named the wrong player.
 * Ground truth is eqemu's own construction of this packet,
 * Client::ProcessInspectRequest (zone/client.cpp:6590):
 *
 *     insr->TargetID = requester->GetID();   // who ASKED -- the recipient
 *     insr->playerid = requestee->GetID();   // who is being INSPECTED
 *
 * TargetID is a ROUTING field: Handle_OP_InspectAnswer
 * (zone/client_packet.cpp:9020) looks it up to decide whom to forward the
 * answer to. The inspected player is `playerid`. Named as eqemu names them,
 * lower-case `playerid` included, so the asymmetry stays visible.
 *
 * ⚠ `text` IS PERSISTED BY THE SERVER. Handle_OP_InspectAnswer memcpy()s it
 * over the character's inspect message and calls SaveCharacterInspectMessage()
 * -- unconditionally, on every answer. Whatever a client puts here BECOMES the
 * character's stored inspect message. See EverQuest::SendInspectAnswer().
 */
struct InspectResponse_Struct {
/*0000*/ uint32_t TargetID;           // ANSWER: the requester (routing target)
/*0004*/ uint32_t playerid;           // ANSWER: the player being inspected
/*0008*/ char     itemnames[22][64];  // Equipment slot names (22 slots * 64 chars)
/*1416*/ uint32_t itemicons[22];      // Equipment slot icon IDs
/*1504*/ char     text[288];          // Inspect message -- SERVER PERSISTS THIS
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

// Merchant purchase action
enum MerchantAction : uint32_t {
    MERCHANT_BUY = 2,
    MERCHANT_SELL = 3
};

// Merchant buy/sell request (client <-> server)
//
// !! DO NOT "FIX" THE NAMES OF THIS STRUCT AND `Merchant_Sell_Struct` BELOW !!
//
// Our two names are INVERTED relative to eqemu's, and that is deliberate --
// both of our wire paths are correct, because eqemu's own naming is
// counterintuitive:
//   * `Handle_OP_ShopPlayerBuy`  (zone/client_packet.cpp:14026) expects
//     eqemu's `Merchant_Sell_Struct`     -- 24 bytes  -> our `Merchant_Purchase_Struct`
//   * `Handle_OP_ShopPlayerSell` (zone/client_packet.cpp:14274) expects
//     eqemu's `Merchant_Purchase_Struct` -- 16 bytes  -> our `Merchant_Sell_Struct`
// Verified T145: `eq_merchant.cpp` sends our 24-byte struct on
// HC_OP_ShopPlayerBuy and our 16-byte struct on HC_OP_ShopPlayerSell, which
// is what the server expects on each. Renaming these to "agree with eqemu"
// breaks BOTH merchant directions at once.
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
    ITEM_PACKET_INVENTORY = 103,    // Item in player inventory
    // T282: eqemu zone/object.cpp Object::HandleClick sends the contents of
    // a WORLD tradeskill container (a forge/oven on the ground) with this
    // type, one packet per occupied index, and the serialized item's slot
    // field is the container's own BAG INDEX 0-9 -- not 4000+index. The
    // client maps it into WORLD_BEGIN..WORLD_END on arrival.
    ITEM_PACKET_WORLD_CONTAINER = 107
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

// OP_TradeCoins - coins were added to a trade (Server->Client notification)
// Size: 12 bytes
//
// INBOUND ONLY. eqemu has no handler for this opcode; it BUILDS it inside
// Handle_OP_MoveCoin (zone/client_process.cpp:1598) and sends it to the other
// trader. To put coins into a trade, send OP_MoveCoin. T168 deleted the
// SendTradeCoins() that sent on this opcode -- nothing could ever have
// received it.
//
// Ground truth: eqemu TradeCoin_Struct (common/eq_packet_structs.h:1691) --
// trader(4), slot(1), unknown5(2), unknown7(1), amount(4).
struct TradeCoins_Struct {
    uint32_t spawn_id;         // eqemu `trader`: whose coins these are
    uint8_t  slot;             // Coin type: 0=copper, 1=silver, 2=gold, 3=platinum
    uint8_t  unknown[3];       // eqemu writes unknown5=0x4fD2, unknown7=0 here.
                               // NOT a client-side magic value: an older
                               // comment called these "magic bytes ... for
                               // server to accept", which described this
                               // server-built constant as something we had to
                               // send. There is no server accept path at all.
    uint32_t amount;           // Amount of this coin type
};
static_assert(sizeof(TradeCoins_Struct) == 12, "TradeCoins_Struct must be 12 bytes");

// OP_TradeAcceptClick - Click accept button in trade
// Size: 8 bytes
// Byte 4 (`accepted`) is UNSETTLED: the 8-byte size is certain, but whether
// that byte is a meaningful accept flag or eqemu's `unknown4` garbage has not
// been observed on the wire. Recorded in refactor/deferred.md (T147).
struct TradeAcceptClick_Struct {
    uint32_t spawn_id;         // Player clicking accept
    uint8_t  accepted;         // 1 = accepted, 0 = not accepted
    uint8_t  unknown[3];       // Possibly trade ID or padding
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
static_assert(sizeof(BookRequest_Struct) == 3, "BookRequest_Struct must be 3 bytes");  // eqemu titanium_structs.h:2071 (fixed header; txtfile is variable-length)

// OP_ReadBook - Book/note text response (S->C)
// Variable length - booktext is null-terminated string
struct BookText_Struct {
    uint8_t window;            // Display window (0xFF = new window)
    uint8_t type;              // 0=scroll, 1=book, 2=item info
    char booktext[1];          // Variable length - text content (use '^' for newlines)
};
static_assert(sizeof(BookText_Struct) == 3, "BookText_Struct must be 3 bytes");  // eqemu titanium_structs.h:2063 (fixed header; booktext is variable-length)

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

// (T147: the file-level `#pragma pack(pop)` used to sit here, ~700 lines
// before EOF, leaving everything below it at natural alignment. It now sits
// at the bottom of the file so that every struct in this header is packed.
// See the banner comment at the top of the file.)

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
/*0132*/ uint32_t officer;        // T317: NOT the inviter's rank, despite the
                                  // name. eqemu reads it as the rank to GRANT
                                  // `othername` (0=member, 1=officer,
                                  // 2=leader, Titanium encoding); the sender's
                                  // authority comes from the server's own
                                  // GuildRank(). See client/guild_packet_bodies.h.
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
/*0132*/ uint32_t leaderstatus;   // Remover's rank (Titanium encoding). T317:
                                  // eqemu's Handle_OP_GuildRemove casts the
                                  // body to GuildCommand_Struct and reads only
                                  // `othername`; this field is never read.
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
 * Emote Struct (T243)
 * Used in: OP_Emote (C->S) -- the /emote text form.
 * eqemu common/patches/titanium_structs.h Emote_Struct; the server-side
 * Handle_OP_Emote rejects any packet whose size != sizeof(Emote_Struct), so
 * the client must always send the full 1028 bytes. S->C the same opcode
 * carries u32 type + a NUL-terminated variable-length string (the Titanium
 * ENCODE trims it), with the actor's name already prepended by the server.
 */
struct Emote_Struct {
/*0000*/ uint32_t type;           // 0 = custom text, 0xffffffff = command (/dance etc.)
/*0004*/ char     message[1024];
/*1028*/
};
static_assert(sizeof(Emote_Struct) == 1028, "Emote_Struct must be 1028 bytes");

/**
 * Set Server Filter Struct (T243)
 * Used in: OP_SetServerFilter (C->S), eqemu titanium_structs.h.
 * Slot indices are eqemu's eqFilterType (common/eq_constants.h). The
 * per-slot convention is NOT uniform -- see ServerFilterSlot below.
 */
struct SetServerFilter_Struct {
/*0000*/ uint32_t filters[29];
/*0116*/
};
static_assert(sizeof(SetServerFilter_Struct) == 116, "SetServerFilter_Struct must be 116 bytes");

/**
 * eqemu eqFilterType slot indices with the value convention eqemu's
 * Client::ServerFilter() applies to each (zone/client.cpp):
 *   "0=hide, 1=show" slots (Filter0 macro -- ONLY the value 1 shows):
 *     GuildChat, Socials, GroupChat, Shouts, Auctions, OOC, BadWords,
 *     MyMisses, OthersMiss, OthersHit, MissedMe
 *   every other slot: 0 = show; 1.. = hide / mine-only / group-only.
 * Sending 0xFFFFFFFF everywhere (the pre-T243 payload) therefore hid ALL
 * of them, including FilterSocials which gates the OP_Emote broadcast.
 */
enum ServerFilterSlot : uint32_t {
	FILTER_NONE = 0,
	FILTER_GUILD_CHAT = 1,      // 0=hide, 1=show
	FILTER_SOCIALS = 2,         // 0=hide, 1=show (emotes)
	FILTER_GROUP_CHAT = 3,      // 0=hide, 1=show
	FILTER_SHOUTS = 4,          // 0=hide, 1=show
	FILTER_AUCTIONS = 5,        // 0=hide, 1=show
	FILTER_OOC = 6,             // 0=hide, 1=show
	FILTER_BAD_WORDS = 7,       // 0=hide, 1=show
	FILTER_PC_SPELLS = 8,       // 0=show, 1=hide, 2=group only
	FILTER_NPC_SPELLS = 9,      // 0=show, 1=hide
	FILTER_BARD_SONGS = 10,     // 0=show, 1=mine, 2=group, 3=hide
	FILTER_SPELL_CRITS = 11,    // 0=show, 1=mine, 2=hide
	FILTER_MELEE_CRITS = 12,    // 0=show, 1=hide
	FILTER_SPELL_DAMAGE = 13,   // 0=show, 1=mine, 2=hide
	FILTER_MY_MISSES = 14,      // 0=hide, 1=show
	FILTER_OTHERS_MISS = 15,    // 0=hide, 1=show
	FILTER_OTHERS_HIT = 16,     // 0=hide, 1=show
	FILTER_MISSED_ME = 17,      // 0=hide, 1=show
	FILTER_DAMAGE_SHIELDS = 18, // 0=show, 1=hide
	FILTER_DOT = 19,            // 0=show, 1=hide
	FILTER_PET_HITS = 20,       // 0=show, 1=hide
	FILTER_PET_MISSES = 21,     // 0=show, 1=hide
	FILTER_FOCUS_EFFECTS = 22,  // 0=show, 1=hide
	FILTER_PET_SPELLS = 23,     // 0=show, 1=hide
	FILTER_HEAL_OVER_TIME = 24, // 0=show, 1=mine, 2=hide
	FILTER_ITEM_SPEECH = 25,    // 0=show, 1=hide
	FILTER_STRIKETHROUGH = 26,  // 0=show, 1=hide
	FILTER_STUNS = 27,          // 0=show, 1=hide
	FILTER_BARD_SONGS_ON_PETS = 28, // 0=show, 1=hide
	FILTER_COUNT = 29
};

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
static_assert(sizeof(Consent_Struct) == 1, "Consent_Struct must be 1 bytes");  // eqemu titanium_structs.h:3196 (variable-length name)

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
 * Used for TAKING or RELEASING control of a boat -- not for steering it.
 * Size: 8 bytes (Titanium format)
 * Used in: OP_ControlBoat (C->S)
 *
 * Ground truth: eqemu ControlBoat_Struct (common/eq_packet_structs.h:5327);
 * titanium.cpp has no ENCODE/DECODE for OP_ControlBoat, so this goes on the
 * wire raw and `Handle_OP_ControlBoat` (zone/client_packet.cpp:5421) rejects
 * a mismatch with a visible "Wrong size: OP_ControlBoat" log line.
 *
 * T145: our previous 20-byte version had a `heading` float and a `type`
 * control byte. NEITHER FIELD EXISTS ON THE WIRE -- the packet carries only
 * a boat entity id and a single take/release boolean. Boat heading is not
 * sent through this opcode.
 */
struct ControlBoat_Struct {
/*0000*/ uint32_t boatid;             // Boat entity ID
/*0004*/ uint8_t  take_control;       // 1 = taking control, 0 = releasing it
/*0005*/ uint8_t  unknown[3];         // Unknown
/*0008*/
};
static_assert(sizeof(ControlBoat_Struct) == 8, "ControlBoat_Struct must be 8 bytes");

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
 * Used for damage from environment (falling, lava, drowning, traps)
 * Size: 31 bytes (Titanium format)
 * Used in: OP_EnvDamage
 *
 * Ground truth: eqemu EnvDamage2_Struct -- base
 * common/eq_packet_structs.h:3020 and Titanium
 * common/patches/titanium_structs.h:2251 are byte-identical, and titanium.cpp
 * has no ENCODE/DECODE for OP_EnvDamage, so this struct goes on the wire raw
 * and `Client::Handle_OP_EnvDamage`'s exact-size check
 * (zone/client_packet.cpp:6301) is the gate.
 *
 * DIRECTION (T145): despite the old comment above, OP_EnvDamage is C->S in
 * eqemu -- it is registered in ConnectedOpcodes (zone/client_packet.cpp:198)
 * and the server NEVER sends it. `ZoneProcessEnvDamage` is therefore an
 * unreachable receive path regardless of the struct size; there is no
 * corresponding sender in this client at all. See refactor/T145 Review.
 *
 * dmgtype values: eqemu EQ::constants::EnvironmentalDamage
 * (common/emu_constants.h:308) -- 250=Lava, 251=Drowning, 252=Falling,
 * 253=Trap.
 */
// T145 needed a LOCAL pack(1) here because this struct used to sit below the
// file's `#pragma pack(pop)`; at natural alignment its correct field list
// measured 36 bytes instead of 31. T147 moved that pop to EOF, so the local
// pragmas are gone and this struct is covered by the file-level pack(1) like
// every other struct in the header.
struct EnvDamage2_Struct {
/*0000*/ uint32_t id;                 // Entity ID
/*0004*/ uint16_t unknown4;           // Unknown
/*0006*/ uint32_t damage;             // Amount of damage
/*0010*/ uint8_t  unknown10[12];      // Unknown
/*0022*/ uint8_t  dmgtype;            // 250=lava, 251=drowning, 252=falling, 253=trap
/*0023*/ uint8_t  unknown2[4];        // Unknown
/*0027*/ uint16_t constant;           // Always 0xFFFF
/*0029*/ uint16_t unknown29;          // Unknown
/*0031*/
};
static_assert(sizeof(EnvDamage2_Struct) == 31, "EnvDamage2_Struct must be 31 bytes");

// ============================================================================
// Phase 3: Discipline System Structures
// (Disciplines_Struct is already defined with MAX_PP_DISCIPLINES)
// ============================================================================

/**
 * Discipline Timer Struct
 * Used for discipline reuse timers
 * Size: 12 bytes (Titanium format)
 * Used in: OP_DisciplineTimer (S->C)
 *
 * Ground truth: eqemu DisciplineTimer_Struct
 * (common/eq_packet_structs.h:5418) -- TimerID, Duration, Unknown08.
 * T145: we were missing the trailing `unknown08`. Nothing was broken by this
 * today (the only consumer is a read whose guard is `<`, and both consumed
 * fields are at correct offsets), but it would have been a live bug the
 * moment this struct was used to SEND.
 */
struct DisciplineTimer_Struct {
/*0000*/ uint32_t timer_id;           // Timer slot ID (eqemu: TimerID)
/*0004*/ uint32_t timer_value;        // Remaining time in seconds (eqemu: Duration)
/*0008*/ uint32_t unknown08;          // Unknown
/*0012*/
};
static_assert(sizeof(DisciplineTimer_Struct) == 12, "DisciplineTimer_Struct must be 12 bytes");

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
 * Size: 8 bytes (Titanium format)
 * Used in: OP_PopupResponse (C->S)
 *
 * Ground truth: eqemu PopupResponse_Struct (common/eq_packet_structs.h:1903);
 * titanium.cpp has no ENCODE/DECODE for OP_PopupResponse, so this goes on the
 * wire raw and `Handle_OP_PopupResponse` (zone/client_packet.cpp:11976)
 * rejects a mismatch.
 *
 * T145: our previous 12-byte version had a third `response` field carrying
 * "which button was clicked". THAT FIELD IS FICTIONAL -- the wire format
 * carries only the popup id, and eqemu's handler dispatches on the id alone.
 */
struct PopupResponse_Struct {
/*0000*/ uint32_t sender;             // eqemu: unknown0000
/*0004*/ uint32_t popup_id;           // Popup ID from PopupText
/*0008*/
};
static_assert(sizeof(PopupResponse_Struct) == 8, "PopupResponse_Struct must be 8 bytes");

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
 * Size: 7 bytes (Titanium format -- NOT padded to 8)
 * Used in: OP_FaceChange (C->S)
 *
 * Ground truth: eqemu Titanium::structs::FaceChange_Struct
 * (common/patches/titanium_structs.h:1860) -- seven uint8 fields, no padding.
 * titanium.cpp has DECODE(OP_FaceChange) (:2779) using DECODE_LENGTH_EXACT,
 * so the Titanium struct (7) is the wire format, NOT the 24-byte base
 * eq_packet_structs.h version (which carries Drakkin/entity_id fields that
 * Titanium never sends). A mismatch makes DECODE_LENGTH_EXACT rewrite the
 * opcode to OP_Unknown, so the packet is discarded before any handler runs.
 * (T145: we previously invented an 8th `unused` padding byte.)
 */
struct FaceChange_Struct {
/*0000*/ uint8_t haircolor;           // Hair color
/*0001*/ uint8_t beardcolor;          // Beard color
/*0002*/ uint8_t eyecolor1;           // Left eye color
/*0003*/ uint8_t eyecolor2;           // Right eye color
/*0004*/ uint8_t hairstyle;           // Hair style
/*0005*/ uint8_t beard;               // Beard style
/*0006*/ uint8_t face;                // Face type
/*0007*/
};
static_assert(sizeof(FaceChange_Struct) == 7, "FaceChange_Struct must be 7 bytes");

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
 * Size: 152 bytes (Titanium format)
 * Used in: OP_GMSummon, OP_GMGoto (C->S; both of eqemu's handlers size-check
 * against sizeof(GMSummon_Struct) = 152).
 *
 * T229: y/x/z are FLOATS, not int32 -- eqemu common/eq_packet_structs.h:1949
 * declares `float y; float x; float z;` and Client::OPGMSummon
 * (zone/client_process.cpp:1877) READS them: for an in-zone player target it
 * calls MovePC(zone, inst, gms->x, gms->y, gms->z, GetHeading()), and for a
 * cross-zone target it copies them into ServerZonePlayer_Struct. The
 * coordinates are only ignored for a non-client mob (GMMove to the GM's own
 * position) and for a corpse. Until T229 SendGMSummon left them zero, which is
 * why T148's live run relocated the target to (0, 0, 0). Note the y-before-x
 * order, which differs from GMFind_Struct below.
 */
struct GMSummon_Struct {
/*0000*/ char     charname[64];       // Target character name
/*0064*/ char     gmname[64];         // GM name
/*0128*/ uint32_t success;            // Success flag
/*0132*/ uint32_t zoneID;             // Zone ID
/*0136*/ float    y;                  // Destination, server coordinates (float, T229)
/*0140*/ float    x;
/*0144*/ float    z;
/*0148*/ uint32_t unknown2;           // Unknown
/*0152*/
};
static_assert(sizeof(GMSummon_Struct) == 152, "GMSummon_Struct must be 152 bytes");

/**
 * GM Find Struct (T229)
 * Size: 152 bytes -- same size as GMSummon_Struct, DIFFERENT field order.
 * Used in: OP_GMFind (Bidirectional).
 *
 * eqemu common/eq_packet_structs.h:1960 -- `float x; float y; float z;` (x
 * FIRST, unlike GMSummon_Struct's y-first). Client::Handle_OP_GMFind
 * (zone/client_packet.cpp:6640) echoes the request back with success=1,
 * zoneID and the target's GetX()/GetY()/GetZ() when the target is in the
 * GM's zone; an out-of-zone name comes back with success=0 and zeroed coords.
 * Until T229 the client decoded the reply through GMSummon_Struct's int32
 * fields, printing float bit patterns (`-998490112` = 0xC47C4000 = -1009.0f,
 * review/T148/README.md).
 */
struct GMFind_Struct {
/*0000*/ char     charname[64];       // Target character name
/*0064*/ char     gmname[64];         // GM name
/*0128*/ uint32_t success;            // 1 = found in this zone
/*0132*/ uint32_t zoneID;             // Zone the target is in
/*0136*/ float    x;                  // Server coordinates (x first)
/*0140*/ float    y;
/*0144*/ float    z;
/*0148*/ uint32_t unknown2;
/*0152*/
};
static_assert(sizeof(GMFind_Struct) == 152, "GMFind_Struct must be 152 bytes");

/**
 * GM Kick/Kill Struct
 * Size: 129 bytes (Titanium format -- NOT padded to 132)
 * Used in: OP_GMKick, OP_GMKill (Bidirectional)
 *
 * Ground truth: eqemu GMKick_Struct -- base common/eq_packet_structs.h:2766
 * and Titanium common/patches/titanium_structs.h:2042 are byte-identical, and
 * titanium.cpp has no ENCODE/DECODE for either opcode, so this goes on the
 * wire raw. eqemu's GMKill_Struct is the same three fields, so one struct
 * serves both opcodes here.
 *
 * `Handle_OP_GMKick` (zone/client_packet.cpp:6729) does a BARE `return` on a
 * size mismatch with no log line at all, so an over-sized packet leaves zero
 * server-side trace. (T145: we previously invented a `padding[3]`.)
 */
struct GMKick_Struct {
/*0000*/ char     name[64];           // Target name
/*0064*/ char     gmname[64];         // GM name
/*0128*/ uint8_t  unknown;            // Unknown
/*0129*/
};
static_assert(sizeof(GMKick_Struct) == 129, "GMKick_Struct must be 129 bytes");

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

#pragma pack(pop)

#endif // EQT_PACKET_STRUCTS_H
