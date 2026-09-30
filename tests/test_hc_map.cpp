// T19: hc_map.cpp (HCMap V1/V2 binary map format parser) unit tests.
//
// Real-fixture tests load actual EQEmu zone maps from
// /mnt/projects/akk-stack/server/maps/base/ (READ-ONLY server install data,
// override via WILLEQ_MAP_FIXTURES env var). They GTEST_SKIP cleanly when
// the fixture directory isn't present, following the WILLEQ_SESSION_LOG
// pattern in test_login_world_flow.cpp.
//
// Header layout (discovered by reading src/client/hc_map.cpp, confirmed by
// loading real fixtures -- see refactor/T19_map_zone_lines.md Review):
//
//   uint32_t version;              // 0x01000000 (V1) or 0x02000000 (V2)
//
//   V1 (uncompressed triangle soup):
//     uint32_t face_count;
//     uint16_t node_count;         // read but unused by the parser
//     uint32_t facelist_count;     // read but unused by the parser
//     repeated face_count times: glm::vec3 a, b, c; float normals[4];
//
//   V2 (zlib-compressed, decompressed payload holds the real structure):
//     uint32_t data_size;          // compressed byte length on disk
//     uint32_t buffer_size;        // exact decompressed byte length
//     <data_size bytes of zlib-compressed data>
//     -- after EQ::InflateData() into a buffer_size-byte vector<char> --
//     uint32_t vert_count, ind_count, nc_vert_count, nc_ind_count,
//              model_count, plac_count, plac_group_count, tile_count,
//              quads_per_tile;
//     float    units_per_vertex;
//     ... vertices/indices/models/placements/placement-groups/terrain
//         tiles, all read via unchecked pointer-advance (read_val<T>) with
//         NO bounds check against the buffer_size-sized decompressed
//         buffer. This is BUG_HCMap_V2_OversizedVertCount_CrashesLoad below.
//
// Both real fixtures (befallen.map, qeynos2.map) turned out to be V2. A
// real V1 fixture exists too (sky.map, one of only 6 V1 files under
// maps/base/ -- the format is legacy).
//
// FindBestZ/CheckLOS/CheckLOSWithHit take EQ server coords (Z-up); the
// class stores/raycasts in Irrlicht mesh coords (Y-up) and converts at the
// API boundary (see hc_map.h coordinate system comment).

#include <gtest/gtest.h>
#include "client/hc_map.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>
#include <zlib.h>
#include <algorithm>
#include <sstream>
#include <iterator>
#include <glm/glm.hpp>

namespace {

std::string MapFixtureDir() {
    if (const char* env = std::getenv("WILLEQ_MAP_FIXTURES")) {
        return env;
    }
    return "/mnt/projects/akk-stack/server/maps/base";
}

// Returns empty string (and skips) if the named fixture can't be opened.
std::string RequireMapFixture(const std::string& filename) {
    std::string path = MapFixtureDir() + "/" + filename;
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        return "";
    }
    return path;
}

std::string TempFilePath(const std::string& tag) {
    return "/tmp/t19_hc_map_" + tag + "_" + std::to_string(getpid()) + ".map";
}

void WriteFile(const std::string& path, const std::vector<char>& bytes) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(bytes.data(), (std::streamsize)bytes.size());
}

void AppendU32(std::vector<char>& buf, uint32_t v) {
    buf.insert(buf.end(), reinterpret_cast<char*>(&v), reinterpret_cast<char*>(&v) + 4);
}
void AppendU16(std::vector<char>& buf, uint16_t v) {
    buf.insert(buf.end(), reinterpret_cast<char*>(&v), reinterpret_cast<char*>(&v) + 2);
}
void AppendFloat(std::vector<char>& buf, float v) {
    buf.insert(buf.end(), reinterpret_cast<char*>(&v), reinterpret_cast<char*>(&v) + 4);
}
void AppendBool(std::vector<char>& buf, bool v) {
    buf.push_back(v ? 1 : 0);
}

