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
//
// T456 (board B478) enforced that note, which by then had been ignored four
// times. Corpse-ness was derived FIVE times from the same three fields and the
// copies disagreed:
//   * src/client/bridge/bridge_component.cpp (OnSpawnAddedGraphics),
//   * src/client/application_zone_loading.cpp, TWICE (the threaded and the
//     sequential zone-load entity registration),
//     -- all three of which recomputed `npc_type == 2 || npc_type == 3` inline
//        and then fell back to a bare `name.find("corpse")` with NO trailing-id
//        check, so a live npc named "a corpse beetle" was buried on sight and
//        frozen in the d05 death pose;
//   * include/client/state/entity_manager.h `Entity::isAnyCorpse()`, which had
//     no name test at all and so disagreed the other way.
// All four now call IsCorpseByFields(). The wire truth they were all
// approximating is Spawn_Struct's NPC byte (offset 0083): eqemu's
// Corpse::FillSpawnStruct writes `ns->spawn.NPC = 2` unconditionally, for a
// player corpse and an npc corpse alike --
// /mnt/projects/akk-stack/code/zone/corpse.cpp:1809.

// True when `name` is one of eqemu's three corpse name forms. base_name and
// corpse_id are written only on a match; either may be null.
bool ParseCorpseName(const std::string& name,
                     std::string* base_name = nullptr,
                     uint32_t* corpse_id = nullptr);

// T185: build the name eqemu will be using for a corpse, WITHOUT being told.
//
// eqemu does not broadcast a rename for PLAYER corpses -- `NPC::Death` sends
// OP_MobRename for NPC corpses only -- and `Corpse::CalcCorpseName`
// (zone/corpse.cpp:629) carries the reason in a comment: "the client does this
// too, so it's unchangable". Deriving the name locally is the client's job,
// not a workaround for a missing packet.
//
// Mirrors CalcCorpseName exactly:
//   * digits are stripped from the base name first (EntityList::RemoveNumbers,
//     zone/entity.cpp:3259 -- ALL digits, not just trailing ones);
//   * player  -> "<base>'s corpse<id>"   (apostrophe, space)
//   * NPC     -> "<base>`s_corpse<id>"   (backtick, underscore)
// `id` is the ENTITY id, which a player corpse inherits from the player:
// every corpse-creation site does `entity_list.AddCorpse(corpse, GetID())`
// followed by `SetID(0)` on the dying client.
//
// ParseCorpseName is its inverse for the two forms produced here.
std::string MakeCorpseName(const std::string& living_name, uint32_t entity_id,
                           bool is_player);

// T486 (2026-09-14 audit, item 2): the Spawn_Struct NPC byte's corpse rule,
// on its own. eqemu's Corpse::FillSpawnStruct writes 2 for EVERY corpse it
// spawns (player and npc alike); 3 is the documented npc-corpse value other
// servers may send. Both Spawn_Struct parsers (ZoneSpawns and NewSpawn,
// src/client/entities/) set Entity::is_corpse from this at spawn, so a corpse
// that was already lying there when we zoned in carries the flag from its
// first frame -- before this, only a death we witnessed (OP_Death) or an NPC
// corpse's rename (OP_MobRename) ever set it, and the three sites that read
// the bare flag (a corpse-target spell's validateCast, the CorpseDecayStarted
// fade, the corpse-name suffix on an appearance change) saw a live mob.
// IsCorpseByFields() applies the same rule; tests/test_corpse_death_pose.cpp
// fails on a parser that stores npc_type without setting the flag here.
bool IsCorpseNpcType(uint8_t npc_type);

// The predicate, on the three fields any entity representation carries:
//   * the flag, set by the death and rename handlers (a death we witnessed);
//   * the Spawn_Struct NPC field (2 for EVERY corpse eqemu spawns, 3 the
//     documented NPC-corpse value) -- the authoritative bit, and the one that
//     covers a corpse already lying there when we zoned in;
//   * the name, for entities whose npc_type we never saw.
bool IsCorpseByFields(bool is_corpse_flag, uint8_t npc_type, const std::string& name);
