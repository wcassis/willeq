// B941 (GitLab #259): the game thread's two big zone structures -- the
// collision map (HCMap + its RaycastMesh AABB tree) and the Detour navmesh
// (PathfinderNavmesh) -- were re-represented to shrink the `game_state`
// memory category. Every query they answer must be BIT-IDENTICAL to what the
// old representation answered:
//
//   * collision map: every triangle's three vertices (raw float bits, as a
//     sorted multiset since B951 stores triangles in leaf order), 4000
//     FindBestZ ground-height queries and
//     4000 CheckLOS / CheckLOSWithHit line-of-sight queries at deterministic
//     pseudo-random points;
//   * navmesh: every detail triangle (raw bits + area), 1500 smoothed
//     FindPath queries (the client's own /follow options) and 500 FindRoute
//     queries between deterministic pseudo-random points ON the mesh.
//
// Each set is folded into a 64-bit FNV-1a digest over the raw bits, and the
// digests were computed with the OLD code (fable 0b84eef6, before B941) and
// committed below. A real zone whose map/nav file is absent (CI runners have no
// /mnt/projects/maps) SKIPS; the synthetic V2 map test below always runs and
// compares the new loader against an in-test copy of the old triangle-soup
// emission, triangle by triangle and ray by ray.
//
// Print the digests instead of asserting: WILLEQ_B941_PRINT=1.
// Map/nav roots: WILLEQ_MAP_FIXTURES (default the akk-stack maps/base) and
// WILLEQ_NAV_FIXTURES (default the akk-stack maps/nav).

#include <gtest/gtest.h>
#include "client/hc_map.h"
#include "client/raycast_mesh.h"
#include "common/logging.h"
#if defined(EQT_HAS_NAVMESH)
#include "client/pathfinder_nav_mesh.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>
#include <zlib.h>
#include <glm/glm.hpp>

namespace {

// ---------------------------------------------------------------------------
// Digest + deterministic sampler
// ---------------------------------------------------------------------------
struct Fnv {
    uint64_t h = 1469598103934665603ull;
    void bytes(const void* p, size_t n) {
        const unsigned char* c = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i) { h ^= c[i]; h *= 1099511628211ull; }
    }
    void f(float v) { uint32_t b; std::memcpy(&b, &v, 4); bytes(&b, 4); }
    void u(uint32_t v) { bytes(&v, 4); }
    void v3(const glm::vec3& v) { f(v.x); f(v.y); f(v.z); }
};

struct Lcg {  // Knuth MMIX; fully specified, no <random> distribution involved
    uint64_t s;
    explicit Lcg(uint64_t seed) : s(seed) {}
    uint32_t next() { s = s * 6364136223846793005ull + 1442695040888963407ull; return (uint32_t)(s >> 32); }
    float unit() { return (float)(next() >> 8) * (1.0f / 16777216.0f); }
    float in(float a, float b) { return a + (b - a) * unit(); }
};

std::string Hex(uint64_t v) { char b[32]; std::snprintf(b, sizeof(b), "%016llx", (unsigned long long)v); return b; }

bool PrintMode() { return std::getenv("WILLEQ_B941_PRINT") != nullptr; }

std::string MapDir() {
    if (const char* e = std::getenv("WILLEQ_MAP_FIXTURES")) return e;
    return "/mnt/projects/akk-stack/server/maps/base";
}
std::string NavDir() {
    if (const char* e = std::getenv("WILLEQ_NAV_FIXTURES")) return e;
    return "/mnt/projects/akk-stack/server/maps/nav";
}
bool Exists(const std::string& p) { std::ifstream f(p, std::ios::binary); return f.is_open(); }

// ---------------------------------------------------------------------------
// Collision map digests
// ---------------------------------------------------------------------------
struct MapBounds { float minX, maxX, minY, maxY, minZ, maxZ; };  // Irrlicht Y-up

// One triangle as raw bits: v1, v2, v3 (x, y, z each) and the placeable flag.
using TriBits = std::array<uint32_t, 10>;

std::vector<TriBits> SortedTriangleBits(const std::vector<HCMap::Triangle>& all) {
    std::vector<TriBits> recs;
    recs.reserve(all.size());
    for (const auto& t : all) {
        TriBits r{};
        std::memcpy(&r[0], &t.v1, 12);
        std::memcpy(&r[3], &t.v2, 12);
        std::memcpy(&r[6], &t.v3, 12);
        r[9] = t.isPlaceable ? 1u : 0u;
        recs.push_back(r);
    }
    std::sort(recs.begin(), recs.end());
    return recs;
}

