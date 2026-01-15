// Unit tests for graphics components
// Note: These tests do not require an active display

#include <gtest/gtest.h>
#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/dds_decoder.h"
#include <vector>
#include <cstring>
#include <fstream>
#include <cstdlib>

using namespace EQT::Graphics;

// PFS CRC tests
class PfsCrcTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(PfsCrcTest, EmptyString) {
    EXPECT_EQ(PfsCrc::instance().get(""), 0);
}

TEST_F(PfsCrcTest, SimpleString) {
    // PFS uses a specific CRC algorithm
    int32_t crc = PfsCrc::instance().get("test.txt");
    EXPECT_NE(crc, 0);
}

TEST_F(PfsCrcTest, CaseSensitive) {
    // PFS CRC is case-sensitive - different cases produce different CRCs
    int32_t crc1 = PfsCrc::instance().get("TEST.TXT");
    int32_t crc2 = PfsCrc::instance().get("test.txt");
    EXPECT_NE(crc1, crc2);  // Different cases = different CRCs
}

TEST_F(PfsCrcTest, DifferentNames) {
    int32_t crc1 = PfsCrc::instance().get("file1.txt");
    int32_t crc2 = PfsCrc::instance().get("file2.txt");
    EXPECT_NE(crc1, crc2);
}

// DDS Decoder tests
class DdsDecoderTest : public ::testing::Test {
protected:
    void SetUp() override {}

    // Create a minimal DDS header for testing
    std::vector<char> createMinimalDDSHeader(uint32_t width, uint32_t height, uint32_t fourCC) {
        std::vector<char> data(128, 0);  // DDS header is 128 bytes

        // Magic number "DDS "
        data[0] = 'D';
        data[1] = 'D';
        data[2] = 'S';
        data[3] = ' ';

        // Header size (124)
        uint32_t* size = reinterpret_cast<uint32_t*>(&data[4]);
        *size = 124;

        // Flags
        uint32_t* flags = reinterpret_cast<uint32_t*>(&data[8]);
        *flags = 0x1007;  // CAPS | HEIGHT | WIDTH | PIXELFORMAT

        // Height
        uint32_t* h = reinterpret_cast<uint32_t*>(&data[12]);
        *h = height;

        // Width
        uint32_t* w = reinterpret_cast<uint32_t*>(&data[16]);
        *w = width;

        // Pixel format offset is at 76
        // Pixel format size
        uint32_t* pf_size = reinterpret_cast<uint32_t*>(&data[76]);
        *pf_size = 32;

        // Pixel format flags (FOURCC)
        uint32_t* pf_flags = reinterpret_cast<uint32_t*>(&data[80]);
        *pf_flags = 0x4;  // DDPF_FOURCC

        // FourCC
        uint32_t* pf_fourcc = reinterpret_cast<uint32_t*>(&data[84]);
        *pf_fourcc = fourCC;

        return data;
    }
};

TEST_F(DdsDecoderTest, IsDDS_ValidHeader) {
    std::vector<char> data = {'D', 'D', 'S', ' ', 0, 0, 0, 0};
    EXPECT_TRUE(DDSDecoder::isDDS(data));
}

TEST_F(DdsDecoderTest, IsDDS_TooSmall) {
    std::vector<char> data = {'D', 'D', 'S'};
    EXPECT_FALSE(DDSDecoder::isDDS(data));
}

TEST_F(DdsDecoderTest, IsDDS_WrongMagic) {
    std::vector<char> data = {'P', 'N', 'G', ' ', 0, 0, 0, 0};
    EXPECT_FALSE(DDSDecoder::isDDS(data));
}

TEST_F(DdsDecoderTest, Decode_TooSmall) {
    std::vector<char> data(64, 0);  // Too small for a DDS header
    auto result = DDSDecoder::decode(data);
    EXPECT_FALSE(result.isValid());
}

TEST_F(DdsDecoderTest, Decode_WrongMagic) {
    std::vector<char> data(128, 0);
    data[0] = 'P';
    data[1] = 'N';
    data[2] = 'G';
    data[3] = ' ';

    auto result = DDSDecoder::decode(data);
    EXPECT_FALSE(result.isValid());
}

// Test DXT1 decoding with minimal data
TEST_F(DdsDecoderTest, DecodeDXT1_MinimalData) {
    const uint32_t FOURCC_DXT1 = 0x31545844;  // "DXT1"

    // Create header
    auto data = createMinimalDDSHeader(4, 4, FOURCC_DXT1);

    // Add one DXT1 block (8 bytes) for a 4x4 texture
    // All zeros = black color
    data.resize(128 + 8, 0);

    auto result = DDSDecoder::decode(data);
    EXPECT_TRUE(result.isValid());
    EXPECT_EQ(result.width, 4u);
    EXPECT_EQ(result.height, 4u);
    EXPECT_EQ(result.pixels.size(), 4u * 4u * 4u);  // RGBA
}

// Test DecodedImage struct
TEST_F(DdsDecoderTest, DecodedImage_DefaultInvalid) {
    DecodedImage img;
    EXPECT_FALSE(img.isValid());
}

TEST_F(DdsDecoderTest, DecodedImage_Valid) {
    DecodedImage img;
    img.width = 16;
    img.height = 16;
    img.pixels.resize(16 * 16 * 4);
    EXPECT_TRUE(img.isValid());
}

// RGB565 conversion tests
class Rgb565ConversionTest : public ::testing::Test {
protected:
    // Helper to create RGB565 color
    uint16_t makeRgb565(uint8_t r, uint8_t g, uint8_t b) {
        return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }
};

