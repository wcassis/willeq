#pragma once

#include "common/logging.h"
#include "common/net/daybreak_connection.h"
#include "common/net/session_recorder.h"
#include "common/packet_structs.h"
#include "client/state/game_state.h"
#include "client/pet_constants.h"
#include "client/string_database.h"
#include "client/door_state_manager.h"
#include "client/zone_load_snapshot.h"
#include <openssl/des.h>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include <chrono>
#include <thread>
#include <atomic>
#include <deque>
#include <array>
#include <optional>
#include <functional>

// Forward declarations
class IPathfinder;
class HCMap;
class CombatManager;
class TradeManager;

namespace eqt {
    struct WorldObject;
}

namespace EQ {
    class SpellManager;
    class BuffManager;
    class SpellEffects;
    class SpellTypeProcessor;
    class SkillManager;
}

namespace EQT {
    class ZoneLines;
    // Trade packet structures (forward declarations)
    struct TradeRequest_Struct;
    struct TradeRequestAck_Struct;
    struct TradeCoins_Struct;
    struct TradeAcceptClick_Struct;
    struct CancelTrade_Struct;
}

namespace eqt {
namespace bridge {
    class GameStateBridge;
    class IrrlichtBridge;
}
// U07c3: WindowManager deleted
class HotbarModel;
}

#ifdef WITH_AUDIO
namespace EQT {
namespace Audio {
    class AudioManager;
    class ZoneAudioManager;
}
}
#endif

#ifdef EQT_HAS_GRAPHICS
namespace EQT {
namespace Graphics {
    class IrrlichtRenderer;
    enum class ConstrainedRenderingPreset;
    enum class ZoneLoadStep : uint8_t;
}
}
#include "client/graphics/constrained_renderer_config.h"
// D20e3: forward declaration for the renderer's own progress-screen type
// (owned by Application, signalled via a pointer). BspTree/PfsArchive/
// WldLoader moved out below — P28: they are zone-load data types with no
// renderer dependency (see zone_bsp.h/pfs.h/wld_loader.h), used by base
// zone loading (LoadZoneSources in eq_zone.cpp) regardless of graphics.
namespace EQT { namespace Graphics { struct LoadingStatus; } }
#endif

// P28: these are plain game-state/data types (inventory manager, chat command
// registry, hotbar button model, zone BSP/archive/WLD loading, the movement
// intent struct) with no renderer dependency — they were misclassified
// under EQT_HAS_GRAPHICS by 55b101e. Un-guarded so the headless
// (EQT_GRAPHICS=OFF) build can see them.
namespace EQT { namespace Graphics { struct BspTree; class PfsArchive; class WldLoader; } }
namespace eqt { namespace events { struct PlayerPositionChanged; } }
namespace eqt {
namespace inventory {
    class InventoryManager;
}
namespace ui {
    class CommandRegistry;

    // Hotbar button type enum (was in deleted WindowManager header)
    enum class HotbarButtonType : uint8_t {
        Spell = 0,
        Skill = 1,
        Item = 2,
        Command = 3
    };

    // Pending hotbar button (was in deleted WindowManager header)
    struct PendingHotbarButton {
        HotbarButtonType type;
        uint32_t id;
        std::string name;
        PendingHotbarButton(HotbarButtonType t, uint32_t i, std::string n)
            : type(t), id(i), name(std::move(n)) {}
    };
}
}

// Titanium login opcodes
enum TitaniumLoginOpcodes {
	HC_OP_SessionReady = 0x0001,
	HC_OP_Login = 0x0002,
	HC_OP_ServerListRequest = 0x0004,
	HC_OP_PlayEverquestRequest = 0x000d,
	HC_OP_ChatMessage = 0x0016,
	HC_OP_LoginAccepted = 0x0017,
	HC_OP_ServerListResponse = 0x0018,
	HC_OP_PlayEverquestResponse = 0x0021
};

// Titanium world opcodes
enum TitaniumWorldOpcodes {
	HC_OP_SendLoginInfo = 0x4dd0,
	HC_OP_GuildsList = 0x6957,
	HC_OP_LogServer = 0x0fa6,
	HC_OP_ApproveWorld = 0x3c25,
	HC_OP_EnterWorld = 0x7cba,
	HC_OP_PostEnterWorld = 0x52a4,
	HC_OP_ExpansionInfo = 0x04ec,
	HC_OP_SendCharInfo = 0x4513,
	HC_OP_World_Client_CRC1 = 0x5072,
	HC_OP_World_Client_CRC2 = 0x5b18,
	HC_OP_AckPacket = 0x7752,
	HC_OP_WorldClientReady = 0x5e99,
	HC_OP_MOTD = 0x024d,
	HC_OP_SetChatServer = 0x00d7,
	HC_OP_SetChatServer2 = 0x6536,
	HC_OP_ZoneServerInfo = 0x61b6,
	HC_OP_WorldComplete = 0x509d
};

// Titanium zone opcodes
enum TitaniumZoneOpcodes {
	HC_OP_ZoneEntry = 0x7213,
	HC_OP_NewZone = 0x0920,
	HC_OP_ReqClientSpawn = 0x0322,
	HC_OP_ZoneSpawns = 0x2e78,
	HC_OP_SendZonepoints = 0x3eba,
	HC_OP_ReqNewZone = 0x7ac5,
	HC_OP_PlayerProfile = 0x75df,
	HC_OP_CharInventory = 0x5394,
	HC_OP_TimeOfDay = 0x1580,
	HC_OP_SpawnDoor = 0x4c24,
	HC_OP_ClientReady = 0x5e20,
	HC_OP_ZoneChange = 0x5dd8,
	HC_OP_RequestClientZoneChange = 0x7834,  // Server requests client to zone
	HC_OP_SetServerFilter = 0x6563,
	HC_OP_GroundSpawn = 0x0f47,
	HC_OP_Weather = 0x254d,
	HC_OP_ClientUpdate = 0x14cb,
	HC_OP_SpawnAppearance = 0x7c32,
	HC_OP_NewSpawn = 0x1860,
	HC_OP_DeleteSpawn = 0x55bc,
	HC_OP_MobHealth = 0x0695,
	HC_OP_HPUpdate = 0x3bcf,
	HC_OP_TributeUpdate = 0x5639,
	HC_OP_TributeTimer = 0x4665,
	HC_OP_SendAATable = 0x367d,
	HC_OP_UpdateAA = 0x5966,
	HC_OP_RespondAA = 0x3af4,
	HC_OP_SendTributes = 0x067a,
	HC_OP_TributeInfo = 0x152d,
	HC_OP_RequestGuildTributes = 0x5e3a,
	HC_OP_SendGuildTributes = 0x5e3d,
	HC_OP_SendAAStats = 0x5996,
	HC_OP_SendExpZonein = 0x0587,
	HC_OP_WorldObjectsSent = 0x0000,
	HC_OP_ExpUpdate = 0x5ecd,
	HC_OP_RaidUpdate = 0x1f21,
	HC_OP_RaidInvite = 0x5891,   // Invite player to raid
	HC_OP_GuildMOTD = 0x475a,
	HC_OP_ChannelMessage = 0x1004,
	HC_OP_WearChange = 0x7441,
	HC_OP_MoveDoor = 0x700d,
	HC_OP_ClickDoor = 0x043b,
	HC_OP_CompletedTasks = 0x76a2,
	HC_OP_TaskDescription = 0x5ef7,    // S->C: Task description data
	HC_OP_TaskActivity = 0x682d,       // S->C: Task activity update
	HC_OP_ClearLeadershipAbilities = 0x7416,  // S->C: Clear leadership AA
	HC_OP_ClearAA = 0x5918,            // S->C: Clear AA on zone-in
	HC_OP_AAExpUpdate = 0x5f58,        // S->C: AA experience update
	HC_OP_DzCompass = 0x28aa,
	HC_OP_DzExpeditionLockoutTimers = 0x7c12,
	HC_OP_BeginCast = 0x3990,
	HC_OP_ManaChange = 0x4839,
	HC_OP_FormattedMessage = 0x5a48,
	HC_OP_PlayerStateAdd = 0x63da,
	HC_OP_Death = 0x6160,
	HC_OP_PlayerStateRemove = 0x381d,
	HC_OP_Stamina = 0x7a83,
	HC_OP_Emote = 0x2acf,
	HC_OP_Jump = 0x0797,
	// Combat opcodes
	HC_OP_AutoAttack = 0x5e55,
	HC_OP_AutoAttack2 = 0x0701,
	HC_OP_TargetCommand = 0x1477,
	HC_OP_TargetMouse = 0x6c47,
	HC_OP_Consider = 0x65ca,
	HC_OP_Action = 0x497c,
	HC_OP_CastSpell = 0x304b,
	HC_OP_InterruptCast = 0x0b97,
	HC_OP_ColoredText = 0x0b2d,
	HC_OP_Buff = 0x6a53,
	HC_OP_Damage = 0x5c78,
	HC_OP_LootRequest = 0x6f90,
	HC_OP_LootItem = 0x7081,
	HC_OP_EndLootRequest = 0x2316,  // Client sends to end loot session
	HC_OP_LootComplete = 0x0a94,    // Server sends to client
	HC_OP_ItemPacket = 0x3397,
	HC_OP_MoneyOnCorpse = 0x7fe4,
	HC_OP_FloatListThing = 0x6a1b,
	// Additional opcodes
	HC_OP_BecomeCorpse = 0x4dbc,
	HC_OP_ZonePlayerToBind = 0x385e,
	HC_OP_LevelUpdate = 0x6d44,
	HC_OP_SimpleMessage = 0x673c,
	HC_OP_TargetHoTT = 0x6a12,
	HC_OP_SkillUpdate = 0x6a93,
	HC_OP_CancelTrade = 0x2dc1,
	// Trade opcodes
	HC_OP_TradeRequest = 0x372f,      // Initiate trade request
	HC_OP_TradeRequestAck = 0x4048,   // Accept trade request
	HC_OP_TradeCoins = 0x34c1,        // Server->Client: coin amounts in trade
	HC_OP_MoveCoin = 0x7657,          // Client->Server: move coins to/from trade
	HC_OP_TradeAcceptClick = 0x0065,  // Click accept button in trade
	HC_OP_FinishTrade = 0x6014,       // Trade completed
	HC_OP_PreLogoutReply = 0x711e,
	HC_OP_MobRename = 0x0498,
	HC_OP_Stun = 0x1e51,
	// Inventory opcodes
	HC_OP_MoveItem = 0x420f,
	HC_OP_DeleteItem = 0x4d81,
	// Group opcodes (from patch_Titanium.conf)
	HC_OP_GroupInvite = 0x1b48,
	HC_OP_GroupInvite2 = 0x12d6,
	HC_OP_GroupFollow = 0x7bc7,
	HC_OP_GroupUpdate = 0x2dd6,
	HC_OP_GroupDisband = 0x0e76,
	HC_OP_GroupCancelInvite = 0x1f27,
	HC_OP_SetGroupTarget = 0x3eec,
	// LFG opcodes
	HC_OP_LFGAppearance = 0x1a85,
	// Spell-related opcodes
	HC_OP_LinkedReuse = 0x6a00,
	HC_OP_MemorizeSpell = 0x308e,
	HC_OP_Illusion = 0x448d,     // Illusion/disguise spell effect
	// Combat ability opcodes
	HC_OP_CombatAbility = 0x5ee8,  // Combat ability use (kick, bash, round kick, etc.)
	HC_OP_Taunt = 0x5e48,          // Taunt skill
	HC_OP_Disarm = 0x17d9,         // Disarm skill
	HC_OP_FeignDeath = 0x7489,     // Feign Death
	HC_OP_Mend = 0x14ef,           // Mend (monk)
	HC_OP_InstillDoubt = 0x389e,   // Intimidation
	// Rogue skill opcodes
	HC_OP_Hide = 0x4312,           // Hide
	HC_OP_Sneak = 0x74e1,          // Sneak
	HC_OP_PickPocket = 0x2ad8,     // Pick Pocket
	HC_OP_SenseTraps = 0x5666,     // Sense Traps
	HC_OP_DisarmTraps = 0x1241,    // Disarm Traps
	HC_OP_ApplyPoison = 0x0c2c,    // Apply Poison
	HC_OP_CancelSneakHide = 0x48c2, // Cancel Sneak/Hide
	// Utility/Trade skill opcodes
	HC_OP_SenseHeading = 0x05ac,   // Sense Heading
	HC_OP_Begging = 0x13e7,        // Begging
	HC_OP_Forage = 0x4796,         // Forage
	HC_OP_Fishing = 0x0b36,        // Fishing
	HC_OP_BindWound = 0x601d,      // Bind Wound
	HC_OP_Track = 0x5d11,          // Track
	HC_OP_TrackTarget = 0x7085,    // Track Target
	// Auto Fire opcode (AutoAttack/AutoAttack2 already defined above)
	HC_OP_AutoFire = 0x6c53,       // Auto Fire (ranged)
	// Misc opcodes
	HC_OP_SpecialMesg = 0x2372,   // Special message (skill-ups, NPC reactions, etc.)
	// Vendor/Merchant opcodes
	HC_OP_ShopRequest = 0x45f9,      // Open vendor window / vendor acknowledgement
	HC_OP_ShopPlayerBuy = 0x221e,    // Buy item from vendor / purchase confirmation
	HC_OP_ShopPlayerSell = 0x0e13,   // Sell item to vendor / sell confirmation
	HC_OP_ShopEnd = 0x7e03,          // Close vendor window
	HC_OP_ShopEndConfirm = 0x20b2,   // Server confirms vendor session ended
	HC_OP_MoneyUpdate = 0x267c,      // Update player money after transaction
	// Book/Note reading opcode
	HC_OP_ReadBook = 0x1496,         // Read note/book item text
	// Tradeskill/Object opcodes
	HC_OP_ClickObject = 0x3bc2,          // Client->Server: click world object (forge, groundspawn, etc.)
	HC_OP_ClickObjectAction = 0x6937,    // Server->Client: object action response (open tradeskill container)
	HC_OP_TradeSkillCombine = 0x0b40,    // Bidirectional: tradeskill combine request/result
// Skill training opcodes
	HC_OP_GMTraining = 0x238f,           // Skill training initiation/data (bidirectional)
	HC_OP_GMTrainSkill = 0x11d2,         // Request to train a specific skill (client -> server)
	HC_OP_GMEndTraining = 0x613d,        // End training session (client -> server)
	HC_OP_GMEndTrainingResponse = 0x0000, // Server ack for end training (unused in Titanium)
	// Pet opcodes
	HC_OP_PetCommands = 0x10a1,          // C->S: Pet command from client
	HC_OP_PetBuffWindow = 0x4e31,        // S->C: Pet buff window data
	// Logout/Camp opcodes
	HC_OP_Camp = 0x78c1,                 // C->S: Camp request (starts 30s timer)
	HC_OP_Logout = 0x61ff,               // C->S: Logout request (after camp timer)
	HC_OP_LogoutReply = 0x3cdc,          // S->C: Server confirmation of logout
	// Resurrection opcodes
	HC_OP_RezzRequest = 0x1035,          // S->C: Resurrection offer
	HC_OP_RezzAnswer = 0x6219,           // C->S: Accept/decline resurrection
	HC_OP_RezzComplete = 0x4b05,         // S->C: Resurrection complete
	// Who opcodes
	HC_OP_WhoAllRequest = 0x5cdd,        // C->S: Request /who results
	HC_OP_WhoAllResponse = 0x757b,       // S->C: Who results
	// Inspect opcodes
	HC_OP_InspectRequest = 0x775d,       // Bidirectional: Request inspect / being inspected
	HC_OP_InspectAnswer = 0x2403,        // Bidirectional: Inspect response data
	// Guild opcodes (C->S)
	HC_OP_GuildInvite = 0x18b7,          // C->S: Invite player to guild
	HC_OP_GuildInviteAccept = 0x61d0,    // C->S: Accept/decline guild invite
	HC_OP_GuildRemove = 0x0179,          // C->S: Remove member from guild
	HC_OP_GuildDelete = 0x6cce,          // C->S: Delete guild
	HC_OP_GuildLeader = 0x12b1,          // C->S: Transfer leadership
	HC_OP_GuildDemote = 0x4eb9,          // C->S: Demote member
	HC_OP_GuildPublicNote = 0x17a2,      // C->S: Set public note
	HC_OP_SetGuildMOTD = 0x591c,         // C->S: Set guild MOTD
	HC_OP_GetGuildMOTD = 0x7fec,         // C->S: Request guild MOTD
	// Guild opcodes (S->C)
	HC_OP_GuildMemberList = 0x147d,      // S->C: Full member list
	HC_OP_GuildMemberUpdate = 0x0f4d,    // S->C: Member status change
	HC_OP_GetGuildMOTDReply = 0x3246,    // S->C: MOTD response
	HC_OP_SetGuildRank = 0x6966,         // S->C: Rank change
	HC_OP_GuildMemberAdd = 0x754e,       // S->C: New member joined
	// Phase 3: Corpse Management
	HC_OP_CorpseDrag = 0x50c0,           // C->S: Start dragging corpse
	HC_OP_CorpseDrop = 0x7c7c,           // C->S: Drop dragged corpse
	HC_OP_ConsiderCorpse = 0x773f,       // C->S: Consider corpse (like /con)
	HC_OP_ConfirmDelete = 0x3838,        // C->S: Confirm corpse deletion
	// Phase 3: Consent System
	HC_OP_Consent = 0x1081,              // C->S: Grant consent to loot corpse
	HC_OP_ConsentDeny = 0x4e8c,          // C->S: Deny consent
	HC_OP_ConsentResponse = 0x6380,      // S->C: Consent granted response
	HC_OP_DenyResponse = 0x7c66,         // S->C: Consent denied response
	// Phase 3: Combat Targeting
	HC_OP_Assist = 0x7709,               // C->S: Assist target (acquire their target)
	HC_OP_AssistGroup = 0x5104,          // C->S: Group assist
	// Phase 3: Travel System
	HC_OP_BoardBoat = 0x4298,            // C->S: Board boat
	HC_OP_LeaveBoat = 0x67c9,            // C->S: Leave boat
	HC_OP_ControlBoat = 0x2c81,          // C->S: Control boat direction
	// Phase 3: Group Split
	HC_OP_Split = 0x4848,                // C->S: Split money with group
	// Phase 3: LFG System
	HC_OP_LFGCommand = 0x68ac,           // C->S: LFG toggle command
	// Phase 3: Raid additions
	HC_OP_RaidJoin = 0x3c24,             // C->S: Join raid
	HC_OP_MarkRaidNPC = 0x5191,          // C->S: Mark NPC for raid
	// Phase 3: Combat Abilities
	HC_OP_Shielding = 0x3fe6,            // C->S: Shield another player
	HC_OP_EnvDamage = 0x31b3,            // S->C: Environmental damage
	// Phase 3: Discipline System
	HC_OP_DisciplineUpdate = 0x7180,     // S->C: Discipline list update
	HC_OP_DisciplineTimer = 0x53df,      // S->C: Discipline cooldown timer
	// Phase 3: Banking
	HC_OP_BankerChange = 0x6a5b,         // S->C: Bank contents changed
	// Phase 3: Misc opcodes
	HC_OP_Save = 0x736b,                 // C->S: Save character request
	HC_OP_SaveOnZoneReq = 0x1540,        // C->S: Save before zone
	HC_OP_PopupResponse = 0x3816,        // C->S: Response to popup dialog
	HC_OP_ClearObject = 0x21ed,          // S->C: Clear world object
	// Phase 4: Dueling
	HC_OP_RequestDuel = 0x28e1,          // Bidirectional: Duel request
	HC_OP_DuelAccept = 0x1b09,           // C->S: Accept duel
	HC_OP_DuelDecline = 0x3bad,          // C->S: Decline duel
	// Phase 4: Skills (Note: HC_OP_BindWound and HC_OP_TrackTarget already defined above)
	HC_OP_TrackUnknown = 0x6177,         // C->S: Track related
	// Phase 4: Tradeskills
	HC_OP_RecipesFavorite = 0x23f0,      // C->S: Favorite recipes list
	HC_OP_RecipesSearch = 0x164d,        // C->S: Search recipes
	HC_OP_RecipeDetails = 0x4ea2,        // C->S: Get recipe details
	HC_OP_RecipeAutoCombine = 0x0353,    // Bidirectional: Auto-combine
	HC_OP_RecipeReply = 0x31f8,          // S->C: Recipe search results
	// Phase 4: Cosmetic
	HC_OP_Surname = 0x4668,              // C->S: Set surname
	HC_OP_FaceChange = 0x0f8e,           // C->S: Change face
	HC_OP_Dye = 0x00dd,                  // Bidirectional: Dye armor
	// Phase 4: Audio
	HC_OP_PlayMP3 = 0x26ab,              // S->C: Play music
	HC_OP_Sound = 0x541e,                // S->C: Play sound effect
	// Phase 4: Misc
	HC_OP_RandomReq = 0x5534,            // C->S: Random roll request
	HC_OP_RandomReply = 0x6cd5,          // S->C: Random roll result
	HC_OP_FindPersonRequest = 0x3c41,    // C->S: Find person path
	HC_OP_FindPersonReply = 0x5711,      // S->C: Find person result
	HC_OP_CameraEffect = 0x0937,         // S->C: Camera shake/effect
	HC_OP_Rewind = 0x4cfa,               // C->S: Rewind (stuck recovery)
	HC_OP_YellForHelp = 0x61ef,          // C->S: Yell for help
	HC_OP_Report = 0x7f9d,               // C->S: Report/bug
	HC_OP_FriendsWho = 0x48fe,           // C->S: Friends list who
	// Phase 4: GM Commands
	HC_OP_GMZoneRequest = 0x1306,        // Bidirectional: GM zone request
	HC_OP_GMSummon = 0x1edc,             // Bidirectional: GM summon player
	HC_OP_GMGoto = 0x1cee,               // C->S: GM goto player
	HC_OP_GMFind = 0x5930,               // Bidirectional: GM find player
	HC_OP_GMKick = 0x692c,               // Bidirectional: GM kick player
	HC_OP_GMKill = 0x6980,               // Bidirectional: GM kill target
	HC_OP_GMHideMe = 0x15b2,             // C->S: GM invisibility
	HC_OP_GMToggle = 0x7fea,             // C->S: GM toggle
	HC_OP_GMEmoteZone = 0x39f2,          // C->S: GM zone emote
	HC_OP_GMBecomeNPC = 0x7864,          // C->S: GM become NPC
	HC_OP_GMSearchCorpse = 0x3c32,       // C->S: GM search corpse
	HC_OP_GMLastName = 0x23a1,           // Bidirectional: GM set last name
	HC_OP_GMApproval = 0x0c0f,           // C->S: GM approval
	HC_OP_GMServers = 0x3387,            // C->S: GM servers
	// Phase 4: Petitions
	HC_OP_Petition = 0x251f,             // C->S: Submit petition
	HC_OP_PetitionQue = 0x33c3,          // C->S: Petition queue
	HC_OP_PetitionDelete = 0x5692        // C->S: Delete petition
};

