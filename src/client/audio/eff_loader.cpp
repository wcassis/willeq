#ifdef WITH_AUDIO

#include "client/audio/eff_loader.h"
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <cctype>

namespace EQT {
namespace Audio {

// Static member definitions
std::vector<std::string> EffLoader::mp3Index_;
bool EffLoader::mp3IndexLoaded_ = false;

bool EffLoader::loadZone(const std::string& zoneName, const std::string& eqPath) {
    clear();
    zoneName_ = zoneName;

    // Convert zone name to lowercase for file matching
    std::string zoneNameLower = zoneName;
    std::transform(zoneNameLower.begin(), zoneNameLower.end(), zoneNameLower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Build file paths
    std::string soundsPath = eqPath + "/" + zoneNameLower + "_sounds.eff";
    std::string sndbnkPath = eqPath + "/" + zoneNameLower + "_sndbnk.eff";

    // Load mp3index.txt if not already loaded
    if (!mp3IndexLoaded_) {
        loadMp3Index(eqPath);
    }

    // Load sound bank first (needed for resolving sound IDs)
    bool sndbnkLoaded = loadSndBnkEff(sndbnkPath);

    // Load binary sound entries
    bool soundsLoaded = loadSoundsEff(soundsPath);

    return soundsLoaded || sndbnkLoaded;
}

bool EffLoader::loadSoundsEff(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Get file size
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    // File should be a multiple of 84 bytes
    if (fileSize == 0 || fileSize % sizeof(EffSoundEntry) != 0) {
        return false;
    }

    size_t numEntries = fileSize / sizeof(EffSoundEntry);
    soundEntries_.reserve(numEntries);

    // Read all entries
    for (size_t i = 0; i < numEntries; ++i) {
        EffSoundEntry entry;
        file.read(reinterpret_cast<char*>(&entry), sizeof(EffSoundEntry));

        if (file.gcount() < static_cast<std::streamsize>(sizeof(EffSoundEntry))) {
            break;
        }

        soundEntries_.push_back(entry);
    }

    return !soundEntries_.empty();
}

bool EffLoader::loadSndBnkEff(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Read entire file
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    if (content.empty()) {
        return false;
    }

    // Parse line by line
    bool inEmitSection = true;
    std::string line;
    std::istringstream stream(content);

    while (std::getline(stream, line)) {
        // Remove carriage return if present (Windows line endings)
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // Remove leading/trailing whitespace
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) {
            continue;  // Empty or whitespace-only line
        }
        size_t end = line.find_last_not_of(" \t");
        line = line.substr(start, end - start + 1);

        if (line.empty()) {
            continue;
        }

        // Check for section markers
        if (line == "EMIT") {
            inEmitSection = true;
            continue;
        }
        if (line == "LOOP" || line == "RAND") {
            inEmitSection = false;
            continue;
        }

        // Add to appropriate section
        if (inEmitSection) {
            emitSounds_.push_back(line);
        } else {
            loopSounds_.push_back(line);
        }
    }

    return !emitSounds_.empty() || !loopSounds_.empty();
}

std::string EffLoader::resolveSoundFile(int32_t soundId) const {
    // 0 = No sound
    if (soundId == 0) {
        return "";
    }

    // < 0 = MP3 from mp3index.txt (abs value = line number, 1-indexed)
    if (soundId < 0) {
        return getMp3File(-soundId);
    }

    // 1-31 = EMIT section (1-indexed, so ID 1 = emitSounds_[0])
    if (soundId < 32) {
        size_t index = static_cast<size_t>(soundId - 1);
        if (index < emitSounds_.size()) {
            return emitSounds_[index];
        }
        return "";
    }

    // 162+ = LOOP section (offset by 161, so ID 162 = loopSounds_[0])
    if (soundId > 161) {
        size_t index = static_cast<size_t>(soundId - 162);
        if (index < loopSounds_.size()) {
            return loopSounds_[index];
        }
        return "";
    }

    // 32-161 = Hardcoded global sounds
    return getHardcodedSound(soundId);
}

size_t EffLoader::getMusicEntryCount() const {
    size_t count = 0;
    for (const auto& entry : soundEntries_) {
        if (entry.SoundType == static_cast<uint8_t>(EffSoundType::BackgroundMusic)) {
            ++count;
        }
    }
    return count;
}

void EffLoader::clear() {
    zoneName_.clear();
    soundEntries_.clear();
    emitSounds_.clear();
    loopSounds_.clear();
}

bool EffLoader::loadMp3Index(const std::string& eqPath) {
    if (mp3IndexLoaded_) {
        return true;
    }

    std::string filepath = eqPath + "/mp3index.txt";
    std::ifstream file(filepath);

    if (!file.is_open()) {
        // Use default entries for Planes of Power era
        // Index 0 is unused (1-indexed)
        mp3Index_.clear();
        mp3Index_.push_back("");  // Index 0 placeholder
        mp3Index_.push_back("bothunder.mp3");
        mp3Index_.push_back("codecay.mp3");
        mp3Index_.push_back("combattheme1.mp3");
        mp3Index_.push_back("combattheme2.mp3");
        mp3Index_.push_back("deaththeme.mp3");
        mp3Index_.push_back("eqtheme.mp3");
        mp3Index_.push_back("hohonor.mp3");
        mp3Index_.push_back("poair.mp3");
        mp3Index_.push_back("podisease.mp3");
        mp3Index_.push_back("poearth.mp3");
        mp3Index_.push_back("pofire.mp3");
        mp3Index_.push_back("poinnovation.mp3");
        mp3Index_.push_back("pojustice.mp3");
        mp3Index_.push_back("poknowledge.mp3");
        mp3Index_.push_back("ponightmare.mp3");
        mp3Index_.push_back("postorms.mp3");
        mp3Index_.push_back("potactics.mp3");
        mp3Index_.push_back("potime.mp3");
        mp3Index_.push_back("potorment.mp3");
        mp3Index_.push_back("potranquility.mp3");
        mp3Index_.push_back("povalor.mp3");
        mp3Index_.push_back("powar.mp3");
        mp3Index_.push_back("powater.mp3");
        mp3Index_.push_back("solrotower.mp3");
        mp3IndexLoaded_ = true;
        return false;
    }

    mp3Index_.clear();
    mp3Index_.push_back("");  // Index 0 placeholder (1-indexed file)

    std::string line;
    while (std::getline(file, line)) {
        // Remove carriage return if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        // Remove whitespace
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) {
            mp3Index_.push_back("");
            continue;
        }
        size_t end = line.find_last_not_of(" \t");
        mp3Index_.push_back(line.substr(start, end - start + 1));
    }

    mp3IndexLoaded_ = true;
    return true;
}

std::string EffLoader::getMp3File(int index) {
    if (index <= 0 || static_cast<size_t>(index) >= mp3Index_.size()) {
        return "";
    }
    return mp3Index_[static_cast<size_t>(index)];
}

std::string EffLoader::getHardcodedSound(int32_t soundId) {
    // Hardcoded sound files for IDs 32-161
    // Based on Shendare's Eff2Emt converter (CC0 license)
    // Most IDs in this range are undefined/unused
    switch (soundId) {
        case 39:  return "death_me";
        case 143: return "thunder1";
        case 144: return "thunder2";
        case 158: return "wind_lp1";
        case 159: return "rainloop";
        case 160: return "torch_lp";
        case 161: return "watundlp";
        default:  return "";
    }
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