std::vector<char> ZlibCompress(const std::vector<char>& in) {
    uLongf boundLen = compressBound((uLong)in.size());
    std::vector<char> out(boundLen);
    uLongf destLen = boundLen;
    int rc = compress2(reinterpret_cast<Bytef*>(out.data()), &destLen,
                        reinterpret_cast<const Bytef*>(in.data()), (uLong)in.size(),
                        Z_BEST_COMPRESSION);
    EXPECT_EQ(rc, Z_OK);
    out.resize(destLen);
    return out;
}

// Builds a minimal, exact-size, valid V1 map file: 1 triangle, no slack.
std::vector<char> BuildValidV1(uint32_t face_count = 1) {
    std::vector<char> buf;
    AppendU32(buf, 0x01000000u);
    AppendU32(buf, face_count);
    AppendU16(buf, 0);   // node_count (unused)
    AppendU32(buf, 0);   // facelist_count (unused)
    for (uint32_t i = 0; i < face_count; ++i) {
        AppendFloat(buf, 0.0f); AppendFloat(buf, 0.0f); AppendFloat(buf, 0.0f); // a
        AppendFloat(buf, 10.0f); AppendFloat(buf, 0.0f); AppendFloat(buf, 0.0f); // b
        AppendFloat(buf, 0.0f); AppendFloat(buf, 10.0f); AppendFloat(buf, 0.0f); // c
        AppendFloat(buf, 0.0f); AppendFloat(buf, 0.0f); AppendFloat(buf, 1.0f); AppendFloat(buf, 0.0f); // normals[4]
    }
    return buf;
}

// Builds a minimal, exact-size, valid V2 map file: header counts all zero
// except vert/ind for one degenerate triangle, decompressed buffer is
// exactly buffer_size bytes (no heap slack, per the T08 lesson).
std::vector<char> BuildValidV2(uint32_t vert_count, uint32_t ind_count,
                                const std::vector<float>* customVerts = nullptr,
                                const std::vector<uint32_t>* customInds = nullptr) {
    std::vector<char> payload;
    AppendU32(payload, vert_count);
    AppendU32(payload, ind_count);
    AppendU32(payload, 0); // nc_vert_count
    AppendU32(payload, 0); // nc_ind_count
    AppendU32(payload, 0); // model_count
    AppendU32(payload, 0); // plac_count
    AppendU32(payload, 0); // plac_group_count
    AppendU32(payload, 0); // tile_count
    AppendU32(payload, 0); // quads_per_tile
    AppendFloat(payload, 1.0f); // units_per_vertex

    if (customVerts) {
        for (float v : *customVerts) AppendFloat(payload, v);
    } else {
        for (uint32_t i = 0; i < vert_count; ++i) {
            AppendFloat(payload, (float)i);
            AppendFloat(payload, (float)i);
            AppendFloat(payload, 0.0f);
        }
    }
    if (customInds) {
        for (uint32_t idx : *customInds) AppendU32(payload, idx);
    } else {
        for (uint32_t i = 0; i < ind_count; ++i) {
            AppendU32(payload, i % (vert_count == 0 ? 1 : vert_count));
        }
    }

    std::vector<char> compressed = ZlibCompress(payload);

    std::vector<char> file;
    AppendU32(file, 0x02000000u);
    AppendU32(file, (uint32_t)compressed.size());
    AppendU32(file, (uint32_t)payload.size());
    file.insert(file.end(), compressed.begin(), compressed.end());
    return file;
}

