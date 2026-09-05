// Tool to dump a dragitem sheet to a raw PPM file for verification.
//
// T398: the TGA decoder that used to live here (the ONLY one in the tree)
// moved to tools/tga_reader.h so ui_atlas_builder can read the official
// client's UI art too. This tool is now just the PPM writer plus a CLI.
#include "tga_reader.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

static void savePPM(const std::string& path, const EQT::ToolTGA::Image& img) {
    std::ofstream file(path, std::ios::binary);
    file << "P6\n" << img.width << " " << img.height << "\n255\n";

    for (int i = 0; i < img.width * img.height; i++) {
        file.put(static_cast<char>(img.rgba[i * 4 + 0]));  // R
        file.put(static_cast<char>(img.rgba[i * 4 + 1]));  // G
        file.put(static_cast<char>(img.rgba[i * 4 + 2]));  // B
    }

    std::cout << "Saved: " << path << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <dragitem.tga> [output.ppm]" << std::endl;
        return 1;
    }

    const std::string inputPath = argv[1];
    const std::string outputPath = (argc > 2) ? argv[2] : "sheet_dump.ppm";

    EQT::ToolTGA::Image img;
    std::string error;
    if (!EQT::ToolTGA::load(inputPath, img, error)) {
        std::cerr << "Failed to load TGA: " << error << std::endl;
        return 1;
    }

    std::cout << "Loaded " << img.width << "x" << img.height << " pixels" << std::endl;

    savePPM(outputPath, img);

    return 0;
}