// UCS (Universal Chat Service) opcodes
enum UCSClientOpcodes {
	HC_OP_UCS_MailLogin = 0x00,
	HC_OP_UCS_ChatMessage = 0x01,
	HC_OP_UCS_ChatJoin = 0x02,
	HC_OP_UCS_ChatLeave = 0x03,
	HC_OP_UCS_ChatWho = 0x04,
	HC_OP_UCS_ChatInvite = 0x05,
	HC_OP_UCS_ChatModerate = 0x06,
	HC_OP_UCS_ChatGrant = 0x07,
	HC_OP_UCS_ChatVoice = 0x08,
	HC_OP_UCS_ChatKick = 0x09,
	HC_OP_UCS_ChatSetOwner = 0x0a,
	HC_OP_UCS_ChatOPList = 0x0b,
	HC_OP_UCS_ChatList = 0x0c,
	HC_OP_UCS_MailHeaderCount = 0x20,
	HC_OP_UCS_MailHeader = 0x21,
	HC_OP_UCS_MailGetBody = 0x22,
	HC_OP_UCS_MailSendBody = 0x23,
	HC_OP_UCS_MailDeleteMsg = 0x24,
	HC_OP_UCS_MailNew = 0x25,
	HC_OP_UCS_Buddy = 0x40,
	HC_OP_UCS_Ignore = 0x41
};

// Character animations
enum Animations {
	ANIM_STAND = 0,
	ANIM_WALK = 12,           // Walk forward (negative = backward)
	ANIM_CROUCH_WALK = 3,
	ANIM_JUMP = 20,
	ANIM_FALL = 5,
	ANIM_SWIM_IDLE = 6,
	ANIM_SWIM = 7,
	ANIM_SWIM_ATTACK = 8,
	ANIM_FLY = 9,
	ANIM_KICK = 11,
	ANIM_BASH = 12,           // Bash attack (same value as ANIM_WALK)
	ANIM_DEATH = 16,
	ANIM_CRY = 18,
	ANIM_KNEEL = 19,
	ANIM_LAUGH = 63,
	ANIM_POINT = 64,
	ANIM_RUN = 27,
	ANIM_CHEER = 27,
	ANIM_SALUTE = 67,
	ANIM_SHRUG = 65,
	ANIM_WAVE = 29,
	ANIM_DANCE = 58,
	ANIM_LOOT = 105,
	// Zone server animation values
	ANIM_STANDING = 100,
	ANIM_FREEZE = 102,
	ANIM_SITTING = 110,
	ANIM_CROUCHING = 111,
	ANIM_LYING = 115
};

// SpawnAppearance types
enum HCAppearanceType {
	AT_DIE = 0,
	AT_WHO_LEVEL = 1,
	AT_MAX_HEALTH = 2,
	AT_INVISIBLE = 3,
	AT_PVP = 4,
	AT_LIGHT = 5,
	AT_ANIMATION = 14,
	AT_SNEAK = 15,
	AT_SPAWN_ID = 16,
	AT_HP_UPDATE = 17,
	AT_LINKDEAD = 18,
	AT_FLYMODE = 19,
	AT_GM = 20,
	AT_ANONYMOUS = 21,
	AT_GUILD_ID = 22,
	AT_GUILD_RANK = 23,
	AT_AFK = 24,
	AT_PET = 25,
	AT_SUMMONED = 27,
	AT_SPLIT = 28,
	AT_SIZE = 29
};

// Movement modes
enum MovementMode {
	MOVE_MODE_RUN = 0,
	MOVE_MODE_WALK = 1,
	MOVE_MODE_SNEAK = 2
};

// Position states
enum PositionState {
	POS_STANDING = 0,
	POS_SITTING = 1,
	POS_CROUCHING = 2,
	POS_FEIGN_DEATH = 3,
	POS_DEAD = 4
};

// Water states for swimming
enum class WaterState : uint8_t {
	NotInWater = 0,   // On land, not touching water
	OnSurface = 1,    // On water surface (swimming)
	Submerged = 2     // Fully underwater
};

// Chat channel types
enum ChatChannelType {
	CHAT_CHANNEL_GUILD = 0,
	CHAT_CHANNEL_GROUP = 2,
	CHAT_CHANNEL_SHOUT = 3,
	CHAT_CHANNEL_AUCTION = 4,
	CHAT_CHANNEL_OOC = 5,
	CHAT_CHANNEL_BROADCAST = 6,
	CHAT_CHANNEL_TELL = 7,
	CHAT_CHANNEL_SAY = 8,
	CHAT_CHANNEL_PETITION = 10,
	CHAT_CHANNEL_GMSAY = 11,
	CHAT_CHANNEL_RAID = 15,
	CHAT_CHANNEL_EMOTE = 22
};

