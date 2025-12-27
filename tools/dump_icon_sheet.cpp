// Tool to dump a dragitem sheet to a raw PPM file for verification
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>
#include <cstdint>
#include "common/logging.h"

bool loadTGA(const std::string& path, std::vector<uint8_t>& pixels, int& width, int& height) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR(MOD_MAIN, "Failed to open: {}", path);
        return false;
    }

    // TGA header
    uint8_t header[18];
    file.read(reinterpret_cast<char*>(header), 18);
    if (!file.good()) {
        return false;
    }

    // Skip ID field
    uint8_t idLength = header[0];
    if (idLength > 0) {
        file.seekg(idLength, std::ios::cur);
    }

    // Parse header
    uint8_t imageType = header[2];
    width = header[12] | (header[13] << 8);
    height = header[14] | (header[15] << 8);
    uint8_t bitsPerPixel = header[16];
    uint8_t descriptor = header[17];

    std::cout << "TGA: " << width << "x" << height << " " << (int)bitsPerPixel << "bpp, type=" << (int)imageType << std::endl;

    if (width <= 0 || height <= 0) {
        LOG_ERROR(MOD_MAIN, "Invalid dimensions");
        return false;
    }

    if (bitsPerPixel != 24 && bitsPerPixel != 32) {
        LOG_ERROR(MOD_MAIN, "Unsupported bit depth: {}", (int)bitsPerPixel);
        return false;
    }

    int bytesPerPixel = bitsPerPixel / 8;
    bool isRLE = (imageType == 10);
    bool topOrigin = (descriptor & 0x20) != 0;

    pixels.resize(width * height * 4);  // Always output as RGBA

    if (isRLE) {
        // RLE compressed
        int pixelCount = width * height;
        int currentPixel = 0;

        while (currentPixel < pixelCount && file.good()) {
            uint8_t packetHeader;
            file.read(reinterpret_cast<char*>(&packetHeader), 1);

            int count = (packetHeader & 0x7F) + 1;
            bool isRunLength = (packetHeader & 0x80) != 0;

            if (isRunLength) {
                uint8_t pixel[4] = {0, 0, 0, 255};
                file.read(reinterpret_cast<char*>(pixel), bytesPerPixel);

                for (int i = 0; i < count && currentPixel < pixelCount; i++, currentPixel++) {
                    int idx = currentPixel * 4;
                    pixels[idx + 0] = pixel[2];  // R (TGA is BGR)
                    pixels[idx + 1] = pixel[1];  // G
                    pixels[idx + 2] = pixel[0];  // B
                    pixels[idx + 3] = (bytesPerPixel == 4) ? pixel[3] : 255;
                }
            } else {
                for (int i = 0; i < count && currentPixel < pixelCount; i++, currentPixel++) {
                    uint8_t pixel[4] = {0, 0, 0, 255};
                    file.read(reinterpret_cast<char*>(pixel), bytesPerPixel);

                    int idx = currentPixel * 4;
                    pixels[idx + 0] = pixel[2];
                    pixels[idx + 1] = pixel[1];
                    pixels[idx + 2] = pixel[0];
                    pixels[idx + 3] = (bytesPerPixel == 4) ? pixel[3] : 255;
                }
            }
        }
    } else {
        // Uncompressed
        int pixelCount = width * height;
        for (int i = 0; i < pixelCount && file.good(); i++) {
            uint8_t pixel[4] = {0, 0, 0, 255};
            file.read(reinterpret_cast<char*>(pixel), bytesPerPixel);

            int idx = i * 4;
            pixels[idx + 0] = pixel[2];
            pixels[idx + 1] = pixel[1];
            pixels[idx + 2] = pixel[0];
            pixels[idx + 3] = (bytesPerPixel == 4) ? pixel[3] : 255;
        }
    }

    // Flip if bottom-origin
    if (!topOrigin) {
        std::vector<uint8_t> flipped(pixels.size());
        for (int y = 0; y < height; y++) {
            int srcRow = height - 1 - y;
            std::memcpy(flipped.data() + y * width * 4,
                       pixels.data() + srcRow * width * 4,
                       width * 4);
        }
        pixels = std::move(flipped);
    }

    return true;
}

void savePPM(const std::string& path, const std::vector<uint8_t>& pixels, int width, int height) {
    std::ofstream file(path, std::ios::binary);
    file << "P6\n" << width << " " << height << "\n255\n";

    for (int i = 0; i < width * height; i++) {
        file.put(pixels[i * 4 + 0]);  // R
        file.put(pixels[i * 4 + 1]);  // G
        file.put(pixels[i * 4 + 2]);  // B
    }

    std::cout << "Saved: " << path << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <dragitem.tga> [output.ppm]" << std::endl;
        return 1;
    }

    std::string inputPath = argv[1];
    std::string outputPath = (argc > 2) ? argv[2] : "sheet_dump.ppm";

    std::vector<uint8_t> pixels;
    int width, height;

    if (!loadTGA(inputPath, pixels, width, height)) {
        LOG_ERROR(MOD_MAIN, "Failed to load TGA");
        return 1;
    }

    std::cout << "Loaded " << width << "x" << height << " pixels" << std::endl;

    savePPM(outputPath, pixels, width, height);

    return 0;
}