// Builds a malicious V2 file: valid header/compression, but the embedded
// vert_count claims far more vertices than fit in the (exact-size,
// no-slack) decompressed buffer_size. See BUG_ test below.
std::vector<char> BuildMaliciousV2OversizedVertCount() {
    std::vector<char> payload;
    AppendU32(payload, 0x7FFFFFFFu); // vert_count: lies about how much data follows
    AppendU32(payload, 0);           // ind_count
    AppendU32(payload, 0); AppendU32(payload, 0); // nc_vert/ind_count
    AppendU32(payload, 0); AppendU32(payload, 0); AppendU32(payload, 0); // model/plac/plac_group
    AppendU32(payload, 0); // tile_count
    AppendU32(payload, 0); // quads_per_tile
    AppendFloat(payload, 1.0f); // units_per_vertex
    // Payload ends here -- buffer_size below is exactly payload.size(), no
    // trailing slack, so the parser's read_val<float> calls for the
    // (nonexistent) 0x7FFFFFFF vertices run straight off the end of the
    // heap-allocated std::vector<char> buffer.

    std::vector<char> compressed = ZlibCompress(payload);

    std::vector<char> file;
    AppendU32(file, 0x02000000u);
    AppendU32(file, (uint32_t)compressed.size());
    AppendU32(file, (uint32_t)payload.size());
    file.insert(file.end(), compressed.begin(), compressed.end());
    return file;
}

} // namespace

// ============================================================================
// 1. Real fixtures: happy path, pinned discovery-run values.
// ============================================================================

TEST(HCMapRealFixtures, BefallenV2_LoadsAndReportsPinnedStats) {
    std::string path = RequireMapFixture("befallen.map");
    if (path.empty()) {
        GTEST_SKIP() << "No map fixture at " << MapFixtureDir()
                     << "/befallen.map (set WILLEQ_MAP_FIXTURES to override)";
    }
    HCMap map;
    ASSERT_TRUE(map.Load(path));
    ASSERT_TRUE(map.IsLoaded());

    // Pinned via discovery run (2026-07-31): befallen.map's first 4 bytes
    // are 0x02000000 -- it is V2, not V1, despite being a "classic" zone.
    auto stats = map.GetMemoryStats();
    // B941 (#259): STORED vertices -- a placed model's vertices are held once
    // per placement, not three per triangle (was 69534). Faces, bounds and
    // every query are unchanged (tests/test_game_state_identity.cpp).
    EXPECT_EQ(stats.vertexCount, 23382u);
    EXPECT_EQ(stats.faceCount, 43018u);

    float minY, maxY, minZ, maxZ;
    map.GetVertexBounds(minY, maxY);
    map.GetZRange(minZ, maxZ);
    EXPECT_NEAR(minY, -90.622269f, 0.001f);
    EXPECT_NEAR(maxY, 26.143688f, 0.001f);
    // GetZRange tracks the same Irrlicht-Y-up vertical extent as
    // GetVertexBounds (both are populated from m_impl->minZ/maxZ, which --
    // despite the name -- hold the Y-up vertical range; see hc_map.cpp).
    EXPECT_FLOAT_EQ(minZ, minY);
    EXPECT_FLOAT_EQ(maxZ, maxY);
}

TEST(HCMapRealFixtures, BefallenV2_FindBestZPinnedValues) {
    std::string path = RequireMapFixture("befallen.map");
    if (path.empty()) {
        GTEST_SKIP() << "No map fixture at " << MapFixtureDir() << "/befallen.map";
    }
    HCMap map;
    ASSERT_TRUE(map.Load(path));

    // EQ server coords in, EQ server coords out. Pinned via discovery run.
    glm::vec3 result;
    EXPECT_NEAR(map.FindBestZ(glm::vec3(0.0f, 0.0f, 1000.0f), &result), 14.0f, 0.001f);
    EXPECT_NEAR(result.x, 0.0f, 0.001f);
    EXPECT_NEAR(result.y, 0.0f, 0.001f);
    EXPECT_NEAR(result.z, 14.0f, 0.001f);

    // Testchar's documented restore position for zone_id=36 (befallen), per
    // refactor/EXECUTOR_PROTOCOL.md eqemu section: x=-77.9232, y=21.9086.
    EXPECT_NEAR(map.FindBestZ(glm::vec3(-77.9232f, 21.9086f, 1000.0f)), 14.0f, 0.001f);

    // Points that fall outside the loaded mesh's XY footprint: no floor,
    // BEST_Z_INVALID.
    EXPECT_EQ(map.FindBestZ(glm::vec3(50.0f, 50.0f, 1000.0f)), (float)BEST_Z_INVALID);
    EXPECT_EQ(map.FindBestZ(glm::vec3(100.0f, 0.0f, 1000.0f)), (float)BEST_Z_INVALID);
}

