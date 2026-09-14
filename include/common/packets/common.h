// Shared wire constants and the colour-tint / texture profile layouts that
// the spawn structures build on.
//
// Wire layouts, extracted verbatim from the umbrella header by
// refactor/J14_packet_structs_split.md. The RULES for this file family live
// in the umbrella, include/common/packet_structs.h -- read them before
// touching a struct or a compile-time size check here.
//
// This header opens and closes its own one-byte packing region; no include
// sits inside it.

#ifndef EQT_PACKETS_COMMON_H
#define EQT_PACKETS_COMMON_H

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace EQT {

#pragma pack(push, 1)


// Constants
static const uint32_t BUFF_COUNT = 25;
static const uint32_t MAX_PP_SKILL = 100;
static const uint32_t MAX_PP_LANGUAGE = 28;
static const uint32_t MAX_PP_AA_ARRAY = 240;
static const uint32_t MAX_PP_DISCIPLINES = 100;
static const uint32_t MAX_PLAYER_TRIBUTES = 5;
static const uint32_t MAX_RECAST_TYPES = 20;
static const uint32_t SPELLBOOK_SIZE = 400;
// T483 (boards B762/B770): the spell gem counts -- the ONE source of truth.
// SPELL_GEM_COUNT is the PROTOCOL count: Titanium's CastingSlot Gem1..Gem9,
// MaxGems = 9 (eqemu common/patches/titanium_limits.h:308-328), and
// PlayerProfile mem_spells[9] at offset 4360 (titanium_structs.h:854). Every
// gem the server can name is STORED at this size, so none is dropped.
static const uint32_t SPELL_GEM_COUNT = 9;
// VISIBLE_SPELL_GEM_COUNT is what the official client SHOWS and OFFERS. Its
// loaded skin (eqclient.ini UISkin=Default) lays out CSPW_Spell0..7 at X=3,
// Y=10+30i, 36x28, in a 48x290 CastSpellWnd, and parks CSPW_Spell8 at
// (-5000,-5000) with no size (uifiles/default/EQUI_CastSpellWnd.xml:32-158).
// eqemu also refuses a cast from slot 8 without AA 1071 Mnemonic Retention
// (zone/client_packet.cpp:4387-4392), which fable does not track. The gem
// panel, its hit test, /cast, /mem, /forget, the gem keys and every
// client-initiated cast use this count; the ninth gem is stored, never offered.
static const uint32_t VISIBLE_SPELL_GEM_COUNT = 8;
static const uint32_t BANDOLIERS_SIZE = 4;
static const uint32_t BANDOLIER_ITEM_COUNT = 4;
static const uint32_t POTION_BELT_SIZE = 4;
static const uint32_t TEXTURE_COUNT = 9;  // materialCount
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

#pragma pack(pop)

} // namespace EQT

#endif // EQT_PACKETS_COMMON_H