// Every triangle's raw bits + placeable flag, as a MULTISET: the records are
// sorted before they are folded in. B951 (#269) stores the triangles in leaf
// order, so the ORDER GetTrianglesInRadius returns them in is no longer the
// file's; everything a query or a consumer depends on is the set (each
// triangle's three vertices in their own winding order, its flag, and how
// many times it occurs), which this digest still covers bit for bit. The
// pinned values were taken with the pre-B951 code (fable 267e5a36). Returns
// the bounds too.
uint64_t TriangleDigest(const HCMap& map, MapBounds& b, size_t& tris) {
    const auto all = map.GetTrianglesInRadius(glm::vec3(0.0f), 1.0e9f);
    b = {1e30f, -1e30f, 1e30f, -1e30f, 1e30f, -1e30f};
    for (const auto& t : all) {
        for (const glm::vec3* v : {&t.v1, &t.v2, &t.v3}) {
            b.minX = std::min(b.minX, v->x); b.maxX = std::max(b.maxX, v->x);
            b.minY = std::min(b.minY, v->y); b.maxY = std::max(b.maxY, v->y);
            b.minZ = std::min(b.minZ, v->z); b.maxZ = std::max(b.maxZ, v->z);
        }
    }
    Fnv d;
    for (const auto& r : SortedTriangleBits(all)) d.bytes(r.data(), sizeof(uint32_t) * r.size());
    tris = all.size();
    return d.h;
}

// FindBestZ + CheckLOS + CheckLOSWithHit at deterministic points. HCMap's API
// is EQ server coords (Z-up): EQ(x, y, z) = mesh(x, z, y).
uint64_t QueryDigest(const HCMap& map, const MapBounds& b, int n) {
    Lcg r(0xB941ull);
    Fnv d;
    float vbMin = 0.0f, vbMax = 0.0f;
    map.GetVertexBounds(vbMin, vbMax);
    d.f(vbMin); d.f(vbMax);
    float zMin = 0.0f, zMax = 0.0f;
    map.GetZRange(zMin, zMax);
    d.f(zMin); d.f(zMax);
    for (int i = 0; i < n; ++i) {
        const glm::vec3 start(r.in(b.minX, b.maxX), r.in(b.minZ, b.maxZ), r.in(b.minY, b.maxY + 20.0f));
        glm::vec3 res(0.0f);
        const float z = map.FindBestZ(start, &res);
        d.f(z); d.v3(res);
    }
    for (int i = 0; i < n; ++i) {
        const glm::vec3 a(r.in(b.minX, b.maxX), r.in(b.minZ, b.maxZ), r.in(b.minY - 10.0f, b.maxY + 10.0f));
        // Half the segments are short (a few hundred units), so they end both
        // inside and outside geometry rather than always crossing the zone.
        glm::vec3 e;
        if (i & 1) {
            e = glm::vec3(r.in(b.minX, b.maxX), r.in(b.minZ, b.maxZ), r.in(b.minY - 10.0f, b.maxY + 10.0f));
        } else {
            e = a + glm::vec3(r.in(-300.0f, 300.0f), r.in(-300.0f, 300.0f), r.in(-60.0f, 60.0f));
        }
        glm::vec3 hit(0.0f);
        const bool los = map.CheckLOSWithHit(a, e, &hit);
        const bool los2 = map.CheckLOS(a, e);
        d.u(los ? 1u : 0u); d.u(los2 ? 1u : 0u); d.v3(hit);
    }
    return d.h;
}

// ---------------------------------------------------------------------------
// Navmesh digests
// ---------------------------------------------------------------------------
#if defined(EQT_HAS_NAVMESH)
void HashPath(Fnv& d, const IPathfinder::IPath& p, bool partial, bool stuck) {
    d.u((uint32_t)p.size()); d.u(partial ? 1u : 0u); d.u(stuck ? 1u : 0u);
    for (const auto& n : p) {
        d.u(n.teleport ? 1u : 0u);
        if (!n.teleport) d.v3(n.pos);
    }
}

