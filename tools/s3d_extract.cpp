// S3D Archive Extraction Tool
// Extracts all files from an EverQuest S3D/PFS archive to a directory

#include "client/graphics/eq/pfs.h"
#include "common/logging.h"
#include <iostream>
#include <fstream>
#include <filesystem>

using namespace EQT::Graphics;
namespace fs = std::filesystem;

void printUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " <archive.s3d> [output_directory]" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Extracts all files from an S3D archive." << std::endl;
    std::cerr << "If output_directory is not specified, files are extracted to" << std::endl;
    std::cerr << "a directory named after the archive (without .s3d extension)." << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string archivePath = argv[1];

    // Determine output directory
    std::string outputDir;
    if (argc >= 3) {
        outputDir = argv[2];
    } else {
        // Use archive name without extension
        outputDir = archivePath;
        size_t dotPos = outputDir.rfind('.');
        if (dotPos != std::string::npos) {
            outputDir = outputDir.substr(0, dotPos);
        }
        // Just use the basename
        size_t slashPos = outputDir.rfind('/');
        if (slashPos != std::string::npos) {
            outputDir = outputDir.substr(slashPos + 1);
        }
        outputDir += "_extracted";
    }

    std::cout << "Opening archive: " << archivePath << std::endl;

    PfsArchive archive;
    if (!archive.open(archivePath)) {
        LOG_ERROR(MOD_MAIN, "Failed to open archive: {}", archivePath);
        return 1;
    }

    const auto& files = archive.getFiles();
    std::cout << "Found " << files.size() << " files in archive" << std::endl;

    // Create output directory
    if (!fs::exists(outputDir)) {
        fs::create_directories(outputDir);
        std::cout << "Created output directory: " << outputDir << std::endl;
    }

    // Extract all files
    int extracted = 0;
    int failed = 0;

    // Get list of filenames from the files map
    for (const auto& [filename, compressedData] : files) {
        std::string outPath = outputDir + "/" + filename;

        // Create subdirectories if needed
        fs::path filePath(outPath);
        if (filePath.has_parent_path()) {
            fs::create_directories(filePath.parent_path());
        }

        // Use get() to properly decompress the file data
        std::vector<char> decompressedData;
        if (!archive.get(filename, decompressedData)) {
            LOG_ERROR(MOD_MAIN, "Failed to decompress: {}", filename);
            failed++;
            continue;
        }

        std::ofstream outFile(outPath, std::ios::binary);
        if (!outFile) {
            LOG_ERROR(MOD_MAIN, "Failed to create: {}", outPath);
            failed++;
            continue;
        }

        outFile.write(decompressedData.data(), decompressedData.size());
        outFile.close();

        std::cout << "  Extracted: " << filename << " (" << decompressedData.size() << " bytes)" << std::endl;
        extracted++;
    }

    std::cout << std::endl;
    std::cout << "Extraction complete:" << std::endl;
    std::cout << "  Extracted: " << extracted << " files" << std::endl;
    if (failed > 0) {
        std::cout << "  Failed: " << failed << " files" << std::endl;
    }
    std::cout << "  Output directory: " << outputDir << std::endl;

    return 0;
}