// Loading phase enum for zone-in progress tracking
// Covers the entire pre-gameplay process: login -> world -> zone -> game state -> graphics
enum class LoadingPhase {
	// Connection phases
	DISCONNECTED = 0,           // Not connected to any server
	LOGIN_CONNECTING = 1,       // Connecting to login server
	LOGIN_AUTHENTICATING = 2,   // Sending credentials, awaiting response
	WORLD_CONNECTING = 3,       // Connecting to world server
	WORLD_CHARACTER_SELECT = 4, // Receiving character list, selecting character
	ZONE_CONNECTING = 5,        // Connecting to zone server

	// Game state phases (network packets)
	ZONE_RECEIVING_PROFILE = 6,  // Waiting for PlayerProfile
	ZONE_RECEIVING_SPAWNS = 7,   // Waiting for ZoneSpawns
	ZONE_REQUEST_PHASE = 8,      // AA data, tributes, ReqClientSpawn
	ZONE_PLAYER_READY = 9,       // GuildMOTD, ClientReady
	ZONE_AWAITING_CONFIRM = 10,  // Waiting for first ClientUpdate with our spawn_id

	// Graphics loading phases
	GRAPHICS_LOADING_ZONE = 11,       // Loading zone S3D (geometry, textures)
	GRAPHICS_LOADING_MODELS = 12,     // Loading character models
	GRAPHICS_CREATING_ENTITIES = 13,  // Creating scene nodes for all entities
	GRAPHICS_FINALIZING = 14,         // Camera, lighting, final setup

	// Complete
	COMPLETE = 15                // Hide loading screen, show game world
};

struct WorldServer
{
	std::string long_name;
	std::string address;
	int type;
	std::string lang;
	std::string region;
	int status;
	int players;
};

// T14: one occupied character-select slot, captured from OP_SendCharInfo.
// name/level/class/race/zone are exactly what the Titanium select screen
// displays, so this is product state a select UI can consume, not test-only.
struct CharacterSelectEntry
{
	int slot = -1;              // 0-9 position in the select screen
	std::string name;
	uint8_t level = 0;
	uint8_t char_class = 0;
	uint32_t race = 0;
	uint32_t zone = 0;
};

struct Entity
{
	uint16_t spawn_id = 0;
	std::string name;
	float x = 0, y = 0, z = 0;
	float heading = 0;
	uint8_t level = 0;
	uint8_t class_id = 0;
	uint16_t race_id = 0;
	uint8_t gender = 0;
	uint32_t guild_id = 0;
	uint8_t animation = 0;
	uint8_t hp_percent = 100;
	uint16_t cur_mana = 0;
	uint16_t max_mana = 0;
	float size = 0;
	bool is_corpse = false;

	// Appearance data (from Spawn_Struct)
	uint8_t face = 0;           // Face/head variant (offset 6 in Spawn_Struct)
	uint8_t haircolor = 0;      // Hair color (offset 85)
	uint8_t hairstyle = 0;      // Hair style (offset 145)
	uint8_t beardcolor = 0;     // Beard color (offset 146)
	uint8_t beard = 0;          // Beard style (offset 156)
	uint8_t equip_chest2 = 0;   // Body texture variant (offset 339)
	uint8_t helm = 0;           // Helm texture (offset 275)
	uint8_t showhelm = 0;       // Show helm (offset 139)
	uint8_t bodytype = 0;       // Body type (offset 335)
	uint8_t npc_type = 0;       // NPC type: 0=player, 1=npc, 2=pc_corpse, 3=npc_corpse (offset 83)
	uint8_t light = 0;          // Light source (offset 330)

	// Equipment textures (9 slots: head, chest, arms, wrist, hands, legs, feet, primary, secondary)
	uint32_t equipment[9] = {0};      // Material IDs from TextureProfile (offset 197)
	uint32_t equipment_tint[9] = {0}; // Tint colors from TintProfile (offset 348)

	// Weapon skill types for combat animations (255 = unknown/none)
	uint8_t primary_weapon_skill = 255;    // Weapon skill type of primary weapon (7 = H2H default)
	uint8_t secondary_weapon_skill = 255;  // Weapon skill type of secondary weapon

	// Movement tracking
	float delta_x = 0, delta_y = 0, delta_z = 0;
	float delta_heading = 0;
	uint32_t last_update_time = 0;

	// Pet tracking
	uint8_t is_pet = 0;           // Non-zero if this entity is a pet (offset 329)
	uint32_t pet_owner_id = 0;    // Spawn ID of pet's owner (offset 189)

	// Appearance state flags (updated via SpawnAppearance packets)
	bool is_invisible = false;    // AT_INVISIBLE: true if invisible to other players
	bool is_sneaking = false;     // AT_SNEAK: true if sneaking
	bool is_linkdead = false;     // AT_LINKDEAD: true if connection lost
	bool is_afk = false;          // AT_AFK: true if player is AFK
	uint8_t flymode = 0;          // AT_FLYMODE: 0=ground, 1=flying, 2=levitate
	uint8_t anon_status = 0;      // AT_ANONYMOUS: 0=normal, 1=anonymous, 2=roleplay
};

// S06: Door struct replaced by DoorState (include/client/door_state.h)
// and DoorStateManager (include/client/door_state_manager.h)

// Maximum group size (leader + 5 members)
static constexpr int MAX_GROUP_MEMBERS = 6;

// Interaction distance for vendors, bankers, tradeskill containers, etc.
// Player must be within this distance to interact with NPCs/objects
static constexpr float NPC_INTERACTION_DISTANCE = 15.0f;
static constexpr float NPC_INTERACTION_DISTANCE_SQUARED = NPC_INTERACTION_DISTANCE * NPC_INTERACTION_DISTANCE;

// Group member information (tracked locally for UI display)
struct GroupMember
{
	std::string name;
	uint16_t spawn_id = 0;       // 0 if not in zone
	uint8_t level = 0;
	uint8_t class_id = 0;
	uint8_t hp_percent = 100;
	uint8_t mana_percent = 100;  // Stored as percent for display
	bool is_leader = false;
	bool in_zone = false;        // True if we can see them in entity list
};

class EverQuest
{
public:
	EverQuest(const std::string &host, int port, const std::string &user, const std::string &pass, const std::string &server, const std::string &character);
	~EverQuest();

	// Initiate login server connection (call after graphics init to avoid timeout during slow startup)
	void ConnectToLogin();

	// Poll-based network tick: drains recv, processes protocol, resends.
	// Call from main loop instead of EQ::EventLoop::Get().Process().
	void TickNetwork();

	// Replay/test seam (Batch R): dispatches a decoded application packet
	// through the same switch ZoneOnPacketRecv uses, without requiring a
	// live m_zone_connection. `opcode` is the application opcode (HC_OP_*)
	// and `data`/`size` is the payload excluding the 2-byte opcode prefix,
	// matching include/common/net/session_log.h's RecordHeader layout.
	// Any handler-triggered sends no-op safely (see m_replay_suppressed_sends).
	void ReplayZonePacket(uint16_t opcode, const uint8_t* data, size_t size);

	// T05: same replay/test seam as ReplayZonePacket, for the login and
	// world opcode switches.
	void ReplayLoginPacket(uint16_t opcode, const uint8_t* data, size_t size);
	void ReplayWorldPacket(uint16_t opcode, const uint8_t* data, size_t size);

	// Count of zone packet sends suppressed because m_zone_connection was
	// null (replay/test mode). Only incremented by the Zone packet senders
	// reachable from S->C handlers (see ReplayZonePacket).
	uint64_t GetReplaySuppressedSendCount() const { return m_replay_suppressed_sends; }

	// T52 test seam: m_pending_zone_x/y/z/id have three producers
	// (ZoneProcessZoneChange, CheckZoneLine, and the bind/death path in
	// eq.cpp) and one consumer (WorldProcessZoneServerInfo), all private.
	// Minimal, behavior-preserving read-only accessors so tests can pin
	// each producer's coordinate convention without a live connection --
	// combined with ReplayZonePacket (for producer A) or a direct
	// UpdateMovement()/CheckZoneLine() drive (for producer B, via existing
	// public entry points), and GetPosition() for the consumer's effect.
	uint16_t GetPendingZoneId() const { return m_pending_zone_id; }
	float GetPendingZoneX() const { return m_pending_zone_x; }
	float GetPendingZoneY() const { return m_pending_zone_y; }
	float GetPendingZoneZ() const { return m_pending_zone_z; }

	// T55 test seam: narrow, read-only reader for the state layer's player
	// position (m_game_state.player().x()/y()/z()), which otherwise has no
	// public accessor -- GetPosition() below reads the legacy raw
	// m_x/m_y/m_z members only, which is exactly the propagation gap this
	// unit closes (see refactor/T55_position_state_propagation.md). Deliberately
	// does not widen GameState's public surface beyond this one read. Not
	// called from any production path.
	glm::vec3 Test_GetGameStatePlayerPosition() const;

	// T52 test seam, continued: WorldProcessZoneServerInfo's consumer
	// branch is gated on `m_zone_connected || m_zone_connection !=
	// nullptr`, which in real operation is only ever set true by a live
	// UDP connection-state callback (ZoneOnStatusChangeReconnectEnabled) --
	// not reachable through ReplayZonePacket/ReplayWorldPacket at all, and
	// (per this unit's live wire evidence) never true by the time
	// WorldProcessZoneServerInfo actually runs in the traced production
	// flows either (ProcessDeferredZoneChange always disconnects first).
	// This setter exists ONLY so a fast unit test can force that branch to
	// execute and pin its corrected (no second swap) behavior, rather than
	// leaving a fixed-but-untested dead branch. Test-only; not called from
	// any production path.
	void Test_SetZoneConnectedForSeam(bool connected) { m_zone_connected = connected; }

	// T88 test seam: SendPositionUpdate()'s top guard
	// (`!IsFullyZonedIn() || !m_zone_connection`) and MoveToWithPath()'s
	// "Not in zone yet" guard both require a live UDP zone connection that
	// no headless test can construct. Everything past those guards already
	// degrades gracefully with a null m_zone_connection --
	// SafeQueueZonePacket() and SendMovementHistory() both self-check and
	// no-op -- so this flag exists ONLY to skip those specific guards (and
	// SendPositionUpdate's send throttle, which is a single process-wide
	// static and would otherwise make test order/timing-dependent) so a
	// test can read back what the outbound C->S coordinate conversion
	// actually computed. Default false; production behavior is bit-for-bit
	// unchanged when unset. Not called from any production path.
	void Test_SetBypassSendGuardForSeam(bool bypass) { m_test_bypass_send_guard = bypass; }

	// T88 test seam: SendPositionUpdate() silently no-ops (by design) when
	// m_my_spawn_id is still 0 -- normally only ever set from a decoded
	// PlayerProfile/ZoneSpawns entity id. Lets a test drive Move()/
	// MoveToWithPath() without first replaying a full PlayerProfile.
	// Test-only; not called from any production path.
	void Test_SetMySpawnIdForSeam(uint16_t spawn_id) { m_my_spawn_id = spawn_id; }

	// T88 test seam: the last outbound client position update body
	// fields SendPositionUpdate() computed, captured unconditionally right
	// before the (possibly-suppressed) send -- this is the actual
	// production encoder output, not a re-derivation of the formula in the
	// test. Read-only; test-only.
	struct TestSentPositionUpdate {
		float x_pos = 0.0f;
		float y_pos = 0.0f;
		float z_pos = 0.0f;
		uint16_t heading_scaled = 0;
	};
	TestSentPositionUpdate Test_GetLastComputedPositionUpdate() const { return m_test_last_sent_update; }

	// T88 test seam: forces immediate arrival at the current movement
	// target, exactly mirroring UpdateMovement()'s own "distance < 2.0f"
	// arrival branch (eq_movement.cpp: m_x = m_target_x; m_y = m_target_y;
	// m_z = m_target_z; then StopMovement(), which sends the final
	// position) -- without depending on wall-clock-driven incremental
	// movement steps for targets outside that 2-unit snap radius. Test-only.
	void Test_SnapToMovementTargetForSeam();

	static void SetDebugLevel(int level) { s_debug_level = level; ::SetDebugLevel(level); }
	static int GetDebugLevel() { return s_debug_level; }

	// Public chat interface
	void SendChatMessage(const std::string &message, const std::string &channel_name, const std::string &target = "");
	void ProcessChatInput(const std::string &input);  // Process chat window input (commands or messages)
	// T29: the first stage of the unified slash-command dispatch order
	// (registry 1: EverQuest::m_command_registry -- see docs/decisions.md
	// 2026-08-02 and refactor/T29_unified_command_channel.md). Tries only
	// this registry; does not touch chat/say fallback or emit an "unknown
	// command" message -- that's ProcessChatInput's job for the live chat
	// window, and Application::DispatchSlashCommand's job for stdin/script.
	// Returns true iff m_command_registry recognised and executed cmdLine
	// (cmdLine has NO leading '/'). Safe to call unconditionally (headless
	// or graphics) -- the registry itself is no longer graphics-gated.
	bool DispatchSlashCommand(const std::string &cmdLine);
	// T29: invoked by ProcessChatInput() when DispatchSlashCommand() (registry
	// 1) doesn't recognise a command typed in the live chat window -- lets
	// the SAME registries 2/3 that stdin/--script use (see
	// Application::dispatchToActionOrRenderer(), wired in by
	// Application::wireUnifiedCommandDispatch()) also be reached from the
	// chat window, without EverQuest needing to know about Application,
	// CommandProcessor, or the renderer bridge's ownership. cmdLine has no
	// leading '/'. Returns true if handled.
	void SetUnrecognizedCommandFallback(std::function<bool(const std::string&)> fallback) {
		m_unrecognized_command_fallback = std::move(fallback);
	}
	void AddChatSystemMessage(const std::string &text);  // Add system message to chat window
	void AddChatCombatMessage(const std::string &text, bool is_self = false);  // Add combat message (is_self for player damage)
	void AddChatMissMessage(const std::string &text);  // Add miss message (filterable separately)

	// T143: read-only access to registry 1's catalog (name/alias/category
	// introspection only -- NOT a way to execute commands; use
	// DispatchSlashCommand() for that). Added so Application, which is the
	// only object that also holds registry 2 (action::CommandProcessor),
	// can build a merged /help view across both. See
	// refactor/T143_registry_aware_help.md.
	const eqt::ui::CommandRegistry* GetCommandRegistry() const { return m_command_registry.get(); }