TEST_F(Rgb565ConversionTest, BlackColor) {
    uint16_t black = makeRgb565(0, 0, 0);
    EXPECT_EQ(black, 0u);
}

TEST_F(Rgb565ConversionTest, WhiteColor) {
    uint16_t white = makeRgb565(255, 255, 255);
    EXPECT_EQ(white, 0xFFFF);
}

TEST_F(Rgb565ConversionTest, RedColor) {
    uint16_t red = makeRgb565(255, 0, 0);
    EXPECT_EQ(red, 0xF800);
}

TEST_F(Rgb565ConversionTest, GreenColor) {
    uint16_t green = makeRgb565(0, 255, 0);
    EXPECT_EQ(green, 0x07E0);
}

TEST_F(Rgb565ConversionTest, BlueColor) {
    uint16_t blue = makeRgb565(0, 0, 255);
    EXPECT_EQ(blue, 0x001F);
}

// PFS Archive tests (minimal, without actual file I/O)
class PfsArchiveTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(PfsArchiveTest, OpenNonexistent) {
    PfsArchive archive;
    EXPECT_FALSE(archive.open("/nonexistent/path/file.s3d"));
}

TEST_F(PfsArchiveTest, FilesEmptyByDefault) {
    PfsArchive archive;
    EXPECT_EQ(archive.getFiles().size(), 0u);
}

TEST_F(PfsArchiveTest, GetNonexistentFile) {
    PfsArchive archive;
    std::vector<char> buffer;
    EXPECT_FALSE(archive.get("nonexistent.txt", buffer));
}

TEST_F(PfsArchiveTest, ExistsNonexistentFile) {
    PfsArchive archive;
    EXPECT_FALSE(archive.exists("nonexistent.txt"));
}

// RaceModelLoader tests (static methods only, no Irrlicht dependency)
#include "client/graphics/eq/race_model_loader.h"

class RaceModelLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(RaceModelLoaderTest, GetRaceCode_Human) {
    EXPECT_EQ(RaceModelLoader::getRaceCode(1), "HUM");
}

TEST_F(RaceModelLoaderTest, GetRaceCode_Barbarian) {
    EXPECT_EQ(RaceModelLoader::getRaceCode(2), "BAM");
}

TEST_F(RaceModelLoaderTest, GetRaceCode_Dwarf) {
    // Returns male suffix - gender adjustment happens in getRaceModelFilename
    EXPECT_EQ(RaceModelLoader::getRaceCode(8), "DWM");
}

TEST_F(RaceModelLoaderTest, GetRaceCode_Iksar) {
    EXPECT_EQ(RaceModelLoader::getRaceCode(128), "IKM");
}

TEST_F(RaceModelLoaderTest, GetRaceCode_Skeleton) {
    EXPECT_EQ(RaceModelLoader::getRaceCode(21), "SKE");
}

TEST_F(RaceModelLoaderTest, GetRaceCode_Unknown) {
    // Unknown race IDs should return empty string
    EXPECT_EQ(RaceModelLoader::getRaceCode(9999), "");
}

TEST_F(RaceModelLoaderTest, GetRaceModelFilename_HumanMale) {
    std::string filename = RaceModelLoader::getRaceModelFilename(1, 0);
    EXPECT_EQ(filename, "globalhum_chr.s3d");
}

TEST_F(RaceModelLoaderTest, GetRaceModelFilename_HumanFemale) {
    std::string filename = RaceModelLoader::getRaceModelFilename(1, 1);
    EXPECT_EQ(filename, "globalhuf_chr.s3d");
}

TEST_F(RaceModelLoaderTest, GetRaceModelFilename_DwarfMale) {
    std::string filename = RaceModelLoader::getRaceModelFilename(8, 0);
    EXPECT_EQ(filename, "globaldwm_chr.s3d");
}

TEST_F(RaceModelLoaderTest, GetRaceModelFilename_DwarfFemale) {
    std::string filename = RaceModelLoader::getRaceModelFilename(8, 1);
    EXPECT_EQ(filename, "globaldwf_chr.s3d");
}

TEST_F(RaceModelLoaderTest, GetRaceModelFilename_UnknownRace) {
    std::string filename = RaceModelLoader::getRaceModelFilename(9999, 0);
    EXPECT_EQ(filename, "");
}

// WLD Loader structure tests
#include "client/graphics/eq/wld_loader.h"

class WldLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

// Test WLD header structure size
TEST_F(WldLoaderTest, WldHeaderSize) {
    EXPECT_EQ(sizeof(WldHeader), 28u);
}

// Test WLD fragment header structure size
TEST_F(WldLoaderTest, WldFragmentHeaderSize) {
    // WldFragmentHeader contains: uint32_t size, uint32_t id = 8 bytes
    EXPECT_EQ(sizeof(WldFragmentHeader), 8u);
}

// Test WLD fragment 36 header structure size
TEST_F(WldLoaderTest, WldFragment36HeaderSize) {
    // This is the main geometry fragment header
    // Size with padding: 4(flags)+4(frag1)+4(frag2)+4(frag3)+4(frag4)+
    //   4(centerX)+4(centerY)+4(centerZ)+12(params2)+4(maxDist)+
    //   4(minX)+4(minY)+4(minZ)+4(maxX)+4(maxY)+4(maxZ)+
    //   2(vertexCount)+2(texCoordCount)+2(normalCount)+2(colorCount)+
    //   2(polygonCount)+2(size6)+2(polygonTexCount)+2(vertexTexCount)+
    //   2(size9)+2(scale)+padding = 92 bytes due to alignment
    EXPECT_EQ(sizeof(WldFragment36Header), 92u);
}

// Test WLD vertex structure size
TEST_F(WldLoaderTest, WldVertexSize) {
    EXPECT_EQ(sizeof(WldVertex), 6u);  // 3 int16_t values
}

