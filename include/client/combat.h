#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include <map>
#include <glm/glm.hpp>

// Forward declarations
class EverQuest;
struct Entity;

// T160: corpse recognition, shared so the two copies of the old
// `name.find("'s_corpse")` literal (CombatManager::IsCorpse and EverQuest's
// HC_OP_MobRename handler) cannot drift apart again. T173 moved the predicate
// and ParseCorpseName to corpse_predicate.h (included below) so the state
// layer can use them too; only this struct overload stayed here.
#include "client/corpse_predicate.h"

bool IsCorpseEntity(const Entity& entity);

// Combat-related enums
enum CombatState {
	COMBAT_STATE_IDLE,
	COMBAT_STATE_ENGAGED,
	COMBAT_STATE_FLEEING,
	COMBAT_STATE_LOOTING,
	COMBAT_STATE_HUNTING,
	COMBAT_STATE_RESTING,
	COMBAT_STATE_SEEKING_GUARD
};

// T26: loot sub-state, nested under CombatState::COMBAT_STATE_LOOTING.
// Replaces four blocking std::this_thread::sleep_for calls that used to sit
// on the game-thread packet-dispatch path (see
// refactor/T26_loot_state_machine.md). Every state advances only from
// CombatManager::UpdateLootState(), ticked unconditionally (graphics or
// headless) from the game thread -- never from CombatManager::Update()
// (which, since T245, is also a game-thread tick from UpdateMovement(),
// but only while IsFullyZonedIn()). Every non-Idle state has both a
// deadline and a corpse-disappeared exit; see CombatManager::CorpseStillPresent().
enum class LootSubState {
	Idle,             // not looting
	AwaitingCorpse,   // auto-hunt only: entered on kill, polling for the
	                  // corpse to spawn; exits on corpse seen or deadline
	AwaitingItems,    // loot request sent; items arrive one per packet;
	                  // exits on item quiescence, hard deadline, or corpse gone
	LootingItems,     // sending LootItem for each staged slot, paced one
	                  // per kInterItemDelayMs tick; exits when drained or
	                  // corpse gone
	Closing,          // brief pause before EndLootRequest, mirrors legacy
	                  // "before closing the window" delay; exits on deadline
	                  // or corpse gone
	ManualOpen        // T268: auto-loot OFF and the window has items -- held
	                  // open for the player (/loot <item>, /lootall,
	                  // /lootclose, or the intents). No deadline: exits only
	                  // on LootAll() (-> LootingItems), a close, or corpse gone
};

enum TargetPriority {
	TARGET_PRIORITY_LOWEST = 0,
	TARGET_PRIORITY_LOW,
	TARGET_PRIORITY_MEDIUM,
	TARGET_PRIORITY_HIGH,
	TARGET_PRIORITY_HIGHEST
};

// Consider colors (faction + level based)
enum HCConsiderColor {
	CON_GREEN = 2,
	CON_LIGHTBLUE = 18,
	CON_BLUE = 4,
	CON_WHITE = 20,
	CON_YELLOW = 15,
	CON_RED = 13,
	CON_GRAY = 6
};

// Spell gem slots
enum SpellSlot {
	SPELL_GEM_1 = 0,
	SPELL_GEM_2,
	SPELL_GEM_3,
	SPELL_GEM_4,
	SPELL_GEM_5,
	SPELL_GEM_6,
	SPELL_GEM_7,
	SPELL_GEM_8,
	SPELL_GEM_9,
	SPELL_GEM_10,
	SPELL_GEM_11,
	SPELL_GEM_12,
	MAX_SPELL_GEMS = 12
};

// Combat actions
enum CombatAction {
	COMBAT_ACTION_ATTACK = 0,
	COMBAT_ACTION_CAST,
	COMBAT_ACTION_HEAL,
	COMBAT_ACTION_BUFF,
	COMBAT_ACTION_FLEE
};

struct CombatTarget {
	uint16_t entity_id;
	std::string name;
	float distance;
	uint8_t hp_percent;
	HCConsiderColor con_color;
	TargetPriority priority;
	bool is_aggro;
	bool has_consider_data;
	uint32_t faction;
	uint32_t con_level;
	int32_t cur_hp;
	int32_t max_hp;
};

