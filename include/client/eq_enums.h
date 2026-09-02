// Free-standing enumerations used across the client, plus the declaration of
// the chat-channel name lookup that is defined in src/client/eq_chat.cpp.
// Lifted verbatim out of the umbrella client header by
// refactor/J16_eq_header_type_extraction.md. Move-only.

#pragma once

#include <cstdint>
#include <string>

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
	// T320: the server's echo of an OUTGOING tell. It printed as "chan14"
	// for T297's entire run because neither name chain knew it existed.
	CHAT_CHANNEL_TELL_ECHO = 14,
	CHAT_CHANNEL_RAID = 15,
	CHAT_CHANNEL_EMOTE = 22
};

// T320: the ONE channel-name table. eq_chat.cpp used to build the name twice
// in ZoneProcessChannelMessage, forty lines apart, from two hand-written
// ternary chains -- and they had drifted: the console [CHAT] chain omitted
// CHAT_CHANNEL_AUCTION (every auction line printed "chan4"), the bridge chain
// had it, and neither knew channel 14. Both consumers now call this; a third
// copy is what tests/test_chat_channel_names.cpp exists to catch.
// Returns "chanN" for any channel with no name, so the number survives.
std::string ChatChannelName(uint32_t channel);

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