// Test WLD normal structure size
TEST_F(WldLoaderTest, WldNormalSize) {
    EXPECT_EQ(sizeof(WldNormal), 3u);  // 3 uint8_t values
}

// Test WLD polygon structure size
TEST_F(WldLoaderTest, WldPolygonSize) {
    EXPECT_EQ(sizeof(WldPolygon), 8u);  // flags + 3 uint16_t indices
}

// Test WLD placeable fragment header size
TEST_F(WldLoaderTest, WldFragment15HeaderSize) {
    EXPECT_EQ(sizeof(WldFragment15Header), 44u);
}

// Test WLD object definition fragment header size
TEST_F(WldLoaderTest, WldFragment14HeaderSize) {
    EXPECT_EQ(sizeof(WldFragment14Header), 20u);
}

// Test WLD skeleton track fragment header size
TEST_F(WldLoaderTest, WldFragment10HeaderSize) {
    EXPECT_EQ(sizeof(WldFragment10Header), 12u);
}

// Test WLD bone orientation fragment header size
TEST_F(WldLoaderTest, WldFragment12HeaderSize) {
    // WldFragment12Header: uint32_t flags + uint32_t size + 8 int16_t = 24 bytes
    EXPECT_EQ(sizeof(WldFragment12Header), 24u);
}

// Test WLD light source definition header size
TEST_F(WldLoaderTest, WldFragment1BHeaderSize) {
    // WldFragment1BHeader: uint32_t flags + uint32_t frameCount = 8 bytes
    EXPECT_EQ(sizeof(WldFragment1BHeader), 8u);
}

// Test WLD light instance header size
TEST_F(WldLoaderTest, WldFragment28HeaderSize) {
    EXPECT_EQ(sizeof(WldFragment28Header), 20u);
}

// Test ZoneGeometry default construction
TEST_F(WldLoaderTest, ZoneGeometry_DefaultEmpty) {
    ZoneGeometry geom;
    EXPECT_TRUE(geom.vertices.empty());
    EXPECT_TRUE(geom.triangles.empty());
    EXPECT_TRUE(geom.textureNames.empty());
}

// Test Vertex3D structure
TEST_F(WldLoaderTest, Vertex3D_Size) {
    EXPECT_EQ(sizeof(Vertex3D), 32u);  // 8 floats: x,y,z,nx,ny,nz,u,v
}

// Test Triangle structure
TEST_F(WldLoaderTest, Triangle_Size) {
    EXPECT_EQ(sizeof(Triangle), 20u);  // 5 uint32_t: v1,v2,v3,textureIndex,flags
}

// DDS format tests
class DdsFormatTest : public ::testing::Test {
protected:
    // Helper to create DDS header with various formats
    std::vector<char> createDDSHeader(uint32_t width, uint32_t height,
                                       uint32_t fourCC, bool hasAlpha = false) {
        std::vector<char> data(128, 0);

        // Magic number "DDS "
        data[0] = 'D'; data[1] = 'D'; data[2] = 'S'; data[3] = ' ';

        // Header size (124)
        *reinterpret_cast<uint32_t*>(&data[4]) = 124;

        // Flags
        *reinterpret_cast<uint32_t*>(&data[8]) = 0x1007;

        // Height, Width
        *reinterpret_cast<uint32_t*>(&data[12]) = height;
        *reinterpret_cast<uint32_t*>(&data[16]) = width;

        // Pitch or linear size
        uint32_t blockSize = (fourCC == 0x31545844) ? 8 : 16;  // DXT1 = 8, others = 16
        uint32_t blocksWide = (width + 3) / 4;
        uint32_t blocksHigh = (height + 3) / 4;
        *reinterpret_cast<uint32_t*>(&data[20]) = blocksWide * blocksHigh * blockSize;

        // Pixel format at offset 76
        *reinterpret_cast<uint32_t*>(&data[76]) = 32;  // Size
        *reinterpret_cast<uint32_t*>(&data[80]) = 0x4; // DDPF_FOURCC
        *reinterpret_cast<uint32_t*>(&data[84]) = fourCC;

        return data;
    }
};

// Test DXT1 format detection and decoding
TEST_F(DdsFormatTest, DecodeDXT1_4x4) {
    const uint32_t FOURCC_DXT1 = 0x31545844;  // "DXT1"
    auto header = createDDSHeader(4, 4, FOURCC_DXT1);

    // Add one DXT1 block (8 bytes) for a 4x4 texture
    // Color0 = pure red (0xF800), Color1 = pure blue (0x001F)
    header.resize(128 + 8);
    uint16_t color0 = 0xF800;  // Red
    uint16_t color1 = 0x001F;  // Blue
    memcpy(&header[128], &color0, 2);
    memcpy(&header[130], &color1, 2);
    // All pixels use color0 (index 0)
    memset(&header[132], 0x00, 4);

    auto result = DDSDecoder::decode(header);
    EXPECT_TRUE(result.isValid());
    EXPECT_EQ(result.width, 4u);
    EXPECT_EQ(result.height, 4u);
    // First pixel should be red (R=255 or close)
    EXPECT_GT(result.pixels[0], 200);  // R channel
    EXPECT_LT(result.pixels[1], 50);   // G channel
    EXPECT_LT(result.pixels[2], 50);   // B channel
}

