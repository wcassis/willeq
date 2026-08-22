#pragma once

#include <cstdint>
#include <string>

// T173: the corpse predicate itself, extracted from combat.cpp so that BOTH
// entity representations can ask the question without a second copy of the
// answer.
//
// T160 established this predicate and its three name forms; this unit only
// MOVED it. The reason for the move: the state layer's `eqt::state::Entity`
// and the client's `Entity` are different types that carry the same three
// fields, and `EntityManager::findEntityByName` (state layer) now needs the
// predicate to rank a living player above their own corpse. Expressing it on
// the FIELDS lets every struct overload delegate here.
//
// **Do not write a second corpse test.** Two copies drifting apart is exactly
// what T160 was fixing, and the state layer is where a fresh copy would be
// most tempting -- `npcType == 2` alone looks sufficient and silently misses
// every corpse whose npc_type we never saw.

// True when `name` is one of eqemu's three corpse name forms. base_name and
// corpse_id are written only on a match; either may be null.
bool ParseCorpseName(const std::string& name,
                     std::string* base_name = nullptr,
                     uint32_t* corpse_id = nullptr);

// The predicate, on the three fields any entity representation carries:
//   * the flag, set by the death and rename handlers (a death we witnessed);
//   * the Spawn_Struct NPC field (2 for EVERY corpse eqemu spawns, 3 the
//     documented NPC-corpse value) -- the authoritative bit, and the one that
//     covers a corpse already lying there when we zoned in;
//   * the name, for entities whose npc_type we never saw.
bool IsCorpseByFields(bool is_corpse_flag, uint8_t npc_type, const std::string& name);
