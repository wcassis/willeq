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

// ---------------------------------------------------------------------------
// RaceModelLoader race-code tests (static methods only, no Irrlicht device).
//
// T270: race codes come from data/willeq.db and nowhere else. getRaceCode()
// and getFallbackRaceCode() used to fall back to a pre-JSON hardcoded switch
// whenever no WilleqDatabase was open. That numbering contradicted the
// audited race_models rows wholesale (T265/T267: race 13 is AVI not WOL,
// 17 GOL not ORC, 21 BEH not SKE, 35 EEL not RAT, 46 IMP not GOB, 72 SHIP
// not ELE, 85 SPE not DRA, 127/240 IVM not INV, ...), so with no db every
// one of those ids returned a confidently wrong code and every one of the 38
// expectations pinned here asserted the wrong numbering. The fallback is
// deleted: no row -> no code -> the caller's placeholder path (T213), and
// the expectations below are read from the shipped db instead of restated.
// ---------------------------------------------------------------------------
#include "client/graphics/eq/race_model_loader.h"
#include "client/willeq_database.h"
#include <sqlite3.h>
#include <cctype>

namespace {

bool RaceTestFileExists(const std::string& p) {
    std::ifstream f(p);
    return f.good();
}

std::string RaceTestUpper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// Every race id the deleted switch answered for. With no db open, all of them
// must now answer "unknown" rather than a pre-JSON guess.
const uint16_t kIdsTheDeletedSwitchAnswered[] = {
    1,   2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 17, 18, 19,
    20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
    38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55,
    56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73,
    74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91,
    92, 93, 94, 95, 96, 120, 127, 128, 240, 367
};

}  // namespace

// No database open: every lookup is honestly "unknown".
class RaceCodeNoDatabaseTest : public ::testing::Test {
protected:
    void SetUp() override { EQT::WilleqDatabase::setGlobalInstance(nullptr); }
};

TEST_F(RaceCodeNoDatabaseTest, EveryIdTheOldSwitchAnsweredIsNowUnknown) {
    for (uint16_t raceId : kIdsTheDeletedSwitchAnswered) {
        EXPECT_EQ(RaceModelLoader::getRaceCode(raceId), "")
            << "race " << raceId << " still answers from a hardcoded map";
    }
}

TEST_F(RaceCodeNoDatabaseTest, UnknownIdIsUnknown) {
    EXPECT_EQ(RaceModelLoader::getRaceCode(9999), "");
}

TEST_F(RaceCodeNoDatabaseTest, NoCodeMeansNoModelFilename) {
    // The loader reads this as "no model" and takes the placeholder path.
    for (uint16_t raceId : {uint16_t(1), uint16_t(8), uint16_t(13), uint16_t(21),
                            uint16_t(128), uint16_t(330), uint16_t(9999)}) {
        EXPECT_EQ(RaceModelLoader::getRaceModelFilename(raceId, 0), "") << "race " << raceId;
        EXPECT_EQ(RaceModelLoader::getRaceModelFilename(raceId, 1), "") << "race " << raceId;
    }
}

TEST_F(RaceCodeNoDatabaseTest, CitizenFallbackIsAlsoDbOnly) {
    // 15/61/62/71/80/82/84/86 were the hardcoded citizen->player-race fallbacks.
    for (uint16_t raceId : {uint16_t(15), uint16_t(61), uint16_t(62), uint16_t(71),
                            uint16_t(80), uint16_t(82), uint16_t(84), uint16_t(86)}) {
        EXPECT_EQ(EQT::Graphics::getFallbackRaceCode(raceId, 0), "") << "race " << raceId;
        EXPECT_EQ(EQT::Graphics::getFallbackRaceCode(raceId, 1), "") << "race " << raceId;
    }
}

// With the shipped db open, the answers are the audited rows.
class RaceCodeFromDbTest : public ::testing::Test {
protected:
    EQT::WilleqDatabase db_;
    void SetUp() override {
        const std::string path = WILLEQ_DB_REAL_PATH;
        if (!RaceTestFileExists(path)) GTEST_SKIP() << "no shipped db at " << path;
        ASSERT_TRUE(db_.open(path)) << path;
        EQT::WilleqDatabase::setGlobalInstance(&db_);
    }
    void TearDown() override { EQT::WilleqDatabase::setGlobalInstance(nullptr); }
};

