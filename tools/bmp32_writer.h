/*
 * bmp32_writer.h — the ONE 32-bit-with-alpha BMP writer the offline atlas
 * builders share.
 *
 * T400 (board B225). Two builders now emit a BMP whose alpha channel is the
 * asset (`ui_atlas_builder`, `particle_atlas_builder`), and both feed the
 * same two readers: `ConstrainedTextureCache::decodeBMP()` (the runtime
 * BMP path) and `tools/eqe1_builder.cpp`'s own decoder (the pack path).
 * Both readers accept 32 bpp with either BI_RGB (0) or BI_BITFIELDS (3)
 * and take the fourth byte of each texel as alpha, so the exact layout
 * below is the one both of them are known to read correctly — it is the
 * layout `ui_atlas_builder` has shipped since U02 and is why `ui_atlas`
 * has an alpha companion in `willeq.eqe1` while `particle_atlas` did not.
 *
 * DO NOT use Irrlicht's `IVideoDriver::writeImageToFile(img, "*.bmp")` for
 * an image whose alpha matters: `CImageWriterBMP` writes 24 bpp and DROPS
 * the alpha channel silently. That is exactly how the shipped particle
 * atlas came to be a flat opaque square in every tile (B225).
 *
 * Layout: BITMAPFILEHEADER (14) + BITMAPV4HEADER (108), 32 bpp,
 * BI_BITFIELDS with explicit ARGB masks, TOP-DOWN rows (negative height),
 * texels written B,G,R,A.
 */

#ifndef WILLEQ_TOOLS_BMP32_WRITER_H
#define WILLEQ_TOOLS_BMP32_WRITER_H

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace willeq_tools {

// Write `w`x`h` RGBA8888 pixels (row-major, top row first) as a 32-bit BMP.
// Returns false if the file could not be written.
inline bool writeBMP32(const std::string& path, const uint8_t* rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) return false;

    const int rowBytes = w * 4;
    const int imageSize = rowBytes * h;
    const int headerSize = 14 + 108;  // file header + BITMAPV4HEADER
    const int fileSize = headerSize + imageSize;

    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    // BMP file header (14 bytes)
    uint8_t fileHeader[14] = {};
    fileHeader[0] = 'B'; fileHeader[1] = 'M';
    fileHeader[2] = fileSize & 0xFF;
    fileHeader[3] = (fileSize >> 8) & 0xFF;
    fileHeader[4] = (fileSize >> 16) & 0xFF;
    fileHeader[5] = (fileSize >> 24) & 0xFF;
    fileHeader[10] = headerSize & 0xFF;
    fileHeader[11] = (headerSize >> 8) & 0xFF;
    f.write(reinterpret_cast<char*>(fileHeader), 14);

    // BITMAPV4HEADER (108 bytes)
    uint8_t dibHeader[108] = {};
    auto write32 = [&](int offset, uint32_t val) {
        dibHeader[offset] = val & 0xFF;
        dibHeader[offset + 1] = (val >> 8) & 0xFF;
        dibHeader[offset + 2] = (val >> 16) & 0xFF;
        dibHeader[offset + 3] = (val >> 24) & 0xFF;
    };
    write32(0, 108);                                   // header size
    write32(4, static_cast<uint32_t>(w));              // width
    write32(8, static_cast<uint32_t>(-h));             // height (negative = top-down)
    dibHeader[12] = 1; dibHeader[13] = 0;              // planes
    dibHeader[14] = 32; dibHeader[15] = 0;             // bits per pixel
    write32(16, 3);                                    // compression = BI_BITFIELDS
    write32(20, static_cast<uint32_t>(imageSize));     // image size
    write32(40, 0x00FF0000);                           // red mask
    write32(44, 0x0000FF00);                           // green mask
    write32(48, 0x000000FF);                           // blue mask
    write32(52, 0xFF000000);                           // alpha mask
    f.write(reinterpret_cast<char*>(dibHeader), 108);

    // Pixel data: convert RGBA to BGRA (BMP byte order)
    std::vector<uint8_t> row(static_cast<size_t>(rowBytes));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int srcIdx = (y * w + x) * 4;
            int dstIdx = x * 4;
            row[dstIdx + 0] = rgba[srcIdx + 2];  // B
            row[dstIdx + 1] = rgba[srcIdx + 1];  // G
            row[dstIdx + 2] = rgba[srcIdx + 0];  // R
            row[dstIdx + 3] = rgba[srcIdx + 3];  // A
        }
        f.write(reinterpret_cast<char*>(row.data()), rowBytes);
    }

    return f.good();
}

}  // namespace willeq_tools

#endif  // WILLEQ_TOOLS_BMP32_WRITER_H
