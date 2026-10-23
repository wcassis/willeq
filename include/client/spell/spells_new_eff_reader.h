/*
 * WillEQ spellsnew.eff + spellsnew.edd reader (B1127, #447)
 *
 * THE OFFICIAL CLIENT DRAWS THESE, NOT spells.eff. A spell's effect is the
 * spellsnew.eff record named by spells_us.txt field 145 (SpellField::
 * NewEffectIndex); each record lists the emitters (spellsnew.edd entries)
 * of its three stages, and an emitter's art is the texture its EDD entry
 * names. Proven on the oracle by same-field-123 / different-field-145 A/B
 * casts and by the art of the B1130 survey's 41 records (GENI00 records
 * draw flare_blsp50a.png, not speli1's skulls; 458's GENA20 record draws
 * gena20.bmp) -- review/B1127/README.md has the captures.
 *
 * spellsnew.eff (no header, 268-byte records, record N at N*268):
 *   +0    char name[64]
 *   +64   three stage groups of 68 bytes: Source, Middle, Target
 *           int32 sound              (spells.eff block +0x21C carries the
 *                                     same 103..112 values; NOT used)
 *           4 x { int32 emitter      spellsnew.edd index, < 1 = empty slot
 *                 int32 minLevel     drawn iff minLevel <= the caster's
 *                                    level; an NPC (or unknown) caster
 *                                    counts as level 1
 *                 int32 anchor       0 world position / 1 caster /
 *                                    2 missile / 3 target
 *                 int32 attachPoint }  SpellsNewEffAttach below; ignored
 *                                    when anchor is 0 or 2
 *           (meanings proven from eqgame.exe 0x495cc3 by B1141 (#461):
 *           docs/spellsnew_format.md). B1144 (#464): fable draws by them --
 *           drawnSlots() is the client's per-slot gate.
 * spellsnew.edd ("EDD\0" "110\0", then 416-byte entries):
 *   +0    char name[64]
 *   +64   char texture[32]   file under SpellEffects/ (any case; one entry
 *                            pads it with '.', trimmed here); +0x60 on
 *                            are flag words
 *   +0x130 int32 frames      16 / 4 / 1 -- agrees with every texture's
 *                            layout (4x4, 2x2, one picture)
 *   everything else          decoded from the client's code by B1141
 *                            (docs/spellsnew_format.md) but NOT used by
 *                            fable yet.
 */

#ifndef EQT_SPELLS_NEW_EFF_READER_H
#define EQT_SPELLS_NEW_EFF_READER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace EQ {

constexpr size_t kSpellsNewEffRecordSize = 268;
constexpr size_t kSpellsNewEffNameBytes = 64;
constexpr size_t kSpellsNewEffStageBytes = 68;
constexpr size_t kSpellsNewEffSlotsPerStage = 4;
constexpr size_t kSpellsNewEddHeaderBytes = 8;
constexpr size_t kSpellsNewEddEntrySize = 416;
constexpr size_t kSpellsNewEddFramesOffset = 0x130;

// slot +8 (eqgame 0x495cc3): which actor a slot's emitter is placed on.
enum class SpellsNewEffAnchor : int32_t {
    World = 0,    // a fixed world position the caller gives, no actor
    Caster = 1,
    Missile = 2,  // the missile object (Middle stage only)
    Target = 3,
};

// slot +12 (eqgame 0x495cc3; points bound by name SPELLPOINT_* at 0x47c17e).
// 2..7 fall back to DEFAULT on a model without that point; REYE skips the
// slot on a model without it. Ignored when the anchor is World or Missile.
enum class SpellsNewEffAttach : int32_t {
    None = 0,             // the actor's position, not attached
    Default = 1,          // SPELLPOINT_DEFAULT (where it sits is model data)
    Chest = 2,
    Head = 3,
    LeftHand = 4,
    RightHand = 5,
    LeftFoot = 6,
    RightFoot = 7,
    Origin = 8,           // attached to the actor with no point
    LeftEye = 9,
    RightEye = 10,
    Mouth = 11,
    FloorUnderChest = 12, // the CHEST point dropped to the floor (0x48a6bf)
};