TEST(HCMapRealFixtures, BefallenV2_CheckLOSPinnedValues) {
    std::string path = RequireMapFixture("befallen.map");
    if (path.empty()) {
        GTEST_SKIP() << "No map fixture at " << MapFixtureDir() << "/befallen.map";
    }
    HCMap map;
    ASSERT_TRUE(map.Load(path));

    // Straight down through solid floor: blocked (CheckLOS false).
    EXPECT_FALSE(map.CheckLOS(glm::vec3(0.0f, 0.0f, 50.0f), glm::vec3(0.0f, 0.0f, -50.0f)));
    glm::vec3 hit;
    EXPECT_FALSE(map.CheckLOSWithHit(glm::vec3(0.0f, 0.0f, 50.0f), glm::vec3(0.0f, 0.0f, -50.0f), &hit));
    EXPECT_NEAR(hit.x, 0.0f, 0.001f);
    EXPECT_NEAR(hit.y, 0.0f, 0.001f);
    EXPECT_NEAR(hit.z, 14.0f, 0.001f);

    // Horizontal ray at z=20 (above the z=14 floor near the origin, and the
    // whole mesh only spans Y-up -90.6..26.1) reaches far outside the
    // mesh's footprint: clear LOS.
    EXPECT_TRUE(map.CheckLOS(glm::vec3(0.0f, 0.0f, 20.0f), glm::vec3(1000.0f, 1000.0f, 20.0f)));
}

TEST(HCMapRealFixtures, Qeynos2V2_LoadsAndFindsZ) {
    std::string path = RequireMapFixture("qeynos2.map");
    if (path.empty()) {
        GTEST_SKIP() << "No map fixture at " << MapFixtureDir() << "/qeynos2.map";
    }
    HCMap map;
    ASSERT_TRUE(map.Load(path));
    ASSERT_TRUE(map.IsLoaded());

    auto stats = map.GetMemoryStats();
    EXPECT_EQ(stats.vertexCount, 13432u);  // B941: stored once per placement (was 32276)
    EXPECT_EQ(stats.faceCount, 24285u);

    float minY, maxY;
    map.GetVertexBounds(minY, maxY);
    EXPECT_NEAR(minY, -390.843750f, 0.001f);
    EXPECT_NEAR(maxY, 79.968750f, 0.001f);

    EXPECT_NEAR(map.FindBestZ(glm::vec3(0.0f, 0.0f, 1000.0f)), 0.0f, 0.001f);
    EXPECT_NEAR(map.FindBestZ(glm::vec3(100.0f, 0.0f, 1000.0f)), 30.0f, 0.001f);
    // (-50, 50) is a miss in this zone's footprint (discovery run).
    EXPECT_EQ(map.FindBestZ(glm::vec3(-50.0f, 50.0f, 1000.0f)), (float)BEST_Z_INVALID);
}

TEST(HCMapRealFixtures, Qeynos2V2_CheckLOSHorizontalHitsGeometry) {
    std::string path = RequireMapFixture("qeynos2.map");
    if (path.empty()) {
        GTEST_SKIP() << "No map fixture at " << MapFixtureDir() << "/qeynos2.map";
    }
    HCMap map;
    ASSERT_TRUE(map.Load(path));

    // Unlike befallen, this long horizontal ray IS blocked in qeynos2
    // (North Qeynos has geometry far out along this diagonal) -- pinned via
    // discovery run, illustrates the fixtures aren't interchangeable.
    glm::vec3 hit;
    EXPECT_FALSE(map.CheckLOSWithHit(glm::vec3(0.0f, 0.0f, 20.0f), glm::vec3(1000.0f, 1000.0f, 20.0f), &hit));
    EXPECT_NEAR(hit.x, 44.7092f, 0.01f);
    EXPECT_NEAR(hit.y, 44.7092f, 0.01f);
    EXPECT_NEAR(hit.z, 20.0f, 0.01f);
}

