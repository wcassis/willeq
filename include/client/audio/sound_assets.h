#pragma once

#ifdef WITH_AUDIO

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace EQT {
namespace Audio {

// Parses the EQ SoundAssets.txt file which maps sound IDs to filenames
// Format: ID^{volume}^{filename} or ID^{filename}
class SoundAssets {
public:
    SoundAssets() = default;

    // Load from SoundAssets.txt file
    bool loadFromFile(const std::string& filepath);

    // Get filename for a sound ID
    // Returns empty string if not found
    std::string getFilename(uint32_t soundId) const;

    // Get volume for a sound ID (normalized 0.0 - 1.0)
    // Returns 1.0 if not specified or not found
    float getVolume(uint32_t soundId) const;

    // Check if a sound ID exists
    bool hasSound(uint32_t soundId) const;

    // Get number of loaded entries
    size_t size() const { return entries_.size(); }

    // Iterate over all entries (for building lookup maps)
    template<typename Func>
    void forEach(Func&& func) const {
        for (const auto& [id, entry] : entries_) {
            func(id, entry.filename, entry.volume);
        }
    }

    // Get all sound IDs
    std::vector<uint32_t> getAllSoundIds() const {
        std::vector<uint32_t> ids;
        ids.reserve(entries_.size());
        for (const auto& [id, _] : entries_) {
            ids.push_back(id);
        }
        return ids;
    }

private:
    struct SoundEntry {
        std::string filename;
        float volume = 1.0f;
    };

    std::unordered_map<uint32_t, SoundEntry> entries_;
};

// ============================================================================
// SoundBank -- the ONE place an id-to-filename mapping is written down.
//
// T328 Leg A MEASURED this table: the official Titanium client preloads its
// sound bank in one contiguous burst, in SoundAssets.txt id order, opening
// sounds/<name>.wav exactly once each. The order below is transcribed
// VERBATIM from that trace (review/T328/opens_run1_stock_client.log, the
// 23:57:40 burst); no id here is inferred from a name.
//
// The numbering is anchored four independent ways, all agreeing:
//   waterin = 100  (WaterSoundIds::WaterIn, predates T328)
//   swing   = 118  (the old MELEE_SWING, predates T328)
//   drink   = 149  (UISoundId::DRINK, predates T328)
//   and, exactly, the shipped /mnt/projects/EverQuestP1999/SoundAssets.txt:
//     awk -F'^' '{id=$1+0; if (id>=98 && id<=149 && $1 ~ /^[0-9]+$/)
//                 print id"\t"$2}' SoundAssets.txt | sort -n
//   reproduces all 52 rows below character for character. The trace and the
//   asset file were derived independently and agree completely.
//
// Anything OUTSIDE 98..149 is not covered by that burst and is not asserted
// here -- see DoorSoundId / ObjectSoundId / PlayerSounds for those ranges.
// ============================================================================
namespace SoundBank {
    // -- Footsteps / water (the tail of the burst that precedes the spells) --
    constexpr uint32_t STEPCRCH = 98;    // StepCrch.WAV
    constexpr uint32_t WATEROUT = 99;    // WaterOut.WAV
    constexpr uint32_t WATERIN  = 100;   // WaterIn.WAV   (== WaterSoundIds::WaterIn)
    constexpr uint32_t WATTRD_1 = 101;   // WatTrd_1.WAV
    constexpr uint32_t WATTRD_2 = 102;   // WatTrd_2.WAV

    // -- Spells --
    constexpr uint32_t SPELL_1  = 103;   // Spell_1.wav
    constexpr uint32_t SPELL_2  = 104;   // Spell_2.wav
    constexpr uint32_t SPELL_3  = 105;   // Spell_3.wav
    constexpr uint32_t SPELL_4  = 106;   // Spell_4.wav
    constexpr uint32_t SPELL_5  = 107;   // Spell_5.wav
    constexpr uint32_t SPELCAST = 108;   // SpelCast.WAV
    constexpr uint32_t SPELGDHT = 109;   // SpelGdHt.WAV
    constexpr uint32_t SPELHIT1 = 110;   // SpelHit1.WAV
    constexpr uint32_t SPELHIT2 = 111;   // SpelHit2.WAV
    constexpr uint32_t SPELHIT3 = 112;   // SpelHit3.WAV
    constexpr uint32_t SPELHIT4 = 113;   // SpelHit4.WAV

    // -- Movement --
    constexpr uint32_t JUMPLAND = 114;   // jumpland.WAV
    constexpr uint32_t FALL_HIT = 115;   // fall_hit.WAV

    // -- Combat (116..132) --
    constexpr uint32_t BOWDRAW  = 116;   // BowDraw.WAV
    constexpr uint32_t ARROWHIT = 117;   // ArrowHit.WAV
    constexpr uint32_t SWING    = 118;   // Swing.WAV
    constexpr uint32_t SWINGBIG = 119;   // SwingBig.WAV
    constexpr uint32_t SWORDBLK = 120;   // SwordBlk.WAV
    constexpr uint32_t IMPALE   = 121;   // Impale.WAV
    constexpr uint32_t STAB     = 122;   // Stab.WAV
    constexpr uint32_t CLUB     = 123;   // Club.WAV
    constexpr uint32_t BASHSHLD = 124;   // BashShld.WAV
    constexpr uint32_t SHLDBLK  = 125;   // ShldBlk.WAV
    constexpr uint32_t KICK1    = 126;   // Kick1.WAV
    constexpr uint32_t KICKHIT  = 127;   // KickHit.WAV
    constexpr uint32_t RNDKICK  = 128;   // RndKick.WAV
    constexpr uint32_t LEFTATK  = 129;   // LeftAtk.WAV
    constexpr uint32_t PUNCH1   = 130;   // Punch1.WAV
    constexpr uint32_t PUNCHHIT = 131;   // PunchHit.WAV
    constexpr uint32_t STAFFBLK = 132;   // StaffBlk.WAV

    // -- Chests and wooden doors (133..136) --
    constexpr uint32_t CHEST_CL = 133;   // Chest_Cl.WAV
    constexpr uint32_t CHEST_OP = 134;   // Chest_Op.WAV
    constexpr uint32_t DOORWD_O = 135;   // DoorWd_O.WAV
    constexpr uint32_t DOORWD_C = 136;   // DoorWd_C.WAV

    // -- UI / notifications (137..149) --
    constexpr uint32_t TELEPORT = 137;   // Teleport.WAV
    constexpr uint32_t BUYITEM  = 138;   // BuyItem.WAV
    constexpr uint32_t LEVELUP  = 139;   // LevelUp.WAV
    constexpr uint32_t LEVDN    = 140;   // LevDn.WAV
    constexpr uint32_t ENDQUEST = 141;   // EndQuest.WAV
    constexpr uint32_t BUTTON_1 = 142;   // Button_1.WAV
    constexpr uint32_t THUNDER1 = 143;   // thunder1.wav
    constexpr uint32_t THUNDER2 = 144;   // thunder2.wav
    constexpr uint32_t ITEMCLTH = 145;   // itemclth.WAV
    constexpr uint32_t WEARCLTH = 146;   // wearclth.WAV
    constexpr uint32_t FLSHHIT1 = 147;   // FlshHit1.WAV
    constexpr uint32_t FLSHHIT2 = 148;   // FlshHit2.WAV
    constexpr uint32_t DRINK    = 149;   // Drink.WAV
}

// Named sound ids used by game code. Every entry in the burst's range is now
// an ALIAS into SoundBank above; only the name is a judgement, the number is
// measured.
namespace SoundId {
    // Combat. NOTE (T328): these five names predate the measurement and do
    // NOT describe the files their ids name -- e.g. MELEE_HIT is
    // swingbig.wav, not a hit sound. T328 Leg A measured UI events only, so
    // the correct combat mapping is UNMEASURED and the values are left
    // exactly as they were rather than guessed. Boarded on B036.
    constexpr uint32_t MELEE_SWING = SoundBank::SWING;      // swing.wav (correct)
    constexpr uint32_t MELEE_HIT   = SoundBank::SWINGBIG;   // 119 = swingbig.wav (name unverified)
    constexpr uint32_t MELEE_MISS  = SoundBank::SWORDBLK;   // 120 = swordblk.wav (name unverified)
    constexpr uint32_t KICK        = SoundBank::IMPALE;     // 121 = impale.wav  (name unverified)
    constexpr uint32_t PUNCH       = SoundBank::STAB;       // 122 = stab.wav    (name unverified)

    // Spells
    constexpr uint32_t SPELL_CAST   = SoundBank::SPELCAST;  // spelcast.wav (correct)
    constexpr uint32_t SPELL_FIZZLE = SoundBank::SPELGDHT;  // 109 = spelgdht.wav, NOT SpelFail (name unverified)

    // Player
    constexpr uint32_t LEVEL_UP = SoundBank::LEVELUP;       // levelup.wav (correct)
    constexpr uint32_t DEATH    = SoundBank::LEVDN;         // 140 = levdn.wav, NOT a death cry.
                                                            // PlayerSounds::getSoundId(Death, race, gender)
                                                            // is the race-specific death sound. Unmeasured; boarded.

    // Environment. WATER_IN/WATER_OUT here are 3/4, which are NOT
    // waterin/waterout (those are SoundBank::WATERIN 100 /
    // SoundBank::WATEROUT 99, used by WaterSoundIds and the one wired
    // production path). Out of the measured burst; zero callers; kept only
    // so nothing silently changes meaning. Boarded on B036.
    constexpr uint32_t WATER_IN  = 3;
    constexpr uint32_t WATER_OUT = 4;
    constexpr uint32_t TELEPORT  = SoundBank::TELEPORT;     // T328: was 141 (= endquest.wav). 141 -> 137.

    // UI
    constexpr uint32_t BUTTON_CLICK = SoundBank::BUTTON_1;  // button_1.wav.
                                                            // MEASURED: the official client preloads this
                                                            // and never plays it -- do not wire it.
    constexpr uint32_t BUY_ITEM     = SoundBank::BUYITEM;   // T328: was 145 (= itemclth.wav). 145 -> 138.
                                                            // MEASURED: this is the generic COIN-MOVE sound,
                                                            // not a purchase sound.
    // T328 DELETED: OPEN_WINDOW (143), CLOSE_WINDOW (144), SELL_ITEM (146).
    // 143/144 are thunder1/thunder2 and 146 is wearclth -- none of the three
    // names existed in the bank, all three had zero production callers, and
    // 146 now collides with the real UISoundId::WEAR_CLOTH.
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