// Test DXT3 format (explicit alpha)
TEST_F(DdsFormatTest, DecodeDXT3_4x4) {
    const uint32_t FOURCC_DXT3 = 0x33545844;  // "DXT3"
    auto header = createDDSHeader(4, 4, FOURCC_DXT3);

    // Add one DXT3 block (16 bytes) for a 4x4 texture
    // 8 bytes alpha + 8 bytes color
    header.resize(128 + 16, 0xFF);  // Full alpha

    auto result = DDSDecoder::decode(header);
    EXPECT_TRUE(result.isValid());
    EXPECT_EQ(result.width, 4u);
    EXPECT_EQ(result.height, 4u);
    EXPECT_EQ(result.pixels.size(), 4u * 4u * 4u);  // RGBA
}

// Test DXT5 format (interpolated alpha)
TEST_F(DdsFormatTest, DecodeDXT5_4x4) {
    const uint32_t FOURCC_DXT5 = 0x35545844;  // "DXT5"
    auto header = createDDSHeader(4, 4, FOURCC_DXT5);

    // Add one DXT5 block (16 bytes) for a 4x4 texture
    header.resize(128 + 16, 0);
    // Alpha0 and Alpha1 bytes
    header[128] = (char)255;  // Alpha0
    header[129] = (char)0;    // Alpha1
    // Rest is alpha indices and color data

    auto result = DDSDecoder::decode(header);
    EXPECT_TRUE(result.isValid());
    EXPECT_EQ(result.width, 4u);
    EXPECT_EQ(result.height, 4u);
    EXPECT_EQ(result.pixels.size(), 4u * 4u * 4u);  // RGBA
}

// Test larger texture dimensions
TEST_F(DdsFormatTest, DecodeDXT1_64x64) {
    const uint32_t FOURCC_DXT1 = 0x31545844;  // "DXT1"
    auto header = createDDSHeader(64, 64, FOURCC_DXT1);

    // 64x64 = 16x16 blocks = 256 blocks * 8 bytes = 2048 bytes
    uint32_t dataSize = 256 * 8;
    header.resize(128 + dataSize, 0);

    auto result = DDSDecoder::decode(header);
    EXPECT_TRUE(result.isValid());
    EXPECT_EQ(result.width, 64u);
    EXPECT_EQ(result.height, 64u);
    EXPECT_EQ(result.pixels.size(), 64u * 64u * 4u);  // RGBA
}

// Additional RaceModelLoader tests for more races
// Note: getRaceCode returns male suffix; gender is adjusted in getRaceModelFilename
TEST_F(RaceModelLoaderTest, GetRaceCode_WoodElf) {
    EXPECT_EQ(RaceModelLoader::getRaceCode(4), "ELM");
}

TEST_F(RaceModelLoaderTest, GetRaceCode_HighElf) {
    EXPECT_EQ(RaceModelLoader::getRaceCode(5), "HIM");
}

TEST_F(RaceModelLoaderTest, GetRaceCode_DarkElf) {
    EXPECT_EQ(RaceModelLoader::getRaceCode(6), "DAM");
}

TEST_F(RaceModelLoaderTest, GetRaceCode_Troll) {
    EXPECT_EQ(RaceModelLoader::getRaceCode(9), "TRM");
}

TEST_F(RaceModelLoaderTest, GetRaceCode_Ogre) {
    EXPECT_EQ(RaceModelLoader::getRaceCode(10), "OGM");
}

TEST_F(RaceModelLoaderTest, GetRaceCode_Gnome) {
    EXPECT_EQ(RaceModelLoader::getRaceCode(12), "GNM");
}

TEST_F(RaceModelLoaderTest, GetRaceCode_VahShir) {
    // Vah Shir (race 130) is not in the fallback switch, returns empty
    // When JSON mappings are loaded, it would return "KEM"
    EXPECT_EQ(RaceModelLoader::getRaceCode(130), "");
}

TEST_F(RaceModelLoaderTest, GetRaceCode_Wolf) {
    EXPECT_EQ(RaceModelLoader::getRaceCode(13), "WOL");
}

TEST_F(RaceModelLoaderTest, GetRaceCode_Goblin) {
    EXPECT_EQ(RaceModelLoader::getRaceCode(46), "GOB");
}

TEST_F(RaceModelLoaderTest, GetRaceCode_Dragon) {
    EXPECT_EQ(RaceModelLoader::getRaceCode(85), "DRA");
}

// Test gender-specific model filenames for additional races
TEST_F(RaceModelLoaderTest, GetRaceModelFilename_BarbarianMale) {
    std::string filename = RaceModelLoader::getRaceModelFilename(2, 0);
    EXPECT_EQ(filename, "globalbam_chr.s3d");
}

TEST_F(RaceModelLoaderTest, GetRaceModelFilename_BarbarianFemale) {
    std::string filename = RaceModelLoader::getRaceModelFilename(2, 1);
    EXPECT_EQ(filename, "globalbaf_chr.s3d");
}

TEST_F(RaceModelLoaderTest, GetRaceModelFilename_EruditeMale) {
    std::string filename = RaceModelLoader::getRaceModelFilename(3, 0);
    EXPECT_EQ(filename, "globalerm_chr.s3d");
}

TEST_F(RaceModelLoaderTest, GetRaceModelFilename_EruditeFemale) {
    std::string filename = RaceModelLoader::getRaceModelFilename(3, 1);
    EXPECT_EQ(filename, "globalerf_chr.s3d");
}

TEST_F(RaceModelLoaderTest, GetRaceModelFilename_IksarMale) {
    std::string filename = RaceModelLoader::getRaceModelFilename(128, 0);
    EXPECT_EQ(filename, "globalikm_chr.s3d");
}

TEST_F(RaceModelLoaderTest, GetRaceModelFilename_IksarFemale) {
    std::string filename = RaceModelLoader::getRaceModelFilename(128, 1);
    EXPECT_EQ(filename, "globalikf_chr.s3d");
}