TEST(HCMapRealFixtures, SkyV1_LoadsPinnedStats) {
    // sky.map is one of only 6 remaining V1-format fixtures under
    // maps/base/ (507 are V2) -- the only real-fixture coverage available
    // for the V1 branch (version == 0x01000000) in HCMap::Load.
    std::string path = RequireMapFixture("sky.map");
    if (path.empty()) {
        GTEST_SKIP() << "No map fixture at " << MapFixtureDir() << "/sky.map";
    }
    HCMap map;
    ASSERT_TRUE(map.Load(path));
    ASSERT_TRUE(map.IsLoaded());

    auto stats = map.GetMemoryStats();
    EXPECT_EQ(stats.vertexCount, 77496u);
    EXPECT_EQ(stats.faceCount, 25832u);

    // V1 loader marks every triangle non-placeable (no placement/model
    // sections exist in the format) -- GetAllTerrainTriangles should
    // therefore return every triangle.
    auto terrain = map.GetAllTerrainTriangles();
    EXPECT_EQ(terrain.size(), stats.faceCount);

    EXPECT_NEAR(map.FindBestZ(glm::vec3(0.0f, 0.0f, 1000.0f)), 0.5578f, 0.001f);
}

// ============================================================================
// 2. Synthetic malformed input: exact-size buffers (T08 lesson -- no heap
//    slack to hide over-reads), unique per-PID temp paths (T03b hygiene).
// ============================================================================

class HCMapMalformedTest : public ::testing::Test {
protected:
    std::vector<std::string> filesToClean_;

    std::string WriteTemp(const std::string& tag, const std::vector<char>& bytes) {
        std::string path = TempFilePath(tag);
        WriteFile(path, bytes);
        filesToClean_.push_back(path);
        return path;
    }

    void TearDown() override {
        for (auto& p : filesToClean_) {
            ::remove(p.c_str());
        }
    }
};

TEST_F(HCMapMalformedTest, ZeroSizeFile_FailsGracefully) {
    std::string path = WriteTemp("zero", {});
    HCMap map;
    EXPECT_FALSE(map.Load(path));
    EXPECT_FALSE(map.IsLoaded());
}

TEST_F(HCMapMalformedTest, NonexistentFile_FailsGracefully) {
    HCMap map;
    EXPECT_FALSE(map.Load("/tmp/t19_hc_map_does_not_exist_" + std::to_string(getpid()) + ".map"));
    EXPECT_FALSE(map.IsLoaded());
}

TEST_F(HCMapMalformedTest, WrongMagicVersion_FailsGracefully) {
    std::vector<char> buf;
    AppendU32(buf, 0xDEADBEEFu);
    AppendU32(buf, 0x12345678u); // some trailing bytes, shouldn't matter
    std::string path = WriteTemp("wrongmagic", buf);
    HCMap map;
    EXPECT_FALSE(map.Load(path));
    EXPECT_FALSE(map.IsLoaded());
}

TEST_F(HCMapMalformedTest, TruncatedAfterVersionOnly_FailsGracefully) {
    std::vector<char> buf;
    AppendU32(buf, 0x01000000u);
    std::string path = WriteTemp("trunc_version_only", buf);
    HCMap map;
    EXPECT_FALSE(map.Load(path));
    EXPECT_FALSE(map.IsLoaded());
}

TEST_F(HCMapMalformedTest, V1_TruncatedMidHeader_FailsGracefully) {
    std::vector<char> buf;
    AppendU32(buf, 0x01000000u);
    AppendU32(buf, 5); // face_count claims 5 faces
    // ...but the file ends right after face_count, before node_count.
    std::string path = WriteTemp("v1_trunc_header", buf);
    HCMap map;
    EXPECT_FALSE(map.Load(path));
    EXPECT_FALSE(map.IsLoaded());
}