struct SpellInfo {
	uint32_t spell_id;
	std::string name;
	uint32_t mana_cost;
	uint32_t cast_time_ms;
	uint32_t recast_time_ms;
	uint32_t range;
	bool is_beneficial;
	bool is_detrimental;
	SpellSlot gem_slot;
	std::chrono::steady_clock::time_point last_cast_time;
};

struct CombatStats {
	uint32_t current_hp;
	uint32_t max_hp;
	uint32_t current_mana;
	uint32_t max_mana;
	uint32_t current_endurance;
	uint32_t max_endurance;
	float hp_percent;
	float mana_percent;
	float endurance_percent;
};

class CombatManager {
public:
	CombatManager(EverQuest* eq);
	~CombatManager();

	// Enable/disable combat manager
	void Enable();
	void Disable();
	bool IsEnabled() const { return m_enabled; }

	// Core combat functions.
	// Update(): the auto-combat tick (target scan, aggro check, combat round,
	// auto-hunt, flee/rest bookkeeping). T245: called every game tick from
	// EverQuest::UpdateMovement() on the GAME thread (it writes
	// m_game_state.combat() via SetTarget/ClearTarget and walks m_entities,
	// so no other thread may call it); paces itself to kUpdateIntervalMs --
	// the cadence the deleted 500 ms m_update_thread used to provide -- and
	// is a no-op in between. No-op unless Enable()d (/combat on).
	void Update();
	static constexpr int kUpdateIntervalMs = 500;
	void SetCombatState(CombatState state);
	CombatState GetCombatState() const { return m_combat_state; }

	// T247: aggro is INFERRED. eqemu never sends its hate list to a client;
	// the official client (and this one) knows an NPC is on it because that
	// NPC's attacks arrive as OP_Damage / OP_Action with target == our spawn
	// id. EverQuest::ZoneProcessDamage/Action call NoteAttackedBy for each
	// such packet from an NPC; the mark lives kAggroMemoryMs from the LAST
	// attack, and GetNearbyHostiles() sets CombatTarget::is_aggro from it,
	// which is what CheckForAggro() (idle + /combat on) engages on.
	void NoteAttackedBy(uint16_t attacker_id);
	bool IsRecentAttacker(uint16_t entity_id) const;
	static constexpr int kAggroMemoryMs = 10000;

	// Targeting
	bool SetTarget(uint16_t entity_id);
	void ClearTarget();
	uint16_t GetTargetId() const { return m_current_target_id; }
	bool HasTarget() const { return m_current_target_id != 0; }
	void ConsiderTarget();

	// T153: which consider opcode the current target takes --
	// HC_OP_ConsiderCorpse for a corpse, HC_OP_Consider otherwise, 0 with no
	// target. Public so the selection is directly assertable: CombatManager's
	// sends go through the generic EverQuest::QueuePacket overload, which
	// no-ops with no observable side effect when there is no zone connection
	// (see tests/combat_manager_test_fixture.h's T23 note), so the packet itself
	// cannot be inspected from a test.
	uint16_t GetConsiderOpcodeForTarget() const;
	void CycleTargets(bool forward = true);

	// Auto attack
	void EnableAutoAttack();
	void DisableAutoAttack();
	// T239: the one toggle rule (on -> off; off -> on only with a target),
	// shared by /aa (EqActionHandler::toggleAutoAttack) and the Combat hotbar
	// slot (EverQuest::ActivateHotbarCommandSlot). Was inlined in the handler.
	void ToggleAutoAttack();
	bool IsAutoAttackEnabled() const { return m_auto_attack_enabled; }

	// Auto fire (ranged)
	void EnableAutoFire();
	void DisableAutoFire();
	void ToggleAutoFire();
	bool IsAutoFireEnabled() const { return m_auto_fire_enabled; }

	// Auto movement during combat
	void EnableAutoMovement() { m_auto_movement_enabled = true; }
	void DisableAutoMovement() { m_auto_movement_enabled = false; }
	bool IsAutoMovementEnabled() const { return m_auto_movement_enabled; }