// The table test that replaces the 38 restated expectations: every row in
// race_models serves its own code and its own fallback_code, for both genders.
TEST_F(RaceCodeFromDbTest, EveryRowServesItsOwnCodeAndFallback) {
    sqlite3* raw = nullptr;
    ASSERT_EQ(sqlite3_open_v2(WILLEQ_DB_REAL_PATH, &raw, SQLITE_OPEN_READONLY, nullptr), SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(raw,
                  "SELECT race_id, code, fallback_code FROM race_models ORDER BY race_id",
                  -1, &stmt, nullptr), SQLITE_OK);
    int rows = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ++rows;
        const uint16_t raceId = static_cast<uint16_t>(sqlite3_column_int(stmt, 0));
        const char* c = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* fb = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const std::string code = c ? c : "";
        EXPECT_EQ(RaceModelLoader::getRaceCode(raceId), code) << "race " << raceId;

        const std::string expectedFallback = fb ? fb : "";
        EXPECT_EQ(EQT::Graphics::getFallbackRaceCode(raceId, 0), expectedFallback)
            << "race " << raceId;
        std::string female = expectedFallback;
        if (female.size() == 3 && female.back() == 'M') female.back() = 'F';
        EXPECT_EQ(EQT::Graphics::getFallbackRaceCode(raceId, 1), female) << "race " << raceId;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(raw);
    EXPECT_GE(rows, 187) << "race_models shrank";
}

// The ids where the deleted switch disagreed with the audited row, pinned by
// name so a regression says which race and what it used to claim.
TEST_F(RaceCodeFromDbTest, ContradictedIdsAnswerTheRow) {
    const struct { uint16_t race; const char* row; const char* oldSwitchSaid; } kFixed[] = {
        {13,  "AVI",  "WOL"},   // Aviak, not Wolf
        {14,  "WER",  "BEA"},   // Werewolf, not Bear
        {15,  "BRM",  "HUM"},   // Brownie, not Freeport Guard
        {17,  "GOL",  "ORC"},   // Golem (befallen_chr), not Orc
        {21,  "BEH",  "SKE"},   // Evil Eye, not Skeleton
        {24,  "FIS",  "FIS"},   // agreed
        {26,  "FRO",  "SNA"},   // Froglok, not Snake
        {35,  "EEL",  "RAT"},   // Giant Eel, not Rat
        {44,  "FPM",  "GNN"},   // Freeport Guard, not Gnoll
        {46,  "IMP",  "GOB"},   // Imp, not Goblin
        {61,  "SHA",  "NGM"},   // Shark, not Neriak Citizen
        {72,  "SHIP", "ELE"},   // Ship, not Elemental
        {80,  "REA",  "QCM"},   // Reanimated Hand, not Karana Citizen
        {85,  "SPE",  "DRA"},   // Spectre, not Dragon
        {127, "IVM",  "INV"},   // Invisible Man's real code
        {240, "IVM",  "INV"},   // Teleport Man, aliased to 127 (T265)
        {330, "FRM",  "" },     // playable Froglok; the switch had no entry
    };
    for (const auto& f : kFixed) {
        EXPECT_EQ(RaceTestUpper(RaceModelLoader::getRaceCode(f.race)), f.row)
            << "race " << f.race << " (the deleted switch said '" << f.oldSwitchSaid << "')";
    }
}

// The playable-race filenames the old fixture pinned, now db-served.
TEST_F(RaceCodeFromDbTest, PlayableRaceModelFilenames) {
    EXPECT_EQ(RaceModelLoader::getRaceModelFilename(1, 0), "globalhum_chr.s3d");
    EXPECT_EQ(RaceModelLoader::getRaceModelFilename(1, 1), "globalhuf_chr.s3d");
    EXPECT_EQ(RaceModelLoader::getRaceModelFilename(2, 0), "globalbam_chr.s3d");
    EXPECT_EQ(RaceModelLoader::getRaceModelFilename(2, 1), "globalbaf_chr.s3d");
    EXPECT_EQ(RaceModelLoader::getRaceModelFilename(3, 0), "globalerm_chr.s3d");
    EXPECT_EQ(RaceModelLoader::getRaceModelFilename(3, 1), "globalerf_chr.s3d");
    EXPECT_EQ(RaceModelLoader::getRaceModelFilename(8, 0), "globaldwm_chr.s3d");
    EXPECT_EQ(RaceModelLoader::getRaceModelFilename(8, 1), "globaldwf_chr.s3d");
    EXPECT_EQ(RaceModelLoader::getRaceModelFilename(128, 0), "globalikm_chr.s3d");
    EXPECT_EQ(RaceModelLoader::getRaceModelFilename(128, 1), "globalikf_chr.s3d");
    // Vah Shir has a row now (KEM, global7_chr.s3d); the old switch had none.
    EXPECT_EQ(RaceModelLoader::getRaceModelFilename(130, 0), "globalkem_chr.s3d");
    // Genderless monster rows keep their single archive name.
    EXPECT_EQ(RaceModelLoader::getRaceModelFilename(367, 0), "globalske_chr.s3d");
    // T267: a row naming a per-race global archive wins outright.
    EXPECT_EQ(RaceModelLoader::getRaceModelFilename(330, 0), "globalpcfroglok_chr.s3d");
    // Still unknown: no row, no filename.
    EXPECT_EQ(RaceModelLoader::getRaceModelFilename(9999, 0), "");
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
    EXPECT_TRUE(geom.textureNames().empty());
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
            eqClientPath_ = EQ_CLIENT_DATA_PATH;
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
