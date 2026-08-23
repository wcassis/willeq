#include "client/corpse_predicate.h"

// T173: moved here verbatim from combat.cpp so the state layer can use the
// same predicate. See corpse_predicate.h for why the field-based form exists.
// The name forms and the id guard are T160's work, unchanged.

namespace {

// The three name forms eqemu can put on the wire. Order does not matter --
// they are mutually exclusive -- but the backtick form is the one a corpse
// that already existed when we zoned in arrives with, because the
// client-side rename in NPC::Death is only broadcast at the moment of death.
constexpr const char* kCorpseNameSeparators[] = {
	"'s corpse",   // Corpse::CalcCorpseName, player corpse (apostrophe+space)
	"'s_corpse",   // NPC corpse after NPC::Death's OP_MobRename
	"`s_corpse",   // Corpse::CalcCorpseName, NPC corpse (backtick+underscore)
};

// Entity ids are 16-bit on the wire; eqemu formats them with %d, so at most
// five digits. Cap generously and reject anything longer rather than letting
// std::stoul throw.
constexpr size_t kMaxCorpseIdDigits = 10;

}  // namespace

bool ParseCorpseName(const std::string& name, std::string* base_name, uint32_t* corpse_id) {
	for (const char* separator : kCorpseNameSeparators) {
		const std::string sep(separator);
		// rfind: the suffix is appended last, so if a mob's own name somehow
		// contained the separator the trailing occurrence is the real one.
		const size_t pos = name.rfind(sep);
		if (pos == std::string::npos) {
			continue;
		}
		const std::string digits = name.substr(pos + sep.size());
		// The id is mandatory. This is the false-positive guard: without it
		// any living mob whose name merely ended in the separator would be
		// buried on sight.
		if (digits.empty() || digits.size() > kMaxCorpseIdDigits) {
			continue;
		}
		if (digits.find_first_not_of("0123456789") != std::string::npos) {
			continue;
		}
		if (base_name) {
			*base_name = name.substr(0, pos);
		}
		if (corpse_id) {
			*corpse_id = static_cast<uint32_t>(std::stoul(digits));
		}
		return true;
	}
	return false;
}

std::string MakeCorpseName(const std::string& living_name, uint32_t entity_id,
                           bool is_player) {
	// EntityList::RemoveNumbers strips EVERY digit, not just a trailing spawn
	// suffix -- "Guard_Rahtiz000" and "a_cave_rat2" both lose all of them.
	// Reproduced rather than approximated: a base name that disagrees with the
	// server's produces a corpse the player cannot reach by typing its name,
	// which is the whole defect this exists to fix.
	std::string base;
	base.reserve(living_name.size());
	for (char c : living_name) {
		if (c < '0' || c > '9') {
			base.push_back(c);
		}
	}
	base += is_player ? "'s corpse" : "`s_corpse";
	base += std::to_string(entity_id);
	return base;
}

bool IsCorpseByFields(bool is_corpse_flag, uint8_t npc_type, const std::string& name) {
	// 1. The flag, set by the HC_OP_Death and HC_OP_MobRename handlers. Only
	//    ever true for a death we personally witnessed this session.
	if (is_corpse_flag) {
		return true;
	}
	// 2. Spawn_Struct NPC field (offset 83). eqemu's Corpse::FillSpawnStruct
	//    (zone/corpse.cpp:1792) sets it to 2 for every corpse it spawns,
	//    player or NPC; 3 is the documented npc-corpse value other servers
	//    may send. This is the authoritative bit, and it is the one that
	//    covers corpses that were already lying there when we zoned in.
	if (npc_type == 2 || npc_type == 3) {
		return true;
	}
	// 3. The name, for entities whose npc_type we never saw (e.g. an entity
	//    only ever named by OP_MobRename).
	return ParseCorpseName(name);
}