	// Spell casting: none here. T240 deleted CastSpell()/InterruptCast()/
	// IsCasting()/MemorizeSpell() and the m_spell_gems map they read -- a
	// second gem store the PlayerProfile load never filled, plus a stub that
	// sent OP_CastSpell with slot 0 and a hardcoded 3000 ms cast time. The
	// one casting subsystem is EQ::SpellManager (beginCastFromGem etc.).

	// Combat abilities
	void UseAbility(uint32_t ability_id, uint16_t target_id = 0);
	void Taunt(uint16_t target_id = 0);

	// B900 (#216): the single ranged attack -- one OP_CombatAbility shot with
	// m_atk = slotRange and m_skill picked from the range-slot item's ItemType
	// (bow -> Archery, throwing -> Throwing), shared by /range and the Range
	// Attack key. Distinct from UseAbility (which hard-codes m_atk = 100 for
	// the melee abilities) and from the auto-fire tick. Returns false (sending
	// nothing) when there is no target or no ranged weapon; with no ranged
	// weapon it also prints the official client's eqstr 9150 refusal.
	bool RangedAttack();

	// Situational awareness
	void UpdateCombatStats(const CombatStats& stats);
	bool ShouldFlee() const;
	std::vector<CombatTarget> GetNearbyHostiles(float range = 50.0f);
	std::vector<CombatTarget> GetNearbyAllies(float range = 50.0f);
	CombatTarget* GetHighestPriorityTarget();

	// Fleeing
	void InitiateFlee();
	bool IsFleeing() const { return m_combat_state == COMBAT_STATE_FLEEING; }
	void SetFleeThreshold(float hp_percent) { m_flee_hp_threshold = hp_percent; }

	// Looting. T268: CombatManager is the loot POLICY (when to take what,
	// pacing, when to close); every packet goes through EverQuest's single
	// senders (RequestLootCorpse / LootItemFromCorpse / CloseLootWindow) and
	// the window's item list lives in EverQuest::m_loot_items. The slot
	// vector here is the auto-loot queue only.
	void LootCorpse(uint16_t corpse_id);
	void LootItem(uint32_t item_slot);
	bool LootAll();                 // false when no window is open (nothing sent)
	void CloseLootWindow();
	bool IsLooting() const { return m_combat_state == COMBAT_STATE_LOOTING; }
	// T268: called by EverQuest::CloseLootWindow() (the ONE EndLootRequest
	// sender) and the corpse-despawn path so the machine leaves LOOTING no
	// matter who closed the window. Idempotent.
	void OnLootWindowClosed();

	// T33: called by EverQuest::ZoneProcessDeath() -- the authoritative,
	// server-signalled death event -- right after DisableAutoAttack() for
	// a victim that was our current combat target. This is now the
	// primary door into AwaitingCorpse (see T33_loot_death_event_wiring.md);
	// ProcessCombatRound()'s synthetic hp_percent<=0 poll is a secondary,
	// near-unreachable path kept only because it cannot be proven
	// impossible (see combat.cpp). Gated on auto_loot && auto_hunting,
	// same as the poll always was. No-op if the gate isn't satisfied.
	void NotifyTargetDied(uint16_t victim_id);

	// T26: event-driven loot state machine. Call once per game tick from
	// the unconditional game-thread path (Application::updateGameState,
	// alongside UpdateMovement() -- NOT from Update(), which only ticks
	// while IsFullyZonedIn() and is paced to kUpdateIntervalMs).
	// No-op when not looting.
	void UpdateLootState();
	LootSubState GetLootSubState() const { return m_loot_sub_state; }

	// Configuration
	void SetAggroRadius(float radius) { m_aggro_radius = radius; }
	void EnableAutoLoot(bool enable) { m_auto_loot_enabled = enable; }

	// Auto-hunting
	void SetAutoHunting(bool enable);
	bool IsAutoHunting() const { return m_auto_hunting_enabled; }
	void UpdateAutoHunting();

	// Spell management
	void LoadSpellSet(const std::string& spell_set_name);
	void SaveSpellSet(const std::string& spell_set_name);
	std::vector<SpellInfo> GetMemorizedSpells() const;

	// Consider result processing
	void ProcessConsiderResponse(uint32_t target_id, uint32_t faction, uint32_t level,
		int32_t cur_hp, int32_t max_hp);