struct SpellsNewEffSlot {
    int32_t emitter = 0;      // slot +0: spellsnew.edd index; < 1 = empty
    int32_t minLevel = 0;     // slot +4: minimum caster level
    int32_t anchor = 0;       // slot +8: SpellsNewEffAnchor, raw
    int32_t attachPoint = 0;  // slot +12: SpellsNewEffAttach, raw
    bool hasEmitter() const { return emitter > 0; }
    SpellsNewEffAnchor anchorKind() const { return static_cast<SpellsNewEffAnchor>(anchor); }
    SpellsNewEffAttach attach() const { return static_cast<SpellsNewEffAttach>(attachPoint); }
    // The client's level gate (0x495cc3): drawn iff minLevel <= casterLevel.
    bool drawnAtLevel(int casterLevel) const { return hasEmitter() && minLevel <= casterLevel; }
};

// Stage indices of a record (eqgame 0x49b260's first argument).
constexpr int kSpellsNewEffSource = 0;  // played from begin-cast only (0x49b39d)
constexpr int kSpellsNewEffMiddle = 1;  // played ONLY by the missile spawner (0x476b12)
constexpr int kSpellsNewEffTarget = 2;  // the landing (0x47f5e1, 0x45448b)

// The level the slot gate compares (0x495cc3: the caster spawn's level at
// +0x24c = Spawn_Struct level @151, or 1 when the caster is an NPC -- spawn
// +0x244 = Spawn_Struct NPC @83 != 0 -- or absent).
inline int SpellsNewEffGateLevel(bool casterKnown, bool casterIsNpc, int spawnLevel) {
    return (!casterKnown || casterIsNpc) ? 1 : spawnLevel;
}

// One slot the client draws: its index in the stage (0..3), its words, and
// its emitter's texture (lower-case basename; empty when the EDD names none).
struct SpellsNewEffDrawnSlot {
    int index = 0;
    SpellsNewEffSlot slot;
    std::string texture;
};

struct SpellsNewEffStage {
    int32_t sound = 0;
    std::array<SpellsNewEffSlot, kSpellsNewEffSlotsPerStage> slots{};
    bool hasContent() const;
};

struct SpellsNewEffRecord {
    std::string name;
    std::array<SpellsNewEffStage, 3> stages{};  // Source, Middle, Target
    bool hasContent() const;
};

struct SpellsNewEddEntry {
    std::string name;
    std::string texture;  // lower-case basename, e.g. "spelua.png"
    int32_t frames = 0;
};

class SpellsNewEffReader {
public:
    // Loads <dir>/spellsnew.eff and <dir>/spellsnew.edd. Both must parse
    // exactly (record-size multiple, EDD magic) or the load fails.
    bool load(const std::string& eqClientDir);
    bool loadFiles(const std::string& effPath, const std::string& eddPath);

    bool isLoaded() const { return m_loaded; }
    const std::string& loadError() const { return m_error; }
    size_t recordCount() const { return m_records.size(); }
    size_t emitterCount() const { return m_edd.size(); }

    // nullptr for index <= 0 (field 145's "none" values) or out of range
    // (spells_us.txt names records up to 3411; the shipped file has 418).
    const SpellsNewEffRecord* getRecord(int32_t index) const;
    const SpellsNewEddEntry* getEmitter(int32_t index) const;

    // The textures of stage `stage` (0 Source, 1 Middle, 2 Target), one per
    // non-empty slot, in slot order (a texture named by two slots appears
    // twice: two emitters draw it). Empty when the record or stage is empty.
    std::vector<std::string> stageTextures(int32_t record, int stage) const;

    // B1144 (#464): the slots of stage `stage` the client draws for a caster
    // of gate level `casterLevel` (SpellsNewEffGateLevel), in slot order: a
    // slot with an emitter whose minLevel <= casterLevel. This is the whole
    // per-slot rule of 0x495cc3; WHICH stages play is the caller's (Source
    // from begin-cast only, Middle from a missile only, Target on landing).
    std::vector<SpellsNewEffDrawnSlot> drawnSlots(int32_t record, int stage, int casterLevel) const;
    // The slots of the stage with an emitter at all (any level): what a
    // `#level 60` oracle capture drew (every slot's minLevel is 0, 24 or 39).
    size_t emitterSlotCount(int32_t record, int stage) const;

private:
    std::vector<SpellsNewEffRecord> m_records;
    std::vector<SpellsNewEddEntry> m_edd;
    bool m_loaded = false;
    std::string m_error;
};

}  // namespace EQ

#endif  // EQT_SPELLS_NEW_EFF_READER_H
