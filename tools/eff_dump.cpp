// Tool to dump EFF sound entries, especially music (Type 1)
// Usage: eff_dump <zone_sounds.eff>

#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdint>
#include <iomanip>
#include <vector>

#pragma pack(push, 1)
struct EffSoundEntry {
    int32_t  UnkRef00;
    int32_t  UnkRef04;
    int32_t  Reserved;
    int32_t  Sequence;
    float    X;
    float    Y;
    float    Z;
    float    Radius;
    int32_t  Cooldown1;
    int32_t  Cooldown2;
    int32_t  RandomDelay;
    int32_t  Unk44;
    int32_t  SoundID1;      // Day sound or primary
    int32_t  SoundID2;      // Night sound or secondary
    uint8_t  SoundType;
    uint8_t  UnkPad57;
    uint8_t  UnkPad58;
    uint8_t  UnkPad59;
    int32_t  AsDistance;
    int32_t  UnkRange64;
    int32_t  FadeOutMS;
    int32_t  UnkRange72;
    int32_t  FullVolRange;
    int32_t  UnkRange80;
};
#pragma pack(pop)

const char* getSoundTypeName(uint8_t type) {
    switch (type) {
        case 0: return "DayNight/Constant";
        case 1: return "BackgroundMusic";
        case 2: return "StaticEffect";
        case 3: return "DayNight/Distance";
        default: return "Unknown";
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <zone_sounds.eff>" << std::endl;
        return 1;
    }

    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Failed to open: " << argv[1] << std::endl;
        return 1;
    }

    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    if (fileSize % sizeof(EffSoundEntry) != 0) {
        std::cerr << "Invalid file size (not multiple of 84)" << std::endl;
        return 1;
    }

    size_t numEntries = fileSize / sizeof(EffSoundEntry);
    std::cout << "File: " << argv[1] << std::endl;
    std::cout << "Total entries: " << numEntries << std::endl;
    std::cout << std::endl;

    // First pass: count by type
    int typeCounts[4] = {0, 0, 0, 0};
    std::vector<EffSoundEntry> entries(numEntries);
    file.read(reinterpret_cast<char*>(entries.data()), fileSize);

    for (const auto& e : entries) {
        if (e.SoundType < 4) typeCounts[e.SoundType]++;
    }

    std::cout << "Entries by type:" << std::endl;
    for (int i = 0; i < 4; i++) {
        std::cout << "  Type " << i << " (" << getSoundTypeName(i) << "): " << typeCounts[i] << std::endl;
    }
    std::cout << std::endl;

    // Show music entries (Type 1)
    std::cout << "=== MUSIC ENTRIES (Type 1) ===" << std::endl;
    std::cout << std::setw(4) << "Seq" << " | "
              << std::setw(10) << "X" << " | "
              << std::setw(10) << "Y" << " | "
              << std::setw(10) << "Z" << " | "
              << std::setw(8) << "Radius" << " | "
              << std::setw(8) << "DayID" << " | "
              << std::setw(8) << "NightID" << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    for (const auto& e : entries) {
        if (e.SoundType == 1) {
            std::cout << std::setw(4) << e.Sequence << " | "
                      << std::setw(10) << std::fixed << std::setprecision(1) << e.X << " | "
                      << std::setw(10) << e.Y << " | "
                      << std::setw(10) << e.Z << " | "
                      << std::setw(8) << e.Radius << " | "
                      << std::setw(8) << e.SoundID1 << " | "
                      << std::setw(8) << e.SoundID2 << std::endl;
        }
    }

    std::cout << std::endl;
    std::cout << "=== ALL ENTRIES ===" << std::endl;
    std::cout << std::setw(4) << "Seq" << " | "
              << std::setw(18) << "Type" << " | "
              << std::setw(10) << "X" << " | "
              << std::setw(10) << "Y" << " | "
              << std::setw(10) << "Z" << " | "
              << std::setw(8) << "Radius" << " | "
              << std::setw(6) << "ID1" << " | "
              << std::setw(6) << "ID2" << std::endl;
    std::cout << std::string(100, '-') << std::endl;

    for (const auto& e : entries) {
        std::cout << std::setw(4) << e.Sequence << " | "
                  << std::setw(18) << getSoundTypeName(e.SoundType) << " | "
                  << std::setw(10) << std::fixed << std::setprecision(1) << e.X << " | "
                  << std::setw(10) << e.Y << " | "
                  << std::setw(10) << e.Z << " | "
                  << std::setw(8) << e.Radius << " | "
                  << std::setw(6) << e.SoundID1 << " | "
                  << std::setw(6) << e.SoundID2 << std::endl;
    }

    return 0;
}