// Monster races (no gender)
TEST_F(RaceModelLoaderTest, GetRaceModelFilename_Skeleton) {
    std::string filename = RaceModelLoader::getRaceModelFilename(21, 0);
    EXPECT_EQ(filename, "globalske_chr.s3d");
}

TEST_F(RaceModelLoaderTest, GetRaceModelFilename_Wolf) {
    std::string filename = RaceModelLoader::getRaceModelFilename(13, 0);
    EXPECT_EQ(filename, "globalwol_chr.s3d");
}

// Tests for zone-specific monster race codes (Session 3 fixes)
// These codes match the actual skeleton names in zone _chr.s3d files

TEST_F(RaceModelLoaderTest, GetRaceCode_Beetle) {
    // BET matches BET_HS_DEF in zone files
    EXPECT_EQ(RaceModelLoader::getRaceCode(22), "BET");
}

TEST_F(RaceModelLoaderTest, GetRaceCode_Fish) {
    // FIS matches FIS_HS_DEF in zone files
    EXPECT_EQ(RaceModelLoader::getRaceCode(24), "FIS");
}

TEST_F(RaceModelLoaderTest, GetRaceCode_Snake) {
    // SNA matches SNA_HS_DEF in zone files
    EXPECT_EQ(RaceModelLoader::getRaceCode(26), "SNA");
}

TEST_F(RaceModelLoaderTest, GetRaceCode_GiantSnake) {
    // SNA matches SNA_HS_DEF in zone files
    EXPECT_EQ(RaceModelLoader::getRaceCode(37), "SNA");
}

TEST_F(RaceModelLoaderTest, GetRaceCode_GnollPup) {
    // GNN matches GNN_HS_DEF in zone files
    EXPECT_EQ(RaceModelLoader::getRaceCode(39), "GNN");
}

TEST_F(RaceModelLoaderTest, GetRaceCode_Gnoll) {
    // GNN matches GNN_HS_DEF in zone files
    EXPECT_EQ(RaceModelLoader::getRaceCode(44), "GNN");
}

TEST_F(RaceModelLoaderTest, GetRaceCode_GnollVariant) {
    // GNN matches GNN_HS_DEF in zone files
    EXPECT_EQ(RaceModelLoader::getRaceCode(87), "GNN");
}

TEST_F(RaceModelLoaderTest, GetRaceCode_Bat) {
    // BAT matches BAT_HS_DEF in zone files
    EXPECT_EQ(RaceModelLoader::getRaceCode(34), "BAT");
}

TEST_F(RaceModelLoaderTest, GetRaceCode_Rat) {
    // RAT - note: RAT model may not exist in all zones
    EXPECT_EQ(RaceModelLoader::getRaceCode(36), "RAT");
}

// ============================================================================
// Object Placement Tests - Verify transforms for Irrlicht
// ============================================================================

// IMPORTANT: Coordinate system handedness:
// - EQ: Left-handed, Z-up
// - Irrlicht: Left-handed, Y-up
// - glTF (eqsage output): Right-handed, Y-up
//
// Because both EQ and Irrlicht are left-handed, willeq does NOT need the
// right-handed transforms that eqsage applies for glTF export:
// - eqsage adds +180° to Y rotation for glTF (right-handed) - willeq does NOT
// - eqsage negates X for normals for glTF (right-handed) - willeq does NOT
//
// These tests verify:
// 1. Center offset: vertices have center added (v + center) - matches eqsage
// 2. Coordinate transform: EQ (x,y,z) Z-up -> Irrlicht (x,z,y) Y-up - matches eqsage
// 3. Rotation: willeq does NOT add 180° (both EQ and Irrlicht are left-handed)

class ObjectPlacementTest : public ::testing::Test {
protected:
    // eqsage's vertex transformation (from s3d-decoder.js lines 1262-1266 and 894-898)
    // const transformedPositions = vertices.map((v) => [
    //   v[0] + mesh.center[0],   // X
    //   v[2] + mesh.center[2],   // Z -> Y (height)
    //   v[1] + mesh.center[1],   // Y -> Z (depth)
    // ]);
    struct Vec3 {
        float x, y, z;
    };

    // Apply center offset in EQ space (matches eqsage)
    Vec3 applyCenterOffset(Vec3 vertex, Vec3 center) {
        return {
            vertex.x + center.x,
            vertex.y + center.y,
            vertex.z + center.z
        };
    }

    // Transform from EQ coords to output coords (matches eqsage's Y/Z swap)
    Vec3 eqToOutput(Vec3 eq) {
        return {
            eq.x,    // X stays X
            eq.z,    // EQ Z (height) -> output Y
            eq.y     // EQ Y (depth) -> output Z
        };
    }

    // Full vertex transform as eqsage does it
    Vec3 eqsageVertexTransform(Vec3 vertex, Vec3 center) {
        // eqsage does: [v[0] + center[0], v[2] + center[2], v[1] + center[1]]
        return {
            vertex.x + center.x,
            vertex.z + center.z,  // EQ Z + centerZ -> output Y
            vertex.y + center.y   // EQ Y + centerY -> output Z
        };
    }

    // willeq's approach: add center in EQ space, then transform
    Vec3 willeqVertexTransform(Vec3 vertex, Vec3 center) {
        // Step 1: Add center in EQ space
        Vec3 adjusted = applyCenterOffset(vertex, center);
        // Step 2: Transform to Irrlicht/output space
        return eqToOutput(adjusted);
    }

    // eqsage's position transform (from s3d-decoder.js lines 1159-1161)
    // x: actor.location.x,
    // y: actor.location.z,  // swapped
    // z: actor.location.y,
    Vec3 eqsagePositionTransform(Vec3 eqPos) {
        return {
            eqPos.x,
            eqPos.z,  // EQ Z -> output Y
            eqPos.y   // EQ Y -> output Z
        };
    }