// Detail triangles over the map's extent, then paths between their centroids.
void NavDigests(PathfinderNavmesh& nav, const MapBounds& b, uint64_t& triDigest, uint64_t& pathDigest,
                size_t& tris, int nPath, int nRoute) {
    const glm::vec3 c((b.minX + b.maxX) * 0.5f, (b.minY + b.maxY) * 0.5f, (b.minZ + b.maxZ) * 0.5f);
    const float radius = 0.5f * std::sqrt((b.maxX - b.minX) * (b.maxX - b.minX) + (b.maxZ - b.minZ) * (b.maxZ - b.minZ)) + 2000.0f;
    const auto all = nav.GetTrianglesInRadius(c, radius);
    Fnv t;
    std::vector<glm::vec3> pts;  // EQ coords (x = irr X, y = irr Z, z = irr Y)
    pts.reserve(all.size());
    for (const auto& tr : all) {
        t.v3(tr.v1); t.v3(tr.v2); t.v3(tr.v3); t.u(tr.areaType);
        const glm::vec3 m = (tr.v1 + tr.v2 + tr.v3) / 3.0f;
        pts.emplace_back(m.x, m.z, m.y);
    }
    triDigest = t.h;
    tris = all.size();
    Fnv d;
    if (pts.empty()) { pathDigest = d.h; return; }
    Lcg r(0x941B0Bull);
    PathfinderOptions opts;       // MovementComponent::FindPath's options
    opts.smooth_path = true;
    opts.step_size = 10.0f;
    opts.offset = 5.0f;
    for (int i = 0; i < nPath; ++i) {
        const glm::vec3& a = pts[r.next() % pts.size()];
        glm::vec3 e = pts[r.next() % pts.size()];
        if (i % 3 == 0) {  // a third are local (a few hundred units), like /follow
            e = a + glm::vec3(r.in(-250.0f, 250.0f), r.in(-250.0f, 250.0f), 0.0f);
        }
        bool partial = false, stuck = false;
        const auto p = nav.FindPath(a, e, partial, stuck, opts);
        HashPath(d, p, partial, stuck);
    }
    for (int i = 0; i < nRoute; ++i) {
        const glm::vec3& a = pts[r.next() % pts.size()];
        const glm::vec3& e = pts[r.next() % pts.size()];
        bool partial = false, stuck = false;
        const auto p = nav.FindRoute(a, e, partial, stuck);
        HashPath(d, p, partial, stuck);
    }
    pathDigest = d.h;
}
#endif

// The OLD code's digests. Query + navmesh columns: fable 0b84eef6 (before
// B941). Triangle column: the sorted (multiset) digest above, taken with
// fable 267e5a36 (before B951; B941 left every triangle bit-identical, so it
// is also the pre-B941 set). "-" = not pinned.
struct ZoneGolden {
    const char* zone;
    const char* triDigest;
    const char* queryDigest;
    const char* navTriDigest;
    const char* navPathDigest;
};
const ZoneGolden kGoldens[] = {
    // B941-GOLDENS-BEGIN
    {"befallen", "eb4b0b95df4e68df", "43950f532ec6c1e8", "106c12499932eded", "c6802350188d5c05"},  // tris=43018
    {"gfaydark", "ce18246f8faf5273", "3f8b44b03c2042b8", "ba76b4f659466d99", "c0e61d3b8df60282"},  // tris=129637
    {"iceclad", "d0861e9cec670bee", "2f53d2a239076ce6", "1b28b12123f1d520", "bee4dce263885fd2"},  // tris=122148
    {"emeraldjungle", "36f2e55484ea526b", "61cf6574dd71ae1e", "36969ab938eeeccc", "14a510ed78b1bf61"},  // tris=618240
    {"wakening", "157aa94ab55e096f", "d8281d24b891f527", "4ce4fb0a06e4998d", "a1687f3c2a118ec1"},  // tris=382855
    {"timorous", "f4ee79871d4ba560", "9267953703bb2046", "f274e7f013024a21", "5fc1f3a302b08a54"},  // tris=257769
    {"growthplane", "0381beee0d14e403", "5d2a8d50340baec5", "10515ddd8f88ef6f", "7e5dc6a8054a3369"},  // tris=107397
    {"overthere", "434909bd49b81783", "57787bac2c5d178f", "c58423f1431d03d2", "e1f56e5862ecd2d9"},  // tris=145391
    {"skyfire", "453f9bb23f56224c", "833e35196acf2756", "f3b670066d3eaab7", "7732a6353a7328ea"},  // tris=309460
    {"warslikswood", "b39a2a60ef368369", "a31b84278855fa67", "0ee0164b4202c602", "139a5c56b5a62b11"},  // tris=312409
    {"sky", "d9529f0737b30aca", "6403e7684bd04812", "-", "-"},  // tris=25832
    // B941-GOLDENS-END
};

const ZoneGolden* FindGolden(const std::string& zone) {
    for (const auto& g : kGoldens) if (zone == g.zone) return &g;
    return nullptr;
}

