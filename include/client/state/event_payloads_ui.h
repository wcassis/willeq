#pragma once

// Event payloads: UI, hotbar, spell visuals and diagnostics.
//
// T451: split out of the 1,269-line include/client/state/event_bus.h.
// Every declaration is a contiguous line range of that file, moved verbatim;
// "client/state/event_bus.h" still includes all of them, so no include site
// changed.

#include <functional>
#include <vector>
#include <variant>
#include <optional>
#include <string>
#include <cstdint>
#include <mutex>
#include <memory>
#include <glm/glm.hpp>

#include "client/hotbar/hotbar_slot_type.h"  // T156: HotbarSlotAssignedData carries the enum, not a raw uint8_t
#include "client/spell/spell_missile_object.h"  // B1170: SpellCastVisualStartedData::missileObject
#include "client/spell/spell_visual_vocabulary.h"  // T381: SpellCastVisual*Data carry the render vocabulary by value, not a SpellDatabase*
#include "client/spell/spell_fx_options.h"  // B1165: SpellCastVisual*Data carry the On-For filter inputs

class HCMap;
class PathfinderNavmesh;

#include "client/state/event_types.h"

namespace eqt {
namespace state {

// --- UI/misc events ---

struct ExpProgressChangedData {
    float progress;  // 0.0 to 1.0
};

struct CharacterInfoChangedData {
    std::string name;
    uint8_t level;
    std::string className;
    std::string deity;
};

struct WorldObjectSpawnedData {
    uint32_t dropId;
    float x, y, z;
    float heading;
    std::string modelName;
    uint8_t objectType;
};

struct NoteWindowOpenedData {
    std::string text;
    uint8_t type;  // 0=book, 1=note
};

struct SpellScribeCompletedData {
    uint32_t spellId;
    uint16_t slot;
    std::string spellName;  // U06g: for new UI display
    uint32_t iconId = 0;    // U06g: spell icon
};

// U06g: Full spellbook snapshot (published once at zone-in from U00)
struct SpellbookEntry {
    uint16_t slot;
    uint32_t spellId;
    std::string name;
    uint32_t iconId;
    uint8_t level;
};

struct SpellbookSnapshotData {
    std::vector<SpellbookEntry> spells;
};

struct HotbarCooldownStartedData {
    int index;
    uint32_t durationMs;
};

struct RendererCommandData {
    std::string command;  // Full command string (e.g., "/sort", "/renderdist 500")
};

// U00: Hotbar slot content assigned (from client-side config or user action)
struct HotbarSlotAssignedData {
    int page = 0;
    int currentPage = 0;
    int index = 0;                      // Slot index (0-9)
    // T156: THE hotbar numbering (Empty=0, Spell=1, Skill=2, Item=3,
    // Emote=4, Macro=5, Combat=6) — see client/hotbar/hotbar_slot_type.h.
    // Typed, so a producer cannot quietly feed it a different numbering.
    HotbarSlotType type = HotbarSlotType::Empty;
    uint32_t id = 0;                    // Spell/skill/item ID
    std::string name;                   // Display name
    uint32_t iconId = 0;                // Icon ID for display
    std::string emoteText;              // T156: for Emote slots (was dropped here)
};

// B1025-H3 (GitLab #345): a hot button the cursor carries while a hotbar
// assignment is in flight. Published by the game thread for /hotbutton (eqstr
// 13278-13283, the one hot button it can create -- a social button,
// HotbarSlotType::Emote with the macro as text). The render thread's
// hold-to-pickup gesture writes CursorAttachmentState directly and never
// publishes this.
struct HotbarAssignPendingData {
    HotbarSlotType type = HotbarSlotType::Empty;
    uint32_t id = 0;
    std::string text;
    uint32_t iconId = 0;
};

struct SkillActivationFeedbackData {
    uint8_t skillId;
    bool success;
    std::string message;
    uint32_t cooldownMs;  // 0 if no cooldown
};

// D20e1: Spell visual FX events (game thread → renderer)
struct SpellCastVisualStartedData {
    uint16_t casterId;
    uint32_t spellId;
    uint32_t castTimeMs;
    std::string spellName;
    std::string casterName;
    bool isPlayerCast;          // true if caster is the local player
    bool isTargetCast;          // true if caster is the player's current target
    // Completion animation info (for NPC casts)
    std::string completionAnim; // empty = no animation on start
    // T381 (B160): resist type / school / spell_affect_index, read off the
    // game thread's own SpellDatabase in SpellManager::handleBeginCast.
    // `known == false` means the id was not in the database.
    EQ::SpellVisualVocabulary spellVisual;
    // B1144 (#464): the level the client's spellsnew.eff slot gate compares
    // (eqgame 0x495cc3): the caster spawn's level, 1 for an NPC or a caster
    // this client does not know (EQ::SpellsNewEffGateLevel).
    int fxGateLevel = 1;
    // B1148 (#468): spell field 120 CastingAnim, the animation id whose track
    // length eqgame adds to the Source duration (0x47ddd4); 0 = unknown.
    int castAnim = 0;
    // B1165 (#484): what the Options > Particles "On For:" filter reads of the
    // caster (EQ::SpellFxActor, spell_fx_options.h): NPC/PC/corpse, in my
    // group, in my raid, me. Default = no such spawn. For a 0x4033 missile
    // object (below) the caster is its source and missileTargetFx its target.
    EQ::SpellFxActor casterFx;
    EQ::SpellFxActor missileTargetFx;
    // B1170 (#489): set when this event carries the server's 0x4033 missile
    // object message instead of an OP_BeginCast: the renderer builds the
    // object (SpellVisualFX::spawnMissileObject) and nothing else of the cast
    // arm runs. Carried here, not on an enumerator of its own, because a new
    // GameEventType changes the event-type count CLAUDE.md states
    // (refactor/B1170/README.md, RULING REQUEST). A pointer, so the other
    // events' variant slot does not grow by the 0x88-byte message.
    std::shared_ptr<const EQ::SpellMissile::Wire> missileObject;
};

struct SpellCastVisualCompleteData {
    uint16_t casterId;
    uint16_t targetId;
    uint32_t spellId;
    std::string spellName;
    bool isSuccess;             // effect_flag == 4
    // T395 (board B204): effect_flag == 0 -- the CAST-ANIMATION action packet.
    // eqemu broadcasts that one to every client in range
    // (Mob::SpellOnTarget, QueueCloseClients, "this causes the caster
    // animation and the particles around the target"); the effect_flag == 4
    // repeat that follows a landing is queued ONLY to the caster and the
    // target, so an observing client never receives it. Gating the impact
    // visual on isSuccess made a bystander's spell FX unreachable by
    // construction.
    bool isCastAnimation = false;
    bool isPlayerCast;
    bool isTargetCast;
    std::string completionAnim; // Animation to play on caster (e.g. "c03", "c04")
    // T381 (B160): see SpellCastVisualStartedData -- the completion path
    // (createSpellComplete / createImpact) needs the same vocabulary, and a
    // cast can land without this client having seen its BeginCast.
    EQ::SpellVisualVocabulary spellVisual;
    int fxGateLevel = 1;  // B1144: see SpellCastVisualStartedData
    // B1165 (#484): the landing's On-For inputs (stage 2 on caster + target).
    EQ::SpellFxActor casterFx;
    EQ::SpellFxActor targetFx;
    // B1174 (#493): what eqgame's OP_Action handler needs to play the spell's
    // CastingAnim on the source (EQ::ActionPlaysSourceCastAnim,
    // spell_action_anim.h): field 120, whether the client's spell table has
    // the id and its field 98 target type, the source spawn's stand byte and
    // class (0 = unknown) and the packet's instrument_mod.
    int castAnim = 0;
    bool spellRecordExists = false;
    uint8_t targetType = 0;
    uint8_t sourceStandState = 0;
    int sourceClass = 0;
    uint32_t instrumentMod = 0;
};

struct SpellCastVisualInterruptedData {
    uint16_t casterId;
    uint32_t spellId;
    bool isPlayerCast;
    bool isTargetCast;
};

struct SpellMemorizeVisualStartedData {
    std::string spellName;
    uint32_t durationMs;
};

struct SpellMemorizeVisualCompleteData {};

// D20e1: Zone lifecycle
struct ZoneUnloadingData {};

// D20e1: Navmesh changed
struct NavmeshChangedData {
    PathfinderNavmesh* navmesh;  // S2: typed handoff (non-owning; game thread retains ownership)
};

// D20e1: Diagnostics
struct DiagnosticsMemoryReportData {
    // External memory info collected by game thread, passed to renderer for full report
    size_t processRssBytes = 0;
    size_t processVmBytes = 0;
    size_t sharedLibBytes = 0;
    size_t anonBytes = 0;
    size_t stackBytes = 0;
    // T127: glibc mallinfo2() allocator bookkeeping -- makes arena
    // fragmentation/unreleased-free-chunk growth visible in /pmem instead of
    // silently inflating "untracked" with no lever to see WHY (T106 found
    // this exact gap: "No dedicated counter -- the lever is malloc_trim() at
    // quiet points, not a report line item"). mallocArenaBytes is glibc's
    // total committed arena size (brk+mmap'd arena blocks); mallocInUseBytes
    // is the subset actually holding live allocations; the difference is
    // free-but-not-released-to-the-OS memory sitting in the allocator's own
    // free lists -- exactly what mallopt(M_ARENA_MAX, 1) (main.cpp) was
    // measured to cut by 8x across a zone-crossing tour.
    size_t mallocArenaBytes = 0;
    size_t mallocInUseBytes = 0;
    bool audioAvailable = false;
    size_t soundBufferCacheBytes = 0;
    size_t soundBufferCacheMaxBytes = 0;
    size_t soundFontEstimateBytes = 0;
    size_t musicDecodedBytes = 0;
    size_t audioPfsArchiveBytes = 0;
    size_t sfxCacheBytes = 0;
    size_t zoneEmitterCount = 0;
    size_t activeEmitterCount = 0;
    // T130: real-time audio callback timing (EQT::Audio::AudioCallbackProbe,
    // include/client/audio/audio_callback_probe.h). Answers T127's open
    // question -- whether M_ARENA_MAX=1 (a single glibc malloc arena, shared
    // with every other thread including the per-zone-crossing LoadingThread)
    // ever visibly stalls the audio callback -- with a direct measurement
    // instead of inference. audioLateCallbackCount counts callbacks whose
    // gap from the previous one exceeded 1.5x the configured period
    // (periodSizeInFrames / sampleRate); audioWorstCallbackDeltaUs is the
    // single worst gap seen since audio init, in microseconds. Both are
    // read here (game thread, /pmem command) from lock-free atomics the
    // audio thread itself only ever writes, never reads.
    uint64_t audioCallbackCount = 0;
    uint64_t audioLateCallbackCount = 0;
    int64_t audioWorstCallbackDeltaUs = 0;
    // T131: peak concurrent TinySoundFont voices seen (EQT::Audio::
    // MidiVoiceProbe, include/client/audio/midi_voice_probe.h) and how many
    // times a note-on was silently dropped because every pre-allocated
    // voice was busy (tsf_note_on's steal-a-releasing-voice path found
    // nothing to steal). Both read here (game thread, /pmem command) from
    // lock-free atomics the audio thread only ever writes -- same pattern
    // as the audioCallback* fields above. If audioNoteOnFailures is ever
    // nonzero, MidiPlayer::kMaxVoices (see its Review,
    // refactor/T131_tsf_preallocate.md) is too low for what was actually
    // played and should be raised with fresh headroom, not ignored.
    int audioPeakVoices = 0;
    uint64_t audioNoteOnFailures = 0;
    size_t entityCount = 0;
    size_t entityEstimateBytes = 0;
    size_t doorCount = 0;
    size_t doorEstimateBytes = 0;
    size_t spellDbCount = 0;
    size_t spellDbEstimateBytes = 0;
    struct ConnectionInfo {
        std::string name;
        uint64_t recvBytes = 0;
        uint64_t sentBytes = 0;
        double avgPing = 0.0;
    };
    std::vector<ConnectionInfo> connections;
    std::string label;
};

struct DiagnosticsSceneDumpData {
    std::string label;
};


} // namespace state
} // namespace eqt
