// Unit tests for graphics components
// Note: These tests do not require an active display

#include <gtest/gtest.h>
#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/dds_decoder.h"
#include <vector>
#include <cstring>

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
    EXPECT_EQ(sizeof(WldFragmentHeader), 12u);
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
    EXPECT_EQ(sizeof(WldFragment12Header), 16u);
}

// Test WLD light source definition header size
TEST_F(WldLoaderTest, WldFragment1BHeaderSize) {
    EXPECT_EQ(sizeof(WldFragment1BHeader), 20u);
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
    EXPECT_EQ(RaceModelLoader::getRaceCode(130), "KEM");
}

TEST_F(RaceModelLoaderTest, GetRaceCode_Wolf) {
    EXPECT_EQ(RaceModelLoader::getRaceCode(13), "WOL");
}

TEST_F(RaceModelLoaderTest, GetRaceCode_Goblin) {
    EXPECT_EQ(RaceModelLoader::getRaceCode(46), "GOB");
}

TEST_F(RaceModelLoaderTest, GetRaceCode_Dragon) {
    EXPECT_EQ(RaceModelLoader::getRaceCode(85), "DRG");
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