	// Public movement interface
	void Move(float x, float y, float z);
	void MoveToEntity(const std::string &name);
	void MoveToEntityWithinRange(const std::string &name, float stop_distance);
	void StartCombatMovement(const std::string &name, float stop_distance);
	void StartCombatMovement(uint16_t entity_id);
	void SetCombatStopDistance(float distance);  // Phase 7.6: also syncs to GameState
	void UpdateCombatMovement();
	void Follow(const std::string &name);
	void StopFollow();
	void Face(float x, float y, float z);
	void FaceEntity(const std::string &name);
	void SetHeading(float heading);
	void SetPosition(float x, float y, float z);
	void SetMoving(bool moving);
	void UpdateMovement();
	void SendPositionUpdate();
	glm::vec3 GetPosition() const;
	float GetHeading() const;
	bool IsMoving() const;
	bool IsFullyZonedIn() const { return m_zone_connected && m_client_ready_sent && m_update_running; }
	bool IsZoneChangeApproved() const { return m_zone_change_approved; }
	void SetZoningEnabled(bool enabled) { m_zoning_enabled = enabled; }
	bool IsZoningEnabled() const { return m_zoning_enabled; }

	// Loading phase tracking (covers entire pre-gameplay process)
	LoadingPhase GetLoadingPhase() const { return m_loading_phase; }
	void SetLoadingPhase(LoadingPhase phase, const char* statusText = nullptr);
	float GetLoadingProgress() const;
	const char* GetLoadingStatusText() const;
	bool IsGameStateReady() const { return static_cast<int>(m_loading_phase) >= static_cast<int>(LoadingPhase::ZONE_AWAITING_CONFIRM); }
	bool IsGraphicsReady() const { return m_loading_phase == LoadingPhase::COMPLETE; }
	void OnGameStateComplete();  // Called when game state setup finishes, triggers graphics loading
	void OnGraphicsComplete();   // Called when graphics loading finishes, enables gameplay
	void ListEntities(const std::string& search = "") const;
	void DumpEntityAppearance(uint16_t spawn_id) const;
	void DumpEntityAppearance(const std::string& name) const;
	void SetPathfinding(bool enabled) { m_use_pathfinding = enabled; }
	bool IsPathfindingEnabled() const { return m_use_pathfinding; }
	bool HasReachedDestination() const;
	void SetMoveSpeed(float speed);
	void SetNavmeshPath(const std::string& path) { m_navmesh_path = path; }
	void SetMapsPath(const std::string& path) { m_maps_path = path; }

	// R01: Session recorder — records decoded application packets to `path`
	// for later replay (--record-session CLI flag). No-op hooks elsewhere
	// check m_session_recorder before recording.
	void StartSessionRecording(const std::string& path);

	// Combat and entity access
	CombatManager* GetCombatManager() { return m_combat_manager.get(); }
	TradeManager* GetTradeManager() { return m_trade_manager.get(); }
	EQ::SpellManager* GetSpellManager() { return m_spell_manager.get(); }
	EQ::BuffManager* GetBuffManager() { return m_buff_manager.get(); }
	EQ::SpellEffects* GetSpellEffects() { return m_spell_effects.get(); }
	EQ::SpellTypeProcessor* GetSpellTypeProcessor() { return m_spell_type_processor.get(); }
	EQ::SkillManager* GetSkillManager() { return m_skill_manager.get(); }
	// T71: needed for /equip's local-state sync fix to be testable without a
	// live server (getItem()/hasCursorItem() must see seeded test items) --
	// same accessor-getter pattern as the managers above, not a new seam.
	eqt::inventory::InventoryManager* GetInventoryManager() { return m_inventory_manager.get(); }

	// Hotbar button management (stub for future implementation).
	// P28: eqt::ui::PendingHotbarButton is now unconditionally declared
	// (moved out of the graphics guard above), so these accessors no longer
	// need to be graphics-only either.
	void AddPendingHotbarButton(uint8_t skill_id);
	const std::vector<eqt::ui::PendingHotbarButton>& GetPendingHotbarButtons() const;
	void ClearPendingHotbarButtons();
	size_t GetPendingHotbarButtonCount() const;

	const std::map<uint16_t, Entity>& GetEntities() const { return m_entities; }
	uint16_t GetEntityID() const { return m_my_spawn_id; }

	// D20c4: Quit signaling — Application checks this flag
	bool IsQuitRequested() const { return m_quit_requested; }
	void RequestQuit() { m_quit_requested = true; }
	const std::string& GetLastTellSender() const { return m_last_tell_sender; }
	void QueuePacket(uint16_t opcode, EQ::Net::DynamicPacket* packet);

	// Door interaction
	void SendClickDoor(uint8_t door_id, uint32_t item_id = 0);
	const EQT::DoorStateManager& GetDoorStateManager() const { return m_door_state_manager; }

	// World object / Tradeskill interaction
	void SendClickObject(uint32_t drop_id);
	void SendTradeSkillCombine(int16_t container_slot);
	void SendCloseContainer(uint32_t drop_id);

	// Skill-specific packet sends
	void SendApplyPoison(uint32_t inventory_slot);
	const std::map<uint32_t, eqt::WorldObject>& GetWorldObjects() const { return m_world_objects; }
	const eqt::WorldObject* GetWorldObject(uint32_t drop_id) const;
	uint32_t GetActiveTradeskillObjectId() const { return m_active_tradeskill_object_id; }
	void ClearWorldObjects();

	// Group queries (Phase 7.5: read from GameState)
	bool IsInGroup() const;
	bool IsGroupLeader() const;
	int GetGroupMemberCount() const;
	const GroupMember* GetGroupMember(int index) const;
	const std::string& GetGroupLeaderName() const;
	const std::string& GetMyName() const { return m_character; }
	const std::string& GetMyLastName() const { return m_last_name; }

	// Read-only observers of login/world state (T09). Trivial inline
	// returns of existing members -- no new storage, no logic.
	uint32_t GetAccountId() const { return m_dbid; }
	const std::string& GetSessionKey() const { return m_key; }
	bool IsWorldReady() const { return m_world_ready; }
	bool HasSentEnterWorld() const { return m_enter_world_sent; }
	const std::string& GetZoneServerHost() const { return m_zone_server_host; }
	uint16_t GetZoneServerPort() const { return m_zone_server_port; }
	const std::map<uint32_t, WorldServer>& GetWorldServers() const { return m_world_servers; }
	int GetCharacterSelectIndex() const { return m_character_select_index; }
	// T14: unlike the T09 observers above, this one is backed by new
	// storage — occupied slots only, rebuilt on every OP_SendCharInfo.
	const std::vector<CharacterSelectEntry>& GetCharacterSelectList() const { return m_character_select_list; }
	const std::string& GetChatServerHost() const { return m_ucs_host; }
	uint16_t GetChatServerPort() const { return m_ucs_port; }
	const std::string& GetMailKey() const { return m_mail_key; }
	char GetChatConnectionType() const { return m_ucs_connection_type; }

	// Group actions (send packets to server)
	void SendGroupInvite(const std::string& target_name);
	void SendGroupFollow(const std::string& inviter_name);  // Accept invite
	void SendGroupDecline(const std::string& inviter_name); // Decline invite
	void SendGroupDisband();
	void SendLeaveGroup();

	// Pending group invite handling (Phase 7.5: read from GameState)
	bool HasPendingGroupInvite() const;
	const std::string& GetPendingInviterName() const;
	void AcceptGroupInvite();
	void DeclineGroupInvite();

	// Raid actions
	void SendRaidInvite(const std::string& target_name);

	// Pet queries
	// T40: GetPetHpPercent()/GetPetButtonState() removed -- their only
	// caller was Application::syncPetState(), which is now replaced by
	// event-fed pet state via GameStateSyncBridge (PetStatsChanged /
	// PetButtonStateChanged, both published in eq_bridge.cpp /
	// eq_entities.cpp). GetPetSpawnId()/GetPetName()/GetPetLevel()/
	// GetPetEntity() stay -- they still have live callers outside the
	// deleted poll (eq_action_handler.cpp, spell_manager.cpp, eq.cpp).
	bool HasPet() const { return m_pet_spawn_id != 0; }
	uint16_t GetPetSpawnId() const { return m_pet_spawn_id; }
	const Entity* GetPetEntity() const;
	std::string GetPetName() const;
	uint8_t GetPetLevel() const;

	// Pet commands
	void SendPetCommand(EQT::PetCommand command, uint16_t target_id = 0);
	void DismissPet();

	// Movement state and behavior methods
	void SetMovementMode(MovementMode mode);
	void SetPositionState(PositionState state);
	void SendSpawnAppearance(uint16_t type, uint32_t value);
	void SendAnimation(uint8_t animation_id, uint8_t animation_speed = 10);
	void SendJump();
	void SendMovementHistory();
	void Jump();
	void UpdateJump();

	// Swimming methods
	void UpdateWaterState();           // Check BSP region and update water state
	void OnEnterWater();               // Called when entering water
	void OnExitWater();                // Called when exiting water
	float GetSwimSpeed() const;        // Get swim speed based on skill
	WaterState GetWaterState() const { return m_water_state; }
	bool IsSwimming() const { return m_water_state != WaterState::NotInWater; }
	void SetSwimUp(bool up) { m_swim_up = up; }
	void SetSwimDown(bool down) { m_swim_down = down; }

	void StartUpdateLoop();
	void StopUpdateLoop();
	void PerformEmote(uint32_t animation);
	void SetAFK(bool afk);
	void SetAnonymous(bool anon);
	void SetRoleplay(bool rp);
	void StartCampTimer();
	void CancelCamp();
	void UpdateCampTimer();
	bool IsCamping() const;  // Phase 7.8: reads from GameState
	void SendCamp();         // Send OP_Camp to server
	void SendLogout();       // Send OP_Logout to server
	void SendRezzAnswer(bool accept);  // Send OP_RezzAnswer (accept/decline)
	bool HasPendingRezz() const { return m_has_pending_rezz; }
	void SendWhoAllRequest(const std::string& name = "", int lvllow = -1, int lvlhigh = -1,
		int race = -1, int class_ = -1, bool gm = false);  // Send /who request
	void SendInspectRequest(uint32_t target_id);  // Send /inspect request
	// Guild functions
	void SendGuildInvite(const std::string& player_name);
	void SendGuildInviteAccept(bool accept);
	void SendGuildRemove(const std::string& player_name);
	void SendGuildDemote(const std::string& player_name);
	void SendGuildLeader(const std::string& player_name);
	void SendGetGuildMOTD();
	void SendSetGuildMOTD(const std::string& motd);
	bool HasPendingGuildInvite() const { return m_has_pending_guild_invite; }
	// Phase 3: Corpse Management
	void SendCorpseDrag(const std::string& corpse_name);
	void SendCorpseDrop();
	void SendConsiderCorpse(uint32_t corpse_id);
	void SendConfirmDelete(uint32_t corpse_id);
	bool IsDraggingCorpse() const { return m_is_dragging_corpse; }
	// Phase 3: Consent System
	void SendConsent(const std::string& player_name);
	void SendConsentDeny(const std::string& player_name);
	// Phase 3: Combat Targeting
	void SendAssist(uint32_t target_id);
	void SendAssistGroup(uint32_t target_id);
	// Phase 3: Travel System
	void SendBoardBoat(uint32_t boat_id);
	void SendLeaveBoat();
	// Take (true) or release (false) control of the boat we are on.
	// OP_ControlBoat carries no heading and no control type -- see T145.
	void SendControlBoat(bool take_control);
	bool IsOnBoat() const { return m_is_on_boat; }
	// Phase 3: Group Split
	void SendSplit(uint32_t platinum, uint32_t gold, uint32_t silver, uint32_t copper);
	// Phase 3: LFG System
	void SendLFGCommand(bool lfg_on);
	bool IsLFG() const { return m_is_lfg; }
	// Phase 3: Combat Abilities
	void SendShielding(uint32_t target_id);
	// Phase 3: Misc
	void SendSave();
	void SendSaveOnZoneReq();
	void SendPopupResponse(uint32_t popup_id, uint32_t button);
	// Phase 4: Dueling
	void SendDuelRequest(uint32_t target_id);
	void SendDuelAccept(uint32_t target_id);
	void SendDuelDecline(uint32_t target_id);
	bool IsInDuel() const { return m_is_dueling; }
	bool HasPendingDuel() const { return m_has_pending_duel; }
	// T102: accessor for EqActionHandler::duelAccept()/duelDecline() -- the
	// duel_cmd/duelaccept_cmd/dueldecline_cmd handlers being migrated read
	// m_duel_initiator_id directly (same TU); the action layer can't (no
	// friendship), so it needs a plain public getter, same shape as
	// GetCombatManager()/GetTradeManager() above.
	uint32_t GetPendingDuelInitiatorId() const { return m_duel_initiator_id; }
	// Phase 4: Skills
	void SendBindWound(uint32_t target_id);
	void SendTrackTarget(uint32_t target_id);
	// Phase 4: Tradeskills
	void SendRecipesFavorite(uint32_t object_type, const std::vector<uint32_t>& favorites);
	void SendRecipesSearch(uint32_t object_type, const std::string& query, uint32_t mintrivial, uint32_t maxtrivial);
	void SendRecipeDetails(uint32_t recipe_id);
	void SendRecipeAutoCombine(uint32_t object_type, uint32_t recipe_id);
	// Phase 4: Cosmetic
	void SendSurname(const std::string& surname);
	void SendFaceChange(uint8_t haircolor, uint8_t beardcolor, uint8_t eyecolor1, uint8_t eyecolor2,
		uint8_t hairstyle, uint8_t beard, uint8_t face);
	// Phase 4: Misc
	void SendRandom(uint32_t low, uint32_t high);
	void SendFindPerson(uint32_t npc_id);
	void SendRewind();
	void SendYellForHelp();
	void SendReport(const std::string& report_text);
	void SendFriendsWho();
	// Phase 4: GM Commands
	void SendGMZoneRequest(const std::string& charname, uint16_t zone_id);
	void SendGMSummon(const std::string& charname);
	void SendGMGoto(const std::string& charname);
	void SendGMFind(const std::string& charname);
	void SendGMKick(const std::string& charname);
	void SendGMKill(const std::string& charname);
	void SendGMHideMe(bool hide);
	void SendGMEmoteZone(const std::string& text);
	void SendGMLastName(const std::string& charname, const std::string& lastname);
	// Phase 4: Petitions
	void SendPetition(const std::string& text);
	void SendPetitionDelete(uint32_t petition_id);
	void SetSneak(bool sneak);
	float GetMovementSpeed() const;