void CheckZone(const std::string& zone) {
    // Print mode also shows the loaders' own [raycastmesh] / [navmesh] lines.
    if (PrintMode()) SetModuleLogLevel(MOD_MAP, LOG_INFO);
    const std::string mapPath = MapDir() + "/" + zone + ".map";
    if (!Exists(mapPath)) GTEST_SKIP() << "no map at " << mapPath;
    HCMap map;
    ASSERT_TRUE(map.Load(mapPath)) << mapPath;
    MapBounds b{};
    size_t tris = 0;
    const uint64_t triD = TriangleDigest(map, b, tris);
    const uint64_t qD = QueryDigest(map, b, 4000);
    std::string navTri = "-", navPath = "-";
#if defined(EQT_HAS_NAVMESH)
    const std::string navPathFile = NavDir() + "/" + zone + ".nav";
    size_t navTris = 0;
    if (Exists(navPathFile)) {
        PathfinderNavmesh nav(navPathFile);
        if (nav.IsLoaded()) {
            uint64_t nt = 0, np = 0;
            NavDigests(nav, b, nt, np, navTris, 1500, 500);
            navTri = Hex(nt);
            navPath = Hex(np);
        }
    }
#endif
    if (PrintMode()) {
        const auto st = map.GetMemoryStats();
        std::printf("    {\"%s\", \"%s\", \"%s\", \"%s\", \"%s\"},  // tris=%zu\n", zone.c_str(),
                    Hex(triD).c_str(), Hex(qD).c_str(), navTri.c_str(), navPath.c_str(), tris);
        std::printf("    // %s: map vertices=%zu faces=%zu vertex+index bytes=%zu\n", zone.c_str(),
                    st.vertexCount, st.faceCount, st.totalBytes);
        return;
    }
    const ZoneGolden* g = FindGolden(zone);
    ASSERT_NE(g, nullptr) << zone << " has no pinned digest";
    EXPECT_EQ(Hex(triD), g->triDigest) << zone << ": the collision triangle multiset differs from the pre-B951 map";
    EXPECT_EQ(Hex(qD), g->queryDigest) << zone << ": FindBestZ/CheckLOS results differ from the pre-B941 map";
    // A host with the .map but no .nav (or a build without Detour) checks
    // the collision map only; the navmesh half needs the .nav to exist.
    if (navTri == "-") {
        std::printf("%s: no navmesh here (%s) -- collision map checked, navmesh not\n", zone.c_str(),
                    NavDir().c_str());
    } else if (std::string(g->navTriDigest) != "-") {
        EXPECT_EQ(navTri, g->navTriDigest) << zone << ": navmesh detail triangles differ";
        EXPECT_EQ(navPath, g->navPathDigest) << zone << ": navmesh FindPath/FindRoute results differ";
    }
}

}  // namespace

// The heavy B854 zones plus the gate's classic zones.
TEST(GameStateIdentity, Befallen)      { CheckZone("befallen"); }
TEST(GameStateIdentity, Gfaydark)      { CheckZone("gfaydark"); }
TEST(GameStateIdentity, Iceclad)       { CheckZone("iceclad"); }
TEST(GameStateIdentity, Emeraldjungle) { CheckZone("emeraldjungle"); }
TEST(GameStateIdentity, Wakening)      { CheckZone("wakening"); }
TEST(GameStateIdentity, Timorous)      { CheckZone("timorous"); }
TEST(GameStateIdentity, Growthplane)   { CheckZone("growthplane"); }
TEST(GameStateIdentity, Overthere)     { CheckZone("overthere"); }
TEST(GameStateIdentity, Skyfire)       { CheckZone("skyfire"); }
TEST(GameStateIdentity, Warslikswood)  { CheckZone("warslikswood"); }
TEST(GameStateIdentity, Sky)           { CheckZone("sky"); }   // a V1 map

