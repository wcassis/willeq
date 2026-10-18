#pragma once

// B888-CC3 (GitLab #204) -- the `clz` character-select backdrop's DEVICE-FREE
// layer: parse the P1999 terrain-archive members (ter_clz.ter EQGT, the .mod
// EQGM static models, clz.zon EQGZ placements) into geometry the graphics
// half (char_backdrop_scene.cpp) stands up. No Irrlicht, no loader, no
// driver -- bytes in, geometry out, so the headless test links only this
// (the README's device-free / graphics split, as in char_preview_host.h).
//
// THE FORMAT (measured this run against data/eq_client/clz.eqg's 256 members,
// cross-checked against the zone-utilities reference importer, whose structs
// and rotation application it matches): all little-endian. A string table
// follows each header (listlen bytes, NUL-terminated names); a material
// property whose type is 2 is a string-table OFFSET (a texture filename), not
// a pointer.
//
//   .mod (EQGM): magic(4) ver listlen matc vertc tric + u32 bone (28-byte
//                header; the bone count is .mod-only). String table @28.
//                Geometry = matc x (16-byte material + pc x 12-byte props)
//                + vertc x 32-byte verts + tric x 20-byte tris.
//   .ter (EQGT): magic(4) ver listlen matc vertc tric (24-byte header, no
//                bone). String table @24. Geometry as above, then an
//                unneeded trailing region (past the triangles).
//   material  : u32 index, u32 nameOff, u32 shaderOff, u32 propCount; then
//                propCount x (u32 nameOff, u32 type, u32 value).
//   vertex    : version<3 (clz is version 2) -> 32 bytes: 8 floats
//                x y z, nx ny nz, u v. (version>=3 -> 48 bytes; not in clz.)
//   triangle  : u32 v1 v2 v3, i32 material (can be -1/0xFFFFFFFF: a tail
//                block of the terrain, drawn textureless), u32 flags.
//   .zon (EQGZ): magic(4) ver listlen modelCount objCount regionCount
//                lightCount. String table @28. Geometry = modelCount x u32
//                modelNameOff + objCount x 36-byte placements. Version 1
//                (clz) has NO per-placement varint skip (that is version>1
//                only). Model 0 is the terrain fragment (TER_clz.TER); the
//                rest are .mod.
//   .lit (EQGP): magic(4) u32 count + count x 4 bytes (a per-instance light
//                bake, 4 bytes/vertex). The graphics half applies it; this
//                layer only knows the count matches the model's vertexCount.
//
// COORDINATES (settled from the measured geometry, not guessed): the geometry
// is Z-up -- the terrain's height is its local z (span 586 vs 1780 in x/y),
// the models' tallest axis is z. The graphics half renders in Irrlicht (Y-up)
// and stands each model up with its placement rotation (the terrain's is
// -pi/2 about X, which sends local +z to world +y), so this layer stores the
// raw local coordinates and the raw placement floats and does NO axis swap.

#include <cstdint>
#include <string>
#include <vector>

#include "client/graphics/eq/pfs.h"

namespace EQT {
namespace Graphics {

// One parsed model/terrain geometry (device-free). The graphics half builds
// one mesh buffer per texture and applies the placement rotation to stand it
// up (the geometry is Z-up; Irrlicht is Y-up).
struct ClzGeometry {
    std::vector<float> positions;  // vertexCount * 3, raw local (Z-up) order
    std::vector<float> normals;    // vertexCount * 3
    std::vector<float> uvs;        // vertexCount * 2
    // textureNames[i] = material i's e_TextureDiffuse0 filename (or the
    // material's own name when it has no diffuse property -- clz's 44
    // shininess-only materials still texture). A triangle's material id
    // indexes this; a material id of -1 (0xFFFFFFFF) means "no texture".
    std::vector<std::string> textureNames;
    std::vector<uint32_t> triangleVerts;      // triangleCount * 3
    std::vector<uint32_t> triangleMaterials;  // triangleCount material ids
    uint32_t vertexCount = 0;
    uint32_t triangleCount = 0;
    // Triangles whose material was -1 (the terrain's tail block) -- the
    // graphics half draws them textureless.
    uint32_t texturelessCount = 0;

    bool valid() const {
        return vertexCount > 0 && triangleCount > 0 &&
               positions.size() == static_cast<size_t>(vertexCount) * 3 &&
               normals.size() == static_cast<size_t>(vertexCount) * 3 &&
               uvs.size() == static_cast<size_t>(vertexCount) * 2 &&
               triangleVerts.size() == static_cast<size_t>(triangleCount) * 3 &&
               triangleMaterials.size() == triangleCount;
    }
};

// One placement from clz.zon. modelIndex indexes ClzZone::modelNames (0 =
// the terrain fragment); name is the instance name (its .lit is <name>.lit);
// position/rotation are the raw EQG floats (Z-up local frame).
struct ClzPlacement {
    int32_t modelIndex = -1;
    std::string name;
    float x = 0, y = 0, z = 0;
    float rx = 0, ry = 0, rz = 0;
};

// The whole backdrop: clz.zon's model list (index 0 = the terrain), its
// placements, and each model's archive member name (for loadModel()).
struct ClzZone {
    std::vector<std::string> modelNames;   // clz.zon's model list, in order
    std::vector<std::string> memberNames;  // model i -> archive member (lowercase)
    std::vector<ClzPlacement> placements;
    bool loaded = false;
    std::string error;

    // The measured census (README: 68 DDS + 29 .mod + 157 .lit + ter_clz.ter/
    // .lit + clz.zon = 256, no .wld). loadZone() pins it.
    int ddsCount = 0, modCount = 0, litCount = 0;

    // Load + validate the archive's census and parse clz.zon's model list +
    // placements. Does NOT parse geometry yet (that is loadModel()).
    bool loadZone(PfsArchive& archive);

    // Parse one model's geometry (index into modelNames). Index 0 parses
    // ter_clz.ter; the rest parse their .mod. Returns false (and sets
    // `error`) on a format error.
    bool loadModel(PfsArchive& archive, size_t modelIndex, ClzGeometry& out);
};

// The raw format parse, exposed for the test (no PFS at all). `bytes` is one
// member's contents; isTerrain selects the 24-byte .ter header from the
// 28-byte .mod header.
bool parseClzModel(const char* bytes, size_t len, bool isTerrain, ClzGeometry& out,
                   std::string& error);
// `bytes` is clz.zon's contents; fills `zone`'s modelNames/placements (the
// memberNames are left for loadZone(), which can see the archive's names).
bool parseClzZone(const char* bytes, size_t len, ClzZone& out, std::string& error);

}  // namespace Graphics
}  // namespace EQT