	// State getters (Phase 7.8: read from GameState)
	bool IsAFK() const;
	bool IsAnonymous() const;
	bool IsRoleplay() const;

	// Additional public methods
	uint16_t GetMySpawnID() const { return m_my_spawn_id; }
	uint16_t GetCurrentTargetId() const { return m_current_target_id; }
	void SetCurrentTargetId(uint16_t id) { m_current_target_id = id; }

	// T133: populated from the player's own PlayerProfile packet
	// (ZoneProcessPlayerProfile, eq.cpp). 0 means "not in a guild" -- every
	// SendGuild*() sender in eq_guild.cpp guards on that, correctly.
	uint32_t GetGuildID() const { return m_guild_id; }

	// T148: "GM mode was ON at login", NOT "this account has GM privileges".
	//
	// Populated once, from PlayerProfile.gm at offset 13054, in
	// ZoneProcessPlayerProfile (eq.cpp) -- the SAME parse site and the same
	// once-per-login shape as m_guild_id above. Its only server-side writer
	// is Client::SetGM(bool) (eqemu zone/client.cpp:2659), i.e. the `#gm
	// on|off` toggle, which Save()s so the value persists in the character
	// profile.
	//
	// Account status -- what actually authorises GM commands SERVER-side, via
	// Client::Admin() -- never reaches the client. There is no account-status
	// field anywhere in the Titanium protocol. So this flag is the only
	// GM-shaped signal a Titanium client can have, and it is a mode, not a
	// permission. Name any user-facing string accordingly.
	//
	// DELIBERATELY STALE, by Will's decision of 2026-08-19: the check happens
	// ONCE AT LOGIN. A GM who toggles `#gm` mid-session must log out and back
	// in for the client to notice. The AT_GM live-update path (an AT_GM case
	// in ZoneProcessSpawnAppearance, mirroring what T133 did for AT_GUILD_ID)
	// was considered and EXPLICITLY DECLINED -- do not "fix" this by adding
	// one. See refactor/T148_gm_status_gate.md.
	//
	// Offset 13054 is not taken on faith: eqemu's own header comments it
	// `// 0=no, 1=yes (guessing!)`. It was captured off the wire across three
	// logins with the flag toggled between them, with the neighbouring
	// pvp@13052/anon@13053 bytes and birthday@13040 used as controls --
	// review/T148/playerprofile_gm_offset_annotated.txt.
	bool GetGMMode() const { return m_gm_mode; }

	// Game state access (new state management system)
	eqt::state::GameState& GetGameState() { return m_game_state; }
	const eqt::state::GameState& GetGameState() const { return m_game_state; }

	// Bridge for cross-thread game state ↔ renderer communication
	// T46: moved out of the header (was an inline one-liner setting only
	// m_bridge) so it can also wire the four managers' own bridge pointers
	// via wireBridgeToSubsystems() -- previously that wiring happened only
	// in InitGraphics(), so headless runs (which call only setBridge()) left
	// InventoryManager/SpellManager/SkillManager/HotbarModel's bridge
	// pointers null for the whole process. See wireBridgeToSubsystems().
	void setBridge(eqt::bridge::GameStateBridge* bridge);
	void ProcessBridgeIntents();  // Drain and process intents from the renderer (D14+)
	void ProcessBridgeEvents();   // Drain and apply events to the renderer (D14+)

	// Keyboard control methods
	void StartMoveForward();
	void StartMoveBackward();
	void StartTurnLeft();
	void StartTurnRight();
	void StopMoveForward();
	void StopMoveBackward();
	void StopTurnLeft();
	void StopTurnRight();
	void UpdateKeyboardMovement();

	// Character stat getters (Phase 7.2 - read from GameState)
	uint8_t GetLevel() const;
	uint32_t GetClass() const;
	uint32_t GetRace() const;
	uint32_t GetGender() const;
	uint32_t GetSTR() const;
	uint32_t GetSTA() const;
	uint32_t GetDEX() const;
	uint32_t GetAGI() const;
	uint32_t GetINT() const;
	uint32_t GetWIS() const;
	uint32_t GetCHA() const;
	uint32_t GetCurrentHP() const;
	uint32_t GetMaxHP() const;
	uint32_t GetCurrentMana() const;
	uint32_t GetMaxMana() const;
	uint32_t GetCurrentEndurance() const;
	uint32_t GetMaxEndurance() const;
	uint32_t GetDeity() const;
	uint32_t GetPlatinum() const;
	uint32_t GetGold() const;
	uint32_t GetSilver() const;
	uint32_t GetCopper() const;
	uint32_t GetBankPlatinum() const;
	uint32_t GetBankGold() const;
	uint32_t GetBankSilver() const;
	uint32_t GetBankCopper() const;
	uint32_t GetPracticePoints() const;
	float GetWeight() const;
	float GetMaxWeight() const;

	// Keyboard state
	bool m_move_forward = false;
	bool m_move_backward = false;
	bool m_turn_left = false;
	bool m_turn_right = false;

	// Friend classes for system access
	friend class CombatManager;
	friend class TradeManager;

	// String database for EQ message lookups (works in all modes)
	bool LoadStringFiles(const std::string& eqClientPath);
	const EQT::StringDatabase& GetStringDatabase() const { return m_string_db; }

	// S04: Pre-allocate spell system at startup (spell DB, buff manager, spell effects, spell type processor)
	// Must be called after SetEQClientPath(). Returns false if spell DB fails to load.
	bool InitializeSpellSystem(const std::string& eqClientPath);

	// Open the WilleqDatabase (SQLite) and set as global instance.
	// Not FATAL if missing — falls back to legacy file loading.
	void OpenDatabase(const std::string& dbPath);

	// P28: game-thread tick, config-path accessors, hotbar persistence, and
	// state-publishing are plain game logic — called unconditionally from
	// Application::updateGameState()/OnGameStateComplete() et al regardless
	// of EQT_HAS_GRAPHICS. Un-guarded (55b101e had folded these into the
	// graphics block along with the genuinely renderer-only methods below).
	// D21a: Split tick — game thread vs render thread
	void GameTick(float deltaTime);        // Game thread: spell/buff updates + intent processing
	void PreRenderTick(float deltaTime);   // Render thread: drain events, apply to renderer
	void PostRenderTick(float deltaTime);  // Game thread: audio sync
	void SetEQClientPath(const std::string& path);
	void SetRegionMapsPath(const std::string& path) { m_region_maps_path = path; }
	const std::string& GetEQClientPath() const { return m_eq_client_path; }
	void SetConfigPath(const std::string& path) { m_config_path = path; }
	const std::string& GetConfigPath() const { return m_config_path; }
	void SaveHotbarConfig();  // Save hotbar assignments to config file
	void LoadHotbarConfig();  // Load hotbar assignments from config file
	void PublishFullStateSnapshot(); // U00: Push all current state through bridge for new UI
	// U07b: Standalone hotbar model
	eqt::HotbarModel* GetHotbarModel() { return m_hotbar_model.get(); }
	// Phase 7.3: Zone accessors read from GameState
	const std::string& GetCurrentZoneName() const { return m_game_state.world().zoneName(); }
	void GetTimeOfDay(uint8_t& hour, uint8_t& minute) const { hour = m_game_state.world().timeHour(); minute = m_game_state.world().timeMinute(); }
	void OnGraphicsMovement(const eqt::events::PlayerPositionChanged& update);  // S2: takes the movement intent directly (name predates this being a plain bridge-intent handler)
	void UpdateInventoryStats();  // Update inventory window with current stats (base + equipment)

#ifdef EQT_HAS_GRAPHICS
	// Graphics renderer methods — genuinely require the renderer/loading-thread
	// lifecycle and stay gated.
	bool InitGraphics(int width, int height,
	                  EQT::Graphics::IrrlichtRenderer* renderer,
	                  eqt::bridge::GameStateBridge* bridge);
	void ShutdownGraphics();
	eqt::ZoneLoadSnapshot CreateZoneLoadSnapshot() const;  // D20c3: snapshot for loading thread
	void SetConstrainedPreset(EQT::Graphics::ConstrainedRenderingPreset preset) { m_constrained_preset = preset; }
	EQT::Graphics::ConstrainedRenderingPreset GetConstrainedPreset() const { return m_constrained_preset; }
	void SetConstrainedConfig(const EQT::Graphics::ConstrainedRendererConfig& config);
	std::optional<EQT::Graphics::ConstrainedRendererConfig> GetConstrainedConfig() const { return m_constrained_config; }
	// D20e3: GetRenderer() removed — EverQuest no longer holds a renderer pointer
	void SetPlayerGraphicsEntityPending(bool v) { m_player_graphics_entity_pending = v; }
	// D20e2: Application owns loading status; EverQuest signals it via this pointer
	void SetLoadingStatus(EQT::Graphics::LoadingStatus* status) { m_loading_status_ptr = status; }
	// D20e2: Flag for Application to detect re-zone loading requests
	bool ConsumeZoneLoadRequest() { bool v = m_zone_load_requested; m_zone_load_requested = false; return v; }
	// U07c3: SetWindowManager removed — old UI deleted
	// D20g: SetRDPServer removed — RDP audio setup moved to Application
	// D20f2: Typed BSP tree for water detection / zone lines
	// P17: tags the tree with the zone name that was current when it was set,
	// so CreateZoneLoadSnapshot() can detect a stale (previous-zone) tree
	// left over from the graphicsCompleteReady finalization racing a
	// same/next-iteration re-zone (see P17_eviction_hygiene.md Review --
	// "stale-zone-name-vs-position snapshot race").
	void SetZoneBspTree(std::shared_ptr<EQT::Graphics::BspTree> bsp) {
		m_zone_bsp_tree = std::move(bsp);
		m_zone_bsp_tree_zone_name = m_zone_bsp_tree ? m_current_zone_name : std::string();
	}
	// D20e2: LoadZoneGraphics moved to Application
#endif

private:
	// Game state (new state management system)
	// This contains the central game state that can be shared across modes and renderers.
	// During the transition period, both m_game_state and the legacy member variables
	// exist. New code should use m_game_state; existing code is gradually migrated.
	eqt::state::GameState m_game_state;

	// Bridge for cross-thread game state → renderer communication (Phase 2+)
	eqt::bridge::GameStateBridge* m_bridge = nullptr;

	// T46: assigns m_bridge and propagates the same pointer (including
	// null) to the four managers' own bridge pointers
	// (InventoryManager/SpellManager/SkillManager/HotbarModel), each of
	// which is a separate member from m_bridge. Called from both
	// setBridge() (the headless path) and InitGraphics() (the graphics
	// path), so the managers get wired whichever path sets the bridge.
	// Plain pointer assignment underneath, so calling it twice with the
	// same pointer (graphics mode: InitGraphics() after Application's own
	// setBridge()-equivalent construction) is a harmless no-op.
	void wireBridgeToSubsystems(eqt::bridge::GameStateBridge* bridge);

	// D20f2: Typed BSP tree for water detection, zone lines, collision
	std::shared_ptr<EQT::Graphics::BspTree> m_zone_bsp_tree;
	// P17: zone name m_zone_bsp_tree was captured for (see SetZoneBspTree).
	// Empty whenever m_zone_bsp_tree is null. Used by CreateZoneLoadSnapshot()
	// to detect a stale tree that outlived a CleanupZone() reset.
	std::string m_zone_bsp_tree_zone_name;

	// Zone S3D/WLD sources — opened/parsed once at game state layer, shared with sequential loader
	std::shared_ptr<EQT::Graphics::PfsArchive> m_zone_archive;
	std::shared_ptr<EQT::Graphics::WldLoader> m_zone_wld;

	// Utility functions
	static void DumpPacket(const std::string &prefix, uint16_t opcode, const EQ::Net::Packet &p);
	static void DumpPacket(const std::string &prefix, uint16_t opcode, const uint8_t *data, size_t size);
	static std::string GetOpcodeName(uint16_t opcode);
	std::string GetStringMessage(uint32_t string_id);
	std::string GetFormattedStringMessage(uint32_t string_id, const std::vector<std::string>& args);
	static std::string GetChatTypeName(uint32_t chat_type);
	static std::string GetClassName(uint32_t class_id);
	static std::string GetRaceName(uint32_t race_id);
	static std::string GetDeityName(uint32_t deity_id);
	static std::string GetBodyTypeName(uint8_t bodytype);
	static std::string GetEquipSlotName(int slot);
	static std::string GetNpcTypeName(uint8_t npc_type);

	// Phase 7.4: Entity sync helpers
	void SyncEntityToGameState(const Entity& entity);
	void RemoveEntityFromGameState(uint16_t spawnId);

	// Phase 7.5: Group sync helper
	void SyncGroupMemberToGameState(int index, const GroupMember& member);

