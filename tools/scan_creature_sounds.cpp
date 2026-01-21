// Tool to scan EQ PFS archives and discover creature sound prefixes
// Outputs a list of creature prefixes found in snd*.pfs files

#include "client/graphics/eq/pfs.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

using namespace EQT::Graphics;

int main(int argc, char* argv[]) {
    std::string eqPath = "/home/user/projects/claude/EverQuestP1999";
    if (argc > 1) {
        eqPath = argv[1];
    }

    std::cout << "Scanning EQ path: " << eqPath << std::endl;

    // Find all snd*.pfs files
    std::vector<std::string> archivePaths;
    for (const auto& entry : std::filesystem::directory_iterator(eqPath)) {
        if (!entry.is_regular_file()) continue;

        std::string filename = entry.path().filename().string();
        std::string lowerFilename = filename;
        std::transform(lowerFilename.begin(), lowerFilename.end(),
                       lowerFilename.begin(), ::tolower);

        if (lowerFilename.length() > 7 &&
            lowerFilename.substr(0, 3) == "snd" &&
            lowerFilename.substr(lowerFilename.length() - 4) == ".pfs") {
            archivePaths.push_back(entry.path().string());
        }
    }

    std::cout << "Found " << archivePaths.size() << " sound archives" << std::endl;

    // Regex patterns for creature sounds
    // Pattern: {prefix}_{type}.wav where type is atk, dam, dth, die, idl, spl, run, wlk
    std::regex attackPattern(R"(^([a-z0-9]+)_atk(\d*)\.wav$)", std::regex_constants::icase);
    std::regex damagePattern(R"(^([a-z0-9]+)_dam(\d*)\.wav$)", std::regex_constants::icase);
    std::regex deathPattern(R"(^([a-z0-9]+)_(dth|die)(\d*)\.wav$)", std::regex_constants::icase);
    std::regex idlePattern(R"(^([a-z0-9]+)_idl(\d*)\.wav$)", std::regex_constants::icase);
    std::regex specialPattern(R"(^([a-z0-9]+)_spl(\d*)\.wav$)", std::regex_constants::icase);
    std::regex runPattern(R"(^([a-z0-9]+)_run(\d*)\.wav$)", std::regex_constants::icase);
    std::regex walkPattern(R"(^([a-z0-9]+)_wlk(\d*)\.wav$)", std::regex_constants::icase);

    // Track prefixes and their sound types
    struct PrefixInfo {
        bool hasAttack = false;
        bool hasDamage = false;
        bool hasDeath = false;
        bool hasIdle = false;
        bool hasSpecial = false;
        bool hasRun = false;
        bool hasWalk = false;
        std::set<std::string> attackFiles;
        std::set<std::string> damageFiles;
        std::set<std::string> deathFiles;
        std::set<std::string> idleFiles;
        std::set<std::string> specialFiles;
        std::set<std::string> runFiles;
        std::set<std::string> walkFiles;
    };

    std::map<std::string, PrefixInfo> prefixes;

    // Scan each archive
    for (const auto& archivePath : archivePaths) {
        PfsArchive archive;
        if (!archive.open(archivePath)) {
            std::cerr << "Failed to open: " << archivePath << std::endl;
            continue;
        }

        std::vector<std::string> wavFiles;
        archive.getFilenames(".wav", wavFiles);

        for (const auto& filename : wavFiles) {
            std::smatch match;

            if (std::regex_match(filename, match, attackPattern)) {
                std::string prefix = match[1].str();
                std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::tolower);
                prefixes[prefix].hasAttack = true;
                prefixes[prefix].attackFiles.insert(filename);
            }
            else if (std::regex_match(filename, match, damagePattern)) {
                std::string prefix = match[1].str();
                std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::tolower);
                prefixes[prefix].hasDamage = true;
                prefixes[prefix].damageFiles.insert(filename);
            }
            else if (std::regex_match(filename, match, deathPattern)) {
                std::string prefix = match[1].str();
                std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::tolower);
                prefixes[prefix].hasDeath = true;
                prefixes[prefix].deathFiles.insert(filename);
            }
            else if (std::regex_match(filename, match, idlePattern)) {
                std::string prefix = match[1].str();
                std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::tolower);
                prefixes[prefix].hasIdle = true;
                prefixes[prefix].idleFiles.insert(filename);
            }
            else if (std::regex_match(filename, match, specialPattern)) {
                std::string prefix = match[1].str();
                std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::tolower);
                prefixes[prefix].hasSpecial = true;
                prefixes[prefix].specialFiles.insert(filename);
            }
            else if (std::regex_match(filename, match, runPattern)) {
                std::string prefix = match[1].str();
                std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::tolower);
                prefixes[prefix].hasRun = true;
                prefixes[prefix].runFiles.insert(filename);
            }
            else if (std::regex_match(filename, match, walkPattern)) {
                std::string prefix = match[1].str();
                std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::tolower);
                prefixes[prefix].hasWalk = true;
                prefixes[prefix].walkFiles.insert(filename);
            }
        }
    }

    // Output results
    std::cout << "\n=== Creature Sound Prefixes ===\n" << std::endl;
    std::cout << "Found " << prefixes.size() << " unique prefixes\n" << std::endl;

    for (const auto& [prefix, info] : prefixes) {
        std::cout << "Prefix: " << prefix << std::endl;
        std::cout << "  Attack: " << (info.hasAttack ? "yes" : "no");
        if (info.hasAttack) {
            std::cout << " (" << info.attackFiles.size() << " files)";
        }
        std::cout << std::endl;

        std::cout << "  Damage: " << (info.hasDamage ? "yes" : "no");
        if (info.hasDamage) {
            std::cout << " (" << info.damageFiles.size() << " files)";
        }
        std::cout << std::endl;

        std::cout << "  Death: " << (info.hasDeath ? "yes" : "no");
        if (info.hasDeath) {
            std::cout << " (" << info.deathFiles.size() << " files)";
        }
        std::cout << std::endl;

        std::cout << "  Idle: " << (info.hasIdle ? "yes" : "no");
        if (info.hasIdle) {
            std::cout << " (" << info.idleFiles.size() << " files)";
        }
        std::cout << std::endl;

        std::cout << "  Special: " << (info.hasSpecial ? "yes" : "no");
        if (info.hasSpecial) {
            std::cout << " (" << info.specialFiles.size() << " files)";
        }
        std::cout << std::endl;

        std::cout << "  Run: " << (info.hasRun ? "yes" : "no");
        if (info.hasRun) {
            std::cout << " (" << info.runFiles.size() << " files)";
        }
        std::cout << std::endl;

        std::cout << "  Walk: " << (info.hasWalk ? "yes" : "no");
        if (info.hasWalk) {
            std::cout << " (" << info.walkFiles.size() << " files)";
        }
        std::cout << std::endl;

        std::cout << std::endl;
    }

    // Output as C++ map initialization
    std::cout << "\n=== C++ Map Initialization ===\n" << std::endl;
    std::cout << "// Paste this into creature_sounds.cpp\n" << std::endl;
    std::cout << "static const std::unordered_map<std::string, std::string> s_prefixToRace = {" << std::endl;
    for (const auto& [prefix, info] : prefixes) {
        std::cout << "    {\"" << prefix << "\", \"" << prefix << "\"},  // TODO: map to race name" << std::endl;
    }
    std::cout << "};" << std::endl;

    return 0;
}