	// Hunt target listing
	void ListHuntTargets();

	// Loot window handling. AddLootItem/RemoveLootItem are fed by
	// EverQuest::ZoneProcessLootItemToUI / ZoneProcessLootedItemToInventory
	// (T268: ProcessLootWindow, which had no caller, is gone).
	void AddLootItem(uint32_t slot_id);
	void RemoveLootItem(uint32_t slot_id);  // a slot the player took by hand
	void CheckAutoLoot();
	void SetAutoLootEnabled(bool enabled) { m_auto_loot_enabled = enabled; }
	bool IsAutoLootEnabled() const { return m_auto_loot_enabled; }

private:
	EverQuest* m_eq;
	bool m_enabled;
	CombatState m_combat_state;

	// Targeting
	uint16_t m_current_target_id;
	std::unique_ptr<CombatTarget> m_current_target_info;
	std::vector<CombatTarget> m_potential_targets;
	std::chrono::steady_clock::time_point m_last_target_scan;
	// T245: Update() pacing clock. Starts at the epoch so the first call
	// after Enable() runs immediately (the old thread's first iteration did).
	std::chrono::steady_clock::time_point m_last_update{};
	std::vector<uint16_t> m_cycle_targets;  // Sorted list of nearby targetable entities
	int m_cycle_index = -1;  // Current position in cycle list (-1 = no cycle active)

	// Auto attack
	bool m_auto_attack_enabled;
	bool m_auto_fire_enabled;  // Ranged auto-attack
	std::chrono::steady_clock::time_point m_last_attack_time;
	uint32_t m_attack_delay_ms;

	// Spells
	std::vector<SpellInfo> m_memorized_spells;

	// Combat stats
	CombatStats m_stats;
	float m_flee_hp_threshold;
	std::chrono::steady_clock::time_point m_flee_start_time;
	glm::vec3 m_flee_destination;

	// Looting
	uint16_t m_current_corpse_id;
	std::vector<uint32_t> m_loot_items;
	bool m_auto_loot_enabled;

	// T26: loot state machine (see LootSubState). m_loot_state_entered is
	// reset on every transition and used for both deadlines (elapsed since
	// entry) and, in LootingItems, repurposed as the last-item-send clock
	// via m_last_item_send_time (kept separate for clarity).
	LootSubState m_loot_sub_state = LootSubState::Idle;
	std::chrono::steady_clock::time_point m_loot_state_entered;
	std::chrono::steady_clock::time_point m_last_loot_item_time;   // AwaitingItems quiescence
	std::chrono::steady_clock::time_point m_last_item_send_time;   // LootingItems pacing
	// T33: AwaitingCorpse search key. Was a name (matched against ANY
	// is_corpse entity -- the T33 wrong-corpse bug). Now the exact
	// spawn_id captured from ZoneProcessDeath's victim_id / the dead
	// ProcessCombatRound target: eqemu renames the SAME entity in place
	// on death (OP_MobRename finds it by parsing the id back out of the
	// "...'s_corpseNNN" name and looks it up by that id -- see
	// the HC_OP_MobRename case of DispatchZonePacket, eq_zone_dispatch.cpp)
	// rather than
	// spawning a second entity, confirmed live against T32's transcript
	// (benchmarks/audits/T32/transcript_primary.txt: "Entity 171 became
	// corpse: 'a_scaled_wolf_pup000' -> 'a_scaled_wolf_pup's_corpse171'"
	// -- same id 171 throughout, no new spawn_id for the corpse).
	uint16_t m_pending_corpse_id = 0;

	// Pacing constants -- named so "still polite enough?" is answerable
	// later without hunting for magic numbers. Values preserve the
	// observable wire pacing of the sleeps they replace (see
	// refactor/T26_loot_state_machine.md Review for the mapping).
	static constexpr int kCorpseSearchDeadlineMs = 2000;      // was: blind 500ms single-shot wait
	static constexpr int kItemQuiescenceMs = 500;             // was: CheckAutoLoot's 500ms "give a moment"
	static constexpr int kAwaitingItemsHardDeadlineMs = 5000; // new: safety net, no legacy equivalent
	static constexpr int kInterItemDelayMs = 100;             // was: LootAll's per-item 100ms
	static constexpr int kClosingDelayMs = 500;                // was: LootAll's pre-close 500ms