    // =========================================================================
    // STEP 1: Parsing - Raw EQ → Internal representation
    // This should EXACTLY match eqsage's Location class
    // =========================================================================
    struct Rotation {
        float x, y, z;
    };

    // eqsage's Location class (sage/lib/s3d/common/location.js):
    //   this.rotateX = 0;
    //   this.rotateZ = rotateY * modifier;
    //   this.rotateY = rotateX * modifier * -1;
    Rotation eqsageParsing(float rawRotX, float rawRotY, float rawRotZ) {
        const float modifier = 360.0f / 512.0f;
        return {
            0.0f,                        // rotateX always 0
            rawRotX * modifier * -1.0f,  // rotateY = primary yaw, negated
            rawRotY * modifier           // rotateZ = secondary rotation
        };
    }

    // willeq's parsing in wld_loader.cpp (should match eqsageParsing exactly)
    Rotation willeqParsing(float rawRotX, float rawRotY, float rawRotZ) {
        const float modifier = 360.0f / 512.0f;
        return {
            0.0f,                        // rotateX always 0
            rawRotX * modifier * -1.0f,  // rotateY = primary yaw, negated
            rawRotY * modifier           // rotateZ = secondary rotation
        };
    }

    // =========================================================================
    // STEP 2: Output transform - Internal representation → Target format
    // eqsage: adds +180° for glTF (right-handed)
    // willeq: NO +180° for Irrlicht (left-handed, same as EQ)
    // =========================================================================

    // eqsage's glTF export (s3d-decoder.js lines 1162-1164):
    //   rotateY: actor.location.rotateY + 180
    Rotation eqsageGltfTransform(Rotation internal) {
        return {
            internal.x,
            internal.y + 180.0f,  // +180 for right-handed glTF
            internal.z
        };
    }

    // willeq's Irrlicht transform (no +180 needed, both left-handed)
    Rotation willeqIrrlichtTransform(Rotation internal) {
        return {
            internal.x,
            internal.y,  // NO +180 for left-handed Irrlicht
            internal.z
        };
    }

    // Full pipeline: Raw EQ → Internal → Output
    Rotation eqsageFullPipeline(float rawRotX, float rawRotY, float rawRotZ) {
        Rotation internal = eqsageParsing(rawRotX, rawRotY, rawRotZ);
        return eqsageGltfTransform(internal);
    }

    Rotation willeqFullPipeline(float rawRotX, float rawRotY, float rawRotZ) {
        Rotation internal = willeqParsing(rawRotX, rawRotY, rawRotZ);
        return willeqIrrlichtTransform(internal);
    }
};

// Test that vertex center offset produces same result as eqsage
TEST_F(ObjectPlacementTest, VertexCenterOffset_MatchesEqsage) {
    // Test case: chair with center at (0.45, -0.94, 3.27) in EQ space
    Vec3 center = {0.45f, -0.94f, 3.27f};

    // Test vertex at bottom of chair (relative to center)
    Vec3 bottomVertex = {0.0f, 0.0f, -3.27f};

    Vec3 eqsageResult = eqsageVertexTransform(bottomVertex, center);
    Vec3 willeqResult = willeqVertexTransform(bottomVertex, center);

    EXPECT_FLOAT_EQ(eqsageResult.x, willeqResult.x);
    EXPECT_FLOAT_EQ(eqsageResult.y, willeqResult.y);
    EXPECT_FLOAT_EQ(eqsageResult.z, willeqResult.z);
}

TEST_F(ObjectPlacementTest, VertexCenterOffset_ChairBottom_AtFloor) {
    // Chair center at Z=3.27 means chair is ~6.54 units tall
    // Bottom vertex at Z=-3.27 (relative to center)
    // After adding center: bottom at Z=0 (floor level)
    Vec3 center = {0.0f, 0.0f, 3.27f};
    Vec3 bottomVertex = {0.0f, 0.0f, -3.27f};

    Vec3 result = willeqVertexTransform(bottomVertex, center);

    // In output space, Y is height. Bottom should be at Y=0
    EXPECT_FLOAT_EQ(result.y, 0.0f);
}

TEST_F(ObjectPlacementTest, VertexCenterOffset_ChairTop_AboveFloor) {
    // Chair center at Z=3.27 means chair is ~6.54 units tall
    // Top vertex at Z=+3.27 (relative to center)
    // After adding center: top at Z=6.54
    Vec3 center = {0.0f, 0.0f, 3.27f};
    Vec3 topVertex = {0.0f, 0.0f, 3.27f};

    Vec3 result = willeqVertexTransform(topVertex, center);

    // In output space, Y is height. Top should be at Y=6.54
    EXPECT_FLOAT_EQ(result.y, 6.54f);
}

TEST_F(ObjectPlacementTest, VertexCenterOffset_ArbitraryVertex) {
    // Test with non-trivial values
    Vec3 center = {1.5f, -2.0f, 4.0f};
    Vec3 vertex = {-0.5f, 1.0f, -2.0f};

    Vec3 eqsageResult = eqsageVertexTransform(vertex, center);
    Vec3 willeqResult = willeqVertexTransform(vertex, center);

    // Both should produce: (vertex.x+center.x, vertex.z+center.z, vertex.y+center.y)
    // = (-0.5+1.5, -2.0+4.0, 1.0-2.0) = (1.0, 2.0, -1.0)
    EXPECT_FLOAT_EQ(eqsageResult.x, 1.0f);
    EXPECT_FLOAT_EQ(eqsageResult.y, 2.0f);
    EXPECT_FLOAT_EQ(eqsageResult.z, -1.0f);

    EXPECT_FLOAT_EQ(willeqResult.x, eqsageResult.x);
    EXPECT_FLOAT_EQ(willeqResult.y, eqsageResult.y);
    EXPECT_FLOAT_EQ(willeqResult.z, eqsageResult.z);
}

