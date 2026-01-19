#ifdef WITH_AUDIO

#include "client/audio/sound_assets.h"
#include "common/logging.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <vector>

namespace EQT {
namespace Audio {

bool SoundAssets::loadFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        LOG_ERROR(MOD_AUDIO, "Failed to open SoundAssets.txt: {}", filepath);
        return false;
    }

    entries_.clear();
    std::string line;
    size_t lineNum = 0;

    while (std::getline(file, line)) {
        ++lineNum;

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == '/') {
            continue;
        }

        // Parse format: ID^{volume}^{filename} or ID^{filename}
        std::vector<std::string> parts;
        std::stringstream ss(line);
        std::string part;

        while (std::getline(ss, part, '^')) {
            parts.push_back(part);
        }

        if (parts.empty()) {
            continue;
        }

        // Parse sound ID
        uint32_t soundId;
        try {
            soundId = std::stoul(parts[0]);
        } catch (...) {
            continue;  // Skip invalid lines
        }

        SoundEntry entry;
        entry.volume = 1.0f;

        if (parts.size() == 2) {
            // Format: ID^filename
            entry.filename = parts[1];
        } else if (parts.size() >= 3) {
            // Format: ID^volume^filename
            try {
                // Volume is stored as integer (e.g., 100 = 100%, 1000 = volume scale)
                int volInt = std::stoi(parts[1]);
                // Normalize: values like 100, 1000 are common
                if (volInt > 100) {
                    entry.volume = static_cast<float>(volInt) / 1000.0f;
                } else if (volInt > 0) {
                    entry.volume = static_cast<float>(volInt) / 100.0f;
                }
            } catch (...) {
                // If volume parse fails, just use the second field as filename
                entry.filename = parts[1];
            }
            if (entry.filename.empty() && parts.size() >= 3) {
                entry.filename = parts[2];
            }
        }

        // Clean up filename
        // Remove leading/trailing whitespace
        auto ltrim = [](std::string& s) {
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
                return !std::isspace(ch);
            }));
        };
        auto rtrim = [](std::string& s) {
            s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
                return !std::isspace(ch);
            }).base(), s.end());
        };

        ltrim(entry.filename);
        rtrim(entry.filename);

        // Skip entries without a valid filename
        if (entry.filename.empty() || entry.filename == "Unknown") {
            continue;
        }

        entries_[soundId] = std::move(entry);
    }

    LOG_INFO(MOD_AUDIO, "Loaded {} sound asset entries from {}", entries_.size(), filepath);
    return true;
}

std::string SoundAssets::getFilename(uint32_t soundId) const {
    auto it = entries_.find(soundId);
    if (it != entries_.end()) {
        return it->second.filename;
    }
    return "";
}

float SoundAssets::getVolume(uint32_t soundId) const {
    auto it = entries_.find(soundId);
    if (it != entries_.end()) {
        return it->second.volume;
    }
    return 1.0f;
}

bool SoundAssets::hasSound(uint32_t soundId) const {
    return entries_.find(soundId) != entries_.end();
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