	// Login
	void LoginOnNewConnection(std::shared_ptr<EQ::Net::DaybreakConnection> connection);
	void LoginOnStatusChangeReconnectEnabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to);
	void LoginOnStatusChangeReconnectDisabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to);
	void LoginOnPacketRecv(std::shared_ptr<EQ::Net::DaybreakConnection> conn, const EQ::Net::Packet &p);
	// Opcode switch shared by LoginOnPacketRecv and the ReplayLoginPacket test seam.
	void DispatchLoginPacket(const EQ::Net::Packet &p);

	void LoginSendSessionReady();
	void LoginSendLogin();
	void LoginSendServerRequest();
	void LoginSendPlayRequest(uint32_t id);
	void LoginProcessLoginResponse(const EQ::Net::Packet &p);
	void LoginProcessServerPacketList(const EQ::Net::Packet &p);
	void LoginProcessServerPlayResponse(const EQ::Net::Packet &p);

	void LoginDisableReconnect();

	std::unique_ptr<EQ::Net::DaybreakConnectionManager> m_login_connection_manager;
	std::shared_ptr<EQ::Net::DaybreakConnection> m_login_connection;
	std::map<uint32_t, WorldServer> m_world_servers;

	// World
	void ConnectToWorld(const std::string& world_address);

	void WorldOnNewConnection(std::shared_ptr<EQ::Net::DaybreakConnection> connection);
	void WorldOnStatusChangeReconnectEnabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to);
	void WorldOnStatusChangeReconnectDisabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to);
	void WorldOnPacketRecv(std::shared_ptr<EQ::Net::DaybreakConnection> conn, const EQ::Net::Packet &p);
	// Opcode switch shared by WorldOnPacketRecv and the ReplayWorldPacket test seam.
	void DispatchWorldPacket(const EQ::Net::Packet &p);

	void WorldSendSessionReady();
	void WorldSendClientAuth();
	void WorldSendEnterWorld(const std::string &character);
	void WorldSendApproveWorld();
	void WorldSendWorldClientCRC();
	void WorldSendWorldClientReady();
	void WorldSendWorldComplete();

	void WorldProcessCharacterSelect(const EQ::Net::Packet &p);
	void WorldProcessGuildsList(const EQ::Net::Packet &p);
	void WorldProcessLogServer(const EQ::Net::Packet &p);
	void WorldProcessApproveWorld(const EQ::Net::Packet &p);
	void WorldProcessEnterWorld(const EQ::Net::Packet &p);
	void WorldProcessPostEnterWorld(const EQ::Net::Packet &p);
	void WorldProcessExpansionInfo(const EQ::Net::Packet &p);
	void WorldProcessMOTD(const EQ::Net::Packet &p);
	void WorldProcessSetChatServer(const EQ::Net::Packet &p);
	void WorldProcessZoneServerInfo(const EQ::Net::Packet &p);

	std::unique_ptr<EQ::Net::DaybreakConnectionManager> m_world_connection_manager;
	std::shared_ptr<EQ::Net::DaybreakConnection> m_world_connection;

	// Variables
	std::string m_host;
	int m_port;
	std::string m_user;
	std::string m_pass;
	std::string m_server;
	std::string m_character;
	std::string m_last_name;

	std::string m_key;
	uint32_t m_dbid;

	// Debug level
	static int s_debug_level;

	// Sequence numbers for packets
	uint32_t m_login_sequence = 2;

	// World server state
	bool m_world_ready = false;
	bool m_enter_world_sent = false;
	std::string m_zone_server_host;
	uint16_t m_zone_server_port = 0;

	// World server state for reconnection
	std::string m_world_server_host;

	// Zone server connection
	void ConnectToZone();
	void ZoneOnNewConnection(std::shared_ptr<EQ::Net::DaybreakConnection> connection);
	void ZoneOnStatusChangeReconnectEnabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to);
	void ZoneOnStatusChangeReconnectDisabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to);
	void ZoneOnPacketRecv(std::shared_ptr<EQ::Net::DaybreakConnection> conn, const EQ::Net::Packet &p);
	// Opcode switch shared by ZoneOnPacketRecv and the ReplayZonePacket test seam.
	void DispatchZonePacket(const EQ::Net::Packet &p);

	void ZoneSendStreamIdentify();
	void ZoneSendAckPacket();
	void ZoneSendSessionReady();
	void ZoneSendZoneEntry();
	void ZoneSendReqNewZone();
	void ZoneSendSendAATable();
	void ZoneSendUpdateAA();
	void ZoneSendSendTributes();
	void ZoneSendRequestGuildTributes();
	void ZoneSendSpawnAppearance();
	void ZoneSendReqClientSpawn();
	void ZoneSendSendExpZonein();
	void ZoneSendSetServerFilter();
	void ZoneSendClientReady();
	void ZoneSendChannelMessage(const std::string &message, ChatChannelType channel, const std::string &target = "");

	void ZoneProcessNewZone(const EQ::Net::Packet &p);
	void ZoneProcessPlayerProfile(const EQ::Net::Packet &p);
	void ZoneProcessCharInventory(const EQ::Net::Packet &p);
	void ZoneProcessZoneSpawns(const EQ::Net::Packet &p);
	void ZoneProcessTimeOfDay(const EQ::Net::Packet &p);
	void ZoneProcessTributeUpdate(const EQ::Net::Packet &p);
	void ZoneProcessTributeTimer(const EQ::Net::Packet &p);
	void ZoneProcessWeather(const EQ::Net::Packet &p);
	void ZoneProcessSendAATable(const EQ::Net::Packet &p);
	void ZoneProcessRespondAA(const EQ::Net::Packet &p);
	void ZoneProcessTributeInfo(const EQ::Net::Packet &p);
	void ZoneProcessSendGuildTributes(const EQ::Net::Packet &p);
	void ZoneProcessSpawnDoor(const EQ::Net::Packet &p);
	void ZoneProcessGroundSpawn(const EQ::Net::Packet &p);
	void ZoneProcessClickObjectAction(const EQ::Net::Packet &p);
	void ZoneProcessTradeSkillCombine(const EQ::Net::Packet &p);
	void ZoneProcessApplyPoison(const EQ::Net::Packet &p);
	void ZoneProcessTrack(const EQ::Net::Packet &p);
	void ZoneProcessSendZonepoints(const EQ::Net::Packet &p);
	void ZoneProcessSendAAStats(const EQ::Net::Packet &p);
	void ZoneProcessSendExpZonein(const EQ::Net::Packet &p);
	void ZoneProcessWorldObjectsSent(const EQ::Net::Packet &p);
	void ZoneProcessSpawnAppearance(const EQ::Net::Packet &p);
	void ZoneProcessEmote(const EQ::Net::Packet &p);
	void ZoneProcessExpUpdate(const EQ::Net::Packet &p);
	void ZoneProcessRaidUpdate(const EQ::Net::Packet &p);
	void ZoneProcessGuildMOTD(const EQ::Net::Packet &p);
	void ZoneProcessNewSpawn(const EQ::Net::Packet &p);
	void ZoneProcessClientUpdate(const EQ::Net::Packet &p);
	void ZoneProcessDeleteSpawn(const EQ::Net::Packet &p);
	void ZoneProcessMobHealth(const EQ::Net::Packet &p);
	void ZoneProcessHPUpdate(const EQ::Net::Packet &p);
	void ZoneProcessChannelMessage(const EQ::Net::Packet &p);
	void ZoneProcessWearChange(const EQ::Net::Packet &p);
	void UpdatePlayerAppearanceFromInventory();
	void ZoneProcessIllusion(const EQ::Net::Packet &p);
	void ZoneProcessMoveDoor(const EQ::Net::Packet &p);
	void ZoneProcessCompletedTasks(const EQ::Net::Packet &p);
	void ZoneProcessDzCompass(const EQ::Net::Packet &p);
	void ZoneProcessDzExpeditionLockoutTimers(const EQ::Net::Packet &p);
	void ZoneProcessBeginCast(const EQ::Net::Packet &p);
	void ZoneProcessManaChange(const EQ::Net::Packet &p);
	void ZoneProcessBuff(const EQ::Net::Packet &p);
	void ZoneProcessColoredText(const EQ::Net::Packet &p);
	void ZoneProcessFormattedMessage(const EQ::Net::Packet &p);
	void ZoneProcessSimpleMessage(const EQ::Net::Packet &p);
	void ZoneProcessPlayerStateAdd(const EQ::Net::Packet &p);
	void ZoneProcessDeath(const EQ::Net::Packet &p);
	void ZoneProcessPlayerStateRemove(const EQ::Net::Packet &p);
	void ZoneProcessStamina(const EQ::Net::Packet &p);
	void ZoneProcessZonePlayerToBind(const EQ::Net::Packet &p);
	void ZoneProcessLevelUpdate(const EQ::Net::Packet &p);
	void ZoneProcessZoneChange(const EQ::Net::Packet &p);
	void ZoneProcessRequestClientZoneChange(const EQ::Net::Packet &p);
	void ZoneProcessLogoutReply(const EQ::Net::Packet &p);
	void ZoneProcessRezzRequest(const EQ::Net::Packet &p);
	void ZoneProcessRezzComplete(const EQ::Net::Packet &p);
	void ZoneProcessWhoAllResponse(const EQ::Net::Packet &p);
	void ZoneProcessInspectRequest(const EQ::Net::Packet &p);
	void ZoneProcessInspectAnswer(const EQ::Net::Packet &p);
	// Guild packet handlers
	void ZoneProcessGuildInvite(const EQ::Net::Packet &p);
	void ZoneProcessGuildMOTDReply(const EQ::Net::Packet &p);
	void ZoneProcessGuildMemberUpdate(const EQ::Net::Packet &p);
	void ZoneProcessGuildMemberAdd(const EQ::Net::Packet &p);
	// Phase 3 packet handlers
	void ZoneProcessConsentResponse(const EQ::Net::Packet &p);
	void ZoneProcessDenyResponse(const EQ::Net::Packet &p);
	void ZoneProcessEnvDamage(const EQ::Net::Packet &p);
	void ZoneProcessDisciplineUpdate(const EQ::Net::Packet &p);
	void ZoneProcessDisciplineTimer(const EQ::Net::Packet &p);
	void ZoneProcessBankerChange(const EQ::Net::Packet &p);
	void ZoneProcessClearObject(const EQ::Net::Packet &p);
	void ZoneProcessLFGAppearance(const EQ::Net::Packet &p);
	// Phase 4 packet handlers
	void ZoneProcessDuelRequest(const EQ::Net::Packet &p);
	void ZoneProcessRecipeReply(const EQ::Net::Packet &p);
	void ZoneProcessRecipeAutoCombine(const EQ::Net::Packet &p);
	void ZoneProcessRandomReply(const EQ::Net::Packet &p);
	void ZoneProcessFindPersonReply(const EQ::Net::Packet &p);
	void ZoneProcessCameraEffect(const EQ::Net::Packet &p);
	void ZoneProcessPlayMP3(const EQ::Net::Packet &p);
	void ZoneProcessSound(const EQ::Net::Packet &p);
	void ZoneProcessGMZoneRequest(const EQ::Net::Packet &p);
	void ZoneProcessGMFind(const EQ::Net::Packet &p);
	void ZoneProcessGMSummon(const EQ::Net::Packet &p);
	// Pet packet handlers
	void ZoneProcessPetBuffWindow(const EQ::Net::Packet &p);

	// Zone transition methods
	void RequestZoneChange(uint16_t zone_id, float x, float y, float z, float heading, bool solicited = false);
	void CleanupZone();  // Cleanup current zone state for zone transition
	void DisconnectFromZone();  // Disconnect from current zone server
	void ProcessDeferredZoneChange();  // Process zone change outside of callback context

	// Combat packet handlers
	void ZoneProcessConsider(const EQ::Net::Packet &p);
	void ZoneProcessAction(const EQ::Net::Packet &p);
	void ZoneProcessDamage(const EQ::Net::Packet &p);
	void ZoneProcessMoneyOnCorpse(const EQ::Net::Packet &p);
	void ZoneProcessLootItem(const EQ::Net::Packet &p);

	std::unique_ptr<EQ::Net::DaybreakConnectionManager> m_zone_connection_manager;
	std::shared_ptr<EQ::Net::DaybreakConnection> m_zone_connection;

	// Safe packet sending helper
	bool SafeQueueZonePacket(EQ::Net::Packet &p, int stream = 0, bool reliable = true);

	// Batch R: incremented whenever a Zone packet sender no-ops because
	// m_zone_connection is null (replay/test mode has no live connection).
	uint64_t m_replay_suppressed_sends = 0;

	// T88 test seam (see Test_SetBypassSendGuardForSeam / Test_GetLastComputedPositionUpdate).
	bool m_test_bypass_send_guard = false;
	TestSentPositionUpdate m_test_last_sent_update;

	// Loading phase tracking (covers entire pre-gameplay process)
	LoadingPhase m_loading_phase = LoadingPhase::DISCONNECTED;
	const char* m_loading_status_text = "";

	// Zone state
	bool m_zone_connected = false;
	bool m_zone_session_established = false;
	bool m_zone_entry_sent = false;
	bool m_weather_received = false;
	bool m_req_new_zone_sent = false;

	// Time of day (from server)
	uint8_t m_time_hour = 12;
	uint8_t m_time_minute = 0;
	uint8_t m_time_day = 1;
	uint8_t m_time_month = 1;
	uint16_t m_time_year = 3100;
	bool m_new_zone_received = false;
	bool m_aa_table_sent = false;
	bool m_update_aa_sent = false;
	bool m_tributes_sent = false;
	bool m_guild_tributes_sent = false;
	bool m_req_client_spawn_sent = false;
	bool m_spawn_appearance_sent = false;
	bool m_exp_zonein_sent = false;
	bool m_send_exp_zonein_received = false;
	bool m_server_filter_sent = false;
	bool m_client_ready_sent = false;
	bool m_client_spawned = false;
	uint32_t m_zone_sequence = 2;
	int m_aa_table_count = 0;
	int m_tribute_count = 0;
	int m_guild_tribute_count = 0;

	// Entity tracking
	std::map<uint16_t, Entity> m_entities;
	uint16_t m_my_spawn_id = 0;
	bool m_player_graphics_entity_pending = false;  // True when player entity needs to be created on renderer after zoning

	// Target tracking (server-authoritative — renderer is a consumer, not the source of truth)
	uint16_t m_current_target_id = 0;

	// Pet tracking
	uint16_t m_pet_spawn_id = 0;                                      // Our pet's spawn ID (0 = no pet)
	bool m_pet_button_states[EQT::PET_BUTTON_COUNT] = {};             // Current button states

	// String database for EQ message lookups (works in all modes)
	EQT::StringDatabase m_string_db;

	// S06: Door game state (replaces std::map<uint8_t, Door> m_doors)
	EQT::DoorStateManager m_door_state_manager;
	std::set<uint8_t> m_pending_door_clicks;  // Doors clicked by user, awaiting server response

	// World object tracking (forges, looms, groundspawns, etc.)
	std::map<uint32_t, eqt::WorldObject> m_world_objects;
	uint32_t m_active_tradeskill_object_id = 0;  // Currently open tradeskill container (0 = none)

	int m_character_select_index = -1;
	std::vector<CharacterSelectEntry> m_character_select_list;

	// Character stats
	uint8_t m_level = 1;
	uint32_t m_exp = 0;          // Current experience points
	uint32_t m_exp_aa = 0;       // Current AA experience points
	uint32_t m_class = 0;
	uint32_t m_race = 0;
	uint32_t m_gender = 0;
	uint32_t m_deity = 0;
	uint32_t m_cur_hp = 0;
	uint32_t m_max_hp = 0;
	uint32_t m_mana = 0;
	uint32_t m_max_mana = 0;
	uint32_t m_endurance = 0;
	uint32_t m_max_endurance = 0;
	uint32_t m_STR = 0;
	uint32_t m_STA = 0;
	uint32_t m_CHA = 0;
	uint32_t m_DEX = 0;
	uint32_t m_INT = 0;
	uint32_t m_AGI = 0;
	uint32_t m_WIS = 0;

	// Currency
	uint32_t m_platinum = 0;
	uint32_t m_gold = 0;
	uint32_t m_silver = 0;
	uint32_t m_copper = 0;

	// Bank currency
	uint32_t m_bank_platinum = 0;
	uint32_t m_bank_gold = 0;
	uint32_t m_bank_silver = 0;
	uint32_t m_bank_copper = 0;

	// Training/Practice points
	uint32_t m_practice_points = 0;

	// Weight
	float m_weight = 0.0f;
	float m_max_weight = 0.0f;

	// Bind point (respawn location on death)
	uint32_t m_bind_zone_id = 0;
	float m_bind_x = 0.0f;
	float m_bind_y = 0.0f;
	float m_bind_z = 0.0f;
	float m_bind_heading = 0.0f;

	// Movement and position tracking
	float m_x = 0.0f;
	float m_y = 0.0f;
	float m_z = 0.0f;
	float m_heading = 0.0f;
	float m_size = 6.0f;
	int16_t m_animation = 0;  // Signed: negative = play animation in reverse
	uint32_t m_movement_sequence = 0;
	bool m_is_moving = false;

	// Movement target
	float m_target_x = 0.0f;
	float m_target_y = 0.0f;
	float m_target_z = 0.0f;
	float m_move_speed = 48.5f;
	uint32_t m_last_movement_update = 0;
	std::chrono::steady_clock::time_point m_last_position_update_time;

	// Follow target
	std::string m_follow_target;
	float m_follow_distance = 10.0f;

	// Combat movement
	std::string m_combat_target;
	float m_combat_stop_distance = 0.0f;
	bool m_in_combat_movement = false;
	std::chrono::steady_clock::time_point m_last_combat_movement_update;

	// Last slain entity (for SimpleMessage "You have slain %1!" substitution)
	std::string m_last_slain_entity_name;

	// Pathfinding
	std::unique_ptr<IPathfinder> m_pathfinder;
	std::string m_current_zone_name;
	uint16_t m_current_zone_id = 0;

	// Zone environment data from NewZone packet
	uint8_t m_zone_sky_type = 0;           // Sky type for rendering (0-255)
	uint8_t m_zone_type = 0;               // Zone type (0=outdoor, 1=dungeon, etc.)
	uint8_t m_zone_fog_red[4] = {0};       // Fog colors for 4 fog ranges
	uint8_t m_zone_fog_green[4] = {0};
	uint8_t m_zone_fog_blue[4] = {0};
	float m_zone_fog_minclip[4] = {0};     // Fog min clip distances
	float m_zone_fog_maxclip[4] = {0};     // Fog max clip distances
	float m_safe_x = 0, m_safe_y = 0, m_safe_z = 0;  // Zone safe coordinates (client coords)

	std::vector<glm::vec3> m_current_path;
	size_t m_current_path_index = 0;

	// Update loop
	std::thread m_update_thread;
	std::atomic<bool> m_update_running{false};
	std::thread::id m_main_thread_id;
	bool m_use_pathfinding = true;
	std::string m_navmesh_path;

	// R01: Session recorder (null unless --record-session was passed)
	std::unique_ptr<EQT::Net::SessionRecorder> m_session_recorder;

	// Map for Z-height calculations. shared_ptr<const HCMap> (P35): the
	// renderer/SimulationWorker boundary holds copies of this same
	// shared_ptr (via CollisionMapChangedData / SimulationZoneData::hcMap),
	// so a zone-change reset() here only drops the game thread's
	// reference -- the object survives until every renderer-side holder
	// (collisionMap_, SimulationWorker's zoneData_.hcMap) also releases
	// it, closing the P34-adjacent CRITICAL UAF where the game thread
	// freed the old HCMap out from under the SimulationWorker thread
	// mid-zone-transition. HCMap is immutable zone data (all read paths
	// are const methods) so const-ness costs nothing on the game-thread
	// side.
	std::shared_ptr<const HCMap> m_zone_map;

	// Zone lines for zone transitions
	std::unique_ptr<EQT::ZoneLines> m_zone_lines;

	// Zone line detection state
	bool m_zoning_enabled = false;                                   // Whether zone line detection triggers zoning (Ctrl+Z toggles)
	bool m_zone_line_triggered = false;                              // Currently in a zone line
	std::chrono::steady_clock::time_point m_zone_line_trigger_time;  // When zone line was triggered
	float m_last_zone_check_x = 0.0f;                                // Last position checked for zone line
	float m_last_zone_check_y = 0.0f;
	float m_last_zone_check_z = 0.0f;
	uint16_t m_pending_zone_id = 0;                                  // Target zone ID for pending transition
	float m_pending_zone_x = 0.0f;                                   // Target coordinates
	float m_pending_zone_y = 0.0f;
	float m_pending_zone_z = 0.0f;
	float m_pending_zone_heading = 0.0f;
	bool m_zone_change_requested = false;                            // Waiting for server zone change response
	bool m_zone_change_approved = false;                             // Zone change approved, needs deferred processing

	// H03: server confirmed our OP_Logout (ZoneProcessLogoutReply) — DisconnectFromZone()
	// must not run synchronously from inside that packet handler (it resets
	// m_zone_connection_manager, destroying the DaybreakConnectionManager whose Tick()
	// is still on the call stack — a use-after-free). Deferred the same way as
	// m_zone_change_approved: set here, processed at the top of UpdateMovement().
	bool m_pending_logout_disconnect = false;

	// Combat manager
	std::unique_ptr<CombatManager> m_combat_manager;

	// Trade manager
	std::unique_ptr<TradeManager> m_trade_manager;

	// Central data database (SQLite)
	std::unique_ptr<EQT::WilleqDatabase> m_willeq_db;

	// Spell manager
	std::unique_ptr<EQ::SpellManager> m_spell_manager;

	// Buff manager
	std::unique_ptr<EQ::BuffManager> m_buff_manager;

	// Spell effects processor
	std::unique_ptr<EQ::SpellEffects> m_spell_effects;

	// Spell type processor (handles targeting and multi-target spells)
	std::unique_ptr<EQ::SpellTypeProcessor> m_spell_type_processor;

	// Pending spell scribe state (for handling server confirmation)
	uint32_t m_pending_scribe_spell_id = 0;
	uint16_t m_pending_scribe_book_slot = 0;
	int16_t m_pending_scribe_source_slot = -1;

	// Skill manager
	std::unique_ptr<EQ::SkillManager> m_skill_manager;

	// Pending hotbar buttons (stub for future hotbar implementation)
	// P28: eqt::ui::PendingHotbarButton is unconditional now (see above).
	std::vector<eqt::ui::PendingHotbarButton> m_pending_hotbar_buttons;

	// Group state
	bool m_in_group = false;
	bool m_is_group_leader = false;
	std::string m_group_leader_name;
	std::array<GroupMember, MAX_GROUP_MEMBERS> m_group_members;
	int m_group_member_count = 0;

	// Pending group invite
	bool m_has_pending_invite = false;
	std::string m_pending_inviter_name;

	// Chat state - for reply to tell
	std::string m_last_tell_sender;

	// Group helper methods
	void ClearGroup();
	int FindGroupMemberByName(const std::string& name) const;
	void UpdateGroupMemberFromEntity(int index);

	// Group packet handlers
	void ZoneProcessGroupInvite(const EQ::Net::Packet& p);
	void ZoneProcessGroupFollow(const EQ::Net::Packet& p);
	void ZoneProcessGroupUpdate(const EQ::Net::Packet& p);
	void ZoneProcessGroupDisband(const EQ::Net::Packet& p);
	void ZoneProcessGroupCancelInvite(const EQ::Net::Packet& p);

	// Movement history for anti-cheat
	struct MovementHistoryEntry {
		float x, y, z;
		uint8_t type;
		uint32_t timestamp;
	};
	std::deque<MovementHistoryEntry> m_movement_history;
	uint32_t m_last_movement_history_send = 0;
	std::string m_maps_path;

	// Movement and position state
	MovementMode m_movement_mode = MOVE_MODE_RUN;
	PositionState m_position_state = POS_STANDING;
	bool m_is_sneaking = false;
	bool m_is_afk = false;
	bool m_is_anonymous = false;
	bool m_is_roleplay = false;
	bool m_is_jumping = false;
	float m_jump_start_z = 0.0f;
	std::chrono::steady_clock::time_point m_jump_start_time;

	// Swimming state
	WaterState m_water_state = WaterState::NotInWater;
	float m_water_surface_z = 0.0f;  // Z coordinate of water surface when swimming
	bool m_swim_up = false;          // Currently pressing swim up key
	bool m_swim_down = false;        // Currently pressing swim down key

	// Camp timer
	bool m_is_camping = false;
	std::chrono::steady_clock::time_point m_camp_start_time;
	static constexpr int CAMP_TIMER_SECONDS = 30;

	// Pending resurrection offer
	bool m_has_pending_rezz = false;
	// Phase 3: Corpse drag state
	bool m_is_dragging_corpse = false;
	std::string m_dragged_corpse_name;
	// Phase 3: Boat state
	bool m_is_on_boat = false;
	uint32_t m_boat_id = 0;
	// Phase 3: LFG state
	bool m_is_lfg = false;
	// Phase 4: Duel state
	bool m_is_dueling = false;
	bool m_has_pending_duel = false;
	uint32_t m_duel_target_id = 0;
	uint32_t m_duel_initiator_id = 0;
	EQT::Resurrect_Struct m_pending_rezz;

	// T148: PlayerProfile.gm @13054 -- "GM mode was on at login". See
	// GetGMMode() for the full contract (login-time only, deliberately
	// stale, NOT an account-privilege flag).
	bool m_gm_mode = false;

	// Guild state
	uint32_t m_guild_id = 0;
	std::string m_guild_name;
	bool m_has_pending_guild_invite = false;
	std::string m_guild_invite_from;
	uint32_t m_guild_invite_id = 0;

	// Movement methods
	void MoveTo(float x, float y, float z);
	void MoveToWithPath(float x, float y, float z);
	void StopMovement();
	float CalculateHeading(float x1, float y1, float x2, float y2);
	float CalculateDistance(float x1, float y1, float z1, float x2, float y2, float z2) const;
	float CalculateDistance2D(float x1, float y1, float x2, float y2) const;

	// Entity helper methods
	Entity* FindEntityByName(const std::string& name);

	// Pathfinding methods
	void LoadPathfinder(const std::string& zone_name);
	bool FindPath(float start_x, float start_y, float start_z, float end_x, float end_y, float end_z);
	void FollowPath();

	// Map methods
	void LoadZoneMap(const std::string& zone_name);
	float GetBestZ(float x, float y, float z);
	void FixZ();

	// Zone source data (S3D/WLD) — opens archive and parses WLD once for game state + graphics
	void LoadZoneSources(const std::string& zone_name);

	// Zone line methods
	void LoadZoneLines(const std::string& zone_name);
	void CheckZoneLine();  // Check if player is in a zone line and handle transition

	// Zone connection flow helpers
	void CheckZoneRequestPhaseComplete();

	// UCS
	std::string m_ucs_host;
	uint16_t m_ucs_port = 0;
	std::string m_mail_key;
	// T13: the connection-type character the world server concatenates onto
	// the mail key. Titanium sends 'B' on OP_SetChatServer (chat) and 'b' on
	// OP_SetChatServer2 (mail) — see emu_versions.h ucsTitaniumChat/Mail. A
	// UCS implementation needs it to tell the two connections apart, so it is
	// stored rather than discarded. 0 = nothing parsed yet.
	char m_ucs_connection_type = 0;

	// World server channel message routing
	void WorldSendChannelMessage(const std::string &channel, const std::string &message, const std::string &target = "");
	void WorldProcessChannelMessage(const EQ::Net::Packet &p);

	// P28: the bulk of this block (eq client/config paths, quit flag, hotbar
	// model, inventory manager, command registry, "Player mode" loot/vendor/
	// bank/trainer state + packet handlers, spawn/pet callbacks) is plain
	// game state and network logic with no renderer dependency — it was
	// folded under EQT_HAS_GRAPHICS by 55b101e alongside the genuinely
	// renderer-only members that remain guarded just below (D20b5 raw
	// pointers, constrained-rendering config, loading-thread signaling,
	// zone-load-request handshake).
	// D20e3: m_renderer removed — Application owns the renderer
	std::string m_eq_client_path;
	std::string m_region_maps_path = "data/region_maps";
	std::string m_config_path;  // Path to per-character config file
	bool m_quit_requested = false;  // D20c4: Application checks this flag
	// U07c3: m_hotbar_window_manager removed — old UI deleted
	std::unique_ptr<eqt::HotbarModel> m_hotbar_model;  // U07b: standalone hotbar
	// D20g: m_rdp_server removed — Application owns RDP setup

	// Inventory manager
	std::unique_ptr<eqt::inventory::InventoryManager> m_inventory_manager;

	// Command registry for chat commands
	std::unique_ptr<eqt::ui::CommandRegistry> m_command_registry;
	// T29: see SetUnrecognizedCommandFallback() above.
	std::function<bool(const std::string&)> m_unrecognized_command_fallback;
	void RegisterCommands();  // Driver: calls per-domain registration methods below, in order (G02)
	// G02: per-domain command registration, split out of the former monolithic
	// RegisterCommands() body. Order between methods (and within each) matches
	// the original registration order exactly — see eq_commands.cpp driver.
	void RegisterSocialCommands();    // Chat/group/trade/guild/corpse/duel/misc social commands (eq_commands.cpp)
	void RegisterMovementCommands();  // Movement/combat/utility-debug/sky commands (eq_commands_movement.cpp)
	void RegisterSpellCommands();     // Spell/skill/hotkey/detail/audio commands (eq_commands_spells.cpp)
	// T136: RegisterVendorCommands() (T48: /vendorlist, /buy, /sell) removed
	// -- migrated to action::CommandProcessor. See RequestVendorToggle()/
	// RequestVendorBuy()/RequestVendorSell() below.
	// T141: RegisterEquipmentCommands() (T66: /equip) removed -- migrated to
	// action::CommandProcessor. See RequestEquip() below.

	// Manual zone load diagnostics helpers
	void runPmemDiagnostics(const std::string& label);
	void runLoadDiagnostics(const std::string& label);

	// Player mode loot state (game-side tracking, D18a)
	uint16_t m_player_looting_corpse_id = 0;  // Corpse being looted in Player mode (0 = not looting)
	std::vector<int16_t> m_pending_loot_slots;  // Corpse slots waiting for server confirmation
	bool m_loot_all_in_progress = false;         // True if we're in the middle of a loot-all operation
	std::vector<int16_t> m_loot_all_remaining_slots;  // Remaining slots to loot in loot-all
	uint16_t m_loot_complete_corpse_id = 0;  // Corpse ID for which looting was completed (ready for deletion)
	struct GameLootItem {
		uint32_t itemId = 0;
		std::string name;
	};
	std::map<int16_t, GameLootItem> m_loot_items;  // slot -> item data (game-side)

	// Player mode vendor state (game-side tracking, D18a)
	uint16_t m_vendor_npc_id = 0;     // NPC being traded with (0 = not trading)
	float m_vendor_sell_rate = 1.0f;  // Price multiplier for this vendor
	std::string m_vendor_name;        // Vendor NPC name
	struct GameVendorItem {
		uint32_t itemId = 0;
		std::string name;
		int32_t price = 0;        // Price in copper
		int32_t quantity = -1;    // -1 = unlimited
	};
	std::map<uint32_t, GameVendorItem> m_vendor_items;  // slot -> item data (game-side)

	// Format copper amount as currency string (e.g. "5p 3g 2s 1c")
	static std::string FormatPrice(int32_t copperAmount);

	// Player mode bank state
	uint16_t m_banker_npc_id = 0;     // Banker NPC being interacted with (0 = bank closed)

	// Player mode trainer state
	uint16_t m_trainer_npc_id = 0;    // Trainer NPC being trained with (0 = not training)
	std::string m_trainer_name;       // Trainer NPC name

	// Graphics callbacks (LoadZoneGraphics is public, others are private)
	void OnSpawnAddedGraphics(const Entity& entity);  // Only creates entity if loading is complete
	void OnSpawnRemovedGraphics(uint16_t spawn_id);
	void OnSpawnMovedGraphics(uint16_t spawn_id, float x, float y, float z, float heading,
	                          float dx, float dy, float dz, int32_t animation);

	// Pet graphics callbacks
	void OnPetCreated(const Entity& pet);
	void OnPetRemoved();
	void OnPetButtonStateChanged(EQT::PetButton button, bool state);

	// Inventory packet handlers
	void ZoneProcessMoveItem(const EQ::Net::Packet& p);
	void ZoneProcessDeleteItem(const EQ::Net::Packet& p);
	void SetupInventoryCallbacks();
	void SendMoveItem(int16_t fromSlot, int16_t toSlot, uint32_t quantity);
	void SendDeleteItem(int16_t slot);

	// Spell scribing from scroll
	void ScribeSpellFromScroll(uint32_t spellId, uint16_t bookSlot, int16_t sourceSlot);

	// Loot packet handlers (Player mode)
	void ZoneProcessLootItemToUI(const EQ::Net::Packet& p);
	void ZoneProcessLootedItemToInventory(const EQ::Net::Packet& p);
	void SetupLootCallbacks();

	// Vendor packet handlers (Player mode)
	void ZoneProcessShopRequest(const EQ::Net::Packet& p);
	void ZoneProcessShopPlayerBuy(const EQ::Net::Packet& p);
	void ZoneProcessShopPlayerSell(const EQ::Net::Packet& p);
	void ZoneProcessShopEndConfirm(const EQ::Net::Packet& p);
	void ZoneProcessVendorItemToUI(const EQ::Net::Packet& p);
	void ZoneProcessMoneyUpdate(const EQ::Net::Packet& p);
	void SetupVendorCallbacks();
	void SetupBankCallbacks();

	// Skill trainer packet handlers and send functions
	void ZoneProcessGMTraining(const EQ::Net::Packet& p);
	void SetupTrainerCallbacks();

	// Trade packet handlers and send functions
	void SetupTradeManagerCallbacks();
	void SetupTradeWindowCallbacks();
	bool ZoneProcessTradePartnerItem(const EQ::Net::Packet& p);
	void SendTradeRequest(const EQT::TradeRequest_Struct& req);
	void SendTradeRequestAck(const EQT::TradeRequestAck_Struct& ack);
	void SendTradeCoins(const EQT::TradeCoins_Struct& coins);
	void SendMoveCoin(const EQT::MoveCoin_Struct& move);
	void SendTradeAcceptClick(const EQT::TradeAcceptClick_Struct& accept);
	void SendCancelTrade(const EQT::CancelTrade_Struct& cancel);

	// Tradeskill container callbacks
	void SetupTradeskillCallbacks();

	// Book/Note reading packet handlers
	void ZoneProcessReadBook(const EQ::Net::Packet& p);
	void SendReadBookRequest(uint8_t window, uint8_t type, const std::string& filename);