TEST_F(HCMapMalformedTest, V1_TruncatedMidFaceData_FailsGracefully) {
    // A valid V1 header claiming 5 faces, but the file is cut off partway
    // through the first face's vertex data. fread()'s return-value checks
    // in the V1 branch catch this cleanly (contrast with the V2 BUG_ test
    // below, which has no equivalent check).
    std::vector<char> buf;
    AppendU32(buf, 0x01000000u);
    AppendU32(buf, 5);   // face_count: claims 5, file only has partial data for 1
    AppendU16(buf, 0);
    AppendU32(buf, 0);
    AppendFloat(buf, 1.0f); AppendFloat(buf, 2.0f); // incomplete vertex `a`
    std::string path = WriteTemp("v1_trunc_face", buf);
    HCMap map;
    EXPECT_FALSE(map.Load(path));
    EXPECT_FALSE(map.IsLoaded());
}

TEST_F(HCMapMalformedTest, V1_ZeroFaceCount_LoadsEmptyButValid) {
    // Zero faces is a degenerate-but-legal V1 map: createRaycastMesh gets
    // 0 verts/faces. HCMap::Load only sets loaded=true if
    // createRaycastMesh() returns non-null; confirm the actual behavior
    // rather than assuming.
    std::vector<char> buf = BuildValidV1(0);
    std::string path = WriteTemp("v1_zero_faces", buf);
    HCMap map;
    bool ok = map.Load(path);
    // Whichever way createRaycastMesh(0, nullptr-ish, 0, ...) resolves,
    // IsLoaded() must be consistent with the Load() return value (no
    // state where Load() reports true but the map is unusable, or vice
    // versa via a stale m_impl).
    EXPECT_EQ(ok, map.IsLoaded());
}

TEST_F(HCMapMalformedTest, V1_OneValidFace_RoundTrips) {
    std::vector<char> buf = BuildValidV1(1);
    std::string path = WriteTemp("v1_one_face", buf);
    HCMap map;
    ASSERT_TRUE(map.Load(path));
    ASSERT_TRUE(map.IsLoaded());
    auto stats = map.GetMemoryStats();
    EXPECT_EQ(stats.faceCount, 1u);
    EXPECT_EQ(stats.vertexCount, 3u);
}

TEST_F(HCMapMalformedTest, V2_ValidMinimalTriangle_RoundTrips) {
    std::vector<char> buf = BuildValidV2(3, 3);
    std::string path = WriteTemp("v2_minimal", buf);
    HCMap map;
    ASSERT_TRUE(map.Load(path));
    ASSERT_TRUE(map.IsLoaded());
    auto stats = map.GetMemoryStats();
    EXPECT_EQ(stats.faceCount, 1u);
    EXPECT_EQ(stats.vertexCount, 3u);
}

TEST_F(HCMapMalformedTest, V2_TruncatedAfterSizesOnly_FailsGracefully) {
    std::vector<char> buf;
    AppendU32(buf, 0x02000000u);
    AppendU32(buf, 100); // data_size: claims 100 compressed bytes
    AppendU32(buf, 200); // buffer_size
    // ...but no compressed payload bytes follow at all.
    std::string path = WriteTemp("v2_trunc_sizes", buf);
    HCMap map;
    EXPECT_FALSE(map.Load(path));
    EXPECT_FALSE(map.IsLoaded());
}

TEST_F(HCMapMalformedTest, V2_TruncatedCompressedBody_FailsGracefully) {
    // data_size claims more compressed bytes than the file actually has --
    // fread(..., data_size, 1, f) returns 0 (not 1), caught by the explicit
    // check in the V2 branch.
    std::vector<char> valid = BuildValidV2(3, 3);
    // valid layout: [4 version][4 data_size][4 buffer_size][data_size bytes]
    uint32_t realDataSize;
    memcpy(&realDataSize, valid.data() + 4, 4);
    uint32_t liarDataSize = realDataSize + 1000; // lie: claim far more than present
    memcpy(valid.data() + 4, &liarDataSize, 4);
    std::string path = WriteTemp("v2_trunc_body", valid);
    HCMap map;
    EXPECT_FALSE(map.Load(path));
    EXPECT_FALSE(map.IsLoaded());
}

