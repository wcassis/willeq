// PFS Archive File Lister
// Lists all files in a PFS/S3D archive
// Usage: pfs_list <archive.pfs>

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include "client/graphics/eq/pfs.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <archive.pfs>" << std::endl;
        return 1;
    }

    EQT::Graphics::PfsArchive archive;
    if (!archive.open(argv[1])) {
        std::cerr << "Failed to open archive: " << argv[1] << std::endl;
        return 1;
    }

    const auto& files = archive.getFiles();

    // Collect and sort filenames
    std::vector<std::string> filenames;
    for (const auto& [name, data] : files) {
        filenames.push_back(name);
    }
    std::sort(filenames.begin(), filenames.end());

    std::cout << "Archive: " << argv[1] << std::endl;
    std::cout << "Total files: " << files.size() << std::endl;
    std::cout << "---" << std::endl;

    for (const auto& name : filenames) {
        const auto& data = files.at(name);
        std::cout << name << " (" << data.size() << " bytes)" << std::endl;
    }

    return 0;
}