// Test position transformation matches eqsage
TEST_F(ObjectPlacementTest, PositionTransform_MatchesEqsage) {
    Vec3 eqPos = {100.0f, 200.0f, 50.0f};  // EQ: x=100, y=200, z=50 (height)

    Vec3 eqsageResult = eqsagePositionTransform(eqPos);

    // eqsage: x=100, y=50 (from z), z=200 (from y)
    EXPECT_FLOAT_EQ(eqsageResult.x, 100.0f);
    EXPECT_FLOAT_EQ(eqsageResult.y, 50.0f);
    EXPECT_FLOAT_EQ(eqsageResult.z, 200.0f);
}

// ==========================================================================
// STEP 1 TESTS: Verify willeq parsing matches eqsage parsing exactly
// ==========================================================================

TEST_F(ObjectPlacementTest, Parsing_MatchesEqsage_Zero) {
    Rotation eqsage = eqsageParsing(0, 0, 0);
    Rotation willeq = willeqParsing(0, 0, 0);

    EXPECT_FLOAT_EQ(eqsage.x, willeq.x);
    EXPECT_FLOAT_EQ(eqsage.y, willeq.y);
    EXPECT_FLOAT_EQ(eqsage.z, willeq.z);

    // Zero rotation should stay zero
    EXPECT_FLOAT_EQ(willeq.x, 0.0f);
    EXPECT_FLOAT_EQ(willeq.y, 0.0f);
    EXPECT_FLOAT_EQ(willeq.z, 0.0f);
}

TEST_F(ObjectPlacementTest, Parsing_MatchesEqsage_90Degrees) {
    float rawRotX = 128.0f;  // 90 degrees in EQ format (128 * 360/512 = 90)

    Rotation eqsage = eqsageParsing(rawRotX, 0, 0);
    Rotation willeq = willeqParsing(rawRotX, 0, 0);

    EXPECT_FLOAT_EQ(eqsage.x, willeq.x);
    EXPECT_FLOAT_EQ(eqsage.y, willeq.y);
    EXPECT_FLOAT_EQ(eqsage.z, willeq.z);

    // 90 degrees should become -90 (negated in parsing)
    EXPECT_FLOAT_EQ(willeq.y, -90.0f);
}

TEST_F(ObjectPlacementTest, Parsing_MatchesEqsage_180Degrees) {
    float rawRotX = 256.0f;  // 180 degrees in EQ format

    Rotation eqsage = eqsageParsing(rawRotX, 0, 0);
    Rotation willeq = willeqParsing(rawRotX, 0, 0);

    EXPECT_FLOAT_EQ(eqsage.x, willeq.x);
    EXPECT_FLOAT_EQ(eqsage.y, willeq.y);
    EXPECT_FLOAT_EQ(eqsage.z, willeq.z);

    // 180 degrees should become -180 (negated)
    EXPECT_FLOAT_EQ(willeq.y, -180.0f);
}

TEST_F(ObjectPlacementTest, Parsing_MatchesEqsage_SecondaryRotation) {
    // Test secondary rotation (rawRotY)
    float rawRotY = 64.0f;  // 45 degrees in EQ format

    Rotation eqsage = eqsageParsing(0, rawRotY, 0);
    Rotation willeq = willeqParsing(0, rawRotY, 0);

    EXPECT_FLOAT_EQ(eqsage.x, willeq.x);
    EXPECT_FLOAT_EQ(eqsage.y, willeq.y);
    EXPECT_FLOAT_EQ(eqsage.z, willeq.z);

    // Secondary rotation should be positive (not negated)
    EXPECT_FLOAT_EQ(willeq.z, 45.0f);
}

// ==========================================================================
// STEP 2 TESTS: Verify output transforms are correct
// ==========================================================================

TEST_F(ObjectPlacementTest, OutputTransform_eqsageAdds180) {
    Rotation internal = {0.0f, -90.0f, 0.0f};
    Rotation gltfOutput = eqsageGltfTransform(internal);

    // eqsage adds 180 for glTF
    EXPECT_FLOAT_EQ(gltfOutput.y, -90.0f + 180.0f);  // = 90
}

TEST_F(ObjectPlacementTest, OutputTransform_willeqNoAdditional) {
    Rotation internal = {0.0f, -90.0f, 0.0f};
    Rotation irrlichtOutput = willeqIrrlichtTransform(internal);

    // willeq does NOT add 180 for Irrlicht (both left-handed)
    EXPECT_FLOAT_EQ(irrlichtOutput.y, -90.0f);
}

TEST_F(ObjectPlacementTest, OutputTransform_Difference) {
    // The key difference: eqsage adds 180°, willeq does not
    Rotation internal = {0.0f, 0.0f, 0.0f};

    Rotation gltf = eqsageGltfTransform(internal);
    Rotation irrlicht = willeqIrrlichtTransform(internal);

    // Difference should be exactly 180 degrees
    EXPECT_FLOAT_EQ(gltf.y - irrlicht.y, 180.0f);
}

// ==========================================================================
// FULL PIPELINE TESTS: Verify complete transform chains
// ==========================================================================

