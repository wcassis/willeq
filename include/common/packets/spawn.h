// Login, world-entry, zone-entry and spawn wire layouts: LoginInfo through
// NewZone, SpawnAppearance and Illusion.
//
// Wire layouts, extracted verbatim from the umbrella header by
// refactor/J14_packet_structs_split.md. The RULES for this file family live
// in the umbrella, include/common/packet_structs.h -- read them before
// touching a struct or a compile-time size check here.
//
// This header opens and closes its own one-byte packing region; no include
// sits inside it.

#ifndef EQT_PACKETS_SPAWN_H
#define EQT_PACKETS_SPAWN_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "common/packets/common.h"

namespace EQT {

#pragma pack(push, 1)


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


#pragma pack(pop)

} // namespace EQT

#endif // EQT_PACKETS_SPAWN_H
