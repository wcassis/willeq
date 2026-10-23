// tools/sprite_image_reader.h -- decodes every file format the official
// client's SpellEffects/ sprites ship in, for spell_sprite_atlas_builder.
//
// B1127 (#447): the oracle draws the sprites spellsnew.edd names
// (`review/B1127/README.md`), and those are PNG, colour-mapped TGA and
// 8-bit palettised BMP as well as the 32-bit TGA `tga_reader.h` already
// reads. Header-only, offline tools only (not the client), integer
// arithmetic only (board B268: the atlas must be bit-identical at every
// optimisation level). A failure returns false and fills `error`.
//
// Formats, exactly as far as the shipped files need and no further:
//   .tga  type 2/10 (24/32-bit) via ToolTGA::load; type 1 colour-mapped
//         8-bit index with a 24- or 32-bit map (speldd.tga).
//   .png  8-bit, non-interlaced, colour type 6 (RGBA), 2 (RGB) or
//         3 (palette, optional tRNS); zlib inflate; all five row filters.
//   .bmp  BITMAPINFOHEADER, BI_RGB, 8-bit palettised or 24/32-bit.
// A file with no alpha channel is opaque (alpha 255): the particle blend is
// additive, so its black texels add nothing whatever their alpha.
// Output is ToolTGA::Image: tightly packed top-down RGBA8.

#pragma once

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "tga_reader.h"