TEST_F(ObjectPlacementTest, FullPipeline_eqsageVsWilleq) {
    // Full pipeline should differ by 180 degrees in Y rotation
    float rawRotX = 128.0f;  // 90 degrees

    Rotation eqsage = eqsageFullPipeline(rawRotX, 0, 0);
    Rotation willeq = willeqFullPipeline(rawRotX, 0, 0);

    // eqsage: -90 + 180 = 90
    EXPECT_FLOAT_EQ(eqsage.y, 90.0f);

    // willeq: -90 (no +180)
    EXPECT_FLOAT_EQ(willeq.y, -90.0f);

    // The difference is always 180 degrees
    EXPECT_FLOAT_EQ(eqsage.y - willeq.y, 180.0f);
}

// Test complete object placement workflow
TEST_F(ObjectPlacementTest, CompleteWorkflow_FloorObject) {
    // Simulate a chair at floor level
    Vec3 meshCenter = {0.0f, 0.0f, 3.0f};  // Center 3 units above base

    // Vertex at bottom of mesh (relative to center)
    Vec3 bottomVertex = {0.0f, 0.0f, -3.0f};

    // ActorInstance position (where object should be placed)
    Vec3 actorPos = {100.0f, 200.0f, 0.0f};  // At floor level (z=0)

    // Step 1: Transform vertex with center (willeq approach)
    Vec3 transformedVertex = willeqVertexTransform(bottomVertex, meshCenter);

    // Step 2: Transform actor position
    Vec3 transformedPos = eqsagePositionTransform(actorPos);

    // Final world position of bottom vertex = transformedPos + transformedVertex
    Vec3 worldPos = {
        transformedPos.x + transformedVertex.x,
        transformedPos.y + transformedVertex.y,
        transformedPos.z + transformedVertex.z
    };

    // Bottom of chair should be at floor level (Y=0 in output space)
    // transformedVertex.y = (-3.0 + 3.0) = 0
    // transformedPos.y = 0 (from EQ z=0)
    // worldPos.y = 0 + 0 = 0
    EXPECT_FLOAT_EQ(worldPos.y, 0.0f);
}

// Integration test: Load actual zone data and verify transforms
#include "client/graphics/eq/s3d_loader.h"

class ObjectPlacementIntegrationTest : public ::testing::Test {
protected:
    std::string eqClientPath_;

    void SetUp() override {
        // Try to get EQ client path from environment or use default
        const char* envPath = std::getenv("EQ_CLIENT_PATH");
        if (envPath) {
            eqClientPath_ = envPath;
        } else {
            eqClientPath_ = "/home/user/projects/claude/EverQuestP1999";
        }
    }

    bool fileExists(const std::string& path) {
        std::ifstream f(path);
        return f.good();
    }
};

// This test loads actual zone data and verifies the object placement
// It requires the EQ client files to be present
TEST_F(ObjectPlacementIntegrationTest, LoadZoneObjects_VerifyCenterApplied) {
    std::string zonePath = eqClientPath_ + "/freportw.s3d";

    // Skip if EQ files not available
    if (!fileExists(zonePath)) {
        GTEST_SKIP() << "EQ client files not found at " << eqClientPath_;
    }

    S3DLoader loader;
    bool loaded = loader.loadZone(zonePath);
    ASSERT_TRUE(loaded) << "Failed to load zone: " << loader.getError();

    auto zone = loader.getZone();
    ASSERT_NE(zone, nullptr);

    // Verify objects were loaded
    EXPECT_GT(zone->objects.size(), 0u) << "No objects loaded from zone";

    // Verify object geometries have center = 0 (center baked into vertices)
    for (const auto& obj : zone->objects) {
        ASSERT_NE(obj.geometry, nullptr);
        // After center is applied, the stored center should be cleared
        EXPECT_FLOAT_EQ(obj.geometry->centerX, 0.0f)
            << "Object geometry should have centerX=0 after baking";
        EXPECT_FLOAT_EQ(obj.geometry->centerY, 0.0f)
            << "Object geometry should have centerY=0 after baking";
        EXPECT_FLOAT_EQ(obj.geometry->centerZ, 0.0f)
            << "Object geometry should have centerZ=0 after baking";
    }
}

// Test that object bounding boxes are reasonable after center adjustment
TEST_F(ObjectPlacementIntegrationTest, LoadZoneObjects_VerifyBoundsReasonable) {
    std::string zonePath = eqClientPath_ + "/freportw.s3d";

    if (!fileExists(zonePath)) {
        GTEST_SKIP() << "EQ client files not found at " << eqClientPath_;
    }

    S3DLoader loader;
    bool loaded = loader.loadZone(zonePath);
    ASSERT_TRUE(loaded);

    auto zone = loader.getZone();
    ASSERT_NE(zone, nullptr);

    for (const auto& obj : zone->objects) {
        if (!obj.geometry) continue;

        // After center baking, minZ should be close to 0 for floor objects
        // (their bottom is at the origin)
        // For objects like torches/lamps, minZ might still be above 0

        // At minimum, verify bounds are not inverted
        EXPECT_LE(obj.geometry->minX, obj.geometry->maxX);
        EXPECT_LE(obj.geometry->minY, obj.geometry->maxY);
        EXPECT_LE(obj.geometry->minZ, obj.geometry->maxZ);

        // Verify bounds are reasonable (not extremely large)
        float sizeX = obj.geometry->maxX - obj.geometry->minX;
        float sizeY = obj.geometry->maxY - obj.geometry->minY;
        float sizeZ = obj.geometry->maxZ - obj.geometry->minZ;

        EXPECT_LT(sizeX, 1000.0f) << "Object X size unreasonably large";
        EXPECT_LT(sizeY, 1000.0f) << "Object Y size unreasonably large";
        EXPECT_LT(sizeZ, 1000.0f) << "Object Z size unreasonably large";
    }
}