TEST_F(HCMapMalformedTest, V2_CorruptedCompressedData_FailsGracefully) {
    // Valid header/sizes, but the "compressed" bytes are garbage -- zlib's
    // inflate() should fail, EQ::InflateData() returns 0, and the explicit
    // `decompressed_size == 0` check in the V2 branch catches it.
    std::vector<char> buf;
    AppendU32(buf, 0x02000000u);
    AppendU32(buf, 16); // data_size
    AppendU32(buf, 64); // buffer_size
    for (int i = 0; i < 16; ++i) buf.push_back((char)0xFF); // not valid zlib
    std::string path = WriteTemp("v2_corrupt_compressed", buf);
    HCMap map;
    EXPECT_FALSE(map.Load(path));
    EXPECT_FALSE(map.IsLoaded());
}

TEST_F(HCMapMalformedTest, V2_ZeroBufferSize_FailsGracefully) {
    // buffer_size=0 means EQ::InflateData()'s avail_out=0; inflate() can't
    // reach Z_STREAM_END with nonzero input pending, so it returns 0 and
    // the decompressed_size==0 check catches it before any buffer access.
    std::vector<char> payload = {1, 2, 3, 4};
    std::vector<char> compressed = ZlibCompress(payload);
    std::vector<char> file;
    AppendU32(file, 0x02000000u);
    AppendU32(file, (uint32_t)compressed.size());
    AppendU32(file, 0); // buffer_size = 0
    file.insert(file.end(), compressed.begin(), compressed.end());
    std::string path = WriteTemp("v2_zero_buffer", file);
    HCMap map;
    EXPECT_FALSE(map.Load(path));
    EXPECT_FALSE(map.IsLoaded());
}

// ============================================================================
// 3. Malformed V2 payloads -- was a crash (T19 finding), fixed in T20.
// ============================================================================