public:
	// Save entity data to JSON file for debugging
	void SaveEntityDataToFile(const std::string& filename);

	// Loot window methods (Player mode with graphics)
	void RequestLootCorpse(uint16_t corpseId);
	void LootItemFromCorpse(uint16_t corpseId, int16_t slot, bool autoLoot = false);
	void LootAllFromCorpse(uint16_t corpseId);
	void DestroyAllCorpseLoot(uint16_t corpseId);
	void CloseLootWindow(uint16_t corpseId);

	// Vendor window methods (Player mode with graphics)
	void RequestOpenVendor(uint16_t npcId);
	void BuyFromVendor(uint16_t npcId, uint32_t itemSlot, uint32_t quantity);
	void SellToVendor(uint16_t npcId, uint32_t itemSlot, uint32_t quantity);
	void CloseVendorWindow();
	bool IsVendorWindowOpen() const { return m_vendor_npc_id != 0; }
	uint16_t GetVendorNpcId() const { return m_vendor_npc_id; }

	// T136: minimal read accessors for the migrated /vendorlist command --
	// see refactor/T136_migrate_merchant_family.md ("the state gap") for why
	// these two reads were exposed instead of building a vendor state
	// domain (T103's GameStateSyncBridge precedent doesn't apply: this
	// serves exactly one display command, not a cross-cutting need).
	// GameVendorItem is private, but its members are public (default for a
	// struct) -- callers iterate via structured bindings without ever
	// naming the type, same idiom already used throughout eq_commands.cpp.
	const std::string& GetVendorName() const { return m_vendor_name; }
	const std::map<uint32_t, GameVendorItem>& GetVendorItems() const { return m_vendor_items; }
	static std::string FormatVendorPrice(int32_t copperAmount) { return FormatPrice(copperAmount); }

	// T136: /vendor, /buy, /sell push the SAME eqt::events::Vendor*Intent
	// the renderer's vendor-window gesture pushes (T48's max-fidelity
	// choice, preserved). action::EqActionHandler has no access to the
	// private m_bridge, so these thin wrappers carry the exact guard bodies
	// eq_commands.cpp's old RegisterVendorCommands() lambdas had --
	// stateful guards stay in the layer owning the state (T134); only
	// usage-string/numeric-token parsing moved up to
	// action::CommandProcessor::cmdBuy/cmdSell. Defined in eq_commands.cpp.
	void RequestVendorToggle();
	void RequestVendorBuy(uint32_t slot, uint8_t quantity);
	void RequestVendorSell(uint32_t slot, uint8_t quantity);

	// T141: /equip's thin orchestration entry point -- form dispatch +
	// read-only report formatting only; all legality/mutation/bridge-push
	// logic lives in InventoryManager::moveItem() and friends
	// (include/client/inventory/inventory_manager.h). Defined in
	// eq_commands.cpp. See refactor/T141_equip_extraction.md.
	void RequestEquip(const std::string& args);

	// Bank window methods (Player mode with graphics)
	void OpenBankWindow(uint16_t bankerNpcId = 0);
	void CloseBankWindow();
	bool IsBankWindowOpen() const { return m_banker_npc_id != 0; }
	uint16_t GetBankerNpcId() const { return m_banker_npc_id; }

	// Skill trainer window methods (Player mode with graphics)
	void RequestTrainerWindow(uint16_t npcId);
	void TrainSkill(uint8_t skillId);
	void CloseTrainerWindow();
	bool IsTrainerWindowOpen() const { return m_trainer_npc_id != 0; }
	uint16_t GetTrainerNpcId() const { return m_trainer_npc_id; }

	// T140: four thin wrappers, same rationale as T136's
	// RequestVendorToggle/Buy/Sell -- action::EqActionHandler has no access
	// to EverQuest's private m_bridge/m_combat_manager/m_entities, so each
	// carries its old eq_commands_*.cpp lambda body verbatim (guard logic
	// included, unchanged) rather than exposing those members directly.
	// Defined in eq_commands_movement.cpp (RequestWarp/RequestTimeOfDay,
	// where the old lambdas lived) and eq_commands_spells.cpp
	// (RequestBankToggle/RequestTraining, ditto).
	void RequestWarp(const std::string& args);
	void RequestTimeOfDay(const std::string& args);
	void RequestBankToggle();
	void RequestTraining();

	// Book/Note reading methods (Player mode with graphics)
	void RequestReadBook(const std::string& filename, uint8_t type = 0);