	// Auto movement during combat
	bool m_auto_movement_enabled;

	// Auto-hunting
	bool m_auto_hunting_enabled;
	std::chrono::steady_clock::time_point m_last_hunt_update;
	std::chrono::steady_clock::time_point m_last_rest_check;
	std::chrono::steady_clock::time_point m_combat_end_time;
	float m_hunt_radius;
	float m_rest_hp_threshold;
	float m_rest_mana_threshold;
	bool m_waiting_for_considers;
	std::vector<uint16_t> m_pending_considers;
	std::chrono::steady_clock::time_point m_consider_start_time;

	// T247: entity id -> steady-clock time of its last attack on us.
	std::map<uint16_t, std::chrono::steady_clock::time_point> m_recent_attackers;

	// Configuration.
	//
	// T414 Leg B (board B299) deleted m_assist_range, m_combat_range and
	// m_spell_range together with SetAssistRange()/SetCombatRange(): each was
	// a fixed number nothing read, and in every case the value that IS used
	// comes from somewhere the server decides.
	//   - assist: /assist is a server round-trip (ActionDispatcher::assist ->
	//     the handler's packet); the server picks and returns the target, so
	//     there is no client range to gate it on.
	//   - combat: the melee reach actually used is per-target and per-race,
	//     CalculateCombatRange(my_size, target_size, race) in
	//     combat_rounds.cpp; a fixed 5.0f would have fought that formula.
	//   - spell: range comes from the spell row (spell.range,
	//     combat_rounds.cpp), not from a manager-wide constant.
	// The server enforces swing timers and reach; the client animates.
	// m_aggro_radius stays -- GetNearbyHostiles() and the hunt scan read it.
	float m_aggro_radius;

	// T104: publish the two combat projection events at their sole choke
	// points (see combat.cpp) -- replaces Application::syncCombatState()'s
	// poll. Not a behavior change: these do not affect targeting/auto-attack
	// decisions, only make the resulting state observable cross-thread.
	void PublishTargetChanged(const Entity& entity);
	void PublishTargetCleared();
	void WriteAutoAttackState(bool enabled);

	// Helper methods
	void ScanForTargets();
	void UpdateTargetPriorities();
	float CalculateTargetPriority(const Entity& entity);
	HCConsiderColor GetConsiderColor(int level_diff);
	bool IsHostile(const Entity& entity) const;
	bool IsAlly(const Entity& entity) const;
	void SelectBestFleePath();
	bool CanCastSpell(const SpellInfo& spell);
	void ProcessCombatRound();
	// T247: engages a recent attacker (see NoteAttackedBy) when idle.
	void CheckForAggro();
	void SendAttackPacket(uint16_t target_id);
	float CalculateCombatRange(float my_size, float target_size, uint16_t target_race) const;

	// Auto-hunting helpers
	void FindNextHuntTarget();
	bool ShouldRest() const;
	void StartResting();
	void StopResting();
	bool IsTargetSuitableForHunt(const Entity& entity) const;
	Entity* FindNearestGuard() const;
	void FleeToGuard();
	bool IsGuard(const Entity& entity) const;
	bool IsCorpse(const Entity& entity) const;
	Entity* FindNearbyCorpse() const;
	// T308: body-type driven (server-sent Spawn_Struct field), not a race-id
	// table -- see the definition in combat.cpp for the five-copy history.
	bool IsHumanoid(const Entity& entity) const;

	// T26: loot state machine helpers.
	void EnterLootSubState(LootSubState state);
	void TickAwaitingCorpse(std::chrono::steady_clock::time_point now);
	void TickAwaitingItems(std::chrono::steady_clock::time_point now);
	void TickLootingItems(std::chrono::steady_clock::time_point now);
	void TickClosing(std::chrono::steady_clock::time_point now);
	void TickManualOpen();                       // T268: corpse-gone exit only
	bool CorpseStillPresent(uint16_t corpse_id) const;
	void BeginLootRequest(uint16_t corpse_id);  // shared by LootCorpse() and AwaitingCorpse's find
	void AbortLoot();                            // deadline / corpse-gone exit
};