// T19 found that the V2 branch's decompressed-payload parser read every
// field with an unchecked memcpy-and-advance over a raw `char*`, never
// comparing the advancing pointer against the buffer end. vert_count,
// ind_count, model_count, plac_count, plac_group_count and tile_count are
// all corruption/attacker-controlled counts read from inside the payload
// itself (buffer_size bounds only the *total* decompressed size, not any
// individual count), so a payload lying about how much vertex data follows
// read straight past the end of the heap buffer -- reproduced as a SIGSEGV.
//
// T20 made the readers bounds-checked (they throw MapParseError, caught by
// Load()), so the same payload must now fail the load cleanly instead of
// crashing. This test was ASSERT_DEATH before the fix.
TEST(HCMapMalformedV2, OversizedVertCountFailsLoadWithoutCrashing) {
    std::vector<char> file = BuildMaliciousV2OversizedVertCount();
    std::string path = TempFilePath("oversized_vertcount");
    WriteFile(path, file);

    HCMap map;
    EXPECT_NO_THROW({
        EXPECT_FALSE(map.Load(path));
    });
    EXPECT_FALSE(map.IsLoaded());

    ::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// T504d: the raycast BVH's identity net. A fixed grid of FindBestZ and
// CheckLOSWithHit queries over each fixture map, printed to 3 decimals and
// compared to a committed golden. The BVH compaction (T504d) must leave every
// line unchanged: the queries are pure functions of the same triangles.
// Bless (regenerate) with WILLEQ_BLESS_HC_MAP_RAYS=1; the golden dir is
// tests/goldens/hc_map/ (WILLEQ_HC_MAP_GOLDENS overrides).
// ---------------------------------------------------------------------------
namespace {
std::string HcMapGoldenDir() {
    if (const char* env = std::getenv("WILLEQ_HC_MAP_GOLDENS")) return env;
    return std::string(WILLEQ_SOURCE_DIR) + "/tests/goldens/hc_map";
}

std::string RaySetReport(const HCMap& map) {
    // XY bounds from the triangles themselves (the map exposes no XY range).
    auto tris = map.GetTrianglesInRadius(glm::vec3(0.0f), 1.0e9f);
    if (tris.empty()) return "";
    float minX = 1e30f, maxX = -1e30f, minZ = 1e30f, maxZ = -1e30f;
    for (const auto& t : tris) {
        for (const glm::vec3* v : {&t.v1, &t.v2, &t.v3}) {
            minX = std::min(minX, v->x); maxX = std::max(maxX, v->x);
            minZ = std::min(minZ, v->z); maxZ = std::max(maxZ, v->z);
        }
    }
    float minY = 0.0f, maxY = 0.0f;
    map.GetVertexBounds(minY, maxY);
    const int n = 24;
    char line[256];
    std::string out;
    std::snprintf(line, sizeof(line), "bounds x=[%.3f,%.3f] y=[%.3f,%.3f] z=[%.3f,%.3f] tris=%zu\n",
                  minX, maxX, minY, maxY, minZ, maxZ, tris.size());
    out += line;
    glm::vec3 prev(0.0f);
    bool havePrev = false;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            const float x = minX + (maxX - minX) * (i + 0.5f) / n;
            const float z = minZ + (maxZ - minZ) * (j + 0.5f) / n;
            glm::vec3 start(x, maxY + 10.0f, z), result(0.0f);
            const float best = map.FindBestZ(start, &result);
            std::snprintf(line, sizeof(line), "%d %d best=%.3f at=%.3f,%.3f,%.3f", i, j, best,
                          result.x, result.y, result.z);
            out += line;
            if (havePrev) {
                glm::vec3 hit(0.0f);
                const glm::vec3 a(prev.x, prev.y + 2.0f, prev.z), b(result.x, result.y + 2.0f, result.z);
                const bool los = map.CheckLOSWithHit(a, b, &hit);
                std::snprintf(line, sizeof(line), " los=%d hit=%.3f,%.3f,%.3f", los ? 1 : 0, hit.x, hit.y, hit.z);
                out += line;
            }
            out += "\n";
            prev = result;
            havePrev = true;
        }
    }
    return out;
}

void CheckRaySetGolden(const char* zone) {
    const std::string path = RequireMapFixture(std::string(zone) + ".map");
    if (path.empty()) GTEST_SKIP() << "No map fixture for " << zone;
    HCMap map;
    ASSERT_TRUE(map.Load(path));
    const std::string report = RaySetReport(map);
    ASSERT_FALSE(report.empty());
    const std::string golden = HcMapGoldenDir() + "/" + zone + "_rays.txt";
    if (std::getenv("WILLEQ_BLESS_HC_MAP_RAYS")) {
        std::ofstream out(golden);
        ASSERT_TRUE(out.is_open()) << golden;
        out << report;
        return;
    }
    std::ifstream in(golden);
    ASSERT_TRUE(in.is_open()) << "no golden at " << golden << " (bless with WILLEQ_BLESS_HC_MAP_RAYS=1)";
    std::string expected((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (expected != report) {
        // Name the first differing line, not the whole 577-line report.
        std::istringstream a(expected), b(report);
        std::string la, lb;
        int lineNo = 0;
        while (true) {
            const bool ga = static_cast<bool>(std::getline(a, la));
            const bool gb = static_cast<bool>(std::getline(b, lb));
            ++lineNo;
            if (!ga && !gb) break;
            if (la != lb) {
                FAIL() << zone << " ray set differs at line " << lineNo << "\n  golden: " << la
                       << "\n  now:    " << lb;
            }
        }
    }
}
}  // namespace

TEST(HCMapRaySetGolden, Befallen)   { CheckRaySetGolden("befallen"); }
TEST(HCMapRaySetGolden, Gfaydark)   { CheckRaySetGolden("gfaydark"); }
TEST(HCMapRaySetGolden, Dreadlands) { CheckRaySetGolden("dreadlands"); }
TEST(HCMapRaySetGolden, Iceclad)    { CheckRaySetGolden("iceclad"); }