private:
#ifdef EQT_HAS_GRAPHICS
	// Genuinely renderer-lifetime-only state: raw pointers Application wires
	// up around InitGraphics/ShutdownGraphics, the constrained-rendering
	// (NxNxN) startup preset/config, the loading-thread progress signal, and
	// the re-zone handshake flag consumed only from the graphics render loop.
	bool m_graphics_initialized = false;
	EQT::Graphics::ConstrainedRenderingPreset m_constrained_preset = EQT::Graphics::ConstrainedRenderingPreset::OrangePi;  // Constrained rendering preset (startup-only)
	std::optional<EQT::Graphics::ConstrainedRendererConfig> m_constrained_config;  // Custom constrained config (from NxNxN spec)
	// D20e2: Loading thread owned by Application; EverQuest holds pointer to signal it
	EQT::Graphics::LoadingStatus* m_loading_status_ptr = nullptr;
	bool m_zone_load_requested = false;  // Set by re-zone, consumed by Application
#endif

#ifdef WITH_AUDIO
	// Audio manager for sound effects and music
	std::unique_ptr<EQT::Audio::AudioManager> m_audio_manager;
	// Zone audio manager for positioned sound emitters
	std::unique_ptr<EQT::Audio::ZoneAudioManager> m_zone_audio_manager;
	void InitializeAudio();
	void ShutdownAudio();
	// D20g: SetupRDPAudio moved to Application

	// Day/night state for audio (calculated from game time)
	bool m_is_daytime = true;
	void UpdateDayNightState();

	// Audio configuration (applied during InitializeAudio)
	bool m_audio_config_enabled = true;
	float m_audio_config_master_volume = 1.0f;
	float m_audio_config_music_volume = 0.7f;
	float m_audio_config_effects_volume = 1.0f;
	std::string m_audio_config_soundfont;
	std::string m_audio_config_vendor_music = "gl.xmi";
	bool m_audio_config_use_3d_audio = true;

public:
	EQT::Audio::AudioManager* GetAudioManager() { return m_audio_manager.get(); }
	EQT::Audio::ZoneAudioManager* GetZoneAudioManager() { return m_zone_audio_manager.get(); }
	bool IsDaytime() const { return m_is_daytime; }

	// Audio configuration setters (call before InitGraphics/InitializeAudio)
	void SetAudioEnabled(bool enabled) { m_audio_config_enabled = enabled; }
	void SetMasterVolume(float volume) { m_audio_config_master_volume = volume; }
	void SetMusicVolume(float volume) { m_audio_config_music_volume = volume; }
	void SetEffectsVolume(float volume) { m_audio_config_effects_volume = volume; }
	void SetSoundFont(const std::string& path) { m_audio_config_soundfont = path; }
	void SetVendorMusic(const std::string& filename) { m_audio_config_vendor_music = filename; }
	void SetUse3DAudio(bool enabled) { m_audio_config_use_3d_audio = enabled; }

	// Sound effect helpers (client-triggered)
	void PlaySound(uint32_t soundId);
	void PlaySoundAt(uint32_t soundId, float x, float y, float z);
	void PlayCombatSound(bool hit, float x, float y, float z);
	void PlaySpellSound(uint32_t spellId);
	void PlayUISound(uint32_t soundId);
#endif
};