// ---------------------------------------------------------------------------
// Synthetic V2 map (always runs): the new loader against an in-test copy of
// the OLD triangle-soup emission (every placed triangle with three private
// vertices), triangle by triangle and ray by ray. The map exercises what the
// dedup touches: models whose vertex lists repeat a position, invisible
// polys, a placement naming no model, a placement group of two entries, and
// flat + non-flat terrain tiles with a skipped quad.
// ---------------------------------------------------------------------------
namespace {

void PutU32(std::vector<char>& b, uint32_t v) { b.insert(b.end(), (char*)&v, (char*)&v + 4); }
void PutF(std::vector<char>& b, float v) { b.insert(b.end(), (char*)&v, (char*)&v + 4); }
void PutU8(std::vector<char>& b, uint8_t v) { b.push_back((char)v); }
void PutStr(std::vector<char>& b, const std::string& s) { b.insert(b.end(), s.begin(), s.end()); b.push_back('\0'); }

struct SynModel {
    std::string name;
    std::vector<glm::vec3> verts;
    struct P { uint32_t a, b, c; uint8_t vis; };
    std::vector<P> polys;
};
struct SynPlacement { std::string name; float v[9]; };
struct SynGroupEntry { std::string name; float v[9]; };
struct SynGroup { float v[12]; std::vector<SynGroupEntry> entries; };

struct SynMap {
    std::vector<glm::vec3> mainVerts;   // file order (x, y, z) with INVERSEXY applied
    std::vector<uint32_t> mainIdx;
    std::vector<SynModel> models;
    std::vector<SynPlacement> placements;
    std::vector<SynGroup> groups;
    uint32_t quadsPerTile = 3;
    float unitsPerVertex = 4.0f;
    struct Tile { bool flat; float x, y, z; std::vector<uint8_t> flags; std::vector<float> floats; };
    std::vector<Tile> tiles;
};

SynMap MakeSynMap() {
    SynMap m;
    Lcg r(77);
    for (int i = 0; i < 40; ++i) m.mainVerts.emplace_back(r.in(-200, 200), r.in(-200, 200), r.in(-20, 20));
    for (int i = 0; i < 60; ++i) m.mainIdx.push_back(r.next() % 40);
    for (int k = 0; k < 3; ++k) {
        SynModel md;
        md.name = "MODEL" + std::to_string(k);
        for (int i = 0; i < 12; ++i) md.verts.emplace_back(r.in(-10, 10), r.in(-10, 10), r.in(0, 15));
        md.verts.push_back(md.verts[2]);   // duplicated positions (same bits)
        md.verts.push_back(md.verts[5]);
        md.verts.emplace_back(-0.0f, 0.0f, 1.0f);
        md.verts.emplace_back(0.0f, 0.0f, 1.0f);  // +0 vs -0: different bits, kept apart
        const uint32_t n = (uint32_t)md.verts.size();
        for (int i = 0; i < 20; ++i) md.polys.push_back({r.next() % n, r.next() % n, r.next() % n, (uint8_t)(i % 5 == 0 ? 0 : 1)});
        m.models.push_back(md);
    }
    for (int i = 0; i < 6; ++i) {
        SynPlacement p;
        p.name = (i == 4) ? "NOSUCHMODEL" : m.models[i % 3].name;
        for (int j = 0; j < 3; ++j) p.v[j] = r.in(-150, 150);
        for (int j = 3; j < 6; ++j) p.v[j] = r.in(-3.0f, 3.0f);
        for (int j = 6; j < 9; ++j) p.v[j] = r.in(0.5f, 2.0f);
        m.placements.push_back(p);
    }
    SynGroup g;
    for (int j = 0; j < 12; ++j) g.v[j] = r.in(-50, 50);
    for (int j = 3; j < 6; ++j) g.v[j] = r.in(-90, 90);     // degrees
    for (int j = 6; j < 9; ++j) g.v[j] = r.in(0.5f, 1.5f);
    for (int e = 0; e < 2; ++e) {
        SynGroupEntry ge;
        ge.name = m.models[e].name;
        for (int j = 0; j < 3; ++j) ge.v[j] = r.in(-40, 40);
        for (int j = 3; j < 6; ++j) ge.v[j] = r.in(-180, 180);
        for (int j = 6; j < 9; ++j) ge.v[j] = r.in(0.5f, 1.5f);
        g.entries.push_back(ge);
    }
    m.groups.push_back(g);
    const uint32_t q = m.quadsPerTile;
    SynMap::Tile flat{true, -300.0f, -300.0f, -5.0f, {}, {}};
    SynMap::Tile bumpy{false, 100.0f, -300.0f, 0.0f, std::vector<uint8_t>(q * q, 0), {}};
    bumpy.flags[4] = 1;  // one skipped quad
    for (uint32_t i = 0; i < (q + 1) * (q + 1); ++i) bumpy.floats.push_back(r.in(-8, 8));
    m.tiles = {flat, bumpy};
    return m;
}

std::vector<char> WriteSynV2(const SynMap& m) {
    std::vector<char> p;
    PutU32(p, (uint32_t)m.mainVerts.size());
    PutU32(p, (uint32_t)m.mainIdx.size());
    PutU32(p, 2); PutU32(p, 3);  // non-collidable: 2 verts, 3 indices (skipped)
    PutU32(p, (uint32_t)m.models.size());
    PutU32(p, (uint32_t)m.placements.size());
    PutU32(p, (uint32_t)m.groups.size());
    PutU32(p, (uint32_t)m.tiles.size());
    PutU32(p, m.quadsPerTile);
    PutF(p, m.unitsPerVertex);
    for (const auto& v : m.mainVerts) { PutF(p, v.x); PutF(p, v.y); PutF(p, v.z); }
    for (uint32_t i : m.mainIdx) PutU32(p, i);
    for (int i = 0; i < 6; ++i) PutF(p, 1.0f);
    for (int i = 0; i < 3; ++i) PutU32(p, 0);
    for (const auto& md : m.models) {
        PutStr(p, md.name);
        PutU32(p, (uint32_t)md.verts.size());
        PutU32(p, (uint32_t)md.polys.size());
        for (const auto& v : md.verts) { PutF(p, v.x); PutF(p, v.y); PutF(p, v.z); }
        for (const auto& pl : md.polys) { PutU32(p, pl.a); PutU32(p, pl.b); PutU32(p, pl.c); PutU8(p, pl.vis); }
    }
    for (const auto& pl : m.placements) { PutStr(p, pl.name); for (float f : pl.v) PutF(p, f); }
    for (const auto& g : m.groups) {
        for (float f : g.v) PutF(p, f);
        PutU32(p, (uint32_t)g.entries.size());
        for (const auto& e : g.entries) { PutStr(p, e.name); for (float f : e.v) PutF(p, f); }
    }
    for (const auto& t : m.tiles) {
        PutU8(p, t.flat ? 1 : 0);
        PutF(p, t.x); PutF(p, t.y);
        if (t.flat) { PutF(p, t.z); continue; }
        for (uint8_t f : t.flags) PutU8(p, f);
        for (float f : t.floats) PutF(p, f);
    }
    uLongf bound = compressBound((uLong)p.size());
    std::vector<char> c(bound);
    EXPECT_EQ(compress2((Bytef*)c.data(), &bound, (const Bytef*)p.data(), (uLong)p.size(), 9), Z_OK);
    c.resize(bound);
    std::vector<char> file;
    PutU32(file, 0x02000000u);
    PutU32(file, (uint32_t)c.size());
    PutU32(file, (uint32_t)p.size());
    file.insert(file.end(), c.begin(), c.end());
    return file;
}

// ---- the OLD emission, verbatim in effect (pre-B941 hc_map.cpp) ----------
void OldRotate(glm::vec3 &v, float rx, float ry, float rz) {
    glm::vec3 nv = v;
    nv.y = (std::cos(rx) * v.y) - (std::sin(rx) * v.z);
    nv.z = (std::sin(rx) * v.y) + (std::cos(rx) * v.z);
    v = nv;
    nv.x = (std::cos(ry) * v.x) + (std::sin(ry) * v.z);
    nv.z = -(std::sin(ry) * v.x) + (std::cos(ry) * v.z);
    v = nv;
    nv.x = (std::cos(rz) * v.x) - (std::sin(rz) * v.y);
    nv.y = (std::sin(rz) * v.x) + (std::cos(rz) * v.y);
    v = nv;
}
void OldScale(glm::vec3 &v, float sx, float sy, float sz) { v.x *= sx; v.y *= sy; v.z *= sz; }
void OldTranslate(glm::vec3 &v, float tx, float ty, float tz) { v.x += tx; v.y += ty; v.z += tz; }

struct Soup { std::vector<glm::vec3> verts; std::vector<uint32_t> idx; size_t placeBegin = 0, placeEnd = 0; };

Soup OldEmit(const SynMap& m) {
    Soup s;
    for (const auto& v : m.mainVerts) s.verts.emplace_back(v.y, v.x, v.z);
    s.idx = m.mainIdx;
    s.placeBegin = m.mainIdx.size() / 3;
    size_t placeTris = 0;
    auto tri = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c) {
        s.verts.push_back(a); s.verts.push_back(b); s.verts.push_back(c);
        const uint32_t n = (uint32_t)s.verts.size();
        s.idx.push_back(n - 3); s.idx.push_back(n - 2); s.idx.push_back(n - 1);
        ++placeTris;
    };
    auto model = [&](const std::string& name) -> const SynModel* {
        for (const auto& md : m.models) if (md.name == name) return &md;
        return nullptr;
    };
    for (const auto& pl : m.placements) {
        const SynModel* md = model(pl.name);
        if (!md) continue;
        for (const auto& p : md->polys) {
            if (!p.vis) continue;
            glm::vec3 v[3] = {md->verts[p.a], md->verts[p.b], md->verts[p.c]};
            for (auto& x : v) OldRotate(x, pl.v[3], pl.v[4], pl.v[5]);
            for (auto& x : v) OldScale(x, pl.v[6], pl.v[7], pl.v[8]);
            for (auto& x : v) OldTranslate(x, pl.v[0], pl.v[1], pl.v[2]);
            tri(v[0], v[1], v[2]);
        }
    }
    for (const auto& g : m.groups) {
        const float x = g.v[0], y = g.v[1], z = g.v[2], x_rot = g.v[3], y_rot = g.v[4], z_rot = g.v[5];
        const float x_scale = g.v[6], y_scale = g.v[7], z_scale = g.v[8];
        const float x_tile = g.v[9], y_tile = g.v[10], z_tile = g.v[11];
        for (const auto& e : g.entries) {
            const SynModel* md = model(e.name);
            if (!md) continue;
            const float p_x = e.v[0], p_y = e.v[1], p_z = e.v[2];
            const float p_x_rot = e.v[3] * 3.14159f / 180, p_y_rot = e.v[4] * 3.14159f / 180, p_z_rot = e.v[5] * 3.14159f / 180;
            for (const auto& p : md->polys) {
                if (!p.vis) continue;
                glm::vec3 v[3] = {md->verts[p.a], md->verts[p.b], md->verts[p.c]};
                for (auto& q : v) OldScale(q, e.v[6], e.v[7], e.v[8]);
                for (auto& q : v) OldTranslate(q, p_x, p_y, p_z);
                for (auto& q : v) OldRotate(q, x_rot * 3.14159f / 180.0f, 0, 0);
                for (auto& q : v) OldRotate(q, 0, y_rot * 3.14159f / 180.0f, 0);
                glm::vec3 correction(p_x, p_y, p_z);
                OldRotate(correction, x_rot * 3.14159f / 180.0f, 0, 0);
                for (auto& q : v) OldTranslate(q, -correction.x, -correction.y, -correction.z);
                for (auto& q : v) OldRotate(q, p_x_rot, 0, 0);
                for (auto& q : v) OldRotate(q, 0, -p_y_rot, 0);
                for (auto& q : v) OldRotate(q, 0, 0, p_z_rot);
                for (auto& q : v) OldTranslate(q, correction.x, correction.y, correction.z);
                for (auto& q : v) OldRotate(q, 0, 0, z_rot * 3.14159f / 180.0f);
                for (auto& q : v) OldScale(q, x_scale, y_scale, z_scale);
                for (auto& q : v) OldTranslate(q, x_tile, y_tile, z_tile);
                for (auto& q : v) OldTranslate(q, x, y, z);
                tri(v[0], v[1], v[2]);
            }
        }
    }
    s.placeEnd = s.placeBegin + placeTris;
    const uint32_t q = m.quadsPerTile;
    const float u = m.unitsPerVertex;
    for (const auto& t : m.tiles) {
        if (t.flat) {
            const float x1 = t.x, y1 = t.y, x2 = x1 + q * u, y3 = y1 + q * u;
            const uint32_t cv = (uint32_t)s.verts.size() + 3;
            s.verts.emplace_back(y1, x1, t.z); s.verts.emplace_back(y1, x2, t.z);
            s.verts.emplace_back(y3, x2, t.z); s.verts.emplace_back(y3, x1, t.z);
            for (uint32_t i : {cv, cv - 2, cv - 1, cv, cv - 3, cv - 2}) s.idx.push_back(i);
            continue;
        }
        int row = -1;
        std::map<std::tuple<float, float, float>, uint32_t> cur;
        for (uint32_t quad = 0; quad < q * q; ++quad) {
            if (quad % q == 0) ++row;
            if (t.flags[quad] & 1) continue;
            const float X1 = t.x + row * u, Y1 = t.y + (quad % q) * u;
            const float c[4][3] = {{X1, Y1, t.floats[quad + row]},
                                   {X1 + u, Y1, t.floats[quad + row + q + 1]},
                                   {X1 + u, Y1 + u, t.floats[quad + row + q + 2]},
                                   {X1, Y1 + u, t.floats[quad + row + 1]}};
            uint32_t id[4];
            for (int k = 0; k < 4; ++k) {
                auto key = std::make_tuple(c[k][0], c[k][1], c[k][2]);
                auto it = cur.find(key);
                if (it != cur.end()) { id[k] = it->second; continue; }
                id[k] = (uint32_t)s.verts.size();
                s.verts.emplace_back(c[k][1], c[k][0], c[k][2]);
                cur[key] = id[k];
            }
            for (uint32_t i : {id[3], id[1], id[2], id[3], id[0], id[1]}) s.idx.push_back(i);
        }
    }
    for (auto& v : s.verts) std::swap(v.y, v.z);
    s.idx.resize(s.idx.size() / 3 * 3);
    return s;
}

}  // namespace

