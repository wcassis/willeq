// PFS/S3D Archive Verification Tool
// Outputs archive statistics in JSON format for comparison testing
// Usage: pfs_verify <archive.s3d> [--json]

#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <algorithm>
#include <iomanip>
#include <cstring>

#include "client/graphics/eq/pfs.h"
#include "common/logging.h"

using namespace EQT::Graphics;

struct ArchiveStats {
    std::string archiveName;
    size_t totalFiles;
    size_t wldCount;
    size_t bmpCount;
    size_t ddsCount;
    size_t otherCount;
    std::vector<std::pair<std::string, size_t>> files;  // filename, size
};

std::string escapeJson(const std::string& s) {
    std::string result;
    for (char c : s) {
        if (c == '"') result += "\\\"";
        else if (c == '\\') result += "\\\\";
        else if (c == '\n') result += "\\n";
        else result += c;
    }
    return result;
}

void outputJson(const ArchiveStats& stats) {
    std::cout << "{\n";
    std::cout << "  \"archive\": \"" << escapeJson(stats.archiveName) << "\",\n";
    std::cout << "  \"totalFiles\": " << stats.totalFiles << ",\n";
    std::cout << "  \"wldCount\": " << stats.wldCount << ",\n";
    std::cout << "  \"bmpCount\": " << stats.bmpCount << ",\n";
    std::cout << "  \"ddsCount\": " << stats.ddsCount << ",\n";
    std::cout << "  \"otherCount\": " << stats.otherCount << ",\n";
    std::cout << "  \"files\": [\n";

    for (size_t i = 0; i < stats.files.size(); ++i) {
        std::cout << "    {\"name\": \"" << escapeJson(stats.files[i].first)
                  << "\", \"size\": " << stats.files[i].second << "}";
        if (i < stats.files.size() - 1) std::cout << ",";
        std::cout << "\n";
    }

    std::cout << "  ]\n";
    std::cout << "}\n";
}

void outputText(const ArchiveStats& stats) {
    std::cout << "Archive: " << stats.archiveName << std::endl;
    std::cout << "Total files: " << stats.totalFiles << std::endl;
    std::cout << "  WLD files: " << stats.wldCount << std::endl;
    std::cout << "  BMP files: " << stats.bmpCount << std::endl;
    std::cout << "  DDS files: " << stats.ddsCount << std::endl;
    std::cout << "  Other: " << stats.otherCount << std::endl;
    std::cout << std::endl;
    std::cout << "Files:" << std::endl;

    for (const auto& file : stats.files) {
        std::cout << "  " << std::setw(40) << std::left << file.first
                  << " " << file.second << " bytes" << std::endl;
    }
}

void printUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " <archive.s3d> [--json]" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Verifies PFS/S3D archive parsing and outputs statistics." << std::endl;
    std::cerr << std::endl;
    std::cerr << "Options:" << std::endl;
    std::cerr << "  --json    Output in JSON format for automated comparison" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string archivePath = argv[1];
    bool jsonOutput = false;

    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "--json") {
            jsonOutput = true;
        }
    }

    PfsArchive archive;
    if (!archive.open(archivePath)) {
        if (jsonOutput) {
            std::cout << "{\"error\": \"Failed to open archive\"}\n";
        } else {
            LOG_ERROR(MOD_MAIN, "Failed to open archive: {}", archivePath);
        }
        return 1;
    }

    ArchiveStats stats;
    stats.archiveName = archivePath;

    // Get all filenames
    std::vector<std::string> wldFiles, bmpFiles, ddsFiles, allFiles;
    archive.getFilenames(".wld", wldFiles);
    archive.getFilenames(".bmp", bmpFiles);
    archive.getFilenames(".dds", ddsFiles);
    archive.getFilenames("*", allFiles);

    stats.wldCount = wldFiles.size();
    stats.bmpCount = bmpFiles.size();
    stats.ddsCount = ddsFiles.size();
    stats.totalFiles = allFiles.size();
    stats.otherCount = stats.totalFiles - stats.wldCount - stats.bmpCount - stats.ddsCount;

    // Get file sizes
    std::sort(allFiles.begin(), allFiles.end());
    for (const auto& filename : allFiles) {
        std::vector<char> buffer;
        if (archive.get(filename, buffer)) {
            stats.files.push_back({filename, buffer.size()});
        } else {
            stats.files.push_back({filename, 0});
        }
    }

    if (jsonOutput) {
        outputJson(stats);
    } else {
        outputText(stats);
    }

    return 0;
}
