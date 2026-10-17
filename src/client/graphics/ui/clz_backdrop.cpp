// B888-CC3 (GitLab #204) -- the device-free clz format parse. What it is and
// the measured format: include/client/graphics/ui/clz_backdrop.h.

#include "client/graphics/ui/clz_backdrop.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>

namespace EQT {
namespace Graphics {

namespace {

// Little-endian reads over an unaligned buffer.
template <class T> T rdLE(const char* p) {
    T v;
    std::memcpy(&v, p, sizeof(T));
    return v;
}
uint32_t rdU32(const char* p) { return rdLE<uint32_t>(p); }
int32_t rdI32(const char* p) { return rdLE<int32_t>(p); }
float rdF32(const char* p) { return rdLE<float>(p); }

struct Slice {
    const char* p;
    size_t n;
    bool empty() const { return n == 0; }
};

// A NUL-terminated name at `off` (offset from the buffer start, which is the
// string table's base in this file's convention).
std::string nameAt(const char* base, size_t len, uint32_t off) {
    if (off + 1 > len) {
        return "";
    }
    const char* s = base + off;
    size_t i = 0;
    while (i < len - off && s[i] != '\0') {
        ++i;
    }
    return std::string(s, i);
}

// The member name a clz.zon model list entry maps to in the archive (PfsArchive
// lowercases names, so the whole lookup is lowercase): TER_* -> <stem>.ter,
// everything else -> <name>.mod. A ')' in the EQG name is a '(' placeholder the
// reference importer also rewrites to '_'.
std::string memberNameForModel(const std::string& modelName) {
    std::string s = modelName;
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    for (auto& c : s) {
        if (c == ')') {
            c = '_';
        }
    }
    const bool terrain = s.rfind("ter_", 0) == 0;
    return s + (terrain ? ".ter" : ".mod");
}

// One material's property walk: the texture filename is the type-2 property
// whose name contains "TextureDiffuse"; a material with none keeps its own
// name (still addressable, still textured) and is flagged textureless=false --
// the graphics half falls back to a flat material for a name it cannot load.
struct MaterialInfo {
    std::string name;
    std::string texture;
    bool textureless = false;
};

bool parseGeometry(const char* buf, size_t len, bool isTerrain, ClzGeometry& out,
                   std::string& error) {
    const size_t headerSize = isTerrain ? 24u : 28u;
    const size_t stringBase = isTerrain ? 24u : 28u;
    if (len < headerSize) {
        error = "member too small for a header";
        return false;
    }
    if (buf[0] != 'E' || buf[1] != 'Q' || buf[2] != 'G' ||
        (isTerrain ? buf[3] != 'T' : buf[3] != 'M')) {
        error = "bad magic (expected EQGT/EQGM, got ";
        error.append(buf, 4);
        error.append(")");
        return false;
    }
    const uint32_t version = rdU32(buf + 4);
    const uint32_t listLen = rdU32(buf + 8);
    const uint32_t matCount = rdU32(buf + 12);
    const uint32_t vertCount = rdU32(buf + 16);
    const uint32_t triCount = rdU32(buf + 20);
    const size_t geoStart = stringBase + static_cast<size_t>(listLen);
    if (geoStart > len) {
        error = "string table overruns the member";
        return false;
    }

    size_t pos = geoStart;
    std::vector<MaterialInfo> materials(matCount);
    for (uint32_t m = 0; m < matCount; ++m) {
        if (pos + 16 > len) {
            error = "material header overruns the member";
            return false;
        }
        (void)rdU32(buf + pos);           // index
        const uint32_t nameOff = rdU32(buf + pos + 4);
        (void)rdU32(buf + pos + 8);       // shader offset (unused)
        const uint32_t propCount = rdU32(buf + pos + 12);
        pos += 16;
        materials[m].name = nameAt(buf, len, nameOff);
        for (uint32_t j = 0; j < propCount; ++j) {
            if (pos + 12 > len) {
                error = "material property overruns the member";
                return false;
            }
            const uint32_t pName = rdU32(buf + pos);
            const uint32_t pType = rdU32(buf + pos + 4);
            const uint32_t pValue = rdU32(buf + pos + 8);
            pos += 12;
            if (pType == 2) {
                const std::string pname = nameAt(buf, len, pName);
                if (pname.find("TextureDiffuse") != std::string::npos) {
                    materials[m].texture = nameAt(buf, len, pValue);
                }
            }
        }
    }
    if (materials.empty()) {
        materials.push_back({});
    }
    for (auto& mi : materials) {
        mi.textureless = mi.texture.empty();
        if (mi.textureless) {
            mi.texture = mi.name;
        }
    }

    // Vertices. Version<3 is 32 bytes (clz is version 2); version>=3 is
    // 48 bytes (a color + two unknowns before the uv) -- supported but not
    // exercised by clz.
    const size_t vertSize = version >= 3 ? 48 : 32;
    if (pos + static_cast<size_t>(vertCount) * vertSize > len) {
        error = "vertex block overruns the member";
        return false;
    }
    out.positions.resize(static_cast<size_t>(vertCount) * 3);
    out.normals.resize(static_cast<size_t>(vertCount) * 3);
    out.uvs.resize(static_cast<size_t>(vertCount) * 2);
    for (uint32_t i = 0; i < vertCount; ++i) {
        const char* v = buf + pos + static_cast<size_t>(i) * vertSize;
        const size_t uvOff = vertSize - 8;  // u v are the final two floats in both
        out.positions[static_cast<size_t>(i) * 3 + 0] = rdF32(v);
        out.positions[static_cast<size_t>(i) * 3 + 1] = rdF32(v + 4);
        out.positions[static_cast<size_t>(i) * 3 + 2] = rdF32(v + 8);
        out.normals[static_cast<size_t>(i) * 3 + 0] = rdF32(v + 12);
        out.normals[static_cast<size_t>(i) * 3 + 1] = rdF32(v + 16);
        out.normals[static_cast<size_t>(i) * 3 + 2] = rdF32(v + 20);
        out.uvs[static_cast<size_t>(i) * 2 + 0] = rdF32(v + uvOff);
        out.uvs[static_cast<size_t>(i) * 2 + 1] = rdF32(v + uvOff + 4);
    }
    pos += static_cast<size_t>(vertCount) * vertSize;

    // Triangles: u32 v1 v2 v3, i32 material, u32 flags. A material of -1
    // (the terrain's tail block) draws textureless; it is NOT an error --
    // the reference importer keeps such polygons and the client paints them.
    if (pos + static_cast<size_t>(triCount) * 20 > len) {
        error = "triangle block overruns the member";
        return false;
    }
    out.textureNames.reserve(materials.size());
    for (const auto& mi : materials) {
        out.textureNames.push_back(mi.texture);
    }
    out.triangleVerts.resize(static_cast<size_t>(triCount) * 3);
    out.triangleMaterials.resize(triCount);
    out.texturelessCount = 0;
    for (uint32_t t = 0; t < triCount; ++t) {
        const char* tr = buf + pos + static_cast<size_t>(t) * 20;
        out.triangleVerts[static_cast<size_t>(t) * 3 + 0] = rdU32(tr);
        out.triangleVerts[static_cast<size_t>(t) * 3 + 1] = rdU32(tr + 4);
        out.triangleVerts[static_cast<size_t>(t) * 3 + 2] = rdU32(tr + 8);
        const int32_t mat = rdI32(tr + 12);
        if (mat < 0 || static_cast<uint32_t>(mat) >= out.textureNames.size()) {
            out.triangleMaterials[t] = 0;
            ++out.texturelessCount;
        } else {
            out.triangleMaterials[t] = static_cast<uint32_t>(mat);
        }
    }

    out.vertexCount = vertCount;
    out.triangleCount = triCount;
    if (!out.valid()) {
        error = "parsed geometry failed its self-check";
        return false;
    }
    return true;
}

}  // namespace

bool parseClzModel(const char* bytes, size_t len, bool isTerrain, ClzGeometry& out,
                   std::string& error) {
    out = ClzGeometry();
    return parseGeometry(bytes, len, isTerrain, out, error);
}

bool parseClzZone(const char* bytes, size_t len, ClzZone& out, std::string& error) {
    if (len < 28) {
        error = "clz.zon too small";
        return false;
    }
    if (std::memcmp(bytes, "EQGZ", 4) != 0) {
        error = "bad clz.zon magic";
        return false;
    }
    const uint32_t listLen = rdU32(bytes + 8);
    const uint32_t modelCount = rdU32(bytes + 12);
    const uint32_t objCount = rdU32(bytes + 16);
    const size_t geoStart = 28u + static_cast<size_t>(listLen);
    if (geoStart + static_cast<size_t>(modelCount) * 4 > len) {
        error = "model list overruns clz.zon";
        return false;
    }
    out.modelNames.clear();
    out.placements.clear();
    for (uint32_t m = 0; m < modelCount; ++m) {
        const uint32_t off = rdU32(bytes + geoStart + static_cast<size_t>(m) * 4);
        out.modelNames.push_back(nameAt(bytes, len, off));
    }
    // Version 1 (clz): 36-byte placements, no varint skip. (The reference
    // importer reads a varint per placement only when version > 1; clz.zon's
    // header version is 1, measured.)
    const size_t placStart = geoStart + static_cast<size_t>(modelCount) * 4;
    if (placStart + static_cast<size_t>(objCount) * 36 > len) {
        error = "placement list overruns clz.zon";
        return false;
    }
    out.placements.resize(objCount);
    for (uint32_t o = 0; o < objCount; ++o) {
        const char* p = bytes + placStart + static_cast<size_t>(o) * 36;
        ClzPlacement& pl = out.placements[o];
        pl.modelIndex = rdI32(p);
        pl.name = nameAt(bytes, len, rdU32(p + 4));
        pl.x = rdF32(p + 8);
        pl.y = rdF32(p + 12);
        pl.z = rdF32(p + 16);
        pl.rx = rdF32(p + 20);
        pl.ry = rdF32(p + 24);
        pl.rz = rdF32(p + 28);
    }
    if (out.placements.empty()) {
        error = "clz.zon has no placements";
        return false;
    }
    return true;
}

bool ClzZone::loadZone(PfsArchive& archive) {
    loaded = false;
    error.clear();

    // The measured census: 68 DDS + 29 .mod + 157 .lit + ter_clz.ter +
    // ter_clz.lit + clz.zon = 256 members, no .wld (README, pinned by
    // ExploreScreen.BackdropZoneLoadsHeadless).
    std::vector<std::string> names;
    if (!archive.getFilenames("*", names)) {
        error = "could not read the archive's filename table";
        return false;
    }
    ddsCount = modCount = litCount = 0;
    for (const auto& n : names) {
        if (n.size() > 4) {
            const std::string ext = n.substr(n.size() - 3);
            if (ext == ".dds") {
                ++ddsCount;
            } else if (ext == ".mod") {
                ++modCount;
            } else if (ext == ".lit") {
                ++litCount;
            }
        }
    }
    if (ddsCount != 68 || modCount != 29 || litCount != 157 || names.size() != 256) {
        error = "unexpected member census (dds=" + std::to_string(ddsCount) +
                " mod=" + std::to_string(modCount) + " lit=" + std::to_string(litCount) +
                " total=" + std::to_string(names.size()) + ")";
        return false;
    }
    for (const auto& n : names) {
        if (n == "clz.zon" || n == "ter_clz.ter") {
            continue;
        }
        if (n.size() > 4 && n.substr(n.size() - 4) == ".wld") {
            error = "clz.eqg unexpectedly carries a .wld";
            return false;
        }
    }

    std::vector<char> zon;
    if (!archive.get("clz.zon", zon)) {
        error = "clz.zon is not in the archive";
        return false;
    }
    if (!parseClzZone(zon.data(), zon.size(), *this, error)) {
        return false;
    }
    // Model 0 must be the terrain fragment; the rest must be .mod members.
    if (modelNames.empty() || modelNames.front().find("TER_") != 0) {
        error = "model 0 is not the terrain fragment (got \"" + modelNames.front() + "\")";
        return false;
    }
    memberNames.resize(modelNames.size());
    for (size_t m = 0; m < modelNames.size(); ++m) {
        memberNames[m] = memberNameForModel(modelNames[m]);
        if (!archive.exists(memberNames[m])) {
            error = "model member missing from the archive: " + memberNames[m];
            return false;
        }
    }
    loaded = true;
    return true;
}

bool ClzZone::loadModel(PfsArchive& archive, size_t modelIndex, ClzGeometry& out) {
    out = ClzGeometry();
    if (!loaded || modelIndex >= memberNames.size()) {
        return false;
    }
    std::vector<char> data;
    if (!archive.get(memberNames[modelIndex], data)) {
        return false;
    }
    return parseClzModel(data.data(), data.size(), modelIndex == 0, out, error);
}

}  // namespace Graphics
}  // namespace EQT
