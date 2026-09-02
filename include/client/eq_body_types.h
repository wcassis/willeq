#pragma once

// T308: THE single copy of EverQuest's body-type numbering in this client.
//
// The values are transcribed from the server that actually sends them --
// eqemu's common/bodytypes.h (BodyType::Humanoid == 1, ...), the same file
// the zone server uses to stamp Spawn_Struct::bodytype (packet offset 335,
// parsed into Entity::bodytype by eq_entities.cpp). They are NOT invented and
// NOT derived from a race id.
//
// Why a header: before this unit three separate hand-written copies of this
// numbering were live in the client and ALL THREE were wrong --
//   * EverQuest::GetBodyTypeName() (eq_strings.cpp) was off by one for every
//     value (case 0 said "Humanoid", which is really Lycanthrope's slot);
//   * SpellManager::CheckTargetBodyType()'s local BT_* constants
//     (spell/spell_manager.cpp) were off by one the same way;
//   * SpellTypeProcessor (spell/spell_type_processor.cpp) used a third,
//     differently-wrong set (21 for Plant, 22 for Giant).
// T308 corrected only the first; T314 (2026-09-02) killed both spell copies --
// they now share EQ::BodyTypeSatisfiesTargetType()
// (include/client/spell/spell_body_type_target.h), which reads its constants
// from here, and tests/test_spell_body_type_targeting.cpp scans
// src/client/spell/ on every ctest so no fourth copy can reappear there.
// Anything that needs a body type includes this header instead of writing a
// number.
//
// T314 re-checked every value below against eqemu/common/bodytypes.h: they
// agree exactly. Nothing here is a guess.
//
// Body types above 64 make the mob invisible (eqemu's own note).

#include <cstdint>
#include <string>

namespace EQT {
namespace BodyType {

inline constexpr uint8_t Unknown        = 0;   // not a valid eqemu value: "no
                                               // spawn data recorded"
inline constexpr uint8_t Humanoid       = 1;
inline constexpr uint8_t Lycanthrope    = 2;
inline constexpr uint8_t Undead         = 3;
inline constexpr uint8_t Giant          = 4;
inline constexpr uint8_t Construct      = 5;
inline constexpr uint8_t Extraplanar    = 6;
inline constexpr uint8_t Magical        = 7;
inline constexpr uint8_t SummonedUndead = 8;
inline constexpr uint8_t RaidGiant      = 9;
inline constexpr uint8_t RaidColdain    = 10;
inline constexpr uint8_t NoTarget       = 11;
inline constexpr uint8_t Vampire        = 12;
inline constexpr uint8_t AtenHaRa       = 13;
inline constexpr uint8_t GreaterAkheva  = 14;
inline constexpr uint8_t KhatiSha       = 15;
inline constexpr uint8_t Seru           = 16;
inline constexpr uint8_t GriegVeneficus = 17;
inline constexpr uint8_t DrazNurakk     = 18;
inline constexpr uint8_t Zek            = 19;
inline constexpr uint8_t Luggald        = 20;
inline constexpr uint8_t Animal         = 21;
inline constexpr uint8_t Insect         = 22;
inline constexpr uint8_t Monster        = 23;
inline constexpr uint8_t Summoned       = 24;
inline constexpr uint8_t Plant          = 25;
inline constexpr uint8_t Dragon         = 26;
inline constexpr uint8_t Summoned2      = 27;
inline constexpr uint8_t Summoned3      = 28;
inline constexpr uint8_t Dragon2        = 29;
inline constexpr uint8_t VeliousDragon  = 30;
inline constexpr uint8_t Familiar       = 31;
inline constexpr uint8_t Dragon3        = 32;
inline constexpr uint8_t Boxes          = 33;
inline constexpr uint8_t Muramite       = 34;
inline constexpr uint8_t NoTarget2      = 60;
inline constexpr uint8_t SwarmPet       = 63;
inline constexpr uint8_t MonsterSummon  = 64;
inline constexpr uint8_t InvisibleMan   = 66;
inline constexpr uint8_t Special        = 67;

// Display name for a body type id, or "BodyType<n>" for one this build does
// not know. Serves EverQuest::GetBodyTypeName().
std::string Name(uint8_t bodyType);

}  // namespace BodyType
}  // namespace EQT