TEST(GameStateIdentity, SyntheticV2MatchesTheOldTriangleSoup) {
    const SynMap m = MakeSynMap();
    const std::vector<char> file = WriteSynV2(m);
    const std::string path = "/tmp/b941_syn_" + std::to_string(getpid()) + ".map";
    { std::ofstream f(path, std::ios::binary); f.write(file.data(), (std::streamsize)file.size()); }
    HCMap map;
    ASSERT_TRUE(map.Load(path));
    ::remove(path.c_str());

    const Soup old = OldEmit(m);
    const auto tris = map.GetTrianglesInRadius(glm::vec3(0.0f), 1.0e9f);
    ASSERT_EQ(tris.size(), old.idx.size() / 3);
    // B951: the map stores triangles in leaf order, so the comparison is the
    // multiset of (three raw vertices, placeable flag) records.
    std::vector<HCMap::Triangle> oldTris;
    for (size_t i = 0; i < old.idx.size() / 3; ++i) {
        HCMap::Triangle t{};
        t.v1 = old.verts[old.idx[i * 3]];
        t.v2 = old.verts[old.idx[i * 3 + 1]];
        t.v3 = old.verts[old.idx[i * 3 + 2]];
        t.isPlaceable = i >= old.placeBegin && i < old.placeEnd;
        oldTris.push_back(t);
    }
    const auto got = SortedTriangleBits(tris);
    const auto want = SortedTriangleBits(oldTris);
    ASSERT_EQ(got.size(), want.size());
    for (size_t i = 0; i < got.size(); ++i) {
        ASSERT_EQ(got[i], want[i]) << "sorted triangle record " << i;
    }
    // The dedup must actually have happened (fewer stored vertices than the soup).
    EXPECT_LT(map.GetMemoryStats().vertexCount, old.verts.size());

    // Rays: the old soup through the SAME tree code (pointer factory) vs the map.
    auto same = [](const glm::vec3& a, const glm::vec3& b) { return std::memcmp(&a, &b, sizeof(a)) == 0; };
    std::vector<float> flat;
    for (const auto& v : old.verts) { flat.push_back(v.x); flat.push_back(v.y); flat.push_back(v.z); }
    RaycastMesh* ref = createRaycastMesh((RmUint32)old.verts.size(), flat.data(), (RmUint32)(old.idx.size() / 3), old.idx.data());
    ASSERT_NE(ref, nullptr);
    MapBounds b{};
    size_t n = 0;
    TriangleDigest(map, b, n);
    Lcg r(5);
    int groundHits = 0, losBlocked = 0;
    for (int i = 0; i < 3000; ++i) {
        const glm::vec3 a(r.in(b.minX, b.maxX), r.in(b.minZ, b.maxZ), r.in(b.minY - 5, b.maxY + 20));
        const glm::vec3 e(r.in(b.minX, b.maxX), r.in(b.minZ, b.maxZ), r.in(b.minY - 5, b.maxY + 20));
        // FindBestZ: ray from (x, z+10, y) straight down, mesh coords.
        glm::vec3 got(0.0f);
        const float z = map.FindBestZ(a, &got);
        const glm::vec3 from(a.x, a.z + 10.0f, a.y), to(a.x, BEST_Z_INVALID, a.y);
        glm::vec3 hit(0.0f);
        float dist = 0.0f;
        const bool h = ref->raycast(&from.x, &to.x, &hit.x, nullptr, &dist);
        if (h) {
            ++groundHits;
            EXPECT_TRUE(same(glm::vec3(got.x, got.y, got.z), glm::vec3(hit.x, hit.z, hit.y))) << "FindBestZ " << i;
            EXPECT_EQ(std::memcmp(&z, &hit.y, 4), 0) << "FindBestZ " << i;
        } else {
            EXPECT_EQ(z, (float)BEST_Z_INVALID) << "FindBestZ " << i;
        }
        // LOS with hit.
        glm::vec3 losHit(0.0f);
        const bool clear = map.CheckLOSWithHit(a, e, &losHit);
        const glm::vec3 ms(a.x, a.z, a.y), me(e.x, e.z, e.y);
        glm::vec3 rh(0.0f);
        const bool blocked = ref->raycast(&ms.x, &me.x, &rh.x, nullptr, nullptr);
        EXPECT_EQ(clear, !blocked) << "LOS " << i;
        if (blocked) ++losBlocked;
        if (blocked) EXPECT_TRUE(same(losHit, glm::vec3(rh.x, rh.z, rh.y))) << "LOS hit " << i;
    }
    ref->release();
    // Not vacuous: a real share of both query kinds hit geometry.
    EXPECT_GT(groundHits, 300);
    EXPECT_GT(losBlocked, 300);
}