namespace EQT {
namespace ToolSprite {

using Image = ToolTGA::Image;

inline bool readFile(const std::string& path, std::vector<uint8_t>& bytes, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) { error = "cannot open " + path; return false; }
    bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

inline uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
inline uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

inline bool loadColourMappedTga(const std::vector<uint8_t>& b, const std::string& path,
                                Image& out, std::string& error) {
    if (b.size() < 18) { error = "short header in " + path; return false; }
    const int idLen = b[0], mapType = b[1], type = b[2];
    const int mapStart = le16(&b[3]), mapLen = le16(&b[5]), mapBits = b[7];
    const int w = le16(&b[12]), h = le16(&b[14]), bpp = b[16];
    const bool top = (b[17] & 0x20) != 0;
    if (mapType != 1 || type != 1 || bpp != 8 || (mapBits != 24 && mapBits != 32) || w <= 0 || h <= 0) {
        error = "unsupported colour-mapped TGA " + path;
        return false;
    }
    const size_t mapBytes = size_t(mapLen) * (mapBits / 8);
    const size_t mapOff = 18 + size_t(idLen), pixOff = mapOff + mapBytes;
    if (b.size() < pixOff + size_t(w) * h) { error = "truncated " + path; return false; }
    out.width = w; out.height = h; out.rgba.assign(size_t(w) * h * 4, 0);
    for (int y = 0; y < h; ++y) {
        const int dy = top ? y : h - 1 - y;
        for (int x = 0; x < w; ++x) {
            const int idx = b[pixOff + size_t(y) * w + x] - mapStart;
            uint8_t* d = &out.rgba[(size_t(dy) * w + x) * 4];
            if (idx < 0 || idx >= mapLen) continue;
            const uint8_t* m = &b[mapOff + size_t(idx) * (mapBits / 8)];
            d[0] = m[2]; d[1] = m[1]; d[2] = m[0]; d[3] = mapBits == 32 ? m[3] : 255;
        }
    }
    return true;
}

inline uint8_t paeth(int a, int b, int c) {
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return uint8_t(a);
    return uint8_t(pb <= pc ? b : c);
}

inline bool loadPng(const std::vector<uint8_t>& b, const std::string& path, Image& out, std::string& error) {
    static const uint8_t kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (b.size() < 8 || std::memcmp(b.data(), kSig, 8) != 0) { error = "not a PNG: " + path; return false; }
    int w = 0, h = 0, depth = 0, ctype = 0, interlace = 0;
    std::vector<uint8_t> idat, palette, trns;
    for (size_t p = 8; p + 12 <= b.size();) {
        const uint32_t len = be32(&b[p]);
        if (p + 12 + len > b.size()) { error = "truncated chunk in " + path; return false; }
        const char* t = reinterpret_cast<const char*>(&b[p + 4]);
        const uint8_t* data = &b[p + 8];
        if (!std::memcmp(t, "IHDR", 4) && len >= 13) {
            w = int(be32(data)); h = int(be32(data + 4));
            depth = data[8]; ctype = data[9]; interlace = data[12];
        } else if (!std::memcmp(t, "PLTE", 4)) {
            palette.assign(data, data + len);
        } else if (!std::memcmp(t, "tRNS", 4)) {
            trns.assign(data, data + len);
        } else if (!std::memcmp(t, "IDAT", 4)) {
            idat.insert(idat.end(), data, data + len);
        } else if (!std::memcmp(t, "IEND", 4)) {
            break;
        }
        p += 12 + len;
    }
    const int channels = ctype == 6 ? 4 : ctype == 2 ? 3 : ctype == 3 ? 1 : 0;
    if (w <= 0 || h <= 0 || depth != 8 || channels == 0 || interlace != 0) {
        error = "unsupported PNG layout in " + path;
        return false;
    }
    const size_t stride = size_t(w) * channels;
    std::vector<uint8_t> raw((stride + 1) * h);
    uLongf rawLen = static_cast<uLongf>(raw.size());
    if (uncompress(raw.data(), &rawLen, idat.data(), static_cast<uLong>(idat.size())) != Z_OK ||
        rawLen != raw.size()) {
        error = "bad zlib stream in " + path;
        return false;
    }
    std::vector<uint8_t> img(stride * h);
    for (int y = 0; y < h; ++y) {
        const uint8_t filter = raw[y * (stride + 1)];
        const uint8_t* src = &raw[y * (stride + 1) + 1];
        uint8_t* cur = &img[y * stride];
        const uint8_t* prev = y ? &img[(y - 1) * stride] : nullptr;
        for (size_t i = 0; i < stride; ++i) {
            const int a = i >= size_t(channels) ? cur[i - channels] : 0;
            const int up = prev ? prev[i] : 0;
            const int c = (prev && i >= size_t(channels)) ? prev[i - channels] : 0;
            int v = src[i];
            switch (filter) {
                case 0: break;
                case 1: v += a; break;
                case 2: v += up; break;
                case 3: v += (a + up) / 2; break;
                case 4: v += paeth(a, up, c); break;
                default: error = "bad PNG filter in " + path; return false;
            }
            cur[i] = uint8_t(v & 0xFF);
        }
    }
    out.width = w; out.height = h; out.rgba.assign(size_t(w) * h * 4, 255);
    for (size_t i = 0; i < size_t(w) * h; ++i) {
        uint8_t* d = &out.rgba[i * 4];
        if (channels == 4) { std::memcpy(d, &img[i * 4], 4); }
        else if (channels == 3) { std::memcpy(d, &img[i * 3], 3); }
        else {
            const size_t k = img[i];
            if (k * 3 + 2 >= palette.size()) { error = "PNG index outside PLTE in " + path; return false; }
            d[0] = palette[k * 3]; d[1] = palette[k * 3 + 1]; d[2] = palette[k * 3 + 2];
            d[3] = k < trns.size() ? trns[k] : 255;
        }
    }
    return true;
}

inline bool loadBmp(const std::vector<uint8_t>& b, const std::string& path, Image& out, std::string& error) {
    if (b.size() < 54 || b[0] != 'B' || b[1] != 'M') { error = "not a BMP: " + path; return false; }
    const uint32_t pixOff = le32(&b[10]), hdr = le32(&b[14]);
    const int w = int(le32(&b[18]));
    const int32_t hRaw = int32_t(le32(&b[22]));
    const int bpp = le16(&b[28]);
    const uint32_t comp = le32(&b[30]);
    uint32_t used = le32(&b[46]);
    const bool topDown = hRaw < 0;
    const int h = topDown ? -hRaw : hRaw;
    if (hdr < 40 || comp != 0 || w <= 0 || h <= 0 || (bpp != 8 && bpp != 24 && bpp != 32)) {
        error = "unsupported BMP layout in " + path;
        return false;
    }
    if (bpp == 8 && used == 0) used = 256;
    const size_t palOff = 14 + size_t(hdr);
    const size_t rowBytes = ((size_t(w) * bpp + 31) / 32) * 4;
    if (b.size() < size_t(pixOff) + rowBytes * h || (bpp == 8 && b.size() < palOff + used * 4)) {
        error = "truncated " + path;
        return false;
    }
    out.width = w; out.height = h; out.rgba.assign(size_t(w) * h * 4, 255);
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = &b[pixOff + rowBytes * y];
        const int dy = topDown ? y : h - 1 - y;
        for (int x = 0; x < w; ++x) {
            uint8_t* d = &out.rgba[(size_t(dy) * w + x) * 4];
            const uint8_t* s;
            if (bpp == 8) {
                if (row[x] >= used) { error = "BMP index outside palette in " + path; return false; }
                s = &b[palOff + size_t(row[x]) * 4];
            } else {
                s = row + size_t(x) * (bpp / 8);
            }
            d[0] = s[2]; d[1] = s[1]; d[2] = s[0];
            d[3] = bpp == 32 ? s[3] : 255;
        }
    }
    return true;
}

inline std::string lowerExtension(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    std::string ext = dot == std::string::npos ? "" : path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext;
}

inline bool load(const std::string& path, Image& out, std::string& error) {
    const std::string ext = lowerExtension(path);
    std::vector<uint8_t> bytes;
    if (ext == ".tga") {
        if (!readFile(path, bytes, error)) return false;
        if (bytes.size() >= 3 && bytes[2] == 1) return loadColourMappedTga(bytes, path, out, error);
        return ToolTGA::load(path, out, error);
    }
    if (!readFile(path, bytes, error)) return false;
    if (ext == ".png") return loadPng(bytes, path, out, error);
    if (ext == ".bmp") return loadBmp(bytes, path, out, error);
    error = "unsupported sprite format " + path;
    return false;
}

}  // namespace ToolSprite
}  // namespace EQT
